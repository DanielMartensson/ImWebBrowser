#!/usr/bin/env bash
set -euo pipefail

# Rebuild WPE WebKit 2.53.90 from scratch on this dev machine (weak: 7.6 GiB RAM -> -j1).
# Produce a WebRTC-enabled WPE WebKit install into /home/mint/wpe-rtc, then point ImWebBrowser at it:
#   export LD_LIBRARY_PATH=/home/mint/wpe-rtc/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
#
# IMPORTANT FLAGS (learned the hard way):
#   -DENABLE_WEB_RTC=ON   +  -DENABLE_MEDIA_STREAM=ON   -> required by play.geforcenow.com
#   -DENABLE_WEBDRIVER=OFF                               -> WebDriver fails to compile in Release builds
#        (unguarded LOG_CHANNEL(WebDriverClassic) in WebDriverService.cpp, logging compiled out). Not needed.
#        Keep this OFF so a plain build never hits the bug; do NOT toggle it mid-build (triggers full rebuild).
#   -DENABLE_JOURNALD_LOG=OFF                            -> explicit, matches the Yocto recipe (wpewebkit_2.53.91.bb)
#   Must use /usr/bin/cmake (3.28.3), NOT ~/.local/bin/cmake (4.4.2).
#   Run with -j1; --max-load helps avoid OOM.

SRC_DIR="${IMWB_WEBKIT_SRC:-/home/mint/wpewebkit-rtc-src}"
BUILD_DIR="${IMWB_WEBKIT_BUILD:-/home/mint/wpe-rtc-build}"
PREFIX="${IMWB_WEBKIT_PREFIX:-/home/mint/wpe-rtc}"

if [ ! -d "$SRC_DIR" ]; then
    echo "source tree not found at $SRC_DIR (extract wpewebkit-2.53.90.tar.xz first)" >&2
    exit 1
fi

mkdir -p "$BUILD_DIR"

/usr/bin/cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DPORT=WPE \
    -DENABLE_MEDIA_STREAM=ON \
    -DENABLE_WEB_RTC=ON \
    -DENABLE_WEBDRIVER=OFF \
    -DENABLE_JOURNALD_LOG=OFF \
    -DENABLE_SPEECH_SYNTHESIS=OFF \
    -DUSE_LIBBACKTRACE=OFF \
    -DENABLE_JIT=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"

ninja -C "$BUILD_DIR" -j1 --max-load="$(nproc)"
ninja -C "$BUILD_DIR" install

echo
echo "Done. Use it with:  export LD_LIBRARY_PATH=$PREFIX/lib"