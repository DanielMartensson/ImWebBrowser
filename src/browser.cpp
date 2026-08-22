#include "browser.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_keyboard.h>
#include <wayland-server-protocol.h>  // wl_shm_buffer accessors + formats (exportable side)
#include <wpe/fdo-egl.h>
#include <wpe/fdo.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Frame export: WebKit hands us dmabuf-backed EGLImages on the GLib main
// thread. We keep the newest one, bind it to a GL texture in
// updateWebTexture() and acknowledge the frame after presenting it so that
// WebKit paces itself to our vsync (same flow as Cog's FDO renderer).
// ---------------------------------------------------------------------------

static Browser* selfOf(void* data) { return static_cast<Browser*>(data); }

void onExportEglImage(void* data, wpe_fdo_egl_exported_image* image)
{
    auto& self = *selfOf(data);

    static bool loggedPath = false;
    if (!loggedPath) {
        loggedPath = true;
        g_message("frame path: dmabuf/EGLImage (zero-copy)");
    }

    const uint32_t w = wpe_fdo_egl_exported_image_get_width(image);
    const uint32_t h = wpe_fdo_egl_exported_image_get_height(image);
    if (w != uint32_t(self.viewWidth()) || h != uint32_t(self.viewHeight())) {
        // Stale geometry (resize in flight): drop and let WebKit continue.
        wpe_view_backend_exportable_fdo_dispatch_frame_complete(self.exportable_);
        wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image(self.exportable_, image);
        return;
    }

    // Acknowledge immediately (like Cog) so WebKit paces itself to its own
    // render speed instead of slipping against our vsync (which locks rAF to
    // half the refresh rate). The EGLImage stays alive through the texture
    // binding even after we release the exportable-side reference.
    wpe_view_backend_exportable_fdo_dispatch_frame_complete(self.exportable_);
    self.statExports_++;

    // Return older buffers, but NEVER the one still bound to the texture:
    // WebKit would start rewriting that dmabuf while we sample it during the
    // present (visible as striped/tiled corruption on fast-repainting pages).
    // The previously displayed image moves to retireImage_ and is released
    // in afterPresent(), i.e. only once it has survived a full swap.
    if (self.retireImage_)
        wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image(self.exportable_, self.retireImage_);
    self.retireImage_ = self.displayedImage_;
    self.displayedImage_ = nullptr;
    if (self.pendingImage_)
        wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image(self.exportable_, self.pendingImage_);
    self.pendingImage_ = image;

    // Wake the GUI thread if it is blocked in SDL_WaitEventTimeout: without
    // this the loop could sit on a fresh frame for up to one heartbeat.
    if (self.frameEventType_) {
        SDL_Event wake{};
        wake.type = self.frameEventType_;
        SDL_PushEvent(&wake);
    }

    // Bind immediately: this callback runs on the main thread with our GL
    // context current (inside pumpEvents), so presenting the newest frame the
    // same iteration removes up to one frame of latency.
    self.updateWebTexture();
}

// CPU fallback for systems where the dmabuf renderer is unavailable. Uploads
// the shared-memory frame into the web texture (one copy per frame).
void onExportShmBuffer(void* data, wpe_fdo_shm_exported_buffer* buffer)
{
    auto& self = *selfOf(data);

    static bool loggedPath = false;
    if (!loggedPath) {
        loggedPath = true;
        g_message("frame path: shared-memory fallback (CPU copy)");
    }

    wl_shm_buffer* shm = wpe_fdo_shm_exported_buffer_get_shm_buffer(buffer);
    const uint32_t w = uint32_t(wl_shm_buffer_get_width(shm));
    const uint32_t h = uint32_t(wl_shm_buffer_get_height(shm));
    const uint32_t format = wl_shm_buffer_get_format(shm);

    if (w == uint32_t(self.viewWidth()) && h == uint32_t(self.viewHeight())
        && (format == WL_SHM_FORMAT_ARGB8888 || format == WL_SHM_FORMAT_XRGB8888)) {
        // An EGLImage-backed texture cannot be re-specified: recreate on switch.
        if (self.frameSource_ == Browser::FrameSource::EglImage) {
            glDeleteTextures(1, &self.webTexture_);
            self.webTexture_ = 0;
            self.frameSource_ = Browser::FrameSource::None;
        }
        if (!self.webTexture_) {
            glGenTextures(1, &self.webTexture_);
            glBindTexture(GL_TEXTURE_2D, self.webTexture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, self.webTexture_);
        }
        wl_shm_buffer_begin_access(shm);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, int(w), int(h), 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE,
                     wl_shm_buffer_get_data(shm));
        wl_shm_buffer_end_access(shm);
        self.frameSource_ = Browser::FrameSource::Shm;
        self.frameBound_ = true;
    } else {
        g_warning("unsupported shm frame %ux%u fmt 0x%x, dropped", w, h, format);
    }

    wpe_view_backend_exportable_fdo_dispatch_frame_complete(self.exportable_);
    wpe_view_backend_exportable_fdo_egl_dispatch_release_shm_exported_buffer(self.exportable_, buffer);
}

