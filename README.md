<p align="center">
  <img alt="ImWebBrowser" src="https://img.shields.io/badge/engine-WPE%20WebKit%202.53-blue" />
  <img alt="SDL3" src="https://img.shields.io/badge/UI-SDL3%20%2B%20Dear%20ImGui-green" />
  <img alt="rendering" src="https://img.shields.io/badge/rendering-zero--copy%20DMA--buf-orange" />
  <img alt="license" src="https://img.shields.io/badge/platform-Linux-lightgrey" />
</p>

# ImWebBrowser

A lightweight kiosk-grade web browser built on **SDL3 + Dear ImGui + WPE WebKit**
with **zero-copy DMA-buf rendering**. Frames produced by WebKit's compositor are
imported directly as `EGLImage` textures and blitted to the screen — no CPU
copies, no intermediate buffers.

<p align="center">
  <img alt="ImWebBrowser demo" src="docs/demo.gif" width="700">
</p>

| Benchmark (5000 fish, fullscreen 1080p) | FPS |
|---|---|
| Cog (reference WPE launcher) | ~43 |
| **ImWebBrowser kiosk** | **60 (vsync cap)** |

Idle kiosk CPU on a static page: **~5 %** (present-on-demand loop with a 2 ms
idle nap instead of a hot render spin).

## Features

- **Zero-copy rendering** — WebKit dmabuf frames become GL textures directly
  (`frame path: dmabuf/EGLImage (zero-copy)`), shared-memory fallback included.
- **Present-on-demand** — the render loop only presents when a new web frame,
  input, stats or a 150 ms heartbeat requires it; between presents it naps,
  so idle CPU stays near zero without delaying frames or input.
- **Deferred buffer retirement** — the buffer still bound to the texture is
  never recycled mid-present (`retireImage_` pipeline); eliminates striped
  corruption on fast-repainting pages and gives WebKit a deeper buffer queue.
- **Direct kiosk path** — in kiosk mode ImGui is skipped entirely and a minimal
  attribute-less fullscreen triangle blits the web texture.
- **Kiosk-safe lifecycle** — spurious WM close-requests are ignored in kiosk
  mode; DOM fullscreen enter/exit restores the launch mode.
- **Smart URL bar** — typing `kernel.org`, `/path/file.html` or plain search
  text does the right thing; non-URLs go to DuckDuckGo.
- **Correct pointer semantics** — WPE button numbers (1=left/2=right/3=middle)
  and held-button bitmasks, matching Cog's input translator.
- Swedish keyboard layout support via system XKB keymap.

## Dependencies

CMake resolves everything through `pkg-config`; the *pkg-config name* column is
the canonical requirement. Versions listed are what the project is developed
and tested against.

### Build-time

