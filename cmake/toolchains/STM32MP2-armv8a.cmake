# STM32MP2 (Arm Cortex-A35) cross-compilation toolchain for ImWebBrowser.
#
# ImWebBrowser targets Linux on STMicroelectronics STM32MP2 boards
# (STM32MP25x, Cortex-A35, VeriSilicon GC7000 GPU). The build runs on an x86-64 host
# and produces an aarch64 binary that runs on the board's OpenSTLinux BSP.
#
# Two ways to use this file:
#
#   Option A - Yocto SDK from OpenSTLinux (recommended)
#
#     The SDK's environment-setup script exports OECORE_TARGET_SYSROOT and a
#     target pkg-config, so just source it and pass the toolchain file:
#
#         source /opt/st/stm32mp2/4.1/environment-setup-cortexa35-poky-linux
#         cmake -S . -B build-stm32mp2 \
#             -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/STM32MP2-armv8a.cmake \
#             -DIMWEBBROWSER_BACKEND_OPENGL_ES=ON
#         cmake --build build-stm32mp2 --parallel 1
#
#   Option B - standalone aarch64 toolchain + sysroot
#
#         cmake -S . -B build-stm32mp2 \
#             -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/STM32MP2-armv8a.cmake \
#             -DSTM32MP2_SYSROOT=/path/to/stm32mp2/sysroot \
#             -DSTM32MP2_CROSS_COMPILE=/usr/bin/aarch64-linux-gnu-
#
# The target sysroot must contain the runtime dependencies (see README):
#   libsdl3, libwpewebkit-2.0, wpebackend-fdo, libwpe, glib, wayland-server,
#   xkbcommon, libegl, libglesv2 (Mesa/etnaviv for the VeriSilicon GC7000).

set(STM32MP2_SYSROOT "" CACHE PATH "STM32MP2 target sysroot")
set(STM32MP2_CROSS_COMPILE "" CACHE STRING "Cross compiler prefix (e.g. aarch64-linux-gnu- or aarch64-ostl-linux-gnu-)")

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Resolve the sysroot: explicit option, else the Yocto SDK environment.
if(NOT STM32MP2_SYSROOT)
    if(DEFINED ENV{OECORE_TARGET_SYSROOT} AND NOT "$ENV{OECORE_TARGET_SYSROOT}" STREQUAL "")
        set(STM32MP2_SYSROOT "$ENV{OECORE_TARGET_SYSROOT}")
    elseif(DEFINED ENV{SDKTARGETSYSROOT} AND NOT "$ENV{SDKTARGETSYSROOT}" STREQUAL "")
        set(STM32MP2_SYSROOT "$ENV{SDKTARGETSYSROOT}")
    else()
        message(FATAL_ERROR
            "STM32MP2: no sysroot. Pass -DSTM32MP2_SYSROOT=<path> or build "
            "inside a sourced Yocto SDK environment.")
    endif()
endif()

if(NOT STM32MP2_CROSS_COMPILE)
    if(DEFINED ENV{CROSS_COMPILE} AND NOT "$ENV{CROSS_COMPILE}" STREQUAL "")
        set(STM32MP2_CROSS_COMPILE "$ENV{CROSS_COMPILE}")
    else()
        set(STM32MP2_CROSS_COMPILE "aarch64-linux-gnu-")
    endif()
endif()

set(CMAKE_SYSROOT "${STM32MP2_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${STM32MP2_SYSROOT}")

set(CMAKE_C_COMPILER "${STM32MP2_CROSS_COMPILE}gcc")
set(CMAKE_CXX_COMPILER "${STM32MP2_CROSS_COMPILE}g++")
set(CMAKE_ASM_COMPILER "${STM32MP2_CROSS_COMPILE}gcc")

# Only look in the sysroot for libraries/includes/packages, never on the host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Cross pkg-config: the Yocto SDK exports one; otherwise use any on PATH.
find_program(PKG_CONFIG_EXECUTABLE NAMES pkg-config)
if(NOT PKG_CONFIG_EXECUTABLE)
    message(FATAL_ERROR "STM32MP2: no pkg-config found. The Yocto SDK provides one.")
endif()

set(ENV{PKG_CONFIG_SYSROOT_DIR} "${STM32MP2_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR}
    "${STM32MP2_SYSROOT}/usr/lib/pkgconfig:${STM32MP2_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${STM32MP2_SYSROOT}/usr/share/pkgconfig")