static const wpe_view_backend_exportable_fdo_egl_client kExportClient = {
    .export_fdo_egl_image = onExportEglImage,
    .export_shm_buffer = onExportShmBuffer,
};

static bool onDomFullscreenRequest(void* data, bool enable)
{
    auto& self = *selfOf(data);
    if (self.onDomFullscreen)
        self.onDomFullscreen(enable);
    // Accept the request; the app dispatches did_enter/did_exit_fullscreen.
    return true;
}

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------

bool Browser::init(EGLDisplay eglDisplay, int width, int height, const char* startUrl)
{
    width_ = width;
    height_ = height;

    // Select the WPE backend implementation and share our EGL display with it,
    // exactly like Cog's wayland/headless platforms do.
    wpe_loader_init("libWPEBackend-fdo-1.0.so");
    wpe_fdo_initialize_for_egl_display(eglDisplay);

    exportable_ = wpe_view_backend_exportable_fdo_egl_create(&kExportClient, this, width, height);
    if (!exportable_) {
        std::fprintf(stderr, "error: failed to create WPE FDO view backend\n");
        return false;
    }
    backend_ = wpe_view_backend_exportable_fdo_get_view_backend(exportable_);

    // Hand the view backend to WebKit; destroying the view destroys the
    // exportable through this destroy notify (ownership follows Cog).
    auto* wkBackend = webkit_web_view_backend_new(
        backend_, reinterpret_cast<GDestroyNotify>(wpe_view_backend_exportable_fdo_destroy), exportable_);
    view_ = webkit_web_view_new(wkBackend);
    g_object_ref_sink(view_);

    wpe_view_backend_add_activity_state(backend_,
                                        wpe_view_activity_state_visible | wpe_view_activity_state_focused |
                                            wpe_view_activity_state_in_window);
    wpe_view_backend_set_fullscreen_handler(backend_, onDomFullscreenRequest, this);

    applySettings();
    connectSignals();

    // xkbcommon keymap for keysym resolution. SDL scancodes are USB-HID usage
    // IDs, NOT evdev codes, so scancode+8 is wrong; instead we resolve the SDL
    // key to a keysym and look up the matching XKB keycode in the keymap.
    xkbCtx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkbKeymap_ = xkb_keymap_new_from_names(xkbCtx_, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    xkbState_ = xkbKeymap_ ? xkb_state_new(xkbKeymap_) : nullptr;
    if (!xkbState_)
        std::fprintf(stderr, "warning: xkbcommon keymap unavailable, keyboard input disabled\n");
    else {
        const xkb_keysym_t* syms = nullptr;
        for (uint32_t kc = 8; kc < 256; ++kc) {
            const int n = xkb_keymap_key_get_syms_by_level(xkbKeymap_, kc, 0, 0, &syms);
            if (n > 0 && syms && syms[0] != XKB_KEY_NoSymbol)
                symToKeycode_.emplace(syms[0], kc);
        }
    }

    glImageTargetTexture2D_ = reinterpret_cast<PFnGlEGLImageTargetTexture2DOES>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!glImageTargetTexture2D_) {
        std::fprintf(stderr, "error: glEGLImageTargetTexture2DOES unavailable\n");
        return false;
    }

    frameEventType_ = SDL_RegisterEvents(1);
    loadUrl(startUrl);
    return true;
}

