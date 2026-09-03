#!/usr/bin/env bash
#
# Run ImWebBrowser — on the development PC AND on the target
# (OpenSTLinux / STM32MP257F).
#
# Dev PC: the whole bundled stack (WPE WebKit, libwpe, wpebackend-fdo,
# GStreamer 1.26, SDL3) is built by setup.sh into the app-local prefix
# deps/install — nothing is installed into the system. That prefix is not on
# the linker path, and wpebackend-fdo is dlopen()ed by libwpe at runtime while
# the WebKit helper processes (WPEWebProcess, WPENetworkProcess, ...) run as
# separate executables, so the binary's own RPATH is not enough: the env below
# makes all of them resolve the bundled prefix.
#
# Target: the Yocto recipe installs the browser and its dependencies into the
# system (/usr), so there is no deps/ prefix — everything is already on the
# default paths and the env overrides simply don't apply.
#
# Either way the browser renders through the FDO backend's EGL export path
# (wpe_fdo_initialize_for_egl_display), drawing the frames into its own SDL3
# window — NO Wayland compositor is needed: it is a plain SDL app.
#
# Usage:
#   ./run.sh [browser args...]   e.g. ./run.sh --kiosk https://example.com
#
# Environment overrides:
#   IMWB_PREFIX=...        bundled prefix (default: deps/install)
#   IMWB_BUILD_DIR=...     build directory (default: build)
#
set -euo pipefail
cd "$(dirname "$0")"

: "${IMWB_PREFIX:=$PWD/deps/install}"
: "${IMWB_BUILD_DIR:=build}"

# Resolve the binary: a dev build tree first, then a system install (target).
BINARY=""
if [ -x "$IMWB_BUILD_DIR/imwebbrowser" ]; then
    BINARY="$IMWB_BUILD_DIR/imwebbrowser"
elif command -v imwebbrowser >/dev/null 2>&1; then
    BINARY="$(command -v imwebbrowser)"
else
    echo "ERROR: imwebbrowser not found." >&2
    echo "       Dev PC: run ./setup.sh (or cmake -B $IMWB_BUILD_DIR && cmake --build $IMWB_BUILD_DIR)." >&2
    echo "       Target: it should be installed by the Yocto recipe." >&2
    exit 1
fi

if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
    export XDG_RUNTIME_DIR="/run/user/$(id -u)"
fi

# Bundled prefix present (dev PC): make the private libs + GStreamer 1.26
# discoverable for the browser AND its WebKit helper processes. Stock Ubuntu
# 24.04 ships GStreamer 1.24, whose webrtcbin reads a=setup only on media
# level and rejects GFN's session-level setup/missing mid -> setRemoteDescription
# collapses with 0xC0F2220E. BubblewrapLauncher binds LD_LIBRARY_PATH/
# GST_PLUGIN_PATH and --setenv's them into the web process, so the prefix
# reaches WPEWebProcess as well. On the target (system install) this block is
# skipped entirely.
if [ -d "$IMWB_PREFIX/lib" ]; then
    export LD_LIBRARY_PATH="$IMWB_PREFIX/lib:$IMWB_PREFIX/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export GST_PLUGIN_PATH="$IMWB_PREFIX/lib/x86_64-linux-gnu/gstreamer-1.0"
    export GST_PLUGIN_SYSTEM_PATH=""
    export GST_PLUGIN_SCANNER="$IMWB_PREFIX/libexec/gstreamer-1.0/gst-plugin-scanner"
    export GST_REGISTRY="$IMWB_PREFIX/registry.bin"
fi

# Share the host network with the WebKit web process.
#
# WebKit 2.52.6's bubblewrap sandbox (ENABLE_BUBBLEWRAP_SANDBOX=ON) launches
# WPEWebProcess with --unshare-net, dropping it into a netns where only loopback
# exists. libnice (WebRTC/ICE) then sees no real interface, produces no host
# candidates, and real-time peers (GeForce Now) abort with 0xC0F2220E
# (patches/wpe-webkit-bwrap-unshare-net-webrtc.patch).
#
# Two ways to make BubblewrapLauncher::shouldUnshareNetwork() skip --unshare-net:
#   1. The patch adds a dedicated WPE branch honouring WEBKIT_ENABLE_NETWORK_ACCESS.
#   2. remoteInspectorEnabled() also returns false for --unshare-net whenever
#      WEBKIT_INSPECTOR_SERVER is set — robust fallback even without the patch.
#      With ENABLE_DEVELOPER_EXTRAS=OFF no inspector port is bound, so
#      loopback:0 is a harmless placeholder.
export WEBKIT_ENABLE_NETWORK_ACCESS=1
if [ -z "${WEBKIT_INSPECTOR_SERVER:-}" ]; then
    export WEBKIT_INSPECTOR_SERVER="127.0.0.1:0"
fi

# Optional preferred GStreamer decoder (empty = GStreamer default rank order).
# Exported here so WebKit's helper processes see it too, not just the binary.
# e.g. IMWB_VIDEO_DECODER=v4l2slh264dec on the STM32MP257F.
if [ -n "${IMWB_VIDEO_DECODER:-}" ]; then
    export GST_PLUGIN_FEATURE_RANK="${IMWB_VIDEO_DECODER}:MAX"
fi

exec "$BINARY" "$@"
