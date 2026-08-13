/* ImWebBrowser - translates SDL3 input events into WPE view backend events. */

#include "input/input_translator.h"

#include <wpe/wpe.h>

#include "input/keymap.h"
#include "logging/log.h"

namespace imwb {

namespace {

constexpr uint32_t kAxisVertical = 0;
constexpr uint32_t kAxisHorizontal = 1;

constexpr uint32_t kWpeModControl = 1u << 0;
constexpr uint32_t kWpeModShift = 1u << 1;
constexpr uint32_t kWpeModAlt = 1u << 2;
constexpr uint32_t kWpeModMeta = 1u << 3;

constexpr uint32_t kWpeBtnLeft = 1u << 20;
constexpr uint32_t kWpeBtnRight = 1u << 21;
constexpr uint32_t kWpeBtnMiddle = 1u << 22;

/* SDL3 mouse buttons: 1=left, 2=middle, 3=right, 4/5=x1/x2.
 * WPE expects: 1=left, 2=right, 3=middle, 4=back, 5=forward. */
uint32_t wpe_button_from_sdl(uint8_t sdl_button)
{
    switch (sdl_button) {
    case SDL_BUTTON_LEFT: return 1;
    case SDL_BUTTON_RIGHT: return 3;
    case SDL_BUTTON_MIDDLE: return 2;
    case SDL_BUTTON_X1: return 4;
    case SDL_BUTTON_X2: return 5;
    default: return 0;
    }
}

uint32_t wpe_button_bit_from_sdl(uint8_t sdl_button)
{
    switch (sdl_button) {
    case SDL_BUTTON_LEFT: return kWpeBtnLeft;
    case SDL_BUTTON_RIGHT: return kWpeBtnRight;
    case SDL_BUTTON_MIDDLE: return kWpeBtnMiddle;
    default: return 0;
    }
}

} /* namespace */

uint32_t InputTranslator::wpe_modifiers_from_sdl(uint16_t sdl_mods) const
{
    uint32_t mods = 0;
    if (sdl_mods & SDL_KMOD_CTRL) mods |= kWpeModControl;
    if (sdl_mods & SDL_KMOD_SHIFT) mods |= kWpeModShift;
    if (sdl_mods & SDL_KMOD_ALT) mods |= kWpeModAlt;
    if (sdl_mods & SDL_KMOD_GUI) mods |= kWpeModMeta;
    return mods;
}

bool InputTranslator::pointer_in_view(float x, float y) const
{
    return x >= static_cast<float>(m_view_x) &&
           y >= static_cast<float>(m_view_y) &&
           x < static_cast<float>(m_view_x + m_view_width) &&
           y < static_cast<float>(m_view_y + m_view_height);
}

void InputTranslator::view_pixel_from_logical(float x, float y, int* px, int* py) const
{
    int local_x = static_cast<int>((x - static_cast<float>(m_view_x)) * m_scale);
    int local_y = static_cast<int>((y - static_cast<float>(m_view_y)) * m_scale);
    if (local_x < 0) local_x = 0;
    if (local_y < 0) local_y = 0;
    if (px) *px = local_x;
    if (py) *py = local_y;
}

void InputTranslator::dispatch_keyboard(SDL_Scancode scancode, uint16_t sdl_mods,
                                        bool pressed, bool repeat)
{
    if (!m_backend)
        return;

    const uint32_t hardware_key_code = keymap_evdev_from_scancode(scancode);
    if (hardware_key_code == 0)
        return;

    /* Use our own XKB keymap (system layout) instead of libwpe's hardcoded
     * "us" layout. This returns the keysym matching the desktop layout. */
    const uint32_t key_code = m_xkb.keysym_for_keycode(hardware_key_code, pressed);

    struct wpe_input_keyboard_event event{};
    event.time = SDL_GetTicks();
    event.key_code = key_code;
    event.hardware_key_code = hardware_key_code;
    event.pressed = pressed || repeat;
    event.modifiers = wpe_modifiers_from_sdl(sdl_mods);

    wpe_view_backend_dispatch_keyboard_event(m_backend, &event);
}

void InputTranslator::dispatch_pointer_motion(float x, float y, uint16_t sdl_mods)
{
    if (!m_backend)
        return;

    int px = 0, py = 0;
    if (!pointer_in_view(x, y))
        return;
    view_pixel_from_logical(x, y, &px, &py);

    struct wpe_input_pointer_event event{};
    event.type = wpe_input_pointer_event_type_motion;
    event.time = SDL_GetTicks();
    event.x = px;
    event.y = py;
    event.button = 0;
    event.state = m_pressed_buttons;
    event.modifiers = wpe_modifiers_from_sdl(sdl_mods);
    wpe_view_backend_dispatch_pointer_event(m_backend, &event);
}

void InputTranslator::dispatch_pointer_button(float x, float y, uint8_t button,
                                              bool pressed, uint16_t sdl_mods)
{
    if (!m_backend)
        return;

    int px = 0, py = 0;
    if (!pointer_in_view(x, y))
        return;
    view_pixel_from_logical(x, y, &px, &py);

    const uint32_t wpe_button = wpe_button_from_sdl(button);
    const uint32_t button_bit = wpe_button_bit_from_sdl(button);

    if (pressed)
        m_pressed_buttons |= button_bit;
    else
        m_pressed_buttons &= ~button_bit;

    struct wpe_input_pointer_event event{};
    event.type = wpe_input_pointer_event_type_button;
    event.time = SDL_GetTicks();
    event.x = px;
    event.y = py;
    event.button = wpe_button;
    event.state = m_pressed_buttons;
    event.modifiers = wpe_modifiers_from_sdl(sdl_mods);
    wpe_view_backend_dispatch_pointer_event(m_backend, &event);
}

void InputTranslator::dispatch_axis(float x, float y, float wheel_y, uint16_t sdl_mods)
{
    if (!m_backend || wheel_y == 0.0f)
        return;

    int px = 0, py = 0;
    if (!pointer_in_view(x, y))
        return;
    view_pixel_from_logical(x, y, &px, &py);

    /* SDL: positive wheel_y scrolls away from the user (up). WPE treats a
     * positive axis value the same way. */
    int32_t value = 0;
    if (m_smooth_scrolling)
        value = static_cast<int32_t>(wheel_y * 40.0f); /* pixel units */
    else
        value = static_cast<int32_t>(wheel_y);         /* line units */

    struct wpe_input_axis_event event{};
    event.type = m_smooth_scrolling ? wpe_input_axis_event_type_motion_smooth
                                    : wpe_input_axis_event_type_motion;
    event.time = SDL_GetTicks();
    event.x = px;
    event.y = py;
    event.axis = kAxisVertical;
    event.value = value;
    event.modifiers = wpe_modifiers_from_sdl(sdl_mods);
    wpe_view_backend_dispatch_axis_event(m_backend, &event);
}

bool InputTranslator::handle_event(const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN: {
        dispatch_keyboard(event.key.scancode, event.key.mod, true, event.key.repeat);
        return true;
    }
    case SDL_EVENT_KEY_UP: {
        dispatch_keyboard(event.key.scancode, event.key.mod, false, false);
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        dispatch_pointer_motion(event.motion.x, event.motion.y, SDL_GetModState());
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        dispatch_pointer_button(event.button.x, event.button.y, event.button.button, true,
                                SDL_GetModState());
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        dispatch_pointer_button(event.button.x, event.button.y, event.button.button, false,
                                SDL_GetModState());
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        dispatch_axis(event.wheel.mouse_x, event.wheel.mouse_y, event.wheel.y, SDL_GetModState());
        return true;
    }
    default:
        return false;
    }
}

} /* namespace imwb */
