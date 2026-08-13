/* ImWebBrowser - web view viewport geometry (logical coordinates).
 *
 * The UI reports where the web view sits inside the window every frame;
 * the browser forwards it to the web page (physical pixels) and to the
 * input translator (for coordinate mapping).
 */

#ifndef IMWEBBROWSER_BROWSER_WEB_VIEW_VIEWPORT_H
#define IMWEBBROWSER_BROWSER_WEB_VIEW_VIEWPORT_H

namespace imwb {

struct WebViewViewport {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float scale = 1.0f;
};

} /* namespace imwb */

#endif /* IMWEBBROWSER_BROWSER_WEB_VIEW_VIEWPORT_H */
