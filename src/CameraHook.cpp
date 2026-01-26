#include "CameraHook.hpp"
#include "VRSystem.hpp"
#include "PatternScanner.hpp"
#include "Utils.hpp"

#include <RED4ext/RED4ext.hpp>

// Include Vector4 for position manipulation
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>

// Access the global VR System and RED4ext handles
extern std::unique_ptr<VRSystem> g_vrSystem;
extern RED4ext::PluginHandle g_pluginHandle;
extern const RED4ext::Sdk* g_sdk;

CameraHook::CameraHook()
{
}

CameraHook::~CameraHook()
{
    // Hook will be automatically removed by RED4ext on unload
}

bool CameraHook::InstallHooks()
{
    Utils::LogInfo("CameraHook: Searching for camera update function...");

    // Try multiple patterns (game updates may change bytes)
    uintptr_t cameraUpdateAddr = 0;

    // Pattern 1: Primary camera update signature
    cameraUpdateAddr = PatternScanner::FindPattern(PatternScanner::Patterns::CameraUpdate);

    if (cameraUpdateAddr == 0)
    {
        // Pattern 2: Alternative - search for specific camera component vtable
        // BaseCameraComponent has a virtual Update function
        cameraUpdateAddr = PatternScanner::FindPattern(
            "48 89 5C 24 ?? 57 48 83 EC ?? 48 8B D9 48 8B 89 ?? ?? ?? ?? 48 85 C9"
        );
    }

    if (cameraUpdateAddr == 0)
    {
        // Pattern 3: Fallback - look for WorldTransform access pattern
        cameraUpdateAddr = PatternScanner::FindPattern(
            "F3 0F 10 ?? ?? ?? ?? ?? F3 0F 10 ?? ?? ?? ?? ?? 48 8D ?? ?? ?? ?? ??"
        );
    }

    if (cameraUpdateAddr == 0)
    {
        Utils::LogWarn("CameraHook: Could not find camera update function!");
        Utils::LogWarn("CameraHook: VR head tracking will be disabled.");
        Utils::LogWarn("CameraHook: Game may have been updated - patterns need refresh.");
        // Return true to allow plugin to load (partial functionality)
        return true;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "CameraHook: Found camera update at 0x%llX",
             static_cast<unsigned long long>(cameraUpdateAddr));
    Utils::LogInfo(msg);

    // Install the hook via RED4ext
    bool success = g_sdk->hooking->Attach(
        g_pluginHandle,
        reinterpret_cast<void*>(cameraUpdateAddr),
        reinterpret_cast<void*>(&CameraHook::OnCameraUpdate),
        reinterpret_cast<void**>(&CameraHook::Real_CameraUpdate)
    );

    if (!success)
    {
        Utils::LogError("CameraHook: Failed to install hook!");
        return false;
    }

    Utils::LogInfo("CameraHook: Hook installed successfully!");
    m_hooksInstalled = true;
    return true;
}

void __fastcall CameraHook::OnCameraUpdate(RED4ext::ent::BaseCameraComponent* aComponent)
{
    // 1. Get VR Head Pose
    float x, y, z, qx, qy, qz, qw;
    if (g_vrSystem && g_vrSystem->Update(x, y, z, qx, qy, qz, qw)) {
        
        // 2. Cast to IPlacedComponent to access Transform
        auto placed = reinterpret_cast<RED4ext::ent::IPlacedComponent*>(aComponent);
        
        // 3. Apply Eye Offset (AER) Logic
        static int frameCount = 0;
        frameCount++;
        float ipd = 0.064f; // 64mm
        
        float offsetX = 0.0f;
        if (frameCount % 2 == 0) {
             offsetX = -(ipd / 2.0f);
        } else {
             offsetX = +(ipd / 2.0f);
        }

        // 4. Construct New Position (Handling FixedPoint conversion)
        // We use the SDK's WorldPosition constructor which takes a Vector4
        // Note: Coordinate system conversion (handedness) might be needed here later.
        RED4ext::Vector4 newPos(x + offsetX, y, z, 1.0f);
        
        placed->worldTransform.Position = RED4ext::WorldPosition(newPos);
        
        // 5. Override Orientation
        placed->worldTransform.Orientation.i = qx;
        placed->worldTransform.Orientation.j = qy;
        placed->worldTransform.Orientation.k = qz;
        placed->worldTransform.Orientation.r = qw;
    }

    // 6. Call Original
    if (CameraHook::Real_CameraUpdate) {
        CameraHook::Real_CameraUpdate(aComponent);
    }
}