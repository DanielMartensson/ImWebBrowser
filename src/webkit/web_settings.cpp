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
    webkit_settings_set_enable_webaudio(settings, config.web_audio);
    webkit_settings_set_enable_fullscreen(settings, config.fullscreen_api);
    webkit_settings_set_enable_developer_extras(settings, config.developer_extras);
    webkit_settings_set_enable_smooth_scrolling(settings, config.smooth_scrolling);
    webkit_settings_set_enable_encrypted_media(settings, config.encrypted_media);
    webkit_settings_set_enable_page_cache(settings, config.page_cache);
    webkit_settings_set_enable_dns_prefetching(settings, config.dns_prefetching);
    webkit_settings_set_enable_spatial_navigation(settings, config.spatial_navigation);
    webkit_settings_set_enable_caret_browsing(settings, config.caret_browsing);
    webkit_settings_set_enable_tabs_to_links(settings, config.tabs_to_links);
    webkit_settings_set_enable_site_specific_quirks(settings, config.site_specific_quirks);
    webkit_settings_set_enable_offline_web_application_cache(settings, config.offline_app_cache);
    webkit_settings_set_enable_html5_database(settings, config.html5_database);
    webkit_settings_set_enable_hyperlink_auditing(settings, config.hyperlink_auditing);
    webkit_settings_set_enable_resizable_text_areas(settings, config.resizable_text_areas);
    webkit_settings_set_enable_mock_capture_devices(settings, config.mock_capture_devices);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, config.console_to_stdout);
#if WEBKIT_CHECK_VERSION(2, 45, 3)
    webkit_settings_set_enable_2d_canvas_acceleration(settings, config.accelerated_2d_canvas);
#endif

    if (!config.user_agent.empty())
        webkit_settings_set_user_agent(settings, config.user_agent.c_str());

    return settings;
}

} /* namespace imwb */
