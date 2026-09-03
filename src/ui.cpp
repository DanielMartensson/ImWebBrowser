#include "ui.hpp"

#include "browser.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // ClearActiveID

#include <cstdio>
#include <cstring>

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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);  // softly rounded buttons + URL field
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
    // Focusing the bar (mouse click or Ctrl+L) keeps the current text intact
    // so it stays visible while editing; the user can select/delete it as
    // needed. (ImGui's AutoSelectAll is unreliable for a mouse click here:
    // the click re-positions the caret via stb_textedit_click and drops the
    // selection.)
    const ImVec2 urlPos = ImGui::GetCursorScreenPos();
    const bool clickIntoBar =
        !b.urlEditing && ImGui::IsWindowHovered() &&
        ImGui::IsMouseHoveringRect(urlPos, ImVec2(urlPos.x + urlWidth, urlPos.y + ImGui::GetFrameHeight())) &&
        ImGui::IsMouseClicked(0);
    if (clickIntoBar)
        ImGui::SetKeyboardFocusHere();
    else if (b.focusUrlRequest) {
        ImGui::SetKeyboardFocusHere();
        b.focusUrlRequest = false;
    }
    if (ImGui::InputText("##url", b.urlBuf, Browser::kUrlBufSize, ImGuiInputTextFlags_EnterReturnsTrue)) {
        b.loadUrl(b.urlBuf);
        ImGui::ClearActiveID();  // hand keyboard focus back to the page
    }
    b.urlEditing = ImGui::IsItemActive();

    // Load progress: just the number 0-100%, no bar, centered in the space
    // left of the URL input.
    {
        char overlay[16];
        snprintf(overlay, sizeof(overlay), "%3.0f%%", b.progress * 100.f);
        ImGui::SameLine();
        const float avail = ImGui::GetContentRegionAvail().x;
        const float tw = ImGui::CalcTextSize(overlay).x;
        if (avail > tw)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw) * 0.5f);
        ImGui::TextUnformatted(overlay);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
    return action;
}

void drawStatsOverlay(bool show, const Browser& b)
{
    if (!show)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(8, kToolbarHeight + 8), ImGuiCond_FirstUseEver);
    // Draggable via the slim title bar; no close/resize, no saved settings.
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoResize;
    ImGui::Begin("Stats", nullptr, flags);
    ImGui::Text("loop fps   %6.1f", double(io.Framerate));
    ImGui::Text("frame time %6.2f ms", io.Framerate > 0 ? 1000.0 / double(io.Framerate) : 0.0);
    ImGui::Text("window     %d x %d", int(io.DisplaySize.x), int(io.DisplaySize.y));
    ImGui::Text("web view   %d x %d", b.viewWidth(), b.viewHeight());
    ImGui::Text("uri        %s", b.urlBuf);
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Hardware guardrails — DEBUG BUILDS ONLY (empty bodies in release).
//
// The browser is hardware-only by principle: rendering must be a real GPU
// context and video must decode on hardware. These catch the silent software
// fallbacks and make them loud (stderr + on-screen banner): a CPU rasterizer
// behind the GL context (Mesa llvmpipe/softpipe, swrast, SwiftShader) and a
// configured hardware decoder that is missing or outranked by avdec.
// ---------------------------------------------------------------------------
#ifdef IMWB_DEBUG_GUARDRAIL

namespace {
constexpr int kMaxWarnings = 4;
char gWarnings[kMaxWarnings][192];
int gWarningCount = 0;

bool containsToken(const char* haystack, const char* needle)
{
    return haystack && needle && strcasestr(haystack, needle) != nullptr;
}
}  // namespace

void reportRenderer(const char* renderer)
{
    if (!renderer || !renderer[0])
        return;
    if (containsToken(renderer, "llvmpipe") || containsToken(renderer, "softpipe")
        || containsToken(renderer, "swrast") || containsToken(renderer, "swiftshader")) {
        char text[sizeof(gWarnings[0])];
        std::snprintf(text, sizeof(text), "SOFTWARE RENDERING (%s)", renderer);
        addHardwareWarning(text);
        std::fprintf(stderr,
                     "\n*** HARDWARE-ONLY VIOLATION: GL renderer is '%s' (CPU rasterizer) ***\n"
                     "    ImWebBrowser requires a real GPU context (Mesa DRI). The UI blit,\n"
                     "    WebKit compositing and WebGL are ALL running on the CPU now.\n"
                     "    Fix the GPU driver instead of running like this.\n\n",
                     renderer);
    }
}

void addHardwareWarning(const char* text)
{
    if (!text || gWarningCount >= kMaxWarnings)
        return;
    // ONE aggregated popup, never spam: skip exact duplicates — every check
    // runs once at startup and the single window below just re-draws the
    // same stable list each frame (no new windows, no flashing).
    for (int i = 0; i < gWarningCount; ++i)
        if (std::strncmp(gWarnings[i], text, sizeof(gWarnings[0])) == 0)
            return;
    std::snprintf(gWarnings[gWarningCount], sizeof(gWarnings[0]), "%s", text);
    ++gWarningCount;
}

void drawHardwareWarnings()
{
    if (!gWarningCount)
        return;
    // A single fixed banner window top-centre; all collected fallbacks are
    // listed inside it. It never multiplies and never repeats itself.
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, kToolbarHeight + 30.f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::Begin("##hw-warnings", nullptr, flags);
    for (int i = 0; i < gWarningCount; ++i)
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", gWarnings[i]);
    ImGui::TextDisabled("hardware-only: debug guardrail");
    ImGui::End();
}

#else  // release pays nothing

void reportRenderer(const char*) {}
void addHardwareWarning(const char*) {}
void drawHardwareWarnings() {}

#endif  // IMWB_DEBUG_GUARDRAIL

}  // namespace ui
