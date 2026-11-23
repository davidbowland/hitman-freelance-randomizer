#include "doctest.h"

#include "PatchPackage.h"
#include "Recipe.h"
#include "RecipeBuild.h"
#include "RecipeCache.h"
#include "Rng.h"
#include "support/PatchPackageTestUtil.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr uint64_t kRidA = 0x00A0000000000001ull; // chunk 26
constexpr uint64_t kRidB = 0x00B0000000000002ull; // chunk 26, in patch 1
constexpr uint64_t kRidC = 0x00C0000000000003ull; // chunk 0
constexpr uint64_t kBase = 0x00F0000000000010ull;
constexpr uint64_t kOther = 0x00F0000000000020ull;

std::string Pattern(size_t p_Size, char p_Seed) {
    std::string s_Out(p_Size, '\0');
    for (size_t i = 0; i < p_Size; ++i)
        s_Out[i] = static_cast<char>(p_Seed + static_cast<char>(i * 7));
    return s_Out;
}

uint64_t Hash(std::string_view p_Text) {
    return fr::Fnv1a64(std::span(reinterpret_cast<const uint8_t*>(p_Text.data()), p_Text.size()));
}

void Write(const fs::path& p_Path, const std::vector<uint8_t>& p_Bytes) {
    std::string s_Error;
    REQUIRE(pkg::WriteFileAtomically(p_Path, p_Bytes, &s_Error));
}

// A Runtime folder with chunk 26 (A in its base package, B in patch 1, a
// mod's patch 300 with another A) and chunk 0 (C); two resources rebuilt from
// them, a variant for each and a layer on the first.
struct Fixture {
    fs::path runtime;
    std::string a = Pattern(64, 3), b = Pattern(48, 50), c = Pattern(40, 90);
    std::array<uint8_t, 4> literals {'W', 'X', 'Y', 'Z'};
    std::vector<fr::RecipeSource> sources;
    std::array<uint16_t, 1> baseSources {0};
    std::array<uint16_t, 2> otherSources {2, 1};
    std::array<fr::RecipeOp, 2> baseOps {{{fr::OpKind::Copy, 0, 4, 30}, {fr::OpKind::Literal, 0, 0, 4}}};
    std::array<fr::RecipeOp, 2> otherOps {{{fr::OpKind::Copy, 1, 0, 48}, {fr::OpKind::Copy, 0, 10, 20}}};
    std::array<uint8_t, 1> refFlags {0x1F};
    std::array<uint64_t, 1> refHashes {kRidA};
    std::string baseBytes, otherBytes;
    std::vector<fr::RecipeResource> resources;
    std::array<uint64_t, 1> baseOwn {kBase};
    std::array<uint64_t, 1> otherOwn {kOther};
    std::vector<fr::RecipeVariant> variants;

    explicit Fixture(const char* p_Name) : runtime(fs::temp_directory_path() / p_Name) {
        fs::remove_all(runtime);
        fs::create_directories(runtime);
        sources = {
            {kRidA, "TEMP", "a.brick", 64, Hash(a), 26},
            {kRidB, "TBLU", "b.brick", 48, Hash(b), 26},
            {kRidC, "TEMP", "c.brick", 40, Hash(c), 0},
        };
        baseBytes = a.substr(4, 30) + "WXYZ";
        otherBytes = b + c.substr(10, 20);
        resources = {
            {kBase, "TEMP", "base.brick", kRidA, "chunk26.rpkg", kRidA, 100, pkg::kNoMemSize, refFlags, refHashes, 26,
             baseSources, baseOps, static_cast<uint32_t>(baseBytes.size()), Hash(baseBytes)},
            {kOther,
             "TBLU",
             "other.brick",
             kRidB,
             "chunk26patch1.rpkg",
             0,
             200,
             pkg::kNoMemSize,
             {},
             {},
             26,
             otherSources,
             otherOps,
             static_cast<uint32_t>(otherBytes.size()),
             Hash(otherBytes)},
        };
        variants = {{"base", baseOwn, {}}, {"other", otherOwn, {}}, {"layer", baseOwn, "base"}};
        WriteChunk26(b);
        WriteChunk0(Package(false, false, {}, {pkg::PlainTestResource(kRidC, c)}));
    }

