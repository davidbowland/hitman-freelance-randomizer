#pragma once

// Wrapped text for the overlays, broken at spaces only (fr::WrapWords). Use
// WrappedTextAvail in place of ImGui::TextWrapped, which can break inside a
// word. The lines are one group, so IsItemHovered() covers the whole text, and
// the lines after the first start where the first did (after a SameLine too).

#include "WordWrap.h"

#include <imgui.h>

#include <algorithm>
#include <string_view>

// The room from the cursor to p_LocalX (window-local, as PushTextWrapPos takes it).
inline float WrapWidthTo(float p_LocalX) {
    return std::max(p_LocalX - ImGui::GetCursorPosX(), 1.0f);
}

// p_Text wrapped to p_Width; p_Color: the text color (nullptr: the default).
inline void WrappedText(std::string_view p_Text, float p_Width, const ImVec4* p_Color = nullptr) {
    const auto s_Measure = [](std::string_view p_Line) {
        return ImGui::CalcTextSize(p_Line.data(), p_Line.data() + p_Line.size()).x;
    };

    if (p_Color)
        ImGui::PushStyleColor(ImGuiCol_Text, *p_Color);

    // Off for these lines: a wrap position pushed around the call (a tooltip's) would wrap again.
    ImGui::PushTextWrapPos(-1.0f);
    ImGui::BeginGroup();

    // Measured again only when the text, the width or the font changed (render thread only).
    static fr::WrapCache s_Cache;

    for (const fr::WrapCache::Line& s_Line :
         s_Cache.Get(p_Text, p_Width, ImGui::GetFont(), ImGui::GetFontSize(), s_Measure))
        ImGui::TextUnformatted(p_Text.data() + s_Line.start, p_Text.data() + s_Line.start + s_Line.size);

    ImGui::EndGroup();
    ImGui::PopTextWrapPos();

    if (p_Color)
        ImGui::PopStyleColor();
}

// Wrapped at the window's (or the column's) right edge.
inline void WrappedTextAvail(std::string_view p_Text, const ImVec4* p_Color = nullptr) {
    WrappedText(p_Text, std::max(ImGui::GetContentRegionAvail().x, 1.0f), p_Color);
}

// In the disabled text color.
inline void WrappedTextDisabled(std::string_view p_Text, float p_Width) {
    const ImVec4 s_Color = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
    WrappedText(p_Text, p_Width, &s_Color);
}
