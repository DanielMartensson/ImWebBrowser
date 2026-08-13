/* ImWebBrowser - web view frame descriptors and the FrameSink interface.
 *
 * WPE produces frames in one of two native GPU forms, depending on the
 * exportable type requested:
 *
 *  - EGL path:  an EGLImageKHR referencing the backing store.
 *  - dmabuf:    one or more dmabuf fds describing a DRM format buffer.
 *
 * The FrameSink is implemented by the active renderer. Both paths are fully
 * GPU-native: no pixel data is ever copied through the CPU.
 */

#ifndef IMWEBBROWSER_RENDERING_WEBVIEW_FRAME_H
#define IMWEBBROWSER_RENDERING_WEBVIEW_FRAME_H

#include <cstdint>

typedef void* EGLImageKHR; /* <EGL/egl.h> -> typedef void* EGLImageKHR */

namespace imwb {

struct EglFrame {
    EGLImageKHR image;
    uint32_t width;
    uint32_t height;
};

struct DmaBufFrame {
    uint32_t width;
    uint32_t height;
    uint32_t format;          /* DRM fourcc */
    uint8_t n_planes;
    int fds[4];
    uint32_t strides[4];
    uint32_t offsets[4];
    uint64_t modifiers[4];
};

/* CPU-shared frame: WebKit falls back to shared memory when the GPU renderer
 * cannot be initialized (e.g. missing DRI driver for the GPU). Pixels are
 * row-major, `stride` bytes per row, in a `wl_shm_format`. The data is only
 * valid for the duration of the on_shm() call. */
struct ShmFrame {
    const void* data;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;          /* wl_shm_format */
};

class FrameSink {
public:
    virtual ~FrameSink() = default;

    /* Which export path does this sink want? */
    virtual bool wants_egl_image() const = 0;
    virtual bool wants_dmabuf() const = 0;

    /* Called from the WPE event source with a fresh web view frame.
     * The implementation must be safe to call during the event loop. */
    virtual void on_egl_image(const EglFrame& frame) = 0;
    virtual void on_dmabuf(const DmaBufFrame& frame) = 0;
    virtual void on_shm(const ShmFrame& frame) = 0;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_RENDERING_WEBVIEW_FRAME_H */