    ~Fixture() { fs::remove_all(runtime); }

    static std::vector<uint8_t> Package(bool p_Version1, bool p_IsPatch, std::vector<uint64_t> p_Deletions,
                                        std::vector<pkg::TestRpkgResource> p_Resources) {
        return pkg::BuildTestRpkg(p_Version1, p_IsPatch, p_Deletions, p_Resources);
    }

    void WriteChunk26(std::string_view p_B, std::vector<uint64_t> p_Deletions = {}) {
        Write(runtime / "chunk26.rpkg", Package(false, false, {}, {pkg::PlainTestResource(kRidA, a, true)}));
        Write(runtime / "chunk26patch1.rpkg", Package(false, true, p_Deletions, {pkg::PlainTestResource(kRidB, p_B)}));
        Write(runtime / "chunk26patch300.rpkg", Package(true, true, {}, {pkg::PlainTestResource(kRidA, b)}));
    }

    void WriteChunk0(const std::vector<uint8_t>& p_Bytes) { Write(runtime / "chunk0.rpkg", p_Bytes); }

    pkg::RecipeTables Tables() const { return {sources, literals, resources}; }

    pkg::PartitionBuild Build() const {
        pkg::RuntimeIndexes s_Indexes(runtime);
        const std::vector<uint16_t> s_Sources = pkg::PartitionSources(Tables(), 26);
        const std::vector<fr::SourceSpot> s_Spots = pkg::LocateSources(Tables(), s_Sources, s_Indexes);
        return pkg::BuildPartition(Tables(), 26, variants, s_Sources, s_Spots, runtime);
    }

    // The package the build should write when the given resources are buildable.
    std::vector<uint8_t> Expected(bool p_Base, bool p_Other) const {
        std::vector<pkg::PackageResource> s_Out;
        if (p_Base)
            s_Out.push_back(
                {kBase, "TEMP", {baseBytes.begin(), baseBytes.end()}, 100, pkg::kNoMemSize, {{0x1F, kRidA}}});
        if (p_Other)
            s_Out.push_back({kOther, "TBLU", {otherBytes.begin(), otherBytes.end()}, 200, pkg::kNoMemSize, {}});
        return pkg::BuildPatchPackage(s_Out);
    }
};

bool Ok(const fr::RecipeOutcome& p_Outcome) {
    return p_Outcome.failure == fr::RecipeFailure::None && p_Outcome.sourceRid == 0;
}

} // namespace

TEST_CASE("recipe_build_partition_sources_in_table_order_once") {
    const Fixture s_F("fr_recipe_build_sources");
    CHECK(pkg::PartitionSources(s_F.Tables(), 26) == std::vector<uint16_t> {0, 1, 2});
    CHECK(pkg::PartitionSources(s_F.Tables(), 25).empty());
}

TEST_CASE("recipe_build_locates_the_newest_ioi_copy") {
    const Fixture s_F("fr_recipe_build_locate");
    pkg::RuntimeIndexes s_Indexes(s_F.runtime);
    const std::vector<uint16_t> s_Sources = pkg::PartitionSources(s_F.Tables(), 26);
    const std::vector<fr::SourceSpot> s_Spots = pkg::LocateSources(s_F.Tables(), s_Sources, s_Indexes);
    REQUIRE(s_Spots.size() == 3);
    // A: the base package's (the mod's patch 300 is left out).
    CHECK(s_Spots[0].rid == kRidA);
    CHECK(s_Spots[0].found);
    CHECK(s_Spots[0].package == "chunk26.rpkg");
    CHECK(s_Spots[0].xored);
    CHECK(s_Spots[0].finalSize == 64);
    CHECK(s_Spots[1].package == "chunk26patch1.rpkg");
    CHECK(s_Spots[2].package == "chunk0.rpkg");
    CHECK(s_Spots[2].finalSize == 40);

    // Each chunk's IOI packages read once (the mod's patch 300 left out), the time counted.
    CHECK(s_Indexes.PackagesRead() == 3);
    pkg::LocateSources(s_F.Tables(), s_Sources, s_Indexes);
    CHECK(s_Indexes.PackagesRead() == 3);
    CHECK(s_Indexes.ReadTime() >= std::chrono::nanoseconds::zero());
}

