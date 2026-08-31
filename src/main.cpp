#include "browser.hpp"
#include "config.h"
#include "ui.hpp"

#include <SDL3/SDL.h>
#include <EGL/egl.h>
#include <signal.h>
#ifdef IMWB_BACKEND_VULKAN
#include <backends/imgui_impl_vulkan.h>
#else
#include <backends/imgui_impl_opengl3.h>
#endif  // IMWB_BACKEND_VULKAN
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

#include <sys/stat.h>
#include <sys/sysmacros.h>  // major/minor
#include <cstdio>
#include <string>
#include <cstdlib>
#include <vector>
#include <unistd.h>
#include <cstring>

// Captured once at startup; forwarded input events run per frame.
static const bool kDebugInput = g_getenv("IMWB_DEBUG_INPUT") != nullptr;

namespace {

struct Args {
    const char* url = "https://duckduckgo.com";
    bool kiosk = false;
#if ENABLE_BENCHMARK_HARNESS
    int benchFish = 0;
#endif  // ENABLE_BENCHMARK_HARNESS
};

Args parseArgs(int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--kiosk") == 0)
            a.kiosk = true;
#if ENABLE_BENCHMARK_HARNESS
        else if (strcmp(argv[i], "--bench-fish") == 0 && i + 1 < argc)
            a.benchFish = atoi(argv[++i]);
#endif  // ENABLE_BENCHMARK_HARNESS
        else if (argv[i][0] != '-')
            a.url = argv[i];
    }
#if ENABLE_BENCHMARK_HARNESS
    // Make the fish count foolproof: --bench-fish N wins over whatever is in
    // the URL (guards against the common 'fishNum' typo, which the page
    // silently ignores and then runs its default count).
    if (a.benchFish > 0) {
        static std::string url;
        url = a.url;
        const std::string wrong = "fishNum=";
        if (url.find(wrong) != std::string::npos)
            std::fprintf(stderr, "[bench] note: ignoring '%s' in URL (correct param is 'numFish=')\n",
                         wrong.c_str());
        const std::string right = "numFish=" + std::to_string(a.benchFish);
        size_t pos = url.find("numFish=");
        if (pos == std::string::npos) {
            url += (url.find('?') == std::string::npos ? '?' : '&') + right;
        } else {
            size_t end = url.find_first_of("&", pos);
            url = url.substr(0, pos) + right + (end == std::string::npos ? "" : url.substr(end));
        }
        a.url = url.c_str();
    }
#endif  // ENABLE_BENCHMARK_HARNESS
    return a;
}

}  // namespace