void Browser::shutdown()
{
    if (view_)
        g_clear_object(&view_);  // chains: view -> WebViewBackend -> exportable
    exportable_ = nullptr;
    backend_ = nullptr;

    g_clear_pointer(&xkbState_, xkb_state_unref);
    g_clear_pointer(&xkbKeymap_, xkb_keymap_unref);
    g_clear_pointer(&xkbCtx_, xkb_context_unref);

    if (webTexture_)
        glDeleteTextures(1, &webTexture_);
}

void Browser::applySettings()
{
    auto* s = webkit_web_view_get_settings(view_);
    (void)s;  // only touched when a feature option differs from the default
#if ENABLE_JAVASCRIPT != 1
    webkit_settings_set_enable_javascript(s, ENABLE_JAVASCRIPT);
#endif
#if ENABLE_WEBGL != 1
    webkit_settings_set_enable_webgl(s, ENABLE_WEBGL);
#endif
    // --- scrolling / caching -------------------------------------------------
#if ENABLE_SMOOTH_SCROLLING != 1
    webkit_settings_set_enable_smooth_scrolling(s, ENABLE_SMOOTH_SCROLLING);
#endif
#if ENABLE_PAGE_CACHE != 1
    webkit_settings_set_enable_page_cache(s, ENABLE_PAGE_CACHE);
#endif
#if ENABLE_DNS_PREFETCH != 1
    webkit_settings_set_enable_dns_prefetching(s, ENABLE_DNS_PREFETCH);
#endif
#if ENABLE_2D_CANVAS != 1
    webkit_settings_set_enable_2d_canvas_acceleration(s, ENABLE_2D_CANVAS);
#endif
    // --- accessibility & input ----------------------------------------------
#if ENABLE_CARET_BROWSING
    webkit_settings_set_enable_caret_browsing(s, true);
#endif
#if ENABLE_SPATIAL_NAVIGATION
    webkit_settings_set_enable_spatial_navigation(s, true);
#endif
#if ENABLE_TAB_FOCUS_CYCLE != 1
    webkit_settings_set_enable_tab_key_cycles_through_elements(s, ENABLE_TAB_FOCUS_CYCLE);
#endif
#if ENABLE_TEXT_AREAS_RESIZE != 1
    webkit_settings_set_enable_resizable_text_areas(s, ENABLE_TEXT_AREAS_RESIZE);
#endif
#if ENABLE_BF_GESTURES
    webkit_settings_set_enable_back_forward_navigation_gestures(s, true);
#endif
    // --- media ----------------------------------------------------------------
#if ENABLE_WEBRTC != 1
    webkit_settings_set_enable_webrtc(s, ENABLE_WEBRTC);
    webkit_settings_set_enable_media_stream(s, ENABLE_WEBRTC);
#endif
#if ENABLE_MEDIA != 1
    webkit_settings_set_enable_mediasource(s, ENABLE_MEDIA);
    webkit_settings_set_media_playback_allows_inline(s, ENABLE_MEDIA);
#endif
#if ENABLE_MEDIA_CAPABILITIES != 1
    webkit_settings_set_enable_media_capabilities(s, ENABLE_MEDIA_CAPABILITIES);
#endif
#if ENABLE_AUTOPLAY
    webkit_settings_set_media_playback_requires_user_gesture(s, false);
#endif
#if ENABLE_AUDIO != 1
    webkit_settings_set_enable_webaudio(s, ENABLE_AUDIO);
#endif
#if ENABLE_ENCRYPTED_MEDIA != 1
    webkit_settings_set_enable_encrypted_media(s, ENABLE_ENCRYPTED_MEDIA);
#endif
#if ENABLE_DEVELOPER_EXTRAS
    webkit_settings_set_enable_developer_extras(s, true);
#endif
}

// ---------------------------------------------------------------------------
// Signals: WebKit is the source of truth for all navigation state.
// ---------------------------------------------------------------------------

