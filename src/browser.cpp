#include "browser.hpp"

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
#ifdef IMWB_BACKEND_VULKAN
        // CPU fallback frames have no dma-buf to import; with a working EGL
        // GPU path WebKit never sends them.
        g_warning("shm frame received but Vulkan build cannot render it");
#else
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
#endif
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
    eglDisplay_ = eglDisplay;

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

    // NVIDIA's login CDN serves stale HTML while it rolls out new builds; the
    // old runtime then imports chunk files that were emptied by the deploy and
    // the OAuth SPA aborts before mounting the form. Never re-serve HTML from
    // the disk cache so every session/attempt lands on what the origin says.
    webkit_web_context_set_cache_model(webkit_web_context_get_default(),
                                       WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);

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

#ifndef IMWB_BACKEND_VULKAN
    glImageTargetTexture2D_ = reinterpret_cast<PFnGlEGLImageTargetTexture2DOES>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!glImageTargetTexture2D_) {
        std::fprintf(stderr, "error: glEGLImageTargetTexture2DOES unavailable\n");
        return false;
    }
#endif

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

#ifndef IMWB_BACKEND_VULKAN
    if (webTexture_)
        glDeleteTextures(1, &webTexture_);
#endif
}

void Browser::applySettings()
{
    auto* s = webkit_web_view_get_settings(view_);
    (void)s;  // only touched when a feature option differs from the default
    // Sites that gate on browser identity (GeForce Now, some DRM/streaming)
    // reject WPE's default/mobile-ish UA; override via IMWB_USER_AGENT.
    if (const char* ua = g_getenv("IMWB_USER_AGENT")) {
        webkit_settings_set_user_agent(s, ua);
        std::fprintf(stderr, "[ua] overriding user agent: %s\n", ua);
    }
    // GeForce Now etc. need RTCPeerConnection on window; it is runtime-gated
    // and off unless enabled (page then dies at "Can't find variable:
    // RTCPeerConnection" and never leaves its splash screen).
    webkit_settings_set_enable_webrtc(s, true);
    std::fprintf(stderr, "[webrtc] enable_webrtc now = %d\n",
                 webkit_settings_get_enable_webrtc(s));
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

static void onProbeFinished(GObject*, GAsyncResult* res, gpointer userData)
{
    auto* view = WEBKIT_WEB_VIEW(userData);
    auto* value = webkit_web_view_evaluate_javascript_finish(view, res, nullptr);
    if (!value)
        return;
    char* txt = nullptr;
    if (jsc_value_is_string(value))
        txt = jsc_value_to_string(value);
    if (txt) {
        std::fprintf(stderr, "[probe] %s\n", txt);
        g_free(txt);
    }
}

namespace {
constexpr const char* kLogProbe = R"JSX((function(){
  var s='(no capture)';
  try{ s = (window.__imwbLog&&window.__imwbLog.length) ? window.__imwbLog.slice(-50).join('\n') : '(window.__imwbLog missing)'; }
  catch(e){ s='capture err'; }
  var bodyTxt=document.body?document.body.innerText.replace(/\s+/g,' ').slice(0,100):'';
  var bodyKids = document.body ? document.body.childElementCount : -1;
  var app = document.getElementById('app');
  var resNum = 0;
  try { resNum = performance.getEntriesByType ? performance.getEntriesByType('resource').length : 0; } catch(e){}
  var cke='?', ckl='?', ls=0;
  try { cke = navigator.cookieEnabled ? 'y' : 'n'; ckl = document.cookie ? document.cookie.length : 0; } catch(e){}
  try { ls = window.localStorage ? window.localStorage.length : -2; } catch(e){ ls = -1; }
  var ch=[];
  try { [].forEach.call(document.querySelectorAll('script[src]'), function(s){ var m=/chunk-[A-Za-z0-9]+\.js/.exec(s.src); if(m) ch.push(m[0]); }); } catch(e){}
  try { var im=document.querySelector('link[rel="importmap"]'); if(im && im.textContent) ch.push.apply(ch, im.textContent.match(/chunk-[A-Za-z0-9]+\.js/g)||[]); } catch(e){}
  var chs=ch.filter(function(v,i,a){return a.indexOf(v)==i;}).join(',');
  var sr=[];
  try { [].forEach.call(document.querySelectorAll('script[src],link[rel="modulepreload"]'), function(n){ var u=n.src||n.href; if(u) sr.push(u.replace(/^.*\//,'')); }); } catch(e){}
  sr=sr.slice(0,10);
  return 'LOG<<\n'+s+'\n>>LOG bodyChars='+(document.body?document.body.innerText.length:-1)
    +' txt=['+bodyTxt+']'
    +' bodyKids='+bodyKids
    +' idAppKids='+(app?app.childElementCount:'no-app')
    +' res='+resNum
    +' cke='+cke+' ckl='+ckl+' ls='+ls
    +' chunks=['+chs+']'
    +' scripts=['+sr.join(',')+']'
    +' loc='+location.href
    +' ready='+document.readyState+' title='+document.title+' | '+bodyTxt;
})())JSX";

constexpr const char* kCaptureScript = R"JSX((function(){
  if (window.__imwbLog) return;
  window.__imwbLog = [];
  var lvl = ['log','info','warn','error','debug'];
  function make(n){ return function(){ var a=Array.prototype.map.call(arguments,function(x){
      try {
        if (x instanceof Error || (x && (x.message!==undefined || typeof x.stack==='string')))
          return '['+(x.name||'Error')+'] '+(x.message||'')+' @ '+(x.stack?String(x.stack).split('\n').slice(0,3).join(' | '):'');
        return (typeof x==='string')?x:JSON.stringify(x);
      } catch(e){ return String(x); } }).join(' ');
    window.__imwbLog.push(n+': '+a); }; }
  lvl.forEach(function(n){ try{ console[n]=make(n); }catch(e){} });
  try {
    window.__realOpen = window.open;
    window.open = function(url, name, features){
      window.__imwbLog.push('OPEN:'+url+' name='+name);
      var navName = String(name||'');
      if (navName === '_self' || navName === '_top') {
        try { window.location.href = url; } catch(e){ window.__imwbLog.push('NAV_ERR:'+String(e)); }
      }
      var w = { closed:false, document:null, location:{href:String(url||'')}, onload:null,
        focus:function(){}, blur:function(){}, close:function(){window.__imwbLog.push('OPEN_CLOSED')},
        postMessage:function(){}, addEventListener:function(){}, removeEventListener:function(){} };
      return w;
    };
  } catch(e){ window.__imwbLog.push('OPENWRAP_ERR:'+String(e)); }
  try {
    var _f = window.fetch;
    window.fetch = function(){
      var args = arguments;
      return _f.apply(this, args).then(function(r){
        if (r.status >= 400)
          window.__imwbLog.push('FETCH_FAIL:'+String(r.url||'').slice(0,200)+' '+r.status);
        return r;
      }, function(e){
        window.__imwbLog.push('FETCH_ERR:'+String(args[0]).slice(0,200));
        throw e;
      });
    };
    var _xo = XMLHttpRequest.prototype.open;
    XMLHttpRequest.prototype.open = function(m, u){ this.__imwbU = u; return _xo.apply(this, arguments); };
    var _xs = XMLHttpRequest.prototype.send;
    XMLHttpRequest.prototype.send = function(){
      var self = this;
      this.addEventListener('loadend', function(){
        if (self.status >= 400)
          window.__imwbLog.push('XHR_FAIL:'+String(self.__imwbU||'').slice(0,200)+' '+self.status);
      });
      return _xs.apply(this, arguments);
    };
  } catch(e){ window.__imwbLog.push('NETWRAP_ERR:'+String(e)); }
  window.addEventListener('error', function(e){ window.__imwbLog.push('UNCAUGHT: '+(e.message||'?')+' @ '+(e.filename||'')+':'+(e.lineno||'?')); });
  (function(){
    var c = document.currentScript ? document.currentScript.getAttribute('data-src') : '';
    if (!c) return;
    window.__imwbLog.push('CURSRC:'+(c||'').slice(0,160));
  })();
  window.addEventListener('unhandledrejection', function(e){ var r=e.reason;
window.__imwbLog.push('REJECTION: '+(r?(r.message?r.message:String(r)):'?')); });
})())JSX";
}

