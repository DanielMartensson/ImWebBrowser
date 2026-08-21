#include "ui.hpp"

#include "browser.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // ClearActiveID

#include <cstdio>

namespace ui {

Action drawToolbar(Browser& b, bool kiosk)
{
    if (kiosk)
        return Action::None;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, kToolbarHeight));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::Begin("##toolbar", nullptr, flags);

    // Back / Forward / Reload
    ImGui::BeginDisabled(!b.canGoBack());
    if (ImGui::Button("<"))
        b.goBack();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!b.canGoForward());
    ImGui::SameLine();
    if (ImGui::Button(">"))
        b.goForward();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(b.loading ? "X" : "R"))
        b.reload(false);

    // Kiosk toggle
    ImGui::SameLine();
    Action action = Action::None;
    if (ImGui::Button("[ ]"))
        action = Action::ToggleKiosk;

    // URL field: Enter navigates; WebKit updates the buffer via notify::uri.
    constexpr float kProgressWidth = 150.f;
    const float urlWidth = ImGui::GetContentRegionAvail().x - kProgressWidth - ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(urlWidth);
    // Clicking into the bar (or Ctrl+L) clears the old URL so whatever the
    // user types becomes the whole new address instead of being appended.
    // (ImGui's AutoSelectAll is unreliable here: a mouse click re-positions
    // the caret via stb_textedit_click and drops the selection.)
    const ImVec2 urlPos = ImGui::GetCursorScreenPos();
    const bool clickIntoBar =
        !b.urlEditing && ImGui::IsWindowHovered() &&
        ImGui::IsMouseHoveringRect(urlPos, ImVec2(urlPos.x + urlWidth, urlPos.y + ImGui::GetFrameHeight())) &&
        ImGui::IsMouseClicked(0);
    if (clickIntoBar || b.focusUrlRequest)
        b.urlBuf[0] = '\0';
    if (b.focusUrlRequest) {
        ImGui::SetKeyboardFocusHere();
        b.focusUrlRequest = false;
    }
    if (ImGui::InputText("##url", b.urlBuf, Browser::kUrlBufSize, ImGuiInputTextFlags_EnterReturnsTrue)) {
        b.loadUrl(b.urlBuf);
        ImGui::ClearActiveID();  // hand keyboard focus back to the page
    }
    b.urlEditing = ImGui::IsItemActive();

    // Load progress 0-100%
    char overlay[16];
    snprintf(overlay, sizeof(overlay), "%3.0f%%", b.progress * 100.f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kProgressWidth);
    ImGui::ProgressBar(b.progress, ImVec2(kProgressWidth, 0), overlay);

    ImGui::End();
    ImGui::PopStyleVar(2);
    return action;
}

void drawStatsOverlay(bool show, const Browser& b)
{
    if (!show)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(8, kToolbarHeight + 8), ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove;
    ImGui::Begin("##stats", nullptr, flags);
    ImGui::Text("loop fps   %6.1f", double(io.Framerate));
    ImGui::Text("frame time %6.2f ms", io.Framerate > 0 ? 1000.0 / double(io.Framerate) : 0.0);
    ImGui::Text("window     %d x %d", int(io.DisplaySize.x), int(io.DisplaySize.y));
    ImGui::Text("web view   %d x %d", b.viewWidth(), b.viewHeight());
    ImGui::Text("uri        %s", b.urlBuf);
    ImGui::End();
}

}  // namespace ui
