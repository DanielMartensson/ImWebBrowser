#pragma once
#include <stdbool.h>

void show_youtube_search_window(bool* display_youtube_search_window);
void show_youtube_configuration_window(bool* display_youtube_configuration_window);
const int get_youtube_configuration_resolution_index();
const char** get_youtube_configuration_resolution_list();
const char* get_youtube_api_key();