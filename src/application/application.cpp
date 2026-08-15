/* ImWebBrowser - application orchestration. */

#include "application/application.h"

#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>

#include "config_defaults.h"
#include "logging/log.h"
#include "rendering/renderer.h"

#if defined(IMWEBBROWSER_BACKEND_VULKAN)
#include "rendering/vulkan/vulkan_renderer.h"
#else
#include "rendering/opengles/gl_renderer.h"
#endif

namespace imwb {

namespace {

constexpr Uint32 kBackendWindowFlags =
#if defined(IMWEBBROWSER_BACKEND_VULKAN)
    SDL_WINDOW_VULKAN;
#else
    SDL_WINDOW_OPENGL;
#endif

Renderer* create_renderer()
{
#if defined(IMWEBBROWSER_BACKEND_VULKAN)
    return new VulkanRenderer();
#else
    return new GlRenderer();
#endif
}

/* Propagate the system's X11 keyboard layout to libxkbcommon so WPE's XKB
 * context uses the same keymap as the desktop. libxkbcommon reads RMLVO from
 * the XKB_DEFAULT_* env vars (or falls back to "us"); under X11 those are
 * usually unset, so we query `setxkbmap -query` and set them ourselves. */
void propagate_xkb_layout()
{
    if (std::getenv("XKB_DEFAULT_LAYOUT"))
        return; /* Respect an explicit override. */

    FILE* pipe = popen("setxkbmap -query 2>/dev/null", "r");
    if (!pipe)
        return;

    char line[256];
    while (std::fgets(line, sizeof(line), pipe)) {
        char* colon = std::strchr(line, ':');
        if (!colon)
            continue;
        *colon = '\0';
        char* val = colon + 1;
        while (*val == ' ' || *val == '\t')
            ++val;
        /* Strip trailing whitespace / newline. */
        size_t len = std::strlen(val);
        while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == ' ' || val[len - 1] == '\t'))
            val[--len] = '\0';
        if (len == 0)
            continue;

        if (std::strcmp(line, "rules") == 0)
            setenv("XKB_DEFAULT_RULES", val, 1);
        else if (std::strcmp(line, "model") == 0)
            setenv("XKB_DEFAULT_MODEL", val, 1);
        else if (std::strcmp(line, "layout") == 0)
            setenv("XKB_DEFAULT_LAYOUT", val, 1);
        else if (std::strcmp(line, "variant") == 0)
            setenv("XKB_DEFAULT_VARIANT", val, 1);
        else if (std::strcmp(line, "options") == 0)
            setenv("XKB_DEFAULT_OPTIONS", val, 1);
    }
    pclose(pipe);

    LOG_INFO("XKB layout: %s (variant %s, model %s, rules %s)",
             std::getenv("XKB_DEFAULT_LAYOUT") ? std::getenv("XKB_DEFAULT_LAYOUT") : "?",
             std::getenv("XKB_DEFAULT_VARIANT") ? std::getenv("XKB_DEFAULT_VARIANT") : "",
             std::getenv("XKB_DEFAULT_MODEL") ? std::getenv("XKB_DEFAULT_MODEL") : "?",
             std::getenv("XKB_DEFAULT_RULES") ? std::getenv("XKB_DEFAULT_RULES") : "?");
}

} /* namespace */

bool Application::initialize(const Config& config)
{
    m_config = config;

    propagate_xkb_layout();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (!m_window.initialize(config, kBackendWindowFlags)) {
        LOG_ERROR("Window initialization failed");
        return false;
    }

    /* ImGui context + style. */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr;

    m_renderer = create_renderer();
    if (!m_renderer->initialize(m_window)) {
        LOG_ERROR("Renderer initialization failed");
        return false;
    }

    /* Browser (WPE) on top of the renderer's sink + EGL display. */
    if (!m_browser.initialize(config, m_renderer->egl_display_for_wpe(),
                              m_renderer->frame_sink())) {
        LOG_ERROR("Browser initialization failed");
        return false;
    }

    /* Route input into the web view. */
    m_input.set_view_backend(m_browser.page().view_backend());
    m_input.set_view_geometry(0, 0, config.window_width, config.window_height, 1.0f);
    m_input.set_smooth_scrolling(config.smooth_scrolling);
    m_input.initialize_xkb();

    m_browser.page().load_uri(config.startup_url.empty() ? "about:blank" : config.startup_url);

    m_running = true;
    LOG_INFO("ImWebBrowser %s started", IMWEBBROWSER_VERSION);
    return true;
}

