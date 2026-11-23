#include "doctest.h"

#include "Recipe.h"
#include "Rng.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace {

constexpr uint64_t kRidA = 0x00A0000000000001ull;
constexpr uint64_t kRidB = 0x00B0000000000002ull;

std::vector<uint8_t> Pattern(size_t p_Size, uint8_t p_Seed) {
    std::vector<uint8_t> s_Bytes(p_Size);
    for (size_t i = 0; i < p_Size; ++i)
        s_Bytes[i] = static_cast<uint8_t>(p_Seed + i * 7);
    return s_Bytes;
}

// Two vanilla sources, some literal bytes, and a resource rebuilt from
// A[10, 30) + literals[1, 4) + B[0, 40).
struct Fixture {
    std::vector<uint8_t> a = Pattern(64, 3);
    std::vector<uint8_t> b = Pattern(40, 101);
    std::array<uint8_t, 5> literals {'?', 'X', 'Y', 'Z', '?'};
    std::vector<fr::RecipeSource> sources;
    std::array<uint16_t, 2> resourceSources {1, 0}; // B, then A
    std::vector<fr::RecipeOp> ops {
        {fr::OpKind::Copy, 1, 10, 20},
        {fr::OpKind::Literal, 0, 1, 3},
        {fr::OpKind::Copy, 0, 0, 40},
    };
    std::vector<uint8_t> expected;
    fr::RecipeResource resource {};
    std::map<uint64_t, std::vector<uint8_t>> available;
    std::set<uint64_t> verified; // sources the caller already verified

    Fixture() {
        expected.insert(expected.end(), a.begin() + 10, a.begin() + 30);
        expected.insert(expected.end(), literals.begin() + 1, literals.begin() + 4);
        expected.insert(expected.end(), b.begin(), b.end());
        sources = {
            {kRidA, "TEMP", "a.brick", static_cast<uint32_t>(a.size()), fr::Fnv1a64(std::span<const uint8_t>(a)), 0},
            {kRidB, "TEMP", "b.brick", static_cast<uint32_t>(b.size()), fr::Fnv1a64(std::span<const uint8_t>(b)), 26},
        };
        available[kRidA] = a;
        available[kRidB] = b;
        Refresh();
    }

    // Points the resource at the current ops and expected bytes.
    void Refresh() {
        resource.rid = 0x00F0000000000003ull;
        resource.type = "TEMP";
        resource.sourceRid = kRidA;
        resource.partition = 26;
        resource.sources = resourceSources;
        resource.ops = ops;
        resource.size = static_cast<uint32_t>(expected.size());
        resource.hash = fr::Fnv1a64(std::span<const uint8_t>(expected));
    }

    fr::RecipeResult Apply() const {
        return fr::ApplyRecipe(resource, sources, literals,
                               [this](uint64_t p_Rid) -> std::optional<fr::RecipeSourceBytes> {
                                   const auto s_It = available.find(p_Rid);
                                   if (s_It == available.end())
                                       return std::nullopt;
                                   return fr::RecipeSourceBytes {s_It->second, verified.contains(p_Rid)};
                               });
    }
};

} // namespace

TEST_CASE("recipe_rebuilds_from_two_sources_and_literals") {
    const Fixture s_F;
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::None);
    CHECK(s_Result.sourceRid == 0);
    CHECK(s_Result.bytes == s_F.expected);
}

TEST_CASE("recipe_source_missing") {
    Fixture s_F;
    s_F.available.erase(kRidB);
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::SourceMissing);
    CHECK(s_Result.sourceRid == kRidB);
    CHECK(s_Result.bytes.empty());
}

TEST_CASE("recipe_source_wrong_size") {
    Fixture s_F;
    s_F.available[kRidA].push_back(0);
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::SourceSize);
    CHECK(s_Result.sourceRid == kRidA);
    CHECK(s_Result.bytes.empty());
}

TEST_CASE("recipe_source_too_short") {
    Fixture s_F;
    s_F.available[kRidB].pop_back();
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::SourceSize);
    CHECK(s_Result.sourceRid == kRidB);
    CHECK(s_Result.bytes.empty());
}

TEST_CASE("recipe_verify_source") {
    const Fixture s_F;
    std::vector<uint8_t> s_A = s_F.a;
    CHECK(fr::VerifyRecipeSource(s_F.sources[0], s_A) == fr::RecipeFailure::None);
    s_A[0] ^= 1;
    CHECK(fr::VerifyRecipeSource(s_F.sources[0], s_A) == fr::RecipeFailure::SourceHash);
    s_A.pop_back();
    CHECK(fr::VerifyRecipeSource(s_F.sources[0], s_A) == fr::RecipeFailure::SourceSize);
}

