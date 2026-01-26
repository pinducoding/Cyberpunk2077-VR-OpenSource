#include "InputHook.hpp"
#include "Utils.hpp"
#include "VRSystem.hpp"
#include <RED4ext/RED4ext.hpp>

// Windows Headers
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

#pragma comment(lib, "xinput.lib")

// Globals from Main.cpp
extern RED4ext::PluginHandle g_pluginHandle;
extern const RED4ext::Sdk* g_sdk;
extern std::unique_ptr<VRSystem> g_vrSystem;

// Typedef for the original function
typedef DWORD (WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
XInputGetState_t Real_XInputGetState = nullptr;

// Our Hook
DWORD WINAPI Hook_XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    // 1. Call Original (so standard controller still works)
    DWORD result = ERROR_SUCCESS;
    
    if (Real_XInputGetState) {
        result = Real_XInputGetState(dwUserIndex, pState);
    } else {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // 2. Inject VR Input (Player 1 only)
    if (result == ERROR_SUCCESS && dwUserIndex == 0 && g_vrSystem) {
        // TODO: Query g_vrSystem for button states
        // Example: if (g_vrSystem->IsTriggerPressed()) pState->Gamepad.wButtons |= XINPUT_GAMEPAD_A;
    }

    return result;
}

namespace InputHook
{
    bool Initialize()
    {
        // 1. Get Address of XInputGetState
        // Try XInput 1.4 (Win 8+) then 1.3 (Win 7)
        HMODULE hXInput = LoadLibraryA("XInput1_4.dll");
        if (!hXInput) hXInput = LoadLibraryA("XInput1_3.dll");
        
        if (!hXInput) {
            Utils::LogError("Could not load XInput DLL");
            return false;
        }

        void* pXInputGetState = (void*)GetProcAddress(hXInput, "XInputGetState");
        if (!pXInputGetState) {
            Utils::LogError("Could not find XInputGetState address");
            return false;
        }

        // 2. Hook it using RED4ext
        if (!g_sdk || !g_sdk->hooking) {
            Utils::LogError("RED4ext Hooking interface missing");
            return false;
        }

        bool success = g_sdk->hooking->Attach(
            g_pluginHandle, 
            pXInputGetState, 
            &Hook_XInputGetState, 
            reinterpret_cast<void**>(&Real_XInputGetState)
        );

        if (success) {
            Utils::LogInfo("XInput Hook Installed");
        } else {
            Utils::LogError("Failed to attach XInput Hook");
        }

        return success;
    }

    void Shutdown()
    {
        // Detach logic would go here
    }
}