int main(int argc, char** argv)
{
    const Args args = parseArgs(argc, argv);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "error: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Force EGL: WPE FDO shares our EGLDisplay for zero-copy frame export.
    SDL_SetHint(SDL_HINT_VIDEO_FORCE_EGL, "1");

#ifndef IMWB_BACKEND_VULKAN
    // OpenGL ES 3 context: shared by ImGui and the WebKit frame texture.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
#endif  // !IMWB_BACKEND_VULKAN

    int winW = 1280, winH = 800;
    if (const char* size = g_getenv("IMWB_WINDOW_SIZE")) {  // "WxH", for fair benchmarks
        if (sscanf(size, "%dx%d", &winW, &winH) == 2 && winW > 0 && winH > 0) {
            // fallthrough with parsed values
        } else {
            std::fprintf(stderr, "warning: bad IMWB_WINDOW_SIZE '%s', using 1280x800\n", size);
            winW = 1280;
            winH = 800;
        }
    }
#ifdef IMWB_BACKEND_VULKAN
    SDL_Window* window = SDL_CreateWindow("ImWebBrowser", winW, winH,
                                          SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#else
    SDL_Window* window = SDL_CreateWindow("ImWebBrowser", winW, winH,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#endif  // IMWB_BACKEND_VULKAN
    if (!window) {
        std::fprintf(stderr, "error: window creation failed: %s\n", SDL_GetError());
        return 1;
    }
#ifdef IMWB_BACKEND_VULKAN
    VulkanPresent vp;
    if (!vp.init(window, winW, winH))
        return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo ii{};
    auto h = vp.handles();
    ii.ApiVersion = VK_API_VERSION_1_1;
    ii.Instance = h.instance;
    ii.PhysicalDevice = h.physical;
    ii.Device = h.device;
    ii.QueueFamily = h.queueFamily;
    ii.Queue = h.queue;
    ii.DescriptorPoolSize = 16;  // backend builds its own pool
    ii.MinImageCount = 2;
    ii.ImageCount = h.imageCount;
    ii.PipelineInfoMain.RenderPass = (VkRenderPass)vp.renderPass();
    ImGui_ImplVulkan_Init(&ii);
#else
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl || !SDL_GL_MakeCurrent(window, gl)) {
        std::fprintf(stderr, "error: GL context creation failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(g_getenv("IMWB_NOVSYNC") ? 0 : 1);  // vsync unless disabled for testing

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 300 es");
#endif  // IMWB_BACKEND_VULKAN

    // Minimal attribute-less fullscreen blit for the kiosk direct path: the
    // exported web frame goes straight to the window surface, no ImGui.
    // (OpenGL ES build only; the Vulkan build draws the frame through the
    // ImGui background draw list in both modes.)
#ifndef IMWB_BACKEND_VULKAN
    GLuint blitProg = 0, blitTexLoc = -1;
    auto ensureBlitProgram = [&] {
        if (blitProg)
            return;
        const char* vs = "#version 300 es\n"
                         "out vec2 vUV;"
                         "void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
                         "vUV=vec2(p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
        const char* fs = "#version 300 es\n"
                         "precision mediump float;in vec2 vUV;uniform sampler2D uTex;out vec4 o;"
                         "void main(){o=texture(uTex,vUV);}";
        auto compile = [](GLenum type, const char* src) {
            GLuint sh = glCreateShader(type);
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);
            GLint ok = GL_FALSE;
            glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[1024];
                glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
                std::fprintf(stderr, "error: blit shader (%s) compile failed: %s\n",
                             type == GL_VERTEX_SHADER ? "vs" : "fs", log);
            }
            return sh;
        };
        GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        blitProg = glCreateProgram();
        glAttachShader(blitProg, v);
        glAttachShader(blitProg, f);
        glLinkProgram(blitProg);
        glDeleteShader(v);
        glDeleteShader(f);
        blitTexLoc = glGetUniformLocation(blitProg, "uTex");
    };
#endif  // !IMWB_BACKEND_VULKAN

    Browser browser;

    int pixW = 1280, pixH = 800;
    bool kiosk = args.kiosk;
    const bool kioskBase = args.kiosk;  // deployment mode: WM can never close us
    int toolbarPx = 0;
    auto applyLayout = [&] {
        int newW = 0, newH = 0;
        SDL_GetWindowSizeInPixels(window, &newW, &newH);
        if (newW == pixW && newH == pixH)
            return;  // SDL3 fires RESIZED and PIXEL_SIZE_CHANGED for one resize
        pixW = newW;
        pixH = newH;
        const float density = SDL_GetWindowPixelDensity(window);
        toolbarPx = kiosk ? 0 : int(ui::kToolbarHeight * density + 0.5f);
        browser.resize(pixW, pixH - toolbarPx);
    };

    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
#ifdef IMWB_BACKEND_VULKAN
    {
        // Vulkan build: there is no GL context, so open a dedicated EGL
        // display for WPE/WebKit. Pin it to the SAME GPU as the Vulkan
        // device by matching DRM render-node minors — cross-vendor dma-buf
        // imports are not safe.
        auto qd = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
        auto qds = (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
        auto gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
        const int wantMinor = h.drmRenderMinor;
        if (qd && qds && gpd && wantMinor >= 0) {
            EGLDeviceEXT devs[8] = {};
            EGLint n = 0;
            if (qd(8, devs, &n)) {
                for (EGLint i = 0; i < n && eglDisplay == EGL_NO_DISPLAY; i++) {
                    const char* node = qds(devs[i], EGL_DRM_RENDER_NODE_FILE_EXT);
                    if (!node)
                        continue;
                    struct stat st{};
                    if (::stat(node, &st) || minor(st.st_rdev) != (unsigned)wantMinor)
                        continue;
                    eglDisplay = gpd(EGL_PLATFORM_DEVICE_EXT, devs[i], nullptr);
                    if (eglDisplay != EGL_NO_DISPLAY &&
                        !eglInitialize(eglDisplay, nullptr, nullptr))
                        eglDisplay = EGL_NO_DISPLAY;
                }
            }
        }
        if (eglDisplay == EGL_NO_DISPLAY) {
            // Fallback: Mesa's surfaceless platform (first hardware node).
            eglDisplay = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                             : eglGetDisplay(EGL_DEFAULT_DISPLAY);
            if (eglDisplay == EGL_NO_DISPLAY || !eglInitialize(eglDisplay, nullptr, nullptr)) {
                std::fprintf(stderr, "error: EGL init failed: 0x%x\n", eglGetError());
                return 1;
            }
        }
    }
#else
    eglDisplay = SDL_EGL_GetCurrentDisplay();
#endif  // IMWB_BACKEND_VULKAN
    if (!eglDisplay) {
        std::fprintf(stderr, "error: no current EGL display: %s\n", SDL_GetError());
        return 1;
    }

    if (!browser.init(eglDisplay, pixW, pixH, args.url))
        return 1;
    applyLayout();

    // DOM fullscreen requests (e.g. YouTube fullscreen button).
    int domFullscreenRequest = 0;  // 0 none, 1 enter, -1 exit
    browser.onDomFullscreen = [&](bool enter) { domFullscreenRequest = enter ? 1 : -1; };
    auto setKiosk = [&](bool enable) {
        kiosk = enable;
        SDL_SetWindowFullscreen(window, enable);
        applyLayout();
    };
    if (args.kiosk)
        setKiosk(true);

#if ENABLE_BENCHMARK_HARNESS
    if (args.benchFish > 0)
        browser.startBenchmark(args.benchFish);
#endif  // ENABLE_BENCHMARK_HARNESS

    bool showStats = false;
    bool minimized = false;
    bool pageMouseDown = false;  // a press that landed on the page (drag completion)
    std::string lastTitle;
    bool hadEvents = false;

    static gint64 lastPresentUs = 0;

    for (;;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            hadEvents = true;
            if (kDebugInput &&
                (ev.type == SDL_EVENT_TEXT_INPUT || ev.type == SDL_EVENT_TEXT_EDITING))
                std::fprintf(stderr, "[input] %s '%s'\n",
                             ev.type == SDL_EVENT_TEXT_INPUT ? "TEXT_INPUT" : "TEXT_EDITING",
                             ev.text.text);
            ImGui_ImplSDL3_ProcessEvent(&ev);

            switch (ev.type) {
            case SDL_EVENT_QUIT:
                goto done;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                // The WM sporadically asks borderless/fullscreen windows to
                // close (observed on Mint/X11 minutes into a session), which
                // killed long-running kiosk sessions silently. In kiosk mode
                // the app is never closable from the outside; Ctrl+W/Q quits.
                if (kioskBase)
                    break;
                goto done;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                applyLayout();
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                // Pause presentation while hidden: swapping into a surface the
                // compositor has hidden can leave the EGL state wedged after a
                // quick minimize/restore cycle (observed with libdecor+weston).
                if (kDebugInput)
                    std::fprintf(stderr, "[input] MINIMIZED flags=0x%lx\n",
                                 (unsigned long)SDL_GetWindowFlags(window));
                minimized = true;
                hadEvents = true;
                break;

            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_SHOWN:
                if (kDebugInput)
                    std::fprintf(stderr, "[input] %s flags=0x%lx\n",
                                 ev.type == SDL_EVENT_WINDOW_RESTORED ? "RESTORED" : "SHOWN",
                                 (unsigned long)SDL_GetWindowFlags(window));
                minimized = false;
                hadEvents = true;  // repaint immediately on the way back
                break;

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                const uint16_t mods = ev.key.mod;
                if (ev.key.down && ev.key.scancode == SDL_SCANCODE_F11) {  // kiosk toggle
                    setKiosk(!kiosk);
                    break;
                }
                if (ev.key.down && ev.key.scancode == SDL_SCANCODE_F3) {  // stats overlay
                    showStats = !showStats;
                    break;
                }
                if (ev.key.down && ev.key.scancode == SDL_SCANCODE_L && (mods & SDL_KMOD_CTRL)) {
                    browser.focusUrlRequest = true;  // focus URL bar
                    break;
                }
                // While the user types in ImGui widgets, keys stay in the UI.
                if (ImGui::GetIO().WantCaptureKeyboard && !(mods & (SDL_KMOD_CTRL | SDL_KMOD_ALT)))
                    break;
                browser.key(ev.key.scancode, mods, ev.key.down);
                break;
            }

            // Route by geometry, not io.WantCaptureMouse: that flag lags a
            // frame behind, so a fast move-then-click (touchpad) would be
            // swallowed by ImGui and never reach the page. A release is
            // ALWAYS forwarded once its press went to the page — dropping a
            // button-up would leave the page stuck "pressed" and eat every
            // later click.
            case SDL_EVENT_MOUSE_MOTION:
                if (ev.motion.y >= toolbarPx)
                    browser.pointerMotion(ev.motion.x, ev.motion.y - toolbarPx);
                else if (pageMouseDown)  // drag crossing the toolbar boundary
                    browser.pointerMotion(ev.motion.x, 0);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.y >= toolbarPx) {
                    browser.pointerButton(ev.button.x, ev.button.y - toolbarPx, ev.button.button, true);
                    pageMouseDown = true;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.y >= toolbarPx || pageMouseDown) {
                    const int vy = ev.button.y - toolbarPx < 0 ? 0 : ev.button.y - toolbarPx;
                    browser.pointerButton(ev.button.x, vy, ev.button.button, false);
                    pageMouseDown = false;
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (ev.wheel.mouse_y >= toolbarPx)
                    browser.scroll(ev.wheel.mouse_x, ev.wheel.mouse_y - toolbarPx, ev.wheel.x, ev.wheel.y);
                break;

            default:
                break;
            }
        } while (SDL_PollEvent(&ev));

        if (domFullscreenRequest) {  // honor DOM fullscreen (YouTube etc.)
            setKiosk(domFullscreenRequest > 0 ? true : kioskBase);
            browser.notifyDomFullscreenDone(domFullscreenRequest > 0);
            domFullscreenRequest = 0;
        }
        if (!browser.alive)
            break;

        const bool stats = g_getenv("IMWB_STATS") != nullptr;
        [[maybe_unused]] gint64 ts0 = stats ? g_get_monotonic_time() : 0;
        browser.pumpEvents();
        [[maybe_unused]] gint64 ts1 = stats ? g_get_monotonic_time() : 0;
        browser.updateWebTexture();
        [[maybe_unused]] gint64 ts2 = stats ? g_get_monotonic_time() : 0;

        // Keep the page ticking while hidden (timers, network, exports) but
        // never present into a surface the compositor is not displaying.
        if (minimized) {
            browser.pumpEvents();
            SDL_Delay(10);
            continue;
        }

        // Present on demand, but never spin hot while waiting: a 2 ms nap
        // keeps idle CPU low without delaying frames or input noticeably.
        const bool newFrame = browser.takeFrameBound();
        const gint64 nowUs = g_get_monotonic_time();
        const bool due = (nowUs - lastPresentUs) >= 150000;  // 150ms heartbeat
        if (!(newFrame || hadEvents || showStats || due)) {
            SDL_Delay(2);
            continue;
        }

#ifdef IMWB_BACKEND_VULKAN
        // Kiosk direct path: draw the imported web frame straight to the
        // window surface with a minimal fullscreen pipeline — no ImGui. This
        // mirrors the GLES kiosk blit path below.
        if (kiosk && !showStats) {
            if (VkDmabufFrame* fr = browser.takeDmabufFrame())
                vp.importFrame(*fr);
            vp.drawFrameKiosk(pixW, pixH);
            browser.afterPresent();
            lastPresentUs = g_get_monotonic_time();
            hadEvents = false;
            continue;
        }

        // Windowed path: draw through ImGui's Vulkan renderer. In kiosk-with-
        // stats the web frame is the only thing on the background list.
        ImTextureID webTex = ImTextureID(0);
        if (VkDmabufFrame* fr = browser.takeDmabufFrame())
            webTex = vp.importFrame(*fr);  // 0 on import failure
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const float toolbarLogicalVk = kiosk ? 0.f : ui::kToolbarHeight;
        if (std::getenv("IMWB_VKGREEN"))  // diagnostic: is the image layer drawn?
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                ImVec2(0.f, 0.f), ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y),
                IM_COL32(0, 160, 0, 255));
        if (webTex)
            ImGui::GetBackgroundDrawList()->AddImage(webTex, ImVec2(0.f, toolbarLogicalVk),
                                                     ImVec2(ImGui::GetIO().DisplaySize.x,
                                                            ImGui::GetIO().DisplaySize.y));
        if (!kiosk || showStats) {
            if (ui::drawToolbar(browser, kiosk) == ui::Action::ToggleKiosk)
                setKiosk(!kiosk);
            ui::drawStatsOverlay(showStats, browser);
        }
        ImGui::Render();
        vp.drawFrame(pixW, pixH);
        browser.afterPresent();
        lastPresentUs = g_get_monotonic_time();
        hadEvents = false;
        continue;  // Vulkan build renders everything above
