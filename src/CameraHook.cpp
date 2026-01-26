#include "CameraHook.hpp"
#include "VRSystem.hpp"
#include "Utils.hpp"

// Include Vector4 for position manipulation
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>

// Access the global VR System
extern std::unique_ptr<VRSystem> g_vrSystem;

CameraHook::CameraHook()
{
}

CameraHook::~CameraHook()
{
}

bool CameraHook::InstallHooks()
{
    // Example Pattern (Fake for compilation):
    uintptr_t cameraUpdateAddr = 0; // FindPattern(...)
    
    if (cameraUpdateAddr == 0) {
        Utils::LogWarn("Camera Hook Address not found (Pattern Scan unimplemented)");
        return true; 
    }

    Utils::LogInfo("Camera Hooks Installed");
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
    if (Real_CameraUpdate) {
        Real_CameraUpdate(aComponent);
    }
}