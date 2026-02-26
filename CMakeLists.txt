cmake_minimum_required(VERSION 4.1.2)
project(ImWebBrowser LANGUAGES CXX)

# Globba alla .cpp i src + imgui + backends
file(GLOB_RECURSE ALL_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/libraries/imgui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/libraries/imgui/backends/*.cpp"
)

add_executable(ImWebBrowser ${ALL_SOURCE_FILES})

# Include paths
target_include_directories(ImWebBrowser PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/libraries/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/libraries/imgui/backends"
)

# Kompilera med Vulkan - Annars OpenGL3
#target_compile_definitions(ImWebBrowser PRIVATE USE_VULKAN)

# Länka mot SDL3
find_library(LIBSDL3_LIBRARY NAMES SDL3)
if (LIBSDL3_LIBRARY)
    message(STATUS "SDL3 library found")
    target_link_libraries(ImWebBrowser PRIVATE ${LIBSDL3_LIBRARY})
    if(WIN32)
        target_link_libraries(ImWebBrowser PRIVATE   
        imm32      # För IME-stöd
        winmm      # För timeBeginPeriod / timeEndPeriod
        version    # För GetFileVersionInfo*
        )
    endif()
else()
    message(FATAL_ERROR "SDL3 library not found")
endif()

# Länka mot OpenGL
find_package(OpenGL REQUIRED)
if (OpenGL_FOUND)
    message(STATUS "OpenGL library found")
    target_link_libraries(ImWebBrowser PRIVATE OpenGL::GL)
    target_include_directories(ImWebBrowser PRIVATE ${OPENGL_INCLUDE_DIR})
else()
    message(FATAL_ERROR "OpenGL library not found")
endif()

# Länka mot Vulkan
find_package(Vulkan REQUIRED)
if(Vulkan_FOUND)
    message(STATUS "Vulkan library found")
    target_link_libraries(ImWebBrowser PRIVATE Vulkan::Vulkan)
else()
    message(FATAL_ERROR "Vulkan library not found")
endif()

# C++ standard
set_target_properties(ImWebBrowser PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
)