static void onNotifyUri(WebKitWebView* view, GParamSpec*, Browser* self)
{
    const char* uri = webkit_web_view_get_uri(view) ? webkit_web_view_get_uri(view) : "";
    if (!self->urlEditing)
        snprintf(self->urlBuf, sizeof(self->urlBuf), "%s", uri);
}

static void onNotifyProgress(WebKitWebView* view, GParamSpec*, Browser* self)
{
    self->progress = std::clamp(float(webkit_web_view_get_estimated_load_progress(view)), 0.f, 1.f);
}

void onNotifyTitle(WebKitWebView* view, GParamSpec*, Browser* self)
{
    const char* t = webkit_web_view_get_title(view);
    self->title = t ? t : "";
#if ENABLE_BENCHMARK_HARNESS
    if (self->benchFish_ && self->title.starts_with("FPS:"))
        self->benchPendingFps_ = atof(self->title.c_str() + 4);
#endif
}

static void onLoadChanged(WebKitWebView*, WebKitLoadEvent event, Browser* self)
{
    switch (event) {
    case WEBKIT_LOAD_STARTED:
        self->loading = true;
        // Covers JS/location navigations too: a click held across this
        // boundary loses its button-up inside the old page.
        self->markPressHeal();
        break;
    case WEBKIT_LOAD_FINISHED:
        self->loading = false;
        self->progress = 1.f;
        break;
    default:
        break;
    }
}

gboolean onLoadFailed(WebKitWebView*, WebKitLoadEvent, const char* failingUri, GError* error, Browser* self)
{
    // Navigations cancelled by a new load or policy decision are not errors.
    if (g_error_matches(error, WEBKIT_POLICY_ERROR, WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE) ||
        g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED))
        return FALSE;

    char message[512];
    snprintf(message, sizeof(message), "%s (%s %d)", error->message ? error->message : "unknown error",
             g_quark_to_string(error->domain), error->code);
    self->showErrorPage(failingUri, "Page load failed", message);
    return TRUE;
}

static void onLoadFailedTls(WebKitWebView*, const char* host, GTlsCertificate*, GTlsCertificateFlags flags, Browser*)
{
    g_warning("TLS certificate error for '%s': flags 0x%x (certificate not accepted)", host, unsigned(flags));
}

struct ProcessRestart {
    unsigned tries = 0;
    unsigned maxTries = 3;
    unsigned resetId = 0;
};

static gboolean resetProcessRestart(ProcessRestart* r)
{
    r->tries = 0;
    r->resetId = 0;
    return G_SOURCE_REMOVE;
}

static gboolean onWebProcessTerminated(WebKitWebView* view, WebKitWebProcessTerminationReason reason, Browser*)
{
    static ProcessRestart restart;  // single-view browser: one static is enough
    if (++restart.tries > restart.maxTries) {
        g_critical("Web process terminated repeatedly (reason %d); giving up", int(reason));
        restart.tries = 0;
        return FALSE;
    }
    g_warning("Web process terminated (reason %d), restarting (attempt %u/%u)", int(reason), restart.tries,
              restart.maxTries);
    if (restart.resetId)
        g_source_remove(restart.resetId);
    restart.resetId = g_timeout_add_seconds(60, G_SOURCE_FUNC(resetProcessRestart), &restart);
    webkit_web_view_reload(view);
    return TRUE;
}

static void onCloseRequest(WebKitWebView*, Browser* self)
{
    g_message("VIEW 'close' signal -> quitting");
    self->alive = false;
}

void Browser::connectSignals()
{
    g_signal_connect(view_, "notify::uri", G_CALLBACK(onNotifyUri), this);
    g_signal_connect(view_, "notify::estimated-load-progress", G_CALLBACK(onNotifyProgress), this);
    g_signal_connect(view_, "notify::title", G_CALLBACK(onNotifyTitle), this);
    g_signal_connect(view_, "load-changed", G_CALLBACK(onLoadChanged), this);
    g_signal_connect(view_, "load-failed", G_CALLBACK(onLoadFailed), this);
    g_signal_connect(view_, "load-failed-with-tls-errors", G_CALLBACK(onLoadFailedTls), this);
    g_signal_connect(view_, "web-process-terminated", G_CALLBACK(onWebProcessTerminated), this);
    g_signal_connect(view_, "close", G_CALLBACK(onCloseRequest), this);
}

