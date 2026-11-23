#pragma once

// Text that must stay on one line (a combo's choice, a fixed-width table
// cell): shortened at a word with "..." instead of being cut off mid-word at
// the edge. Show the whole text somewhere else (a tooltip) when it is
// shortened. Text that may take more lines is better wrapped (WrappedText.h).

#include <imgui.h>

#include <cstddef>
#include <string>

// p_Text cut at a word to fit p_Width, with "..." (the whole text when it fits).
inline std::string FitText(const std::string& p_Text, float p_Width) {
    if (ImGui::CalcTextSize(p_Text.c_str()).x <= p_Width)
        return p_Text;

    const float s_Dots = ImGui::CalcTextSize("...").x;
    std::string s_Cut = p_Text;

    while (!s_Cut.empty() && ImGui::CalcTextSize(s_Cut.c_str()).x + s_Dots > p_Width) {
        const size_t s_Space = s_Cut.find_last_of(' ');

        if (s_Space != std::string::npos && s_Space > 0) {
            s_Cut.resize(s_Space);
            continue;
        }

        // A single word too long to fit whole loses letters instead (whole UTF-8 characters).
        while (!s_Cut.empty() && (static_cast<unsigned char>(s_Cut.back()) & 0xC0) == 0x80)
            s_Cut.pop_back();

        if (!s_Cut.empty())
            s_Cut.pop_back();
    }

    while (!s_Cut.empty() && (s_Cut.back() == ' ' || s_Cut.back() == ':' || s_Cut.back() == ',' || s_Cut.back() == ';'))
        s_Cut.pop_back();

    return s_Cut + "...";
}
