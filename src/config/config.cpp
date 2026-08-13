/* ImWebBrowser - runtime configuration. */

#include "config/config.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "config_defaults.h"

namespace imwb {

Config Config::defaults()
{
    Config c{};
    c.backend = IMWEBBROWSER_DEFAULT_BACKEND;
    c.startup_url = IMWEBBROWSER_DEFAULT_STARTUP_URL;
    c.window_width = IMWEBBROWSER_DEFAULT_WINDOW_WIDTH;
    c.window_height = IMWEBBROWSER_DEFAULT_WINDOW_HEIGHT;
    c.fullscreen = IMWEBBROWSER_DEFAULT_WINDOW_FULLSCREEN;
    c.user_agent = IMWEBBROWSER_DEFAULT_USER_AGENT;

    c.javascript = IMWEBBROWSER_DEFAULT_JAVASCRIPT;
    c.webgl = IMWEBBROWSER_DEFAULT_WEBGL;
    c.webrtc = IMWEBBROWSER_DEFAULT_WEBRTC;
    c.media = IMWEBBROWSER_DEFAULT_MEDIA;
    c.media_stream = IMWEBBROWSER_DEFAULT_MEDIA_STREAM;
    c.web_audio = IMWEBBROWSER_DEFAULT_WEB_AUDIO;
    c.fullscreen_api = IMWEBBROWSER_DEFAULT_FULLSCREEN_API;
    c.developer_extras = IMWEBBROWSER_DEFAULT_DEVELOPER_EXTRAS;
    c.sandbox = IMWEBBROWSER_DEFAULT_SANDBOX;
    c.smooth_scrolling = IMWEBBROWSER_DEFAULT_SMOOTH_SCROLLING;

    c.log_level = LogLevel::Info;
    return c;
}

namespace {

bool parse_bool(const std::string& value)
{
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

} /* namespace */

Config Config::from_command_line(int argc, char** argv, bool* ok)
{
    Config c = defaults();
    *ok = true;

    auto need_value = [&](int i, const std::string& flag) -> const char* {
        if (i + 1 >= argc) {
            LOG_ERROR("option %s requires a value", flag.c_str());
            *ok = false;
            return nullptr;
        }
        return argv[i + 1];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            *ok = false;
            return c;
        } else if (arg == "--fullscreen") {
            c.fullscreen = true;
        } else if (arg.rfind("--url=", 0) == 0) {
            c.startup_url = arg.substr(6);
        } else if (arg == "--url" || arg == "-u") {
            const char* v = need_value(i, arg);
            if (!v) return c;
            c.startup_url = v;
            ++i;
        } else if (arg.rfind("--width=", 0) == 0) {
            c.window_width = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--height=", 0) == 0) {
            c.window_height = std::atoi(arg.c_str() + 9);
        } else if (arg == "--no-javascript") {
            c.javascript = false;
        } else if (arg == "--no-webgl") {
            c.webgl = false;
        } else if (arg == "--no-webrtc") {
            c.webrtc = false;
        } else if (arg == "--no-media") {
            c.media = false;
        } else if (arg == "--no-media-stream") {
            c.media_stream = false;
        } else if (arg == "--no-web-audio") {
            c.web_audio = false;
        } else if (arg == "--no-fullscreen-api") {
            c.fullscreen_api = false;
        } else if (arg == "--developer-extras") {
            c.developer_extras = true;
        } else if (arg == "--no-sandbox") {
            c.sandbox = false;
        } else if (arg == "--no-smooth-scrolling") {
            c.smooth_scrolling = false;
        } else if (arg.rfind("--user-agent=", 0) == 0) {
            c.user_agent = arg.substr(13);
        } else if (arg == "--user-agent") {
            const char* v = need_value(i, arg);
            if (!v) return c;
            c.user_agent = v;
            ++i;
        } else if (arg.rfind("--log-level=", 0) == 0) {
            const std::string v = arg.substr(12);
            if (v == "trace") c.log_level = LogLevel::Trace;
            else if (v == "debug") c.log_level = LogLevel::Debug;
            else if (v == "info") c.log_level = LogLevel::Info;
            else if (v == "warn") c.log_level = LogLevel::Warn;
            else if (v == "error") c.log_level = LogLevel::Error;
            else {
                LOG_ERROR("unknown log level: %s", v.c_str());
                *ok = false;
            }
        } else {
            LOG_ERROR("unknown option: %s", arg.c_str());
            *ok = false;
        }
    }

    return c;
}

void Config::print_summary() const
{
    LOG_INFO("Backend            : %s", backend.c_str());
    LOG_INFO("Startup URL        : %s", startup_url.c_str());
    LOG_INFO("Window             : %dx%d%s",
             window_width, window_height, fullscreen ? " (fullscreen)" : "");
    LOG_INFO("JavaScript         : %s", javascript ? "yes" : "no");
    LOG_INFO("WebGL              : %s", webgl ? "yes" : "no");
    LOG_INFO("WebRTC             : %s", webrtc ? "yes" : "no");
    LOG_INFO("Media              : %s", media ? "yes" : "no");
    LOG_INFO("Media stream       : %s", media_stream ? "yes" : "no");
    LOG_INFO("Web audio          : %s", web_audio ? "yes" : "no");
    LOG_INFO("Fullscreen API     : %s", fullscreen_api ? "yes" : "no");
    LOG_INFO("Developer extras   : %s", developer_extras ? "yes" : "no");
    LOG_INFO("Sandbox            : %s", sandbox ? "yes" : "no");
    LOG_INFO("Smooth scrolling   : %s", smooth_scrolling ? "yes" : "no");
}

} /* namespace imwb */
