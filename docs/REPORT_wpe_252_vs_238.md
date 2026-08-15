# WPE WebKit 2.38.6 vs 2.52.5 — WebGL/ANGLE comparison (ImWebBrowser)

Machine: i7-4700MQ (4C/8T), Intel HD 4600 (crocus), Mesa 25.2.8, X11 (:0).
App: ImWebBrowser 1.0.0 (WPE, EGL-image exportable, 1280x800), same binary rebuilt per
WebKit ABI (2.52 links libWPEWebKit-2.0, 2.38 links libWPEWebKit-1.1). Both WebKit builds
from pristine upstream sources; identical pages and window size.

## 1. WebGL support

| capability          | WPE WebKit 2.38.6 | WPE WebKit 2.52.5 |
|---------------------|-------------------|-------------------|
| getContext('webgl') | yes               | yes               |
| getContext('webgl2')| no (null)         | yes               |
| GLSL ES             | 1.00              | 3.00              |
| renderer string     | "Apple GPU" (ANGLE)| "Apple GPU" (ANGLE)|

WebGL2 is genuinely unavailable in 2.38.6 (context creation returns null); it works in
2.52.5 (WebGL 2.0, GLSL ES 3.00, FRAMEBUFFER_COMPLETE, no GL errors). Both run through
ANGLE (WebKit reports ANGLE's canned renderer string on both).

## 2. Rendering correctness (readPixels-verified triangle)

Red/green/blue triangle on dark background, 256x256 canvas, gl.drawArrays, readPixels:

- 2.38.6 WebGL1: drawErr=0, center=(63,64,128), background=(25,25,25)  -> correct
- 2.52.5 WebGL1: identical pixel values                                       -> correct
- 2.52.5 WebGL2: identical pixel values                                       -> correct
- 2.38.6 WebGL2: no context (page that requests webgl2 also segfaults the web
  process during teardown on 2.38).

WebGL1 output is pixel-identical between the two versions.

## 3. Performance (benchx, 1024x1024 offscreen FBO, pixel-verified)

Instanced draw = 6-vertex quads with per-instance mat4 (native WebGL2 on gl2,
ANGLE_instanced_arrays on gl1); OBJ path = per-object uniformMatrix4fv + drawArrays.

| test      | 2.38.6 gl1 | 2.52.5 gl1 | 2.52.5 gl2 |
|-----------|-----------:|-----------:|-----------:|
| INST1000  | 0.00ms px0 | 0.90ms px15286 | 0.37ms px15286 |
| OBJ1000   | 3.90ms px15286 | 3.10ms px15286 | 2.60ms px15286 |
| INST5000  | 0.47ms px0 | 1.53ms px15300 | 1.70ms px15300 |

px = non-background pixels read back after the timed loop.

Key findings:
- 2.38.6's mat4-instanced path (ANGLE_instanced_arrays + 4x vec4 instance attributes,
  64-byte stride) renders NOTHING (px=0) but reports no GL error. Its "fast" numbers are
  the cost of no-ops. A minimal vec2-instance test does render on 2.38 (instchk: px=10608),
  so this is a 2.38-era ANGLE bug specific to multi-attribute/mat4 instancing.
- 2.52.5 instancing renders correctly and is fast (0.4-1.7ms). The object-draw path is
  comparable or slightly faster on 2.52 (2.6-3.1ms vs 3.9ms on 2.38).

Caveat: an earlier, non-verified run (bench2026.html) showed a large first-call penalty on
2.52 (INST5000 ~96ms) caused by cold-path compilation during the timed loop; steady-state
numbers after warmup are those above. 2.38's earlier "fast" instancing results were the
broken/no-op path.

## 4. EGL/display notes (debugging record)

- App creates a surfaceless EGL display and passes it to wpebackend-fdo
  (wpe_fdo_initialize_for_egl_display).
- wpebackend-fdo returns eglPlatform=0, so WebKit falls back to
  eglGetDisplay(nativeDisplay), where nativeDisplay is the app's EGLDisplay handle
  reinterpreted as a pointer. Mesa cannot autodetect it and falls back to the build-time
  default platform (X11/DRI3).
