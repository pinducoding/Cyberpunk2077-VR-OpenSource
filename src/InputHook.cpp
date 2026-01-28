#include "InputHook.hpp"
#include "Utils.hpp"
#include "VRSystem.hpp"
#include "ThreadSafe.hpp"
#include <RED4ext/RED4ext.hpp>

// Windows Headers
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

// Deadzone helper
static float ApplyDeadzone(float value, float deadzone = 0.15f)
{
    if (std::abs(value) < deadzone)
        return 0.0f;

    // Remap the value from [deadzone, 1] to [0, 1]
    float sign = value > 0 ? 1.0f : -1.0f;
    return sign * (std::abs(value) - deadzone) / (1.0f - deadzone);
}

// Convert float [-1, 1] to SHORT [-32768, 32767]
static SHORT FloatToShort(float value)
{
    value = std::max(-1.0f, std::min(1.0f, value));
    if (value >= 0)
        return static_cast<SHORT>(value * 32767.0f);
    else
        return static_cast<SHORT>(value * 32768.0f);
}

// Convert float [0, 1] to BYTE [0, 255]
static BYTE FloatToByte(float value)
{
    value = std::max(0.0f, std::min(1.0f, value));
    return static_cast<BYTE>(value * 255.0f);
}

// Our Hook
DWORD WINAPI Hook_XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    // 1. Call Original (so standard controller still works)
    DWORD result = ERROR_SUCCESS;

    if (Real_XInputGetState)
    {
        result = Real_XInputGetState(dwUserIndex, pState);
    }
    else
    {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    // 2. If VR is disabled or no VR system, just return original
    if (!VRConfig::IsVREnabled() || !g_vrSystem)
    {
        return result;
    }

    // 3. Inject VR Input (Player 1 only)
    if (dwUserIndex == 0 && pState)
    {
        VRControllerState vrState;
        if (g_vrSystem->GetControllerState(vrState))
        {
            // If original controller is not connected, initialize state
            if (result != ERROR_SUCCESS)
            {
                memset(pState, 0, sizeof(XINPUT_STATE));
                result = ERROR_SUCCESS;
            }

            // Map VR buttons to XInput buttons
            // The VRControllerState already uses XInput-compatible button flags
            pState->Gamepad.wButtons |= vrState.buttons;

            // Map VR triggers to XInput triggers
            pState->Gamepad.bLeftTrigger = std::max(pState->Gamepad.bLeftTrigger, FloatToByte(vrState.leftTrigger));
            pState->Gamepad.bRightTrigger = std::max(pState->Gamepad.bRightTrigger, FloatToByte(vrState.rightTrigger));

            // Map VR thumbsticks to XInput thumbsticks
            float leftX = ApplyDeadzone(vrState.leftThumbX);
            float leftY = ApplyDeadzone(vrState.leftThumbY);
            float rightX = ApplyDeadzone(vrState.rightThumbX);
            float rightY = ApplyDeadzone(vrState.rightThumbY);

            // Combine with existing input (take whichever has greater magnitude)
            if (std::abs(leftX) > std::abs(pState->Gamepad.sThumbLX / 32767.0f))
                pState->Gamepad.sThumbLX = FloatToShort(leftX);
            if (std::abs(leftY) > std::abs(pState->Gamepad.sThumbLY / 32767.0f))
                pState->Gamepad.sThumbLY = FloatToShort(leftY);
            if (std::abs(rightX) > std::abs(pState->Gamepad.sThumbRX / 32767.0f))
                pState->Gamepad.sThumbRX = FloatToShort(rightX);
            if (std::abs(rightY) > std::abs(pState->Gamepad.sThumbRY / 32767.0f))
                pState->Gamepad.sThumbRY = FloatToShort(rightY);

            // Increment packet number when VR input changes
            static DWORD lastVRButtons = 0;
            if (vrState.buttons != lastVRButtons)
            {
                pState->dwPacketNumber++;
                lastVRButtons = vrState.buttons;
            }
        }
    }

    return result;
}

namespace InputHook
{
    static ThreadSafe::Flag s_initialized{false};

    bool Initialize()
    {
        if (s_initialized.load())
        {
            return true;
        }

        // 1. Get Address of XInputGetState
        // Try XInput 1.4 (Win 8+) then 1.3 (Win 7)
        HMODULE hXInput = LoadLibraryA("XInput1_4.dll");
        if (!hXInput) hXInput = LoadLibraryA("XInput1_3.dll");

        if (!hXInput)
        {
            Utils::LogError("InputHook: Could not load XInput DLL");
            return false;
        }

        void* pXInputGetState = (void*)GetProcAddress(hXInput, "XInputGetState");
        if (!pXInputGetState)
        {
            Utils::LogError("InputHook: Could not find XInputGetState address");
            return false;
        }

        // 2. Hook it using RED4ext
        if (!g_sdk || !g_sdk->hooking)
        {
            Utils::LogError("InputHook: RED4ext Hooking interface missing");
            return false;
        }

        bool success = g_sdk->hooking->Attach(
            g_pluginHandle,
            pXInputGetState,
            &Hook_XInputGetState,
            reinterpret_cast<void**>(&Real_XInputGetState)
        );

        if (success)
        {
            Utils::LogInfo("InputHook: XInput hook installed - VR controllers enabled");
            s_initialized.store(true);
        }
        else
        {
            Utils::LogError("InputHook: Failed to attach XInput hook");
        }

        return success;
    }

    void Shutdown()
    {
        if (s_initialized.load())
        {
            // Detach would go here if RED4ext supports it
            s_initialized.store(false);
            Utils::LogInfo("InputHook: Shutdown");
        }
    }
}
