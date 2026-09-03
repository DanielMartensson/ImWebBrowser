#!/usr/bin/env bash
#
# setup.sh — dev-PC helper: build ImWebBrowser AND every dependency from
# source, Gentoo-style, into the app-local deps/ prefix.
#
#   Every dependency (SDL3, libwpe, wpebackend-fdo, WPE WebKit, GStreamer)
#   is downloaded as *source*, configured, built and installed into deps/.
#   The distro's packages are either missing (SDL3) or too old/broken for
#   GeForce Now (GStreamer 1.24 webrtcbin), so nothing relies on apt for these.
#   On the target (OpenSTLinux/Yocto) setup.sh is NOT used — the meta-layer's
#   recipes build the same stack; run.sh works on both machines.
#
# It is idempotent: sources are only downloaded when missing, and existing
# build trees under deps/src are reused — it only (re)builds what is missing
# or out of date.
#
# Usage
# -----
#   ./setup.sh                          # build everything from source into deps/install
#   ./setup.sh --prefix ~/imwb          # install into a private prefix instead
#   ./setup.sh --jobs 4                 # parallel build
#   ./setup.sh --with-sdl3/--skip-sdl3  # include/exclude a dependency
#
#   # Hardware / feature options (passed on to the ImWebBrowser CMake build):
#   ./setup.sh --vulkan                 # Vulkan render backend (default: OpenGL ES 3)
#   ./setup.sh --decoder=vah264dec      # bake in a default GStreamer decoder
#   ./setup.sh --media-hw-types='video/mp4; codecs="avc1"'
#   ./setup.sh --gfn-input              # GeForce NOW input bridge (off by default)
#   ./setup.sh --cmake="-DENABLE_WEBRTC=OFF -DENABLE_DEVELOPER_EXTRAS=ON"
#
# Sources are fetched automatically when missing — re-running reuses both the
# downloaded sources and the build caches, so there are no download/build mode
# flags to think about.
#
# All dependencies are bundled app-locally under deps/ (never into the system):
# deps/src      — downloaded + extracted + patched sources
# deps/install  — the single private prefix (lib, include, bin, ...) everything
#                 is installed into. run.sh points at it, so the whole tree is
#                 self-contained and apt never touches these.
#
# Build order (respects the dependency graph):
#   SDL3 -> libwpe -> wpebackend-fdo -> GStreamer 1.26 -> WPE WebKit -> ImWebBrowser
#
set -euo pipefail

# ---------------------------------------------------------------------------
# Configurable defaults (override on the command line or via env).
# ---------------------------------------------------------------------------
PREFIX="${IMWB_PREFIX:-$PWD/deps/install}"          # where everything lands
SRC_DIR="${IMWB_SRC_DIR:-$PWD/deps/src}"            # downloaded + extracted source
JOBS="$(nproc)"
EXTRA_CMAKE_ARGS=""   # extra defines for the final ImWebBrowser CMake build
# Dependency enable/disable switches (all ON by default; the two needed for
# GeForce Now are wpewebkit + gstreamer, both mandatory).
WITH_SDL3=1
WITH_LIBWPE=1
WITH_WPEBACKEND=1
WITH_GSTREAMER=1
WITH_WPEWEBKIT=1

# --- read command line ---
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix=*)     PREFIX="${1#*=}" ;;
        --jobs=*)       JOBS="${1#*=}" ;;
        # Hardware / feature options — forwarded to the ImWebBrowser CMake
        # build (setup.sh runs the project as a plain CMake project last).
        --vulkan)       EXTRA_CMAKE_ARGS="$EXTRA_CMAKE_ARGS -DIMWB_BACKEND_VULKAN=ON" ;;
        --decoder=*)    EXTRA_CMAKE_ARGS="$EXTRA_CMAKE_ARGS -DIMWB_VIDEO_DECODER=${1#*=}" ;;
        --media-hw-types=*) EXTRA_CMAKE_ARGS="$EXTRA_CMAKE_ARGS -DIMWB_MEDIA_HW_TYPES=${1#*=}" ;;
        --gfn-input)    EXTRA_CMAKE_ARGS="$EXTRA_CMAKE_ARGS -DENABLE_GFN_INPUT_BRIDGE=ON" ;;
        --cmake=*)      EXTRA_CMAKE_ARGS="$EXTRA_CMAKE_ARGS ${1#*=}" ;;
        --with-sdl3)    WITH_SDL3=1 ;;
        --skip-sdl3)    WITH_SDL3=0 ;;
        --with-libwpe)  WITH_LIBWPE=1 ;;
        --skip-libwpe)  WITH_LIBWPE=0 ;;
        --with-wpebackend-fdo) WITH_WPEBACKEND=1 ;;
        --skip-wpebackend-fdo) WITH_WPEBACKEND=0 ;;
        --with-gstreamer) WITH_GSTREAMER=1 ;;
        --skip-gstreamer) WITH_GSTREAMER=0 ;;
        --with-wpewebkit) WITH_WPEWEBKIT=1 ;;
        --skip-wpewebkit) WITH_WPEWEBKIT=0 ;;
        -h|--help)
            sed -n '2,33p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 1 ;;
    esac
    shift
