cmake_minimum_required(VERSION 3.22)
project(ImWebBrowser VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to an optimized (Release) build so a bare `cmake -B build` never
# silently produces an unoptimized binary (CMAKE_CXX_FLAGS_RELEASE = -O3 -DNDEBUG).
# Pass -DCMAKE_BUILD_TYPE=Debug explicitly when you want symbols + no
# optimization. This is a target-hardware-focused browser: speed is the point.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Release default)" FORCE)
endif()

# ---------------------------------------------------------------------------
# Required dependencies (system installations, no vendored copies).
# ---------------------------------------------------------------------------
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)
find_package(PkgConfig REQUIRED)

# ---------------------------------------------------------------------------
# Rendering backend selection: OpenGL ES (default, works everywhere SDL3 +
# Mesa do) or Vulkan (kiosk/UI present through the ImGui Vulkan backend and
# dma-buf external-memory import). Enabling Vulkan takes precedence.
# ---------------------------------------------------------------------------
option(IMWB_BACKEND_OPENGL_ES "Render with an OpenGL ES 3 context (SDL3 GL)" ON)
option(IMWB_BACKEND_VULKAN    "Render with Vulkan (dma-buf import + swapchain)" OFF)

if(IMWB_BACKEND_VULKAN AND IMWB_BACKEND_OPENGL_ES)
    message(STATUS "IMWB_BACKEND_VULKAN=ON: disabling the OpenGL ES backend")
    set(IMWB_BACKEND_OPENGL_ES OFF CACHE BOOL "" FORCE)
endif()

pkg_check_modules(WPEWEBKIT REQUIRED IMPORTED_TARGET wpe-webkit-2.0>=2.52.6)
pkg_check_modules(WPEFDO REQUIRED IMPORTED_TARGET wpebackend-fdo-1.0)
pkg_check_modules(LIBWPE REQUIRED IMPORTED_TARGET wpe-1.0)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)  # shm frame fallback
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
if(IMWB_BACKEND_OPENGL_ES)
    pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)
else()
    find_package(Vulkan REQUIRED)
endif()

message(STATUS "WPE WebKit ${WPEWEBKIT_VERSION}")
message(STATUS "WPE FDO    ${WPEFDO_VERSION}")
message(STATUS "libwpe     ${LIBWPE_VERSION}")
message(STATUS "SDL3       ${SDL3_VERSION}")