TEST_CASE("recipe_build_rebuilds_every_variant") {
    const Fixture s_F("fr_recipe_build_all");
    const pkg::PartitionBuild s_Build = s_F.Build();
    REQUIRE(s_Build.variants.size() == 3);
    for (const fr::RecipeOutcome& s_Outcome : s_Build.variants)
        CHECK(Ok(s_Outcome));
    CHECK(s_Build.problems.empty());
    CHECK(s_Build.decoded == 3);
    CHECK(s_Build.decodedBytes == 64 + 48 + 40);
    CHECK_FALSE(s_Build.packageFailed);
    CHECK(s_Build.package == s_F.Expected(true, true));
    CHECK(pkg::HoldsExactly(pkg::ParsePatchPackage(s_Build.package), std::array<uint64_t, 2> {kBase, kOther}));
}

TEST_CASE("recipe_build_a_changed_source_drops_only_its_variants") {
    Fixture s_F("fr_recipe_build_changed");
    s_F.WriteChunk26(Pattern(48, 51)); // B, same size, other bytes
    const pkg::PartitionBuild s_Build = s_F.Build();
    REQUIRE(s_Build.variants.size() == 3);
    CHECK(Ok(s_Build.variants[0]));
    CHECK(s_Build.variants[1].failure == fr::RecipeFailure::SourceHash);
    CHECK(s_Build.variants[1].sourceRid == kRidB);
    CHECK(Ok(s_Build.variants[2]));
    REQUIRE(s_Build.problems.size() == 1);
    CHECK(s_Build.problems[0].source == 1);
    CHECK(s_Build.problems[0].failure == fr::RecipeFailure::SourceHash);
    CHECK(s_Build.problems[0].detail.find("chunk26patch1.rpkg") != std::string::npos);
    CHECK(s_Build.package == s_F.Expected(true, false));

    s_F.WriteChunk26(Pattern(47, 50)); // shorter
    const pkg::PartitionBuild s_Short = s_F.Build();
    CHECK(s_Short.variants[1].failure == fr::RecipeFailure::SourceSize);
    CHECK(s_Short.package == s_F.Expected(true, false));
}

TEST_CASE("recipe_build_a_deleted_source_takes_its_layer_too") {
    Fixture s_F("fr_recipe_build_deleted");
    s_F.WriteChunk26(s_F.b, {kRidA});
    const pkg::PartitionBuild s_Build = s_F.Build();
    REQUIRE(s_Build.variants.size() == 3);
    CHECK(s_Build.variants[0].failure == fr::RecipeFailure::SourceMissing);
    CHECK(s_Build.variants[0].sourceRid == kRidA);
    CHECK(Ok(s_Build.variants[1]));
    // The layer shares its base's resources, so it fails with the same first failure.
    CHECK(s_Build.variants[2].failure == fr::RecipeFailure::SourceMissing);
    CHECK(s_Build.variants[2].sourceRid == kRidA);
    CHECK(s_Build.package == s_F.Expected(false, true));
}

TEST_CASE("recipe_build_corrupt_packages_never_crash") {
    Fixture s_F("fr_recipe_build_corrupt");
    // Chunk 0's only package: garbage, then a package cut one byte short (its data runs past the end).
    s_F.WriteChunk0({'2', 'K', 'P', 'R', 0xFF, 0xFF});
    const pkg::PartitionBuild s_Garbage = s_F.Build();
    CHECK(Ok(s_Garbage.variants[0]));
    CHECK(s_Garbage.variants[1].failure == fr::RecipeFailure::SourceMissing);
    CHECK(s_Garbage.variants[1].sourceRid == kRidC);
    CHECK(s_Garbage.package == s_F.Expected(true, false));

    std::vector<uint8_t> s_Cut = Fixture::Package(false, false, {}, {pkg::PlainTestResource(kRidC, s_F.c)});
    REQUIRE(!s_Cut.empty());
    s_Cut.pop_back();
    s_F.WriteChunk0(s_Cut);
    const pkg::PartitionBuild s_Short = s_F.Build();
    CHECK(s_Short.variants[1].failure == fr::RecipeFailure::SourceMissing);
    REQUIRE(s_Short.problems.size() == 1);
    CHECK(s_Short.problems[0].detail.find("chunk0.rpkg") != std::string::npos);

    // No chunk 26 at all: nothing builds, and the package is empty.
    fs::remove(s_F.runtime / "chunk26.rpkg");
    const pkg::PartitionBuild s_None = s_F.Build();
    for (const fr::RecipeOutcome& s_Outcome : s_None.variants)
        CHECK_FALSE(Ok(s_Outcome));
    CHECK(s_None.package.empty());
    CHECK_FALSE(s_None.packageFailed);
    CHECK(s_None.decoded == 0);
}

