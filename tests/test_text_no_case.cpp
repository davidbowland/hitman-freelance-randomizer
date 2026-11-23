#include "doctest.h"

#include "TextNoCase.h"

#include <string_view>

TEST_CASE("text no case: compare, prefix, find and contain") {
    CHECK(fr::EqualsNoCase("Chunk26Patch290.RPKG", "chunk26patch290.rpkg"));
    CHECK_FALSE(fr::EqualsNoCase("chunk26", "chunk2"));
    CHECK(fr::StartsWithNoCase("OnPickDone", "onpick"));
    CHECK_FALSE(fr::StartsWithNoCase("On", "onpick"));
    CHECK(fr::FindNoCase("a Mercer_Pickup", "MERCER") == 2);
    CHECK(fr::FindNoCase("abcabc", "C", 3) == 5);
    CHECK(fr::FindNoCase("abc", "") == std::string_view::npos);
    CHECK(fr::FindNoCase("abc", "c", 3) == std::string_view::npos);
    CHECK(fr::FindNoCase("abc", "c", 4) == std::string_view::npos);
    CHECK(fr::FindNoCase("abc", "c", std::string_view::npos) == std::string_view::npos);
    CHECK(fr::ContainsNoCase("Pirate Saber", "pirate"));
    CHECK(fr::ContainsNoCase("anything", ""));
    CHECK_FALSE(fr::ContainsNoCase("sabre", "saber"));
    // Bytes outside ASCII compare as they are.
    CHECK_FALSE(fr::EqualsNoCase("\xC9", "\xE9"));
}
