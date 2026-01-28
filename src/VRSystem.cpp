#include "VRSystem.hpp"
#include "ThreadSafe.hpp"
#include "Utils.hpp"
#include <vector>
#include <string>
#include <cmath>

// Windows / DirectX / OpenXR Headers
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#include <algorithm>

// Coordinate system conversion
// REDengine uses: X-right, Y-forward, Z-up (left-handed)
// OpenXR uses: X-right, Y-up, Z-back (right-handed)
namespace CoordinateConversion
{
    inline void OpenXRToRED(float oxrX, float oxrY, float oxrZ,
                            float& redX, float& redY, float& redZ)
    {
        redX = oxrX;
        redY = -oxrZ;
        redZ = oxrY;
    }

    inline void OpenXRQuatToRED(float oxrX, float oxrY, float oxrZ, float oxrW,
                                 float& redI, float& redJ, float& redK, float& redR)
    {
        redI = oxrX;
        redJ = -oxrZ;
        redK = oxrY;
        redR = oxrW;
    }
}

// OpenXR session states
enum class SessionState
{
    Unknown,
    Idle,
    Ready,
    Synchronized,
    Visible,
    Focused,
    Stopping,
    LossPending,
    Exiting
};

// The Actual Implementation Class
class VRSystem::Impl
{
public:
    // Thread safety
    mutable std::mutex m_mutex;
    ThreadSafe::Flag m_initialized{false};
    ThreadSafe::Flag m_sessionReady{false};
    ThreadSafe::Flag m_frameInProgress{false};
    std::atomic<SessionState> m_sessionState{SessionState::Unknown};

    // OpenXR handles
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_systemId = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_appSpace = XR_NULL_HANDLE;

