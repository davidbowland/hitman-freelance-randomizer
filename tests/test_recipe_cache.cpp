#include "doctest.h"

#include "LandslideFull.h"
#include "Recipe.h"
#include "RecipeCache.h"
#include "Rng.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

fr::PackageCacheLine Line() {
    fr::PackageCacheLine s_Line;
    s_Line.partition = 26;
    s_Line.sourceKey = 0x0123456789ABCDEFull;
    s_Line.recipeKey = 0x1111111111111111ull;
    s_Line.fileSize = 1234;
    s_Line.packageHash = 0xFEDCBA9876543210ull;
    s_Line.variants = {{"landslide", {}}, {"icon", {fr::RecipeFailure::SourceHash, 0x00CAFC0C93568981ull}}};
    return s_Line;
}

bool Same(const fr::PackageCacheLine& p_A, const fr::PackageCacheLine& p_B) {
    if (p_A.partition != p_B.partition || p_A.sourceKey != p_B.sourceKey || p_A.recipeKey != p_B.recipeKey ||
        p_A.fileSize != p_B.fileSize || p_A.packageHash != p_B.packageHash ||
        p_A.variants.size() != p_B.variants.size())
        return false;
    for (size_t i = 0; i < p_A.variants.size(); ++i)
        if (p_A.variants[i].key != p_B.variants[i].key ||
            p_A.variants[i].outcome.failure != p_B.variants[i].outcome.failure ||
            p_A.variants[i].outcome.sourceRid != p_B.variants[i].outcome.sourceRid)
            return false;
    return true;
}

constexpr std::array<std::string_view, 2> kKeys {"landslide", "icon"};

} // namespace

TEST_CASE("recipe_failure_names") {
    CHECK(fr::RecipeFailureName(fr::RecipeFailure::None) == "None");
    CHECK(fr::RecipeFailureName(fr::RecipeFailure::SourceHash) == "SourceHash");
    CHECK(fr::RecipeFailureName(fr::RecipeFailure::BaseNotBuildable) == "BaseNotBuildable");
    for (int i = 0; i <= static_cast<int>(fr::RecipeFailure::BaseNotBuildable); ++i) {
        const auto s_Failure = static_cast<fr::RecipeFailure>(i);
        CHECK(fr::RecipeFailureOfName(fr::RecipeFailureName(s_Failure)) == s_Failure);
    }
    CHECK_FALSE(fr::RecipeFailureOfName("sourcehash").has_value());
    CHECK_FALSE(fr::RecipeFailureOfName("").has_value());
}

TEST_CASE("package_cache_line_text") {
    CHECK(fr::FormatPackageCacheLine(Line()) ==
          "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 landslide=ok icon=SourceHash:00CAFC0C93568981");

    fr::PackageCacheLine s_Empty = Line();
    s_Empty.fileSize = 0;
    s_Empty.packageHash.reset();
    s_Empty.variants = {{"icon", {fr::RecipeFailure::SourceMissing, 0x1ull}}};
    CHECK(fr::FormatPackageCacheLine(s_Empty) ==
          "26 0123456789ABCDEF 1111111111111111 0 empty icon=SourceMissing:0000000000000001");

    for (const fr::PackageCacheLine& s_Line : {Line(), s_Empty}) {
        const std::optional<fr::PackageCacheLine> s_Parsed =
            fr::ParsePackageCacheLine(fr::FormatPackageCacheLine(s_Line));
        REQUIRE(s_Parsed.has_value());
        CHECK(Same(*s_Parsed, s_Line));
    }

    CHECK(fr::PartitionIdText(std::nullopt) == "empty");
    CHECK(fr::PartitionIdText(0xABCull) == "0000000000000ABC");
}

