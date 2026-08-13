/* ImWebBrowser - browser state. */

#include "browser/browser.h"

#include "config/config.h"
#include "logging/log.h"
#include "rendering/webview_frame.h"

namespace imwb {

bool Browser::initialize(const Config& config, void* egl_display, FrameSink& sink)
{
    m_config = config;

    if (!m_wpe.initialize(egl_display)) {
        LOG_ERROR("Browser: WPE initialization failed");
        return false;
    }

    /* Initial viewport: full window in physical pixels. */
    const uint32_t initial_w = static_cast<uint32_t>(config.window_width);
    const uint32_t initial_h = static_cast<uint32_t>(config.window_height);

    if (!m_page.create(sink, initial_w, initial_h, config)) {
        LOG_ERROR("Browser: web page creation failed");
        return false;
    }

    return true;
}

void Browser::shutdown()
{
    m_page.destroy();
    m_wpe.shutdown();
}

void Browser::set_viewport(const WebViewViewport& viewport)
{
    m_viewport = viewport;

    const uint32_t w = static_cast<uint32_t>(viewport.width * viewport.scale);
    const uint32_t h = static_cast<uint32_t>(viewport.height * viewport.scale);
    if (w == 0 || h == 0)
        return;

    m_page.resize(w, h, viewport.scale);
}

} /* namespace imwb */
