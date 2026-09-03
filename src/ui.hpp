#pragma once

// Dear ImGui browser chrome: toolbar (Back/Forward/Reload/Kiosk/URL/Progress)
// and an optional stats overlay.

class Browser;

namespace ui {

inline constexpr float kToolbarHeight = 25.f;  // logical pixels

enum class Action { None, ToggleKiosk };

Action drawToolbar(Browser& browser, bool kiosk);
void drawStatsOverlay(bool show, const Browser& browser);

// Debug-only hardware guardrails (no-ops in release builds): ImWebBrowser is
// hardware-only, so silent software fallbacks must become loud messages.
//   reportRenderer()      — GL renderer token-check (llvmpipe/softpipe/...)
//   addHardwareWarning()  — any other fallback (e.g. missing HW decoder)
//   drawHardwareWarnings()— red banner(s), call once per frame
void reportRenderer(const char* renderer);
void addHardwareWarning(const char* text);
void drawHardwareWarnings();

}  // namespace ui
