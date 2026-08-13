/* ImWebBrowser - browser state.
 *
 * Owns the web page and the WPE module, and tracks the web view viewport
 * (logical rectangle + scale) shared with the UI and the input translator.
 */

#ifndef IMWEBBROWSER_BROWSER_BROWSER_H
#define IMWEBBROWSER_BROWSER_BROWSER_H

#include <string>

#include "browser/web_view_viewport.h"
#include "config/config.h"
#include "webkit/wpe_module.h"
#include "webkit/web_page.h"

namespace imwb {

class FrameSink;

class Browser {
public:
    Browser() = default;

    bool initialize(const Config& config, void* egl_display, FrameSink& sink);
    void shutdown();

    WebPage& page() { return m_page; }
    WpeModule& wpe() { return m_wpe; }

    /* Web view viewport in logical coordinates (filled in by the UI). */
    const WebViewViewport& viewport() const { return m_viewport; }
    void set_viewport(const WebViewViewport& viewport);

private:
    WpeModule m_wpe;
    WebPage m_page;
    Config m_config;
    WebViewViewport m_viewport;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_BROWSER_BROWSER_H */
