# ImWebBrowser

A lightweight kiosk-grade web browser built on **SDL3 + Dear ImGui + WPE WebKit**
with **zero-copy DMA-buf rendering**. Frames produced by WebKit's compositor are
imported directly as `EGLImage` textures and blitted to the screen — no CPU
copies, no intermediate buffers.

| Benchmark (5000 fish, fullscreen 1080p) | FPS |
|---|---|
| Cog (reference WPE launcher) | ~43 |
| **ImWebBrowser kiosk** | **60 (vsync cap)** |

## Features

- **Zero-copy rendering** — WebKit dmabuf frames become GL textures directly
  (`frame path: dmabuf/EGLImage (zero-copy)`), shared-memory fallback included.
- **Present-on-demand** — the render loop only presents when a new web frame,
  input, stats or a 150 ms heartbeat requires it; idle CPU use is near zero.
- **Deferred buffer retirement** — the buffer still bound to the texture is
  never recycled mid-present (`retireImage_` pipeline); eliminates striped
  corruption on fast-repainting pages and gives WebKit a deeper buffer queue.
- **Direct kiosk path** — in kiosk mode ImGui is skipped entirely and a minimal
  attribute-less fullscreen triangle blits the web texture.
- **Smart URL bar** — typing `kernel.org`, `/path/file.html` or plain search
  text does the right thing; non-URLs go to DuckDuckGo.
- **Correct pointer semantics** — WPE button numbers (1=left/2=right/3=middle)
  and held-button bitmasks, matching Cog's input translator.
- Swedish keyboard layout support via system XKB keymap.

## Building

```bash
sudo apt install libsdl3-dev libwebkit2gtk-4.1-dev libwpe-1.0-dev \
                 libwpebackend-fdo-1.0-dev libxkbcommon-dev libglib2.0-dev \
                 libegl-dev libgles2-mesa-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Dear ImGui is vendored under `src/libraries/imgui`.

## Running

```bash
./build/imwebbrowser [URL] [--kiosk] [--bench-fish N]
```

| Argument | Meaning |
|---|---|
| `URL` | Start page (http(s), file://). Defaults to the built-in home page. |
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

Open `index.html` in any browser — an animated HTML5/WebGL reference with
tooltips for every public function, live performance charts and an explorable
3-D render-pipeline model.