    XrGraphicsBindingD3D12KHR m_graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};

    // D3D12 resources (using ComPtr for automatic cleanup)
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    std::atomic<UINT64> m_fenceValue{0};

    // Frame state
    XrFrameState m_frameState{XR_TYPE_FRAME_STATE};

    struct SwapchainInfo {
        XrSwapchain handle = XR_NULL_HANDLE;
        int32_t width = 0;
        int32_t height = 0;
        std::vector<XrSwapchainImageD3D12KHR> images;
    };

    SwapchainInfo m_swapchains[2];

    std::vector<XrViewConfigurationView> m_viewConfigs;
    std::vector<XrView> m_views;
    std::vector<XrCompositionLayerProjectionView> m_projectionViews;

    bool CreateInstance()
    {
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

        XrResult result = xrCreateInstance(&createInfo, &m_instance);
        if (XR_FAILED(result))
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "OpenXR: xrCreateInstance failed with code %d", result);
            Utils::LogError(msg);
            return false;
        }
        return true;
    }

    bool CreateSession(ID3D12CommandQueue* gameCommandQueue)
    {
        if (!gameCommandQueue)
        {
            Utils::LogError("OpenXR: CreateSession called with null command queue");
            return false;
        }

        XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        XrResult result = xrGetSystem(m_instance, &systemInfo, &m_systemId);
        if (XR_FAILED(result))
        {
            Utils::LogError("OpenXR: No HMD found! Is your headset connected?");
            return false;
        }

        // Get device from command queue
        if (FAILED(gameCommandQueue->GetDevice(IID_PPV_ARGS(&m_device))))
        {
            Utils::LogError("OpenXR: Failed to get D3D12 device from command queue");
            return false;
        }

        m_commandQueue = gameCommandQueue;
        m_commandQueue->AddRef(); // Take ownership

        m_graphicsBinding.device = m_device.Get();
        m_graphicsBinding.queue = gameCommandQueue;

        XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
        sessionInfo.next = &m_graphicsBinding;
        sessionInfo.systemId = m_systemId;

        result = xrCreateSession(m_instance, &sessionInfo, &m_session);
        if (XR_FAILED(result))
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "OpenXR: xrCreateSession failed with code %d", result);
            Utils::LogError(msg);
            return false;
        }

        // Try STAGE space first, fall back to LOCAL
        XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;

        result = xrCreateReferenceSpace(m_session, &spaceInfo, &m_appSpace);
        if (XR_FAILED(result))
        {
            Utils::LogWarn("OpenXR: STAGE space not available, trying LOCAL");
            spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            result = xrCreateReferenceSpace(m_session, &spaceInfo, &m_appSpace);
            if (XR_FAILED(result))
            {
                Utils::LogError("OpenXR: Failed to create reference space");
                return false;
            }
        }

        return true;
    }

    bool CreateSwapchains()
    {
        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews(m_instance, m_systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);

        if (viewCount != 2)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "OpenXR: Expected 2 views, got %u", viewCount);
            Utils::LogError(msg);
            return false;
        }

        m_viewConfigs.resize(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
        xrEnumerateViewConfigurationViews(m_instance, m_systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, m_viewConfigs.data());

        m_views.resize(viewCount, { XR_TYPE_VIEW });
        m_projectionViews.resize(viewCount, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW });

        for (uint32_t i = 0; i < viewCount; i++)
        {
            XrSwapchainCreateInfo swapchainInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
            swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            swapchainInfo.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapchainInfo.sampleCount = 1;
            swapchainInfo.width = m_viewConfigs[i].recommendedImageRectWidth;
            swapchainInfo.height = m_viewConfigs[i].recommendedImageRectHeight;
            swapchainInfo.arraySize = 1;
            swapchainInfo.faceCount = 1;
            swapchainInfo.mipCount = 1;

            XrResult result = xrCreateSwapchain(m_session, &swapchainInfo, &m_swapchains[i].handle);
            if (XR_FAILED(result))
            {
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
            snprintf(msg, sizeof(msg), "OpenXR: Swapchain %u: %dx%d (%u images)",
                     i, m_swapchains[i].width, m_swapchains[i].height, imageCount);
            Utils::LogInfo(msg);
        }

        return true;
    }

    bool CreateD3D12Resources()
    {
        if (!m_device)
        {
            Utils::LogError("D3D12: No device available");
            return false;
        }

        if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandAllocator))))
        {
            Utils::LogError("D3D12: Failed to create command allocator");
            return false;
        }

        if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList))))
        {
            Utils::LogError("D3D12: Failed to create command list");
            return false;
        }
        m_commandList->Close();

        if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
        {
            Utils::LogError("D3D12: Failed to create fence");
            return false;
        }

        m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent)
        {
            Utils::LogError("D3D12: Failed to create fence event");
            return false;
        }

        Utils::LogInfo("D3D12: Copy resources created");
        return true;
    }

    bool WaitForGPU()
    {
        if (!m_fence || !m_commandQueue) return false;

        UINT64 fenceValue = m_fenceValue.fetch_add(1) + 1;
        HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fenceValue);
        if (FAILED(hr)) return false;

        if (m_fence->GetCompletedValue() < fenceValue)
        {
            hr = m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
            if (FAILED(hr)) return false;

            DWORD timeout = VRConfig::GetGPUWaitTimeout();
            DWORD result = WaitForSingleObject(m_fenceEvent, timeout);

            if (result == WAIT_TIMEOUT)
            {
                Utils::LogWarn("D3D12: GPU wait timed out");
                return false;
            }
            else if (result != WAIT_OBJECT_0)
            {
                Utils::LogError("D3D12: GPU wait failed");
                return false;
            }
        }

        return true;
    }

    void CopyTexture(ID3D12Resource* source, ID3D12Resource* dest)
    {
        if (!source || !dest || !m_commandList || !m_commandAllocator) return;

        HRESULT hr = m_commandAllocator->Reset();
        if (FAILED(hr)) return;

        hr = m_commandList->Reset(m_commandAllocator.Get(), nullptr);
        if (FAILED(hr)) return;

        D3D12_RESOURCE_BARRIER barriers[2] = {};

        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = source;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = dest;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandList->ResourceBarrier(2, barriers);

        D3D12_RESOURCE_DESC srcDesc = source->GetDesc();
        D3D12_RESOURCE_DESC dstDesc = dest->GetDesc();

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = source;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = dest;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_BOX srcBox = {};
        srcBox.right = static_cast<UINT>(std::min(srcDesc.Width, dstDesc.Width));
        srcBox.bottom = std::min(srcDesc.Height, dstDesc.Height);
        srcBox.back = 1;

        m_commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);

        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        m_commandList->ResourceBarrier(2, barriers);
        m_commandList->Close();

        ID3D12CommandList* lists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(1, lists);

        WaitForGPU();
    }

    void HandleSessionStateChange(XrSessionState newState)
    {
        switch (newState)
        {
        case XR_SESSION_STATE_IDLE:
            m_sessionState.store(SessionState::Idle);
            Utils::LogInfo("OpenXR: Session IDLE");
            break;

        case XR_SESSION_STATE_READY:
        {
            m_sessionState.store(SessionState::Ready);
            Utils::LogInfo("OpenXR: Session READY - Beginning session");

            XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
            beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

            XrResult result = xrBeginSession(m_session, &beginInfo);
            if (XR_FAILED(result))
            {
                Utils::LogError("OpenXR: Failed to begin session");
            }
            break;
        }

        case XR_SESSION_STATE_SYNCHRONIZED:
            m_sessionState.store(SessionState::Synchronized);
            Utils::LogInfo("OpenXR: Session SYNCHRONIZED");
            break;

        case XR_SESSION_STATE_VISIBLE:
            m_sessionState.store(SessionState::Visible);
            Utils::LogInfo("OpenXR: Session VISIBLE");
            break;

        case XR_SESSION_STATE_FOCUSED:
            m_sessionState.store(SessionState::Focused);
            Utils::LogInfo("OpenXR: Session FOCUSED");
            break;

        case XR_SESSION_STATE_STOPPING:
        {
            m_sessionState.store(SessionState::Stopping);
            Utils::LogInfo("OpenXR: Session STOPPING");

            XrResult result = xrEndSession(m_session);
            if (XR_FAILED(result))
            {
                Utils::LogWarn("OpenXR: Failed to end session gracefully");
            }
            break;
        }

        case XR_SESSION_STATE_LOSS_PENDING:
            m_sessionState.store(SessionState::LossPending);
            Utils::LogWarn("OpenXR: Session LOSS_PENDING - HMD may have disconnected");
            break;

        case XR_SESSION_STATE_EXITING:
            m_sessionState.store(SessionState::Exiting);
            Utils::LogInfo("OpenXR: Session EXITING");
            break;

        default:
            break;
        }
    }

    bool IsSessionRunning() const
    {
        SessionState state = m_sessionState.load();
        return state == SessionState::Synchronized ||
               state == SessionState::Visible ||
               state == SessionState::Focused;
    }
};

