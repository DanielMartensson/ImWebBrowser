/* ImWebBrowser - dmabuf -> VkImage import for the web view. */

#include "rendering/vulkan/webview_import.h"

#include <cstdio>
#include <unistd.h>

#include "logging/log.h"

namespace imwb {

namespace {

uint32_t make_fourcc(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
           (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24);
}

/* DRM fourcc -> Vulkan format for the formats WebKit produces. */
VkFormat fourcc_to_vk_format(uint32_t fourcc)
{
    switch (fourcc) {
    case 0x34325258: /* DRM_FORMAT_XRGB8888 'XR24' */
    case 0x34325241: /* DRM_FORMAT_ARGB8888 'AR24' */
        return VK_FORMAT_B8G8R8A8_UNORM;
    case 0x34324258: /* DRM_FORMAT_XBGR8888 'XB24' */
    case 0x34324241: /* DRM_FORMAT_ABGR8888 'AB24' */
        return VK_FORMAT_R8G8B8A8_UNORM;
    case 0x36314752: /* DRM_FORMAT_R8 'R1G6' - not used for web content */
    default:
        return VK_FORMAT_B8G8R8A8_UNORM;
    }
}

} /* namespace */

void WebViewImporter::initialize(VkDevice device, VkPhysicalDevice physical_device)
{
    m_device = device;
    m_physical_device = physical_device;

    /* Create the per-slot binary semaphores. */
    for (int i = 0; i < kSlotCount; ++i) {
        VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(m_device, &info, nullptr, &m_slots[i].semaphore) != VK_SUCCESS) {
            LOG_ERROR("VK: failed to create import slot semaphore");
            m_slots[i].semaphore = VK_NULL_HANDLE;
        }
    }

    /* Verify the device supports dmabuf import. */
    VkPhysicalDeviceExternalBufferInfo ext_info{};
    ext_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
    ext_info.flags = 0;
    ext_info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    ext_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkExternalBufferProperties ext_props{};
    ext_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
    vkGetPhysicalDeviceExternalBufferProperties(m_physical_device, &ext_info, &ext_props);

