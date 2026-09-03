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
#   ./run-native.sh --rebuild                # force a fresh GLES rebuild
#   ./run-native.sh https://example.com      # pass a URL (or --kiosk etc.)
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="build"
REBUILD=0

# Allow pointing at an alternative build directory without editing the
# script: IMWB_BUILD_DIR=build-dev
: "${IMWB_BUILD_DIR:=$BUILD_DIR}"
BUILD_DIR="$IMWB_BUILD_DIR"

# Filter out this script's own flags; keep everything else to pass to the browser.
APP_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --rebuild) REBUILD=1 ;;
        --help|-h) echo "See the header of this script for usage."; exit 0 ;;
        *) APP_ARGS+=("$arg") ;;
    esac
done

# ---------------------------------------------------------------------------
# 1. Build the natively-running GLES binary. Always configures at least once
#    and lets cmake rebuild incrementally, so editing the sources is enough —
#    ./run-native.sh always compiles the latest code. --rebuild forces a
#    fresh configure as well.
# ---------------------------------------------------------------------------
if [ "$REBUILD" = 1 ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "==> Configuring (GLES backend) into $BUILD_DIR ..."
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DIMWB_BACKEND_OPENGL_ES=ON
fi
echo "==> Building into $BUILD_DIR ..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

# ---------------------------------------------------------------------------
# 1b. GStreamer tuning for GeForce Now / streamed video (STM32MP25x based).
# ---------------------------------------------------------------------------
# ImWebBrowser decodes both WebRTC (GeForce Now) and <video> content through
# GStreamer decodebin, and the actual decode element is picked by GStreamer
# rank. Boosting the stateless V4L2 H.264 decoder to MAX makes the on-board
# video decoder win over avdec_h264 (pure software), which is what gives
# smooth 1080p60 with the target hardware (mirrors the known-good fallback):
#   gst-launch-1.0 filesrc ... ! v4l2slh264dec ! waylandsink
#
# Controls (all optional):
#   IMWB_HW_DECODE=0      keep default ranks (allow software decode)
#   IMWB_GST_DECODER=     name to boost instead of v4l2slh264dec
#   IMWB_GST_DEBUG=1      enable WebKit/GStreamer decode debug traces
if [ "${IMWB_HW_DECODE:-1}" != "0" ]; then
    HW_DECODER="${IMWB_GST_DECODER:-v4l2slh264dec}"
    if command -v gst-inspect-1.0 >/dev/null 2>&1 && gst-inspect-1.0 "$HW_DECODER" >/dev/null 2>&1; then
        if [ -n "${GST_PLUGIN_FEATURE_RANK:-}" ]; then
            GST_PLUGIN_FEATURE_RANK="$GST_PLUGIN_FEATURE_RANK,$HW_DECODER:MAX"
        else
            GST_PLUGIN_FEATURE_RANK="$HW_DECODER:MAX"
        fi
        export GST_PLUGIN_FEATURE_RANK
        echo "==> GStreamer: boosted '$HW_DECODER' to MAX rank (hardware H.264 decode preferred)"
    fi
fi
if [ "${IMWB_GST_DEBUG:-0}" = "1" ]; then
    export GST_DEBUG="${GST_DEBUG:+$GST_DEBUG,}webkit:5,webkitquirks:5,webkitlibwebrtcvideodecoder:5,decodebin:5"
    echo "==> GStreamer debug traces enabled"
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

    # The canvas is bigger than the 1280x800 browser window so there is room
    # to auto-centre the window on the output below. Weston's move binding is
    # switched to Left-Ctrl (default is Super, which the host X11 window
    # manager hijacks for moving the nested compositor window) and the debug
    # extension is enabled so the centring code can read back the exact
    # surface position via `weston-debug scene-graph`.
    if [ -n "${DISPLAY:-}" ] \
        && command -v weston-debug >/dev/null 2>&1 \
        && command -v xdotool >/dev/null 2>&1
    then
        printf '[shell]\nbinding-modifier=ctrl\n' >"$BUILD_DIR/weston.ini"
        CANVAS_WIDTH=1600
        CANVAS_HEIGHT=900
        WESTON_CONFIG_ARGS=(--config "$PWD/$BUILD_DIR/weston.ini" --debug)
    else
        CANVAS_WIDTH=1280
        CANVAS_HEIGHT=800
        WESTON_CONFIG_ARGS=()
    fi

    echo "==> Starting nested Weston (x11-backend, window '$WESTON_SOCKET') ..."
    weston --backend=x11-backend.so --socket="$WESTON_SOCKET" \
        --width="$CANVAS_WIDTH" --height="$CANVAS_HEIGHT" --idle-time=0 \
        "${WESTON_CONFIG_ARGS[@]}" \
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

    # Position the nested Weston window on the X11 screen: horizontally
    # centered, vertically BELOW the middle of the usable desktop so its
    # window frame is easy to reach with the mouse. Weston itself takes no
    # initial window position, so ask the X11 window manager to move it once
    # frame decorations exist (_NET_FRAME_EXTENTS). The WM's geometry math is
    # not exact, so iterate until the measured position matches the target.
    # (Harmless no-op if wmctrl/xdpyinfo/xprop are not installed.)
    if command -v wmctrl >/dev/null 2>&1 \
        && command -v xdpyinfo >/dev/null 2>&1 \
        && command -v xprop >/dev/null 2>&1
    then
        local_winid=""
        for _ in $(seq 1 50); do
            local_winid=$(wmctrl -l 2>/dev/null | awk '/Weston Compositor/{print $1; exit}')
            [ -n "$local_winid" ] && break
            sleep 0.2
        done
        if [ -n "$local_winid" ]; then
            for _ in $(seq 1 50); do
                xprop -id "$local_winid" _NET_FRAME_EXTENTS 2>/dev/null | grep -q 'CARDINAL' && break
                sleep 0.2
            done
            read -r sw sh < <(xdpyinfo -display "$DISPLAY" 2>/dev/null \
                | awk '/dimensions/{split($2,d,"x"); print d[1], d[2]; exit}') || true
            [ "$sw" = "" ] && sw=1280; [ "$sh" = "" ] && sh=1000
            # Usable desktop height = screen height minus an estimated panel
            # height. Deliberately locale-independent and approximate: reading
            # the real panel geometry would require matching a translateable
            # window title, and the ~40px difference does not matter here.
            dh=$((sh - 40))
            read -r cw ch < <(wmctrl -lG 2>/dev/null \
                | awk -v id="$local_winid" '$1==id{print $5, $6}') || true
            [ -n "$ch" ] || ch="$CANVAS_HEIGHT"
            tx=$(( (sw - cw) / 2 ))
            [ "$tx" -lt 0 ] && tx=0
            ty=$(( dh * 58 / 100 - ch / 2 ))   # window top sits below screen middle
            if [ "$ty" -lt "$(( dh / 10 ))" ]; then ty=$(( (dh - ch) / 2 )); fi
            if [ "$((ty + ch))" -gt "$dh" ]; then ty=$(( dh - ch )); fi
            [ "$ty" -lt 0 ] && ty=0
            for _ in $(seq 1 8); do
                read -r rx ry < <(xwininfo -id "$local_winid" -display "$DISPLAY" 2>/dev/null \
                    | awk '/Absolute upper-left X/{x=$4} /Absolute upper-left Y/{y=$4} END{print x, y}') || true
                dx=$(( tx - ${rx:-0} ))
                dy=$(( ty - ${ry:-0} ))
                [ "$dx" -ge -1 ] && [ "$dx" -le 1 ] && [ "$dy" -ge -1 ] && [ "$dy" -le 1 ] && break
                wmctrl -ir "$local_winid" -e "0,$(( ${rx:-0} + dx )),$(( ${ry:-0} + dy )),-1,-1" 2>/dev/null || true
                sleep 0.3
            done
            echo "==> Nested Weston window placed (target ${tx}px,${ty}px)"
        fi
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
# Share the host network with the WebKit web process. WebKit 2.52.6's
# bubblewrap sandbox launches WPEWebProcess with --unshare-net so the web
# process gets its own netns with only loopback -> libnice/WebRTC sees no real
# interface -> no host ICE candidates -> real-time peers (GeForce Now) abort
# with 0xC0F2220E (patches/wpe-webkit-bwrap-unshare-net-webrtc.patch).
# WEBKIT_ENABLE_NETWORK_ACCESS is the clean switch added by the patch; setting
# WEBKIT_INSPECTOR_SERVER is a robustness fallback that makes
# shouldUnshareNetwork() skip --unshare-net even on an unpatched build.
export WEBKIT_ENABLE_NETWORK_ACCESS=1
if [ -z "${WEBKIT_INSPECTOR_SERVER:-}" ]; then
    export WEBKIT_INSPECTOR_SERVER=127.0.0.1:0
fi

# Use a private GStreamer 1.26 prefix (IMWB_GST_PREFIX=/opt/gst126) for
# WebRTC/GeForce Now. Stock Ubuntu 24.04 ships GStreamer 1.24, whose webrtcbin
# reads a=setup only on media level and rejects GFN's session-level
# setup/missing mid -> setRemoteDescription collapses with 0xC0F2220E.
# GStreamer 1.26.11 added "allow session level in setup attribute" + optional
# mid. BubblewrapLauncher explicitly binds LD_LIBRARY_PATH/GST_PLUGIN_PATH and
# --setenv's LD_LIBRARY_PATH into the web process, so the prefix reaches
# WPEWebProcess. Opt-in only: when unset, the system GStreamer is used.
if [ -n "${IMWB_GST_PREFIX:-}" ]; then
    export LD_LIBRARY_PATH="$IMWB_GST_PREFIX/lib/x86_64-linux-gnu:$IMWB_GST_PREFIX/lib:${LD_LIBRARY_PATH:-}"
    export GST_PLUGIN_PATH="$IMWB_GST_PREFIX/lib/x86_64-linux-gnu/gstreamer-1.0"
    export GST_PLUGIN_SYSTEM_PATH=""
    export GST_PLUGIN_SCANNER="$IMWB_GST_PREFIX/libexec/gstreamer-1.0/gst-plugin-scanner"
    export GST_REGISTRY="$IMWB_GST_PREFIX/registry.bin"
fi
launch_app() {
    # SDL_VIDEO_WAYLAND_MODE_EMULATION=0 avoids a SIGFPE (divide-by-zero) in
    # SDL3 3.4.14's handle_wl_output_done() while libdecor-gtk drains the
    # wl_output.done queue — seen with the nested Weston on the dev PC. Mode
    # emulation is only cosmetic here, so disabling it is a safe no-op that
    # prevents the crash and keeps the libdecor titlebar (so the window can
    # still be moved by hand).
    WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
    SDL_VIDEODRIVER=wayland \
    EGL_PLATFORM=wayland \
    SDL_VIDEO_WAYLAND_PREFER_LIBDECOR=1 \
    SDL_VIDEO_WAYLAND_MODE_EMULATION=0 \
    "$BUILD_DIR/imwebbrowser" "$@"
}

# Weston places new windows at a random position, so after the browser maps
# we read its exact surface position from `weston-debug scene-graph` and drag the
# window (with Weston's own move binding, binding-modifier=ctrl) so its
# centre lands on the output's centre. Harmless no-op if the tools or the
# debug extension are unavailable.
center_browser_window() {
    local winid="" block="" best_x="" best_y="" best_w="" best_h=""
    local best_area=-1 x0="" y0="" x1="" y1="" w="" h="" area=""
    local tx="" ty="" dx="" dy="" cwx="" cwy="" px="" py=""

    for _ in $(seq 1 50); do
        winid=$(wmctrl -l 2>/dev/null | awk '/Weston Compositor/{print $1; exit}')
        [ -n "$winid" ] && break
        sleep 0.2
    done
    [ -z "$winid" ] && return 0

    for _ in $(seq 1 80); do
        block=$(WAYLAND_DISPLAY="$WAYLAND_DISPLAY" weston-debug scene-graph 2>/dev/null \
            | awk -v p="$APP_PID" '
                /^[[:space:]]*View / {cur=$0; next}
                /^[[:space:]]*position:/ && cur != "" {
                    if (cur ~ ("PID " p) && cur !~ /wl_pointer-cursor|, cursor,|panel for/) {
                        n = split($0, a, /[^0-9]+/)
                        if (n >= 4) print a[2], a[3], a[4], a[5]
                    }
                    cur = ""
                }') || true
        [ -n "$block" ] && break
        sleep 0.3
    done
    [ -z "$block" ] && return 0

    # Several surfaces may share the browser PID; use the largest one.
    while read -r x0 y0 x1 y1; do
        w=$((x1 - x0)); h=$((y1 - y0)); area=$((w * h))
        if [ "$area" -gt "$best_area" ]; then
            best_area=$area; best_x=$x0; best_y=$y0; best_w=$w; best_h=$h
        fi
    done <<<"$block"
    [ "$best_area" -gt 0 ] || return 0

    tx=$(( (CANVAS_WIDTH - best_w) / 2 )); [ "$tx" -lt 0 ] && tx=0
    ty=$(( (CANVAS_HEIGHT - best_h) / 2 )); [ "$ty" -lt 0 ] && ty=0
    dx=$(( tx - best_x )); dy=$(( ty - best_y ))
    if [ "$dx" -eq 0 ] && [ "$dy" -eq 0 ]; then
        echo "==> Browser already centred on the Weston output."
        return 0
    fi

    read -r cwx cwy < <(xwininfo -id "$winid" -display "${DISPLAY:-}" 2>/dev/null \
        | awk '/Absolute upper-left X/{x=$4} /Absolute upper-left Y/{y=$4} END{print x, y}') || true
    [ -n "${cwx:-}" ] || return 0

    px=$((cwx + best_x + best_w / 2)); py=$((cwy + best_y + best_h / 2))
    xdotool windowactivate "$winid" 2>/dev/null || true
    sleep 0.2
    xdotool mousemove "$px" "$py"
    sleep 0.2
    xdotool keydown ctrl
    xdotool mousedown 1
    sleep 0.1
    xdotool mousemove $((px + dx / 4)) $((py + dy / 4))
    sleep 0.05
    xdotool mousemove $((px + dx / 2)) $((py + dy / 2))
    sleep 0.05
    xdotool mousemove $((px + dx * 3 / 4)) $((py + dy * 3 / 4))
    sleep 0.05
    xdotool mousemove $((px + dx)) $((py + dy))
    sleep 0.1
    xdotool mouseup 1
    xdotool keyup ctrl
    echo "==> Browser centred on the Weston output (moved ${dx}px,${dy}px)"
}

if [ -z "$WESTON_PID" ]; then
    exec launch_app "${APP_ARGS[@]}"
fi

echo "==> Launching ImWebBrowser (WAYLAND_DISPLAY=$WAYLAND_DISPLAY) ..."
launch_app "${APP_ARGS[@]}" &
APP_PID=$!
center_browser_window

# Weston's incremental damage rarely clears the rectangle a window used to
# fill, so after a minimize the old frame lingers until the cursor damages it
# region by region (the "MS Paint eraser" effect). Watch for the browser view
# disappearing while the process is still alive (that is, the '_' button was
# pressed) and sweep the pointer once over the whole output to force the
# gl-renderer to repaint the stale surface. Requires the debug extension and
# the same tools as the centring code; otherwise a no-op.
if command -v weston-debug >/dev/null 2>&1 \
    && command -v xdotool >/dev/null 2>&1 \
    && command -v wmctrl >/dev/null 2>&1 \
    && command -v xwininfo >/dev/null 2>&1
then
    local_winid=$(wmctrl -l 2>/dev/null | awk '/Weston Compositor/{print $1; exit}') || true
    read -r cwx cwy < <(xwininfo -id "$local_winid" -display "${DISPLAY:-}" 2>/dev/null \
        | awk '/Absolute upper-left X/{x=$4} /Absolute upper-left Y/{y=$4} END{print x, y}') || true

    seen=0
    was_gone=0
    while kill -0 "$APP_PID" 2>/dev/null; do
        # Is the browser still visible in the compositor's scene graph?
        visible=$(WAYLAND_DISPLAY="$WAYLAND_DISPLAY" weston-debug scene-graph 2>/dev/null \
            | awk -v p="$APP_PID" '
                /^[[:space:]]*View / {
                    if ($0 ~ ("PID " p) && $0 !~ /wl_pointer-cursor|, cursor,|panel for/) found = 1
                }
                END { print (found ? 1 : 0) }') || true
        if [ -n "$visible" ]; then
            [ "$visible" = 1 ] && seen=1
            if [ "$seen" = 1 ] && [ -n "${cwx:-}" ] \
                && [ "$visible" = 0 ] && [ "$was_gone" = 0 ]
            then
                if ! kill -0 "$APP_PID" 2>/dev/null; then
                    break
                fi
                echo "==> Browser window hidden (minimized); sweeping output to clear stale pixels ..."
                for py in $(seq 4 24 "$((CANVAS_HEIGHT - 32))"); do
                    xdotool mousemove "$((cwx + CANVAS_WIDTH - 4))" "$((cwy + py))"
                    xdotool mousemove "$((cwx + 4))" "$((cwy + py))"
                done
                # Park the pointer at the canvas centre away from the panel.
                xdotool mousemove "$((cwx + CANVAS_WIDTH / 2))" "$((cwy + CANVAS_HEIGHT / 2))"
            fi
            was_gone=0
            [ "$visible" = 0 ] && was_gone=1
        fi
        sleep 2
    done
fi

wait "$APP_PID"