void Application::shutdown()
{
    m_input.set_view_backend(nullptr);

    m_browser.shutdown();

    if (m_renderer) {
        m_renderer->shutdown();
        delete m_renderer;
        m_renderer = nullptr;
    }

    ImGui::DestroyContext();
    m_window.shutdown();
    SDL_Quit();
    LOG_INFO("ImWebBrowser exited");
}

bool Application::handle_shortcuts(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
        return false;

    const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
    const bool alt = (event.key.mod & SDL_KMOD_ALT) != 0;

    if (ctrl && event.key.scancode == SDL_SCANCODE_L) {
        m_ui.focus_address_bar();
    } else if (ctrl && event.key.scancode == SDL_SCANCODE_R) {
        m_browser.page().reload();
    } else if (!ctrl && !alt && event.key.scancode == SDL_SCANCODE_F5) {
        m_browser.page().reload();
    } else if (alt && event.key.scancode == SDL_SCANCODE_LEFT) {
        m_browser.page().go_back();
    } else if (alt && event.key.scancode == SDL_SCANCODE_RIGHT) {
        m_browser.page().go_forward();
    } else if (event.key.scancode == SDL_SCANCODE_F11) {
        toggle_kiosk();
        return true;
    } else if (m_kiosk && event.key.scancode == SDL_SCANCODE_ESCAPE) {
        exit_kiosk();
        return true;
    }
    return false;
}

void Application::enter_kiosk()
{
    if (m_kiosk)
        return;
    m_kiosk = true;
    m_ui.set_kiosk(true);
    m_window.set_fullscreen(true);
    LOG_INFO("Entered kiosk mode");
}

void Application::exit_kiosk()
{
    if (!m_kiosk)
        return;
    m_kiosk = false;
    m_ui.set_kiosk(false);
    m_window.set_fullscreen(false);
    LOG_INFO("Exited kiosk mode");
}

void Application::toggle_kiosk()
{
    if (m_kiosk)
        exit_kiosk();
    else
        enter_kiosk();
}

void Application::handle_event(const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        m_running = false;
        break;
    case SDL_EVENT_WINDOW_RESIZED: {
        int w = 0, h = 0;
        m_window.drawable_size(&w, &h);
        m_renderer->on_resize(w, h);
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        m_browser.page().set_activity(true, true, true);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        m_browser.page().set_activity(true, false, true);
        break;
    default:
        break;
    }

    /* Keyboard shortcuts first (they also work while the page has focus). */
    const bool shortcut_handled = handle_shortcuts(event);

    /* Forward input to the web view unless a text field owns the keyboard. */
    if (!m_ui.wants_keyboard() && !shortcut_handled)
        m_input.handle_event(event);
}

void Application::update_window_title()
{
    const std::string title = m_browser.page().title();
    if (title.empty())
        return;

    static std::string last_title;
    const std::string full = "ImWebBrowser - " + title;
    if (full != last_title) {
        last_title = full;
        SDL_SetWindowTitle(m_window.sdl_window(), full.c_str());
    }
}

int Application::run(const Config& config)
{
    if (!initialize(config))
        return 1;

    while (m_running) {
        /* SDL events. */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            handle_event(event);
        }

        /* Begin the ImGui frame (also makes the GL context current). */
        m_renderer->begin_frame();

        /* Service the WPE/GLib main loop: new frames arrive here. */
        m_browser.wpe().pump_main_context();

        /* Build the web view texture handle for the UI. */
        BrowserUi::WebViewTexture texture{};
#if defined(IMWEBBROWSER_BACKEND_VULKAN)
        auto* vk_renderer = static_cast<VulkanRenderer*>(m_renderer);
        texture.valid = vk_renderer->frame_ready();
        texture.handle = static_cast<uint64_t>(vk_renderer->webview_texture());
        texture.width = vk_renderer->frame_width();
        texture.height = vk_renderer->frame_height();
#else
        auto* gl_renderer = static_cast<GlRenderer*>(m_renderer);
        texture.valid = gl_renderer->frame_ready();
        texture.handle = static_cast<uint64_t>(gl_renderer->webview_texture());
        texture.width = gl_renderer->frame_width();
        texture.height = gl_renderer->frame_height();
#endif

        /* UI: toolbar + web view, and report the viewport back to WPE. */
        m_ui.draw(m_browser, texture);

        if (m_ui.consume_kiosk_request())
            enter_kiosk();

        const WebViewViewport& viewport = m_browser.viewport();
        m_input.set_view_geometry(viewport.x, viewport.y, viewport.width, viewport.height,
                                  viewport.scale);

        update_window_title();

        /* Present the frame. */
        m_renderer->render_present();

        if (m_window.minimized())
            SDL_Delay(16);
    }

    shutdown();
    return 0;
}

} /* namespace imwb */