done

LIBDIR="lib"
[ "$(uname -m)" = x86_64 ] && LIBDIR="lib/x86_64-linux-gnu"

# ---------------------------------------------------------------------------
# Environment so later packages see earlier ones (pkg-config drives the build).
# NOTE: deliberately NOT inheriting PKG_CONFIG_PATH / CMAKE_PREFIX_PATH /
# LD_LIBRARY_PATH from the caller — a polluted environment (e.g. some other
# private prefix on this machine) leaks into dependency lookups and produces
# cross-prefix header mismatches that are very painful to debug. The system
# defaults are still reachable: pkg-config/ldconfig always fall back to their
# built-in paths after these.
# ---------------------------------------------------------------------------
export PKG_CONFIG_PATH="$PREFIX/$LIBDIR/pkgconfig:$PREFIX/lib/pkgconfig"
export CMAKE_PREFIX_PATH="$PREFIX"
export LD_LIBRARY_PATH="$PREFIX/$LIBDIR:$PREFIX/lib"
export PATH="$PREFIX/bin:$PATH"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
say() { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
need_sudo() { [ "$(id -u)" -eq 0 ]; }

require_cmd() {
    for c in "$@"; do
        command -v "$c" >/dev/null 2>&1 || {
            echo "FATAL: missing tool '$c'. Install build tools first (cmake, ninja, meson, ...)." >&2
            exit 1
        }
    done
}

# fetch <url> <name> — download + extract a source tarball into $SRC_DIR only once.
fetch() {
    local url="$1" name="$2"
    local tar
    tar="$SRC_DIR/$(basename "$url")"
    mkdir -p "$SRC_DIR"
    if [ -d "$SRC_DIR/$name" ]; then
        echo "   [skip] $name already extracted"
        return
    fi
    if [ ! -f "$tar" ]; then
        echo "   [fetch] $url"
        curl -fSL --retry 3 -o "$tar" "$url"
    fi
    echo "   [extract] $name"
    mkdir -p "$SRC_DIR/$name.tmp"
    tar -xf "$tar" -C "$SRC_DIR/$name.tmp" --strip-components=1
    mv "$SRC_DIR/$name.tmp" "$SRC_DIR/$name"
    # Keep the tarball: a later forced clean rebuild (deleted build dir or
    # re-extract) reuses it instead of re-downloading.
}

# fetch_gstreamer — GStreamer ships as a monorepo whose layout
# (subprojects/gst-plugins-bad/...) is what our ./patches target. The split
# release tarballs do NOT have that layout, so clone the git monorepo at the
# pinned tag. Sparse + shallow keeps the download small.
fetch_gstreamer() {
    # NOTE: two separate `local` lines — bash expands all arguments of a single
    # `local` statement BEFORE it runs, so `dest=$SRC_DIR/gstreamer-$ver` in the
    # same statement would hit `ver` unbound under `set -u`.
    local ver="1.26.11"
    local dest="$SRC_DIR/gstreamer-$ver"
    [ -d "$dest" ] && { echo "   [skip] gstreamer-$ver already present"; return; }
    echo "   [clone] GStreamer monorepo @ $ver"
    git clone --depth 1 --branch "$ver" \
        https://gitlab.freedesktop.org/gstreamer/gstreamer.git "$dest"
}

# apply_patch <srcdir> <patch-file>
# Apply an upstream patch with `git apply` (these are git-format diffs; GNU
# `patch` rejects git diffs). Works on plain extracted tarballs (no .git
# needed) and detects already-applied patches via `git apply --reverse --check`
# so the script is idempotent.
apply_patch() {
    local srcdir="$1" patch="$2"
    # Absolutize the patch path first: we `cd` into $srcdir below, which would
    # break a repo-relative path like patches/foo.patch.
    case "$patch" in
        /*) ;;
        *)  patch="$PWD/$patch" ;;
    esac
    if [ ! -f "$patch" ]; then
        echo "   [warn] patch not found: $patch" >&2
        return 1
    fi
    if ( cd "$srcdir" && git apply --check "$patch" 2>/dev/null ); then
        echo "   [patch] $(basename "$patch")"
        ( cd "$srcdir" && git apply "$patch" )
    elif ( cd "$srcdir" && git apply --reverse --check "$patch" 2>/dev/null ); then
        echo "   [skip]  $(basename "$patch") already applied"
    else
        echo "   [FAIL]  $(basename "$patch") does not apply — wrong source version?" >&2
        return 1
    fi
}

# install_ <srcdir> — run `ninja install`/`cmake --install`, elevating to sudo
# when the prefix is not user-writable.
run_install() {
    local dir="$1" extra="${2:-}"
    if need_sudo; then
        eval "cmake --install \"$dir\" $extra"
    else
        # Bump permissions via sudo only if the prefix needs it (e.g. /usr/local).
        local k="$PREFIX"
        while [ "$k" != "/" ] && [ ! -w "$k" ]; do k=$(dirname "$k"); done
        if [ -w "$k" ]; then
            eval "cmake --install \"$dir\" $extra"
        else
            echo "   [sudo] installing into $PREFIX"
            eval "sudo env PKG_CONFIG_PATH=\"$PKG_CONFIG_PATH\" \
                LD_LIBRARY_PATH=\"$LD_LIBRARY_PATH\" PATH=\"$PATH\" \
                cmake --install \"$dir\" $extra"
        fi
    fi
}

# ---------------------------------------------------------------------------
# 0. System packages via APT (the ONLY thing apt installs — standard, distro
#    packages we never re-build or touch). Everything under the "Bundled deps"
#    sections below is built from source into deps/install instead.
#
# Run once (Debian/Ubuntu desktop) — the same list as in README.md. On
# Yocto/OpenSTLinux the meta-layer's recipes provide the equivalents.
#
#   sudo apt-get install \
#       build-essential cmake ninja-build meson pkg-config curl git tar python3 \
#       flex bison gobject-introspection libgirepository1.0-dev \
#       libglib2.0-dev libsoup-3.0-dev libgcrypt20-dev libepoxy-dev \
#       libegl-dev libgles2-mesa-dev libxkbcommon-dev libwayland-dev \
#       libdrm-dev libffi-dev libxml2-dev libxslt1-dev libsqlite3-dev \
#       libharfbuzz-dev libfreetype-dev libfontconfig1-dev libicu-dev \
#       libpng-dev libjpeg-dev libwebp-dev libtasn1-dev libpsl-dev \
#       libseccomp-dev \
#       bubblewrap xdg-dbus-proxy \
#       libgl1-mesa-dri libgles2-mesa pipewire wireplumber \
#       fonts-dejavu-core fonts-liberation \
#       libva-dev i965-va-driver  # VAAPI hardware decode (va plugin, vah264dec)
#
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# 1. SDL3 3.4.14 — windowing, input, render backend.
# ---------------------------------------------------------------------------
build_sdl3() {
    local d="$SRC_DIR/SDL"
    [ -d "$d" ] || return 0
    local b="$d/build"
    say "SDL3"
    cmake -S "$d" -B "$b" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" "$@"
    cmake --build "$b" -j"$JOBS"
    run_install "$b"
}

# ---------------------------------------------------------------------------
# 3. libwpe 1.16.3 — the WPE generic backend API.
# ---------------------------------------------------------------------------
build_libwpe() {
    local d="$SRC_DIR/libwpe-1.16.3"
    [ -d "$d" ] || return 0
    local b="$d/build"
    say "libwpe"
    cmake -S "$d" -B "$b" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_DOCS=OFF "$@"
    cmake --build "$b" -j"$JOBS"
    run_install "$b"
}

# ---------------------------------------------------------------------------
# 4. wpebackend-fdo 1.16.1 — exports WebKit frames as dmabuf/EGLImage.
#    A MESON project (no CMakeLists.txt).
# ---------------------------------------------------------------------------
build_wpebackend() {
    local d="$SRC_DIR/wpebackend-fdo-1.16.1"
    [ -d "$d" ] || return 0
    local b="$d/build"
    say "wpebackend-fdo"
    meson setup "$b" "$d" --prefix="$PREFIX" -Dbuild_docs=false "$@"
    meson compile -C "$b" -j"$JOBS"
    if need_sudo; then
        meson install -C "$b"
    else
        local k="$PREFIX"
        while [ "$k" != "/" ] && [ ! -w "$k" ]; do k=$(dirname "$k"); done
        if [ -w "$k" ]; then
            meson install -C "$b"
        else
            echo "   [sudo] installing wpebackend-fdo into $PREFIX"
            sudo env LD_LIBRARY_PATH="$LD_LIBRARY_PATH" PATH="$PATH" meson install -C "$b"
        fi
    fi
}

# ---------------------------------------------------------------------------
# 5. GStreamer 1.26.11 (monorepo) — media framework + WebRTC (GeForce Now).
#    Needs the two webrtcbin patches in ./patches. Builds core + base + good +
#    bad + libav + webrtc + libnice so <audio>/<video> and RTCPeerConnection
#    all work. System GStreamer 1.24's webrtcbin fails GeForce Now (0xC0F2220E),
#    hence the 1.26 source build.
# ---------------------------------------------------------------------------
build_gstreamer() {
    local d="$SRC_DIR/gstreamer-1.26.11"
    [ -d "$d" ] || return 0
    say "GStreamer 1.26.11 (source build with WebRTC)"
    apply_patch "$d" "patches/gstreamer-webrtcbin-audio-opus-ptmap-fallback.patch"
    apply_patch "$d" "patches/gstreamer-webrtcbin-balanced-to-maxbundle.patch"
    local b="$d/build"
    # NOTE: -Dva must be namespaced (-Dgst-plugins-bad:va=...) — the option
    # lives in the subproject, unlike e.g. -Dwebrtc which the monorepo
    # re-exports at the top level.
    meson setup "$b" "$d" \
        --prefix="$PREFIX" \
        -Dbase=enabled -Dgood=enabled -Dbad=enabled -Dugly=disabled -Dlibav=enabled -Dwebrtc=enabled \
        -Dgst-plugins-bad:va=enabled \
        -Dgst-plugins-bad:tests=disabled -Dtests=disabled -Dexamples=disabled \
        -Dbenchmarks=disabled -Dgtk_doc=disabled "$@"
    meson compile -C "$b" -j"$JOBS"
    if need_sudo; then
        meson install -C "$b"
    else
        local k="$PREFIX"
        while [ "$k" != "/" ] && [ ! -w "$k" ]; do k=$(dirname "$k"); done
        if [ -w "$k" ]; then
            meson install -C "$b"
        else
            echo "   [sudo] installing GStreamer into $PREFIX"
            sudo env LD_LIBRARY_PATH="$LD_LIBRARY_PATH" PATH="$PATH" meson install -C "$b"
        fi
    fi
}

# ---------------------------------------------------------------------------
# 6. WPE WebKit 2.52.6 (stable) — the browser engine. WebRTC + media enabled,
#    with the two patches this project needs.
# ---------------------------------------------------------------------------
build_wpewebkit() {
    local d="$SRC_DIR/wpewebkit-2.52.6"
    [ -d "$d" ] || return 0
    say "WPE WebKit 2.52.6 (WebRTC + media)"
    apply_patch "$d" "patches/wpe-webkit-bwrap-unshare-net-webrtc.patch"
    apply_patch "$d" "patches/wpe-webkit-empty-body-js-mime.patch"
    local b="$d/build"
    cmake -S "$d" -B "$b" \
        -DPORT=WPE \
        -DENABLE_MEDIA_STREAM=ON -DENABLE_WEB_RTC=ON \
        -DENABLE_WEBDRIVER=OFF -DENABLE_JOURNALD_LOG=OFF \
        -DENABLE_SPEECH_SYNTHESIS=OFF -DUSE_LIBBACKTRACE=OFF \
        -DENABLE_ENCRYPTED_MEDIA=OFF \
        -DENABLE_JIT=ON \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" "$@"
    cmake --build "$b" -j"$JOBS"
    run_install "$b"
}

# ---------------------------------------------------------------------------
# 7. ImWebBrowser — this project. Found via pkg-config, so the prefix must
#    already hold SDL3/libwpe/backend/wpewebkit/gstreamer.
# ---------------------------------------------------------------------------
build_imwebbrowser() {
    local root="$PWD"
    say "ImWebBrowser"
    # The project itself is a plain CMake project — this is just
    # cmake -S . -B .deps/build-imwb with the bundled prefix on the search
    # path, plus any hardware/feature defines from the command line.
    # (Vulkan auto-disables the GLES backend inside CMake when both are set.)
    rm -rf "$root/.deps/build-imwb"
    # shellcheck disable=SC2086 # EXTRA_CMAKE_ARGS is intentionally word-split
    cmake -S "$root" -B "$root/.deps/build-imwb" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DIMWB_BACKEND_OPENGL_ES=ON \
        $EXTRA_CMAKE_ARGS
    cmake --build "$root/.deps/build-imwb" -j"$JOBS"
    run_install "$root/.deps/build-imwb"
}

# ---------------------------------------------------------------------------
# Full dependency list (name | enable | fetch-url | build-fn), in dependency
# order. GStreamer uses a git clone instead of a tarball — the URL decides.
# Each enabled dep is fetched-if-missing and then built, one at a time, so an
# earlier install satisfies the next configure (pkg-config picks up $PREFIX).
# ---------------------------------------------------------------------------
declare -a DEPS=(
    "SDL3|$WITH_SDL3|https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.14.tar.gz|build_sdl3"
    "libwpe|$WITH_LIBWPE|https://wpewebkit.org/releases/libwpe-1.16.3.tar.xz|build_libwpe"
    "wpebackend-fdo|$WITH_WPEBACKEND|https://wpewebkit.org/releases/wpebackend-fdo-1.16.1.tar.xz|build_wpebackend"
    "GStreamer|$WITH_GSTREAMER|https://gitlab.freedesktop.org/gstreamer/gstreamer.git|build_gstreamer"
    "WPE-WebKit|$WITH_WPEWEBKIT|https://wpewebkit.org/releases/wpewebkit-2.52.6.tar.xz|build_wpewebkit"
)

name_for_fetch() {
    # Reduce a URL/tag to the extract-dir name the build_* fn expects.
    case "$1" in
        *SDL*)          echo SDL ;;
        *libwpe-1.16.3*) echo libwpe-1.16.3 ;;
        *wpebackend-fdo-1.16.1*) echo wpebackend-fdo-1.16.1 ;;
        *wpewebkit-2.52.6*) echo wpewebkit-2.52.6 ;;
    esac
}

main() {
    echo "ImWebBrowser — from-source setup"
    echo "  prefix : $PREFIX"
    echo "  source : $SRC_DIR"
    echo "  jobs   : $JOBS"
    echo

    require_cmd cmake curl tar git ninja meson pkg-config

    for row in "${DEPS[@]}"; do
        IFS='|' read -r _name _en _url _fn <<<"$row"
        [ "$_en" = 1 ] || continue
        case "$_url" in
            *gstreamer.git*) fetch_gstreamer ;;
            *)               fetch "$_url" "$(name_for_fetch "$_url")" ;;
        esac
        "$_fn"
    done

    build_imwebbrowser

    say "Done. ImWebBrowser + bundled dependencies are installed into: $PREFIX"
    if [ "$PREFIX" = "$PWD/deps/install" ] || [ "$PREFIX" = "deps/install" ]; then
        echo "Run it with:  ./run.sh --kiosk https://example.com"
    else
        echo "Run it with:  IMWB_PREFIX=$PREFIX ./run.sh --kiosk https://example.com"
    fi
}

main "$@"
