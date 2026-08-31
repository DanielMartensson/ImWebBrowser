// Vulkan presentation backend for ImWebBrowser.
//
// Imports WPE WebKit's dma-buf frames as sampled VkImages (external-memory +
// DRM-modifier extensions) and presents them through a swapchain using Dear
// ImGui's Vulkan renderer — whose SPIR-V shaders are precompiled inside
// imgui_impl_vulkan.cpp, so builds need no shader toolchain.

#include "vk_backend.hpp"

#include <SDL3/SDL_vulkan.h>
#include <backends/imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal DRM format constants (avoids a libdrm dependency).
// ---------------------------------------------------------------------------
#define IMWB_FOURCC(a, b, c, d)                                                                    \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define IMWB_FMT_ARGB8888 IMWB_FOURCC('A', 'R', '2', '4')
#define IMWB_FMT_XRGB8888 IMWB_FOURCC('X', 'R', '2', '4')
#define IMWB_FMT_ABGR8888 IMWB_FOURCC('A', 'B', '2', '4')
#define IMWB_FMT_XBGR8888 IMWB_FOURCC('X', 'B', '2', '4')
#define IMWB_MOD_LINEAR 0ULL
#define IMWB_MOD_INVALID (1ULL << 61) // fourcc_mod_code(NONE, DRM_FORMAT_RESERVED)

#define VK_CHECK(call)                                                                             \
    do {                                                                                           \
        VkResult _r = (call);                                                                      \
        if (_r != VK_SUCCESS) {                                                                     \
            std::fprintf(stderr, "[vk] %s failed (%d) at %d\n", #call, int(_r), __LINE__);          \
            return false;                                                                           \
        }                                                                                           \
    } while (0)

static VkFormat fourccToVk(uint32_t fourcc)
{
    switch (fourcc) {
    case IMWB_FMT_ARGB8888:
    case IMWB_FMT_XRGB8888: return VK_FORMAT_B8G8R8A8_UNORM;
    case IMWB_FMT_ABGR8888:
    case IMWB_FMT_XBGR8888: return VK_FORMAT_R8G8B8A8_UNORM;
    default: return VK_FORMAT_UNDEFINED;
    }
}

struct FrameResource {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
};

struct VulkanPresent::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    int drmRenderMinor = -1;  // DRM render node of `physical` (-1 unknown)

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    std::vector<VkFramebuffer> framebuffers;

    std::vector<VkFence> submitFences;     // per swapchain image
    std::vector<VkSemaphore> acquireSems;  // per frame in flight
    std::vector<VkSemaphore> renderSems;   // per swapchain image
    uint32_t framesInFlight = 2;
    uint32_t syncIndex = 0;
    uint32_t imageIndex = 0;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    std::vector<FrameResource> alive;  // imported web frames (bounded)

    bool createInstance(SDL_Window* window);
    bool pickAndCreateDevice();
    bool createRenderPass();
    bool createSwapchainObjects();
    bool createFramebuffers();
    void destroySwapchainObjects();
    void destroyFrame(FrameResource& fr);
};

// One-shot debug dump (IMWB_VKDUMP); defined at the end of this file.
static void dumpSwapchainImage(VkDevice dev, VkPhysicalDevice phys, VkQueue q,
                               VkCommandPool pool, VkImage src, uint32_t w, uint32_t h);

