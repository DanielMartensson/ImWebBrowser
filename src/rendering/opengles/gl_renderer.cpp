/* ImWebBrowser - OpenGL ES 3 renderer. */

#include "rendering/opengles/gl_renderer.h"

#include <EGL/egl.h>
#include <GLES2/gl2ext.h>

#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>

#include "logging/log.h"
#include "platform/window.h"

namespace imwb {

namespace {

/* EGL image extension entry points; resolved at runtime via eglGetProcAddress
 * (not exported by the GLES/GL shared libraries). */
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC g_egl_image_target_texture_2d = nullptr;

/* Pick a (format, type) pair compatible with the internal format used by
 * the WPE backing store when allocating our owned texture. */
void format_for_internal(GLenum internal_format, GLenum* format, GLenum* type)
{
    switch (internal_format) {
    case GL_RGB8:
    case GL_RGB565:
        *format = GL_RGB;
        *type = GL_UNSIGNED_BYTE;
        break;
    case GL_RGBA4:
    case GL_RGB5_A1:
    case GL_RGBA8:
    default:
        *format = GL_RGBA;
        *type = GL_UNSIGNED_BYTE;
        break;
    }
}

} /* namespace */

bool GlRenderer::initialize(Window& window)
{
    m_window = &window;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

    m_gl_context = SDL_GL_CreateContext(window.sdl_window());
    if (!m_gl_context) {
        LOG_ERROR("GL: SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_GL_MakeCurrent(window.sdl_window(), m_gl_context)) {
        LOG_ERROR("GL: SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        return false;
    }
    int swap_interval = 1;
    const char* env = SDL_getenv("IMWB_SWAP_INTERVAL");
    if (env && *env)
        swap_interval = SDL_atoi(env);
    SDL_GL_SetSwapInterval(swap_interval);
    LOG_INFO("GL: swap interval %d", swap_interval);

    LOG_INFO("GL: renderer %s, version %s", (const char*)glGetString(GL_RENDERER),
             (const char*)glGetString(GL_VERSION));

    g_egl_image_target_texture_2d = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!g_egl_image_target_texture_2d) {
        LOG_ERROR("GL: glEGLImageTargetTexture2DOES not available");
        return false;
    }

    /* The EGL display shared with WPE. */
    m_egl_display = eglGetCurrentDisplay();
    if (m_egl_display == EGL_NO_DISPLAY) {
        /* Fallback: create an EGL display ourselves. Mesa returns the same
         * display object for the same native display, so WPE images made on
         * it remain usable by SDL's context. */
        m_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0;
        if (m_egl_display == EGL_NO_DISPLAY ||
            !eglInitialize(m_egl_display, &major, &minor)) {
            LOG_ERROR("GL: no EGL display is current and none could be created");
            return false;
        }
        eglBindAPI(EGL_OPENGL_ES_API);
        LOG_INFO("GL: created EGL display %p (EGL %d.%d)",
                 (void*)m_egl_display, major, minor);
    }

    /* Scratch texture that EGL images are bound into. */
    glGenTextures(1, &m_scratch_texture);

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOG_ERROR("GL: ImGui_ImplOpenGL3_Init failed");
        return false;
    }
    if (!ImGui_ImplSDL3_InitForOpenGL(window.sdl_window(), m_gl_context)) {
        LOG_ERROR("GL: ImGui_ImplSDL3_InitForOpenGL failed");
        return false;
    }

    LOG_INFO("GL: renderer initialized (EGL display %p)", (void*)m_egl_display);
    return true;
}

void GlRenderer::shutdown()
{
    if (m_scratch_texture) {
        glDeleteTextures(1, &m_scratch_texture);
        m_scratch_texture = 0;
    }
    if (m_webview_texture) {
        glDeleteTextures(1, &m_webview_texture);
        m_webview_texture = 0;
    }

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplOpenGL3_Shutdown();

    if (m_gl_context) {
        SDL_GL_DestroyContext(m_gl_context);
        m_gl_context = nullptr;
    }
}

void GlRenderer::begin_frame()
{
    SDL_GL_MakeCurrent(m_window->sdl_window(), m_gl_context);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void GlRenderer::render_present()
{
    ImGui::Render();

    int drawable_w = 0, drawable_h = 0;
    m_window->drawable_size(&drawable_w, &drawable_h);

    glViewport(0, 0, drawable_w, drawable_h);
    glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(m_window->sdl_window());
}

void GlRenderer::on_resize(int width_px, int height_px)
{
    (void)width_px;
    (void)height_px;
    /* Nothing to do: the swap window + viewport adapt every frame. */
}

void GlRenderer::wait_idle()
{
    glFinish();
}

void* GlRenderer::egl_display_for_wpe()
{
    return m_egl_display;
}

void GlRenderer::ensure_webview_texture(GLenum internal_format, uint32_t width, uint32_t height)
{
    if (m_webview_texture != 0 && m_webview_width == width &&
        m_webview_height == height && m_webview_internal_format == internal_format)
        return;

    if (m_webview_texture == 0)
        glGenTextures(1, &m_webview_texture);

    glBindTexture(GL_TEXTURE_2D, m_webview_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum format = GL_RGBA, type = GL_UNSIGNED_BYTE;
    format_for_internal(internal_format, &format, &type);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, nullptr);

    m_webview_width = width;
    m_webview_height = height;
    m_webview_internal_format = internal_format;
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GlRenderer::on_egl_image(const EglFrame& frame)
{
    if (frame.image == nullptr || !m_scratch_texture)
        return;

    /* Attach the EGL image to the scratch texture. */
    glBindTexture(GL_TEXTURE_2D, m_scratch_texture);
    g_egl_image_target_texture_2d(GL_TEXTURE_2D, reinterpret_cast<GLeglImageOES>(frame.image));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    GLint internal_format = GL_RGBA8;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal_format);

    ensure_webview_texture(static_cast<GLenum>(internal_format), frame.width, frame.height);

    /* GPU-only copy into our owned texture, then release the image. */
    glCopyImageSubData(m_scratch_texture, GL_TEXTURE_2D, 0, 0, 0, 0,
                       m_webview_texture, GL_TEXTURE_2D, 0, 0, 0, 0,
                       frame.width, frame.height, 1);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Wait for the copy to actually execute before returning: the exportable
     * callback hands the image straight back to WPE and signals frame_complete
     * right after this, which may recycle the image and redraw into it while
     * an unexecuted copy is still queued, showing as flicker/tearing. */
    glFinish();

    m_frame_ready = true;
    m_frame_width = frame.width;
    m_frame_height = frame.height;
}

void GlRenderer::on_dmabuf(const DmaBufFrame& frame)
{
    (void)frame;
    LOG_WARN("GL: unexpected dmabuf frame (sink requested EGL images)");
}

/* wl_shm_format values for the common BGRA-in-memory pixel formats. */
namespace {
constexpr uint32_t kShmFormatArgb8888 = 0;
constexpr uint32_t kShmFormatXrgb8888 = 1;
} /* namespace */

void GlRenderer::on_shm(const ShmFrame& frame)
{
    if (!frame.data || !frame.width || !frame.height)
        return;

    if (frame.format != kShmFormatArgb8888 && frame.format != kShmFormatXrgb8888) {
        LOG_WARN("GL: unsupported shm frame format 0x%08x", frame.format);
        return;
    }

    ensure_webview_texture(GL_RGBA8, frame.width, frame.height);

    /* wl_shm ARGB8888/XRGB8888 store bytes as B,G,R,A in memory; GL_RGBA
     * upload needs R,G,B,A. Swap on the way through a staging row buffer. */
    const uint8_t* src = static_cast<const uint8_t*>(frame.data);
    const size_t row_bytes = static_cast<size_t>(frame.width) * 4;
    const size_t stride = frame.stride >= row_bytes ? frame.stride : row_bytes;
    std::vector<uint8_t> row(row_bytes);

    glBindTexture(GL_TEXTURE_2D, m_webview_texture);
    for (uint32_t y = 0; y < frame.height; ++y) {
        const uint8_t* line = src + static_cast<size_t>(y) * stride;
        for (uint32_t x = 0; x < frame.width; ++x) {
            const uint8_t* p = line + static_cast<size_t>(x) * 4;
            row[x * 4 + 0] = p[2];
            row[x * 4 + 1] = p[1];
            row[x * 4 + 2] = p[0];
            row[x * 4 + 3] = p[3];
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, frame.width, 1, GL_RGBA,
                        GL_UNSIGNED_BYTE, row.data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);

    {
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            FILE* f = fopen("/tmp/opencode/shmdump.bgra", "wb");
            if (f) {
                for (uint32_t y = 0; y < frame.height; ++y)
                    fwrite(src + static_cast<size_t>(y) * stride, 1, row_bytes, f);
                fclose(f);
            }
            LOG_INFO("GL: dumped first shm frame to /tmp/opencode/shmdump.bgra (%ux%u)",
                     frame.width, frame.height);
        }
    }

    m_frame_ready = true;
    m_frame_width = frame.width;
    m_frame_height = frame.height;
}

} /* namespace imwb */
