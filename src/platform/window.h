/* ImWebBrowser - SDL3 platform window. */

#ifndef IMWEBBROWSER_PLATFORM_WINDOW_H
#define IMWEBBROWSER_PLATFORM_WINDOW_H

#include <SDL3/SDL.h>

#include "config/config.h"

namespace imwb {

class Window {
public:
    Window() = default;
    ~Window() = default;

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /* backend_flags: SDL_WINDOW_OPENGL or SDL_WINDOW_VULKAN. */
    bool initialize(const Config& config, Uint32 backend_flags);
    void shutdown();

    SDL_Window* sdl_window() const { return m_window; }

    /* Logical (UI) size in points; drawable size in pixels. */
    int width() const { return m_width; }
    int height() const { return m_height; }
    void drawable_size(int* w, int* h) const;
    float content_scale() const;

    /* Toggles borderless fullscreen ("kiosk") covering the display. */
    void set_fullscreen(bool on);
    bool is_fullscreen() const;

    bool minimized() const;

private:
    void refresh_logical_size();

    SDL_Window* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_PLATFORM_WINDOW_H */
