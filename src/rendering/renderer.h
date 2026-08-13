/* ImWebBrowser - rendering abstraction.
 *
 * A Renderer owns the graphics context (GLES or Vulkan), drives the ImGui
 * frame, and owns a FrameSink that receives web view frames straight from
 * WPE. The browser never touches GPU APIs directly.
 */

#ifndef IMWEBBROWSER_RENDERING_RENDERER_H
#define IMWEBBROWSER_RENDERING_RENDERER_H

#include <cstdint>

#include "rendering/webview_frame.h"

typedef void* EGLDisplay; /* <EGL/eglplatform.h> -> typedef EGLDisplay void* */

namespace imwb {

class Window;

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool initialize(Window& window) = 0;
    virtual void shutdown() = 0;

    /* Begin/end a frame. render_present() swaps buffers / presents. */
    virtual void begin_frame() = 0;
    virtual void render_present() = 0;

    virtual void on_resize(int width_px, int height_px) = 0;
    virtual void wait_idle() = 0;

    /* The EGL display that WPE WebKit must render into. */
    virtual EGLDisplay egl_display_for_wpe() = 0;

    /* The sink that WPE frames are pushed into. */
    virtual FrameSink& frame_sink() = 0;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_RENDERING_RENDERER_H */
