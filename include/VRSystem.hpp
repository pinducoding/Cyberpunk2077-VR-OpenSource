#pragma once
#include <memory>

// Forward declarations only! No includes of D3D12 or OpenXR here.
struct ID3D12CommandQueue;
struct ID3D12Resource;

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

private:
    // Opaque pointer to the actual implementation (PIMPL)
    // This hides all OpenXR types from the rest of the project
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
