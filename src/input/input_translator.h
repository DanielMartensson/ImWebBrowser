/* ImWebBrowser - translates SDL3 input events into WPE view backend events.
 *
 * WPE wants XKB keycodes (evdev + 8) for key events and device-pixel
 * coordinates relative to the web view for pointer events, so this module
 * owns all the coordinate / keycode / button / modifier translation.
 */

#ifndef IMWEBBROWSER_INPUT_INPUT_TRANSLATOR_H
#define IMWEBBROWSER_INPUT_INPUT_TRANSLATOR_H

#include <cstdint>

#include <SDL3/SDL.h>

#include "input/xkb_layout.h"

struct wpe_view_backend;

namespace imwb {

class InputTranslator {
public:
    InputTranslator() = default;

    /* The view backend events are dispatched into. */
    void set_view_backend(struct wpe_view_backend* backend) { m_backend = backend; }

    /* Web view rectangle in logical (window) coordinates, plus the
     * window content scale to convert logical -> device pixels. */
    void set_view_geometry(int x, int y, int width, int height, float scale)
    {
        m_view_x = x;
        m_view_y = y;
        m_view_width = width;
        m_view_height = height;
        m_scale = scale;
    }

    bool has_view_backend() const { return m_backend != nullptr; }

    /* Wheel behaviour: smooth pixel scrolling vs discrete line steps. */
    void set_smooth_scrolling(bool smooth) { m_smooth_scrolling = smooth; }

    /* Route one SDL event. Returns true if it was consumed by the web view. */
    bool handle_event(const SDL_Event& event);

    /* Initialise the XKB keymap from the system layout. Call once at startup. */
    bool initialize_xkb() { return m_xkb.initialize(); }

private:
    void dispatch_keyboard(SDL_Scancode scancode, uint16_t sdl_mods, bool pressed, bool repeat);
    void dispatch_pointer_motion(float x, float y, uint16_t sdl_mods);
    void dispatch_pointer_button(float x, float y, uint8_t button, bool pressed, uint16_t sdl_mods);
    void dispatch_axis(float x, float y, float wheel_y, uint16_t sdl_mods);
    bool pointer_in_view(float x, float y) const;
    void view_pixel_from_logical(float x, float y, int* px, int* py) const;
    uint32_t wpe_modifiers_from_sdl(uint16_t sdl_mods) const;

    struct wpe_view_backend* m_backend = nullptr;

    int m_view_x = 0;
    int m_view_y = 0;
    int m_view_width = 0;
    int m_view_height = 0;
    float m_scale = 1.0f;

    /* Buttons currently held, as wpe_input_pointer_modifier bits. */
    uint32_t m_pressed_buttons = 0;
    bool m_smooth_scrolling = true;
    XkbLayout m_xkb;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_INPUT_INPUT_TRANSLATOR_H */
