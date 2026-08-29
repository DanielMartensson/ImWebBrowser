#!/usr/bin/env bash
#
# Run ImWebBrowser natively on the development PC, inside a Wayland session.
#
# WPEBackend-FDO renders through a Wayland compositor. On an X11 desktop the
# script transparently starts a nested Weston (x11-backend) window that closes
# again when the browser exits. If you are already inside a Wayland session
# (e.g. the Weston desktop from the Yocto image) it just runs the browser there.
#
# Usage:
#   ./run-native.sh                          # build (if needed) and run
#   ./run-native.sh --rebuild                # force a fresh GLES build
#   ./run-native.sh https://example.com      # pass a URL (or --kiosk etc.)
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="build-native"
REBUILD=0

for arg in "$@"; do
    case "$arg" in
        --rebuild) REBUILD=1; shift ;;
        --help|-h) echo "See the header of this script for usage."; exit 0 ;;
    esac
done

# ---------------------------------------------------------------------------
# 1. Build the natively-running GLES binary if it is missing or requested.
# ---------------------------------------------------------------------------
if [ "$REBUILD" = 1 ] || [ ! -x "$BUILD_DIR/imwebbrowser" ]; then
    echo "==> Building (GLES backend) into $BUILD_DIR ..."
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DIMWB_BACKEND_OPENGL_ES=ON
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

# ---------------------------------------------------------------------------
# 2. Find a usable Wayland display, or start a nested Weston.
# ---------------------------------------------------------------------------
RUNTIME="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export XDG_RUNTIME_DIR="$RUNTIME"

has_wayland() {
    [ -n "${WAYLAND_DISPLAY:-}" ] && [ -S "$RUNTIME/$WAYLAND_DISPLAY" ]
}

WESTON_PID=""
WESTON_SOCKET=""

if ! has_wayland; then
    if ! command -v weston >/dev/null 2>&1; then
        echo "ERROR: No running Wayland session and 'weston' is not installed." >&2
        echo "       Install weston (apt install weston) or run inside an existing" >&2
        echo "       Wayland/Weston session." >&2
        exit 1
    fi
    if [ -z "${DISPLAY:-}" ]; then
        echo "ERROR: No Wayland session and no X11 display to nest Weston in." >&2
        exit 1
    fi

    # Pick a free socket name and clean up stale sockets from crashed runs.
    WESTON_SOCKET="imwb"
    while [ -S "$RUNTIME/$WESTON_SOCKET" ]; do WESTON_SOCKET="imwb-$$"; done
    rm -f "$RUNTIME/$WESTON_SOCKET"

    echo "==> Starting nested Weston (x11-backend, window '$WESTON_SOCKET') ..."
    weston --backend=x11-backend.so --socket="$WESTON_SOCKET" \
        --width=1280 --height=800 --idle-time=0 \
        >"$BUILD_DIR/weston.log" 2>&1 &
    WESTON_PID=$!

    # Wait for the Wayland socket to appear (max 10 s).
    for _ in $(seq 1 50); do
        [ -S "$RUNTIME/$WESTON_SOCKET" ] && break
        sleep 0.2
    done
    if [ ! -S "$RUNTIME/$WESTON_SOCKET" ]; then
        echo "ERROR: Weston did not come up. See $BUILD_DIR/weston.log" >&2
        kill "$WESTON_PID" 2>/dev/null || true
        exit 1
    fi

    export WAYLAND_DISPLAY="$WESTON_SOCKET"
fi

cleanup() {
    if [ -n "$WESTON_PID" ]; then
        echo "==> Shutting down nested Weston."
        kill "$WESTON_PID" 2>/dev/null || true
        rm -f "$RUNTIME/$WESTON_SOCKET"
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# 3. Run the browser.
# ---------------------------------------------------------------------------
echo "==> Launching ImWebBrowser (WAYLAND_DISPLAY=$WAYLAND_DISPLAY) ..."
WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
SDL_VIDEODRIVER=wayland \
EGL_PLATFORM=wayland \
"$BUILD_DIR/imwebbrowser" "$@"