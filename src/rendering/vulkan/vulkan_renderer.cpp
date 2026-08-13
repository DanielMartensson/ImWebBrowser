/* ImWebBrowser - Vulkan renderer. */

#include "rendering/vulkan/vulkan_renderer.h"

#include <vector>

#include <SDL3/SDL_vulkan.h>
#include <backends/imgui_impl_sdl3.h>

#include "logging/log.h"
#include "platform/window.h"
#include "rendering/egl_helpers.h"

namespace imwb {

namespace {

void vk_check(VkResult result)
{
    if (result != VK_SUCCESS)
        LOG_ERROR("VK: API call failed with VkResult %d", static_cast<int>(result));
}

} /* namespace */

bool VulkanRenderer::create_instance()
{
    uint32_t extension_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(m_window->sdl_window(), &extension_count, nullptr)) {
        LOG_ERROR("VK: SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
        return false;
    }
    std::vector<const char*> extensions(extension_count);
    SDL_Vulkan_GetInstanceExtensions(m_window->sdl_window(), &extension_count, extensions.data());

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "ImWebBrowser";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "ImWebBrowser";
    app_info.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instance_info.ppEnabledExtensionNames = extensions.data();

    const VkResult result = vkCreateInstance(&instance_info, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        LOG_ERROR("VK: vkCreateInstance failed: %d", static_cast<int>(result));
        return false;
    }
    LOG_INFO("VK: instance created (%zu extensions)", extensions.size());
    return true;
}

bool VulkanRenderer::create_surface_and_device()
{
    m_physical_device = ImGui_ImplVulkanH_SelectPhysicalDevice(m_instance);
    if (m_physical_device == VK_NULL_HANDLE) {
        LOG_ERROR("VK: no suitable physical device found");
        return false;
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physical_device, &props);
    LOG_INFO("VK: device: %s", props.deviceName);

    m_queue_family = ImGui_ImplVulkanH_SelectQueueFamilyIndex(m_physical_device);
    if (m_queue_family == UINT32_MAX) {
        LOG_ERROR("VK: no graphics queue family available");
        return false;
    }

    const char* device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
    };

    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = m_queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 2;
    device_info.ppEnabledExtensionNames = device_extensions;

    const VkResult result = vkCreateDevice(m_physical_device, &device_info, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        LOG_ERROR("VK: vkCreateDevice failed: %d", static_cast<int>(result));
        return false;
    }
    vkGetDeviceQueue(m_device, m_queue_family, 0, &m_graphics_queue);

    if (!SDL_Vulkan_CreateSurface(m_window->sdl_window(), m_instance, nullptr, &m_surface)) {
        LOG_ERROR("VK: SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        return false;
    }
    LOG_INFO("VK: device and surface ready");
    return true;
}

void VulkanRenderer::create_descriptor_pool()
{
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 16},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16},
    };
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 64;
    pool_info.poolSizeCount = 3;
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_descriptor_pool) != VK_SUCCESS)
        LOG_ERROR("VK: failed to create descriptor pool");
}