TEST_CASE("package_cache_line_rejects_anything_else") {
    for (const std::string_view s_Text : {
             "",
             "26",
             "26 0123456789ABCDEF 1111111111111111 1234",
             "26 0123456789ABCDEF 1234 FEDCBA9876543210 landslide=ok",                    // no recipe key
             "26 0123456789ABCDEF 111111111111111 1234 FEDCBA9876543210 landslide=ok",    // short recipe key
             "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210",                // no variant
             "x6 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 landslide=ok",   // partition
             "-1 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 landslide=ok",   // partition
             "26 0123456789ABCDE 1111111111111111 1234 FEDCBA9876543210 landslide=ok",    // short key
             "26 0123456789ABCDEG 1111111111111111 1234 FEDCBA9876543210 landslide=ok",   // not hex
             "26 0123456789ABCDEF 1111111111111111 12a4 FEDCBA9876543210 landslide=ok",   // size
             "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA987654321 landslide=ok",    // short id
             "26 0123456789ABCDEF 1111111111111111 1234 empty landslide=ok",              // empty with a size
             "26 0123456789ABCDEF 1111111111111111 0 FEDCBA9876543210 landslide=ok",      // a file of no bytes
             "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 landslide",      // no state
             "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 =ok",            // no key
             "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 landslide=fine", // state
             "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 landslide=None:0000000000000000", // None is ok
             "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 landslide=SourceHash",            // no rid
             "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 landslide=SourceHash:12",         // short rid
             "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 landslide=ok landslide=ok",       // twice
             "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210  landslide=ok",                   // two spaces
             "26 0123456789ABCDEF 1111111111111111 99999999999999999999999 FEDCBA9876543210 landslide=ok", // overflow
         }) {
        CAPTURE(s_Text);
        CHECK_FALSE(fr::ParsePackageCacheLine(s_Text).has_value());
    }
}

TEST_CASE("package_cache_file_keeps_the_lines_that_parse") {
    fr::PackageCacheLine s_Other = Line();
    s_Other.partition = 25;
    s_Other.variants = {{"marrakesh", {}}};
    const std::vector<fr::PackageCacheLine> s_Lines {Line(), s_Other};
    const std::string s_Text = fr::FormatPackageCache(s_Lines);
    CHECK(s_Text == fr::FormatPackageCacheLine(Line()) + "\n" + fr::FormatPackageCacheLine(s_Other) + "\n");

    const std::vector<fr::PackageCacheLine> s_Parsed = fr::ParsePackageCache(s_Text);
    REQUIRE(s_Parsed.size() == 2);
    CHECK(Same(s_Parsed[0], Line()));
    CHECK(Same(s_Parsed[1], s_Other));

    // CRLF, garbage, and a second line for a partition (the first wins).
    fr::PackageCacheLine s_Again = Line();
    s_Again.sourceKey = 7;
    const std::string s_Messy = "garbage\r\n" + fr::FormatPackageCacheLine(Line()) + "\r\n\r\n" +
                                fr::FormatPackageCacheLine(s_Again) + "\n" + fr::FormatPackageCacheLine(s_Other);
    const std::vector<fr::PackageCacheLine> s_Kept = fr::ParsePackageCache(s_Messy);
    REQUIRE(s_Kept.size() == 2);
    CHECK(Same(s_Kept[0], Line()));
    CHECK(Same(s_Kept[1], s_Other));

    CHECK(fr::ParsePackageCache("").empty());
    CHECK(fr::ParsePackageCache(std::string("\0\xFF\n", 3)).empty());
}

