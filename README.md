<p align="center">
  <img alt="engine" src="https://img.shields.io/badge/engine-WPE%20WebKit%202.52-blue" />
  <img alt="UI" src="https://img.shields.io/badge/UI-SDL3%20%2B%20Dear%20ImGui-green" />
  <img alt="rendering" src="https://img.shields.io/badge/rendering-zero--copy%20DMA--buf-orange" />
  <img alt="backends" src="https://img.shields.io/badge/backends-OpenGLES%203%20%7C%20Vulkan-purple" />
  <img alt="platform" src="https://img.shields.io/badge/platform-Linux-lightgrey" />
</p>

# ImWebBrowser

A lightweight **kiosk-grade web browser** built on **SDL3 + Dear ImGui + WPE WebKit**
with **zero-copy DMA-buf rendering** (two render backends: OpenGL ES 3, default, and Vulkan).
Because it rides WPE WebKit, the exact same binary targets a desktop GPU, a Yocto board
(STM32MP257F + VPU), or any embedded Linux — it was built to bring cloud gaming
(NVIDIA GeForce Now), video and WebGL to a small screen.

<p align="center">
  <img alt="ImWebBrowser demo" src="docs/demo.gif" width="640">
</p>

---

## Build everything from source

The whole stack — **SDL3, libwpe, wpebackend-fdo, GStreamer 1.26 (with WebRTC), WPE WebKit**
and ImWebBrowser itself — is built **from source** by a single script, Gentoo-style.
Nothing depends on your distro packaging these, which is what you need on Yocto/OpenSTLinux:

```bash
./setup.sh                 # downloads + builds + installs everything -> /usr/local
./run --kiosk https://play.geforcenow.com   # then just run it
```

What `setup.sh` does under the hood (each step is one source build):

| # | Component | Version | Purpose |
|---|---|---|---|
| 1 | SDL3 | 3.4.14 | Windowing, input, render backend |
| 2 | libwpe | 1.16.3 | WPE backend API |
| 3 | wpebackend-fdo | 1.16.1 | Exports WebKit frames as dmabuf/EGLImage |
| 4 | **GStreamer** | 1.26.11 | Media framework + **WebRTC** (GeForce Now) |
| 5 | **WPE WebKit** | 2.52.6 | The browser engine (WebRTC + media enabled) |
| 6 | ImWebBrowser | *this repo* | The project itself |

Useful flags:

```bash
./setup.sh --download-only          # fetch all sources, build nothing
./setup.sh --build-only             # reuse downloaded sources, rebuild
./setup.sh --prefix ~/imwb          # install into a private prefix (not /usr/local)
./setup.sh --jobs 4 --skip-sdl3     # parallelism / dependency selection
```

`setup.sh` is **idempotent**: re-running it reuses already-downloaded source and any
existing build cache, and only rebuilds what is missing.

> **Why GStreamer 1.26 from source?** Ubuntu 24.04 ships GStreamer 1.24, whose
> `webrtcbin` fails GeForce Now's SDP exchange (`0xC0F2220E`). GStreamer 1.26 fixes
> session-level `a=setup` inheritance. Hence the pinned 1.26 source build.
>
> **Why WPE WebKit from source?** Distro WPE builds lack the WebRTC/media bits this
> project needs. The build enables `WEB_RTC` + `MEDIA_STREAM`.

### Already have the deps installed?

Then just build ImWebBrowser with plain CMake (deps are found via pkg-config):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

---

## What works