// ---------------------------------------------------------------------------
// Instance / device / swapchain
// ---------------------------------------------------------------------------
bool VulkanPresent::Impl::createInstance(SDL_Window* window)
{
    uint32_t count = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!sdlExts) {
        std::fprintf(stderr, "[vk] SDL instance extensions: %s\n", SDL_GetError());
        return false;
    }
    std::vector<const char*> exts(sdlExts, sdlExts + count);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "ImWebBrowser";
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = count;
    ci.ppEnabledExtensionNames = exts.data();
    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));

    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        std::fprintf(stderr, "[vk] SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

bool VulkanPresent::Impl::pickAndCreateDevice()
{
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    if (!n)
        return false;
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance, &n, devs.data());

    // Prefer a discrete GPU when one is presentable.
    for (int pass = 1; pass >= 0 && !physical; pass--) {
        for (VkPhysicalDevice pd : devs) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(pd, &props);
            bool discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (pass == 1 && !discrete)
                continue;
            uint32_t qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
            std::vector<VkQueueFamilyProperties> qs(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qs.data());
            for (uint32_t i = 0; i < qn && physical == VK_NULL_HANDLE; i++) {
                VkBool32 presentable = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &presentable);
                if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentable) {
                    physical = pd;
                    queueFamily = i;
                }
            }
        }
    }
    if (!physical) {
        std::fprintf(stderr, "[vk] no graphics+present queue found\n");
        return false;
    }

    // DRM render-node minor of the chosen device, so the app can pin WPE's
    // EGL display to the same GPU (cross-vendor dma-buf tiling is unsafe).
    VkPhysicalDeviceDrmPropertiesEXT drm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    p2.pNext = &drm;
    vkGetPhysicalDeviceProperties2(physical, &p2);
    drmRenderMinor = drm.hasRender ? int(drm.renderMinor) : -1;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);
    std::fprintf(stderr, "[vk] device: %s (render node minor %d)\n", props.deviceName,
                 drmRenderMinor);

    float prio = 1.f;
    VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qc.queueFamilyIndex = queueFamily;
    qc.queueCount = 1;
    qc.pQueuePriorities = &prio;

    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                             VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                             VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
                             VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME};
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dc.queueCreateInfoCount = 1;
    dc.pQueueCreateInfos = &qc;
    dc.enabledExtensionCount = 4;
    dc.ppEnabledExtensionNames = devExts;
    VK_CHECK(vkCreateDevice(physical, &dc, nullptr, &device));
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    return true;
}

bool VulkanPresent::Impl::createRenderPass()
{
    VkAttachmentDescription color{};
    color.format = swapFormat;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = 1;
    ci.pAttachments = &color;
    ci.subpassCount = 1;
    ci.pSubpasses = &sp;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(device, &ci, nullptr, &renderPass));
    return true;
}

bool VulkanPresent::Impl::createSwapchainObjects()
{
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));

    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fn, fmts.data());
    VkSurfaceFormatKHR chosen = fmts[0];
    for (auto& f : fmts)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    swapFormat = chosen.format;

    extent = caps.currentExtent;
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount)
        count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface;
    ci.minImageCount = count;
    ci.imageFormat = swapFormat;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // vsync, guaranteed available
    ci.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain));

    uint32_t in = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &in, nullptr);
    swapImages.resize(in);
    vkGetSwapchainImagesKHR(device, swapchain, &in, swapImages.data());

    // Per-image sync objects.
    submitFences.resize(in);
    renderSems.resize(in);
    acquireSems.resize(framesInFlight);
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < in; i++) {
        VK_CHECK(vkCreateFence(device, &fi, nullptr, &submitFences[i]));
        VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &renderSems[i]));
    }
    for (uint32_t i = 0; i < framesInFlight; i++)
        VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &acquireSems[i]));

    VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pc.queueFamilyIndex = queueFamily;
    VK_CHECK(vkCreateCommandPool(device, &pc, nullptr, &cmdPool));
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = cmdPool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device, &ca, &cmd));
    return createFramebuffers();
}

bool VulkanPresent::Impl::createFramebuffers()
{
    swapViews.resize(swapImages.size());
    framebuffers.resize(swapImages.size());
    for (size_t i = 0; i < swapImages.size(); i++) {
        VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        v.image = swapImages[i];
        v.viewType = VK_IMAGE_VIEW_TYPE_2D;
        v.format = swapFormat;
        v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &v, nullptr, &swapViews[i]));

        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &swapViews[i];
        fb.width = extent.width;
        fb.height = extent.height;
        fb.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fb, nullptr, &framebuffers[i]));
    }
    return true;
}

