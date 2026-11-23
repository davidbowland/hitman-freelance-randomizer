#pragma once

// Word wrapping that never splits a word (pure; WrappedText.h draws it). A
// line breaks only at a space (or a '\n'); a word wider than the line gets a
// line of its own. Each candidate line is measured whole with the function
// that draws it, so what fits here fits on screen. ImGui's own wrapping can
// move the last letter of a word to the next line.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fr {

// p_Text in lines no wider than p_Width by p_Measure (a string's width),
// broken at spaces only. The spaces at a break are dropped; a line keeps its
// inner spacing and a paragraph its leading spaces. '\n' always breaks; an
// empty paragraph is an empty line. At least one line, even for "".
template <class Measure>
std::vector<std::string_view> WrapWords(std::string_view p_Text, float p_Width, Measure&& p_Measure) {
    std::vector<std::string_view> s_Lines;
    size_t s_Para = 0;

    while (true) {
        const size_t s_Newline = p_Text.find('\n', s_Para);
        const size_t s_ParaEnd = s_Newline == std::string_view::npos ? p_Text.size() : s_Newline;
        size_t s_LineStart = s_Para; // the current line's first character
        size_t s_LineEnd = s_Para;   // the end of its last word (nothing yet when == s_LineStart)
        size_t s_Pos = s_Para;

        // The leading spaces stay with the paragraph's first word.
        while (s_Pos < s_ParaEnd && p_Text[s_Pos] == ' ')
            ++s_Pos;

        while (s_Pos < s_ParaEnd) {
            size_t s_WordEnd = s_Pos;

            while (s_WordEnd < s_ParaEnd && p_Text[s_WordEnd] != ' ')
                ++s_WordEnd;

            if (s_LineEnd == s_LineStart) {
                // The line's first word: it goes here whatever its width.
                s_LineEnd = s_WordEnd;
            }
            else if (p_Measure(p_Text.substr(s_LineStart, s_WordEnd - s_LineStart)) <= p_Width) {
                s_LineEnd = s_WordEnd;
            }
            else {
                s_Lines.push_back(p_Text.substr(s_LineStart, s_LineEnd - s_LineStart));
                s_LineStart = s_Pos;
                s_LineEnd = s_WordEnd;
            }

            s_Pos = s_WordEnd;

            while (s_Pos < s_ParaEnd && p_Text[s_Pos] == ' ')
                ++s_Pos;
        }

        s_Lines.push_back(p_Text.substr(s_LineStart, s_LineEnd - s_LineStart));

        if (s_Newline == std::string_view::npos)
            break;

        s_Para = s_Newline + 1;
    }

    return s_Lines;
}

// The wrapped lines of the texts drawn lately, so a paragraph drawn every
// frame is measured again only when its text, width or font changes (a window
// being resized re-wraps each frame, as ImGui's own wrapping does anyway).
// Keyed by the text's contents (a caller's string may be a fresh copy each
// frame); a line is kept as its offset and size in the text. Holds at most
// kMaxEntries texts and starts over when full (a text that changes every
// second, a countdown, adds one each time). One thread (the render thread).
class WrapCache {
public:
    struct Line {
        size_t start;
        size_t size;

        bool operator==(const Line&) const = default;
    };

    static constexpr size_t kMaxEntries = 1024;

    // p_Text's lines at p_Width (WrapWords), measured by p_Measure only when
    // the text, the width or the font (p_Font, any id of it; p_FontSize) is
    // new. The reference is valid until the next Get.
    template <class Measure>
    const std::vector<Line>& Get(std::string_view p_Text, float p_Width, const void* p_Font, float p_FontSize,
                                 Measure&& p_Measure) {
        auto s_It = m_Entries.find(p_Text);

        if (s_It != m_Entries.end() && s_It->second.width == p_Width && s_It->second.font == p_Font &&
            s_It->second.fontSize == p_FontSize)
            return s_It->second.lines;

        if (s_It == m_Entries.end()) {
            if (m_Entries.size() >= kMaxEntries)
                m_Entries.clear();

            s_It = m_Entries.emplace(std::string(p_Text), Entry {}).first;
        }

        Entry& s_Entry = s_It->second;
        s_Entry.width = p_Width;
        s_Entry.font = p_Font;
        s_Entry.fontSize = p_FontSize;
        s_Entry.lines.clear();

        for (const std::string_view s_Line : WrapWords(p_Text, p_Width, p_Measure))
            s_Entry.lines.push_back({static_cast<size_t>(s_Line.data() - p_Text.data()), s_Line.size()});

        return s_Entry.lines;
    }

    size_t Size() const { return m_Entries.size(); }

private:
    struct Entry {
        float width = 0.0f;
        const void* font = nullptr;
        float fontSize = 0.0f;
        std::vector<Line> lines;
    };

    // Looked up by a string_view without building a string.
    struct Hash {
        using is_transparent = void;

        size_t operator()(std::string_view p_Text) const { return std::hash<std::string_view> {}(p_Text); }
    };

    std::unordered_map<std::string, Entry, Hash, std::equal_to<>> m_Entries;
};

} // namespace fr
