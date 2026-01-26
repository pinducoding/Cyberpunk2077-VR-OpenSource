#include "VRSystem.hpp"
#include "Utils.hpp"
#include <vector>
#include <string>
#include <cmath>

// Windows / DirectX / OpenXR Headers
// Defined ONLY here to prevent pollution in other files
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include <algorithm> // for std::min

// Coordinate system conversion constants
// REDengine uses: X-right, Y-forward, Z-up (left-handed)
// OpenXR uses: X-right, Y-up, Z-back (right-handed)
namespace CoordinateConversion
{
    // Convert REDengine position to OpenXR
    inline void REDToOpenXR(float redX, float redY, float redZ,
                            float& oxrX, float& oxrY, float& oxrZ)
    {
        oxrX = redX;      // X stays the same
        oxrY = redZ;      // RED Z (up) -> OpenXR Y (up)
        oxrZ = -redY;     // RED Y (forward) -> OpenXR -Z (forward)
    }

    // Convert OpenXR position to REDengine
    inline void OpenXRToRED(float oxrX, float oxrY, float oxrZ,
                            float& redX, float& redY, float& redZ)
    {
        redX = oxrX;      // X stays the same
        redY = -oxrZ;     // OpenXR -Z (forward) -> RED Y (forward)
        redZ = oxrY;      // OpenXR Y (up) -> RED Z (up)
    }

    // Convert OpenXR quaternion to REDengine quaternion
    inline void OpenXRQuatToRED(float oxrX, float oxrY, float oxrZ, float oxrW,
                                 float& redI, float& redJ, float& redK, float& redR)
    {
        // Quaternion conversion for coordinate system change
        // This swaps Y<->Z and negates accordingly
        redI = oxrX;
        redJ = -oxrZ;     // Swap and negate
        redK = oxrY;      // Swap
        redR = oxrW;
    }
}

// The Actual Implementation Class
class VRSystem::Impl
{
public:
    bool m_initialized = false;
    bool m_sessionReady = false;

    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_systemId = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_appSpace = XR_NULL_HANDLE;