// A source the caller verified is not hashed again (byte 5 of A is not copied).
TEST_CASE("recipe_skips_verified_sources") {
    Fixture s_F;
    s_F.available[kRidA][5] ^= 0xFF;
    s_F.verified.insert(kRidA);
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::None);
    CHECK(s_Result.bytes == s_F.expected);
}

TEST_CASE("recipe_source_wrong_hash") {
    Fixture s_F;
    s_F.available[kRidA][5] ^= 0xFF;
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::SourceHash);
    CHECK(s_Result.sourceRid == kRidA);
    CHECK(s_Result.bytes.empty());
}

TEST_CASE("recipe_copy_past_source_end") {
    Fixture s_F;
    s_F.ops[2] = {fr::OpKind::Copy, 0, 1, 40}; // B is 40 bytes
    s_F.Refresh();
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::OpOutOfRange);
    CHECK(s_Result.sourceRid == kRidB);
    CHECK(s_Result.bytes.empty());
}

TEST_CASE("recipe_copy_offset_overflow") {
    Fixture s_F;
    s_F.ops[2] = {fr::OpKind::Copy, 0, 0xFFFFFFF0u, 40};
    s_F.Refresh();
    CHECK(s_F.Apply().failure == fr::RecipeFailure::OpOutOfRange);
}

TEST_CASE("recipe_copy_length_overflow") {
    Fixture s_F;
    s_F.ops[2] = {fr::OpKind::Copy, 0, 0x20, 0xFFFFFFF0u};
    s_F.Refresh();
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::OpOutOfRange);
    CHECK(s_Result.sourceRid == kRidB);
}

TEST_CASE("recipe_unknown_op_kind") {
    Fixture s_F;
    s_F.ops[1].kind = static_cast<fr::OpKind>(7);
    s_F.Refresh();
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::OpOutOfRange);
    CHECK(s_Result.bytes.empty());
}

TEST_CASE("recipe_copy_source_index_out_of_range") {
    Fixture s_F;
    s_F.ops[0] = {fr::OpKind::Copy, 2, 10, 20}; // the resource has two sources
    s_F.Refresh();
    CHECK(s_F.Apply().failure == fr::RecipeFailure::OpOutOfRange);
}

TEST_CASE("recipe_source_table_index_out_of_range") {
    Fixture s_F;
    s_F.resourceSources[0] = 7; // kSources has two
    s_F.Refresh();
    CHECK(s_F.Apply().failure == fr::RecipeFailure::OpOutOfRange);
}

TEST_CASE("recipe_literal_past_literals_end") {
    Fixture s_F;
    s_F.ops[1] = {fr::OpKind::Literal, 0, 3, 3}; // kLiterals is 5 bytes
    s_F.Refresh();
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::OpOutOfRange);
    CHECK(s_Result.sourceRid == 0);
    CHECK(s_Result.bytes.empty());
}

TEST_CASE("recipe_output_size") {
    SUBCASE("ops write past size") {
        Fixture s_F;
        s_F.Refresh();
        s_F.resource.size -= 1;
        const fr::RecipeResult s_Result = s_F.Apply();
        CHECK(s_Result.failure == fr::RecipeFailure::OutputSize);
        CHECK(s_Result.bytes.empty());
    }
    SUBCASE("ops end before size") {
        Fixture s_F;
        s_F.Refresh();
        s_F.resource.size += 1;
        const fr::RecipeResult s_Result = s_F.Apply();
        CHECK(s_Result.failure == fr::RecipeFailure::OutputSize);
        CHECK(s_Result.bytes.empty());
    }
}

TEST_CASE("recipe_output_hash") {
    Fixture s_F;
    s_F.resource.hash ^= 1;
    const fr::RecipeResult s_Result = s_F.Apply();
    CHECK(s_Result.failure == fr::RecipeFailure::OutputHash);
    CHECK(s_Result.sourceRid == 0);
    CHECK(s_Result.bytes.empty());
}