// On the NVIDIA login page the OAuth SPA lazily imports a chunk whose module
// load failed with a MIME type error. Ask the page what it truly receives for
// that chunk (fresh bypass + default cache) to see content-type and length.
constexpr const char* kChunkProbe = R"JSX((function(){
  if (window.__imwbChunkDone) return '';
  if (!/login\.nvgs\.nvidia\.com/.test(location.host)) return '';
  window.__imwbChunkDone = true;
  var log = window.__imwbLog;
  var failed = [];
  ['UC7ZX2T5','WQKKENAX','3QP5QXBT'].forEach(function(tag){
    var u = 'chunk-'+tag+'.js';
    var c = new AbortController();
    var t = setTimeout(function(){ c.abort(); log.push('CHUNK_TIMEOUT '+tag); }, 8000);
    fetch(u, {cache:'no-store', signal:c.signal}).then(function(r){
      clearTimeout(t);
      log.push('CHUNK ' + tag + ': status=' + r.status + ' ct="' + (r.headers.get('content-type')||'none') + '" len=' + r.status+'' );
      return r.text().then(function(tx){ log.push('CHUNK_BODY ' + tag + '=' + tx.length); });
    }).catch(function(e){ clearTimeout(t); log.push('CHUNK_ERR ' + tag + ': ' + e); });
  });
  return '';
})())JSX";

