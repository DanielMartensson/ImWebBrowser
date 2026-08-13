/* ImWebBrowser - ImGui browser UI.
 *
 * Draws the toolbar (navigation buttons, address bar, status) and the web
 * view surface, and reports the web view viewport to the browser each frame.
 */

#ifndef IMWEBBROWSER_UI_BROWSER_UI_H
#define IMWEBBROWSER_UI_BROWSER_UI_H

#include <cstdint>
#include <string>

namespace imwb {

class Browser;

class BrowserUi {
public:
    /* The renderer's web view texture, in a backend-neutral form. */
    struct WebViewTexture {
        bool valid = false;
        uint64_t handle = 0;      /* GL texture id or VkDescriptorSet */
        uint32_t width = 0;       /* physical pixels */
        uint32_t height = 0;
    };

    BrowserUi() = default;

    /* Draws one frame of UI. Returns the web view viewport (logical px). */
    void draw(Browser& browser, const WebViewTexture& texture);

    /* True while the address bar (or another text field) owns keyboard
     * input; keyboard events must not reach the web view then. */
    bool wants_keyboard() const { return m_address_editing; }

    /* Focuses the address bar (e.g. on Ctrl+L). */
    void focus_address_bar() { m_focus_address_bar = true; }

    /* Kiosk (fullscreen, chrome-less) mode. Driven by the toolbar button;
     * the application confirms by calling set_kiosk() once the window is
     * actually fullscreen. */
    void set_kiosk(bool on) { m_kiosk = on; }
    bool is_kiosk() const { return m_kiosk; }
    bool consume_kiosk_request()
    {
        bool r = m_kiosk_requested;
        m_kiosk_requested = false;
        return r;
    }

private:
    void draw_toolbar(Browser& browser);

    bool m_focus_address_bar = false;
    bool m_address_editing = false;
    std::string m_last_synced_uri;
    bool m_kiosk = false;
    bool m_kiosk_requested = false;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_UI_BROWSER_UI_H */