| Dependency | pkg-config name | Tested version | Why |
|---|---|---|---|
| [WPE WebKit](https://wpewebkit.org) | `wpe-webkit-2.0` | **≥ 2.53.90** | The browser engine (`WPEBrowser::WebView`) |
| [WPE Backend FDO](https://github.com/WebPlatformForEmbedded/WPEBackend-fdo) | `wpebackend-fdo-1.0` | 1.15.90 | EGLImage/dmabuf frame export protocol |
| [libwpe](https://github.com/WebPlatformForEmbedded/libwpe) | `wpe-1.0` | 1.16.3 | Generic WPE backend API |
| [SDL3](https://github.com/libsdl-org/SDL) | `sdl3` | 3.4.14 (any ≥ 3.2) | Windowing, input, GL context |
| libxkbcommon | `xkbcommon` | 1.6.0 | Keyboard mapping (Swedish layout etc.) |
| wayland-server | `wayland-server` | 1.22.0 | Shared-memory frame fallback path |
| EGL | `egl` | 1.5 | Headless EGL display for WebKit + app |
| OpenGL ES | `glesv2` | 3.2 | Rendering (kernels blit + ImGui GLES3) |
| GLib | *(via WPE WebKit)* | 2.x | Main-loop integration |
| [Dear ImGui](https://github.com/ocornut/imgui) | *vendored* | 1.9x | Toolbar/UI — included under `src/libraries/imgui` |

Toolchain: **CMake ≥ 3.22**, **pkg-config**, and a **C++20** compiler
(GCC ≥ 11 or Clang ≥ 14).

Debian/Ubuntu package names (where available):

```bash
sudo apt install cmake pkg-config g++ \
    libwpe-1.0-dev libwpebackend-fdo-1.0-dev libxkbcommon-dev \
    libwayland-dev libegl-dev libgles2-mesa-dev libglib2.0-dev
```

> **SDL3** is not yet packaged for most distros — build it from source
> (`cmake -B build && cmake --build build && sudo cmake --install build`).
>
> **WPE WebKit ≥ 2.53.90** is required for the FDO bridge API this embedder
> uses; if your distro ships an older release, build it per the
> [WPE WebKit instructions](https://wpewebkit.org/release/) (or use the
> prebuilt packages linked there).

### Run-time

| Component | Packages (Debian/Ubuntu names) | Needed for |
|---|---|---|
| GStreamer core + base | `gstreamer1.0-tools gstreamer1.0-plugins-base` | Media framework WebKit links against |
| GStreamer good/bad | `gstreamer1.0-plugins-good gstreamer1.0-plugins-bad` | Most audio/video codecs, v4l2, http srcs |
| GStreamer ugly / libav | `gstreamer1.0-plugins-ugly gstreamer1.0-libav` | H.264, MP3 and other common codecs |
| GPU driver with GLES 3 | `mesa-utils` (Mesa: `libgl1-mesa-dri`) | Zero-copy dmabuf import & blit |
| Fonts | `fonts-dejavu-core fonts-liberation` | Page text rendering |
| D-Bus accessibility (optional) | `at-spi2-core` | Silences WebKit a11y-bus warnings |

Without GStreamer plugins pages still render, but `<audio>`/`<video>` will not
play. Without a working GLES 3 driver the browser falls back to CPU-uploaded
shared-memory frames (slower but functional).

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## CMake compile flags

All engine capabilities map 1:1 onto WebKit settings and can be toggled at
configure time. Defaults produce a full-featured browser.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_WEBGL=ON -DENABLE_MEDIA=OFF ...
```

| Option | Default | Controls |
|---|---|---|
| `ENABLE_JAVASCRIPT` | ON | JavaScript execution |
| `ENABLE_WEBGL` | ON | WebGL 3D content |
| `ENABLE_SMOOTH_SCROLLING` | ON | Animated smooth scrolling |
| `ENABLE_PAGE_CACHE` | ON | Back/forward page cache |
| `ENABLE_DNS_PREFETCH` | ON | Speculative DNS resolution |
| `ENABLE_2D_CANVAS` | ON | Accelerated 2D canvas |
| `ENABLE_CARET_BROWSING` | OFF | Moveable text caret without a pointer |
| `ENABLE_SPATIAL_NAVIGATION` | OFF | Arrow-key spatial element navigation |
| `ENABLE_TAB_FOCUS_CYCLE` | ON | Tab key cycles focusable elements |
| `ENABLE_TEXT_AREAS_RESIZE` | ON | Resizable `<textarea>` handles |
| `ENABLE_BF_GESTURES` | OFF | Back/forward swipe gestures |
| `ENABLE_WEBRTC` | ON | WebRTC and getUserMedia |
| `ENABLE_MEDIA` | ON | HTML5 audio/video playback (GStreamer) |
| `ENABLE_AUDIO` | ON | WebAudio API |
| `ENABLE_AUTOPLAY` | OFF | Autoplay media without user gesture |
| `ENABLE_MEDIA_CAPABILITIES` | ON | `navigator.mediaCapabilities` |
| `ENABLE_ENCRYPTED_MEDIA` | ON | Encrypted Media Extensions (DRM) |
| `ENABLE_GSTREAMER` | ON | Require and verify the GStreamer stack |
| `ENABLE_DEVELOPER_EXTRAS` | OFF | WebKit Web Inspector (context-menu) |
| `ENABLE_BENCHMARK_HARNESS` | OFF | Automated WebGL Aquarium benchmark CLI |

`CMAKE_BUILD_TYPE=Release` is strongly recommended; debug builds render the
same pages several times slower.

## Running

```bash
./build/imwebbrowser [URL] [--kiosk] [--bench-fish N]
```

| Argument | Meaning |
|---|---|
| `URL` | Start page (http(s), file://). Defaults to `https://duckduckgo.com`. |
| `--kiosk` | Fullscreen direct-blit mode without toolbar/UI. |
| `--bench-fish N` | Auto-start the WebGL Aquarium benchmark with N fish. |

## Keyboard

| Key | Action |
|---|---|
| `Ctrl+L` | Focus URL bar |
| `Enter` (URL bar) | Navigate / search |
| `Ctrl+R` | Reload · `Ctrl+Shift+R` bypass cache |
| `Alt+Left` / `Alt+Right` | History back / forward |
| `F11` | Toggle kiosk mode |
| `F3` | Stats overlay |
| `Ctrl+W` / `Ctrl+Q` | Quit |

## Environment variables

| Variable | Effect |
|---|---|
| `IMWB_WINDOW_SIZE=WxH` | Force initial window size |
| `IMWB_NOVSYNC` | Disable vsync (benchmarking, tearing possible) |
| `IMWB_STATS` | Per-second `[stats]` line: fps, exports/s, present counts |
| `IMWB_DEBUG_INPUT` | Verbose input logging: every forwarded button/motion/key event |
| `IMWB_DUMP=1` | One-shot framebuffer dump to `/tmp/opencode/fb-dump.ppm` |

## Architecture

```
SDL3 events ──► main.cpp (routing by geometry) ──► browser.cpp
                                                    │ wpe_input_* events
                                                    ▼
                                            WPE WebKit (WebProcess)
                                                    │ dmabuf frames
                                                    ▼
              browser.cpp updateWebTexture() ── EGLImage ──► GL texture
                                                    ▼
        kiosk: attribute-less blit triangle   or   ImGui UI + textured view
                                                    ▼
                                          SDL_GL_SwapWindow → afterPresent()
```

- `src/main.cpp` — window/GL setup, event loop, geometry-routed input, kiosk
  fast path, benchmark harness.
- `src/browser.cpp/.hpp` — WPE WebKit embedding, zero-copy frame import, input
  translation to `wpe_input_*`, navigation and load signals.
- `src/ui.cpp/.hpp` — Dear ImGui toolbar: URL bar, back/forward/reload,
  progress overlay, stats.

### Input notes (hard-won lessons)

WPE expects **plain button integers** (1 = left, 2 = right, 3 = middle) plus a
**bitmask of currently pressed buttons** in `state` (`1<<20` left, `1<<21`
right, `1<<22` middle) — *not* Linux `BTN_*` keycodes and not a 0/1 flag.
Motion events must carry the same bitmask so WebKit can track drags.

A click that overlaps a page load can lose its button-up inside the old page;
the embedder re-arms via `markPressHeal()` on every `load-changed` and sends a
synthetic release before the next press.

## Interactive documentation

Open **[`index.html`](index.html)** in any browser — an animated HTML5/WebGL
reference manual with a draggable 3-D render-pipeline model, tooltips on every
public function, live charts and per-module deep dives.
