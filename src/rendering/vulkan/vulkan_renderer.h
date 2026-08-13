/* ImWebBrowser - Vulkan renderer.
 *
 * WPE renders into a headless EGL display and exports dmabufs; each frame's
 * dmabuf is imported into a VkImage via VK_EXT_external_memory_dma_buf and
 * drawn by ImGui through a descriptor set created with
 * ImGui_ImplVulkan_AddTexture(). The frame stays on the GPU end to end.
 *
 * Swapchain + render pass + framebuffers are managed with ImGui's VulkanH
 * helpers (the same ones used by the official ImGui Vulkan examples).
 */

#ifndef IMWEBBROWSER_RENDERING_VULKAN_VULKAN_RENDERER_H
#define IMWEBBROWSER_RENDERING_VULKAN_VULKAN_RENDERER_H

#include <cstdint>

#include <vulkan/vulkan.h>
#include <backends/imgui_impl_vulkan.h>

#include "rendering/renderer.h"
#include "rendering/webview_frame.h"
#include "rendering/vulkan/webview_import.h"

namespace imwb {

class Window;

class VulkanRenderer : public Renderer, public FrameSink {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer() override = default;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    /* Renderer interface. */
    bool initialize(Window& window) override;
    void shutdown() override;
    void begin_frame() override;
    void render_present() override;
    void on_resize(int width_px, int height_px) override;
    void wait_idle() override;
    void* egl_display_for_wpe() override;
    FrameSink& frame_sink() override { return *this; }

    /* FrameSink interface. */
    bool wants_egl_image() const override { return false; }
    bool wants_dmabuf() const override { return true; }
    void on_egl_image(const EglFrame& frame) override;
    void on_dmabuf(const DmaBufFrame& frame) override;
    void on_shm(const ShmFrame& frame) override;

    /* Current web view texture (for the UI to draw). */
    bool frame_ready() const { return m_frame_ready; }
    VkDescriptorSet webview_texture() const { return m_webview_descriptor; }
    uint32_t frame_width() const { return m_frame_width; }
    uint32_t frame_height() const { return m_frame_height; }

private:
    bool create_instance();
    bool create_surface_and_device();
    void create_descriptor_pool();
    bool create_swapchain(bool initial);
    void recreate_swapchain();
    void frame_render();
    void frame_present();
    void update_webview_texture(const DmaBufFrame& frame);
    void destroy_webview_texture();

    Window* m_window = nullptr;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphics_queue = VK_NULL_HANDLE;
    uint32_t m_queue_family = 0;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
    VkPipelineCache m_pipeline_cache = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window m_wd;
    bool m_swapchain_ok = false;
    bool m_resize_pending = false;

    /* Headless EGL display that WPE renders into. */
    void* m_wpe_egl_display = nullptr;

    WebViewImporter m_importer;
    VkSampler m_webview_sampler = VK_NULL_HANDLE;
    VkDescriptorSet m_webview_descriptor = VK_NULL_HANDLE;
    bool m_frame_ready = false;
    uint32_t m_frame_width = 0;
    uint32_t m_frame_height = 0;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_RENDERING_VULKAN_VULKAN_RENDERER_H */
