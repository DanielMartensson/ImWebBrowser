<p align="center">
  <img alt="engine" src="https://img.shields.io/badge/engine-WPE%20WebKit%202.52-blue" />
  <img alt="UI" src="https://img.shields.io/badge/UI-SDL3%20%2B%20Dear%20ImGui-green" />
  <img alt="rendering" src="https://img.shields.io/badge/rendering-zero--copy%20DMA--buf-orange" />
  <img alt="backends" src="https://img.shields.io/badge/backends-OpenGLES%203%20%7C%20Vulkan-purple" />
  <img alt="platform" src="https://img.shields.io/badge/platform-Linux-lightgrey" />
</p>

# ImWebBrowser

A fast **kiosk-grade web browser** built on **SDL3 + Dear ImGui + WPE WebKit**
with **zero-copy DMA-buf rendering** (OpenGL ES 3 by default, Vulkan optional).
Because it rides WPE WebKit, the same binary runs on a desktop, a Yocto board
(STM32MP257F + VPU) or any embedded Linux — it was built to bring cloud gaming
(NVIDIA GeForce Now), video and WebGL to a small screen.

## Getting started

```bash
# 1. Standard packages via apt (installed once — see list below)
sudo apt-get install build-essential cmake ninja-build meson pkg-config curl git ...

# 2. Build the whole stack from source (lands in ./deps/ — never touches the system)
./setup.sh

# 3. Run
./run.sh --kiosk https://example.com
```

## Which script runs where

| Script | Dev PC (Linux) | Target (STM32MP257F / OpenSTLinux) |
|---|---|---|
| `setup.sh` | ✅ builds every dependency from source into `deps/` | ❌ never used — the Yocto meta-layer's recipes build the dependencies |
| `cmake -B build` | ✅ (what `setup.sh` runs as its last step) | ✅ the same build, driven by the recipe |
| `run.sh` | ✅ | ✅ the same script on both |

`run.sh` works unchanged on both machines: on the dev PC it picks the build in
`build/` and exports the env needed to find the bundled `deps/install` prefix
(private libs + GStreamer 1.26); on the target the recipe has installed the
browser and its dependencies into the system, so `run.sh` simply finds the
system binary and runs it — the bundled-prefix env block is skipped
automatically. `setup.sh` is the dev-PC-only convenience that produces the same
end state the recipes produce on the target.

## What is installed from where?

Two kinds of dependencies, strictly separated:

**1. From source — built by `setup.sh` into `deps/`** (pinned versions,
app-local, apt never touches them):

| Component | Version | Why from source |
|---|---|---|
| SDL3 | 3.4.14 | Not in apt at all (Ubuntu ships only SDL2) |
| libwpe | 1.16.3 | WPE backend API |
| wpebackend-fdo | 1.16.1 | Exports WebKit frames as dmabuf/EGLImage |
| GStreamer | 1.26.11 | Ubuntu's 1.24 has a broken `webrtcbin` for GeForce Now (`0xC0F2220E`) |
| WPE WebKit | 2.52.6 | Distro builds lack the WebRTC/media bits |

**2. Via apt-get — standard packages that never need patching:**

```bash
sudo apt-get install \
    build-essential cmake ninja-build meson pkg-config curl git tar python3 \
    flex bison gobject-introspection libgirepository1.0-dev \
    libglib2.0-dev libsoup-3.0-dev libgcrypt20-dev libepoxy-dev \
    libegl-dev libgles2-mesa-dev libxkbcommon-dev libwayland-dev \
    libdrm-dev libffi-dev libxml2-dev libxslt1-dev libsqlite3-dev \
    libharfbuzz-dev libfreetype-dev libfontconfig1-dev libicu-dev \
    libpng-dev libjpeg-dev libwebp-dev libtasn1-dev libpsl-dev \
    libseccomp-dev \
    bubblewrap xdg-dbus-proxy \
    libgl1-mesa-dri pipewire wireplumber \
    fonts-dejavu-core fonts-liberation
```

> The rule is the same everywhere: whatever the platform cannot deliver must be
> built from source. On the dev PC that means apt's versions (missing/broken) —
> handled by `setup.sh`. On the target it means whatever the OpenSTLinux
> (Yocto Scarthgap) layers cannot provide (e.g. Vulkan) — handled by the
> recipes in the meta-layer.

## What works

| Platform | Status |
|---|---|
| Linux x86-64 (dev PC) | ✅ |
| STM32MP257F (Yocto/Watermelon-Wine) | ✅ |
| Windows / macOS | ❌ |

| Service | Status |
|---|---|
| GeForce Now (WebRTC) | ✅ |
| YouTube | ✅ |
| Web Fish Tank (WebGL) | ✅ |
| DuckDuckGo | ✅ |
| Netflix (Widevine/EME) | ⚠️ untested |

## Running

```bash
./run.sh                              # normal window
./run.sh --kiosk https://example.com  # kiosk mode
./run.sh --kiosk "https://play.geforcenow.com/games?game-id=0b3b25bf-a12d-4b0e-892a-348dba794901"
```

