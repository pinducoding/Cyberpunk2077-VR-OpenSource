#pragma once
#include <atomic>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/entIPlacedComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/BaseCameraComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/WorldTransform.hpp>

// Forward declarations
namespace RED4ext {
    struct CBaseRTTIType;
    namespace game {
        struct CameraSystem;
    }
}

// Function pointer type for the camera update hook
using CameraUpdateFunc = void (*)(RED4ext::ent::BaseCameraComponent*);

// Cached VR pose for use across frames
struct VRPose
{
    float x = 0, y = 0, z = 0;
    float qx = 0, qy = 0, qz = 0, qw = 1;
};

class CameraHook
{
public:
    CameraHook();
    ~CameraHook();

    // Initialize camera hooking (tries SDK first, then pattern scan)
    bool InstallHooks();

    // Called each frame to update VR camera (for SDK approach)
    void UpdateVRCamera();

    // The hook target (for pattern-based hooking)
    static void __fastcall OnCameraUpdate(RED4ext::ent::BaseCameraComponent* aComponent);

    // Trampoline (Original function)
    static CameraUpdateFunc Real_CameraUpdate;

private:
    // Try to access camera via RED4ext SDK (preferred)
    bool TrySDKApproach();

    // Get camera system instance
    RED4ext::game::CameraSystem* GetCameraSystem();

    bool m_hooksInstalled = false;
    bool m_useSDKApproach = false;

    // RTTI type for CameraSystem (cached)
    RED4ext::CBaseRTTIType* m_cameraSystemType = nullptr;

    // Cached VR pose
    VRPose m_lastPose;
    std::atomic<bool> m_hasPose{false};
};