// ---------------------------------------------------------------------------
// Error pages (simplified from Cog's cog_handle_web_view_load_failed).
// ---------------------------------------------------------------------------

static void escapeHtml(std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '\'': out += "&#39;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    s = std::move(out);
}

void Browser::showErrorPage(const char* uri, const char* errorTitle, const char* message)
{
    g_warning("<%s> %s: %s", uri, errorTitle, message);

    std::string u = uri, t = errorTitle, m = message;
    escapeHtml(u);
    escapeHtml(m);
    char html[4096];
    snprintf(html, sizeof(html),
             "<!DOCTYPE html><html><head><title>%s</title><style>"
             "body{background:#fffafa;color:#111;font-family:sans-serif;margin:2em}"
             "h3{background:#555;color:#fffafa;padding:.3em .6em;border-radius:4px}"
             ".uri{font-family:monospace;color:#888}"
             "</style></head><body><h3>%s</h3><p class='uri'>%s</p><p>%s</p>"
             "<button onclick=\"window.location.href='%s'\">Try again</button></body></html>",
             t.c_str(), t.c_str(), u.c_str(), m.c_str(), u.c_str());
    webkit_web_view_load_alternate_html(view_, html, uri, nullptr);
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void Browser::loadUrl(std::string url)
{
    while (!url.empty() && (url.back() == ' ' || url.back() == '\n' || url.back() == '\r'))
        url.pop_back();
    if (url.empty())
        return;

    // Classification order: explicit scheme -> local path -> domain -> search.
    if (url.find("://") == std::string::npos) {
        const bool looksLikeDomain =
            url.rfind("localhost", 0) == 0 ||
            (url.find('.') != std::string::npos && url.find(' ') == std::string::npos);
        if (!url.empty() && url[0] == '/') {
            url = "file://" + url;
        } else if (looksLikeDomain) {
            url = "https://" + url;
        } else {  // anything else is a web search
            std::string query;
            char hex[4];
            for (unsigned char c : url) {
                if (g_ascii_isalnum(c) || strchr("-_.~", c))
                    query += char(c);
                else if (c == ' ')
                    query += '+';
                else {
                    snprintf(hex, sizeof(hex), "%%%02X", c);
                    query += hex;
                }
            }
            url = "https://duckduckgo.com/?q=" + query;
        }
    }

    snprintf(urlBuf, sizeof(urlBuf), "%s", url.c_str());
    progress = 0.f;
    loading = true;
    markPressHeal();  // page swap may swallow a held button-up
    if (g_getenv("IMWB_DEBUG_INPUT"))
        std::fprintf(stderr, "[nav] loadUrl -> '%s'\n", url.c_str());
    webkit_web_view_load_uri(view_, url.c_str());
}

void Browser::reload(bool bypassCache)
{
    markPressHeal();
    if (bypassCache)
        webkit_web_view_reload_bypass_cache(view_);
    else
        webkit_web_view_reload(view_);
}

// ---------------------------------------------------------------------------
// Input forwarding (SDL -> wpe_input_* -> view backend). Mirrors the event
// construction in Cog's wayland platform.
// ---------------------------------------------------------------------------

void Browser::pointerMotion(float x, float y)
{
    // Mirror Cog/reference: motion carries the pressed-buttons bitmask so
    // WebKit can maintain drag state between button transitions.
    wpe_input_pointer_event ev = {wpe_input_pointer_event_type_motion, uint32_t(g_get_monotonic_time() / 1000),
                                  int(x), int(std::max(0.f, y)), 0, pressedButtons_};
    if (g_getenv("IMWB_DEBUG_INPUT"))
        std::fprintf(stderr, "[input] motion at (%.0f,%.0f) t=%u\n", x, y, (unsigned)(ev.time % 100000));
    wpe_view_backend_dispatch_pointer_event(backend_, &ev);
}

void Browser::pointerButton(float x, float y, uint8_t sdlButton, bool pressed)
{
    // SDL buttons 1..5 -> WPE button numbers (1=left, 2=right, 3=middle,
    // 4=back, 5=forward). NOT Linux BTN_* keycodes — WebKit switches on the
    // plain integers and ignores anything else.
    static constexpr uint32_t kBtnMap[] = {1, 3, 2, 4, 5};
    const uint32_t btn = (sdlButton >= 1 && sdlButton <= 5) ? kBtnMap[sdlButton - 1] : 0;

    // state = bitmask of currently pressed buttons (left=1<<20, right=1<<21,
    // middle=1<<22).
    const uint32_t bit = sdlButton == 1 ? 1u << 20 : sdlButton == 3 ? 1u << 21
                                       : sdlButton == 2             ? 1u << 22
                                                                      : 0;
    if (pressed)
        pressedButtons_ |= bit;
    else
        pressedButtons_ &= ~bit;

    wpe_input_pointer_event ev = {wpe_input_pointer_event_type_button, uint32_t(g_get_monotonic_time() / 1000),
                                  int(x), int(std::max(0.f, y)), btn, pressedButtons_};
    if (g_getenv("IMWB_DEBUG_INPUT"))
        std::fprintf(stderr, "[input] button sdl=%u wpe=%u %s at (%.0f,%.0f) t=%u\n", sdlButton, btn,
                     pressed ? "down" : "up", x, y, (unsigned)(ev.time % 100000));
    wpe_view_backend_dispatch_pointer_event(backend_, &ev);
}

void Browser::scroll(float x, float y, float dx, float dy)
{
    wpe_input_axis_2d_event ev = {};
    ev.base.type = static_cast<wpe_input_axis_event_type>(wpe_input_axis_event_type_mask_2d |
                   wpe_input_axis_event_type_motion_smooth);
    ev.base.time = uint32_t(g_get_monotonic_time() / 1000);
    ev.base.x = int(x);
    ev.base.y = int(std::max(0.f, y));
    ev.x_axis = dx * 10.0;  // one wheel notch == 10, matching Wayland units
    ev.y_axis = dy * 10.0;
    wpe_view_backend_dispatch_axis_event(backend_, &ev.base);
}

bool Browser::handleKeyBinding(uint32_t sym, uint32_t mods)
{
    static constexpr float kZoomStep = 0.1f;
    const bool ctrl = mods & wpe_input_keyboard_modifier_control;
    const bool shift = mods & wpe_input_keyboard_modifier_shift;
    const bool alt = mods & wpe_input_keyboard_modifier_alt;

    if (ctrl && !shift && !alt && sym == XKB_KEY_w) {
        alive = false;  // Ctrl+W: quit
        return true;
    }
    if (ctrl && !alt && (sym == XKB_KEY_plus || sym == XKB_KEY_equal)) {
        webkit_web_view_set_zoom_level(view_, webkit_web_view_get_zoom_level(view_) + kZoomStep);
        return true;
    }
    if (ctrl && !shift && !alt && sym == XKB_KEY_minus) {
        webkit_web_view_set_zoom_level(view_, webkit_web_view_get_zoom_level(view_) - kZoomStep);
        return true;
    }
    if (ctrl && !shift && !alt && sym == XKB_KEY_0) {
        webkit_web_view_set_zoom_level(view_, 1.0);
        return true;
    }
    if (alt && !ctrl && !shift && sym == XKB_KEY_Left) {
        goBack();
        return true;
    }
    if (alt && !ctrl && !shift && sym == XKB_KEY_Right) {
        goForward();
        return true;
    }
    if (ctrl && !shift && !alt && (sym == XKB_KEY_r || sym == XKB_KEY_R)) {
        reload(shift);  // Ctrl+R reload, Ctrl+Shift+R bypass cache
        return true;
    }
    if (!ctrl && !alt && (sym == XKB_KEY_F5)) {
        reload(shift);  // F5 reload, Shift+F5 bypass cache
        return true;
    }
    return false;
}

uint32_t Browser::specialKeySym(uint32_t sdlKey)
{
    switch (sdlKey) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return XKB_KEY_Return;
    case SDLK_ESCAPE:
        return XKB_KEY_Escape;
    case SDLK_BACKSPACE:
        return XKB_KEY_BackSpace;
    case SDLK_TAB:
        return XKB_KEY_Tab;
    case SDLK_DELETE:
        return XKB_KEY_Delete;
    case SDLK_INSERT:
        return XKB_KEY_Insert;
    case SDLK_HOME:
        return XKB_KEY_Home;
    case SDLK_END:
        return XKB_KEY_End;
    case SDLK_PAGEUP:
        return XKB_KEY_Page_Up;
    case SDLK_PAGEDOWN:
        return XKB_KEY_Page_Down;
    case SDLK_LEFT:
        return XKB_KEY_Left;
    case SDLK_RIGHT:
        return XKB_KEY_Right;
    case SDLK_UP:
        return XKB_KEY_Up;
    case SDLK_DOWN:
        return XKB_KEY_Down;
    default:
        break;
    }
    if (sdlKey >= SDLK_F1 && sdlKey <= SDLK_F12)
        return XKB_KEY_F1 + (sdlKey - SDLK_F1);
    return 0;
}

