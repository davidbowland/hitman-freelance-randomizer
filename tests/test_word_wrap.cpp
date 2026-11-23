#include "doctest.h"

#include "WordWrap.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

// One unit per character.
float Chars(std::string_view p_Text) {
    return static_cast<float>(p_Text.size());
}

std::vector<std::string> Wrap(std::string_view p_Text, float p_Width) {
    std::vector<std::string> s_Out;

    for (const std::string_view s_Line : fr::WrapWords(p_Text, p_Width, Chars))
        s_Out.emplace_back(s_Line);

    return s_Out;
}

using Lines = std::vector<std::string>;

} // namespace

TEST_CASE("word wrap: breaks at spaces, never inside a word") {
    // The tooltip that left "thre" / "e" on two lines: every word stays whole at every width.
    const std::string_view s_Help = "Check the common objectives the shuffle must never pick. At least three must "
                                    "stay unchecked; Kill Only Targets and Do Not Pacify count as one, since a "
                                    "campaign never gets both.";
    for (int s_Width = 1; s_Width <= 200; ++s_Width) {
        CAPTURE(s_Width);
        const Lines s_Lines = Wrap(s_Help, static_cast<float>(s_Width));
        std::string s_Joined;

        for (const std::string& s_Line : s_Lines) {
            // A line is wider than the room only when it is one word that cannot fit.
            if (s_Line.size() > static_cast<size_t>(s_Width))
                CHECK(s_Line.find(' ') == std::string::npos);

            CHECK(!s_Line.empty());
            CHECK(s_Line.front() != ' ');
            CHECK(s_Line.back() != ' ');
            s_Joined += (s_Joined.empty() ? "" : " ") + s_Line;
        }

        CHECK(s_Joined == s_Help);
    }
}

TEST_CASE("word wrap: fills each line greedily") {
    CHECK(Wrap("At least three must stay", 14.0f) == Lines {"At least three", "must stay"});
    CHECK(Wrap("At least three must stay", 13.0f) == Lines {"At least", "three must", "stay"});
    CHECK(Wrap("count as one, since", 12.0f) == Lines {"count as", "one, since"});
    CHECK(Wrap("fits", 4.0f) == Lines {"fits"});
    CHECK(Wrap("two words", 9.0f) == Lines {"two words"});
    CHECK(Wrap("two words", 8.0f) == Lines {"two", "words"});
}

TEST_CASE("word wrap: a word wider than the line overflows whole") {
    CHECK(Wrap("a Professional-difficulty b", 5.0f) == Lines {"a", "Professional-difficulty", "b"});
    CHECK(Wrap("assembly:/_pro/scenes/missions/theark/scene.entity", 10.0f) ==
          Lines {"assembly:/_pro/scenes/missions/theark/scene.entity"});
    CHECK(Wrap("three", 0.0f) == Lines {"three"});
}

TEST_CASE("word wrap: spaces and new lines") {
    CHECK(Wrap("", 10.0f) == Lines {""});
    CHECK(Wrap("   ", 10.0f) == Lines {""});
    // Inner spacing kept on a line, dropped at a break; a paragraph keeps its indent.
    CHECK(Wrap("!!  extreme", 20.0f) == Lines {"!!  extreme"});
    CHECK(Wrap("one  two", 4.0f) == Lines {"one", "two"});
    CHECK(Wrap("  indented text", 20.0f) == Lines {"  indented text"});
    CHECK(Wrap("trailing   ", 20.0f) == Lines {"trailing"});
    // '\n' always breaks; an empty paragraph is an empty line.
    CHECK(Wrap("first\nsecond line", 20.0f) == Lines {"first", "second line"});
    CHECK(Wrap("a\n\nb", 20.0f) == Lines {"a", "", "b"});
    CHECK(Wrap("end\n", 20.0f) == Lines {"end", ""});
}

TEST_CASE("word wrap: measures the whole candidate line") {
    // A measure where a line costs more than its words (spacing, kerning): the line is what is checked.
    const auto s_Wide = [](std::string_view p_Text) { return static_cast<float>(p_Text.size()) + 2.0f; };
    std::vector<std::string> s_Lines;

    for (const std::string_view s_Line : fr::WrapWords("aa bb cc", 7.0f, s_Wide))
        s_Lines.emplace_back(s_Line);

    CHECK(s_Lines == Lines {"aa bb", "cc"});
}