TEST_CASE("package_cache_hit_rules") {
    const fr::PackageCacheLine s_Line = Line();
    const fr::InstalledPackage s_File {true, 1234};
    int s_Calls = 0;
    const auto s_Ours = [&] {
        ++s_Calls;
        return true;
    };
    const auto s_NotOurs = [&] {
        ++s_Calls;
        return false;
    };

    CHECK(fr::PackageCacheHit(s_Line, s_Line.sourceKey, kKeys, s_File, s_Ours));
    CHECK(s_Calls == 1);
    CHECK_FALSE(fr::PackageCacheHit(s_Line, s_Line.sourceKey, kKeys, s_File, s_NotOurs));

    // Another source key, other variants, or another file: a miss, and the file's index is not read.
    s_Calls = 0;
    CHECK_FALSE(fr::PackageCacheHit(s_Line, s_Line.sourceKey + 1, kKeys, s_File, s_Ours));
    constexpr std::array<std::string_view, 2> s_Swapped {"icon", "landslide"};
    CHECK_FALSE(fr::PackageCacheHit(s_Line, s_Line.sourceKey, s_Swapped, s_File, s_Ours));
    constexpr std::array<std::string_view, 1> s_Fewer {"landslide"};
    CHECK_FALSE(fr::PackageCacheHit(s_Line, s_Line.sourceKey, s_Fewer, s_File, s_Ours));
    CHECK_FALSE(fr::PackageCacheHit(s_Line, s_Line.sourceKey, kKeys, {true, 1235}, s_Ours));
    CHECK_FALSE(fr::PackageCacheHit(s_Line, s_Line.sourceKey, kKeys, {false, 0}, s_Ours));
    CHECK(s_Calls == 0);

    // empty: a hit only while no file is there.
    fr::PackageCacheLine s_Empty = Line();
    s_Empty.fileSize = 0;
    s_Empty.packageHash.reset();
    CHECK(fr::PackageCacheHit(s_Empty, s_Empty.sourceKey, kKeys, {false, 0}, s_Ours));
    CHECK_FALSE(fr::PackageCacheHit(s_Empty, s_Empty.sourceKey, kKeys, {true, 0}, s_Ours));
    CHECK_FALSE(fr::PackageCacheHit(s_Empty, s_Empty.sourceKey, kKeys, s_File, s_Ours));
    CHECK(s_Calls == 0);
}

TEST_CASE("variant_build_id_names_its_own_entries") {
    const std::string s_Id = fr::VariantBuildId("1.0.0", 0xABull);
    const std::string_view s_Text = "00000000000000AB";
    CHECK(s_Id ==
          fr::LandslideBuildId("1.0.0", std::span(reinterpret_cast<const uint8_t*>(s_Text.data()), s_Text.size())));
    CHECK(fr::VariantBuildId("1.0.0", 0xABull) == s_Id);
    CHECK(fr::VariantBuildId("1.0.0", 0xACull) != s_Id);
    CHECK(fr::VariantBuildId("1.0.0", std::nullopt) != s_Id);
    CHECK(fr::VariantBuildId("1.0.1", 0xABull) != s_Id);
}

