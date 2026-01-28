#include "VRSettings.hpp"
#include "ThreadSafe.hpp"
#include "Utils.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/RTTITypes.hpp>

// Native function implementations callable from CET Lua

// SetVREnabled(enabled: Bool) -> Void
void Native_SetVREnabled(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame,
                          void* aOut, int64_t a4)
{
    bool enabled;
    RED4ext::GetParameter(aFrame, &enabled);
    aFrame->code++;

    VRConfig::SetVREnabled(enabled);
    Utils::LogInfo(enabled ? "VR: Enabled via CET" : "VR: Disabled via CET");
}

// GetVREnabled() -> Bool
void Native_GetVREnabled(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame,
                          bool* aOut, int64_t a4)
{
    aFrame->code++;
    if (aOut)
    {
        *aOut = VRConfig::IsVREnabled();
    }
}

// SetIPD(ipdMillimeters: Float) -> Void
void Native_SetIPD(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame,
                    void* aOut, int64_t a4)
{
    float ipdMM;
    RED4ext::GetParameter(aFrame, &ipdMM);
    aFrame->code++;

    // Convert mm to meters
    float ipdMeters = ipdMM / 1000.0f;

    // Clamp to reasonable values (50mm - 80mm)
    if (ipdMeters < 0.050f) ipdMeters = 0.050f;
    if (ipdMeters > 0.080f) ipdMeters = 0.080f;

    VRConfig::SetIPD(ipdMeters);

    char msg[64];
    snprintf(msg, sizeof(msg), "VR: IPD set to %.1fmm via CET", ipdMM);
    Utils::LogInfo(msg);
}

// GetIPD() -> Float (returns millimeters)
void Native_GetIPD(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame,
                    float* aOut, int64_t a4)
{
    aFrame->code++;
    if (aOut)
    {
        // Convert meters to mm for Lua
        *aOut = VRConfig::GetIPD() * 1000.0f;
    }
}

// SetWorldScale(scale: Float) -> Void
void Native_SetWorldScale(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame,
                           void* aOut, int64_t a4)
{
    float scale;
    RED4ext::GetParameter(aFrame, &scale);
    aFrame->code++;

    // Clamp to reasonable values
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 2.0f) scale = 2.0f;

    VRConfig::SetWorldScale(scale);

    char msg[64];
    snprintf(msg, sizeof(msg), "VR: World scale set to %.2f via CET", scale);
    Utils::LogInfo(msg);
}

// GetWorldScale() -> Float
void Native_GetWorldScale(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame,
                           float* aOut, int64_t a4)
{
    aFrame->code++;
    if (aOut)
    {
        *aOut = VRConfig::GetWorldScale();
    }
}

// SetDecoupledAiming(enabled: Bool) -> Void
void Native_SetDecoupledAiming(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame,
                                void* aOut, int64_t a4)
{
    bool enabled;
    RED4ext::GetParameter(aFrame, &enabled);
    aFrame->code++;

    VRConfig::SetDecoupledAiming(enabled);
    Utils::LogInfo(enabled ? "VR: Decoupled aiming enabled via CET" : "VR: Decoupled aiming disabled via CET");
}

// GetDecoupledAiming() -> Bool
void Native_GetDecoupledAiming(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame,
                                bool* aOut, int64_t a4)
{
    aFrame->code++;
    if (aOut)
    {
        *aOut = VRConfig::IsDecoupledAiming();
    }
}

// SetAimSmoothing(factor: Float) -> Void
void Native_SetAimSmoothing(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame,
                             void* aOut, int64_t a4)
{
    float factor;
    RED4ext::GetParameter(aFrame, &factor);
    aFrame->code++;

    // Clamp to valid range [0, 0.95]
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 0.95f) factor = 0.95f;

    VRConfig::SetAimSmoothing(factor);

    char msg[64];
    snprintf(msg, sizeof(msg), "VR: Aim smoothing set to %.2f via CET", factor);
    Utils::LogInfo(msg);
}

// GetAimSmoothing() -> Float
void Native_GetAimSmoothing(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame,
                             float* aOut, int64_t a4)
{
    aFrame->code++;
    if (aOut)
    {
        *aOut = VRConfig::GetAimSmoothing();
    }
}