TEST_CASE("recipe_table_hash") {
    const Fixture s_F;
    const std::array<fr::RecipeResource, 1> s_Resources {s_F.resource};
    const uint64_t s_Hash = fr::RecipeTableHash(s_F.sources, s_F.literals, s_Resources);
    CHECK(s_Hash == fr::RecipeTableHash(s_F.sources, s_F.literals, s_Resources));

    SUBCASE("a source changes it") {
        std::vector<fr::RecipeSource> s_Sources = s_F.sources;
        s_Sources[1].hash ^= 1;
        CHECK(fr::RecipeTableHash(s_Sources, s_F.literals, s_Resources) != s_Hash);
    }
    SUBCASE("a literal byte changes it") {
        std::array<uint8_t, 5> s_Literals = s_F.literals;
        s_Literals[4] = '!';
        CHECK(fr::RecipeTableHash(s_F.sources, s_Literals, s_Resources) != s_Hash);
    }
    SUBCASE("an op changes it") {
        std::vector<fr::RecipeOp> s_Ops = s_F.ops;
        s_Ops[1].length = 2;
        std::array<fr::RecipeResource, 1> s_Changed {s_F.resource};
        s_Changed[0].ops = s_Ops;
        CHECK(fr::RecipeTableHash(s_F.sources, s_F.literals, s_Changed) != s_Hash);
    }
    // Each resource field that reaches the package or the rebuild.
    const auto s_Changed = [&](auto p_Change) {
        std::array<fr::RecipeResource, 1> s_Copy {s_F.resource};
        p_Change(s_Copy[0]);
        return fr::RecipeTableHash(s_F.sources, s_F.literals, s_Copy) != s_Hash;
    };
    const std::array<uint64_t, 1> s_RefHashes {0x42};
    const std::array<uint8_t, 1> s_RefFlags {0x1F};
    const std::array<uint16_t, 2> s_OtherSources {0, 1};
    CHECK(s_Changed([&](fr::RecipeResource& p_R) { p_R.refHashes = s_RefHashes; }));
    CHECK(s_Changed([&](fr::RecipeResource& p_R) { p_R.refFlags = s_RefFlags; }));
    CHECK(s_Changed([](fr::RecipeResource& p_R) { p_R.memSize = 1; }));
    CHECK(s_Changed([](fr::RecipeResource& p_R) { p_R.vmemSize = 1; }));
    CHECK(s_Changed([&](fr::RecipeResource& p_R) { p_R.sources = s_OtherSources; }));
    CHECK(s_Changed([](fr::RecipeResource& p_R) { p_R.size += 1; }));
    CHECK(s_Changed([](fr::RecipeResource& p_R) { p_R.hash ^= 1; }));
    CHECK(s_Changed([](fr::RecipeResource& p_R) { p_R.type = "TBLU"; }));
    CHECK(s_Changed([](fr::RecipeResource& p_R) { p_R.path = "x"; }));
    CHECK(s_Changed([](fr::RecipeResource& p_R) { p_R.sourceRid = kRidB; }));
    CHECK(s_Changed([](fr::RecipeResource& p_R) { p_R.blueprint = 1; }));
    CHECK(s_Changed([](fr::RecipeResource& p_R) { p_R.partition = 27; }));

    SUBCASE("adjacent text fields are length framed") {
        std::array<fr::RecipeResource, 1> s_Joined {s_F.resource};
        s_Joined[0].type = "TEMP";
        s_Joined[0].path = "x";
        std::array<fr::RecipeResource, 1> s_Moved {s_Joined[0]};
        s_Moved[0].type = "TEMPx";
        s_Moved[0].path = "";
        CHECK(fr::RecipeTableHash(s_F.sources, s_F.literals, s_Joined) !=
              fr::RecipeTableHash(s_F.sources, s_F.literals, s_Moved));
    }
}

namespace {

constexpr uint64_t kSapienzaRid = 0x10;
constexpr uint64_t kIconRid = 0x11;
constexpr uint64_t kBadBoyRid = 0x12;
constexpr uint64_t kMarrakeshRid = 0x20;
constexpr uint64_t kEntertainerRid = 0x21;
constexpr uint64_t kSharedRid = 0x30;
constexpr uint64_t kFailingSource = 0x00CAFC0C93568981ull;

constexpr std::array<uint64_t, 2> kLandslide {kSapienzaRid, kSharedRid};
constexpr std::array<uint64_t, 2> kIcon {kSharedRid, kIconRid};
constexpr std::array<uint64_t, 1> kBadBoy {kBadBoyRid};
constexpr std::array<uint64_t, 1> kMarrakesh {kMarrakeshRid};
constexpr std::array<uint64_t, 1> kEntertainer {kEntertainerRid};

// A layer listed before its base, as order must not matter.
const std::array<fr::RecipeVariant, 5> kVariants {{
    {"landslide", kLandslide, {}},
    {"badboy", kBadBoy, "icon"},
    {"icon", kIcon, {}},
    {"marrakesh", kMarrakesh, {}},
    {"entertainer", kEntertainer, "marrakesh"},
}};

std::vector<fr::RecipeOutcome> Resolve(const std::map<uint64_t, fr::RecipeOutcome>& p_Failures) {
    return fr::ResolveBuildable(kVariants, [&](uint64_t p_Rid) {
        const auto s_It = p_Failures.find(p_Rid);
        return s_It == p_Failures.end() ? fr::RecipeOutcome {} : s_It->second;
    });
}

} // namespace