// Public Interface

VRSystem::VRSystem() : m_impl(std::make_unique<Impl>())
{
}

VRSystem::~VRSystem()
{
    ThreadSafe::Lock lock(m_impl->m_mutex);

    if (m_impl->m_fence)
    {
        m_impl->WaitForGPU();
    }

    for (int i = 0; i < 2; i++)
    {
        if (m_impl->m_swapchains[i].handle != XR_NULL_HANDLE)
        {
            xrDestroySwapchain(m_impl->m_swapchains[i].handle);
        }
    }

    if (m_impl->m_appSpace != XR_NULL_HANDLE) xrDestroySpace(m_impl->m_appSpace);

    // End session before destroying
    if (m_impl->m_session != XR_NULL_HANDLE)
    {
        if (m_impl->IsSessionRunning())
        {
            xrEndSession(m_impl->m_session);
        }
        xrDestroySession(m_impl->m_session);
    }

    if (m_impl->m_instance != XR_NULL_HANDLE) xrDestroyInstance(m_impl->m_instance);

    if (m_impl->m_fenceEvent) CloseHandle(m_impl->m_fenceEvent);
}

bool VRSystem::Initialize(ID3D12CommandQueue* gameCommandQueue)
{
    ThreadSafe::Lock lock(m_impl->m_mutex);

    // Phase 1: Create OpenXR instance
    if (!m_impl->m_initialized.load())
    {
        if (!m_impl->CreateInstance())
        {
            Utils::LogError("Failed to create OpenXR Instance");
            return false;
        }
        m_impl->m_initialized.store(true);
        Utils::LogInfo("OpenXR: Instance created");
    }

    // Phase 2: Create session (needs D3D12)
    if (!gameCommandQueue)
    {
        Utils::LogWarn("OpenXR: Waiting for D3D12 command queue...");
        return true;
    }

    if (m_impl->m_sessionReady.load())
    {
        return true;
    }

    if (!m_impl->CreateSession(gameCommandQueue))
    {
        Utils::LogError("Failed to create OpenXR Session");
        return false;
    }

    if (!m_impl->CreateSwapchains())
    {
        Utils::LogError("Failed to create OpenXR Swapchains");
        return false;
    }

    if (!m_impl->CreateD3D12Resources())
    {
        Utils::LogError("Failed to create D3D12 resources");
        return false;
    }

    m_impl->m_sessionReady.store(true);
    Utils::LogInfo("OpenXR: Fully initialized!");
    return true;
}

