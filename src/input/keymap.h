/* ImWebBrowser - SDL scancode to Linux evdev keycode mapping.
 *
 * SDL3 reports *USB-HID* scancodes (SDL_SCANCODE_*), while WPE wants
 * *evdev* keycodes so its XKB context can interpret them. This module
 * provides the translation table.
 */

#ifndef IMWEBBROWSER_INPUT_KEYMAP_H
#define IMWEBBROWSER_INPUT_KEYMAP_H

#include <cstdint>

#include <SDL3/SDL.h>

namespace imwb {

/* Returns the Linux evdev keycode for the given SDL scancode,
 * or 0 (KEY_RESERVED) if unknown. */
uint32_t keymap_evdev_from_scancode(SDL_Scancode scancode);

} /* namespace imwb */

#endif /* IMWEBBROWSER_INPUT_KEYMAP_H */
