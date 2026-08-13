/* ImWebBrowser - ImGui browser UI. */

#include "ui/browser_ui.h"

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "browser/browser.h"
#include "config/config.h"
#include "logging/log.h"

namespace imwb {

namespace {

constexpr int kAddressBufferSize = 4096;

/* Completes a user-typed address into a URL. */
std::string complete_url(const std::string& input)
{
    std::string url = input;
    if (url.empty())
        return url;

    /* Trim whitespace. */
    size_t start = url.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    url = url.substr(start);

    const bool has_scheme = url.find("://") != std::string::npos ||
                            url.rfind("about:", 0) == 0 || url.rfind("data:", 0) == 0 ||
                            url.rfind("file:", 0) == 0;
    if (!has_scheme) {
        /* A dotted or local host is a bare domain; otherwise assume a
         * single search term. */
        const bool looks_like_domain = url.find('.') != std::string::npos;
        if (looks_like_domain)
            url = "https://" + url;
    }
    return url;
}

} /* namespace */

void BrowserUi::draw(Browser& browser, const WebViewTexture& texture)
{
    ImGuiIO& io = ImGui::GetIO();

    /* In kiosk mode the web view fills the whole window; no toolbar. */
    const float toolbar_height = m_kiosk ? 0.0f : (ImGui::GetFrameHeight() + 4.0f);
    const float viewport_y = toolbar_height;
    const float viewport_height = io.DisplaySize.y - viewport_y;

    if (!m_kiosk)
        draw_toolbar(browser);

    if (viewport_height > 0.0f) {
        /* Draw the web view into the background draw list: it must not be
         * clipped by an ImGui window. */
        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
        const ImVec2 view_min(0.0f, viewport_y);
        const ImVec2 view_max(io.DisplaySize.x, io.DisplaySize.y);
        if (texture.valid) {
            draw_list->AddImage(ImTextureRef(static_cast<ImTextureID>(texture.handle)),
                                view_min, view_max);
        } else {
            const ImVec2 text_pos(io.DisplaySize.x * 0.5f - 60.0f,
                                  viewport_y + viewport_height * 0.5f - 10.0f);
            draw_list->AddText(text_pos, IM_COL32(210, 210, 210, 255), "Loading...");
        }
    }

    /* Report the web view viewport to the browser. */
    WebViewViewport viewport{};
    viewport.x = 0;
    viewport.y = static_cast<int>(viewport_y);
    viewport.width = static_cast<int>(io.DisplaySize.x);
    viewport.height = static_cast<int>(viewport_height);
    viewport.scale = io.DisplayFramebufferScale.x;
    browser.set_viewport(viewport);
}

void BrowserUi::draw_toolbar(Browser& browser)
{
    static char url_buffer[kAddressBufferSize] = {0};

    WebPage& page = browser.page();

    /* Keep the address bar in sync with the page unless the user is editing. */
    if (!m_address_editing) {
        const std::string page_uri = page.uri();
        if (page_uri != m_last_synced_uri) {
            std::snprintf(url_buffer, kAddressBufferSize, "%s", page_uri.c_str());
            m_last_synced_uri = page_uri;
        }
    }

    if (m_focus_address_bar) {
        ImGui::SetKeyboardFocusHere();
        m_focus_address_bar = false;
    }

    ImGui::BeginMainMenuBar();

    /* Navigation buttons. */
    if (ImGui::MenuItem("Back", nullptr, false, page.can_go_back()))
        page.go_back();
    if (ImGui::MenuItem("Fwd", nullptr, false, page.can_go_forward()))
        page.go_forward();
    if (ImGui::MenuItem("Reload"))
        page.is_loading() ? page.stop() : page.reload();
    if (ImGui::MenuItem("Kiosk"))
        m_kiosk_requested = true;

    /* Status: a fixed-width loading-progress percentage. The page title is
     * shown only in the window frame (OS titlebar), not here. */
    float status_w = 0.0f;
    std::string status;
    const bool loading = page.is_loading();
    const double progress = page.load_progress();
    if (loading || progress >= 1.0) {
        char pct[16] = {0};
        std::snprintf(pct, sizeof(pct), "%.0f%%", progress * 100.0f);
        status = pct;
        status_w = ImGui::CalcTextSize("100%").x + ImGui::GetStyle().ItemSpacing.x;
    }

    ImGui::Separator();
    ImGui::PushItemWidth(-status_w);
    const bool submitted = ImGui::InputText("##address", url_buffer, kAddressBufferSize,
                                            ImGuiInputTextFlags_EnterReturnsTrue |
                                                ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemFocused())
        m_address_editing = true;
    else if (m_address_editing)
        m_address_editing = false;
    ImGui::PopItemWidth();

    if (submitted && url_buffer[0] != '\0') {
        const std::string url = complete_url(url_buffer);
        if (!url.empty())
            page.load_uri(url);
        m_address_editing = false;
        m_last_synced_uri = url;
    }

    /* Progress percentage in a fixed-width slot; empty when idle. */
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    if (!status.empty())
        ImGui::TextUnformatted(status.c_str());
    ImGui::PopStyleColor();

    ImGui::EndMainMenuBar();
}

} /* namespace imwb */