void VulkanPresent::Impl::destroySwapchainObjects()
{
    for (VkFramebuffer fb : framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    for (VkImageView v : swapViews)
        vkDestroyImageView(device, v, nullptr);
    for (VkFence f : submitFences)
        vkDestroyFence(device, f, nullptr);
    for (VkSemaphore s : acquireSems)
        vkDestroySemaphore(device, s, nullptr);
    for (VkSemaphore s : renderSems)
        vkDestroySemaphore(device, s, nullptr);
    if (cmd)
        vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    if (cmdPool)
        vkDestroyCommandPool(device, cmdPool, nullptr);
    framebuffers.clear();
    swapViews.clear();
    swapImages.clear();
    submitFences.clear();
    acquireSems.clear();
    renderSems.clear();
    cmd = VK_NULL_HANDLE;
    cmdPool = VK_NULL_HANDLE;
    if (swapchain)
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Frame import
// ---------------------------------------------------------------------------
static bool importFrameInternal(VulkanPresent::Impl& d, const VkDmabufFrame& f, FrameResource& out)
{
    VkFormat format = fourccToVk(f.fourcc);
    if (format == VK_FORMAT_UNDEFINED || f.planeCount < 1 || f.planes[0].fd < 0) {
        std::fprintf(stderr, "[vk] unusable dmabuf (fourcc %.4s planes %u)\n",
                     (const char*)&f.fourcc, f.planeCount);
        return false;
    }

    const bool modifiered =
        f.modifier != IMWB_MOD_INVALID && f.modifier != IMWB_MOD_LINEAR &&
        !std::getenv("IMWB_VKLINEAR");  // diagnostic: force stride-based linear import

    // The Haswell-class Mesa driver ("hasvk") advertises incomplete support;
    // refuse modifier imports rather than risk driver crashes.
    static int haveModExt = -1;
    if (modifiered && haveModExt < 0) {
        uint32_t n = 0;
        vkEnumerateDeviceExtensionProperties(d.physical, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> ex(n);
        vkEnumerateDeviceExtensionProperties(d.physical, nullptr, &n, ex.data());
        haveModExt = 0;
        for (auto& e : ex)
            if (!std::strcmp(e.extensionName, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME))
                haveModExt = 1;
        std::fprintf(stderr, "[vk] drm_format_modifier ext: %d\n", haveModExt);
    }
    if (modifiered && !haveModExt) {
        std::fprintf(stderr,
                     "[vk] driver lacks VK_EXT_image_drm_format_modifier; "
                     "cannot import modifier frame (%llu)\n",
                     (unsigned long long)f.modifier);
        return false;
    }

    VkExternalMemoryImageCreateInfo ext{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ext.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkSubresourceLayout planeLayouts[4]{};
    for (uint32_t i = 0; i < f.planeCount; i++) {
        planeLayouts[i].offset = f.planes[i].offset;
        planeLayouts[i].rowPitch = f.planes[i].stride;
    }
    VkImageDrmFormatModifierExplicitCreateInfoEXT mod{
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
    mod.drmFormatModifier = f.modifier;
    mod.drmFormatModifierPlaneCount = f.planeCount;
    mod.pPlaneLayouts = planeLayouts;

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.pNext = &ext;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {f.width, f.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = modifiered ? VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT : VK_IMAGE_TILING_LINEAR;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (modifiered)
        ext.pNext = &mod;

    VkImage image = VK_NULL_HANDLE;
    if (vkCreateImage(d.device, &ici, nullptr, &image) != VK_SUCCESS) {
        static int dbgFails = 0;
        if (dbgFails++ < 3)
            std::fprintf(stderr, "[vk] vkCreateImage %ux%u mod=%llu failed (%.4s)\n", f.width,
                         f.height, (unsigned long long)f.modifier, (const char*)&f.fourcc);
        std::fprintf(stderr, "[vk] vkCreateImage %ux%u mod=%llu failed\n", f.width, f.height,
                     (unsigned long long)f.modifier);
        return false;
    }

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(d.device, image, &req);

    VkImportMemoryFdInfoKHR imp{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
    imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    imp.fd = f.planes[0].fd;  // ownership moves to Vulkan on success

    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(d.physical, &mp);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.pNext = &imp;
    ai.memoryTypeIndex = UINT32_MAX;
    for (uint32_t t = 0; t < mp.memoryTypeCount && ai.memoryTypeIndex == UINT32_MAX; t++)
        if (req.memoryTypeBits & (1u << t))
            ai.memoryTypeIndex = t;

    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkResult ra = ai.memoryTypeIndex == UINT32_MAX ? VK_ERROR_OUT_OF_DEVICE_MEMORY
                                                   : vkAllocateMemory(d.device, &ai, nullptr, &mem);
    VkResult rb = ra == VK_SUCCESS ? vkBindImageMemory(d.device, image, mem, 0) : VK_ERROR_UNKNOWN;
    if (ra != VK_SUCCESS || rb != VK_SUCCESS) {
        std::fprintf(stderr, "[vk] dmabuf memory import/bind failed\n");
        close(f.planes[0].fd);  // not consumed on failure
        vkDestroyImage(d.device, image, nullptr);
        return false;
    }

    // Single-plane formats always address subresources via COLOR, even with
    // DRM-modifier tiling; MEMORY_PLANE_* aspects are only for multi-plane
    // formats (YUV).
    const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageView view = VK_NULL_HANDLE;
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v.image = image;
    v.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v.format = format;
    v.subresourceRange = {aspect, 0, 1, 0, 1};
    if (vkCreateImageView(d.device, &v, nullptr, &view) != VK_SUCCESS) {
        std::fprintf(stderr, "[vk] image view creation failed\n");
        vkDestroyImage(d.device, image, nullptr);
        vkFreeMemory(d.device, mem, nullptr);
        return false;
    }

    // UNDEFINED -> SHADER_READ_ONLY_OPTIMAL via one-shot command buffer.
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = d.cmdPool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VkCommandBuffer c = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(d.device, &ca, &c);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(c, &bi);

    VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bar.image = image;
    bar.subresourceRange = {aspect, 0, 1, 0, 1};
    bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    vkEndCommandBuffer(c);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &c;
    vkQueueSubmit(d.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(d.queue);
    vkFreeCommandBuffers(d.device, d.cmdPool, 1, &c);

    out.image = image;
    out.memory = mem;
    out.view = view;
    out.descriptor = ImGui_ImplVulkan_AddTexture(d.sampler, view,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!out.descriptor) {
        std::fprintf(stderr, "[vk] ImGui_ImplVulkan_AddTexture failed (pool exhausted?)\n");
        vkDestroyImageView(d.device, view, nullptr);
        vkDestroyImage(d.device, image, nullptr);
        vkFreeMemory(d.device, mem, nullptr);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool VulkanPresent::init(SDL_Window* window, int width, int height)
{
    d = new Impl;
    if (!d->createInstance(window) || !d->pickAndCreateDevice())
        return false;

    VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sc.magFilter = VK_FILTER_LINEAR;
    sc.minFilter = VK_FILTER_LINEAR;
    sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(d->device, &sc, nullptr, &d->sampler) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = 32;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(d->device, &dp, nullptr, &d->descPool) != VK_SUCCESS)
        return false;

    if (!d->createRenderPass() || !d->createSwapchainObjects())
        return false;
    (void)width;
    (void)height;
    std::fprintf(stderr, "[vk] present backend ready (%ux%u, %s)\n", d->extent.width,
                 d->extent.height,
                 d->swapFormat == VK_FORMAT_B8G8R8A8_UNORM ? "BGRA8" : "RGBA8");
    return true;
}

void VulkanPresent::resize(int width, int height)
{
    if (!d || !d->swapchain)
        return;
    vkDeviceWaitIdle(d->device);
    d->destroySwapchainObjects();
    d->extent.width = uint32_t(width > 0 ? width : 1);
    d->extent.height = uint32_t(height > 0 ? height : 1);
    // Recreate with the surface's own constraints.
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d->physical, d->surface, &caps);
    d->extent.width = std::clamp(d->extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    d->extent.height = std::clamp(d->extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    d->createSwapchainObjects();
}

ImTextureID VulkanPresent::importFrame(VkDmabufFrame& frame)
{
    // Bound ring of live imports; evict oldest once we exceed four. A device
    // idle wait is conservative but simple: imports are infrequent compared
    // to presents (only when WebKit rotates buffers).
    while (d->alive.size() >= 4) {
        vkDeviceWaitIdle(d->device);
        d->destroyFrame(d->alive.front());
        d->alive.erase(d->alive.begin());
    }

    FrameResource fr;
    ImTextureID out = importFrameInternal(*d, frame, fr) ? (ImTextureID)(intptr_t)(VkDescriptorSet)fr.descriptor
                                                         : ImTextureID(0);
    frame.consumed = true;  // success consumed the fds; failure invalidated them
    if (out)
        d->alive.push_back(fr);
    return out;
}

void VulkanPresent::Impl::destroyFrame(FrameResource& fr)
{
    if (fr.descriptor)
        ImGui_ImplVulkan_RemoveTexture(fr.descriptor);
    if (fr.view)
        vkDestroyImageView(device, fr.view, nullptr);
    if (fr.image)
        vkDestroyImage(device, fr.image, nullptr);
    if (fr.memory)
        vkFreeMemory(device, fr.memory, nullptr);
    fr = {};
}

bool VulkanPresent::drawFrame(int width, int height)
{
    Impl& D = *d;
    uint32_t fi = D.syncIndex++ % D.framesInFlight;
    (void)width;
    (void)height;

    vkWaitForFences(D.device, 1, &D.submitFences[D.imageIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(D.device, 1, &D.submitFences[D.imageIndex]);

    uint32_t idx = 0;
    VkResult r = vkAcquireNextImageKHR(D.device, D.swapchain, UINT64_MAX,
                                       D.acquireSems[fi], VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
        return true;  // stale: caller resizes and retries next frame
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        return false;
    D.imageIndex = idx;

    vkResetCommandBuffer(D.cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(D.cmd, &bi) != VK_SUCCESS)
        return false;

    VkClearValue clear{{{0.f, 0.f, 0.f, 1.f}}};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = D.renderPass;
    rp.framebuffer = D.framebuffers[idx];
    rp.renderArea.extent = D.extent;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(D.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), D.cmd);

    vkCmdEndRenderPass(D.cmd);
    if (vkEndCommandBuffer(D.cmd) != VK_SUCCESS)
        return false;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &D.acquireSems[fi];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &D.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &D.renderSems[idx];
    if (vkQueueSubmit(D.queue, 1, &si, D.submitFences[idx]) != VK_SUCCESS)
        return false;

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &D.renderSems[idx];
    pi.swapchainCount = 1;
    pi.pSwapchains = &D.swapchain;
    pi.pImageIndices = &idx;
    r = vkQueuePresentKHR(D.queue, &pi);
    static const bool kDump = std::getenv("IMWB_VKDUMP") != nullptr;
    static int dbgFrames = 0;
    ++dbgFrames;
    if ((r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) && kDump) {
        static bool dumped = false;
        if (!dumped && dbgFrames > 90) {  // let the page load first
            dumped = true;
            dumpSwapchainImage(D.device, D.physical, D.queue, D.cmdPool, D.swapImages[idx],
                               D.extent.width, D.extent.height);
        }
    }
    return r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
}

void VulkanPresent::shutdown()
{
    if (!d)
        return;
    vkDeviceWaitIdle(d->device);
    for (auto& fr : d->alive)
        d->destroyFrame(fr);
    d->alive.clear();
    d->destroySwapchainObjects();
    if (d->renderPass)
        vkDestroyRenderPass(d->device, d->renderPass, nullptr);
    if (d->descPool)
        vkDestroyDescriptorPool(d->device, d->descPool, nullptr);
    if (d->sampler)
        vkDestroySampler(d->device, d->sampler, nullptr);
    if (d->device)
        vkDestroyDevice(d->device, nullptr);
    if (d->surface)
        vkDestroySurfaceKHR(d->instance, d->surface, nullptr);
    if (d->instance)
        vkDestroyInstance(d->instance, nullptr);
    delete d;
    d = nullptr;
}

VulkanPresent::Handles VulkanPresent::handles() const
{
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(d->device, d->swapchain, &n, nullptr);
    return {d->instance, d->physical, d->device, d->queueFamily, d->queue, n, d->drmRenderMinor};
}

struct VkRenderPass_T* VulkanPresent::renderPass() const
{
    return (struct VkRenderPass_T*)d->renderPass;
}

// One-shot debug dump (IMWB_VKDUMP): copies the last presented swapchain
// image to a linear staging image and writes /tmp/opencode/vk-dump.ppm.
static void dumpSwapchainImage(VkDevice dev, VkPhysicalDevice phys, VkQueue q,
                               VkCommandPool pool, VkImage src, uint32_t w, uint32_t h)
{
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_B8G8R8A8_UNORM;
    ii.extent = {w, h, 1};
    ii.mipLevels = ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_LINEAR;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage dst;
    if (vkCreateImage(dev, &ii, nullptr, &dst) != VK_SUCCESS)
        return;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, dst, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    VkPhysicalDeviceMemoryProperties s_mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &s_mp);
    ai.memoryTypeIndex = [&] {
        for (uint32_t i = 0; i < s_mp.memoryTypeCount; i++)
            if ((s_mp.memoryTypes[i].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT) &&
                (mr.memoryTypeBits & (1u << i)))
                return i;
        for (uint32_t i = 0; i < s_mp.memoryTypeCount; i++)
            if ((s_mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (mr.memoryTypeBits & (1u << i)))
                return i;
        return 0u;
    }();
    VkDeviceMemory mem;
    if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS) {
        vkDestroyImage(dev, dst, nullptr);
        return;
    }
    vkBindImageMemory(dev, dst, mem, 0);

    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev, &ca, &cb);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cb, &bi);
    auto barrier = [&](VkImage img, VkImageLayout oldL, VkImageLayout newL, VkAccessFlags srcA,
                       VkAccessFlags dstA) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcAccessMask = srcA;
        b.dstAccessMask = dstA;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    barrier(src, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            0, VK_ACCESS_TRANSFER_READ_BIT);
    barrier(dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy region{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0},
                       {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {w, h, 1}};
    vkCmdCopyImage(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(q);

    VkImageSubresource sub{VK_IMAGE_ASPECT_COLOR_BIT};
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(dev, dst, &sub, &layout);
    void* data = nullptr;
    if (vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, &data) == VK_SUCCESS) {
        FILE* f = fopen("/tmp/opencode/vk-dump.ppm", "wb");
        if (f) {
            std::fprintf(f, "P6\n%u %u\n255\n", w, h);
            const uint8_t* row = (const uint8_t*)data + layout.offset;
            std::vector<uint8_t> rgb(size_t(w) * 3);
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    rgb[x * 3 + 0] = row[size_t(x) * 4 + 2];
                    rgb[x * 3 + 1] = row[size_t(x) * 4 + 1];
                    rgb[x * 3 + 2] = row[size_t(x) * 4 + 0];
                }
                fwrite(rgb.data(), 1, rgb.size(), f);
                row += layout.rowPitch;
            }
            fclose(f);
            std::fprintf(stderr, "[vk] wrote vk-dump.ppm\n");
        }
        vkUnmapMemory(dev, mem);
    }
    vkFreeCommandBuffers(dev, pool, 1, &cb);
    vkFreeMemory(dev, mem, nullptr);
    vkDestroyImage(dev, dst, nullptr);
}
