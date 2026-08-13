/* ImWebBrowser - EGL helpers. */

#include "rendering/egl_helpers.h"

#include "logging/log.h"

namespace imwb {

namespace {

/* EGL_KHR_image target value (not always exposed by the system headers). */
#ifndef EGL_IMAGE_KHR
#define EGL_IMAGE_KHR 0x305C
#endif

PFNEGLCREATEIMAGEKHRPROC g_create_image = nullptr;
PFNEGLDESTROYIMAGEKHRPROC g_destroy_image = nullptr;
PFNEGLGETPLATFORMDISPLAYEXTPROC g_get_platform_display_ext = nullptr;

} /* namespace */

bool egl_helpers_init()
{
    g_create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    g_destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    g_get_platform_display_ext =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (!g_create_image || !g_destroy_image) {
        LOG_ERROR("EGL: KHR_image extension not available (eglCreateImageKHR/eglDestroyImageKHR)");
        return false;
    }
    return true;
}

EGLImageKHR egl_helpers_create_image(EGLDisplay display, EGLContext context,
                                     EGLClientBuffer buffer, EGLint* attribs)
{
    if (!g_create_image)
        return EGL_NO_IMAGE_KHR;
    return g_create_image(display, context, EGL_IMAGE_KHR, buffer, attribs);
}

void egl_helpers_destroy_image(EGLDisplay display, EGLImageKHR image)
{
    if (g_destroy_image && image != EGL_NO_IMAGE_KHR)
        g_destroy_image(display, image);
}

EGLDisplay egl_helpers_create_headless_display()
{
    EGLDisplay display = EGL_NO_DISPLAY;

    if (g_get_platform_display_ext) {
        display = g_get_platform_display_ext(EGL_PLATFORM_SURFACELESS_MESA,
                                             EGL_DEFAULT_DISPLAY, nullptr);
    }

    if (display == EGL_NO_DISPLAY)
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        LOG_ERROR("EGL: no platform display could be created");
        return EGL_NO_DISPLAY;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        LOG_ERROR("EGL: eglInitialize failed: 0x%x", (unsigned)eglGetError());
        return EGL_NO_DISPLAY;
    }

    LOG_INFO("EGL: headless display %p, version %d.%d", (void*)display, major, minor);
    return display;
}

} /* namespace imwb */
