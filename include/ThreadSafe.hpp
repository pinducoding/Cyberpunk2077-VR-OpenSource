#pragma once

#include <atomic>
#include <mutex>
#include <wrl/client.h>  // For Microsoft::WRL::ComPtr

// Thread-safe wrapper for shared state
namespace ThreadSafe
{
    // Atomic flag for simple boolean state
    using Flag = std::atomic<bool>;

    // Atomic counter for frame numbers
    using Counter = std::atomic<uint64_t>;

    // Scoped lock helper
    using Lock = std::lock_guard<std::mutex>;
    using UniqueLock = std::unique_lock<std::mutex>;

    // Recursive mutex for nested locks
    using RecursiveMutex = std::recursive_mutex;
    using RecursiveLock = std::lock_guard<std::recursive_mutex>;
}

// COM smart pointer alias
template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// Configuration constants (will be replaced by settings system)
namespace VRConfig
{
    // IPD in meters (default 64mm)
    inline std::atomic<float> g_ipd{0.064f};

    // World scale multiplier
    inline std::atomic<float> g_worldScale{1.0f};

    // Enable VR rendering
    inline std::atomic<bool> g_vrEnabled{true};

    // GPU wait timeout in milliseconds (0 = infinite)
    inline std::atomic<DWORD> g_gpuWaitTimeout{5000};

    // Setters (thread-safe)
    inline void SetIPD(float ipdMeters) { g_ipd.store(ipdMeters); }
    inline void SetWorldScale(float scale) { g_worldScale.store(scale); }
    inline void SetVREnabled(bool enabled) { g_vrEnabled.store(enabled); }
    inline void SetGPUWaitTimeout(DWORD ms) { g_gpuWaitTimeout.store(ms); }

    // Getters (thread-safe)
    inline float GetIPD() { return g_ipd.load(); }
    inline float GetWorldScale() { return g_worldScale.load(); }
    inline bool IsVREnabled() { return g_vrEnabled.load(); }
    inline DWORD GetGPUWaitTimeout() { return g_gpuWaitTimeout.load(); }
}
