/* ImWebBrowser - one WPE WebKit web view. */

#include "webkit/web_page.h"

#include <glib.h>
#include <wpe/wpe.h>
#include <wpe/fdo.h>
#include <wpe/fdo-egl.h>
#include <wpe/webkit.h>
#include <wayland-server-core.h>

#include <cstring>
#include <vector>

#include "config/config.h"
#include "logging/log.h"
#include "rendering/webview_frame.h"
#include "webkit/web_settings.h"

namespace imwb {

namespace {

/* A WebPage instance passed as `data` to the C callbacks. */
WebPage* self(void* data)
{
    return static_cast<WebPage*>(data);
}

} /* namespace */

bool WebPage::create(FrameSink& sink, uint32_t width_px, uint32_t height_px,
                     const Config& config)
{
    m_sink = &sink;
    m_width = width_px;
    m_height = height_px;

    if (sink.wants_egl_image()) {
        static const struct wpe_view_backend_exportable_fdo_egl_client client = {
            on_export_egl_image,
            nullptr, /* export_fdo_egl_image */
            on_export_shm_buffer,
            nullptr, /* _wpe_reserved0 */
            nullptr, /* _wpe_reserved1 */
        };
        m_exportable = wpe_view_backend_exportable_fdo_egl_create(&client, this,
                                                                  width_px, height_px);
        LOG_INFO("WPE: using EGL image exportable (%ux%u)", width_px, height_px);
    } else if (sink.wants_dmabuf()) {
        static const struct wpe_view_backend_exportable_fdo_client client = {
            on_export_buffer_resource,
            on_export_dmabuf_resource,
            on_export_shm_buffer,
            nullptr, /* _wpe_reserved0 */
            nullptr, /* _wpe_reserved1 */
        };
        m_exportable = wpe_view_backend_exportable_fdo_create(&client, this,
                                                              width_px, height_px);
        LOG_INFO("WPE: using dmabuf exportable (%ux%u)", width_px, height_px);
    } else {
        LOG_ERROR("WPE: sink supports neither EGL image nor dmabuf frames");
        return false;
    }

    if (!m_exportable) {
        LOG_ERROR("WPE: failed to create view backend exportable");
        return false;
    }

    m_view_backend = wpe_view_backend_exportable_fdo_get_view_backend(m_exportable);

    WebKitWebViewBackend* wb = webkit_web_view_backend_new(m_view_backend, nullptr, nullptr);
    WebKitSettings* settings = web_settings_create(config);
    m_web_view = webkit_web_view_new(wb);
    if (!m_web_view) {
        LOG_ERROR("WPE: failed to create WebKitWebView");
        g_object_unref(settings);
        return false;
    }
    webkit_web_view_set_settings(m_web_view, settings);
    g_object_unref(settings);
    /* Note: WebKitWebViewBackend is a boxed type, not a GObject; the web
     * view takes its own reference, so `wb` must not be unreffed here. */

    g_signal_connect(m_web_view, "load-changed", G_CALLBACK(on_load_changed), this);
    g_signal_connect(m_web_view, "notify::estimated-load-progress",
                     G_CALLBACK(on_load_progress), this);
    g_signal_connect(m_web_view, "permission-request", G_CALLBACK(on_permission_request), this);
    g_signal_connect(m_web_view, "web-process-terminated",
                     G_CALLBACK(on_web_process_terminated), this);
    g_signal_connect(m_web_view, "create", G_CALLBACK(on_web_view_created), this);

    /* Inject a user script to auto-dismiss cookie consent dialogs
     * (YouTube, Google, etc.) by clicking "Reject all" or "Accept all". */
    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(m_web_view);
    static const char* consent_js =
        "setTimeout(function() {\n"
        "  var btns = document.querySelectorAll('button, [role=button]');\n"
        "  for (var i = 0; i < btns.length; i++) {\n"
        "    var t = btns[i].textContent.toLowerCase().trim();\n"
        "    if (t === 'reject all' || t === 'accept all' || t === 'acceptera alla' ||\n"
        "        t === 'avvisa alla' || t === 'i agree' || t === 'agree') {\n"
        "      btns[i].click(); break;\n"
        "    }\n"
        "  }\n"
        "}, 1500);\n";
    WebKitUserScript* script = webkit_user_script_new(
        consent_js,
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
        nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, script);
    webkit_user_script_unref(script);

    set_activity(true, true, true);
    return true;
}

void WebPage::destroy()
{
    if (m_web_view) {
        g_object_unref(m_web_view);
        m_web_view = nullptr;
    }
    /* Destroy the exportable last: it owns the view backend. */
    if (m_exportable) {
        wpe_view_backend_exportable_fdo_destroy(m_exportable);
        m_exportable = nullptr;
    }
    m_view_backend = nullptr;
}

void WebPage::load_uri(const std::string& uri)
{
    if (m_web_view)
        webkit_web_view_load_uri(m_web_view, uri.c_str());
}

void WebPage::go_back()
{
    if (m_web_view)
        webkit_web_view_go_back(m_web_view);
}

void WebPage::go_forward()
{
    if (m_web_view)
        webkit_web_view_go_forward(m_web_view);
}

void WebPage::reload()
{
    if (m_web_view)
        webkit_web_view_reload(m_web_view);
}

void WebPage::stop()
{
    if (m_web_view)
        webkit_web_view_stop_loading(m_web_view);
}

bool WebPage::can_go_back() const
{
    return m_web_view && webkit_web_view_can_go_back(m_web_view);
}

bool WebPage::can_go_forward() const
{
    return m_web_view && webkit_web_view_can_go_forward(m_web_view);
}

std::string WebPage::uri() const
{
    if (!m_web_view)
        return {};
    const char* uri = webkit_web_view_get_uri(m_web_view);
    return uri ? uri : "";
}

std::string WebPage::title() const
{
    if (!m_web_view)
        return {};
    const char* title = webkit_web_view_get_title(m_web_view);
    return title ? title : "";
}

void WebPage::resize(uint32_t width_px, uint32_t height_px, float scale)
{
    if (!m_view_backend)
        return;
    if (width_px == m_width && height_px == m_height && scale == m_scale)
        return;

    m_width = width_px;
    m_height = height_px;
    m_scale = scale;

    wpe_view_backend_dispatch_set_size(m_view_backend, m_width, m_height);
    wpe_view_backend_dispatch_set_device_scale_factor(m_view_backend, scale);
}

void WebPage::set_activity(bool visible, bool focused, bool in_window)
{
    if (!m_view_backend)
        return;

    uint32_t add = 0, remove = 0;
    if (visible) add |= wpe_view_activity_state_visible; else remove |= wpe_view_activity_state_visible;
    if (focused) add |= wpe_view_activity_state_focused; else remove |= wpe_view_activity_state_focused;
    if (in_window) add |= wpe_view_activity_state_in_window; else remove |= wpe_view_activity_state_in_window;

    wpe_view_backend_add_activity_state(m_view_backend, add);
    wpe_view_backend_remove_activity_state(m_view_backend, remove);
}

/* -------------------------------------------------------------------------
 * WPEBackend-FDO exportable callbacks (invoked from the GLib main loop).
 * ---------------------------------------------------------------------- */

void WebPage::on_export_egl_image(void* data, EGLImageKHR image)
{
    WebPage* page = self(data);
    if (!page->m_sink || !image)
        return;

    LOG_DEBUG("WPE: egl image frame");
    EglFrame frame{ image, page->m_width, page->m_height };
    page->m_sink->on_egl_image(frame);

    /* The GLES renderer copies synchronously during on_egl_image, so the
     * image can be handed back to WPE immediately. */
    wpe_view_backend_exportable_fdo_egl_dispatch_release_image(page->m_exportable, image);
    /* Signal that the frame has been presented so the compositor can
     * release the wl_surface.frame callback and render the next frame. */
    wpe_view_backend_exportable_fdo_dispatch_frame_complete(page->m_exportable);
}

void WebPage::on_export_dmabuf_resource(void* data,
                                        struct wpe_view_backend_exportable_fdo_dmabuf_resource* resource)
{
    WebPage* page = self(data);
    if (!page->m_sink || !resource || !resource->buffer_resource)
        return;