| Service / site | Status | Notes |
|---|---|---|
| 🎮 **NVIDIA GeForce Now** | ✅ | Cloud gaming: video + keyboard/mouse input bridge + auto-unmuted audio (`IMWB_GFN_BRIDGE=1`, script in `src/js/gfn/`) |
| 📺 **YouTube** | ✅ | VP9/AV1/H.264 + GPU compositing |
| 🎬 **Netflix** | ⚠️ Untested | Needs Widevine/EME — see [DRM note](#drm-netflix) |
| 🐠 **Web Fish Tank** | ✅ | Pure WebGL; benchmark it with `--bench-fish N` |
| 🔍 **DuckDuckGo** | ✅ | Default start page / search fallback |

| Building | Status |
|---|---|
| Linux x86-64 (dev PC) | ✅ |
| STM32MP257F (Yocto / OpenSTLinux) | ✅ |
| Windows / macOS | ❌ *(Linux-only — WPE WebKit backend)* |

---

## Running

```bash
./build/imwebbrowser [URL] [--kiosk] [--bench-fish N]   # OpenGL ES build
```

| Argument | Meaning |
|---|---|
| `URL` | Start page; defaults to `https://duckduckgo.com` |
| `--kiosk` | Fullscreen direct-blit, no toolbar/UI |
| `--bench-fish N` | Auto-start the WebGL Aquarium benchmark with N fish |

**GeForce Now** (the flags the whole stack was tuned for):

```bash
IMWB_GFN_BRIDGE=1 IMWB_GST_PREFIX=/opt/gst126 SDL_VIDEODRIVER=x11 \
  ./run --kiosk "https://play.geforcenow.com/games?game-id=0b3b25bf-a12d-4b0e-892a-348dba794901"
```

> `IMWB_GST_PREFIX` points at a GStreamer prefix at runtime. With `setup.sh`'s default
> `/usr/local` install you don't need it — the prefix is already the system one.

### Development: nested Weston

WPE renders through Wayland, so on an X11 desktop `run-native.sh` brings up a nested
Weston window automatically and tears it down on exit:

```bash
./run-native.sh https://example.com --kiosk
./run-native.sh --rebuild          # force a fresh configure + rebuild
```

### Keyboard

| Key | Action |
|---|---|
| `Ctrl+L` | Focus URL bar |
| `Ctrl+R` / `Ctrl+Shift+R` | Reload / bypass cache |
| `Alt+Left` / `Alt+Right` | Back / forward |
| `F11` | Toggle kiosk |
| `F3` | Stats overlay |
| `Ctrl+W` / `Ctrl+Q` | Quit |

### Environment variables

| Variable | Effect |
|---|---|
| `IMWB_WINDOW_SIZE=WxH` | Force initial window size |
| `IMWB_NOVSYNC` | Disable vsync |
| `IMWB_STATS` | Per-second FPS/export/present counters |
| `IMWB_DEBUG_INPUT` | Verbose input logging |
| `IMWB_PREFIX` | Install prefix used by `setup.sh` / `run` |
| `IMWB_GST_PREFIX` | Runtime GStreamer prefix (GeForce Now) |
| `IMWB_VIDEO_DECODER` | Force a GStreamer video decoder to MAX rank (`vah264dec`, `v4l2slh264dec`, …) |

---

## Features

- **Two render backends** — OpenGL ES 3 (default) or Vulkan, chosen at configure time; both use the same zero-copy pipeline.
- **Zero-copy rendering** — WebKit dmabuf frames become GPU textures directly (`EGLImage` in GLES, DRM-modifier `VkImage` in Vulkan). No CPU copies.
- **Present-on-demand** — idle kiosk CPU ~5 % (naps between presents; hot render spin avoided).
- **Direct kiosk path** — UI skipped entirely; web texture blitted fullscreen.
- **Gaming-grade input** — the `src/js/gfn/` bridge injects keyboard/mouse into GeForce Now and auto-unmutes the game audio, surviving WebRTC renegotiations and stuck-shutdown hangs (dead-stream + NVST watchdogs).
- **Correct pointer/media semantics** — WPE button numbers/bitmasks, DOM fullscreen ↔ kiosk, `target=_blank` routed into the single view.
- **Swedish keyboard layout** via system XKB keymap.

---

## Patches

`setup.sh` auto-applies four upstream patches (via `git apply`, idempotent) before building:

| Patch | Applied to | Why |
|---|---|---|
| `patches/wpe-webkit-bwrap-unshare-net-webrtc.patch` | WPE WebKit | Let the sandboxed web process share the host network so libnice sees a real interface (GeForce Now needs it) |
| `patches/wpe-webkit-empty-body-js-mime.patch` | WPE WebKit | Don't let the content sniffer downgrade empty-body JS responses to `text/plain` (breaks ES module boots) |
| `patches/gstreamer-webrtcbin-audio-opus-ptmap-fallback.patch` | GStreamer | Keep audio m-line/codec for a receive-only Opus transceiver |
| `patches/gstreamer-webrtcbin-balanced-to-maxbundle.patch` | GStreamer | Map unsupported balanced bundle policy to max-bundle |

The `patches/imwebbrowser-*.patch` files are historical snapshots of the GFN bridge work;
that work is already in `src/` on the current branch and is **not** re-applied by `setup.sh`.

---

## Hardware tuning (decoders by platform)

| Platform | Backend | Decoder | Media HW types |
|---|---|---|---|
| Lenovo W540 (dev PC) | GLES or Vulkan | `vah264dec` (VA-API) | `video/mp4; codecs="avc1"` |
| STM32MP257F (VPU+Mali) | Vulkan | `v4l2slh264dec` | `video/mp4; codecs="avc1"` |
| Software-only / headless | GLES | `avdec_h264` | *(empty)* |

```bash
cmake -B build -DIMWB_VIDEO_DECODER=vah264dec -DIMWB_MEDIA_HW_TYPES='video/mp4; codecs="avc1"'
```

Leave `IMWB_VIDEO_DECODER` empty to let GStreamer pick by rank (usually software `avdec_h264`).

> On the W540, Haswell only hardware-decodes **H.264** — HEVC/VP9/AV1 (used by
> YouTube/Netflix at high quality) fall back to software via `gstreamer1.0-libav`.

### DRM (Netflix)

`ENABLE_ENCRYPTED_MEDIA` is a compile-time flag. It only works functionally when the
WPE **Thunder/Widevine** CDM module is present; the default build ships with EME off,
so **Netflix won't play** until that module is installed and WebKit is rebuilt with EME on.
GeForce Now and YouTube do not depend on it.

### WebRTC backend

`ENABLE_WEBRTC` controls WebKit's runtime setting, but **which engine implements
`RTCPeerConnection` is decided when WPE WebKit is built**: `USE_GSTREAMER_WEBRTC=ON`
(the WPE default, and what this project builds against) uses GStreamer's `webrtcbin`;
`OFF` uses WPE's bundled libwebrtc. The GStreamer path needs `gstreamer1.0-nice`
(ICE/STUN/TURN) and OpenSSL ≥ 3.0.

---

## Architecture

```
SDL3 events ──► main.cpp (geometry routing) ──► browser.cpp
                                                    │ wpe_input_* events
                                                    ▼
                                            WPE WebKit (WebProcess)
                                                    │ dmabuf frames
                                                    ▼
              browser.cpp updateWebTexture()
                    │
        ┌───────────┴────────────┐
        ▼ GLES                   ▼ Vulkan
  EGLImage → GL texture   eglExportDMABUFImageMESA → VkImage import
        │                        │ (same GPU, pinned via DRM node)
        └───────────┬────────────┘
                    ▼
      SDL_GL_SwapWindow / vkQueuePresentKHR
```

- `src/main.cpp` — window/backend setup, event loop, kiosk fast path, benchmark harness.
- `src/browser.cpp/.hpp` — WPE WebKit embedding, zero-copy frame import, input → `wpe_input_*`, signals.
- `src/vk_backend.cpp/.hpp` — *(Vulkan)* swapchain + external-memory dma-buf import.
- `src/ui.cpp/.hpp` — Dear ImGui toolbar: URL bar, nav, progress, stats.
- `src/js/gfn/gfn_input_bridge.js` — injected GFN bridge (input, auto-unmute, watchdogs).

### Vulkan/input lessons (brief)

- **Same-GPU pinning**: WPE's EGL display matches the Vulkan dev node by DRM render-node minor (`VK_EXT_physical_device_drm`) so cross-vendor tiled imports don't misrender.
- **Handle type**: import dma-buf fds as `VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT`, not `OPAQUE_FD`.
- **Input**: WPE wants plain button integers (1/2/3) + a pressed-bitmask in `state`, not `BTN_*` codes. A click overlapping a page load can lose its button-up; the embedder re-arms on every `load-changed`.

## Interactive documentation

Open **[`index.html`](index.html)** — an animated HTML5/WebGL reference manual with a
draggable 3-D render-pipeline model, tooltips on every public function and per-module dives.