#else
        // Kiosk direct path: blit the web frame straight to the window
        // surface with a minimal shader — no ImGui, no extra scene. This is
        // architecturally what Cog does.
        if (kiosk && !showStats && browser.webTexture()) {
            ensureBlitProgram();
            glViewport(0, 0, pixW, pixH);
            glUseProgram(blitProg);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, browser.webTexture());
            glUniform1i(blitTexLoc, 0);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            if (g_getenv("IMWB_DUMP")) {  // one-shot framebuffer dump for debugging
                static bool dumped = false;
                if (!dumped && g_get_monotonic_time() > 0) {
                    dumped = true;
                    sleep(3);
                    std::vector<GLubyte> px(size_t(pixW) * pixH * 4);
                    glReadPixels(0, 0, pixW, pixH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                    FILE* f = fopen("/tmp/opencode/fb-dump.ppm", "wb");
                    if (f) {
                        std::fprintf(f, "P6\n%d %d\n255\n", pixW, pixH);
                        for (int y = pixH - 1; y >= 0; y--)
                            fwrite(&px[size_t(y) * pixW * 4], 1, size_t(pixW) * 3, f);
                        fclose(f);
                        std::fprintf(stderr, "[dump] wrote fb-dump.ppm\n");
                    }
                }
            }
            SDL_GL_SwapWindow(window);
            browser.afterPresent();
            lastPresentUs = g_get_monotonic_time();
            hadEvents = false;
            if (stats) {  // presents vs exports in the direct path
                static uint64_t frames = 0;
                static gint64 t0 = 0;
                frames++;
                gint64 now = g_get_monotonic_time();
                if (!t0)
                    t0 = now;
                if (now - t0 >= 5000000) {
                    std::fprintf(stderr, "[stats] direct: present=%.1f fps exports=%.1f/s\n",
                                 frames * 1e6 / (now - t0), browser.statExports_ * 1e6 / (now - t0));
                    frames = 0;
                    browser.statExports_ = 0;
                    t0 = now;
                }
            }
            continue;
        }