TEST_CASE("word wrap: never splits a word that fits on a line, with a proportional font") {
    // Widths per letter as a proportional font's: 'i' and 'l' narrow, 'm' and 'w' wide, a space in between.
    const auto s_Proportional = [](std::string_view p_Text) {
        float s_Width = 0.0f;

        for (const char s_Char : p_Text) {
            if (s_Char == 'i' || s_Char == 'l')
                s_Width += 0.4f;
            else if (s_Char == 'm' || s_Char == 'w')
                s_Width += 1.5f;
            else if (s_Char == ' ')
                s_Width += 0.5f;
            else
                s_Width += 1.0f;
        }

        return s_Width;
    };
    const std::string_view s_Text = "Choosing Exit in a Freelancer mission's pause menu leaves without the usual "
                                    "penalty. Keep your tools when you switch. Freelancer goes straight to the "
                                    "safehouse.";
    std::vector<std::string_view> s_Words;

    for (size_t s_At = 0; s_At < s_Text.size();) {
        const size_t s_End = std::min(s_Text.find(' ', s_At), s_Text.size());
        s_Words.push_back(s_Text.substr(s_At, s_End - s_At));
        s_At = s_End + 1;
    }

    // Every width a window being resized passes through, in steps finer than a letter.
    for (int s_Step = 5; s_Step <= 800; ++s_Step) {
        const float s_Width = static_cast<float>(s_Step) / 10.0f;
        CAPTURE(s_Width);
        size_t s_Word = 0;

        for (const std::string_view s_Line : fr::WrapWords(s_Text, s_Width, s_Proportional)) {
            // Each line is whole words in order, so no word is ever split across two lines.
            size_t s_At = 0;

            while (s_At < s_Line.size()) {
                REQUIRE(s_Word < s_Words.size());
                CHECK(s_Line.substr(s_At, s_Words[s_Word].size()) == s_Words[s_Word]);
                s_At += s_Words[s_Word].size() + 1;
                ++s_Word;
            }

            // Wider than the room only when it is one word too wide for any line.
            if (s_Proportional(s_Line) > s_Width)
                CHECK(s_Line.find(' ') == std::string_view::npos);
        }

        CHECK(s_Word == s_Words.size());
    }
}

TEST_CASE("word wrap cache: measures again only when the text, width or font changes") {
    int s_Calls = 0;
    const auto s_Counting = [&](std::string_view p_Text) {
        ++s_Calls;
        return static_cast<float>(p_Text.size());
    };
    using Line = fr::WrapCache::Line;
    fr::WrapCache s_Cache;
    const int s_Font = 0;
    const int s_OtherFont = 0;

    const std::string s_Text = "At least three must stay";
    CHECK(s_Cache.Get(s_Text, 14.0f, &s_Font, 16.0f, s_Counting) == std::vector<Line> {{0, 14}, {15, 9}});
    const int s_First = s_Calls;
    CHECK(s_First > 0);

    // The same text again, even from another string (a fresh copy each frame): nothing measured.
    const std::string s_Copy = s_Text;
    CHECK(s_Cache.Get(s_Copy, 14.0f, &s_Font, 16.0f, s_Counting) == std::vector<Line> {{0, 14}, {15, 9}});
    CHECK(s_Calls == s_First);
    CHECK(s_Cache.Size() == 1);

    // A new width (the window resized) wraps again.
    CHECK(s_Cache.Get(s_Text, 13.0f, &s_Font, 16.0f, s_Counting) == std::vector<Line> {{0, 8}, {9, 10}, {20, 4}});
    CHECK(s_Calls > s_First);

    // So does another font or font size.
    int s_Before = s_Calls;
    s_Cache.Get(s_Text, 13.0f, &s_OtherFont, 16.0f, s_Counting);
    CHECK(s_Calls > s_Before);
    s_Before = s_Calls;
    s_Cache.Get(s_Text, 13.0f, &s_OtherFont, 20.0f, s_Counting);
    CHECK(s_Calls > s_Before);
    s_Before = s_Calls;
    s_Cache.Get(s_Text, 13.0f, &s_OtherFont, 20.0f, s_Counting);
    CHECK(s_Calls == s_Before);
    CHECK(s_Cache.Size() == 1);
}

TEST_CASE("word wrap cache: starts over when full") {
    const auto s_Chars = [](std::string_view p_Text) { return static_cast<float>(p_Text.size()); };
    fr::WrapCache s_Cache;

    for (size_t i = 0; i < fr::WrapCache::kMaxEntries; ++i)
        s_Cache.Get("countdown " + std::to_string(i), 40.0f, nullptr, 16.0f, s_Chars);

    CHECK(s_Cache.Size() == fr::WrapCache::kMaxEntries);
    CHECK(s_Cache.Get("one more", 40.0f, nullptr, 16.0f, s_Chars) == std::vector<fr::WrapCache::Line> {{0, 8}});
    CHECK(s_Cache.Size() == 1);
}