# ---------------------------------------------------------------------------
# Feature options — the full WPE WebKit capability matrix. Every option maps
# to a real WebKitSettings property applied in browser.cpp applySettings()
# (see https://wpewebkit.org/wpt-status/ for the conformance areas covered).
#
# Core engine
#   ENABLE_JAVASCRIPT        JavaScript execution      (enable-javascript)
#   ENABLE_WEBGL             WebGL rendering           (enable-webgl)
#   ENABLE_SMOOTH_SCROLLING  Animated scrolling        (enable-smooth-scrolling)
#   ENABLE_PAGE_CACHE        Back/forward page cache   (enable-page-cache)
#   ENABLE_DNS_PREFETCH      DNS prefetching           (enable-dns-prefetching)
#   ENABLE_2D_CANVAS accel   Accelerated <canvas> 2D   (enable-2d-canvas-acceleration)
#
# Accessibility & input
#   ENABLE_CARET_BROWSING    Visible text caret        (enable-caret-browsing)
#   ENABLE_SPATIAL_NAVIGATION Arrow-key link navigation (enable-spatial-navigation)
#   ENABLE_TAB_FOCUS_CYCLE   Tab cycles through elements (enable-tab-key-cycles-through-elements)
#   ENABLE_TEXT_AREAS_RESIZE Drag-resize <textarea>    (enable-resizable-text-areas)
#   ENABLE_BF_GESTURES       Swipe back/forward gestures (enable-back-forward-navigation-gestures)
#
# Media
#   ENABLE_MEDIA             HTML5 media playback      (enable-mediasource + inline playback)
#   ENABLE_AUDIO             WebAudio                  (enable-webaudio)
#   ENABLE_AUTOPLAY          Media autoplay w/o gesture (media-playback-requires-user-gesture=OFF)
#   ENABLE_MEDIA_CAPABILITIES navigator.mediaCapabilities (enable-media-capabilities)
#   ENABLE_ENCRYPTED_MEDIA   EME/DRM playback          (enable-encrypted-media; needs Widevine module)
#   ENABLE_GSTREAMER         Verify GStreamer codec stack
#
# Misc
#   ENABLE_WEBRTC            WebRTC + getUserMedia     (enable-webrtc + enable-media-stream)
#   ENABLE_DEVELOPER_EXTRAS  WebKit Web Inspector      (developer-extras)
#   ENABLE_BENCHMARK_HARNESS --bench-fish N automated WebGL Aquarium benchmark
# ---------------------------------------------------------------------------
option(ENABLE_JAVASCRIPT        "Enable JavaScript execution" ON)
option(ENABLE_WEBGL             "Enable WebGL" ON)
option(ENABLE_SMOOTH_SCROLLING  "Enable smooth animated scrolling" ON)
option(ENABLE_PAGE_CACHE        "Enable page cache for back/forward" ON)
option(ENABLE_DNS_PREFETCH      "Enable DNS prefetching" ON)
option(ENABLE_2D_CANVAS         "Enable accelerated 2D canvas" ON)
option(ENABLE_CARET_BROWSING    "Enable caret browsing" OFF)
option(ENABLE_SPATIAL_NAVIGATION "Enable spatial navigation" OFF)
option(ENABLE_TAB_FOCUS_CYCLE   "Tab key cycles through focusable elements" ON)
option(ENABLE_TEXT_AREAS_RESIZE "Allow resizing text areas" ON)
option(ENABLE_BF_GESTURES       "Enable back/forward swipe gestures" OFF)
option(ENABLE_WEBRTC            "Enable WebRTC and getUserMedia" ON)
option(ENABLE_MEDIA             "Enable HTML5 audio/video playback (GStreamer)" ON)
option(ENABLE_AUDIO             "Enable WebAudio" ON)
option(ENABLE_AUTOPLAY          "Allow media autoplay without user gesture" OFF)
option(ENABLE_MEDIA_CAPABILITIES "Enable mediaCapabilities API" ON)
option(ENABLE_ENCRYPTED_MEDIA   "Enable Encrypted Media Extensions (DRM)" ON)
option(ENABLE_GSTREAMER         "Require and verify the GStreamer media stack" ON)
option(ENABLE_DEVELOPER_EXTRAS  "Enable the WebKit Web Inspector" OFF)
option(ENABLE_BENCHMARK_HARNESS "Build the automated WebGL Aquarium benchmark harness" OFF)

# Preferred GStreamer video decoder, applied at runtime via
# GST_PLUGIN_FEATURE_RANK when ENABLE_GSTREAMER is on. Empty (default) leaves
# GStreamer's own ranking untouched (usually lifts avdec_h264 to primary); set
# it to force a specific decoder, e.g. -DIMWB_VIDEO_DECODER=vah264dec for
# VA-API hardware decode, openh264dec, vulkanh264dec, avdec_h265, ...
set(IMWB_VIDEO_DECODER "" CACHE STRING
    "GStreamer video-decoder element forced to MAX rank at startup (empty = GStreamer default)")

# Media content types that must be decoded in HARDWARE, forwarded to
# webkit_settings_set_media_content_types_requiring_hardware_support() as a
# string. Empty keeps the engine default (software decode allowed). On the
# STM32MP257F set it to force H.264 MP4 onto the VPU:
#   -DIMWB_MEDIA_HW_TYPES='video/mp4; codecs="avc1"'
set(IMWB_MEDIA_HW_TYPES "" CACHE STRING
    "Media content types that require hardware decoding (e.g. 'video/mp4; codecs=\"avc1\"'); empty = engine default")

if(ENABLE_MEDIA AND NOT ENABLE_GSTREAMER)
    message(FATAL_ERROR "ENABLE_MEDIA=ON requires ENABLE_GSTREAMER=ON (media playback needs GStreamer)")
endif()
if(ENABLE_GSTREAMER)
    pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.20 gstreamer-plugins-base-1.0>=1.20)
    pkg_check_modules(GSTBAD IMPORTED_TARGET gstreamer-plugins-bad-1.0>=1.20)
    if(NOT GSTBAD_FOUND)
        message(WARNING "gstreamer-plugins-bad not found: some media formats (e.g. HLS) may not play")
    endif()
    message(STATUS "GStreamer  ${GST_VERSION}")
endif()

configure_file(src/config.h.in config.h @ONLY)