TEST_CASE("recipe_build_missing_runtime_folder") {
    pkg::RuntimeIndexes s_Indexes(fs::temp_directory_path() / "fr_recipe_build_no_such_folder");
    CHECK(s_Indexes.Chunk(26).indexes.empty());
    CHECK_FALSE(s_Indexes.Chunk(26).error.empty());
}

TEST_CASE("recipe_build_only_a_failed_read_keeps_the_outcome_out_of_the_cache") {
    Fixture s_F("fr_recipe_build_cacheable");
    CHECK(s_F.Build().Cacheable());

    // A changed or deleted source is the game's files as they are: kept.
    s_F.WriteChunk26(Pattern(48, 51));
    CHECK(s_F.Build().Cacheable());
    s_F.WriteChunk26(s_F.b, {kRidA});
    const pkg::PartitionBuild s_Deleted = s_F.Build();
    CHECK(s_Deleted.variants[0].failure == fr::RecipeFailure::SourceMissing);
    CHECK(s_Deleted.Cacheable());
    s_F.WriteChunk26(s_F.b);

    // Data that can't be read or decoded, or an index that can't be read: tried again at the next start.
    std::vector<uint8_t> s_Cut = Fixture::Package(false, false, {}, {pkg::PlainTestResource(kRidC, s_F.c)});
    REQUIRE(!s_Cut.empty());
    s_Cut.pop_back();
    s_F.WriteChunk0(s_Cut);
    const pkg::PartitionBuild s_Short = s_F.Build();
    REQUIRE(s_Short.problems.size() == 1);
    CHECK(s_Short.problems[0].readFailed);
    CHECK_FALSE(s_Short.Cacheable());
    s_F.WriteChunk0({'2', 'K', 'P', 'R', 0xFF, 0xFF});
    CHECK_FALSE(s_F.Build().Cacheable());
}

TEST_CASE("recipe_build_a_repair_in_place_changes_the_source_key") {
    Fixture s_F("fr_recipe_build_stamp");
    const uint64_t s_Tables = fr::RecipeTableHash(s_F.sources, s_F.literals, s_F.resources);
    const auto s_Key = [&] {
        pkg::RuntimeIndexes s_Indexes(s_F.runtime);
        const std::vector<uint16_t> s_Sources = pkg::PartitionSources(s_F.Tables(), 26);
        const std::vector<fr::SourceSpot> s_Spots = pkg::LocateSources(s_F.Tables(), s_Sources, s_Indexes);
        CHECK(s_Spots[1].packageSize == fs::file_size(s_F.runtime / "chunk26patch1.rpkg"));
        return fr::PackageSourceKey(s_Tables, s_F.variants, "1.0.0", s_Spots);
    };
    const uint64_t s_Before = s_Key();
    CHECK(s_Key() == s_Before);
    const fs::path s_Patch = s_F.runtime / "chunk26patch1.rpkg";
    fs::last_write_time(s_Patch, fs::last_write_time(s_Patch) + std::chrono::hours(1));
    CHECK(s_Key() != s_Before);
}

TEST_CASE("recipe_build_holds_only_file") {
    Fixture s_F("fr_recipe_build_holds_only");
    const fs::path s_Own = s_F.runtime / "chunk26patch290.rpkg";
    const std::array<uint64_t, 2> s_OwnRids {kBase, kOther};
    using O = pkg::FileOwner;
    CHECK(pkg::HoldsOnlyFile(s_Own, s_OwnRids, {}) == O::Unreadable); // none there
    Write(s_Own, s_F.Expected(true, false));
    CHECK(pkg::HoldsOnlyFile(s_Own, s_OwnRids, {}) == O::Mod);
    Write(s_Own, pkg::BuildPatchRpkg(0x1234, "TEMP", "another mod's"));
    CHECK(pkg::HoldsOnlyFile(s_Own, s_OwnRids, {}) == O::Other);
    const std::array<uint64_t, 1> s_Former {0x1234};
    CHECK(pkg::HoldsOnlyFile(s_Own, s_OwnRids, s_Former) == O::Mod);
    Write(s_Own, {'j', 'u', 'n', 'k'});
    CHECK(pkg::HoldsOnlyFile(s_Own, s_OwnRids, {}) == O::Other);
    CHECK(pkg::HoldsOnlyFile(s_F.runtime, s_OwnRids, {}) == O::Unreadable); // a folder
}

