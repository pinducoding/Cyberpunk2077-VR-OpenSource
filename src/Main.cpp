#include <RED4ext/RED4ext.hpp>
#include "VRSystem.hpp"
#include "CameraHook.hpp"
#include "InputHook.hpp"
#include "D3D12Hook.hpp"
#include "VRSettings.hpp"
#include "Utils.hpp"

// Global Systems
std::unique_ptr<VRSystem> g_vrSystem;
std::unique_ptr<CameraHook> g_cameraHook;

// Global Plugin Handles
RED4ext::PluginHandle g_pluginHandle = nullptr;
const RED4ext::Sdk* g_sdk = nullptr;

// Utils Implementation
namespace Utils
{
    void LogInfo(const char* msg) {
        if (g_sdk && g_sdk->logger) {
            g_sdk->logger->Info(g_pluginHandle, msg);
        }
    }
    void LogError(const char* msg) {
        if (g_sdk && g_sdk->logger) {
            g_sdk->logger->Error(g_pluginHandle, msg);
        }
    }
    void LogWarn(const char* msg) {
        if (g_sdk && g_sdk->logger) {
            g_sdk->logger->Warn(g_pluginHandle, msg);
        }
    }
}

// RED4ext Plugin Entry Point
RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::PluginHandle aHandle, RED4ext::EMainReason aReason, const RED4ext::Sdk* aSdk)
{
    switch (aReason)
    {
    case RED4ext::EMainReason::Load:
    {
        g_pluginHandle = aHandle;
        g_sdk = aSdk;

        // 1. Initialize Logging
        Utils::LogInfo("Initializing VR Mod...");

        // 2. Initialize VR System (OpenXR)
        g_vrSystem = std::make_unique<VRSystem>();
        // Note: passing nullptr for queue now, will hook later
        if (!g_vrSystem->Initialize(nullptr)) {
            Utils::LogError("Failed to initialize OpenXR!");
            return false;
        }

        // 3. Initialize D3D12 Hooks (captures command queue for VR)
        if (!D3D12Hook::Initialize()) {
            Utils::LogError("Failed to install D3D12 hooks!");
            return false;
        }

        // 4. Initialize Camera Hooks
        g_cameraHook = std::make_unique<CameraHook>();
        if (!g_cameraHook->InstallHooks()) {
            Utils::LogError("Failed to install camera hooks!");
            return false;
        }

        // 5. Initialize Input Hooks
        if (!InputHook::Initialize()) {
            Utils::LogWarn("Failed to install Input hooks (Controller support may be limited)");
        }

        // 6. Register Native Functions for CET Settings UI
        VRSettings::RegisterNativeFunctions(g_sdk, g_pluginHandle);

        Utils::LogInfo("CyberpunkVR: All systems initialized!");
        break;
    }
    case RED4ext::EMainReason::Unload:
    {
        // Cleanup in reverse order
        Utils::LogInfo("Unloading VR Mod...");

        VRSettings::UnregisterNativeFunctions(g_sdk, g_pluginHandle);
        InputHook::Shutdown();
        g_cameraHook.reset();
        D3D12Hook::Shutdown();
        g_vrSystem.reset();

        g_sdk = nullptr;
        g_pluginHandle = nullptr;
        Utils::LogInfo("CyberpunkVR: Unloaded successfully");
        break;
    }
    }

    return true;
}

// Metadata Registration
RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::PluginInfo* aInfo)
{
    aInfo->name = L"CyberpunkVR";
    aInfo->author = L"OpenSourceCommunity";
    aInfo->version = RED4EXT_SEMVER(0, 0, 1);
    aInfo->runtime = RED4EXT_RUNTIME_LATEST;
    aInfo->sdk = RED4EXT_SDK_LATEST;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_LATEST;
}