// Automatically click the GFN "Log In" / "Sign in" control once we are on the
// real Play app (body populated), then let the following LOG snapshots watch
// the login flow (login.nvgs.nvidia.com) from inside the same session.
constexpr const char* kLoginClickProbe = R"JSX((function(){
  if (!/geforcenow\.com/.test(location.host)) return '';
  if (!document.body || !document.body.childElementCount) return '';
  try {
    if (localStorage.imwbBounceTs && Date.now() - (+localStorage.imwbBounceTs) < 90000)
      return ''; // damp: wait 90s after a login-stall bounce before auto-clicking again
  } catch(e){}
  var els = [].slice.call(document.querySelectorAll('button,a,[role="button"],[role="link"]'));
  var b = els.filter(function(e){
    var t = (e.textContent||'').replace(/\s+/g,' ');
    return /log in|sign in|logga in|sign on/i.test(t) && t.length < 40;
  })[0];
  if (b && !window.__imwbLoginClicked) {
    window.__imwbLoginClicked = true;
    window.__imwbLog.push('IMWB_CLICKED_LOGIN');
    b.click();
    return 'clicked-login:'+b.textContent.trim();
  }
  var g = els.filter(function(e){
    var t = (e.textContent||'').replace(/\s+/g,' ').trim().toLowerCase();
    return (t === 'get in' || t === 'get in now' || /^get in\b/.test(t)) && t.length < 25;
  })[0];
  if (g && !window.__imwbGetinClicked) {
    window.__imwbGetinClicked = true;
    window.__imwbLog.push('IMWB_CLICKED_GETIN');
    g.click();
    return 'clicked-getin:'+g.textContent.trim();
  }
  return '';
})())JSX";

