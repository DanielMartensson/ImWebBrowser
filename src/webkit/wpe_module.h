/* ImWebBrowser - WPE WebKit integration.
 *
 * Initializes the WPEBackend-FDO implementation on the provided EGL display
 * and services the GLib main context that the WPE machinery runs on, which
 * is driven from the SDL frame loop.
 */

#ifndef IMWEBBROWSER_WEBKIT_WPE_MODULE_H
#define IMWEBBROWSER_WEBKIT_WPE_MODULE_H

#include <cstdint>

namespace imwb {

class WpeModule {
public:
    WpeModule() = default;
    ~WpeModule() = default;

    WpeModule(const WpeModule&) = delete;
    WpeModule& operator=(const WpeModule&) = delete;

    /* Binds WPEBackend-FDO to the given EGL display (either the SDL GL
     * context's display or a headless display created by the renderer).
     * Call once, before creating any web views. */
    bool initialize(void* egl_display);

    void shutdown();

    bool initialized() const { return m_initialized; }

    /* Runs one iteration of the default GLib main context (WPE uses it for
     * the internal Wayland compositor, the WebProcess/UIProcess IPC and the
     * frame export callbacks). Must be called from the SDL frame loop. */
    static void pump_main_context();

private:
    bool m_initialized = false;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_WEBKIT_WPE_MODULE_H */
