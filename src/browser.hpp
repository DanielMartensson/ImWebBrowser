#pragma once

// Browser core: WPE WebKit integration (view backend, rendering export,
// input dispatch, navigation state). Cog 0.19.1 is the behavioral reference.

#include "config.h"

#include <wpe/webkit.h>
#include <wpe/wpe.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>  // EGLImageKHR
#ifdef IMWB_BACKEND_VULKAN
#include "vk_backend.hpp"
#else
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>  // GLeglImageOES, glEGLImageTargetTexture2DOES
#endif
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>

struct wpe_view_backend;
struct wpe_view_backend_exportable_fdo;
struct wpe_fdo_egl_exported_image;

class Browser {
public:
    static constexpr int kUrlBufSize = 4096;

    Browser() = default;
    bool init(EGLDisplay eglDisplay, int width, int height, const char* startUrl);
    void shutdown();

    Browser(const Browser&) = delete;
    Browser& operator=(const Browser&) = delete;
    Browser(Browser&&) = delete;
    Browser& operator=(Browser&&) = delete;

    // ---- UI-facing state (kept current by WebKit signals) ----
    char urlBuf[kUrlBufSize] = "";   // URL bar text buffer (C-string for ImGui::InputText)
    bool urlEditing = false;         // user is typing in the URL bar
    bool focusUrlRequest = false;    // one-shot: focus the URL bar (Ctrl+L)
    std::string title;               // current page title
    float progress = 1.f;            // estimated load progress 0..1 (1 = idle/done)
    bool loading = false;
    bool alive = true;               // false => the browser should quit

    std::function<void(bool enter)> onDomFullscreen;  // page requests fullscreen

    // Viewport size in device pixels (excludes the toolbar area).
    void resize(int width, int height);

    // Dispatch pending GLib/WebKit events on the main thread.
    void pumpEvents();

    // Input forwarding (device-pixel coordinates relative to the web view).
    void pointerMotion(float x, float y);
    void pointerButton(float x, float y, uint8_t sdlButton, bool pressed);
    void scroll(float x, float y, float dx, float dy);
    // Applies app key bindings first; forwards unconsumed keys to WebKit.
    void key(uint32_t sdlScancode, uint16_t sdlMods, bool pressed);

    // Navigation.
    void loadUrl(std::string url);
    void goBack() { webkit_web_view_go_back(view_); }
    void goForward() { webkit_web_view_go_forward(view_); }
    void reload(bool bypassCache);
    bool canGoBack() const { return webkit_web_view_can_go_back(view_); }
    bool canGoForward() const { return webkit_web_view_can_go_forward(view_); }

    // Acknowledge a DOM fullscreen request after the app honored it.
    void notifyDomFullscreenDone(bool entered)
    {
        if (entered)
            wpe_view_backend_dispatch_did_enter_fullscreen(backend_);
        else
            wpe_view_backend_dispatch_did_exit_fullscreen(backend_);
    }

    // Rendering: import the latest exported WebKit frame, then present it.
    void updateWebTexture();
    void afterPresent();  // call after the swap/present completed
#ifdef IMWB_BACKEND_VULKAN
    VkDmabufFrame* takeDmabufFrame();  // non-null when a fresh frame was bound
#else
    GLuint webTexture() const { return webTexture_; }
#endif
    int viewWidth() const { return width_; }
    int viewHeight() const { return height_; }

    WebKitWebView* webView() { return view_; }

#if ENABLE_BENCHMARK_HARNESS
    void startBenchmark(int fishCount);
#endif

    // Frame export: WebKit hands us dmabuf-backed EGLImages.
    uint64_t statExports_ = 0;  // exported frames received (debug stats)
    bool takeFrameBound()
    {
        bool b = frameBound_;
        frameBound_ = false;
        return b;
    }