#endif  // !IMWB_BACKEND_VULKAN

#ifndef IMWB_BACKEND_VULKAN
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (kDebugInput) {  // ImGui mouse-state diagnostics
            static gint64 lastIoLog = 0;
            gint64 now = g_get_monotonic_time();
            if (now - lastIoLog >= 1000000) {
                lastIoLog = now;
                ImGuiIO& io = ImGui::GetIO();
                float gx, gy;
                SDL_GetGlobalMouseState(&gx, &gy);
                int wx, wy;
                SDL_GetWindowPosition(window, &wx, &wy);
                std::fprintf(stderr,
                             "[io] global=(%.0f,%.0f) winPos=(%d,%d) ioMouse=(%.0f,%.0f) capMouse=%d capKey=%d wantText=%d\n",
                             gx, gy, wx, wy, io.MousePos.x, io.MousePos.y, io.WantCaptureMouse,
                             io.WantCaptureKeyboard, io.WantTextInput);
            }
        }

        // Web content fills the whole window behind the toolbar.
        const float toolbarLogical = kiosk ? 0.f : ui::kToolbarHeight;
        if (GLuint tex = browser.webTexture())
            ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(uintptr_t)tex, ImVec2(0.f, toolbarLogical),
                                                     ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y));

        if (ui::drawToolbar(browser, kiosk) == ui::Action::ToggleKiosk)
            setKiosk(!kiosk);

        ui::drawStatsOverlay(showStats, browser);

        ImGui::Render();
        glViewport(0, 0, pixW, pixH);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        gint64 ts3 = stats ? g_get_monotonic_time() : 0;
        SDL_GL_SwapWindow(window);
        gint64 ts4 = stats ? g_get_monotonic_time() : 0;