TEST_CASE("recipe_build_variant_package_hash_covers_only_its_own_entries") {
    Fixture s_F("fr_recipe_build_variant_hash");
    const auto s_Hash = [&](bool p_Base, bool p_Other, std::span<const uint64_t> p_Rids) {
        const pkg::ParsedPackage s_Package = pkg::ParsePatchPackage(s_F.Expected(p_Base, p_Other));
        REQUIRE(s_Package.ok);
        return pkg::VariantPackageHash(s_Package, p_Rids);
    };
    const uint64_t s_Base = s_Hash(true, true, s_F.baseOwn);

    // Another variant's entry gone (its source could not be read) or changed: the same.
    CHECK(s_Hash(true, false, s_F.baseOwn) == s_Base);
    s_F.otherBytes += "x";
    CHECK(s_Hash(true, true, s_F.baseOwn) == s_Base);

    // Its own entry changed or gone: another.
    CHECK(s_Hash(true, true, s_F.otherOwn) != s_Base);
    s_F.baseBytes += "x";
    CHECK(s_Hash(true, true, s_F.baseOwn) != s_Base);
    CHECK(s_Hash(false, true, s_F.baseOwn) != s_Base);
}

TEST_CASE("recipe_build_a_second_start_with_nothing_changed_decodes_nothing") {
    Fixture s_F("fr_recipe_build_cache");
    const fs::path s_Own = s_F.runtime / "chunk26patch290.rpkg";
    const std::array<uint64_t, 2> s_OwnRids {kBase, kOther};
    const uint64_t s_Tables = fr::RecipeTableHash(s_F.sources, s_F.literals, s_F.resources);
    const auto s_Plan = [&](std::span<const fr::PackageCacheLine> p_Cached, std::string_view p_Version = "1.0.0") {
        pkg::RuntimeIndexes s_Indexes(s_F.runtime);
        return pkg::PlanPartition(s_F.Tables(), s_Tables, 26, s_F.variants, p_Version, s_Indexes, p_Cached, s_Own,
                                  s_OwnRids, {});
    };

    // First start: no cache, so the partition is built, written, and its line kept.
    const pkg::PartitionPlan s_First = s_Plan({});
    CHECK_FALSE(s_First.cached.has_value());
    const pkg::PartitionBuild s_Build =
        pkg::BuildPartition(s_F.Tables(), 26, s_F.variants, s_First.sources, s_First.spots, s_F.runtime);
    Write(s_Own, s_Build.package);
    const fr::PackageCacheLine s_Line =
        pkg::BuiltCacheLine(26, s_First.sourceKey, s_First.recipeKey, s_F.variants, s_Build.variants, s_Build.package);
    CHECK(s_Line.fileSize == s_Build.package.size());
    REQUIRE(s_Line.packageHash.has_value());
    CHECK(*s_Line.packageHash == fr::Fnv1a64(s_Build.package));
    const std::array<fr::PackageCacheLine, 1> s_Cache {s_Line};

    CHECK(s_Line.recipeKey == fr::PackageRecipeKey(s_Tables, s_F.variants, "1.0.0"));

    // Second start: the cache line holds, nothing to decode, and each variant's entries (its build id's part)
    // read back from the file are the ones the first start wrote.
    const pkg::PartitionPlan s_Second = s_Plan(s_Cache);
    REQUIRE(s_Second.cached.has_value());
    CHECK(s_Second.sourceKey == s_First.sourceKey);
    CHECK(s_Second.recipeKey == s_First.recipeKey);
    CHECK(fr::FormatPackageCacheLine(*s_Second.cached) == fr::FormatPackageCacheLine(s_Line));
    bool s_Read = false;
    const pkg::ParsedPackage s_Installed = pkg::ParsePatchPackage(pkg::ReadFileBytes(s_Own, &s_Read));
    REQUIRE(s_Read);
    const pkg::ParsedPackage s_Written = pkg::ParsePatchPackage(s_Build.package);
    REQUIRE(s_Installed.ok);
    REQUIRE(s_Written.ok);
    CHECK(pkg::VariantPackageHash(s_Installed, s_F.baseOwn) == pkg::VariantPackageHash(s_Written, s_F.baseOwn));

    // Another mod version, a changed source, someone else's file there or none: a miss.
    CHECK_FALSE(s_Plan(s_Cache, "1.0.1").cached.has_value());
    // Another mod's file of the very same size: only its index tells.
    std::vector<uint8_t> s_Other;
    const std::string s_Fill(s_Build.package.size(), 'x');
    for (size_t n = 0; n < s_Fill.size() && s_Other.size() != s_Build.package.size(); ++n)
        s_Other = pkg::BuildPatchRpkg(0x1234, "TEMP", std::string_view(s_Fill).substr(0, n));
    REQUIRE(s_Other.size() == s_Build.package.size());
    Write(s_Own, s_Other);
    CHECK_FALSE(s_Plan(s_Cache).cached.has_value());
    fs::remove(s_Own);
    CHECK_FALSE(s_Plan(s_Cache).cached.has_value());
    Write(s_Own, s_Build.package);
    CHECK(s_Plan(s_Cache).cached.has_value());
    // The mod's own resources and size, other bytes: the hash tells.
    std::vector<uint8_t> s_Edited = s_Build.package;
    s_Edited.back() ^= 0x01;
    Write(s_Own, s_Edited);
    CHECK_FALSE(s_Plan(s_Cache).cached.has_value());
    Write(s_Own, s_Build.package);
    // A game update: a new patch package holds a new copy of B (the key reads indexes only, so where the copy
    // is is what changes).
    Write(s_F.runtime / "chunk26patch2.rpkg",
          Fixture::Package(false, true, {}, {pkg::PlainTestResource(kRidB, Pattern(48, 51))}));
    CHECK_FALSE(s_Plan(s_Cache).cached.has_value());
}

