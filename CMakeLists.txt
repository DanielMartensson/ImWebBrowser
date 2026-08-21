cmake_minimum_required(VERSION 3.22)
project(ImWebBrowser VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Required dependencies (system installations, no vendored copies).
# ---------------------------------------------------------------------------
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3)
find_package(PkgConfig REQUIRED)

pkg_check_modules(WPEWEBKIT REQUIRED IMPORTED_TARGET wpe-webkit-2.0>=2.53.90)
pkg_check_modules(WPEFDO REQUIRED IMPORTED_TARGET wpebackend-fdo-1.0)
pkg_check_modules(LIBWPE REQUIRED IMPORTED_TARGET wpe-1.0)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)  # shm frame fallback
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)

message(STATUS "WPE WebKit ${WPEWEBKIT_VERSION}")
message(STATUS "WPE FDO    ${WPEFDO_VERSION}")
message(STATUS "libwpe     ${LIBWPE_VERSION}")
message(STATUS "SDL3       ${SDL3_VERSION}")

# ---------------------------------------------------------------------------
# Feature options. Each option maps to a real WebKit setting or a verified
# system dependency - nothing here is cosmetic.
#
#   ENABLE_JAVASCRIPT        JavaScript execution (WebKitSettings:enable-javascript)
#   ENABLE_WEBGL             WebGL rendering (WebKitSettings:enable-webgl)
#   ENABLE_WEBRTC            WebRTC + getUserMedia (enable-webrtc / enable-media-stream)
#   ENABLE_MEDIA             HTML5 media playback (MediaSource, inline playback;
#                            requires GStreamer at runtime)
#   ENABLE_AUDIO             WebAudio (WebKitSettings:enable-webaudio)
#   ENABLE_ENCRYPTED_MEDIA   EME/DRM playback such as Netflix
#                            (WebKitSettings:enable-encrypted-media; still needs
#                            a DRM module on the system, e.g. Widevine)
#   ENABLE_GSTREAMER         Verify the GStreamer stack needed for media codecs
#   ENABLE_DEVELOPER_EXTRAS  WebKit Web Inspector (WebKitSettings:developer-extras)
#   ENABLE_BENCHMARK_HARNESS --bench-fish N automated WebGL Aquarium benchmark
# ---------------------------------------------------------------------------
option(ENABLE_JAVASCRIPT       "Enable JavaScript execution" ON)
option(ENABLE_WEBGL            "Enable WebGL" ON)
option(ENABLE_WEBRTC           "Enable WebRTC and getUserMedia" ON)
option(ENABLE_MEDIA            "Enable HTML5 audio/video playback (GStreamer)" ON)
option(ENABLE_AUDIO            "Enable WebAudio" ON)
option(ENABLE_ENCRYPTED_MEDIA  "Enable Encrypted Media Extensions (DRM)" ON)
option(ENABLE_GSTREAMER        "Require and verify the GStreamer media stack" ON)
option(ENABLE_DEVELOPER_EXTRAS "Enable the WebKit Web Inspector" OFF)
option(ENABLE_BENCHMARK_HARNESS "Build the automated WebGL Aquarium benchmark harness" OFF)

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

# ---------------------------------------------------------------------------
# Dear ImGui (bundled in src/libraries/imgui) with SDL3 + OpenGL ES 3 backends.
# ---------------------------------------------------------------------------
add_library(imgui STATIC
    src/libraries/imgui/imgui.cpp
    src/libraries/imgui/imgui_draw.cpp
    src/libraries/imgui/imgui_tables.cpp
    src/libraries/imgui/imgui_widgets.cpp
    src/libraries/imgui/backends/imgui_impl_sdl3.cpp
    src/libraries/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC src/libraries/imgui)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
# The app creates an OpenGL ES 3 context; make the renderer use its ES3 path.
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)

# ---------------------------------------------------------------------------
# Browser.
# ---------------------------------------------------------------------------
add_executable(imwebbrowser
    src/main.cpp
    src/browser.cpp
    src/ui.cpp
)
target_compile_options(imwebbrowser PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_include_directories(imwebbrowser PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(imwebbrowser PRIVATE
    imgui
    SDL3::SDL3
    PkgConfig::WPEWEBKIT
    PkgConfig::WPEFDO
    PkgConfig::LIBWPE
    PkgConfig::XKBCOMMON
    PkgConfig::WAYLAND_SERVER
    PkgConfig::EGL
    PkgConfig::GLESV2
)
