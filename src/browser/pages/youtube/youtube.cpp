#include "youtube.h"
#include "imgui.h"

static char youtube_API_key[50] = {0};
static int youtube_resolution_index = 0;
const static char* youtube_resolution_list[] = {"144p", "240p", "360p", "480p", "720p", "1080p"};

void show_youtube_search_window(bool* display_youtube_search_window){
    if(ImGui::Begin("Youtube search", display_youtube_search_window)){
        ImGui::End();
    }
}

void show_youtube_configuration_window(bool* display_youtube_configuration_window){
   if(ImGui::Begin("Youtube configuration", display_youtube_configuration_window)){
        ImGui::Text("Youtube API key:");
        ImGui::InputText("##Youtube_API_key", youtube_API_key, sizeof(youtube_API_key), ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_Password);
        ImGui::Text("Youtube resolution:");
        ImGui::Combo("##youtube_resolution", &youtube_resolution_index, youtube_resolution_list, IM_ARRAYSIZE(youtube_resolution_list));
        if(ImGui::Button("Save configurations")){

        }
        ImGui::End();
    }
}

const int get_youtube_configuration_resolution_index(){
    return youtube_resolution_index;
}

const char** get_youtube_configuration_resolution_list(){
    return youtube_resolution_list;
}

const char* get_youtube_api_key(){
    return youtube_API_key;
}