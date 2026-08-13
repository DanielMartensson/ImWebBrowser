/* ImWebBrowser - EGL helpers.
 *
 * Resolves the EGL extension entry points that WPE's exportable backends
 * need (eglCreateImageKHR / eglDestroyImageKHR / eglGetPlatformDisplayEXT)
 * and creates the EGL display the Vulkan backend renders WPE into.
 */

#ifndef IMWEBBROWSER_RENDERING_EGL_HELPERS_H
#define IMWEBBROWSER_RENDERING_EGL_HELPERS_H

#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace imwb {

bool egl_helpers_init();

/* Creates/destroys an EGL image (KHR_image). */
EGLImageKHR egl_helpers_create_image(EGLDisplay display, EGLContext context,
                                     EGLClientBuffer buffer, EGLint* attribs);
void egl_helpers_destroy_image(EGLDisplay display, EGLImageKHR image);

/* Creates a headless (surfaceless) EGL display for off-screen WPE rendering.
 * Returns EGL_NO_DISPLAY on failure. Caller owns the display. */
EGLDisplay egl_helpers_create_headless_display();

} /* namespace imwb */

#endif /* IMWEBBROWSER_RENDERING_EGL_HELPERS_H */