TEST_CASE("kept_outcomes_after_a_read_failure") {
    const std::vector<uint8_t> s_File(1234, 7);
    fr::PackageCacheLine s_Previous = Line(); // landslide ok, icon SourceHash:00CAFC0C93568981
    s_Previous.packageHash = fr::Fnv1a64(s_File);
    const uint64_t s_Recipes = s_Previous.recipeKey;
    const fr::RecipeOutcome s_Unread {fr::RecipeFailure::SourceMissing, 0xAull};
    const fr::RecipeOutcome s_Icon = s_Previous.variants[1].outcome;
    const std::array<fr::RecipeOutcome, 2> s_BothUnread {s_Unread, s_Unread};
    const std::vector<bool> s_Both {true, true};

    // The file the last good build installed: every variant keeps that build's outcome (built or not).
    const std::optional<std::vector<fr::RecipeOutcome>> s_Kept =
        fr::KeptOutcomes(&s_Previous, s_Recipes, kKeys, s_BothUnread, s_Both, s_File);
    REQUIRE(s_Kept.has_value());
    REQUIRE(s_Kept->size() == 2);
    CHECK(s_Kept->at(0).failure == fr::RecipeFailure::None);
    CHECK(s_Kept->at(1).failure == fr::RecipeFailure::SourceHash);
    CHECK(s_Kept->at(1).sourceRid == 0x00CAFC0C93568981ull);

    // Partly read: the variants that were read build as the last good build says.
    const std::array<fr::RecipeOutcome, 2> s_IconRead {s_Unread, s_Icon};
    CHECK(fr::KeptOutcomes(&s_Previous, s_Recipes, kKeys, s_IconRead, {true, false}, s_File).has_value());
    const std::array<fr::RecipeOutcome, 2> s_LandslideRead {fr::RecipeOutcome {}, s_Unread};
    CHECK(fr::KeptOutcomes(&s_Previous, s_Recipes, kKeys, s_LandslideRead, {false, true}, s_File).has_value());

    // ... else the game's files changed since (a source's size or hash now fails, or another source does).
    const std::array<fr::RecipeOutcome, 2> s_NowSize {fr::RecipeOutcome {fr::RecipeFailure::SourceSize, 0xBull},
                                                      s_Unread};
    CHECK_FALSE(fr::KeptOutcomes(&s_Previous, s_Recipes, kKeys, s_NowSize, {false, true}, s_File).has_value());
    const std::array<fr::RecipeOutcome, 2> s_OtherRid {s_Unread, {fr::RecipeFailure::SourceHash, 0xCull}};
    CHECK_FALSE(fr::KeptOutcomes(&s_Previous, s_Recipes, kKeys, s_OtherRid, {true, false}, s_File).has_value());

    // Other recipes (a mod update, new tables) built it.
    CHECK_FALSE(fr::KeptOutcomes(&s_Previous, s_Recipes + 1, kKeys, s_BothUnread, s_Both, s_File).has_value());

    // Not the previous build's file (other bytes, size or keys), no previous build, or one that installed none.
    std::vector<uint8_t> s_Other = s_File;
    s_Other[5] = 8;
    CHECK_FALSE(fr::KeptOutcomes(&s_Previous, s_Recipes, kKeys, s_BothUnread, s_Both, s_Other).has_value());
    CHECK_FALSE(fr::KeptOutcomes(&s_Previous, s_Recipes, kKeys, s_BothUnread, s_Both, std::span(s_File).first(1000))
                    .has_value());
    CHECK_FALSE(fr::KeptOutcomes(&s_Previous, s_Recipes, kKeys, s_BothUnread, s_Both, {}).has_value());
    CHECK_FALSE(fr::KeptOutcomes(nullptr, s_Recipes, kKeys, s_BothUnread, s_Both, s_File).has_value());
    constexpr std::array<std::string_view, 2> s_Swapped {"icon", "landslide"};
    CHECK_FALSE(fr::KeptOutcomes(&s_Previous, s_Recipes, s_Swapped, s_BothUnread, s_Both, s_File).has_value());
    constexpr std::array<std::string_view, 1> s_Fewer {"landslide"};
    CHECK_FALSE(fr::KeptOutcomes(&s_Previous, s_Recipes, s_Fewer, std::span(s_BothUnread).first(1), {true}, s_File)
                    .has_value());
    fr::PackageCacheLine s_NoFile = s_Previous;
    s_NoFile.fileSize = 0;
    s_NoFile.packageHash.reset();
    CHECK_FALSE(fr::KeptOutcomes(&s_NoFile, s_Recipes, kKeys, s_BothUnread, s_Both, {}).has_value());
}