TEST_CASE("buildable_all_ok") {
    const std::vector<fr::RecipeOutcome> s_Out = Resolve({});
    REQUIRE(s_Out.size() == kVariants.size());
    for (const fr::RecipeOutcome& s_O : s_Out)
        CHECK(s_O.failure == fr::RecipeFailure::None);
}

TEST_CASE("buildable_failing_source_of_icon_only_blocks_icon_and_badboy") {
    const std::vector<fr::RecipeOutcome> s_Out = Resolve({{kIconRid, {fr::RecipeFailure::SourceHash, kFailingSource}}});
    REQUIRE(s_Out.size() == kVariants.size());
    CHECK(s_Out[0].failure == fr::RecipeFailure::None);
    CHECK(s_Out[1].failure == fr::RecipeFailure::BaseNotBuildable);
    CHECK(s_Out[1].sourceRid == kFailingSource);
    CHECK(s_Out[2].failure == fr::RecipeFailure::SourceHash);
    CHECK(s_Out[2].sourceRid == kFailingSource);
    CHECK(s_Out[3].failure == fr::RecipeFailure::None);
    CHECK(s_Out[4].failure == fr::RecipeFailure::None);
}

TEST_CASE("buildable_shared_resource_blocks_both_users_and_their_layers") {
    const std::vector<fr::RecipeOutcome> s_Out =
        Resolve({{kSharedRid, {fr::RecipeFailure::SourceMissing, kFailingSource}}});
    CHECK(s_Out[0].failure == fr::RecipeFailure::SourceMissing);
    CHECK(s_Out[1].failure == fr::RecipeFailure::BaseNotBuildable);
    CHECK(s_Out[1].sourceRid == kFailingSource);
    CHECK(s_Out[2].failure == fr::RecipeFailure::SourceMissing);
    CHECK(s_Out[3].failure == fr::RecipeFailure::None);
    CHECK(s_Out[4].failure == fr::RecipeFailure::None);
}

TEST_CASE("buildable_first_failure_wins") {
    const std::vector<fr::RecipeOutcome> s_Out = Resolve({
        {kSharedRid, {fr::RecipeFailure::SourceSize, 1}},
        {kIconRid, {fr::RecipeFailure::OutputHash, 2}},
    });
    CHECK(s_Out[2].failure == fr::RecipeFailure::SourceSize);
    CHECK(s_Out[2].sourceRid == 1);
}

TEST_CASE("buildable_layer_own_failure_beats_base") {
    const std::vector<fr::RecipeOutcome> s_Out = Resolve({
        {kBadBoyRid, {fr::RecipeFailure::OutputSize, 3}},
        {kIconRid, {fr::RecipeFailure::SourceHash, 4}},
    });
    CHECK(s_Out[1].failure == fr::RecipeFailure::OutputSize);
    CHECK(s_Out[1].sourceRid == 3);
}

TEST_CASE("buildable_layer_without_base_or_in_a_cycle") {
    const std::array<fr::RecipeVariant, 3> s_Variants {{
        {"orphan", kBadBoy, "nothere"},
        {"x", kIcon, "y"},
        {"y", kMarrakesh, "x"},
    }};
    const std::vector<fr::RecipeOutcome> s_Out =
        fr::ResolveBuildable(s_Variants, [](uint64_t) { return fr::RecipeOutcome {}; });
    REQUIRE(s_Out.size() == 3);
    for (const fr::RecipeOutcome& s_O : s_Out) {
        CHECK(s_O.failure == fr::RecipeFailure::BaseNotBuildable);
        CHECK(s_O.sourceRid == 0);
    }
}

TEST_CASE("buildable_layer_on_a_shared_key") {
    const std::array<fr::RecipeVariant, 3> s_Variants {{
        {"icon", kIcon, {}},
        {"icon", kMarrakesh, {}},
        {"badboy", kBadBoy, "icon"},
    }};
    const std::vector<fr::RecipeOutcome> s_Out =
        fr::ResolveBuildable(s_Variants, [](uint64_t) { return fr::RecipeOutcome {}; });
    CHECK(s_Out[0].failure == fr::RecipeFailure::None);
    CHECK(s_Out[1].failure == fr::RecipeFailure::None);
    CHECK(s_Out[2].failure == fr::RecipeFailure::BaseNotBuildable);
    CHECK(s_Out[2].sourceRid == 0);
}
