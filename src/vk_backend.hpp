#pragma once
// Vulkan presentation backend: imports WPE WebKit's dma-buf frames as VkImages
// and presents them through a swapchain using Dear ImGui's Vulkan renderer
// (whose shaders are precompiled inside imgui_impl_vulkan.cpp).

#include <vulkan/vulkan.h>
#include <cstdint>
#include <imgui.h>

struct SDL_Window;

struct VkDmabufPlane {
    int fd = -1;      // owned by the importer once handed over
    uint32_t stride = 0;
    uint32_t offset = 0;
};

struct VkDmabufFrame {
    uint32_t fourcc = 0;        // DRM fourcc, e.g. 'AR24'
    uint64_t modifier = 0;      // DRM_FORMAT_MOD_* (INVALID = assume linear)
    uint32_t width = 0, height = 0;
    uint32_t planeCount = 0;
    VkDmabufPlane planes[4];
    bool consumed = false;      // fds handed to Vulkan; re-export before reuse
};

class VulkanPresent {
public:
    struct Handles {
        VkInstance instance;
        VkPhysicalDevice physical;
        VkDevice device;
        uint32_t queueFamily;
        VkQueue queue;
        uint32_t imageCount;
        int drmRenderMinor;  // -1 if unknown
    };
    // Opaque Vulkan handles (defined in the header as void* to avoid leaking
    // vulkan.h into every includer is not possible — include <vulkan/vulkan.h>
    // before this header when using handles()).
    bool init(SDL_Window* window, int width, int height);
    void resize(int width, int height);
    // Imports a frame; takes ownership of the plane fds and sets
    // frame.consumed (caller should re-export before importing it again).
    // Returns an ImTextureID usable with ImGui, or 0 on failure.
    ImTextureID importFrame(VkDmabufFrame& frame);
    // Records GetDrawData(), submits and presents (vsync FIFO).
    bool drawFrame(int width, int height);
    // Kiosk direct path: draws the latest imported frame with a minimal
    // fullscreen pipeline (no ImGui), submits and presents.
    bool drawFrameKiosk(int width, int height);
    void shutdown();

#ifdef IMWB_BACKEND_VULKAN
    struct Impl;
    Handles handles() const;
    struct VkRenderPass_T* renderPass() const;  // opaque; cast at ImGui init site
#endif

private:
    Impl* d = nullptr;
};