# Configure-time capability report (mirrors the WPT feature areas).
message(STATUS "")
message(STATUS "ImWebBrowser ${PROJECT_VERSION} feature matrix:")
message(STATUS "  core     : js=${ENABLE_JAVASCRIPT} webgl=${ENABLE_WEBGL} canvas2d=${ENABLE_2D_CANVAS} smooth=${ENABLE_SMOOTH_SCROLLING} pagecache=${ENABLE_PAGE_CACHE} dnsprefetch=${ENABLE_DNS_PREFETCH}")
message(STATUS "  a11y     : caret=${ENABLE_CARET_BROWSING} spatial=${ENABLE_SPATIAL_NAVIGATION} tabcycle=${ENABLE_TAB_FOCUS_CYCLE} resize=${ENABLE_TEXT_AREAS_RESIZE} gestures=${ENABLE_BF_GESTURES}")
message(STATUS "  media    : playback=${ENABLE_MEDIA} audio=${ENABLE_AUDIO} autoplay=${ENABLE_AUTOPLAY} mediacaps=${ENABLE_MEDIA_CAPABILITIES} eme=${ENABLE_ENCRYPTED_MEDIA} gstreamer=${ENABLE_GSTREAMER}")
message(STATUS "  hw       : mediaHwTypes='${IMWB_MEDIA_HW_TYPES}' decoder='${IMWB_VIDEO_DECODER}'")
message(STATUS "  misc     : webrtc=${ENABLE_WEBRTC} devtools=${ENABLE_DEVELOPER_EXTRAS} bench=${ENABLE_BENCHMARK_HARNESS}")
message(STATUS "")

# ---------------------------------------------------------------------------
# Dear ImGui (bundled in src/libraries/imgui) with SDL3 + the chosen render
# backend (OpenGL ES 3 or Vulkan — imgui_impl_vulkan carries precompiled
# SPIR-V, so no shader toolchain is required for Vulkan builds).
# ---------------------------------------------------------------------------
set(IMGUI_SOURCES
    src/libraries/imgui/imgui.cpp
    src/libraries/imgui/imgui_draw.cpp
    src/libraries/imgui/imgui_tables.cpp
    src/libraries/imgui/imgui_widgets.cpp
    src/libraries/imgui/backends/imgui_impl_sdl3.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC src/libraries/imgui)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

if(IMWB_BACKEND_OPENGL_ES)
    list(APPEND IMGUI_SOURCES src/libraries/imgui/backends/imgui_impl_opengl3.cpp)
    target_sources(imgui PRIVATE src/libraries/imgui/backends/imgui_impl_opengl3.cpp)
    # The app creates an OpenGL ES 3 context; make the renderer use its ES3 path.
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
else()
    target_sources(imgui PRIVATE src/libraries/imgui/backends/imgui_impl_vulkan.cpp)
    # Resolve the vk* calls inside imgui_impl_vulkan.cpp.
    target_link_libraries(imgui PUBLIC Vulkan::Vulkan)
endif()

# ---------------------------------------------------------------------------
# Browser.
# ---------------------------------------------------------------------------
add_executable(imwebbrowser
    src/main.cpp
    src/browser.cpp
    src/ui.cpp
)
if(IMWB_BACKEND_VULKAN)
    target_sources(imwebbrowser PRIVATE src/vk_backend.cpp)
endif()
target_compile_options(imwebbrowser PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_include_directories(imwebbrowser PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
if(IMWB_BACKEND_OPENGL_ES)
    target_compile_definitions(imwebbrowser PRIVATE IMWB_BACKEND_OPENGL_ES=1)
    target_link_libraries(imwebbrowser PRIVATE PkgConfig::GLESV2)
else()
    target_compile_definitions(imwebbrowser PRIVATE IMWB_BACKEND_VULKAN=1)
    target_link_libraries(imwebbrowser PRIVATE Vulkan::Vulkan)
endif()
target_link_libraries(imwebbrowser PRIVATE
    imgui
    SDL3::SDL3
    PkgConfig::WPEWEBKIT
    PkgConfig::WPEFDO
    PkgConfig::LIBWPE
    PkgConfig::XKBCOMMON
    PkgConfig::WAYLAND_SERVER
    PkgConfig::EGL
)

# ---------------------------------------------------------------------------
# Install.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS imwebbrowser RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
