/* ImWebBrowser - WebKitSettings builder. */

#include "webkit/web_settings.h"

#include <wpe/webkit.h>

#include "config/config.h"
#include "logging/log.h"

namespace imwb {

WebKitSettings* web_settings_create(const Config& config)
{
    WebKitSettings* settings = webkit_settings_new();

    webkit_settings_set_enable_javascript(settings, config.javascript);
    webkit_settings_set_enable_webgl(settings, config.webgl);
    webkit_settings_set_enable_webrtc(settings, config.webrtc);
    webkit_settings_set_enable_media(settings, config.media);
    webkit_settings_set_enable_media_stream(settings, config.media_stream);
    webkit_settings_set_enable_mediasource(settings, config.media);
    webkit_settings_set_enable_media_capabilities(settings, config.media);
    /* WPE WebKit 2.38 exposes no per-view web-audio toggle; Web Audio is
     * gated by the media setting above. */
    webkit_settings_set_enable_fullscreen(settings, config.fullscreen_api);
    webkit_settings_set_enable_developer_extras(settings, config.developer_extras);
    webkit_settings_set_enable_smooth_scrolling(settings, config.smooth_scrolling);

    if (!config.user_agent.empty())
        webkit_settings_set_user_agent(settings, config.user_agent.c_str());

    return settings;
}

void web_settings_apply_context(WebKitWebContext* context, const Config& config)
{
    webkit_web_context_set_sandbox_enabled(context, config.sandbox);
}

} /* namespace imwb */