bool VulkanRenderer::create_swapchain(bool initial)
{
    ImGui_ImplVulkanH_Window* wd = &m_wd;

    if (wd->Width != 0 && wd->Height != 0 && wd->Swapchain != VK_NULL_HANDLE)
        return true;

    int drawable_w = 0, drawable_h = 0;
    m_window->drawable_size(&drawable_w, &drawable_h);
    if (drawable_w == 0 || drawable_h == 0)
        return false;

    wd->Surface = m_surface;

    const VkFormat request_formats[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM,
                                        VK_FORMAT_R8G8B8A8_UNORM};
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        m_physical_device, m_surface, request_formats, 3, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

    const VkPresentModeKHR request_modes[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(m_physical_device, m_surface,
                                                          request_modes, 2);

    const uint32_t min_image_count = ImGui_ImplVulkanH_GetMinImageCountFromPresentMode(wd->PresentMode);

    ImGui_ImplVulkanH_CreateOrResizeWindow(m_instance, m_physical_device, m_device, wd,
                                           m_queue_family, nullptr, drawable_w, drawable_h,
                                           min_image_count,
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                               VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                               VK_IMAGE_USAGE_SAMPLED_BIT);
    m_swapchain_ok = true;

    LOG_INFO("VK: swapchain created (%dx%d, %d images, present mode %d)",
             drawable_w, drawable_h, wd->ImageCount, static_cast<int>(wd->PresentMode));

    if (initial) {
        /* Initialize the ImGui Vulkan backend with the first swapchain. */
        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.ApiVersion = VK_API_VERSION_1_1;
        init_info.Instance = m_instance;
        init_info.PhysicalDevice = m_physical_device;
        init_info.Device = m_device;
        init_info.QueueFamily = m_queue_family;
        init_info.Queue = m_graphics_queue;
        init_info.DescriptorPool = m_descriptor_pool;
        init_info.MinImageCount = min_image_count;
        init_info.ImageCount = wd->ImageCount;
        init_info.PipelineCache = m_pipeline_cache;
        init_info.PipelineInfoMain.RenderPass = wd->RenderPass;
        init_info.PipelineInfoMain.Subpass = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.CheckVkResultFn = vk_check;
        init_info.MinAllocationSize = 1024 * 1024;

        if (!ImGui_ImplVulkan_Init(&init_info)) {
            LOG_ERROR("VK: ImGui_ImplVulkan_Init failed");
            return false;
        }
    } else {
        /* Recreate the backend's main pipeline for the new render pass. */
        ImGui_ImplVulkan_PipelineInfo pipeline_info{};
        pipeline_info.RenderPass = wd->RenderPass;
        pipeline_info.Subpass = 0;
        pipeline_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_CreateMainPipeline(&pipeline_info);
    }
    return true;
}

void VulkanRenderer::recreate_swapchain()
{
    wait_idle();
    m_swapchain_ok = false;

    /* Tear down the window data so it is rebuilt on the next frame. */
    ImGui_ImplVulkanH_DestroyWindow(m_instance, m_device, &m_wd, nullptr);
}

bool VulkanRenderer::initialize(Window& window)
{
    m_window = &window;

    /* Create the headless EGL display WPE renders into (the dmabuf source). */
    if (!egl_helpers_init()) {
        LOG_ERROR("VK: EGL helpers unavailable (needed for WPE dmabuf export)");
        return false;
    }
    m_wpe_egl_display = egl_helpers_create_headless_display();
    if (m_wpe_egl_display == EGL_NO_DISPLAY) {
        LOG_ERROR("VK: failed to create headless EGL display for WPE");
        return false;
    }

    if (!create_instance())
        return false;
    if (!create_surface_and_device())
        return false;

    create_descriptor_pool();

    m_importer.initialize(m_device, m_physical_device);

    /* Web view sampler (linear filtering, clamped). */
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(m_device, &sampler_info, nullptr, &m_webview_sampler);

    /* The SDL Vulkan presentation is bound to the window; the swapchain is
     * created below. */
    if (!ImGui_ImplSDL3_InitForVulkan(window.sdl_window())) {
        LOG_ERROR("VK: ImGui_ImplSDL3_InitForVulkan failed");
        return false;
    }

    if (!create_swapchain(true))
        return false;

    LOG_INFO("VK: renderer initialized");
    return true;
}

void VulkanRenderer::destroy_webview_texture()
{
    if (m_webview_descriptor) {
        ImGui_ImplVulkan_RemoveTexture(m_webview_descriptor);
        m_webview_descriptor = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::shutdown()
{
    wait_idle();

    destroy_webview_texture();
    m_importer.shutdown();

    if (m_webview_sampler) {
        vkDestroySampler(m_device, m_webview_sampler, nullptr);
        m_webview_sampler = VK_NULL_HANDLE;
    }

    if (m_swapchain_ok)
        ImGui_ImplVulkanH_DestroyWindow(m_instance, m_device, &m_wd, nullptr);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();

    if (m_descriptor_pool) {
        vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
        m_descriptor_pool = VK_NULL_HANDLE;
    }
    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
    if (m_wpe_egl_display != EGL_NO_DISPLAY) {
        eglTerminate(static_cast<EGLDisplay>(m_wpe_egl_display));
        m_wpe_egl_display = nullptr;
    }
}

void VulkanRenderer::begin_frame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void VulkanRenderer::update_webview_texture(const DmaBufFrame& frame)
{
    int slot = -1;
    VkImageView view = m_importer.import(frame, &slot);
    if (!view)
        return;

    /* Update the descriptor set used by the UI to sample the web view. */
    if (!m_webview_descriptor) {
        m_webview_descriptor = ImGui_ImplVulkan_AddTexture(
            m_webview_sampler, view, VK_IMAGE_LAYOUT_GENERAL);
    } else {
        VkDescriptorImageInfo image_info{};
        image_info.sampler = m_webview_sampler;
        image_info.imageView = view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_webview_descriptor;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }

    m_frame_width = frame.width;
    m_frame_height = frame.height;
    m_frame_ready = true;
}

void VulkanRenderer::on_dmabuf(const DmaBufFrame& frame)
{
    update_webview_texture(frame);
}

void VulkanRenderer::on_egl_image(const EglFrame& frame)
{
    (void)frame;
    LOG_WARN("VK: unexpected EGL image frame (sink requested dmabufs)");
}

void VulkanRenderer::on_shm(const ShmFrame& frame)
{
    (void)frame;
    LOG_WARN("VK: unexpected shared-memory frame; not supported");
}

void VulkanRenderer::on_resize(int width_px, int height_px)
{
    (void)width_px;
    (void)height_px;
    m_resize_pending = true;
}

void VulkanRenderer::frame_render()
{
    ImGui_ImplVulkanH_Window* wd = &m_wd;

    /* Recreate the swapchain if the window was resized or invalidated. */
    if (m_resize_pending || !m_swapchain_ok) {
        m_resize_pending = false;
        recreate_swapchain();
        if (!create_swapchain(false))
            return;
    }

    VkSemaphore image_acquired = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    uint32_t image_index = 0;
    VkResult result = vkAcquireNextImageKHR(m_device, wd->Swapchain, UINT64_MAX,
                                            image_acquired, VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
        m_resize_pending = true;
        return;
    }
    if (result != VK_SUCCESS) {
        LOG_ERROR("VK: vkAcquireNextImageKHR failed: %d", static_cast<int>(result));
        return;
    }
    wd->FrameIndex = image_index;

    ImGui_ImplVulkanH_Frame* frame = &wd->Frames[wd->FrameIndex];
    vkWaitForFences(m_device, 1, &frame->Fence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &frame->Fence);
    vkResetCommandPool(m_device, frame->CommandPool, 0);

    VkCommandBuffer command_buffer = frame->CommandBuffer;
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command_buffer, &begin_info);

    VkClearValue clear_value{};
    clear_value.color = {{0.12f, 0.12f, 0.13f, 1.0f}};

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = wd->RenderPass;
    render_pass_info.framebuffer = frame->Framebuffer;
    render_pass_info.renderArea = {{0, 0}, {wd->Width, wd->Height}};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_value;
    vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
    vkCmdEndRenderPass(command_buffer);
    vkEndCommandBuffer(command_buffer);

    /* Signal the web view slot's semaphore once, on the first submit that
     * draws the current import, so it can be safely reused later. */
    VkSemaphore extra_signal = m_importer.take_signal_semaphore();

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphore signal_semaphores[2] = {render_complete, extra_signal};
    const uint32_t signal_count = extra_signal ? 2u : 1u;

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_acquired;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    submit_info.signalSemaphoreCount = signal_count;
    submit_info.pSignalSemaphores = signal_semaphores;

    result = vkQueueSubmit(m_graphics_queue, 1, &submit_info, frame->Fence);
    if (result != VK_SUCCESS)
        LOG_ERROR("VK: vkQueueSubmit failed: %d", static_cast<int>(result));
}

void VulkanRenderer::frame_present()
{
    ImGui_ImplVulkanH_Window* wd = &m_wd;
    if (wd->Swapchain == VK_NULL_HANDLE)
        return;

    VkSemaphore render_complete = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_complete;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &wd->Swapchain;
    present_info.pImageIndices = &wd->FrameIndex;

    const VkResult result = vkQueuePresentKHR(m_graphics_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
        m_resize_pending = true;
    }

    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

void VulkanRenderer::render_present()
{
    ImGui::Render();
    frame_render();
    frame_present();
}

void VulkanRenderer::wait_idle()
{
    if (m_device)
        vkDeviceWaitIdle(m_device);
}

void* VulkanRenderer::egl_display_for_wpe()
{
    return m_wpe_egl_display;
}

} /* namespace imwb */