    DmaBufFrame frame{};
    frame.width = resource->width;
    frame.height = resource->height;
    frame.format = resource->format;
    frame.n_planes = resource->n_planes;
    for (uint8_t i = 0; i < resource->n_planes; ++i) {
        frame.fds[i] = resource->fds[i];
        frame.strides[i] = resource->strides[i];
        frame.offsets[i] = resource->offsets[i];
        frame.modifiers[i] = resource->modifiers[i];
    }

    page->m_sink->on_dmabuf(frame);

    /* The Vulkan renderer imports the dmabuf (the driver dups the fd), so
     * the Wayland buffer can be released to WPE immediately. */
    wpe_view_backend_exportable_fdo_dispatch_release_buffer(page->m_exportable,
                                                            resource->buffer_resource);
    wpe_view_backend_exportable_fdo_dispatch_frame_complete(page->m_exportable);
}

void WebPage::on_export_buffer_resource(void* data, struct wl_resource* buffer_resource)
{
    (void)data;
    (void)buffer_resource;
    LOG_WARN("WPE: got an export_buffer_resource frame; not supported");
}

void WebPage::on_export_shm_buffer(void* data, struct wpe_fdo_shm_exported_buffer* buffer)
{
    WebPage* page = self(data);
    if (!page->m_sink || !buffer)
        return;

    struct wl_shm_buffer* shm = wpe_fdo_shm_exported_buffer_get_shm_buffer(buffer);
    if (!shm) {
        wpe_view_backend_exportable_fdo_egl_dispatch_release_shm_exported_buffer(
            page->m_exportable, buffer);
        return;
    }

    const int32_t width = wl_shm_buffer_get_width(shm);
    const int32_t height = wl_shm_buffer_get_height(shm);
    const int32_t stride = wl_shm_buffer_get_stride(shm);
    const uint32_t format = static_cast<uint32_t>(wl_shm_buffer_get_format(shm));
    const uint8_t* pixels = static_cast<const uint8_t*>(wl_shm_buffer_get_data(shm));

    if (width <= 0 || height <= 0 || stride < 0 || !pixels) {
        wpe_view_backend_exportable_fdo_egl_dispatch_release_shm_exported_buffer(
            page->m_exportable, buffer);
        return;
    }

    /* The pool may be reused once the buffer is released, so copy first. */
    const size_t bytes = static_cast<size_t>(height) * static_cast<size_t>(stride);
    std::vector<uint8_t> copy(bytes);
    std::memcpy(copy.data(), pixels, bytes);

    const ShmFrame frame{ copy.data(), static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height), static_cast<uint32_t>(stride),
                          format };
    page->m_sink->on_shm(frame);
    static unsigned shm_frame_count = 0;
    LOG_DEBUG("WPE: shm frame #%u %dx%d stride %d format 0x%08x", ++shm_frame_count,
              width, height, stride, format);
    wpe_view_backend_exportable_fdo_egl_dispatch_release_shm_exported_buffer(
        page->m_exportable, buffer);
    wpe_view_backend_exportable_fdo_dispatch_frame_complete(page->m_exportable);
}

