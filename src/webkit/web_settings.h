/* ImWebBrowser - WebKitSettings builder.
 *
 * Turns the runtime Config into a WebKitSettings object. Each setting is
 * applied explicitly so the mapping from the CMake feature switches to the
 * WPE WebKit properties is easy to audit.
 */

#ifndef IMWEBBROWSER_WEBKIT_WEB_SETTINGS_H
#define IMWEBBROWSER_WEBKIT_WEB_SETTINGS_H

struct _WebKitSettings;
struct _WebKitWebContext;
typedef struct _WebKitSettings WebKitSettings;
typedef struct _WebKitWebContext WebKitWebContext;

namespace imwb {

struct Config;

/* Creates a new WebKitSettings populated from the configuration. */
WebKitSettings* web_settings_create(const Config& config);

/* Applies sandbox / network settings on the shared web context. */
void web_settings_apply_context(WebKitWebContext* context, const Config& config);

} /* namespace imwb */

#endif /* IMWEBBROWSER_WEBKIT_WEB_SETTINGS_H */
