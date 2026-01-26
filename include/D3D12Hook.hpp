#pragma once

#include <cstdint>

// Forward declarations to avoid header pollution
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct IDXGISwapChain;

namespace D3D12Hook
{
    // Initialize the D3D12 hooks
    // Must be called after RED4ext SDK is available
    bool Initialize();

    // Shutdown and remove hooks
    void Shutdown();

    // Get the captured game resources (will be nullptr until first Present)
    ID3D12CommandQueue* GetCommandQueue();
    ID3D12Resource* GetBackBuffer();
    IDXGISwapChain* GetSwapChain();

    // Check if we've captured the resources
    bool IsReady();

    // Callback type for when D3D12 resources become available
    using OnReadyCallback = void(*)(ID3D12CommandQueue* queue, IDXGISwapChain* swapChain);

    // Register a callback for when D3D12 resources are captured
    void SetOnReadyCallback(OnReadyCallback callback);
}