namespace VRSettings
{
    void RegisterNativeFunctions(const RED4ext::Sdk* sdk, RED4ext::PluginHandle handle)
    {
        Utils::LogInfo("VRSettings: Registering native functions for CET...");

        auto rtti = RED4ext::CRTTISystem::Get();

        // Register global functions that CET can call
        // Use CGlobalFunction for global (non-class) functions

        // native func CyberpunkVR_SetEnabled(enabled: Bool) -> Void
        {
            auto func = RED4ext::CGlobalFunction::Create("CyberpunkVR_SetEnabled", "CyberpunkVR_SetEnabled", &Native_SetVREnabled);
            func->AddParam("Bool", "enabled");
            rtti->RegisterFunction(func);
        }

        // native func CyberpunkVR_GetEnabled() -> Bool
        {
            auto func = RED4ext::CGlobalFunction::Create("CyberpunkVR_GetEnabled", "CyberpunkVR_GetEnabled", &Native_GetVREnabled);
            func->SetReturnType("Bool");
            rtti->RegisterFunction(func);
        }

        // native func CyberpunkVR_SetIPD(ipdMM: Float) -> Void
        {
            auto func = RED4ext::CGlobalFunction::Create("CyberpunkVR_SetIPD", "CyberpunkVR_SetIPD", &Native_SetIPD);
            func->AddParam("Float", "ipdMM");
            rtti->RegisterFunction(func);
        }

        // native func CyberpunkVR_GetIPD() -> Float
        {
            auto func = RED4ext::CGlobalFunction::Create("CyberpunkVR_GetIPD", "CyberpunkVR_GetIPD", &Native_GetIPD);
            func->SetReturnType("Float");
            rtti->RegisterFunction(func);
        }

        // native func CyberpunkVR_SetWorldScale(scale: Float) -> Void
        {
            auto func = RED4ext::CGlobalFunction::Create("CyberpunkVR_SetWorldScale", "CyberpunkVR_SetWorldScale", &Native_SetWorldScale);
            func->AddParam("Float", "scale");
            rtti->RegisterFunction(func);
        }

        // native func CyberpunkVR_GetWorldScale() -> Float
        {
            auto func = RED4ext::CGlobalFunction::Create("CyberpunkVR_GetWorldScale", "CyberpunkVR_GetWorldScale", &Native_GetWorldScale);
            func->SetReturnType("Float");
            rtti->RegisterFunction(func);
        }

        // native func CyberpunkVR_SetDecoupledAiming(enabled: Bool) -> Void
        {
            auto func = RED4ext::CGlobalFunction::Create("CyberpunkVR_SetDecoupledAiming", "CyberpunkVR_SetDecoupledAiming", &Native_SetDecoupledAiming);
            func->AddParam("Bool", "enabled");
            rtti->RegisterFunction(func);
        }

        // native func CyberpunkVR_GetDecoupledAiming() -> Bool
        {
            auto func = RED4ext::CGlobalFunction::Create("CyberpunkVR_GetDecoupledAiming", "CyberpunkVR_GetDecoupledAiming", &Native_GetDecoupledAiming);
            func->SetReturnType("Bool");
            rtti->RegisterFunction(func);
        }

        // native func CyberpunkVR_SetAimSmoothing(factor: Float) -> Void
        {
            auto func = RED4ext::CGlobalFunction::Create("CyberpunkVR_SetAimSmoothing", "CyberpunkVR_SetAimSmoothing", &Native_SetAimSmoothing);
            func->AddParam("Float", "factor");
            rtti->RegisterFunction(func);
        }

        // native func CyberpunkVR_GetAimSmoothing() -> Float
        {
            auto func = RED4ext::CGlobalFunction::Create("CyberpunkVR_GetAimSmoothing", "CyberpunkVR_GetAimSmoothing", &Native_GetAimSmoothing);
            func->SetReturnType("Float");
            rtti->RegisterFunction(func);
        }

        Utils::LogInfo("VRSettings: Native functions registered successfully");
    }

    void UnregisterNativeFunctions(const RED4ext::Sdk* sdk, RED4ext::PluginHandle handle)
    {
        // Functions are cleaned up automatically by RED4ext on unload
        Utils::LogInfo("VRSettings: Native functions will be unregistered on shutdown");
    }
}
