/* ImWebBrowser - entry point.
 *
 * Parses the command line, sets up logging and starts the application.
 *
 * Build with a single job on constrained machines:
 *
 *     cmake --build build --parallel 1
 */

#include <cstdio>

#include "application/application.h"
#include "config/config.h"
#include "config_defaults.h"
#include "logging/log.h"

namespace {

void print_help()
{
    std::printf(
        "ImWebBrowser %s - a minimal GPU-accelerated web browser\n"
        "Engine: WPE WebKit | UI: Dear ImGui | Platform: SDL3\n"
        "Backend (compile-time): %s\n"
        "\n"
        "Usage: imwebbrowser [options]\n"
        "\n"
        "  -u, --url=<url>          URL to open on startup (default: %s)\n"
        "      --width=<px>         Initial window width\n"
        "      --height=<px>        Initial window height\n"
        "      --fullscreen         Start fullscreen\n"
        "      --user-agent=<str>   Custom user agent string\n"
        "      --no-javascript      Disable JavaScript\n"
        "      --no-webgl           Disable WebGL\n"
        "      --no-webrtc          Disable WebRTC\n"
        "      --no-media           Disable HTML audio/video\n"
        "      --no-media-stream    Disable getUserMedia capture\n"
        "      --no-web-audio       Disable Web Audio\n"
        "      --no-fullscreen-api  Disable the Fullscreen API\n"
        "      --developer-extras   Enable the Web Inspector\n"
        "      --no-sandbox         Disable the WebKit sandbox\n"
        "      --no-smooth-scrolling  Use discrete (not smooth) wheel scrolling\n"
        "      --log-level=<lvl>    trace | debug | info | warn | error\n"
        "  -h, --help               Show this help\n",
        IMWEBBROWSER_VERSION, IMWEBBROWSER_DEFAULT_BACKEND, IMWEBBROWSER_DEFAULT_STARTUP_URL);
}

} /* namespace */

int main(int argc, char** argv)
{
    bool ok = true;
    imwb::Config config = imwb::Config::from_command_line(argc, argv, &ok);
    if (!ok) {
        print_help();
        return 1;
    }

    imwb::log_set_level(config.log_level);
    LOG_INFO("ImWebBrowser %s (backend: %s)", IMWEBBROWSER_VERSION, config.backend.c_str());
    config.print_summary();

    imwb::Application app;
    return app.run(config);
}
