/* ImWebBrowser - WPE WebKit integration. */

#include "webkit/wpe_module.h"

#include <glib.h>

#include <wpe/wpe.h>
#include <wpe/fdo-egl.h>

#include "logging/log.h"

namespace imwb {

bool WpeModule::initialize(void* egl_display)
{
    if (m_initialized)
        return true;

    if (!egl_display) {
        LOG_ERROR("WPE: no EGL display provided");
        return false;
    }

    /* On Intel GPUs, force the native DRI driver (i965) instead of Zink
     * (OpenGL-over-Vulkan).  Only effective on x86; harmless on ARM. */
#if defined(__x86_64__) || defined(__i386__)
    if (!g_getenv("MESA_LOADER_DRIVER_OVERRIDE"))
        g_setenv("MESA_LOADER_DRIVER_OVERRIDE", "i965", TRUE);
#endif

    /* Disable the WebProcess sandbox so it can access /dev/dri directly.
     * Without this, Mesa falls back to Vulkan/Zink inside the sandbox,
     * causing performance warnings on Haswell GPUs. */
    if (!g_getenv("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS"))
        g_setenv("WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS", "1", TRUE);

    /* Point libwpe at the FDO backend unless the caller already selected one.
     * (Distros sometimes omit the libWPEBackend-default.so alias.) */
    if (!g_getenv("WPE_BACKEND_LIBRARY"))
        g_setenv("WPE_BACKEND_LIBRARY", "libWPEBackend-fdo-1.0.so", TRUE);

    if (!wpe_fdo_initialize_for_egl_display(egl_display)) {
        LOG_ERROR("WPE: wpe_fdo_initialize_for_egl_display failed");
        return false;
    }

    m_initialized = true;
    LOG_INFO("WPE: initialized on EGL display %p", egl_display);
    return true;
}

void WpeModule::shutdown()
{
    m_initialized = false;
}

void WpeModule::pump_main_context()
{
    /* Non-blocking; WPE's sources run on the default context. */
    while (g_main_context_iteration(nullptr, false))
        ;
}

} /* namespace imwb */