    m_dma_buf_supported =
        (ext_props.externalMemoryProperties.externalMemoryFeatures &
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
    if (!m_dma_buf_supported)
        LOG_ERROR("VK: device does not support dmabuf memory import");
}

void WebViewImporter::shutdown()
{
    if (m_device) {
        for (auto& slot : m_slots) {
            if (slot.semaphore)
                vkDestroySemaphore(m_device, slot.semaphore, nullptr);
            destroy_slot(slot);
        }
    }
    m_current_slot = -1;
}

void WebViewImporter::destroy_slot(Slot& slot)
{
    if (slot.view)
        vkDestroyImageView(m_device, slot.view, nullptr);
    if (slot.memory)
        vkFreeMemory(m_device, slot.memory, nullptr);
    if (slot.image)
        vkDestroyImage(m_device, slot.image, nullptr);
    slot.view = VK_NULL_HANDLE;
    slot.memory = VK_NULL_HANDLE;
    slot.image = VK_NULL_HANDLE;
    slot.in_use = false;
    slot.width = 0;
    slot.height = 0;
    slot.format = VK_FORMAT_UNDEFINED;
}

bool WebViewImporter::wait_slot(Slot& slot)
{
    /* If the slot was used by a previous frame, wait for that submit to
     * complete (the semaphore is signaled by the submit that drew it). */
    if (!slot.in_use)
        return true;
    if (!slot.signal_pending)
        return true; /* never drawn, nothing to wait for */

    VkSemaphoreWaitInfo wait_info{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait_info.semaphoreCount = 1;
    wait_info.pSemaphores = &slot.semaphore;
    const VkResult result = vkWaitSemaphores(m_device, &wait_info, UINT64_MAX);
    if (result != VK_SUCCESS) {
        LOG_ERROR("VK: waiting for import slot failed: %d", static_cast<int>(result));
        return false;
    }
    slot.signal_pending = false;
    return true;
}

uint32_t WebViewImporter::pick_memory_type() const
{
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(m_physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        VkPhysicalDeviceExternalBufferInfo ext_info{};
        ext_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
        ext_info.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
        ext_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

        VkExternalBufferProperties ext_props{};
        ext_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
        vkGetPhysicalDeviceExternalBufferProperties(m_physical_device, &ext_info, &ext_props);

        const bool importable =
            (ext_props.externalMemoryProperties.externalMemoryFeatures &
             VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
        const bool device_local =
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;

        if (importable && device_local)
            return i;
    }
    return UINT32_MAX;
}

bool WebViewImporter::create_slot(Slot& slot, const DmaBufFrame& frame, bool* fd_consumed)
{
    const VkFormat format = fourcc_to_vk_format(frame.format);
    *fd_consumed = false;

    /* Each frame is a new dmabuf, so (re)create the image + imported memory. */
    destroy_slot(slot);

    VkExternalMemoryImageCreateInfo ext_image_info{};
    ext_image_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    ext_image_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.pNext = &ext_image_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {frame.width, frame.height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    /* Try OPTIMAL tiling first (works with AFBC on Mali-G57), then fall
     * back to LINEAR for drivers that only support linear dmabuf import. */
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    VkResult result = vkCreateImage(m_device, &image_info, nullptr, &slot.image);
    if (result != VK_SUCCESS) {
        image_info.tiling = VK_IMAGE_TILING_LINEAR;
        result = vkCreateImage(m_device, &image_info, nullptr, &slot.image);
    }
    if (result != VK_SUCCESS) {
        LOG_ERROR("VK: vkCreateImage for dmabuf import failed: %d", static_cast<int>(result));
        return false;
    }

    const uint32_t memory_type = pick_memory_type();
    if (memory_type == UINT32_MAX) {
        LOG_ERROR("VK: no memory type supports dmabuf import");
        destroy_slot(slot);
        return false;
    }

    VkImportMemoryFdInfoKHR import_fd_info{};
    import_fd_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    import_fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import_fd_info.fd = frame.fds[0];

    VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.pNext = &import_fd_info;
    alloc_info.allocationSize = 0;
    alloc_info.memoryTypeIndex = memory_type;

    result = vkAllocateMemory(m_device, &alloc_info, nullptr, &slot.memory);
    if (result != VK_SUCCESS) {
        LOG_ERROR("VK: vkAllocateMemory for dmabuf import failed: %d", static_cast<int>(result));
        destroy_slot(slot);
        return false;
    }

    /* The driver took ownership of the fd on success. */
    *fd_consumed = true;

    /* Close any extra planes the frame carried (not imported). */
    for (uint8_t i = 1; i < frame.n_planes; ++i) {
        if (frame.fds[i] >= 0)
            ::close(frame.fds[i]);
    }

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = slot.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    result = vkCreateImageView(m_device, &view_info, nullptr, &slot.view);
    if (result != VK_SUCCESS) {
        LOG_ERROR("VK: vkCreateImageView for web view failed: %d", static_cast<int>(result));
        destroy_slot(slot);
        return false;
    }

    slot.width = frame.width;
    slot.height = frame.height;
    slot.format = format;
    slot.in_use = true;
    return true;
}

VkImageView WebViewImporter::import(const DmaBufFrame& frame, int* out_slot)
{
    if (!m_device || !m_dma_buf_supported)
        return VK_NULL_HANDLE;

    if (frame.n_planes != 1) {
        LOG_WARN("VK: unsupported multi-plane web view frame (%u planes)", frame.n_planes);
        for (uint8_t i = 0; i < frame.n_planes; ++i) {
            if (frame.fds[i] >= 0)
                ::close(frame.fds[i]);
        }
        return VK_NULL_HANDLE;
    }
    if (frame.fds[0] < 0)
        return VK_NULL_HANDLE;

    Slot& slot = m_slots[m_next_slot];
    if (!wait_slot(slot)) {
        for (uint8_t i = 0; i < frame.n_planes; ++i)
            if (frame.fds[i] >= 0)
                ::close(frame.fds[i]);
        return VK_NULL_HANDLE;
    }

    bool fd_consumed = false;
    if (!create_slot(slot, frame, &fd_consumed)) {
        /* Close plane 0 unless the driver already owns it. */
        if (!fd_consumed && frame.fds[0] >= 0)
            ::close(frame.fds[0]);
        return VK_NULL_HANDLE;
    }

    /* If the slot was reused as-is, plane 0 was not imported and is still
     * owned by us; close it. Extra planes were closed by create_slot. */
    if (!fd_consumed && frame.fds[0] >= 0)
        ::close(frame.fds[0]);

    slot.signal_pending = true;
    m_current_slot = m_next_slot;
    m_next_slot = (m_next_slot + 1) % kSlotCount;

    if (out_slot)
        *out_slot = m_current_slot;
    return slot.view;
}

VkSemaphore WebViewImporter::take_signal_semaphore()
{
    if (m_current_slot < 0)
        return VK_NULL_HANDLE;
    Slot& slot = m_slots[m_current_slot];
    if (!slot.signal_pending)
        return VK_NULL_HANDLE;
    slot.signal_pending = false;
    return slot.semaphore;
}

VkImageView WebViewImporter::current_view() const
{
    if (m_current_slot < 0)
        return VK_NULL_HANDLE;
    return m_slots[m_current_slot].view;
}

uint32_t WebViewImporter::current_width() const
{
    if (m_current_slot < 0)
        return 0;
    return m_slots[m_current_slot].width;
}

uint32_t WebViewImporter::current_height() const
{
    if (m_current_slot < 0)
        return 0;
    return m_slots[m_current_slot].height;
}

} /* namespace imwb */