- The display exposes EGL_KHR_no_config_context + EGL_MESA_configless_context, so ANGLE
  creates its native contexts with EGL_NO_CONFIG_KHR.
- WebGL1 contexts on 2.52 share with WebKit's ES2 "sharing" GL context via ANGLE external
  contexts; this works on the browser's display.
- Sharing with an ES3 sharing context (the experiment below) fails with EGL_BAD_CONTEXT on
  this display. Root cause was never fully isolated (standalone X11 reproductions behaved
  differently, returning EGL_BAD_MATCH, so the browser display is not equivalent to a plain
  X11 platform display). Moot after reverting the experiment.

## 5. The "ES3 patch" episode (what was tried and why it was reverted)

A source patch was applied to WPE WebKit 2.52.5 to force ES3 support on the legacy
exportable path:
- GLContext.{h,cpp}: clientVersion param, getEGLConfig -> EGL_OPENGL_ES3_BIT
- PlatformDisplayANGLE.cpp: angleSharingGLContext -> createSharing(*this, 3) (ES3 share)
- GraphicsContextGLTextureMapperANGLE.cpp: config allows ES2|ES3 bits

Outcome: it broke BOTH WebGL1 and WebGL2 (web process log: "Failed to create a shared
renderer: eglCreateContext failed", EGL_BAD_CONTEXT x8).

Why it was wrong: the premise "WebGL2 is null on 2.52" came from w2err.html, which called
getContext('webgl') then getContext('webgl2') on the SAME canvas. Per spec, requesting a
different context type on a canvas that already has a context returns null. With separate
canvases (w2sep.html), pristine 2.52.5 gives webgl2=yes. The patch fixed a problem that
did not exist, and it was reverted (sources restored from the pristine tarball, rebuilt,
reinstalled). Patched copies kept in /tmp/opencode/patched-backup/ for reference.

## 6. Reproducing

Test pages used are committed under docs/webgl-tests/ in this repo:
- w2sep.html — WebGL1/WebGL2 support probe (separate canvases; a shared canvas is
  invalid because switching context types on one canvas returns null).
- tri_gl2.html — triangle render test with readPixels verification
  (#gl1 / #gl2 via URL hash).
- benchx.html — pixel-verified instanced vs object-draw benchmark (#gl1 / #gl2).
- instchk.html — minimal ANGLE_instanced_arrays render check (2.38 works here but
  fails with mat4 instance attributes — see section 3).

Run: ImWebBrowser --url file:///path/to/page.html (read results from the window title).

## 7. Final state

- /usr: pristine WPE WebKit 2.52.5 (rebuilt from reverted sources).
- App source: one compat guard added in src/webkit/web_settings.cpp
  (WEBKIT_CHECK_VERSION(2,45,3) around webkit_settings_set_enable_2d_canvas_acceleration,
  a 2.45.3+ API) so the app builds against both 1.1 and 2.0 pc files.
- Conclusion: no patch needed. Use 2.52.5 for WebGL2. 2.38.6 has no WebGL2 and its
  mat4-instancing silently fails to render.

## 8. Aquarium 5000-fish performance (2.52.5, i7-4700MQ / HD 4600)

WebGL Aquarium (webglsamples.org), 1280x800 window, 1024x1024 canvas by default.
FPS = the aquarium's own 16-frame rAF average, read from the window title (local mirror
with a title patch; mirror kept at /tmp/opencode/aqua/webglsamples.org, served via
`python3 -m http.server 8000`).

| configuration (numFish=5000)          | steady fps |
|---------------------------------------|-----------:|
| default (MSAA on)                     | 21         |
| + webgl={antialias:false}             | 30-33      |
| + antialias:false, 512x512 canvas     | 36         |
| + antialias:false, 800x450 canvas     | 34         |
| JS-only (GL draws removed, MSAA off)  | 41         |

Fish-count scaling (antialias:false): 1 fish = 41, 100 = 47, 5000 = 33 fps.

Findings:
- WebGL canvas contexts are created with WebKit's default antialias:true; disabling MSAA
  (URL `webgl={antialias:false}`) is worth ~10 fps and is the biggest single lever.
- The bottleneck is the single-threaded web process, CPU-bound at 64-88% of one core:
  per-fish JS update (sin/cos matrix math) plus ANGLE per-draw-call overhead for 5000
  separate draws. Even with all GL draws removed the JS loop caps at ~41 fps, so ~50 fps
  at 5000 fish is not reachable on this machine without rewriting the demo renderer
  (e.g., WebGL2 instancing into few draw calls).
- The fixed scene cost (~45 fps ceiling even with 1 fish) is DOM/GL per-frame work in the
  aquarium's own loop, not an app/WPE pipeline cap (a pure rAF page runs at 62 fps).

Counter-proof that the app/browser pipeline is not the bottleneck: a WebGL2 instanced
renderer drawing 5000 animated fish (one drawArraysInstanced call, per-instance vertex
shader math) runs at a solid 60 fps with the web process at only ~25% CPU
(docs/webgl-tests/instfish.html). The aquarium's ~33 fps is its 2009-era renderer design
(5000 separate draw calls + per-fish JS on one thread), not the SDL3/ImGui/GLES app stack
or WPE itself.

## 9. WebGL2 instanced aquarium port (aquarium-gl2.html)

A from-scratch WebGL2 (GLSL ES 3.00) rewrite of the aquarium that renders the full
scene (skybox cubemap, 89 placed props, normal-mapped rocks/chest/seaweed with vertex
sway, 240 instanced bubbles, translucent tank glass) and 5000 fish as **five instanced
draws** (per-instance position + orientation basis + scale + tail-phase uploaded in one
Float32Array; fish meshes position-deduplicated, e.g. SmallFishA 138 -> 34 unique
vertices). Renderer fragment is ambient+Lambert+fog; depth-tested, no MSAA.

Committed as `docs/webgl-tests/aquarium/` (page + full asset set; serve the directory,
open `aquarium-gl2.html`). Tunables via URL params: `fish`, `speed`, `instscale`,
`fscale`, `tris`, `frag`, `vs`, `res`. FPS + per-frame JS ms in the window title.

Measured on the same machine (i7-4700MQ / HD 4600 / Mesa 25.2.8 / 1280x800 window,
cold-soaked between runs to avoid the ~85C package thermal throttle):

| configuration (full scene)          | steady fps |
|-------------------------------------|-----------:|
| 0 fish (scene only)                 | 60 (capped)|
| 2500 fish                           | 54-59      |
| 5000 fish                           | 45-52      |
| 5000 fish, 960x583 backing          | ~45        |
| bare instfish (5000, no scene)      | 60         |

Findings / isolation:
- The scene alone saturates the 60fps frame budget on HD 4600 (vsync-capped at
  fish->0); the fish pass adds ~4-6ms, landing the full scene at ~21ms/frame.
- Isolation experiments (each measured cold): fragment shader stripped to a flat color
  (no texture/lighting/fog) - no change; vertex shader reduced to a passthrough (no
  tail bend / rotation / fog) - no change; instance upload rate halved (every-other
  frame) - no change; backing resolution cut 44% (960x583) - no change. The cost is
  raw instance/vertex throughput of the Gen7.5 GPU (approx. 700k vertex shader
  invocations for the instanced indexed fish), not pixel/fill, shader ALU, or upload.
- Instance buffer layout is fetch-sensitive on this driver: 11 floats/instance (44B
  stride) is fastest; an 8-float layout that moved `vx` reconstruction into the shader
  regressed to ~33fps and was reverted.
- Conclusion: ~45-52fps is the practical ceiling for the full scene + 5000 fish at
  native 1280x800 on the HD 4600. The same page at fish=2500 reaches ~56fps, and the
  bare instanced-fish benchmark reaches 60fps - confirming the app/WPE/GLES stack is
  not the bottleneck. The 2009-era original aquarium's 21-33fps is its per-fish
  draw-call design.
