#pragma once

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>

// Native function bindings for CET Lua access
// These functions are registered with RED4ext and callable from Cyber Engine Tweaks
namespace VRSettings
{
    // Register all native functions with RED4ext
    void RegisterNativeFunctions(const RED4ext::Sdk* sdk, RED4ext::PluginHandle handle);

    // Unregister on shutdown
    void UnregisterNativeFunctions(const RED4ext::Sdk* sdk, RED4ext::PluginHandle handle);
}