// NVIDIA's login portal was observed shipping builds whose static JS imports
// chunk-* files that its own CDN serves as 0-byte (emptied during rolling
// deploys). The SPA then catches the module-load TypeError, logs ERROR {} and
// never mounts the sign-in form. We cannot conjure the missing code, but
// restarting the flow into the app picks up the next deploy generation; bound
// it so a persistently broken portal cannot loop forever.
constexpr const char* kStallProbe = R"JSX((function(){
  if (!/login\.nvgs\.nvidia\.com/.test(location.host)) return '';
  if (!document.body || document.body.innerText.length > 250) return '';
  var bad = (window.__imwbLog||[]).filter(function(l){ return l.indexOf('error: ERROR')===0; }).length > 0;
  if (!bad) return '';
  var r = (window.__imwbStallCount||0);
  if (r < 2) {
    window.__imwbStallCount = r + 1;
    try { localStorage.setItem('imwbBounceTs', String(Date.now())); } catch(e){}
    window.__imwbLog.push('IMWB_BOUNCE_STALL->mall shot '+(r+1));
    location.href = 'https://play.geforcenow.com/mall/';
    return 'bounce';
  }
  window.__imwbLog.push('IMWB_STALL_TWICE_STOPPED');
  return 'stopped';
})())JSX";

// Delayed snapshot: the SPA splash keeps bootstrapping after LOAD_FINISHED;
// re-ask every 30s for the captured console + final page state.
static gboolean onDelayedProbe(gpointer userData)
{
    static unsigned shots = 0;
    auto* view = static_cast<WebKitWebView*>(userData);
    webkit_web_view_evaluate_javascript(view, kLoginClickProbe, -1,
                                        nullptr, nullptr, nullptr,
                                        onProbeFinished, view);
    webkit_web_view_evaluate_javascript(view, kLogProbe, -1,
                                        nullptr, nullptr, nullptr,
                                        onProbeFinished, view);
    webkit_web_view_evaluate_javascript(view, kChunkProbe, -1,
                                        nullptr, nullptr, nullptr,
                                        onProbeFinished, view);
    webkit_web_view_evaluate_javascript(view, kStallProbe, -1,
                                        nullptr, nullptr, nullptr,
                                        onProbeFinished, view);
    return (++shots < 20) ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

// Site bootstrap failures ("splash then nothing") are usually WebGL or
// user-agent gating. With IMWB_PROBE set, inject a console/error capture into
// the page (WPE WebKit has no console-message signal) and snapshots the state
// after each finished load plus one delayed pass to catch the SPA failing late.
static void maybeEnableProbe(WebKitWebView* view, Browser*)
{
    if (g_getenv("IMWB_PROBE")) {
        auto* ucm = webkit_web_view_get_user_content_manager(view);
        auto* script = webkit_user_script_new(kCaptureScript,
            WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
            nullptr, nullptr);
        webkit_user_content_manager_add_script(ucm, script);
        webkit_user_script_unref(script);
        g_timeout_add_seconds(30, onDelayedProbe, view);
    }
}

static void maybeRunProbe(WebKitWebView* view)
{
    if (!g_getenv("IMWB_PROBE"))
        return;
    const char* probe =
        "(function(){"
        "  var c=document.createElement('canvas');"
        "  var gl1=null, gl2=null;"
        "  try{gl2=c.getContext('webgl2');}catch(e){}"
        "  try{gl1=gl2||c.getContext('webgl')||c.getContext('experimental-webgl');}catch(e){}"
        "  var v=document.querySelector('video');"
        "  var cds='n/a';"
        "  try{"
        "    var capSrc=window.RTCRtpSender&&RTCRtpSender.getCapabilities"
        "      ? RTCRtpSender : (window.RTCPeerConnection&&RTCPeerConnection.getCapabilities"
        "        ? RTCPeerConnection : null);"
        "    var caps=capSrc?capSrc.getCapabilities('video'):null;"
        "    cds=(caps&&caps.codecs)?caps.codecs.map(function(x){"
        "      return String(x.mimeType).split('/')[1]||'?'}).join(','):'none';"
        "  }catch(e){cds='err';}"
        "  return 'webgl2='+(gl2?'yes':'no')+' webgl1='+(gl1?'yes':'no')"
        "    +' rtc='+(typeof window.RTCPeerConnection)"
        "    +' wrtc='+(typeof window.webkitRTCPeerConnection)"
        "    +' video='+(v?'yes':'no')+' readyState='+document.readyState"
        "    +' codecs='+cds"
        "    +' ua='+navigator.userAgent;"
        "})()";
    webkit_web_view_evaluate_javascript(view, probe, -1, nullptr, nullptr, nullptr,
                                        onProbeFinished, view);
}

static void onLoadChanged(WebKitWebView* view, WebKitLoadEvent event, Browser* self)
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
        maybeRunProbe(view);
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

    maybeEnableProbe(view_, this);
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

#ifdef IMWB_BACKEND_VULKAN
    void* key = pendingImage_;
    displayedImage_ = pendingImage_;
    pendingImage_ = nullptr;
    // WebKit recycles exported-image pointers, so a cached entry whose fds
    // were already handed to Vulkan must be re-exported fresh.
    auto it = dmabufCache_.find(key);
    if (it == dmabufCache_.end() || it->second.consumed)
        exportDmabuf(key, wpe_fdo_egl_exported_image_get_egl_image(
                              static_cast<wpe_fdo_egl_exported_image*>(key)));
    vkCurrentKey_ = key;
    vkNew_ = true;
    frameBound_ = true;
#else
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
#endif
}