    XrGraphicsBindingD3D12KHR m_graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};

    // D3D12 resources for texture copy
    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_commandQueue = nullptr;
    ID3D12CommandAllocator* m_commandAllocator = nullptr;
    ID3D12GraphicsCommandList* m_commandList = nullptr;
    ID3D12Fence* m_fence = nullptr;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_fenceValue = 0;

    // Frame state
    XrFrameState m_frameState{XR_TYPE_FRAME_STATE};
    bool m_frameInProgress = false;

    struct SwapchainInfo {
        XrSwapchain handle = XR_NULL_HANDLE;
        int32_t width = 0;
        int32_t height = 0;
        std::vector<XrSwapchainImageD3D12KHR> images;
    };

    // Two swapchains for stereo (left and right eye)
    SwapchainInfo m_swapchains[2];

    // View configuration
    std::vector<XrViewConfigurationView> m_viewConfigs;
    std::vector<XrView> m_views;
    std::vector<XrCompositionLayerProjectionView> m_projectionViews;

    bool CreateInstance() {
        XrApplicationInfo appInfo = {};
        strcpy_s(appInfo.applicationName, "CyberpunkVR");
        appInfo.applicationVersion = 1;
        strcpy_s(appInfo.engineName, "RED4ext");
        appInfo.engineVersion = 1;
        appInfo.apiVersion = XR_CURRENT_API_VERSION;

        const char* extensions[] = { "XR_KHR_D3D12_enable" };

        XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
        createInfo.applicationInfo = appInfo;
        createInfo.enabledExtensionCount = 1;
        createInfo.enabledExtensionNames = extensions;

        return XR_SUCCEEDED(xrCreateInstance(&createInfo, &m_instance));
    }

    bool CreateSession(ID3D12CommandQueue* gameCommandQueue) {
        XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (XR_FAILED(xrGetSystem(m_instance, &systemInfo, &m_systemId))) {
            Utils::LogError("OpenXR: No HMD found!");
            return false;
        }

        m_commandQueue = gameCommandQueue;
        gameCommandQueue->GetDevice(IID_PPV_ARGS(&m_device));

        m_graphicsBinding.device = m_device;
        m_graphicsBinding.queue = gameCommandQueue;

        XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
        sessionInfo.next = &m_graphicsBinding;
        sessionInfo.systemId = m_systemId;

        if (XR_FAILED(xrCreateSession(m_instance, &sessionInfo, &m_session))) {
            Utils::LogError("OpenXR: Failed to create session");
            return false;
        }

        XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;

        if (XR_FAILED(xrCreateReferenceSpace(m_session, &spaceInfo, &m_appSpace))) {
            Utils::LogError("OpenXR: Failed to create reference space");
            return false;
        }

        return true;
    }

    bool CreateSwapchains() {
        // Get view configuration
        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews(m_instance, m_systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);

        if (viewCount != 2) {
            Utils::LogError("OpenXR: Expected 2 views for stereo, got different count");
            return false;
        }

        m_viewConfigs.resize(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
        xrEnumerateViewConfigurationViews(m_instance, m_systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, m_viewConfigs.data());

        m_views.resize(viewCount, { XR_TYPE_VIEW });
        m_projectionViews.resize(viewCount, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW });

        // Create a swapchain for each eye
        for (uint32_t i = 0; i < viewCount; i++) {
            XrSwapchainCreateInfo swapchainInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
            swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            swapchainInfo.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapchainInfo.sampleCount = 1;
            swapchainInfo.width = m_viewConfigs[i].recommendedImageRectWidth;
            swapchainInfo.height = m_viewConfigs[i].recommendedImageRectHeight;
            swapchainInfo.arraySize = 1;
            swapchainInfo.faceCount = 1;
            swapchainInfo.mipCount = 1;

            if (XR_FAILED(xrCreateSwapchain(m_session, &swapchainInfo, &m_swapchains[i].handle))) {
                Utils::LogError("OpenXR: Failed to create swapchain");
                return false;
            }

            m_swapchains[i].width = swapchainInfo.width;
            m_swapchains[i].height = swapchainInfo.height;

            uint32_t imageCount;
            xrEnumerateSwapchainImages(m_swapchains[i].handle, 0, &imageCount, nullptr);
            m_swapchains[i].images.resize(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
            xrEnumerateSwapchainImages(m_swapchains[i].handle, imageCount, &imageCount,
                (XrSwapchainImageBaseHeader*)m_swapchains[i].images.data());

            char msg[128];
            snprintf(msg, sizeof(msg), "OpenXR: Swapchain %d created (%dx%d, %d images)",
                     i, m_swapchains[i].width, m_swapchains[i].height, imageCount);
            Utils::LogInfo(msg);
        }

        return true;
    }

    bool CreateD3D12Resources() {
        if (!m_device) return false;

        // Create command allocator
        if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandAllocator)))) {
            Utils::LogError("D3D12: Failed to create command allocator");
            return false;
        }

        // Create command list
        if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_commandAllocator, nullptr, IID_PPV_ARGS(&m_commandList)))) {
            Utils::LogError("D3D12: Failed to create command list");
            return false;
        }
        m_commandList->Close();

        // Create fence for GPU synchronization
        if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
            Utils::LogError("D3D12: Failed to create fence");
            return false;
        }

        m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent) {
            Utils::LogError("D3D12: Failed to create fence event");
            return false;
        }

        Utils::LogInfo("D3D12: Copy resources created successfully");
        return true;
    }

    void WaitForGPU() {
        if (!m_fence || !m_commandQueue) return;

        m_fenceValue++;
        m_commandQueue->Signal(m_fence, m_fenceValue);

        if (m_fence->GetCompletedValue() < m_fenceValue) {
            m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

    void CopyTexture(ID3D12Resource* source, ID3D12Resource* dest) {
        if (!source || !dest || !m_commandList || !m_commandAllocator) return;

        // Reset command allocator and list
        m_commandAllocator->Reset();
        m_commandList->Reset(m_commandAllocator, nullptr);

        // Transition source to copy source state
        D3D12_RESOURCE_BARRIER barrierSrc = {};
        barrierSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierSrc.Transition.pResource = source;
        barrierSrc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrierSrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrierSrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        // Transition dest to copy dest state
        D3D12_RESOURCE_BARRIER barrierDst = {};
        barrierDst.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDst.Transition.pResource = dest;
        barrierDst.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrierDst.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrierDst.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        D3D12_RESOURCE_BARRIER barriers[2] = { barrierSrc, barrierDst };
        m_commandList->ResourceBarrier(2, barriers);

        // Get source dimensions for region copy
        D3D12_RESOURCE_DESC srcDesc = source->GetDesc();
        D3D12_RESOURCE_DESC dstDesc = dest->GetDesc();

        // Copy region (handle size mismatch by copying what fits)
        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = source;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = dest;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_BOX srcBox = {};
        srcBox.left = 0;
        srcBox.top = 0;
        srcBox.front = 0;
        srcBox.right = static_cast<UINT>(std::min(srcDesc.Width, dstDesc.Width));
        srcBox.bottom = std::min(srcDesc.Height, dstDesc.Height);
        srcBox.back = 1;

        m_commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);

        // Transition back
        barrierSrc.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrierSrc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrierDst.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrierDst.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        barriers[0] = barrierSrc;
        barriers[1] = barrierDst;
        m_commandList->ResourceBarrier(2, barriers);

        // Execute
        m_commandList->Close();
        ID3D12CommandList* lists[] = { m_commandList };
        m_commandQueue->ExecuteCommandLists(1, lists);

        WaitForGPU();
    }
};

