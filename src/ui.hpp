#pragma once

// Dear ImGui browser chrome: toolbar (Back/Forward/Reload/Kiosk/URL/Progress)
// and an optional stats overlay.

class Browser;

namespace ui {

inline constexpr float kToolbarHeight = 34.f;  // logical pixels

enum class Action { None, ToggleKiosk };

Action drawToolbar(Browser& browser, bool kiosk);
void drawStatsOverlay(bool show, const Browser& browser);

}  // namespace ui