void Browser::key(uint32_t sdlScancode, uint16_t sdlMods, bool pressed)
{
    if (!xkbState_)
        return;

    // Resolve the character through SDL so the ACTIVE server layout (se,us
    // here) decides the symbol, including Shift and AltGr (@, $ ...). WebKit
    // derives the inserted text from this keysym. The keymap scan only maps
    // it back to a physical keycode for state tracking / DOM .code.
    const SDL_Keycode sdlKey =
        SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sdlScancode), static_cast<SDL_Keymod>(sdlMods), false);
    uint32_t sym = 0;
    if (sdlKey & SDLK_SCANCODE_MASK)
        sym = specialKeySym(sdlKey);
    else
        sym = xkb_utf32_to_keysym(sdlKey);
    if (!sym)
        return;

    auto keycodeFor = [this](uint32_t s) -> uint32_t {
        if (s) {
            if (auto it = symToKeycode_.find(s); it != symToKeycode_.end())
                return it->second;
        }
        return 0;
    };
    uint32_t keycode = keycodeFor(sym);
    if (!keycode) {
        // Retry with the unmodified key so state tracking stays consistent.
        const SDL_Keycode base =
            SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sdlScancode), SDL_KMOD_NONE, false);
        const uint32_t bsym = (base & SDLK_SCANCODE_MASK) ? specialKeySym(base) : xkb_utf32_to_keysym(base);
        keycode = keycodeFor(bsym);
    }
    if (!keycode)
        keycode = sdlScancode + 8;  // best effort fallback

    uint32_t mods = 0;
    if (sdlMods & SDL_KMOD_CTRL)
        mods |= wpe_input_keyboard_modifier_control;
    if (sdlMods & SDL_KMOD_SHIFT)
        mods |= wpe_input_keyboard_modifier_shift;
    if (sdlMods & SDL_KMOD_ALT)
        mods |= wpe_input_keyboard_modifier_alt;
    if (sdlMods & SDL_KMOD_GUI)
        mods |= wpe_input_keyboard_modifier_meta;

    xkb_state_update_key(xkbState_, keycode, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

    if (pressed && handleKeyBinding(sym, mods))
        return;

    wpe_input_keyboard_event ev = {uint32_t(g_get_monotonic_time() / 1000), sym, keycode, pressed, mods};
    if (g_getenv("IMWB_DEBUG_INPUT"))
        std::fprintf(stderr, "[input] key sym=0x%x kc=%u %s mods=0x%x\n", sym, keycode, pressed ? "down" : "up",
                     mods);
    wpe_view_backend_dispatch_keyboard_event(backend_, &ev);
}

