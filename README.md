# ImWebBrowser

A minimal, GPU-accelerated web browser built on
[WPE WebKit](https://wpewebkit.org/), [Dear ImGui](https://github.com/ocornut/imgui)
and [SDL3](https://github.com/libsdl-org/SDL). The page is composited by the WPE
web process into native dmabuf/EGL buffers that the host imports and renders
through a Vulkan or OpenGL ES backend, so the rendering stays on the GPU for the
whole pipeline.

The UI chrome (back / forward / reload / kiosk, address bar and a fixed-width
loading-progress indicator) is drawn with Dear ImGui on top of the web-view
texture. The OS window title mirrors the live page title.

## Demo

![ImWebBrowser running the GeForce NOW page](assets/demo-nvidia.gif)

The web process composites the page into dmabuf/EGL buffers that the host renders
through OpenGL ES, here showing the dark GeForce NOW marketing page with the
toolbar (Back / Fwd / Reload / Kiosk, address bar, loading-progress).

## Status

Tested on a Lenovo ThinkPad W540 (Intel HD 4600, crocus) running Linux Mint 22.3
with Mesa 25.2.8 and WPE WebKit 2.38. Real content renders and is interactive on:

- YouTube
- GeForce Now (`play.geforcenow.com` and the `nvidia.com` marketing pages)
- Netflix
- Google Docs

The keyboard layout follows the system XKB keymap (Swedish on the test machine).

## Build

> Build with a single job on constrained machines:
>
>     cmake --build build --parallel 1
>
> Parallel builds (`-j2` and above) can OOM / wedge low-RAM machines.

### Build dependencies (Debian/Ubuntu)

    sudo apt install build-essential cmake pkg-config libsdl3-dev \
        libwpe-1.0-dev libwpebackend-fdo-1.0-dev libwpewebkit-1.1-dev \
        libglib2.0-dev libegl-dev libgles2-dev libwayland-dev

For the optional Vulkan backend: `libvulkan-dev`.

### Configure and build

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel 1

### CMake options

| Option | Default | Effect |
| ------ | ------- | ------ |
| `IMWEBBROWSER_BACKEND_VULKAN` | `OFF` | Render the web view through Vulkan |
| `IMWEBBROWSER_BACKEND_OPENGL_ES` | `ON` | Render the web view through OpenGL ES |
| `IMWEBBROWSER_ENABLE_JAVASCRIPT` | `ON` | Enable JavaScript in web pages |
| `IMWEBBROWSER_ENABLE_WEBGL` | `ON` | Enable WebGL |
| `IMWEBBROWSER_ENABLE_WEBRTC` | `ON` | Enable WebRTC |
| `IMWEBBROWSER_ENABLE_MEDIA` | `ON` | Enable HTML audio/video playback |
| `IMWEBBROWSER_ENABLE_MEDIA_STREAM` | `ON` | Enable `getUserMedia` capture |
| `IMWEBBROWSER_ENABLE_WEB_AUDIO` | `ON` | Enable Web Audio |
| `IMWEBBROWSER_ENABLE_FULLSCREEN` | `ON` | Allow web pages to request fullscreen |
| `IMWEBBROWSER_ENABLE_DEVELOPER_EXTRAS` | `OFF` | Enable the Web Inspector |
| `IMWEBBROWSER_ENABLE_SANDBOX` | `ON` | Enable the WebKit sandbox |
| `IMWEBBROWSER_ENABLE_SMOOTH_SCROLLING` | `ON` | Prefer smooth pixel axis scrolling |
| `IMWEBBROWSER_TRACE_LOGGING` | `OFF` | Compile with trace-level logging |
| `IMWEBBROWSER_INSTALL` | `ON` | Install the `imwebbrowser` binary |

## Run

    ./build/imwebbrowser [options]

| Option | Description |
| ------ | ----------- |
| `-u, --url=<url>` | URL to open on startup |
| `--width=<px>` / `--height=<px>` | Initial window size |
| `--fullscreen` | Start fullscreen |
| `--user-agent=<str>` | Custom user agent string |
| `--no-javascript` | Disable JavaScript |
| `--no-webgl` | Disable WebGL |
| `--no-webrtc` | Disable WebRTC |
| `--no-media` | Disable HTML audio/video |
| `--no-media-stream` | Disable `getUserMedia` capture |
| `--no-web-audio` | Disable Web Audio |
| `--no-fullscreen-api` | Disable the Fullscreen API |
| `--developer-extras` | Enable the Web Inspector |
| `--no-sandbox` | Disable the WebKit sandbox |
| `--no-smooth-scrolling` | Use discrete (not smooth) wheel scrolling |
| `--log-level=<lvl>` | `trace` \| `debug` \| `info` \| `warn` \| `error` |

### Keyboard shortcuts

| Shortcut | Action |
| -------- | ------ |
| `F11` or the **Kiosk** button | Toggle kiosk (fullscreen, chrome-less) mode |
| `Esc` | Leave kiosk mode |
| `Ctrl+L` | Focus the address bar |
| `Ctrl+R` / `F5` | Reload |
| `Alt+Left` / `Alt+Right` | Back / forward |

## Source layout

```
src/
  main.cpp              Entry point + CLI
  application/          SDL3 window, renderer ownership, frame loop, shortcuts
  browser/              Browser state; reports the web-view viewport each frame
  config/               Runtime config + CLI parser
  input/                SDL3 -> WPE input translation (evdev keymap)
  logging/              Tiny leveled logger
  platform/             SDL3 window wrapper (fullscreen, DPI, drawable size)
  rendering/
    renderer.h          Backend-neutral frame sink / EGL display interface
    opengles/           OpenGL ES renderer; imports the WPE EGL image
    vulkan/             Vulkan renderer (optional, off by default)
    egl_helpers.*       EGL boilerplate shared by the backends
  ui/                   Dear ImGui toolbar + web-view quad
  webkit/               WebKitWebView + WPE FDO exportable; load/progress signals
  libraries/imgui/      Dear ImGui (vendored)
cmake/
  config_defaults.h.in  Template for the compile-time defaults header
```

## Renderer notes

The web process of WPE WebKit composites the page into a `wl_surface` and
exports each frame as a dmabuf (or, on the software path, a shared-memory
buffer). The host side of WPEBackend-fdo hands that buffer to the application as
an `EGLImage`, which the OpenGL ES backend attaches to a scratch texture, marks
complete (`BASE_LEVEL` / `MAX_LEVEL` / `LINEAR` filtering), copies into an
owned web-view texture, and draws as a full-viewport quad with ImGui.

The compositor drives frame pacing: the application must call
`wpe_view_backend_exportable_fdo_dispatch_frame_complete()` after releasing an
exported buffer, otherwise the web process blocks in `eglSwapBuffers` after the
first frame.

## License

See `LICENSE`.