// Interface Implementation (Redirects to Impl)

VRSystem::VRSystem() : m_impl(std::make_unique<Impl>())
{
}

VRSystem::~VRSystem()
{
    // Wait for GPU before destroying resources
    if (m_impl->m_fence) {
        m_impl->WaitForGPU();
    }

    // Destroy OpenXR resources
    for (int i = 0; i < 2; i++) {
        if (m_impl->m_swapchains[i].handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(m_impl->m_swapchains[i].handle);
        }
    }
    if (m_impl->m_appSpace != XR_NULL_HANDLE) xrDestroySpace(m_impl->m_appSpace);
    if (m_impl->m_session != XR_NULL_HANDLE) xrDestroySession(m_impl->m_session);
    if (m_impl->m_instance != XR_NULL_HANDLE) xrDestroyInstance(m_impl->m_instance);

    // Destroy D3D12 resources
    if (m_impl->m_fenceEvent) CloseHandle(m_impl->m_fenceEvent);
    if (m_impl->m_fence) m_impl->m_fence->Release();
    if (m_impl->m_commandList) m_impl->m_commandList->Release();
    if (m_impl->m_commandAllocator) m_impl->m_commandAllocator->Release();
    if (m_impl->m_device) m_impl->m_device->Release();
}

bool VRSystem::Initialize(ID3D12CommandQueue* gameCommandQueue)
{
    // Phase 1: Create OpenXR instance (no D3D12 needed)
    if (!m_impl->m_initialized) {
        if (!m_impl->CreateInstance()) {
            Utils::LogError("Failed to create OpenXR Instance");
            return false;
        }
        m_impl->m_initialized = true;
        Utils::LogInfo("OpenXR: Instance created");
    }

    // Phase 2: Create session (needs D3D12 queue)
    if (!gameCommandQueue) {
        Utils::LogWarn("OpenXR: Instance created, waiting for D3D12 queue...");
        return true;
    }

    if (m_impl->m_sessionReady) {
        return true; // Already initialized
    }

    if (!m_impl->CreateSession(gameCommandQueue)) {
        Utils::LogError("Failed to create OpenXR Session");
        return false;
    }

    if (!m_impl->CreateSwapchains()) {
        Utils::LogError("Failed to create OpenXR Swapchains");
        return false;
    }

    if (!m_impl->CreateD3D12Resources()) {
        Utils::LogError("Failed to create D3D12 copy resources");
        return false;
    }

    m_impl->m_sessionReady = true;
    Utils::LogInfo("OpenXR: Fully initialized with D3D12!");
    return true;
}

