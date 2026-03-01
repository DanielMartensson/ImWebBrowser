#include "main.h"
#include "imgui.h"
#include "pages/pages.h"

static bool display_youtube_search_window = false;
static bool display_youtube_configuration_window = false;

void im_web_browser(bool* exit_browser){
    if (ImGui::BeginMainMenuBar()){
        if (ImGui::BeginMenu("Program")){
            if (ImGui::MenuItem("Exit")){
                *exit_browser = true;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Video")){
            if (ImGui::BeginMenu("Youtube")){
                if(ImGui::MenuItem("Search", nullptr)){
                    display_youtube_search_window = true;
                }
                if(ImGui::MenuItem("Configuration", nullptr)){
                    display_youtube_configuration_window = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if(display_youtube_search_window){
        show_youtube_search_window(&display_youtube_search_window);
    }
    if(display_youtube_configuration_window){
        show_youtube_configuration_window(&display_youtube_configuration_window);
    }
}