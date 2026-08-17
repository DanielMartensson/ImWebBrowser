# ImWebBrowser - a minimal, GPU-accelerated web browser powered by
# WPE WebKit + Dear ImGui + SDL3, with Vulkan and OpenGL ES backends.
#
# Targets:
#   - x86-64 Linux (development host) - native build.
#   - STM32MP2 (Arm Cortex-A35, VeriSilicon GC7000) - cross-compiled. Pass
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/STM32MP2-armv8a.cmake.
#     All dependencies are resolved through the target sysroot's pkg-config
#     and CMake configs (see README "Cross-compiling for STM32MP2").
#
# NOTE: The development host is a Lenovo ThinkPad W540 (2013, 8 GB RAM).
# Always build with a single job:
#
#     cmake --build build --parallel 1
#
# Parallel builds (-j2 and above) will OOM / wedge the machine.

cmake_minimum_required(VERSION 3.21)

project(ImWebBrowser
    VERSION 1.0.0
    DESCRIPTION "A minimal GPU-accelerated web browser (WPE WebKit + ImGui + SDL3)"
    HOMEPAGE_URL "https://github.com/DanielMartensson/ImWebBrowser"
    LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(IMWEBBROWSER_BACKEND_VULKAN "Render the web view through Vulkan" OFF)
option(IMWEBBROWSER_BACKEND_OPENGL_ES "Render the web view through OpenGL ES" ON)

option(IMWEBBROWSER_ENABLE_JAVASCRIPT "Enable JavaScript in web pages" ON)
option(IMWEBBROWSER_ENABLE_WEBGL "Enable WebGL" ON)
option(IMWEBBROWSER_ENABLE_WEBRTC "Enable WebRTC" OFF)
option(IMWEBBROWSER_ENABLE_MEDIA "Enable HTML audio/video playback" ON)
option(IMWEBBROWSER_ENABLE_MEDIA_STREAM "Enable getUserMedia capture" OFF)
option(IMWEBBROWSER_ENABLE_WEB_AUDIO "Enable Web Audio" OFF)
option(IMWEBBROWSER_ENABLE_FULLSCREEN "Allow web pages to request fullscreen" ON)
option(IMWEBBROWSER_ENABLE_DEVELOPER_EXTRAS "Enable the Web Inspector / developer extras" OFF)
option(IMWEBBROWSER_ENABLE_SMOOTH_SCROLLING "Prefer smooth pixel axis scrolling" ON)
option(IMWEBBROWSER_ENABLE_ENCRYPTED_MEDIA "Enable Encrypted Media Extensions (DRM)" ON)
option(IMWEBBROWSER_ENABLE_PAGE_CACHE "Enable the back/forward page cache" ON)
option(IMWEBBROWSER_ENABLE_DNS_PREFETCHING "Enable DNS prefetching of links" OFF)
option(IMWEBBROWSER_ENABLE_SPATIAL_NAVIGATION "Enable arrow-key spatial navigation" OFF)
option(IMWEBBROWSER_ENABLE_CARET_BROWSING "Enable caret browsing mode" OFF)
option(IMWEBBROWSER_ENABLE_TABS_TO_LINKS "Allow Tab to focus page links" ON)
option(IMWEBBROWSER_ENABLE_SITE_SPECIFIC_QUIRKS "Apply per-site compatibility quirks" ON)
option(IMWEBBROWSER_ENABLE_OFFLINE_APP_CACHE "Enable the legacy offline app cache" OFF)
option(IMWEBBROWSER_ENABLE_HTML5_DATABASE "Enable HTML5 SQL databases" ON)
option(IMWEBBROWSER_ENABLE_HYPERLINK_AUDITING "Enable hyperlink auditing (ping)" OFF)
option(IMWEBBROWSER_ENABLE_RESIZABLE_TEXT_AREAS "Allow text areas to be resized" ON)
option(IMWEBBROWSER_ENABLE_MOCK_CAPTURE_DEVICES "Enable mock capture devices" OFF)
option(IMWEBBROWSER_ENABLE_CONSOLE_TO_STDOUT "Write page console messages to stdout" OFF)
option(IMWEBBROWSER_ENABLE_ACCELERATED_2D_CANVAS "Enable accelerated 2D canvas" ON)

option(IMWEBBROWSER_TRACE_LOGGING "Compile with trace-level logging" OFF)
option(IMWEBBROWSER_INSTALL "Install the imwebbrowser binary" ON)

# ---------------------------------------------------------------------------
# Map the ENABLE_* options onto the DEFAULT_* variables consumed by
# cmake/config_defaults.h.in (the generated header drives WebKit settings).
# ---------------------------------------------------------------------------
set(IMWEBBROWSER_DEFAULT_JAVASCRIPT ${IMWEBBROWSER_ENABLE_JAVASCRIPT})
set(IMWEBBROWSER_DEFAULT_WEBGL ${IMWEBBROWSER_ENABLE_WEBGL})
set(IMWEBBROWSER_DEFAULT_WEBRTC ${IMWEBBROWSER_ENABLE_WEBRTC})
set(IMWEBBROWSER_DEFAULT_MEDIA ${IMWEBBROWSER_ENABLE_MEDIA})
set(IMWEBBROWSER_DEFAULT_MEDIA_STREAM ${IMWEBBROWSER_ENABLE_MEDIA_STREAM})
set(IMWEBBROWSER_DEFAULT_WEB_AUDIO ${IMWEBBROWSER_ENABLE_WEB_AUDIO})
set(IMWEBBROWSER_DEFAULT_FULLSCREEN_API ${IMWEBBROWSER_ENABLE_FULLSCREEN})
set(IMWEBBROWSER_DEFAULT_DEVELOPER_EXTRAS ${IMWEBBROWSER_ENABLE_DEVELOPER_EXTRAS})
set(IMWEBBROWSER_DEFAULT_SMOOTH_SCROLLING ${IMWEBBROWSER_ENABLE_SMOOTH_SCROLLING})
set(IMWEBBROWSER_DEFAULT_ENCRYPTED_MEDIA ${IMWEBBROWSER_ENABLE_ENCRYPTED_MEDIA})
set(IMWEBBROWSER_DEFAULT_PAGE_CACHE ${IMWEBBROWSER_ENABLE_PAGE_CACHE})
set(IMWEBBROWSER_DEFAULT_DNS_PREFETCHING ${IMWEBBROWSER_ENABLE_DNS_PREFETCHING})
set(IMWEBBROWSER_DEFAULT_SPATIAL_NAVIGATION ${IMWEBBROWSER_ENABLE_SPATIAL_NAVIGATION})
set(IMWEBBROWSER_DEFAULT_CARET_BROWSING ${IMWEBBROWSER_ENABLE_CARET_BROWSING})
set(IMWEBBROWSER_DEFAULT_TABS_TO_LINKS ${IMWEBBROWSER_ENABLE_TABS_TO_LINKS})
set(IMWEBBROWSER_DEFAULT_SITE_SPECIFIC_QUIRKS ${IMWEBBROWSER_ENABLE_SITE_SPECIFIC_QUIRKS})
set(IMWEBBROWSER_DEFAULT_OFFLINE_APP_CACHE ${IMWEBBROWSER_ENABLE_OFFLINE_APP_CACHE})
set(IMWEBBROWSER_DEFAULT_HTML5_DATABASE ${IMWEBBROWSER_ENABLE_HTML5_DATABASE})
set(IMWEBBROWSER_DEFAULT_HYPERLINK_AUDITING ${IMWEBBROWSER_ENABLE_HYPERLINK_AUDITING})
set(IMWEBBROWSER_DEFAULT_RESIZABLE_TEXT_AREAS ${IMWEBBROWSER_ENABLE_RESIZABLE_TEXT_AREAS})
set(IMWEBBROWSER_DEFAULT_MOCK_CAPTURE_DEVICES ${IMWEBBROWSER_ENABLE_MOCK_CAPTURE_DEVICES})
set(IMWEBBROWSER_DEFAULT_CONSOLE_TO_STDOUT ${IMWEBBROWSER_ENABLE_CONSOLE_TO_STDOUT})
set(IMWEBBROWSER_DEFAULT_ACCELERATED_2D_CANVAS ${IMWEBBROWSER_ENABLE_ACCELERATED_2D_CANVAS})

set(IMWEBBROWSER_STARTUP_URL "https://www.raspberrypi.com/" CACHE STRING "URL loaded at startup")
set(IMWEBBROWSER_WINDOW_WIDTH 1280 CACHE STRING "Initial window width (pixels)")
set(IMWEBBROWSER_WINDOW_HEIGHT 800 CACHE STRING "Initial window height (pixels)")
set(IMWEBBROWSER_WINDOW_FULLSCREEN OFF CACHE BOOL "Start the window fullscreen")
set(IMWEBBROWSER_USER_AGENT "" CACHE STRING "Custom user agent string (empty = engine default)")

# ---------------------------------------------------------------------------
# Backend selection and validation
# ---------------------------------------------------------------------------
if(IMWEBBROWSER_BACKEND_VULKAN)
    if(IMWEBBROWSER_BACKEND_OPENGL_ES)
        message(STATUS "ImWebBrowser: both backends requested - using Vulkan, ignoring OpenGL ES.")
    endif()
    set(IMWEBBROWSER_ACTIVE_BACKEND "vulkan")
elseif(IMWEBBROWSER_BACKEND_OPENGL_ES)
    set(IMWEBBROWSER_ACTIVE_BACKEND "opengles")
else()
    message(FATAL_ERROR
        "ImWebBrowser: no renderer backend selected. Enable either\n"
        "  -DIMWEBBROWSER_BACKEND_OPENGL_ES=ON  or  -DIMWEBBROWSER_BACKEND_VULKAN=ON")
endif()

# ---------------------------------------------------------------------------
# Toolchain / package discovery
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# SDL3 (has a CMake config since 3.x).
#
# Prefer an installed SDL3 (host, sysroot, or distribution package). When it
# is not available - e.g. Linux Mint 22.3 and Yocto scarthgap ship no SDL3
# package - fetch the SDL3 sources and build them here, so the browser still
# builds without requiring an extra system dependency.
set(IMWEBBROWSER_SDL3_VERSION "release-3.4.14" CACHE STRING
    "SDL3 tag fetched when no installed SDL3 is found")
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    message(STATUS
        "ImWebBrowser: SDL3 not found - fetching ${IMWEBBROWSER_SDL3_VERSION} from source")
    include(FetchContent)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${IMWEBBROWSER_SDL3_VERSION}
        GIT_SHALLOW TRUE)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
