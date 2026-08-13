/* ImWebBrowser - application orchestration.
 *
 * Owns the platform window, the renderer, the browser (WPE) and the UI,
 * and drives the frame loop: SDL events -> ImGui frame -> WPE main loop
 * pump -> UI -> present.
 */

#ifndef IMWEBBROWSER_APPLICATION_APPLICATION_H
#define IMWEBBROWSER_APPLICATION_APPLICATION_H

#include <SDL3/SDL.h>

#include "browser/browser.h"
#include "config/config.h"
#include "input/input_translator.h"
#include "platform/window.h"
#include "ui/browser_ui.h"

namespace imwb {

class Renderer;

class Application {
public:
    Application() = default;
    ~Application() = default;

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(const Config& config);

private:
    bool initialize(const Config& config);
    void shutdown();
    void handle_event(const SDL_Event& event);
    bool handle_shortcuts(const SDL_Event& event);
    void update_window_title();
    void enter_kiosk();
    void exit_kiosk();
    void toggle_kiosk();

    Config m_config;
    Window m_window;
    Renderer* m_renderer = nullptr;
    Browser m_browser;
    BrowserUi m_ui;
    InputTranslator m_input;

    bool m_running = false;
    bool m_kiosk = false;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_APPLICATION_APPLICATION_H */
