#include "CameraHook.hpp"
#include "VRSystem.hpp"
#include "PatternScanner.hpp"
#include "ThreadSafe.hpp"
#include "Utils.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/GameEngine.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/CameraSystem.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/BaseCameraComponent.hpp>

// Include Vector4 for position manipulation
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>

// Access the global VR System and RED4ext handles
extern std::unique_ptr<VRSystem> g_vrSystem;
extern RED4ext::PluginHandle g_pluginHandle;
extern const RED4ext::Sdk* g_sdk;

// Static member definitions
CameraUpdateFunc CameraHook::Real_CameraUpdate = nullptr;

CameraHook::CameraHook()
{
}

CameraHook::~CameraHook()
{
    // Hook will be automatically removed by RED4ext on unload
}

bool CameraHook::InstallHooks()
{
    Utils::LogInfo("CameraHook: Setting up camera access...");

    // Method 1: Try SDK-based approach first (preferred - no pattern scanning needed)
    if (TrySDKApproach())
    {
        Utils::LogInfo("CameraHook: Using SDK-based camera access (recommended)");
        m_useSDKApproach = true;
        m_hooksInstalled = true;
        return true;
    }

    // Method 2: Fall back to pattern scanning if SDK approach fails
    Utils::LogInfo("CameraHook: SDK approach unavailable, trying pattern scan...");

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

    Utils::LogInfo("CameraHook: Pattern-based hook installed successfully!");
    m_hooksInstalled = true;
    return true;
}

bool CameraHook::TrySDKApproach()
{
    // Try to access the CameraSystem via RED4ext SDK
    // This is more stable than pattern scanning

    auto engine = RED4ext::CGameEngine::Get();
    if (!engine)
    {
        Utils::LogWarn("CameraHook: Game engine not available yet");
        return false;
    }

    if (!engine->framework)
    {
        Utils::LogWarn("CameraHook: Game framework not available yet");
        return false;
    }

    auto gameInstance = engine->framework->gameInstance;
    if (!gameInstance)
    {
        Utils::LogWarn("CameraHook: Game instance not available yet");
        return false;
    }

    // Get RTTI type for CameraSystem
    auto rtti = RED4ext::CRTTISystem::Get();
    if (!rtti)
    {
        Utils::LogWarn("CameraHook: RTTI system not available");
        return false;
    }

    auto cameraSystemType = rtti->GetClass("gameCameraSystem");
    if (!cameraSystemType)
    {
        Utils::LogWarn("CameraHook: CameraSystem type not found in RTTI");
        return false;
    }

    // Try to get the camera system
    auto cameraSystem = gameInstance->GetSystem(cameraSystemType);
    if (!cameraSystem)
    {
        Utils::LogWarn("CameraHook: CameraSystem instance not available");
        return false;
    }

    Utils::LogInfo("CameraHook: Successfully accessed CameraSystem via SDK!");
    m_cameraSystemType = cameraSystemType;
    return true;
}

RED4ext::game::CameraSystem* CameraHook::GetCameraSystem()
{
    auto engine = RED4ext::CGameEngine::Get();
    if (!engine || !engine->framework || !engine->framework->gameInstance)
    {
        return nullptr;
    }

    if (!m_cameraSystemType)
    {
        auto rtti = RED4ext::CRTTISystem::Get();
        if (rtti)
        {
            m_cameraSystemType = rtti->GetClass("gameCameraSystem");
        }
    }

    if (!m_cameraSystemType)
    {
        return nullptr;
    }

    return reinterpret_cast<RED4ext::game::CameraSystem*>(
        engine->framework->gameInstance->GetSystem(m_cameraSystemType)
    );
}

void CameraHook::UpdateVRCamera()
{
    // Called each frame to inject VR head pose
    if (!g_vrSystem || !VRConfig::IsVREnabled())
    {
        return;
    }

    // Get VR head pose
    float x, y, z, qx, qy, qz, qw;
    if (!g_vrSystem->Update(x, y, z, qx, qy, qz, qw))
    {
        return;
    }

    // If using SDK approach, we modify via the camera system
    if (m_useSDKApproach)
    {
        auto cameraSystem = GetCameraSystem();
        if (cameraSystem)
        {
            // TODO: Access active camera component and modify transform
            // The exact method depends on CameraSystem's internal structure
            // which requires further reverse engineering or testing
        }
    }

    // AER (Alternate Eye Rendering) logic
    static ThreadSafe::Counter frameCount{0};
    uint64_t frame = frameCount.fetch_add(1);

    float ipd = VRConfig::GetIPD();
    float worldScale = VRConfig::GetWorldScale();

    // Apply world scale
    x *= worldScale;
    y *= worldScale;
    z *= worldScale;

    // Eye offset for stereo rendering
    float offsetX = 0.0f;
    if (frame % 2 == 0) {
         offsetX = -(ipd / 2.0f);  // Left eye
    } else {
         offsetX = +(ipd / 2.0f);  // Right eye
    }

    // Store for use in hook callback
    m_lastPose = { x + offsetX, y, z, qx, qy, qz, qw };
    m_hasPose.store(true);
}

void __fastcall CameraHook::OnCameraUpdate(RED4ext::ent::BaseCameraComponent* aComponent)
{
    // 1. Get VR Head Pose
    float x, y, z, qx, qy, qz, qw;
    if (g_vrSystem && VRConfig::IsVREnabled() && g_vrSystem->Update(x, y, z, qx, qy, qz, qw)) {

        // 2. Cast to IPlacedComponent to access Transform
        auto placed = reinterpret_cast<RED4ext::ent::IPlacedComponent*>(aComponent);

        // 3. Apply Eye Offset (AER) Logic
        static ThreadSafe::Counter frameCount{0};
        uint64_t frame = frameCount.fetch_add(1);

        // Get configurable IPD and world scale (thread-safe)
        float ipd = VRConfig::GetIPD();
        float worldScale = VRConfig::GetWorldScale();

        // Apply world scale to position
        x *= worldScale;
        y *= worldScale;
        z *= worldScale;

        float offsetX = 0.0f;
        if (frame % 2 == 0) {
             offsetX = -(ipd / 2.0f);  // Left eye
        } else {
             offsetX = +(ipd / 2.0f);  // Right eye
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