bool VRSystem::Update(float& outX, float& outY, float& outZ,
                      float& outQX, float& outQY, float& outQZ, float& outQW)
{
    if (!m_impl->m_session || !m_impl->m_sessionReady.load())
    {
        return false;
    }

    // Poll events
    XrEventDataBuffer eventBuffer = { XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(m_impl->m_instance, &eventBuffer) == XR_SUCCESS)
    {
        // Validate event type before casting
        if (eventBuffer.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
        {
            auto* stateEvent = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventBuffer);
            m_impl->HandleSessionStateChange(stateEvent->state);
        }
        eventBuffer = { XR_TYPE_EVENT_DATA_BUFFER };
    }

    // Only proceed if session is running
    if (!m_impl->IsSessionRunning())
    {
        return false;
    }

    // Wait for frame
    XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
    XrResult result = xrWaitFrame(m_impl->m_session, &waitInfo, &m_impl->m_frameState);
    if (XR_FAILED(result))
    {
        return false;
    }

    // Begin frame
    XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
    result = xrBeginFrame(m_impl->m_session, &beginInfo);
    if (XR_FAILED(result))
    {
        return false;
    }

    m_impl->m_frameInProgress.store(true);

    // Locate views
    XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = m_impl->m_frameState.predictedDisplayTime;
    locateInfo.space = m_impl->m_appSpace;

    XrViewState viewState = { XR_TYPE_VIEW_STATE };
    uint32_t viewCount = 2;

    result = xrLocateViews(m_impl->m_session, &locateInfo, &viewState, 2, &viewCount, m_impl->m_views.data());
    if (XR_SUCCEEDED(result))
    {
        float oxrX = m_impl->m_views[0].pose.position.x;
        float oxrY = m_impl->m_views[0].pose.position.y;
        float oxrZ = m_impl->m_views[0].pose.position.z;

        float oxrQX = m_impl->m_views[0].pose.orientation.x;
        float oxrQY = m_impl->m_views[0].pose.orientation.y;
        float oxrQZ = m_impl->m_views[0].pose.orientation.z;
        float oxrQW = m_impl->m_views[0].pose.orientation.w;

        CoordinateConversion::OpenXRToRED(oxrX, oxrY, oxrZ, outX, outY, outZ);
        CoordinateConversion::OpenXRQuatToRED(oxrQX, oxrQY, oxrQZ, oxrQW, outQX, outQY, outQZ, outQW);

        return true;
    }

    return false;
}

void VRSystem::SubmitFrame(ID3D12Resource* gameTexture, bool isLeftEye)
{
    if (!m_impl->m_sessionReady.load() || !m_impl->IsSessionRunning())
    {
        return;
    }

    int eyeIndex = isLeftEye ? 0 : 1;

    if (m_impl->m_swapchains[eyeIndex].handle == XR_NULL_HANDLE || !gameTexture)
    {
        return;
    }

    uint32_t imageIndex;
    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (XR_FAILED(xrAcquireSwapchainImage(m_impl->m_swapchains[eyeIndex].handle, &acquireInfo, &imageIndex)))
    {
        return;
    }

    XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = 100000000; // 100ms timeout instead of infinite
    if (XR_FAILED(xrWaitSwapchainImage(m_impl->m_swapchains[eyeIndex].handle, &waitInfo)))
    {
        Utils::LogWarn("OpenXR: Swapchain wait timed out");
        return;
    }

    ID3D12Resource* destTexture = m_impl->m_swapchains[eyeIndex].images[imageIndex].texture;
    m_impl->CopyTexture(gameTexture, destTexture);

    XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage(m_impl->m_swapchains[eyeIndex].handle, &releaseInfo);

    // End frame after right eye
    if (!isLeftEye && m_impl->m_frameInProgress.load())
    {
        for (int i = 0; i < 2; i++)
        {
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
        m_impl->m_frameInProgress.store(false);
    }
}