// ---------------------------------------------------------------------------
// Rendering integration
// ---------------------------------------------------------------------------

void Browser::resize(int width, int height)
{
    if (width == width_ && height == height_)
        return;
    width_ = width;
    height_ = height;
    wpe_view_backend_dispatch_set_size(backend_, width, height);
}

void Browser::updateWebTexture()
{
    if (!pendingImage_)
        return;

    // A shm-uploaded texture cannot be rebound to an EGLImage: recreate.
    if (frameSource_ == FrameSource::Shm) {
        glDeleteTextures(1, &webTexture_);
        webTexture_ = 0;
        frameSource_ = FrameSource::None;
    }

    if (!webTexture_) {
        glGenTextures(1, &webTexture_);
        glBindTexture(GL_TEXTURE_2D, webTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, webTexture_);
    }

    // Zero-copy: bind the exported EGLImage as the texture storage.
    glImageTargetTexture2D_(GL_TEXTURE_2D, wpe_fdo_egl_exported_image_get_egl_image(pendingImage_));
    frameSource_ = FrameSource::EglImage;

    // The previous image was already returned to WebKit in the export
    // callback; the texture's own EGLImage reference kept its storage alive.
    displayedImage_ = pendingImage_;
    pendingImage_ = nullptr;
    frameBound_ = true;
}

