/* ImWebBrowser - dmabuf -> VkImage import for the web view.
 *
 * WPE renders each frame into a GBM/dmabuf buffer (exported through the
 * WPEBackend-FDO dmabuf exportable). The fd is imported into a VkDeviceMemory
 * backed VkImage via VK_EXT_external_memory_dma_buf / VK_KHR_external_memory_fd,
 * keeping the frame on the GPU end to end.
 *
 * A small ring of import slots gives the GPU headroom: a slot is only reused
 * once the submit that last drew its image has completed (tracked with a
 * binary semaphore signaled by that submit and waited on before reuse).
 */

#ifndef IMWEBBROWSER_RENDERING_VULKAN_WEBVIEW_IMPORT_H
#define IMWEBBROWSER_RENDERING_VULKAN_WEBVIEW_IMPORT_H

#include <cstdint>

#include <vulkan/vulkan.h>

#include "rendering/webview_frame.h"

namespace imwb {

class WebViewImporter {
public:
    static constexpr int kSlotCount = 3;

    WebViewImporter() = default;
    ~WebViewImporter() = default;

    void initialize(VkDevice device, VkPhysicalDevice physical_device);
    void shutdown();

    /* Imports `frame` into the next slot, waiting (if needed) for the slot's
     * previous GPU work to finish. On success returns the image view for the
     * imported buffer and the slot index; on failure returns VK_NULL_HANDLE.
     * The fds in `frame` are consumed (owned by the driver after a successful
     * import; closed here on failure). */
    VkImageView import(const DmaBufFrame& frame, int* out_slot);

    /* Semaphore that must be signaled by the submit that first draws the
     * current import (set once per import). Returns VK_NULL_HANDLE when the
     * current import was already signalled. */
    VkSemaphore take_signal_semaphore();

    /* Current import (most recent imported view). */
    bool has_current() const { return m_current_slot >= 0; }
    VkImageView current_view() const;
    uint32_t current_width() const;
    uint32_t current_height() const;

private:
    struct Slot {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        bool signal_pending = false; /* submit must signal this slot once */
        bool in_use = false;         /* has an imported frame pending */
        uint32_t width = 0;
        uint32_t height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    void destroy_slot(Slot& slot);
    bool wait_slot(Slot& slot);
    bool create_slot(Slot& slot, const DmaBufFrame& frame, bool* fd_consumed);
    uint32_t pick_memory_type() const;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    bool m_dma_buf_supported = false;

    Slot m_slots[kSlotCount];
    int m_next_slot = 0;
    int m_current_slot = -1;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_RENDERING_VULKAN_WEBVIEW_IMPORT_H */
