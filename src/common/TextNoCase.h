#pragma once

// Text comparisons that ignore ASCII case (the same in every locale).

#include <cstddef>
#include <string_view>

namespace fr {

constexpr char LowerAscii(char p_Char) {
    return p_Char >= 'A' && p_Char <= 'Z' ? static_cast<char>(p_Char - 'A' + 'a') : p_Char;
}

constexpr bool EqualsNoCase(std::string_view p_A, std::string_view p_B) {
    if (p_A.size() != p_B.size())
        return false;

    for (size_t i = 0; i < p_A.size(); ++i) {
        if (LowerAscii(p_A[i]) != LowerAscii(p_B[i]))
            return false;
    }

    return true;
}

constexpr bool StartsWithNoCase(std::string_view p_Text, std::string_view p_Prefix) {
    return p_Text.size() >= p_Prefix.size() && EqualsNoCase(p_Text.substr(0, p_Prefix.size()), p_Prefix);
}

// Where p_Part first occurs in p_Text at or after p_From; npos when it does
// not, when p_Part is empty, or when p_From is past the end.
constexpr size_t FindNoCase(std::string_view p_Text, std::string_view p_Part, size_t p_From = 0) {
    if (p_Part.empty() || p_From > p_Text.size() || p_Text.size() - p_From < p_Part.size())
        return std::string_view::npos;

    for (size_t i = p_From; i + p_Part.size() <= p_Text.size(); ++i) {
        if (EqualsNoCase(p_Text.substr(i, p_Part.size()), p_Part))
            return i;
    }

    return std::string_view::npos;
}

// p_Text holds p_Part (always true for an empty p_Part).
constexpr bool ContainsNoCase(std::string_view p_Text, std::string_view p_Part) {
    return p_Part.empty() || FindNoCase(p_Text, p_Part) != std::string_view::npos;
}

} // namespace fr
