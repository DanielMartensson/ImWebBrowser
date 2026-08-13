/* ImWebBrowser - runtime configuration.
 *
 * Defaults are baked in at compile time by CMake (config_defaults.h).
 * They can be overridden on the command line; see Config::from_command_line().
 */

#ifndef IMWEBBROWSER_CONFIG_CONFIG_H
#define IMWEBBROWSER_CONFIG_CONFIG_H

#include <cstdint>
#include <string>

#include "logging/log.h"

namespace imwb {

struct Config {
    /* Platform / window. */
    std::string backend;        /* Compiled-in backend: "vulkan" or "opengles". */
    std::string startup_url;
    int window_width;
    int window_height;
    bool fullscreen;
    std::string user_agent;     /* Empty -> engine default. */

    /* WebKit feature flags. */
    bool javascript;
    bool webgl;
    bool webrtc;
    bool media;
    bool media_stream;
    bool web_audio;
    bool fullscreen_api;
    bool developer_extras;
    bool sandbox;
    bool smooth_scrolling;

    LogLevel log_level;

    static Config defaults();
    static Config from_command_line(int argc, char** argv, bool* ok);

    void print_summary() const;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_CONFIG_CONFIG_H */
