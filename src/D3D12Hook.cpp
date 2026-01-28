#include "D3D12Hook.hpp"
#include "PatternScanner.hpp"
#include "VRSystem.hpp"
#include "ThreadSafe.hpp"
#include "Utils.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
    // Thread-safe state using atomics and mutex
    static std::mutex s_stateMutex;
    static ComPtr<ID3D12CommandQueue> s_commandQueue;
    static ComPtr<IDXGISwapChain> s_swapChain;
    static ComPtr<ID3D12Device> s_device;

    // Atomic flags for lock-free checks
    static ThreadSafe::Flag s_initialized{false};
    static ThreadSafe::Flag s_resourcesCaptured{false};
    static ThreadSafe::Flag s_shutdownRequested{false};

    // Frame counter (atomic for thread safety)
    static ThreadSafe::Counter s_frameCount{0};

    // Original function pointer (trampoline)
    static HRESULT(STDMETHODCALLTYPE* Real_Present)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) = nullptr;

    // Callback
    static OnReadyCallback s_onReadyCallback = nullptr;

    // Our hook function
    static HRESULT STDMETHODCALLTYPE Hook_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
    {
        // Early exit if shutdown requested or VR disabled
        if (s_shutdownRequested.load() || !VRConfig::IsVREnabled()) {
            return Real_Present ? Real_Present(pSwapChain, SyncInterval, Flags) : E_FAIL;
        }

        // Null check on swapchain
        if (!pSwapChain) {
            Utils::LogWarn("D3D12Hook: Present called with null swapchain");
            return Real_Present ? Real_Present(pSwapChain, SyncInterval, Flags) : E_FAIL;
        }

        // First time capture (thread-safe)
        if (!s_resourcesCaptured.load())
        {
            ThreadSafe::Lock lock(s_stateMutex);

            // Double-check after acquiring lock
            if (!s_resourcesCaptured.load())
            {
                ComPtr<IDXGISwapChain3> swapChain3;
                if (SUCCEEDED(pSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))))
                {
                    ComPtr<ID3D12Device> device;
                    if (SUCCEEDED(swapChain3->GetDevice(IID_PPV_ARGS(&device))))
                    {
                        // Create our own command queue for VR submission
                        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
                        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
                        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
                        queueDesc.NodeMask = 0;

                        ComPtr<ID3D12CommandQueue> commandQueue;
                        if (SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue))))
                        {
                            // Store references (ComPtr handles AddRef)
                            s_device = device;
                            s_commandQueue = commandQueue;
                            s_swapChain = pSwapChain;

                            s_resourcesCaptured.store(true);
                            Utils::LogInfo("D3D12Hook: Resources captured successfully!");

                            char msg[128];
                            snprintf(msg, sizeof(msg), "D3D12Hook: Device=0x%p Queue=0x%p",
                                     s_device.Get(), s_commandQueue.Get());
                            Utils::LogInfo(msg);

                            // Initialize VR system with the command queue (thread-safe)
                            if (g_vrSystem)
                            {
                                g_vrSystem->Initialize(s_commandQueue.Get());
                            }

                            // Notify callback
                            if (s_onReadyCallback)
                            {
                                s_onReadyCallback(s_commandQueue.Get(), s_swapChain.Get());
                            }
                        }
                        else
                        {
                            Utils::LogError("D3D12Hook: Failed to create command queue");
                        }
                    }
                    else
                    {
                        Utils::LogError("D3D12Hook: Failed to get D3D12 device");
                    }
                }
            }
        }

        // VR Frame Submission (only if resources captured and VR system ready)
        if (s_resourcesCaptured.load() && g_vrSystem && VRConfig::IsVREnabled())
        {
            ComPtr<IDXGISwapChain3> swapChain3;
            if (SUCCEEDED(pSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))))
            {
                UINT bufferIndex = swapChain3->GetCurrentBackBufferIndex();

                ComPtr<ID3D12Resource> currentBackBuffer;
                if (SUCCEEDED(swapChain3->GetBuffer(bufferIndex, IID_PPV_ARGS(&currentBackBuffer))))
                {
                    // Alternate eye rendering (atomic increment)
                    uint64_t frame = s_frameCount.fetch_add(1);
                    bool isLeftEye = (frame % 2) == 0;

                    g_vrSystem->SubmitFrame(currentBackBuffer.Get(), isLeftEye);
                    // ComPtr automatically releases currentBackBuffer
                }
            }
        }

        // Call original Present
        return Real_Present ? Real_Present(pSwapChain, SyncInterval, Flags) : E_FAIL;
    }

    bool Initialize()
    {
        if (s_initialized.load())
        {
            return true;
        }

        Utils::LogInfo("D3D12Hook: Initializing...");

        // Validate RED4ext SDK
        if (!g_sdk || !g_sdk->hooking)
        {
            Utils::LogError("D3D12Hook: RED4ext SDK not available");
            return false;
        }

        // Create temporary D3D12 resources to get vtable
        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        {
            Utils::LogError("D3D12Hook: Failed to create DXGI factory");
            return false;
        }

        // Find hardware adapter
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                break;
            }
            adapter.Reset();
        }

        if (!adapter)
        {
            Utils::LogError("D3D12Hook: No hardware adapter found");
            return false;
        }

        // Create temporary D3D12 device
        ComPtr<ID3D12Device> tempDevice;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&tempDevice))))
        {
            Utils::LogError("D3D12Hook: Failed to create temp D3D12 device");
            return false;
        }

        // Create temporary command queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        ComPtr<ID3D12CommandQueue> tempQueue;
        if (FAILED(tempDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&tempQueue))))
        {
            Utils::LogError("D3D12Hook: Failed to create temp command queue");
            return false;
        }

        // Create dummy window for swapchain
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CyberpunkVR_DummyWindow";

        if (!RegisterClassExW(&wc))
        {
            // Class might already exist, continue anyway
        }

        HWND tempWindow = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                           0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

        if (!tempWindow)
        {
            Utils::LogError("D3D12Hook: Failed to create temp window");
            return false;
        }

        // Create temporary swapchain
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = 100;
        swapChainDesc.Height = 100;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        ComPtr<IDXGISwapChain1> tempSwapChain;
        HRESULT hr = factory->CreateSwapChainForHwnd(tempQueue.Get(), tempWindow, &swapChainDesc,
                                                      nullptr, nullptr, &tempSwapChain);

        if (FAILED(hr))
        {
            Utils::LogError("D3D12Hook: Failed to create temp swapchain");
            DestroyWindow(tempWindow);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        // Get vtable pointer for Present
        // IDXGISwapChain vtable layout: QueryInterface(0), AddRef(1), Release(2), ..., Present(8)
        constexpr int PRESENT_VTABLE_INDEX = 8;
        void** vtable = *reinterpret_cast<void***>(tempSwapChain.Get());
        void* presentAddr = vtable[PRESENT_VTABLE_INDEX];

        char msg[128];
        snprintf(msg, sizeof(msg), "D3D12Hook: Present vtable address: 0x%p", presentAddr);
        Utils::LogInfo(msg);

        // Cleanup temporary resources before hooking
        tempSwapChain.Reset();
        DestroyWindow(tempWindow);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        tempQueue.Reset();
        tempDevice.Reset();

        // Install hook using RED4ext
        bool success = g_sdk->hooking->Attach(
            g_pluginHandle,
            presentAddr,
            reinterpret_cast<void*>(&Hook_Present),
            reinterpret_cast<void**>(&Real_Present)
        );

        if (success)
        {
            s_initialized.store(true);
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
        if (!s_initialized.load())
        {
            return;
        }

        Utils::LogInfo("D3D12Hook: Shutting down...");

        // Signal shutdown to hook
        s_shutdownRequested.store(true);

        // Wait a frame to ensure hook isn't in use
        Sleep(50);

        // Thread-safe cleanup
        {
            ThreadSafe::Lock lock(s_stateMutex);

            // ComPtr handles Release automatically
            s_commandQueue.Reset();
            s_swapChain.Reset();
            s_device.Reset();

            s_resourcesCaptured.store(false);
        }

        s_initialized.store(false);
        Utils::LogInfo("D3D12Hook: Shutdown complete");
    }

    ID3D12CommandQueue* GetCommandQueue()
    {
        return s_commandQueue.Get();
    }

    ID3D12Resource* GetBackBuffer()
    {
        // Back buffer is now fetched fresh each frame, not stored
        return nullptr;
    }

    IDXGISwapChain* GetSwapChain()
    {
        return s_swapChain.Get();
    }

    bool IsReady()
    {
        return s_resourcesCaptured.load();
    }

    void SetOnReadyCallback(OnReadyCallback callback)
    {
        s_onReadyCallback = callback;
    }
}
