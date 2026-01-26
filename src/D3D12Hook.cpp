#include "D3D12Hook.hpp"
#include "PatternScanner.hpp"
#include "VRSystem.hpp"
#include "Utils.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <RED4ext/RED4ext.hpp>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// External references
extern RED4ext::PluginHandle g_pluginHandle;
extern const RED4ext::Sdk* g_sdk;
extern std::unique_ptr<VRSystem> g_vrSystem;

namespace D3D12Hook
{
    // Captured resources
    static ID3D12CommandQueue* s_commandQueue = nullptr;
    static ID3D12Resource* s_backBuffer = nullptr;
    static IDXGISwapChain* s_swapChain = nullptr;
    static ID3D12Device* s_device = nullptr;

    // State tracking
    static bool s_initialized = false;
    static bool s_resourcesCaptured = false;
    static OnReadyCallback s_onReadyCallback = nullptr;

    // Original function pointer (trampoline)
    static HRESULT(STDMETHODCALLTYPE* Real_Present)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) = nullptr;

    // Frame counter for alternating eye rendering
    static uint64_t s_frameCount = 0;

    // Our hook function
    static HRESULT STDMETHODCALLTYPE Hook_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
    {
        // First time capture
        if (!s_resourcesCaptured && pSwapChain)
        {
            s_swapChain = pSwapChain;

            // Get the D3D12 device from swapchain
            IDXGISwapChain3* swapChain3 = nullptr;
            if (SUCCEEDED(pSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))))
            {
                ID3D12Device* device = nullptr;
                if (SUCCEEDED(swapChain3->GetDevice(IID_PPV_ARGS(&device))))
                {
                    s_device = device;

                    // Get the current back buffer
                    UINT bufferIndex = swapChain3->GetCurrentBackBufferIndex();
                    if (SUCCEEDED(swapChain3->GetBuffer(bufferIndex, IID_PPV_ARGS(&s_backBuffer))))
                    {
                        // Now we need to find/create a command queue
                        // Option 1: Hook CreateCommandQueue (complex)
                        // Option 2: Create our own queue for VR submission

                        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
                        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
                        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
                        queueDesc.NodeMask = 0;

                        if (SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&s_commandQueue))))
                        {
                            s_resourcesCaptured = true;
                            Utils::LogInfo("D3D12Hook: Resources captured successfully!");

                            char msg[128];
                            snprintf(msg, sizeof(msg), "D3D12Hook: Device=0x%p Queue=0x%p",
                                     s_device, s_commandQueue);
                            Utils::LogInfo(msg);

                            // Initialize VR system with the command queue
                            if (g_vrSystem)
                            {
                                g_vrSystem->Initialize(s_commandQueue);
                            }

                            // Notify callback
                            if (s_onReadyCallback)
                            {
                                s_onReadyCallback(s_commandQueue, s_swapChain);
                            }
                        }
                        else
                        {
                            Utils::LogError("D3D12Hook: Failed to create command queue");
                        }
                    }
                    else
                    {
                        Utils::LogError("D3D12Hook: Failed to get back buffer");
                    }
                }
                else
                {
                    Utils::LogError("D3D12Hook: Failed to get D3D12 device");
                }
                swapChain3->Release();
            }
        }

        // VR Frame Submission
        if (s_resourcesCaptured && g_vrSystem)
        {
            // Update back buffer reference each frame (it rotates)
            IDXGISwapChain3* swapChain3 = nullptr;
            if (SUCCEEDED(pSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))))
            {
                ID3D12Resource* currentBackBuffer = nullptr;
                UINT bufferIndex = swapChain3->GetCurrentBackBufferIndex();

                if (SUCCEEDED(swapChain3->GetBuffer(bufferIndex, IID_PPV_ARGS(&currentBackBuffer))))
                {
                    // Alternate eye rendering
                    bool isLeftEye = (s_frameCount % 2) == 0;
                    g_vrSystem->SubmitFrame(currentBackBuffer, isLeftEye);
                    currentBackBuffer->Release();
                }
                swapChain3->Release();
            }
            s_frameCount++;
        }

        // Call original Present
        return Real_Present(pSwapChain, SyncInterval, Flags);
    }

    bool Initialize()
    {
        if (s_initialized)
        {
            return true;
        }

        Utils::LogInfo("D3D12Hook: Initializing...");

        // Method: Hook via vtable from a temporary swapchain
        // Create a dummy D3D12 device and swapchain to get the vtable

        IDXGIFactory4* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        {
            Utils::LogError("D3D12Hook: Failed to create DXGI factory");
            return false;
        }

        // Find hardware adapter
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                break;
            }
            adapter->Release();
            adapter = nullptr;
        }

        if (!adapter)
        {
            Utils::LogError("D3D12Hook: No hardware adapter found");
            factory->Release();
            return false;
        }

        // Create temporary D3D12 device
        ID3D12Device* tempDevice = nullptr;
        if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&tempDevice))))
        {
            Utils::LogError("D3D12Hook: Failed to create temp D3D12 device");
            adapter->Release();
            factory->Release();
            return false;
        }

        // Create temporary command queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        ID3D12CommandQueue* tempQueue = nullptr;
        if (FAILED(tempDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&tempQueue))))
        {
            Utils::LogError("D3D12Hook: Failed to create temp command queue");
            tempDevice->Release();
            adapter->Release();
            factory->Release();
            return false;
        }

        // Create dummy window for swapchain
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CyberpunkVR_DummyWindow";
        RegisterClassExW(&wc);

        HWND tempWindow = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                           0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

        // Create temporary swapchain
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = 100;
        swapChainDesc.Height = 100;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        IDXGISwapChain1* tempSwapChain = nullptr;
        if (FAILED(factory->CreateSwapChainForHwnd(tempQueue, tempWindow, &swapChainDesc,
                                                    nullptr, nullptr, &tempSwapChain)))
        {
            Utils::LogError("D3D12Hook: Failed to create temp swapchain");
            DestroyWindow(tempWindow);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            tempQueue->Release();
            tempDevice->Release();
            adapter->Release();
            factory->Release();
            return false;
        }

        // Get vtable pointer for Present
        // IDXGISwapChain vtable layout: QueryInterface, AddRef, Release, ..., Present (index 8)
        void** vtable = *reinterpret_cast<void***>(tempSwapChain);
        void* presentAddr = vtable[8]; // Present is at index 8

        char msg[128];
        snprintf(msg, sizeof(msg), "D3D12Hook: Present vtable address: 0x%p", presentAddr);
        Utils::LogInfo(msg);

        // Install hook using RED4ext
        bool success = g_sdk->hooking->Attach(
            g_pluginHandle,
            presentAddr,
            reinterpret_cast<void*>(&Hook_Present),
            reinterpret_cast<void**>(&Real_Present)
        );

        // Cleanup temporary resources
        tempSwapChain->Release();
        DestroyWindow(tempWindow);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        tempQueue->Release();
        tempDevice->Release();
        adapter->Release();
        factory->Release();

        if (success)
        {
            s_initialized = true;
            Utils::LogInfo("D3D12Hook: Present hook installed successfully!");
            return true;
        }
        else
        {
            Utils::LogError("D3D12Hook: Failed to install Present hook");
            return false;
        }
    }

    void Shutdown()
    {
        if (!s_initialized)
        {
            return;
        }

        Utils::LogInfo("D3D12Hook: Shutting down...");

        // Detach hook
        if (Real_Present)
        {
            // Note: RED4ext doesn't have a public Detach, hooks are auto-removed on unload
        }

        // Release captured resources
        if (s_commandQueue)
        {
            s_commandQueue->Release();
            s_commandQueue = nullptr;
        }
        if (s_backBuffer)
        {
            s_backBuffer->Release();
            s_backBuffer = nullptr;
        }

        s_swapChain = nullptr;
        s_device = nullptr;
        s_resourcesCaptured = false;
        s_initialized = false;

        Utils::LogInfo("D3D12Hook: Shutdown complete");
    }

    ID3D12CommandQueue* GetCommandQueue()
    {
        return s_commandQueue;
    }

    ID3D12Resource* GetBackBuffer()
    {
        return s_backBuffer;
    }

    IDXGISwapChain* GetSwapChain()
    {
        return s_swapChain;
    }

    bool IsReady()
    {
        return s_resourcesCaptured;
    }

    void SetOnReadyCallback(OnReadyCallback callback)
    {
        s_onReadyCallback = callback;
    }
}
