#pragma once
#include <memory>
#include <cstdint>

// Forward declarations only! No includes of D3D12 or OpenXR here.
struct ID3D12CommandQueue;
struct ID3D12Resource;

// Hand pose for motion controller aiming
struct VRHandPose
{
    // Position (meters, in game coordinate space)
    float x = 0.0f, y = 0.0f, z = 0.0f;

    // Orientation (quaternion)
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;

    // Aim direction (unit vector, derived from orientation)
    float aimX = 0.0f, aimY = 1.0f, aimZ = 0.0f;

    // Aim angles (degrees, for input injection)
    float yaw = 0.0f;    // Horizontal angle
    float pitch = 0.0f;  // Vertical angle

    bool valid = false;
};

// VR Controller state (matches XInput gamepad layout for easy mapping)
struct VRControllerState
{
    // Buttons (bitmask)
    uint16_t buttons = 0;

    // Triggers (0.0 - 1.0)
    float leftTrigger = 0.0f;
    float rightTrigger = 0.0f;

    // Thumbsticks (-1.0 to 1.0)
    float leftThumbX = 0.0f;
    float leftThumbY = 0.0f;
    float rightThumbX = 0.0f;
    float rightThumbY = 0.0f;

    // Grip (0.0 - 1.0)
    float leftGrip = 0.0f;
    float rightGrip = 0.0f;

    // Controller tracking valid
    bool leftHandValid = false;
    bool rightHandValid = false;

    // Hand poses for motion aiming
    VRHandPose leftHand;
    VRHandPose rightHand;

    // Button constants (XInput compatible)
    static constexpr uint16_t BUTTON_A = 0x1000;           // Right controller primary
    static constexpr uint16_t BUTTON_B = 0x2000;           // Right controller secondary
    static constexpr uint16_t BUTTON_X = 0x4000;           // Left controller primary
    static constexpr uint16_t BUTTON_Y = 0x8000;           // Left controller secondary
    static constexpr uint16_t BUTTON_LEFT_SHOULDER = 0x0100;   // Left grip
    static constexpr uint16_t BUTTON_RIGHT_SHOULDER = 0x0200;  // Right grip
    static constexpr uint16_t BUTTON_LEFT_THUMB = 0x0040;      // Left thumbstick click
    static constexpr uint16_t BUTTON_RIGHT_THUMB = 0x0080;     // Right thumbstick click
    static constexpr uint16_t BUTTON_START = 0x0010;           // Menu button
    static constexpr uint16_t BUTTON_BACK = 0x0020;            // System button (if available)
    static constexpr uint16_t BUTTON_DPAD_UP = 0x0001;
    static constexpr uint16_t BUTTON_DPAD_DOWN = 0x0002;
    static constexpr uint16_t BUTTON_DPAD_LEFT = 0x0004;
    static constexpr uint16_t BUTTON_DPAD_RIGHT = 0x0008;
};

class VRSystem
{
public:
    VRSystem();
    ~VRSystem();

    // Initialize OpenXR Loader and connect to headset
    bool Initialize(ID3D12CommandQueue* gameCommandQueue);

    // Per-frame update: Get head pose
    // Returns true if head pose is valid
    bool Update(float& outX, float& outY, float& outZ, float& outQX, float& outQY, float& outQZ, float& outQW);

    // Submit frame to headset (AER)
    // isLeftEye: true for frame N, false for frame N+1
    void SubmitFrame(ID3D12Resource* gameTexture, bool isLeftEye);

    // Get VR controller state for input mapping
    // Returns true if controllers are available
    bool GetControllerState(VRControllerState& outState);

private:
    // Opaque pointer to the actual implementation (PIMPL)
    // This hides all OpenXR types from the rest of the project
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