bool VRSystem::Update(float& outX, float& outY, float& outZ, float& outQX, float& outQY, float& outQZ, float& outQW)
{
    if (m_impl->m_session == XR_NULL_HANDLE) return false;

    // Poll OpenXR events
    XrEventDataBuffer eventBuffer = { XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(m_impl->m_instance, &eventBuffer) == XR_SUCCESS) {
        switch (eventBuffer.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto* stateEvent = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventBuffer);
                if (stateEvent->state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
                    beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    xrBeginSession(m_impl->m_session, &beginInfo);
                    Utils::LogInfo("OpenXR: Session started");
                }
                break;
            }
            default:
                break;
        }
        eventBuffer = { XR_TYPE_EVENT_DATA_BUFFER };
    }

    // Wait for frame
    XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
    if (XR_FAILED(xrWaitFrame(m_impl->m_session, &waitInfo, &m_impl->m_frameState))) {
        return false;
    }

    // Begin frame
    XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
    if (XR_FAILED(xrBeginFrame(m_impl->m_session, &beginInfo))) {
        return false;
    }
    m_impl->m_frameInProgress = true;

    // Locate views (get head pose)
    XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = m_impl->m_frameState.predictedDisplayTime;
    locateInfo.space = m_impl->m_appSpace;

    XrViewState viewState = { XR_TYPE_VIEW_STATE };
    uint32_t viewCount = 2;

    if (XR_SUCCEEDED(xrLocateViews(m_impl->m_session, &locateInfo, &viewState, 2, &viewCount, m_impl->m_views.data()))) {
        // Convert from OpenXR coordinate system to REDengine
        float oxrX = m_impl->m_views[0].pose.position.x;
        float oxrY = m_impl->m_views[0].pose.position.y;
        float oxrZ = m_impl->m_views[0].pose.position.z;

        float oxrQX = m_impl->m_views[0].pose.orientation.x;
        float oxrQY = m_impl->m_views[0].pose.orientation.y;
        float oxrQZ = m_impl->m_views[0].pose.orientation.z;
        float oxrQW = m_impl->m_views[0].pose.orientation.w;

        // Apply coordinate conversion
        CoordinateConversion::OpenXRToRED(oxrX, oxrY, oxrZ, outX, outY, outZ);
        CoordinateConversion::OpenXRQuatToRED(oxrQX, oxrQY, oxrQZ, oxrQW, outQX, outQY, outQZ, outQW);

        return true;
    }
    return false;
}

void VRSystem::SubmitFrame(ID3D12Resource* gameTexture, bool isLeftEye)
{
    int eyeIndex = isLeftEye ? 0 : 1;

    if (m_impl->m_swapchains[eyeIndex].handle == XR_NULL_HANDLE) return;
    if (!gameTexture) return;

    // Acquire swapchain image
    uint32_t imageIndex;
    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (XR_FAILED(xrAcquireSwapchainImage(m_impl->m_swapchains[eyeIndex].handle, &acquireInfo, &imageIndex))) {
        Utils::LogWarn("OpenXR: Failed to acquire swapchain image");
        return;
    }

    // Wait for image to be ready
    XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(m_impl->m_swapchains[eyeIndex].handle, &waitInfo))) {
        Utils::LogWarn("OpenXR: Failed to wait for swapchain image");
        return;
    }

    // Copy game texture to swapchain image
    ID3D12Resource* destTexture = m_impl->m_swapchains[eyeIndex].images[imageIndex].texture;
    m_impl->CopyTexture(gameTexture, destTexture);

    // Release swapchain image
    XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage(m_impl->m_swapchains[eyeIndex].handle, &releaseInfo);

    // After right eye, end the frame
    if (!isLeftEye && m_impl->m_frameInProgress) {
        // Build projection views
        for (int i = 0; i < 2; i++) {
            m_impl->m_projectionViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            m_impl->m_projectionViews[i].pose = m_impl->m_views[i].pose;
            m_impl->m_projectionViews[i].fov = m_impl->m_views[i].fov;
            m_impl->m_projectionViews[i].subImage.swapchain = m_impl->m_swapchains[i].handle;
            m_impl->m_projectionViews[i].subImage.imageRect.offset = { 0, 0 };
            m_impl->m_projectionViews[i].subImage.imageRect.extent = {
                m_impl->m_swapchains[i].width,
                m_impl->m_swapchains[i].height
            };
            m_impl->m_projectionViews[i].subImage.imageArrayIndex = 0;
        }

        XrCompositionLayerProjection projectionLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        projectionLayer.space = m_impl->m_appSpace;
        projectionLayer.viewCount = 2;
        projectionLayer.views = m_impl->m_projectionViews.data();

        const XrCompositionLayerBaseHeader* layers[] = { (XrCompositionLayerBaseHeader*)&projectionLayer };

        XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
        endInfo.displayTime = m_impl->m_frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = m_impl->m_frameState.shouldRender ? 1 : 0;
        endInfo.layers = m_impl->m_frameState.shouldRender ? layers : nullptr;

        xrEndFrame(m_impl->m_session, &endInfo);
        m_impl->m_frameInProgress = false;
    }
}