/* -------------------------------------------------------------------------
 * WebKitWebView signals.
 * ---------------------------------------------------------------------- */
void WebPage::on_load_changed(WebKitWebView* view, void* load_event, void* data)
{
    (void)view;
    WebPage* page = self(data);
    const auto event = static_cast<WebKitLoadEvent>(reinterpret_cast<intptr_t>(load_event));

    switch (event) {
    case WEBKIT_LOAD_STARTED:
    case WEBKIT_LOAD_REDIRECTED:
        page->m_loading = true;
        page->m_progress = 0.0;
        break;
    case WEBKIT_LOAD_COMMITTED:
        break;
    case WEBKIT_LOAD_FINISHED:
        page->m_loading = false;
        page->m_progress = 1.0;
        LOG_INFO("WPE: load finished: %s", page->uri().c_str());
        break;
    }
}

void WebPage::on_load_progress(WebKitWebView* view, void* pspec, void* data)
{
    (void)view;
    (void)pspec;
    WebPage* page = self(data);
    page->m_progress = webkit_web_view_get_estimated_load_progress(page->m_web_view);
}

int WebPage::on_permission_request(WebKitWebView* view, void* request, void* data)
{
    (void)view;
    (void)data;

    /* Camera/microphone: grant when media capture is requested. */
    if (WEBKIT_IS_PERMISSION_REQUEST(request)) {
        LOG_DEBUG("WPE: granting permission request");
        webkit_permission_request_allow(WEBKIT_PERMISSION_REQUEST(request));
        return TRUE;
    }
    return FALSE;
}

void WebPage::on_web_process_terminated(WebKitWebView* view, void* reason, void* data)
{
    (void)view;
    WebPage* page = self(data);
    const auto termination = static_cast<WebKitWebProcessTerminationReason>(
        reinterpret_cast<intptr_t>(reason));

    LOG_ERROR("WPE: web process terminated (reason %d); reloading", static_cast<int>(termination));
    if (page->m_web_view)
        webkit_web_view_reload(page->m_web_view);
}

void WebPage::on_web_view_created(WebKitWebView* view, void* data)
{
    (void)view;
    (void)data;
    /* No new windows: reject the request by not returning a WebKitWebView. */
    LOG_WARN("WPE: window.open() requested; ignored");
}

} /* namespace imwb */