Environment variables (all optional):

| Variable | Meaning |
|---|---|
| `IMWB_PREFIX` | A bundled prefix other than `deps/install` |
| `IMWB_BUILD_DIR` | A build directory other than `build` |
| `IMWB_VIDEO_DECODER` | Boost a GStreamer decoder to MAX rank, e.g. `v4l2slh264dec` on the STM32MP257F |

`setup.sh` flags: `--prefix=`, `--jobs=`, `--with-*` / `--skip-*` (idempotent —
sources are fetched automatically when missing and build caches are reused).
Use `--jobs 1` or `--jobs 2` when building WPE WebKit on a machine with little RAM.

## Features

- Zero-copy rendering: WebKit exports frames as dmabuf/EGLImage imported as GL textures
- WebRTC + media stream (cloud gaming, video)
- Swedish keyboard support via xkbcommon
- Audio through GStreamer → PipeWire
- Kiosk mode, FPS overlay (Dear ImGui)

## Patches

Four upstream patches are applied by `setup.sh` (via `git apply`, idempotent):

| Patch | Purpose |
|---|---|
| `wpe-webkit-bwrap-unshare-net-webrtc.patch` | Let WebRTC see the network through the bubblewrap sandbox |
| `wpe-webkit-empty-body-js-mime.patch` | Empty JS responses with a wrong MIME type |
| `gstreamer-webrtcbin-audio-opus-ptmap-fallback.patch` | Opus fallback in webrtcbin PT mapping |
| `gstreamer-webrtcbin-balanced-to-maxbundle.patch` | BUNDLE085/MAXBUNDLE for GFN's SDP |

## Hardware acceleration & build options

The project itself is a plain **CMake project** — `setup.sh` runs it as the
last step of its dependency build, and every option below is an ordinary
CMake define that also works standalone:

```bash
cmake -B build -DIMWB_BACKEND_VULKAN=ON -DIMWB_VIDEO_DECODER=v4l2slh264dec
```

### Render backend (GPU)

| CMake option | Default | Meaning |
|---|---|---|
| `IMWB_BACKEND_OPENGL_ES` | `ON` | OpenGL ES 3 context via SDL3 GL |
| `IMWB_BACKEND_VULKAN` | `OFF` | Vulkan: dma-buf import + swapchain (auto-disables GLES) |

```bash
./setup.sh --vulkan
```

### Video decoding

| Knob | Where | Meaning |
|---|---|---|
| `IMWB_VIDEO_DECODER` | CMake (baked default) **and** runtime env | Boost one GStreamer decoder to MAX rank |
| `IMWB_MEDIA_HW_TYPES` | CMake | Advertise hardware-decodable MIME types to the page |

```bash
./setup.sh --decoder=v4l2slh264dec --media-hw-types='video/mp4; codecs="avc1"'
IMWB_VIDEO_DECODER=v4l2slh264dec ./run.sh --kiosk "https://play.geforcenow.com/..."
```

Decoder examples: `v4l2slh264dec` (STM32MP257F VPU, 1080p60),
`vah264dec` (Intel/AMD VAAPI), `avdec_h264` (software fallback).

### Any other CMake option

Everything else passes straight through:

```bash
./setup.sh --cmake="-DENABLE_GFN_INPUT_BRIDGE=ON -DENABLE_DEVELOPER_EXTRAS=ON"
./setup.sh --gfn-input          # shortcut for the GeForce NOW input bridge
```

Feature toggles (ON/OFF unless noted): `ENABLE_WEBRTC`, `ENABLE_MEDIA`,
`ENABLE_AUDIO`, `ENABLE_AUTOPLAY`, `ENABLE_ENCRYPTED_MEDIA` (Widevine/EME),
`ENABLE_WEBGL`, `ENABLE_2D_CANVAS`, `ENABLE_JAVASCRIPT`,
`ENABLE_MEDIA_CAPABILITIES`, `ENABLE_DEVELOPER_EXTRAS` (Web Inspector),
`ENABLE_BENCHMARK_HARNESS`, `ENABLE_GFN_INPUT_BRIDGE` (OFF by default),
plus accessibility and browsing toggles — see the header of
`CMakeLists.txt` for the full annotated list.

## Architecture (short)

```
SDL3 window (X11/Wayland/Vulkan)      ← the app draws it itself
   ↑ EGLImage/dmabuf (zero-copy)
wpebackend-fdo (EGL mode)             ← wpe_fdo_initialize_for_egl_display
   ↑ WPE bridge
WPE WebKit 2.52.6 (WPEWebProcess, WPENetworkProcess)
   ↑ webrtcbin
GStreamer 1.26.11
```

The app runs the WPE backend in EGL-export mode and blits WebKit's frames
itself into its own SDL3 window — so **no Wayland compositor is needed** (no
weston). Documentation lives in the code as comments and in this README.
