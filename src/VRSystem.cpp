#include "VRSystem.hpp"
#include "Utils.hpp"
#include <vector>
#include <string>

// Windows / DirectX / OpenXR Headers
// Defined ONLY here to prevent pollution in other files
#include <windows.h>
#include <d3d12.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#pragma comment(lib, "d3d12.lib")

// The Actual Implementation Class
class VRSystem::Impl
{
public:
    bool m_initialized = false;
    
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_systemId = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_appSpace = XR_NULL_HANDLE;

    XrGraphicsBindingD3D12KHR m_graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    
    struct SwapchainInfo {
        XrSwapchain handle;
        int32_t width;
        int32_t height;
        std::vector<XrSwapchainImageD3D12KHR> images;
    } m_swapchain;

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
        if (XR_FAILED(xrGetSystem(m_instance, &systemInfo, &m_systemId))) return false;

        m_graphicsBinding.device = nullptr; 
        gameCommandQueue->GetDevice(IID_PPV_ARGS(&m_graphicsBinding.device));
        m_graphicsBinding.queue = gameCommandQueue;

        XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
        sessionInfo.next = &m_graphicsBinding;
        sessionInfo.systemId = m_systemId;

        if (XR_FAILED(xrCreateSession(m_instance, &sessionInfo, &m_session))) return false;

        XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
        return XR_SUCCEEDED(xrCreateReferenceSpace(m_session, &spaceInfo, &m_appSpace));
    }

    bool CreateSwapchain() {
        uint32_t viewCount;
        xrEnumerateViewConfigurationViews(m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
        std::vector<XrViewConfigurationView> views(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
        xrEnumerateViewConfigurationViews(m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, views.data());

        if (viewCount == 0) return false;

        XrSwapchainCreateInfo swapchainInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchainInfo.sampleCount = 1;
        swapchainInfo.width = views[0].recommendedImageRectWidth;
        swapchainInfo.height = views[0].recommendedImageRectHeight;
        swapchainInfo.arraySize = 1;
        swapchainInfo.faceCount = 1;
        swapchainInfo.mipCount = 1;

        if (XR_FAILED(xrCreateSwapchain(m_session, &swapchainInfo, &m_swapchain.handle))) return false;
        
        m_swapchain.width = swapchainInfo.width;
        m_swapchain.height = swapchainInfo.height;

        uint32_t imageCount;
        xrEnumerateSwapchainImages(m_swapchain.handle, 0, &imageCount, nullptr);
        m_swapchain.images.resize(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR });
        return XR_SUCCEEDED(xrEnumerateSwapchainImages(m_swapchain.handle, imageCount, &imageCount, (XrSwapchainImageBaseHeader*)m_swapchain.images.data()));
    }
};

// Interface Implementation (Redirects to Impl)

VRSystem::VRSystem() : m_impl(std::make_unique<Impl>())
{
}

VRSystem::~VRSystem()
{
    if (m_impl->m_swapchain.handle != XR_NULL_HANDLE) xrDestroySwapchain(m_impl->m_swapchain.handle);
    if (m_impl->m_session != XR_NULL_HANDLE) xrDestroySession(m_impl->m_session);
    if (m_impl->m_instance != XR_NULL_HANDLE) xrDestroyInstance(m_impl->m_instance);
}

bool VRSystem::Initialize(ID3D12CommandQueue* gameCommandQueue)
{
    if (m_impl->m_initialized) return true;

    if (!m_impl->CreateInstance()) {
        Utils::LogError("Failed to create OpenXR Instance");
        return false;
    }

    if (!gameCommandQueue) {
        Utils::LogWarn("OpenXR Instance created, waiting for Queue.");
        m_impl->m_initialized = true; 
        return true;
    }

    if (!m_impl->CreateSession(gameCommandQueue)) {
        Utils::LogError("Failed to create OpenXR Session");
        return false;
    }

    if (!m_impl->CreateSwapchain()) {
        Utils::LogError("Failed to create OpenXR Swapchain");
        return false;
    }

    m_impl->m_initialized = true;
    Utils::LogInfo("OpenXR Initialized Successfully");
    return true;
}

bool VRSystem::Update(float& outX, float& outY, float& outZ, float& outQX, float& outQY, float& outQZ, float& outQW)
{
    if (m_impl->m_session == XR_NULL_HANDLE) return false;

    XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState frameState = { XR_TYPE_FRAME_STATE };
    if (XR_FAILED(xrWaitFrame(m_impl->m_session, &waitInfo, &frameState))) return false;

    XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
    if (XR_FAILED(xrBeginFrame(m_impl->m_session, &beginInfo))) return false;

    XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = frameState.predictedDisplayTime;
    locateInfo.space = m_impl->m_appSpace;

    XrViewState viewState = { XR_TYPE_VIEW_STATE };
    uint32_t viewCountOutput;
    XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    
    if (XR_SUCCEEDED(xrLocateViews(m_impl->m_session, &locateInfo, &viewState, 2, &viewCountOutput, views))) {
        outX = views[0].pose.position.x;
        outY = views[0].pose.position.y;
        outZ = views[0].pose.position.z;
        
        outQX = views[0].pose.orientation.x;
        outQY = views[0].pose.orientation.y;
        outQZ = views[0].pose.orientation.z;
        outQW = views[0].pose.orientation.w;
        return true;
    }
    return false;
}

void VRSystem::SubmitFrame(ID3D12Resource* gameTexture, bool isLeftEye)
{
    if (m_impl->m_swapchain.handle == XR_NULL_HANDLE) return;

    uint32_t imageIndex;
    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    xrAcquireSwapchainImage(m_impl->m_swapchain.handle, &acquireInfo, &imageIndex);

    XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(m_impl->m_swapchain.handle, &waitInfo);

    // TODO: Copy texture logic

    XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage(m_impl->m_swapchain.handle, &releaseInfo);

    if (!isLeftEye) {
        // Just end frame for now
        XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
        endInfo.displayTime = 0; 
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = 0; 
        xrEndFrame(m_impl->m_session, &endInfo);
    }
}