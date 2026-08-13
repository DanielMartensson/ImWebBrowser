/* ImWebBrowser - SDL3 platform window. */

#include "platform/window.h"

#include "logging/log.h"

namespace imwb {

bool Window::initialize(const Config& config, Uint32 backend_flags)
{
    m_width = config.window_width;
    m_height = config.window_height;

    /* The WPE EGL-image path needs the GL context to be an EGL context so we
     * can share the EGL display with WPE. On X11 SDL may otherwise fall back
     * to a GLX-created ES context, which has no EGL display to share. */
    SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");

    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | backend_flags;
    if (config.fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;

    m_window = SDL_CreateWindow("ImWebBrowser", m_width, m_height, flags);
    if (!m_window) {
        LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    refresh_logical_size();
    LOG_INFO("Window created: %dx%d (logical), scale %.2f",
             m_width, m_height, content_scale());
    return true;
}

void Window::shutdown()
{
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void Window::refresh_logical_size()
{
    if (m_window) {
        int w = 0, h = 0;
        SDL_GetWindowSize(m_window, &w, &h);
        m_width = w;
        m_height = h;
    }
}

void Window::drawable_size(int* w, int* h) const
{
    int pw = 0, ph = 0;
    if (m_window)
        SDL_GetWindowSizeInPixels(m_window, &pw, &ph);
    if (w) *w = pw;
    if (h) *h = ph;
}

float Window::content_scale() const
{
    int pw = 0, ph = 0;
    drawable_size(&pw, &ph);
    if (m_width > 0 && pw > 0)
        return static_cast<float>(pw) / static_cast<float>(m_width);
    return 1.0f;
}

bool Window::minimized() const
{
    return m_window && (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0;
}

void Window::set_fullscreen(bool on)
{
    if (!m_window)
        return;

    SDL_SetWindowFullscreen(m_window, on);
    refresh_logical_size();
}

bool Window::is_fullscreen() const
{
    return m_window && (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

} /* namespace imwb */
