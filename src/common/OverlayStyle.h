#pragma once

// Look-and-feel values the overlay's Status and Settings tabs share.

#include <imgui.h>

// Amber: the Status tab's notable rows, the Settings tab's pending changes.
inline constexpr ImVec4 kNotableColor(1.00f, 0.75f, 0.20f, 1.00f);

// Tooltips wrap at this many font sizes, so they fit on the Deck's screen.
inline constexpr float kTooltipWrapEms = 28.0f;