#ifdef IMWB_BACKEND_VULKAN
VkDmabufFrame* Browser::takeDmabufFrame()
{
    if (!vkNew_ || !vkCurrentKey_)
        return nullptr;
    vkNew_ = false;
    auto it = dmabufCache_.find(vkCurrentKey_);
    return it != dmabufCache_.end() ? &it->second : nullptr;
}

// Export an EGLImage as dma-buf planes via the Mesa extensions, cached per
// exported-image pointer so each buffer is exported exactly once.
bool Browser::exportDmabuf(void* key, EGLImageKHR image)
{
    using QueryFn = EGLBoolean (*)(EGLDisplay, EGLImageKHR, int*, int*, EGLuint64KHR*);
    using ExportFn = EGLBoolean (*)(EGLDisplay, EGLImageKHR, int*, EGLint*, EGLint*);
    static QueryFn queryFn = nullptr;
    static ExportFn exportFn = nullptr;
    if (!queryFn) {
        queryFn = (QueryFn)eglGetProcAddress("eglExportDMABUFImageQueryMESA");
        exportFn = (ExportFn)eglGetProcAddress("eglExportDMABUFImageMESA");
        if (!queryFn || !exportFn) {
            std::fprintf(stderr, "[vk] EGL MESA dmabuf export not available\n");
            return false;
        }
    }

    int fourcc = 0, nplanes = 0;
    EGLuint64KHR modifiers[4] = {0, 0, 0, 0};
    if (!queryFn(eglDisplay_, image, &fourcc, &nplanes, modifiers) || nplanes < 1 ||
        nplanes > 4) {
        std::fprintf(stderr, "[vk] eglExportDMABUFImageQueryMESA failed\n");
        return false;
    }

    VkDmabufFrame f;
    f.fourcc = (uint32_t)fourcc;
    f.modifier = modifiers[0];
    f.width = uint32_t(width_);
    f.height = uint32_t(height_);
    f.planeCount = uint32_t(nplanes);
    int fds[4] = {-1, -1, -1, -1};
    EGLint strides[4] = {0, 0, 0, 0}, offsets[4] = {0, 0, 0, 0};
    if (!exportFn(eglDisplay_, image, fds, strides, offsets)) {
        std::fprintf(stderr, "[vk] eglExportDMABUFImageMESA failed\n");
        return false;
    }
    for (int i = 0; i < nplanes; i++) {
        f.planes[i].fd = fds[i];
        f.planes[i].stride = uint32_t(strides[i]);
        f.planes[i].offset = uint32_t(offsets[i]);
    }
    dmabufCache_[key] = f;  // fd ownership moves to the importer on use
    // Recycled pointers reuse entries, but distinct buffers accumulate: drop
    // consumed ones once the cache grows large.
    if (dmabufCache_.size() > 32)
        for (auto it2 = dmabufCache_.begin(); it2 != dmabufCache_.end();)
            it2->second.consumed ? it2 = dmabufCache_.erase(it2) : ++it2;
    return true;
}
#endif

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
