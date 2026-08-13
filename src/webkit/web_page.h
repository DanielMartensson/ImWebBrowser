/* ImWebBrowser - one WPE WebKit web view.
 *
 * Owns the FDO view backend exportable, the WebKitWebView and the GPU frame
 * sink integration. WebKit renders into native GPU buffers which are pushed
 * to the renderer's FrameSink; no pixel data touches the CPU.
 */

#ifndef IMWEBBROWSER_WEBKIT_WEB_PAGE_H
#define IMWEBBROWSER_WEBKIT_WEB_PAGE_H

#include <cstdint>
#include <string>

#include <glib.h>

struct wpe_view_backend;
struct wpe_view_backend_exportable_fdo;
struct wpe_view_backend_exportable_fdo_dmabuf_resource;
struct wpe_fdo_shm_exported_buffer;
struct wl_resource;
struct wl_shm_buffer;
struct _WebKitWebView;
struct _WebKitPermissionRequest;
typedef struct _WebKitWebView WebKitWebView;
typedef struct _WebKitPermissionRequest WebKitPermissionRequest;

/* EGLImageKHR is opaque (void*) in every platform EGL header. Declared here
 * so this header stays independent of <EGL/egl.h>. */
typedef void* EGLImageKHR;

namespace imwb {

struct Config;
class FrameSink;

class WebPage {
public:
    WebPage() = default;
    ~WebPage() = default;

    WebPage(const WebPage&) = delete;
    WebPage& operator=(const WebPage&) = delete;

    /* Creates the exportable matching the sink's capabilities and the
     * WebKitWebView. `width_px`/`height_px` are physical pixels. */
    bool create(FrameSink& sink, uint32_t width_px, uint32_t height_px,
                const Config& config);
    void destroy();

    bool valid() const { return m_web_view != nullptr; }
    struct wpe_view_backend* view_backend() const { return m_view_backend; }

    /* Navigation. */
    void load_uri(const std::string& uri);
    void go_back();
    void go_forward();
    void reload();
    void stop();
    bool can_go_back() const;
    bool can_go_forward() const;

    /* State. */
    std::string uri() const;
    std::string title() const;
    bool is_loading() const { return m_loading; }
    double load_progress() const { return m_progress; }

    /* WebView geometry: `width_px`/`height_px` are physical pixels,
     * `scale` is the device pixel ratio for CSS layout. */
    void resize(uint32_t width_px, uint32_t height_px, float scale);
    void set_activity(bool visible, bool focused, bool in_window);

private:
    /* WPEBackend-FDO exportable callbacks. */
    static void on_export_egl_image(void* data, EGLImageKHR image);
    static void on_export_dmabuf_resource(void* data,
                                          struct wpe_view_backend_exportable_fdo_dmabuf_resource* resource);
    static void on_export_buffer_resource(void* data, struct wl_resource* buffer_resource);
    static void on_export_shm_buffer(void* data, struct wpe_fdo_shm_exported_buffer* buffer);

    /* WebKitWebView signals.
     * g_signal_connect passes plain gpointer/int values, so the handlers use
     * pointer-typed parameters and reinterpret them to the real types. */
    static void on_load_changed(WebKitWebView* view, void* load_event, void* data);
    static void on_load_progress(WebKitWebView* view, void* pspec, void* data);
    static int on_permission_request(WebKitWebView* view, void* request, void* data);
    static void on_web_process_terminated(WebKitWebView* view, void* reason, void* data);
    static void on_web_view_created(WebKitWebView* view, void* data);

    struct wpe_view_backend_exportable_fdo* m_exportable = nullptr;
    struct wpe_view_backend* m_view_backend = nullptr;
    WebKitWebView* m_web_view = nullptr;
    FrameSink* m_sink = nullptr;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    float m_scale = 1.0f;

    bool m_loading = false;
    double m_progress = 0.0;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_WEBKIT_WEB_PAGE_H */