void Browser::afterPresent()
{
    // The swap finished: the previously displayed buffer is no longer
    // sampled, so WebKit may reuse it from here on.
    if (retireImage_) {
        wpe_view_backend_exportable_fdo_egl_dispatch_release_exported_image(exportable_, retireImage_);
        retireImage_ = nullptr;
    }
}

void Browser::pumpEvents()
{
    while (g_main_context_pending(nullptr))
        g_main_context_iteration(nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Benchmark harness: loads the WebGL Aquarium with a fixed fish count and
// samples the page's own FPS counter once per second. The counter is read by
// publishing it through document.title, which arrives via notify::title - no
// JavaScriptCore headers required.
// ---------------------------------------------------------------------------

#if ENABLE_BENCHMARK_HARNESS

static void onEvalDone(GObject*, GAsyncResult*, gpointer) {}

gboolean benchSample(Browser* self)
{
    if (self->benchPendingFps_ > 0 && self->benchSamplesTaken_ < 32) {
        self->benchSamples_[self->benchSamplesTaken_++] = self->benchPendingFps_;
        std::fprintf(stderr, "[bench] sample %2u: %6.1f fps\n", self->benchSamplesTaken_, self->benchPendingFps_);
    }
    self->benchPendingFps_ = 0;

    if (self->benchSamplesTaken_ >= 32) {
        double lo = 1e9, hi = 0, sum = 0;
        for (double f : self->benchSamples_) {
            if (f <= 0)
                continue;
            lo = std::min(lo, f);
            hi = std::max(hi, f);
            sum += f;
        }
        std::fprintf(stderr,
                     "[bench] RESULT: %d fish, avg %.1f fps, min %.1f fps, max %.1f fps at %dx%d\n",
                     self->benchFish_, sum / self->benchSamplesTaken_, lo, hi, self->viewWidth(),
                     self->viewHeight());
        // Restore the page title.
        webkit_web_view_evaluate_javascript(self->view_,
                                            "document.title='WebGL Aquarium'", -1, nullptr, nullptr, nullptr,
                                            onEvalDone, nullptr);
        return G_SOURCE_REMOVE;
    }

    webkit_web_view_evaluate_javascript(self->view_,
                                        "document.title='FPS:'+(document.getElementById('fps')?.textContent||'0')",
                                        -1, nullptr, nullptr, nullptr, onEvalDone, nullptr);
    return G_SOURCE_CONTINUE;
}

gboolean benchBegin(Browser* self)
{
    std::fprintf(stderr, "[bench] sampling (%d fish)...\n", self->benchFish_);
    g_timeout_add_seconds(1, G_SOURCE_FUNC(benchSample), self);
    return G_SOURCE_REMOVE;
}

void Browser::startBenchmark(int fishCount)
{
    benchFish_ = fishCount;
    char url[256];
    snprintf(url, sizeof(url), "https://webglsamples.org/aquarium/aquarium.html?numFish=%d", fishCount);
    loadUrl(url);
    g_timeout_add_seconds(8, G_SOURCE_FUNC(benchBegin), this);  // load + settle
}

#endif  // ENABLE_BENCHMARK_HARNESS