TEST_CASE("package_recipe_key_leaves_out_where_the_sources_are") {
    const std::array<uint64_t, 2> s_Own {1, 2};
    const std::array<fr::RecipeVariant, 1> s_Variants {{{"icon", s_Own, {}}}};
    const uint64_t s_Key = fr::PackageRecipeKey(7, s_Variants, "1.0.0");
    CHECK(s_Key == fr::PackageRecipeKey(7, s_Variants, "1.0.0"));
    CHECK(s_Key != fr::PackageRecipeKey(8, s_Variants, "1.0.0"));
    CHECK(s_Key != fr::PackageRecipeKey(7, s_Variants, "1.0.1"));
    CHECK(s_Key != fr::PackageRecipeKey(7, {}, "1.0.0"));
    std::vector<fr::SourceSpot> s_Spots(1);
    s_Spots[0] = {0xA, true, "chunk26patch4.rpkg", 100, 50, true, 80, 4096, 1234, false, {}};
    std::vector<fr::SourceSpot> s_Moved = s_Spots;
    s_Moved[0].packageTime = 1;
    CHECK(fr::PackageSourceKey(7, s_Variants, "1.0.0", s_Spots) !=
          fr::PackageSourceKey(7, s_Variants, "1.0.0", s_Moved));
}

TEST_CASE("package_source_key_changes_with_any_input") {
    const std::array<uint64_t, 2> s_Own {1, 2};
    const std::array<fr::RecipeVariant, 2> s_Variants {{{"icon", s_Own, {}}, {"badboy", s_Own, "icon"}}};
    std::vector<fr::SourceSpot> s_Spots(2);
    s_Spots[0] = {0xA, true, "chunk26patch4.rpkg", 100, 50, true, 80, 4096, 1234, false, {}};
    s_Spots[1] = {0xB, false, {}, 0, 0, false, 0, 0, 0, false, "not in chunk26's packages"};
    const uint64_t s_Key = fr::PackageSourceKey(7, s_Variants, "1.0.0", s_Spots);
    CHECK(s_Key == fr::PackageSourceKey(7, s_Variants, "1.0.0", s_Spots));
    CHECK(s_Key != fr::PackageSourceKey(8, s_Variants, "1.0.0", s_Spots));
    CHECK(s_Key != fr::PackageSourceKey(7, s_Variants, "1.0.1", s_Spots));
    CHECK(s_Key != fr::PackageSourceKey(7, std::span(s_Variants).first(1), "1.0.0", s_Spots));

    std::array<fr::RecipeVariant, 2> s_Unlayered = s_Variants;
    s_Unlayered[1].layerOf = {};
    CHECK(s_Key != fr::PackageSourceKey(7, s_Unlayered, "1.0.0", s_Spots));
    const std::array<uint64_t, 1> s_Less {1};
    std::array<fr::RecipeVariant, 2> s_Fewer = s_Variants;
    s_Fewer[0].resources = s_Less;
    CHECK(s_Key != fr::PackageSourceKey(7, s_Fewer, "1.0.0", s_Spots));

    // Each field of where a source is, and whether it is found; an error's text alone changes nothing.
    const auto s_Changed = [&](auto p_Change) {
        std::vector<fr::SourceSpot> s_Other = s_Spots;
        p_Change(s_Other);
        return fr::PackageSourceKey(7, s_Variants, "1.0.0", s_Other) != s_Key;
    };
    CHECK(s_Changed([](auto& p_S) { p_S[0].rid = 0xC; }));
    CHECK(s_Changed([](auto& p_S) { p_S[0].found = false; }));
    CHECK(s_Changed([](auto& p_S) { p_S[0].package = "chunk26patch5.rpkg"; }));
    CHECK(s_Changed([](auto& p_S) { p_S[0].offset = 101; }));
    CHECK(s_Changed([](auto& p_S) { p_S[0].stored = 51; }));
    CHECK(s_Changed([](auto& p_S) { p_S[0].xored = false; }));
    CHECK(s_Changed([](auto& p_S) { p_S[0].finalSize = 81; }));
    // A repair in place rewrites the package: its size or time changes, if nothing else does.
    CHECK(s_Changed([](auto& p_S) { p_S[0].packageSize = 1; }));
    CHECK(s_Changed([](auto& p_S) { p_S[0].packageTime = 1; }));
    CHECK(s_Changed([](auto& p_S) { p_S.pop_back(); }));
    CHECK_FALSE(s_Changed([](auto& p_S) { p_S[1].error = "another reason"; }));
}
