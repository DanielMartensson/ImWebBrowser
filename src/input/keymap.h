/* ImWebBrowser - SDL scancode to XKB keycode mapping.
 *
 * SDL3 reports *USB-HID* scancodes (SDL_SCANCODE_*), while WPE wants
 * *XKB keycodes* (the Linux evdev keycode plus 8, matching the X11 /
 * Wayland convention) so its XKB context can interpret them. This module
 * provides the translation table and applies the +8 offset.
 */

#ifndef IMWEBBROWSER_INPUT_KEYMAP_H
#define IMWEBBROWSER_INPUT_KEYMAP_H

#include <cstdint>

#include <SDL3/SDL.h>

namespace imwb {

/* Returns the XKB keycode (evdev keycode + 8) for the given SDL scancode,
 * or 0 (KEY_RESERVED) if unknown. */
uint32_t keymap_evdev_from_scancode(SDL_Scancode scancode);

} /* namespace imwb */

#endif /* IMWEBBROWSER_INPUT_KEYMAP_H */