#endif  // !IMWB_BACKEND_VULKAN (windowed GLES render tail)
        browser.afterPresent();
        lastPresentUs = g_get_monotonic_time();
        hadEvents = false;

        if (stats) {  // loop vs export rate + per-stage cost diagnostics (GL path)
#ifndef IMWB_BACKEND_VULKAN
            static uint64_t frames = 0;
            static gint64 t0 = 0;
            static double ms_pump = 0, ms_update = 0, ms_draw = 0, ms_swap = 0;
            frames++;
            ms_pump += (ts1 - ts0) / 1000.0;
            ms_update += (ts2 - ts1) / 1000.0;
            ms_draw += (ts3 - ts2) / 1000.0;
            ms_swap += (ts4 - ts3) / 1000.0;
            gint64 now = g_get_monotonic_time();
            if (!t0)
                t0 = now;
            if (now - t0 >= 5000000) {
                std::fprintf(stderr,
                             "[stats] loop=%.1f fps exports=%.1f/s | pump=%.2f update=%.2f draw=%.2f swap=%.2f ms\n",
                             frames * 1e6 / (now - t0), browser.statExports_ * 1e6 / (now - t0), ms_pump / frames,
                             ms_update / frames, ms_draw / frames, ms_swap / frames);
                frames = 0;
                browser.statExports_ = 0;
                t0 = now;
                ms_pump = ms_update = ms_draw = ms_swap = 0;
            }
#endif  // !IMWB_BACKEND_VULKAN
        }

        if (browser.title != lastTitle) {  // keep the OS window title current
            lastTitle = browser.title;
            char title[512];
            snprintf(title, sizeof(title), "%s%s%s", "ImWebBrowser", lastTitle.empty() ? "" : " - ",
                     lastTitle.c_str());
            SDL_SetWindowTitle(window, title);
        }
    }

done:
    browser.shutdown();
#ifdef IMWB_BACKEND_VULKAN
    ImGui_ImplVulkan_Shutdown();
    vp.shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
    SDL_GL_DestroyContext(gl);
#endif  // IMWB_BACKEND_VULKAN
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
