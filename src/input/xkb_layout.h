/* ImWebBrowser - XKB keyboard layout handling.
 *
 * libwpe's wpe_input_xkb_context hardcodes layout="us" in its
 * xkb_rule_names, ignoring the system keyboard layout. This module creates
 * its own xkb_context + xkb_keymap + xkb_state from the system RMLVO
 * (XKB_DEFAULT_* env vars or via setxkbmap -query) so the web view
 * receives keysyms matching the desktop's layout (e.g. Swedish).
 */

#ifndef IMWEBBROWSER_INPUT_XKB_LAYOUT_H
#define IMWEBBROWSER_INPUT_XKB_LAYOUT_H

#include <cstdint>

namespace imwb {

class XkbLayout {
public:
    XkbLayout() = default;
    ~XkbLayout();

    XkbLayout(const XkbLayout&) = delete;
    XkbLayout& operator=(const XkbLayout&) = delete;

    /* Initialises the xkb context, keymap and state from the system RMLVO.
     * Safe to call once at startup; returns false on failure. */
    bool initialize();

    /* Returns the XKB keysym for the given XKB keycode (evdev + 8) using the
     * current modifier state, or 0 if unmapped. */
    uint32_t keysym_for_keycode(uint32_t keycode, bool pressed);

    /* Not copyable / movable by value. */
private:
    void* m_context = nullptr;   /* xkb_context*  */
    void* m_keymap = nullptr;    /* xkb_keymap*   */
    void* m_state = nullptr;     /* xkb_state*    */
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_INPUT_XKB_LAYOUT_H */