TEST_CASE("recipe_build_an_empty_line_holds_while_no_file_is_there") {
    Fixture s_F("fr_recipe_build_cache_empty");
    fs::remove(s_F.runtime / "chunk26.rpkg"); // nothing builds
    const fs::path s_Own = s_F.runtime / "chunk26patch290.rpkg";
    const std::array<uint64_t, 2> s_OwnRids {kBase, kOther};
    const uint64_t s_Tables = fr::RecipeTableHash(s_F.sources, s_F.literals, s_F.resources);
    const auto s_Plan = [&](std::span<const fr::PackageCacheLine> p_Cached) {
        pkg::RuntimeIndexes s_Indexes(s_F.runtime);
        return pkg::PlanPartition(s_F.Tables(), s_Tables, 26, s_F.variants, "1.0.0", s_Indexes, p_Cached, s_Own,
                                  s_OwnRids, {});
    };

    const pkg::PartitionPlan s_First = s_Plan({});
    const pkg::PartitionBuild s_Build =
        pkg::BuildPartition(s_F.Tables(), 26, s_F.variants, s_First.sources, s_First.spots, s_F.runtime);
    REQUIRE(s_Build.package.empty());
    const fr::PackageCacheLine s_Line =
        pkg::BuiltCacheLine(26, s_First.sourceKey, s_First.recipeKey, s_F.variants, s_Build.variants, s_Build.package);
    CHECK_FALSE(s_Line.packageHash.has_value());
    CHECK(s_Line.fileSize == 0);
    const std::array<fr::PackageCacheLine, 1> s_Cache {s_Line};

    const pkg::PartitionPlan s_Second = s_Plan(s_Cache);
    REQUIRE(s_Second.cached.has_value());
    CHECK(s_Second.cached->variants[0].outcome.failure == fr::RecipeFailure::SourceMissing);
    Write(s_Own, pkg::BuildPatchRpkg(kBase, "TEMP", "stale"));
    CHECK_FALSE(s_Plan(s_Cache).cached.has_value());
}