    // A navigation may swallow a pending button-up (page swapped mid-click),
    // leaving WebKit stuck in "pressed" state: every later click then acts as
    // a drag. The embedder consumes this flag once and sends a synthetic
    // button-up before the next real press.
    void markPressHeal() { pressHealNeeded_ = true; }
    bool takePressHeal()
    {
        bool b = pressHealNeeded_;
        pressHealNeeded_ = false;
        return b;
    }

private:
    bool frameBound_ = false;  // a fresh web frame was bound since last present
    bool pressHealNeeded_ = true;  // assume stuck until proven otherwise
    uint32_t pressedButtons_ = 0;  // bitmask of held buttons for wpe state field
    void connectSignals();
    void applySettings();
    void showErrorPage(const char* uri, const char* title, const char* message);
    bool handleKeyBinding(uint32_t keysym, uint32_t modifiers);

    struct wpe_view_backend_exportable_fdo* exportable_ = nullptr;
    struct wpe_view_backend* backend_ = nullptr;
    WebKitWebView* view_ = nullptr;
    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;

    // Frame import state. WebKit exports dmabuf-backed EGLImages; we bind the
    // newest one to a GL texture (or export its dma-buf planes to Vulkan) and
    // sample it directly (zero copies). If the GPU path is unavailable WebKit
    // falls back to shared-memory frames, uploaded once per frame (CPU copy).
#ifdef IMWB_BACKEND_VULKAN
    std::map<void*, VkDmabufFrame> dmabufCache_;
    void* vkCurrentKey_ = nullptr;
    bool vkNew_ = false;
    bool exportDmabuf(void* key, EGLImageKHR image);
#else
    enum class FrameSource { None, EglImage, Shm };
    GLuint webTexture_ = 0;
    FrameSource frameSource_ = FrameSource::None;
#endif
    wpe_fdo_egl_exported_image* displayedImage_ = nullptr;  // bound texture
    wpe_fdo_egl_exported_image* pendingImage_ = nullptr;     // not yet shown
    wpe_fdo_egl_exported_image* retireImage_ = nullptr;      // presented once, released after next swap
    int width_ = 0, height_ = 0;

    // xkbcommon: SDL scancodes -> XKB keysyms for WPE input.
    struct xkb_context* xkbCtx_ = nullptr;
    struct xkb_keymap* xkbKeymap_ = nullptr;
    struct xkb_state* xkbState_ = nullptr;
    // keysym -> XKB keycode (evdev + 8), built from the loaded keymap.
    std::unordered_map<uint32_t, uint32_t> symToKeycode_;
    static uint32_t specialKeySym(uint32_t sdlKey);

#ifndef IMWB_BACKEND_VULKAN
    using PFnGlEGLImageTargetTexture2DOES = void(GL_APIENTRYP)(GLenum target, GLeglImageOES image);
    PFnGlEGLImageTargetTexture2DOES glImageTargetTexture2D_ = nullptr;
#endif

#if ENABLE_BENCHMARK_HARNESS
    // Automated WebGL Aquarium benchmark (--bench-fish N). Samples the page's
    // own FPS counter by publishing it through document.title.
    int benchFish_ = 0;
    unsigned benchSamplesTaken_ = 0;
    double benchSamples_[32] = {};
    double benchPendingFps_ = 0;
    std::string benchOrigTitle_;
#endif

    // GLib/WPE callbacks (defined in browser.cpp).
    friend void onExportEglImage(void* data, wpe_fdo_egl_exported_image* image);
    friend void onExportShmBuffer(void* data, struct wpe_fdo_shm_exported_buffer* buffer);
    friend void onNotifyTitle(WebKitWebView* view, GParamSpec* pspec, Browser* self);
    friend gboolean onLoadFailed(WebKitWebView* view, WebKitLoadEvent event, const char* failingUri, GError* error,
                                 Browser* self);
#if ENABLE_BENCHMARK_HARNESS
    friend gboolean benchSample(Browser* self);
    friend gboolean benchBegin(Browser* self);
#endif
};