endif()

# WPE WebKit - probe the known pkg-config names, newest first.
set(WPE_WEBKIT_PC_NAMES wpe-webkit-2.0 wpe-webkit-1.1 wpe-webkit-1.0)
set(WPE_WEBKIT_PC "wpe-webkit-1.0")
foreach(_name IN LISTS WPE_WEBKIT_PC_NAMES)
    pkg_check_modules(WPE_WEBKIT QUIET IMPORTED_TARGET ${_name})
    if(WPE_WEBKIT_FOUND)
        set(WPE_WEBKIT_PC ${_name})
        break()
    endif()
endforeach()
if(NOT WPE_WEBKIT_FOUND)
    message(FATAL_ERROR "ImWebBrowser: WPE WebKit not found (tried: ${WPE_WEBKIT_PC_NAMES}).")
endif()

pkg_check_modules(WPE REQUIRED IMPORTED_TARGET wpe-1.0)
pkg_check_modules(WPE_FDO REQUIRED IMPORTED_TARGET wpebackend-fdo-1.0)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gobject-2.0)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)

if(IMWEBBROWSER_ACTIVE_BACKEND STREQUAL "opengles")
    pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)
endif()

if(IMWEBBROWSER_ACTIVE_BACKEND STREQUAL "vulkan")
    find_package(Vulkan REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Cross-compilation (STM32MP2 and other aarch64 targets)
# ---------------------------------------------------------------------------
if(CMAKE_CROSSCOMPILING)
    if(NOT PKG_CONFIG_EXECUTABLE)
        message(FATAL_ERROR
            "ImWebBrowser: cross-compiling without a target pkg-config.\n"
            "Provide a toolchain file that points PKG_CONFIG_EXECUTABLE at the\n"
            "target's pkg-config (see cmake/toolchains/STM32MP2-armv8a.cmake).")
    endif()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        message(STATUS "ImWebBrowser: cross-compiling for aarch64 (STM32MP2 class target)")
        message(STATUS "  Sysroot            : ${CMAKE_SYSROOT}")
        message(STATUS "  pkg-config         : ${PKG_CONFIG_EXECUTABLE}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Generated configuration header
# ---------------------------------------------------------------------------
configure_file(
    cmake/config_defaults.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config_defaults.h
    @ONLY)

# ---------------------------------------------------------------------------
# ImGui (vendored, kept untouched)
# ---------------------------------------------------------------------------
add_library(imwb_imgui STATIC
    src/libraries/imgui/imgui.cpp
    src/libraries/imgui/imgui_draw.cpp
    src/libraries/imgui/imgui_tables.cpp
    src/libraries/imgui/imgui_widgets.cpp)
target_include_directories(imwb_imgui PUBLIC src/libraries/imgui)
target_compile_definitions(imwb_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)
set_target_properties(imwb_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Core libraries
# ---------------------------------------------------------------------------
add_library(imwb_logging STATIC src/logging/log.cpp src/logging/log.h)
target_include_directories(imwb_logging PUBLIC src)

add_library(imwb_config STATIC src/config/config.cpp src/config/config.h)
target_include_directories(imwb_config PUBLIC src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(imwb_config PUBLIC imwb_logging)

add_library(imwb_platform STATIC
    src/platform/window.cpp
    src/platform/window.h
    src/libraries/imgui/backends/imgui_impl_sdl3.cpp)
target_include_directories(imwb_platform PUBLIC src)
target_link_libraries(imwb_platform PUBLIC
    imwb_imgui imwb_logging imwb_config SDL3::SDL3)

add_library(imwb_input STATIC
    src/input/keymap.cpp
    src/input/keymap.h
    src/input/input_translator.cpp
    src/input/input_translator.h
    src/input/xkb_layout.cpp
    src/input/xkb_layout.h)
target_include_directories(imwb_input PUBLIC src)
target_link_libraries(imwb_input PUBLIC
    imwb_logging SDL3::SDL3 PkgConfig::WPE PkgConfig::XKBCOMMON)

add_library(imwb_rendering STATIC
    src/rendering/renderer.h
    src/rendering/webview_frame.h
    src/rendering/egl_helpers.cpp
    src/rendering/egl_helpers.h)
target_include_directories(imwb_rendering PUBLIC src)
target_link_libraries(imwb_rendering PUBLIC imwb_logging PkgConfig::EGL)

add_library(imwb_webkit STATIC
    src/webkit/wpe_module.cpp
    src/webkit/wpe_module.h
    src/webkit/web_settings.cpp
    src/webkit/web_settings.h
    src/webkit/web_page.cpp
    src/webkit/web_page.h)
target_include_directories(imwb_webkit PUBLIC src)
target_link_libraries(imwb_webkit PUBLIC
    imwb_logging imwb_config imwb_rendering
    PkgConfig::WPE PkgConfig::WPE_FDO PkgConfig::WPE_WEBKIT PkgConfig::GLIB
    PkgConfig::WAYLAND_SERVER)

add_library(imwb_browser STATIC src/browser/browser.cpp src/browser/browser.h)
target_include_directories(imwb_browser PUBLIC src)
target_link_libraries(imwb_browser PUBLIC
    imwb_webkit imwb_config imwb_logging)

add_library(imwb_ui STATIC src/ui/browser_ui.cpp src/ui/browser_ui.h)
target_include_directories(imwb_ui PUBLIC src)
target_link_libraries(imwb_ui PUBLIC
    imwb_imgui imwb_browser imwb_config imwb_logging)

# ---------------------------------------------------------------------------
# Renderer backends
# ---------------------------------------------------------------------------
if(IMWEBBROWSER_ACTIVE_BACKEND STREQUAL "opengles")
    add_library(imwb_renderer_gl STATIC
        src/rendering/opengles/gl_renderer.cpp
        src/rendering/opengles/gl_renderer.h
        src/libraries/imgui/backends/imgui_impl_opengl3.cpp)
    target_include_directories(imwb_renderer_gl PUBLIC src)
    target_compile_definitions(imwb_renderer_gl PUBLIC IMGUI_IMPL_OPENGL_ES3=1)
    target_link_libraries(imwb_renderer_gl PUBLIC
        imwb_rendering imwb_imgui imwb_platform imwb_logging
        PkgConfig::GLESV2 PkgConfig::EGL)
else()
    add_library(imwb_renderer_vk STATIC
        src/rendering/vulkan/vulkan_renderer.cpp
        src/rendering/vulkan/vulkan_renderer.h
        src/rendering/vulkan/webview_import.cpp
        src/rendering/vulkan/webview_import.h
        src/libraries/imgui/backends/imgui_impl_vulkan.cpp)
    target_include_directories(imwb_renderer_vk PUBLIC src)
    target_link_libraries(imwb_renderer_vk PUBLIC
        imwb_rendering imwb_imgui imwb_platform imwb_logging
        Vulkan::Vulkan PkgConfig::EGL)
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(imwebbrowser
    src/main.cpp
    src/application/application.cpp
    src/application/application.h)

string(TOUPPER "${IMWEBBROWSER_ACTIVE_BACKEND}" _UPPER_BACKEND)
target_compile_definitions(imwebbrowser PRIVATE
    IMWEBBROWSER_BACKEND_${_UPPER_BACKEND}=1)

target_include_directories(imwebbrowser PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(imwebbrowser PRIVATE
    imwb_logging
    imwb_config
    imwb_platform
    imwb_input
    imwb_rendering
    imwb_webkit
    imwb_browser
    imwb_ui)

if(IMWEBBROWSER_ACTIVE_BACKEND STREQUAL "opengles")
    target_link_libraries(imwebbrowser PRIVATE imwb_renderer_gl)
else()
    target_link_libraries(imwebbrowser PRIVATE imwb_renderer_vk)
endif()

# Compiler warnings for first-party code only.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(_target imwb_logging imwb_config imwb_platform imwb_input
                    imwb_rendering imwb_webkit imwb_browser imwb_ui imwebbrowser)
        target_compile_options(${_target} PRIVATE -Wall -Wextra)
    endforeach()
endif()

# Runtime search path for shared libraries (WPE helpers, SDL).
set_target_properties(imwebbrowser PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH ON)

if(IMWEBBROWSER_INSTALL)
    install(TARGETS imwebbrowser RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "ImWebBrowser ${PROJECT_VERSION} - configuration summary")
message(STATUS "  Build type         : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Target triple      : ${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_CROSSCOMPILING)
    message(STATUS "  Cross-compiling    : yes")
else()
    message(STATUS "  Cross-compiling    : no")
endif()
message(STATUS "  C compiler         : ${CMAKE_C_COMPILER}")
message(STATUS "  C++ compiler       : ${CMAKE_CXX_COMPILER}")
message(STATUS "  Backend            : ${IMWEBBROWSER_ACTIVE_BACKEND}")
message(STATUS "  WPE WebKit         : ${WPE_WEBKIT_PC} (${WPE_WEBKIT_VERSION})")
message(STATUS "  Startup URL        : ${IMWEBBROWSER_STARTUP_URL}")
message(STATUS "  Window size        : ${IMWEBBROWSER_WINDOW_WIDTH}x${IMWEBBROWSER_WINDOW_HEIGHT}")
message(STATUS "  Features")
message(STATUS "    JavaScript       : ${IMWEBBROWSER_ENABLE_JAVASCRIPT}")
message(STATUS "    WebGL            : ${IMWEBBROWSER_ENABLE_WEBGL}")
message(STATUS "    WebRTC           : ${IMWEBBROWSER_ENABLE_WEBRTC}")
message(STATUS "    Media            : ${IMWEBBROWSER_ENABLE_MEDIA}")
message(STATUS "    Media stream     : ${IMWEBBROWSER_ENABLE_MEDIA_STREAM}")
message(STATUS "    Web audio        : ${IMWEBBROWSER_ENABLE_WEB_AUDIO}")
message(STATUS "    Fullscreen API   : ${IMWEBBROWSER_ENABLE_FULLSCREEN}")
message(STATUS "    Developer extras : ${IMWEBBROWSER_ENABLE_DEVELOPER_EXTRAS}")
message(STATUS "    Sandbox          : ${IMWEBBROWSER_ENABLE_SANDBOX}")
message(STATUS "    Smooth scrolling : ${IMWEBBROWSER_ENABLE_SMOOTH_SCROLLING}")
message(STATUS "  Trace logging     : ${IMWEBBROWSER_TRACE_LOGGING}")
message(STATUS "")
message(STATUS "Build with: cmake --build <build dir> --parallel 1")
