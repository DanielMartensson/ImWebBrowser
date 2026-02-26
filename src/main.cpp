#include "main.h"

int main(int, char**){
    int status = 0;
    #ifdef USE_VULKAN
        status = vulkan_run();
    #else
        status = opengl_run();
    #endif

    return status;
}