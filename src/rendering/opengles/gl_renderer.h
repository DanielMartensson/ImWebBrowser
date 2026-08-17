/* ImWebBrowser - OpenGL ES 3 renderer.
 *
 * WPE renders into EGL images on the same EGL display as the SDL GL
 * context (display sharing via wpe_fdo_initialize_for_egl_display). Each
 * exported frame is:
 *
 *   1. bound to a scratch texture (glEGLImageTargetTexture2DOES),
 *   2. copied GPU-only into an owned texture (glCopyImageSubData),
 *   3. released back to WPE immediately,
 *   4. drawn by ImGui via ImGui::Image().
 *
 * No pixel data ever passes through the CPU.
 */

#ifndef IMWEBBROWSER_RENDERING_OPENGLES_GL_RENDERER_H
#define IMWEBBROWSER_RENDERING_OPENGLES_GL_RENDERER_H

#include <cstdint>

#include <GLES3/gl32.h>
#include <SDL3/SDL.h>

#include "rendering/renderer.h"
#include "rendering/webview_frame.h"

namespace imwb {

class Window;

class GlRenderer : public Renderer, public FrameSink {
public:
    GlRenderer() = default;
    ~GlRenderer() override = default;

    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;

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
    bool wants_egl_image() const override { return true; }
    bool wants_dmabuf() const override { return false; }
    void on_egl_image(const EglFrame& frame) override;
    void on_dmabuf(const DmaBufFrame& frame) override;
    void on_shm(const ShmFrame& frame) override;

    /* Current web view texture (for the UI to draw). */
    bool frame_ready() const { return m_frame_ready; }
    GLuint webview_texture() const { return m_webview_texture; }
    uint32_t frame_width() const { return m_frame_width; }
    uint32_t frame_height() const { return m_frame_height; }

private:
    void ensure_webview_texture(GLenum internal_format, uint32_t width, uint32_t height);

    Window* m_window = nullptr;
    SDL_GLContext m_gl_context = nullptr;
    void* m_egl_display = nullptr;

    GLuint m_scratch_texture = 0;
    GLuint m_webview_texture = 0;
    uint32_t m_webview_width = 0;
    uint32_t m_webview_height = 0;
    GLenum m_webview_internal_format = 0;
    bool m_frame_ready = false;
    uint32_t m_frame_width = 0;
    uint32_t m_frame_height = 0;

    /* Fence for asynchronous glCopyImageSubData: replaces glFinish() so
     * the CPU isn't blocked while the GPU completes the copy. The previous
     * frame's fence is waited before starting a new copy. */
    GLsync m_pending_fence = nullptr;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_RENDERING_OPENGLES_GL_RENDERER_H */
