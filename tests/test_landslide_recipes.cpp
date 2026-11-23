#include "doctest.h"

#include "LandslideFull.h"
#include "PatchPackage.h"
#include "Recipe.h"
#include "RecipeBuild.h"
#include "support/PatchPackageTestUtil.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recipes {
#include "../src/mod/LandslideRecipes.inc"
}

namespace {

const recipes::Variant* TableOf(std::string_view p_Key) {
    for (const recipes::Variant& s_Table : recipes::kVariants)
        if (p_Key == s_Table.name)
            return &s_Table;
    return nullptr;
}

const fr::RecipeResource* ResourceOf(uint64_t p_Rid) {
    for (const fr::RecipeResource& s_Res : recipes::kResources)
        if (s_Res.rid == p_Rid)
            return &s_Res;
    return nullptr;
}

// Partition p_Partition's variants, as LandslidePackage builds them (in fr::FullVariants() order).
std::vector<fr::RecipeVariant> PartitionVariants(int p_Partition) {
    std::vector<fr::RecipeVariant> s_Out;
    for (const fr::FullVariant* s_Full : fr::FullVariants())
        if (const recipes::Variant* s_Table = TableOf(s_Full->key); s_Table && s_Table->partition == p_Partition)
            s_Out.push_back({s_Full->key, s_Table->own, s_Full->layerOf});
    return s_Out;
}

} // namespace

TEST_CASE("landslide recipes: every op stays inside its source or the literals and fills the resource") {
    REQUIRE(!recipes::kResources.empty());
    std::set<uint64_t> s_Rids;
    for (const fr::RecipeSource& s_Source : recipes::kSources)
        CHECK(s_Rids.insert(s_Source.rid).second);

    for (const fr::RecipeResource& s_Res : recipes::kResources) {
        CAPTURE(s_Res.path);
        CAPTURE(s_Res.type);
        CHECK(s_Res.refFlags.size() == s_Res.refHashes.size());
        if (s_Res.type == "TEMP")
            CHECK(std::find(s_Res.refHashes.begin(), s_Res.refHashes.end(), s_Res.blueprint) != s_Res.refHashes.end());
        else
            CHECK(s_Res.blueprint == 0);
        for (uint16_t s_Index : s_Res.sources)
            REQUIRE(s_Index < recipes::kSources.size());

        uint64_t s_Size = 0;
        for (const fr::RecipeOp& s_Op : s_Res.ops) {
            CHECK(s_Op.length > 0);
            if (s_Op.kind == fr::OpKind::Copy) {
                REQUIRE(s_Op.source < s_Res.sources.size());
                CHECK(uint64_t {s_Op.offset} + s_Op.length <= recipes::kSources[s_Res.sources[s_Op.source]].size);
            }
            else {
                CHECK(s_Op.kind == fr::OpKind::Literal);
                CHECK(uint64_t {s_Op.offset} + s_Op.length <= recipes::kLiterals.size());
            }
            s_Size += s_Op.length;
        }
        CHECK(s_Size == s_Res.size);
    }
}

TEST_CASE("landslide recipes: each variant's tables name its resources and its partition") {
    for (const recipes::Variant& s_Table : recipes::kVariants) {
        CAPTURE(s_Table.name);
        const auto s_Variant = std::find_if(fr::FullVariants().begin(), fr::FullVariants().end(),
                                            [&](const fr::FullVariant* p_V) { return p_V->key == s_Table.name; });
        REQUIRE(s_Variant != fr::FullVariants().end());
        CHECK((*s_Variant)->partition == s_Table.partition);
        CHECK(!s_Table.own.empty());
        for (uint64_t s_Rid : s_Table.own) {
            const fr::RecipeResource* s_Res = ResourceOf(s_Rid);
            REQUIRE(s_Res != nullptr);
            CHECK(s_Res->partition == s_Table.partition);
        }
        // A layer's resources are its base's.
        if (!(*s_Variant)->layerOf.empty()) {
            const recipes::Variant* s_Base = TableOf((*s_Variant)->layerOf);
            REQUIRE(s_Base != nullptr);
            CHECK(std::ranges::equal(s_Base->own, s_Table.own));
        }
    }
    for (const fr::RecipeResource& s_Res : recipes::kResources) {
        CAPTURE(s_Res.path);
        CHECK(std::any_of(std::begin(recipes::kVariants), std::end(recipes::kVariants),
                          [&](const recipes::Variant& p_Table) {
                              return std::find(p_Table.own.begin(), p_Table.own.end(), s_Res.rid) != p_Table.own.end();
                          }));
    }
}

TEST_CASE("landslide recipes: the resources, partitions and former ids") {
    std::set<uint64_t> s_Rids;
    for (const fr::RecipeResource& s_Res : recipes::kResources) {
        CAPTURE(s_Res.path);
        CHECK(s_Rids.insert(s_Res.rid).second);
        CHECK(s_Res.rid != s_Res.sourceRid);
        // A partition the full variants know, and a source in its chunk's packages (or chunk 0's).
        CHECK(std::any_of(fr::FullVariants().begin(), fr::FullVariants().end(),
                          [&](const fr::FullVariant* p_V) { return p_V->partition == s_Res.partition; }));
    }
    for (const uint64_t s_Former : recipes::kFormerRids)
        CHECK_FALSE(s_Rids.contains(s_Former));

    // Every full variant has its table, and so its partition's package.
    for (const fr::FullVariant* s_Variant : fr::FullVariants()) {
        CAPTURE(s_Variant->key);
        CHECK(TableOf(s_Variant->key) != nullptr);
    }

    // An earlier build's file (a former brick) is still the mod's: rewritten, not left alone as another mod's.
    std::vector<uint64_t> s_Own;
    for (const fr::RecipeResource& s_Res : recipes::kResources)
        if (s_Res.partition == recipes::kFormerPartition)
            s_Own.push_back(s_Res.rid);
    pkg::PackageResource s_Old;
    s_Old.hash = recipes::kFormerRids[0];
    s_Old.type = "TEMP";
    s_Old.data = {1, 2, 3};
    const std::vector<uint8_t> s_OldRpkg = pkg::BuildPatchPackage(std::span(&s_Old, 1));
    const std::vector<uint8_t> s_Wanted = {4, 5, 6};
    CHECK(pkg::DecidePackageFile(true, &s_OldRpkg, true, s_Own, s_Wanted, recipes::kFormerRids) ==
          pkg::FileAction::Write);
    CHECK(pkg::DecidePackageFile(false, &s_OldRpkg, true, s_Own, {}, recipes::kFormerRids) == pkg::FileAction::Remove);
}

TEST_CASE("landslide recipes: the variants' tables") {
    // Each a known full variant (by name and scene), its own resources the
    // package's, every dependency once, no own resource among them, every
    // fingerprint a dependency. A layer (the full Bad Boy) has its base's
    // resources, the base's swap bricks first, and every one of the base's
    // fingerprints and dependencies.
    REQUIRE(std::size(recipes::kVariants) > 0);
    std::set<uint64_t> s_Claimed;
    std::set<std::string_view> s_Names; // one table per name: the runtime finds a variant's by its key

    for (const recipes::Variant& s_Variant : recipes::kVariants) {
        CAPTURE(std::string(s_Variant.name));
        CHECK(s_Names.insert(s_Variant.name).second);
        const fr::FullVariant* s_Full = fr::FullVariantOfKey(s_Variant.name);
        REQUIRE(s_Full != nullptr);
        CHECK(s_Full->scene == std::string_view(s_Variant.scene));
        CHECK(s_Variant.swapBricks.size() > 0);

        if (!s_Full->layerOf.empty()) {
            const recipes::Variant* s_Base = TableOf(s_Full->layerOf);
            REQUIRE(s_Base != nullptr);
            CHECK(std::string_view(s_Base->scene) == s_Variant.scene);
            REQUIRE(s_Variant.swapBricks.size() > s_Base->swapBricks.size());
            for (size_t i = 0; i < s_Base->swapBricks.size(); ++i)
                CHECK(std::string_view(s_Variant.swapBricks[i]) == s_Base->swapBricks[i]);
            const std::set<uint64_t> s_Deps(s_Variant.dependencies.begin(), s_Variant.dependencies.end());
            for (const uint64_t s_Dep : s_Base->dependencies)
                CHECK(s_Deps.contains(s_Dep));
            REQUIRE(s_Variant.fingerprints.size() > s_Base->fingerprints.size());
            for (size_t i = 0; i < s_Base->fingerprints.size(); ++i)
                CHECK(s_Variant.fingerprints[i].rid == s_Base->fingerprints[i].rid);
        }

        // Each resource belongs to one variant (and the layers on it).
        for (const uint64_t s_Rid : s_Variant.own)
            CHECK((s_Claimed.insert(s_Rid).second || !s_Full->layerOf.empty()));

        const std::set<uint64_t> s_Deps(s_Variant.dependencies.begin(), s_Variant.dependencies.end());
        CHECK(s_Deps.size() == s_Variant.dependencies.size());
        for (const fr::RecipeResource& s_Res : recipes::kResources)
            CHECK_FALSE(s_Deps.contains(s_Res.rid));
        for (const fr::LandslideFingerprint& s_Print : s_Variant.fingerprints) {
            CAPTURE(std::string(s_Print.name));
            CHECK(s_Deps.contains(s_Print.rid));
            CHECK(s_Print.size > 0);
        }
    }
    CHECK(s_Claimed.size() == recipes::kResources.size());
}

TEST_CASE("landslide recipes: the Freelancer layer's bricks") {
    // Each brick comes as a TEMP and a TBLU, the TEMP's blueprint the TBLU;
    // each variant's swap loads one brick of its own (a layer's then appends
    // its vanilla bricks; an appending variant's swap bricks are its vanilla
    // ones and then its own, last).
    REQUIRE(recipes::kFreelancerLayer);
    for (const fr::RecipeResource& s_Res : recipes::kResources) {
        CAPTURE(s_Res.path);
        REQUIRE((s_Res.type == "TEMP" || s_Res.type == "TBLU"));
        size_t s_Pair = 0;
        for (const fr::RecipeResource& s_Other : recipes::kResources)
            if (s_Other.path == s_Res.path && s_Other.type != s_Res.type) {
                ++s_Pair;
                if (s_Res.type == "TEMP")
                    CHECK(s_Res.blueprint == s_Other.rid);
            }
        CHECK(s_Pair == 1);
    }

    for (const recipes::Variant& s_Variant : recipes::kVariants) {
        CAPTURE(std::string(s_Variant.name));
        const fr::FullVariant* s_Full = fr::FullVariantOfKey(s_Variant.name);
        REQUIRE(s_Full != nullptr);
        REQUIRE((s_Full->layerOf.empty() && !s_Full->appends ? s_Variant.swapBricks.size() == 1
                                                             : s_Variant.swapBricks.size() > 1));
        const std::string_view s_Ours =
            s_Full->appends ? s_Variant.swapBricks[s_Variant.swapBricks.size() - 1] : s_Variant.swapBricks[0];
        CHECK(std::any_of(recipes::kResources.begin(), recipes::kResources.end(), [&](const fr::RecipeResource& p_R) {
            return p_R.type == "TEMP" && p_R.path == s_Ours &&
                   std::find(s_Variant.own.begin(), s_Variant.own.end(), p_R.rid) != s_Variant.own.end();
        }));
    }
}

// End to end, on real game files (never in CI): FR_RECIPE_E2E names a folder
// relative to the repository, under a gitignored build-*/ folder (it holds
// game data), with runtime/ (the game's Runtime packages, or links to them)
// and reference/ (known-good chunk<P>patch290.rpkg files). Every partition's package,
// rebuilt as the DLL does it, must equal its reference byte for byte; it is
// written to installed/ there, and the next start's plan then decodes nothing.
TEST_CASE("landslide recipes: rebuilt from the game's files, every package is the reference's") {
    const char* s_Env = std::getenv("FR_RECIPE_E2E");
    if (!s_Env || !*s_Env)
        return;
    namespace fs = std::filesystem;
    const fs::path s_Relative = fs::path(s_Env).lexically_normal();
    REQUIRE_MESSAGE(
        (s_Relative.is_relative() && !s_Relative.empty() && s_Relative.begin()->string().starts_with("build-")),
        "FR_RECIPE_E2E must be a build-*/ folder of the repository, relative to it");
    const fs::path s_Root = fs::path(FR_REPO_ROOT) / s_Relative;
    const fs::path s_Runtime = s_Root / "runtime";
    REQUIRE(fs::is_directory(s_Runtime));

    const pkg::RecipeTables s_Tables {recipes::kSources, recipes::kLiterals, recipes::kResources};
    std::set<int> s_Partitions;
    for (const fr::RecipeResource& s_Res : recipes::kResources)
        s_Partitions.insert(s_Res.partition);

    const uint64_t s_TableHash = fr::RecipeTableHash(recipes::kSources, recipes::kLiterals, recipes::kResources);
    fs::create_directories(s_Root / "installed");
    pkg::RuntimeIndexes s_Indexes(s_Runtime);
    for (const int s_Partition : s_Partitions) {
        CAPTURE(s_Partition);
        const std::vector<fr::RecipeVariant> s_Variants = PartitionVariants(s_Partition);
        const fs::path s_Own = s_Root / "installed" / ("chunk" + std::to_string(s_Partition) + "patch290.rpkg");
        std::vector<uint64_t> s_OwnRids;
        for (const fr::RecipeResource& s_Res : recipes::kResources)
            if (s_Res.partition == s_Partition)
                s_OwnRids.push_back(s_Res.rid);
        fs::remove(s_Own);
        const pkg::PartitionPlan s_Plan = pkg::PlanPartition(s_Tables, s_TableHash, s_Partition, s_Variants, "e2e",
                                                             s_Indexes, {}, s_Own, s_OwnRids, {});
        const std::vector<uint16_t>& s_Sources = s_Plan.sources;
        const std::vector<fr::SourceSpot>& s_Spots = s_Plan.spots;
        for (const fr::SourceSpot& s_Spot : s_Spots) {
            CAPTURE(s_Spot.rid);
            CHECK_MESSAGE(s_Spot.found, s_Spot.error);
        }

        const auto s_Start = std::chrono::steady_clock::now();
        const pkg::PartitionBuild s_Build =
            pkg::BuildPartition(s_Tables, s_Partition, s_Variants, s_Sources, s_Spots, s_Runtime);
        const auto s_Ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_Start).count();
        for (const pkg::SourceProblem& s_Problem : s_Build.problems)
            FAIL_CHECK(recipes::kSources[s_Problem.source].name << ": " << s_Problem.detail);
        for (size_t i = 0; i < s_Variants.size(); ++i) {
            CAPTURE(s_Variants[i].key);
            CHECK(s_Build.variants[i].failure == fr::RecipeFailure::None);
        }

        // The rebuilt bytes as the game reads them: each memory size is the BIN1 header's, each TEMP's
        // blueprint index names the blueprint its table expects.
        const pkg::ParsedPackage s_Parsed = pkg::ParsePatchPackage(s_Build.package);
        REQUIRE(s_Parsed.ok);
        for (const pkg::PackageResource& s_Got : s_Parsed.resources) {
            const fr::RecipeResource* s_Res = ResourceOf(s_Got.hash);
            REQUIRE(s_Res != nullptr);
            CAPTURE(s_Res->path);
            CHECK(pkg::BinMemSize(s_Got.data) == s_Res->memSize);
            if (s_Res->type == "TEMP") {
                const std::optional<uint32_t> s_Index = pkg::TempBlueprintIndex(s_Got.data);
                REQUIRE(s_Index.has_value());
                REQUIRE(*s_Index < s_Got.refs.size());
                CHECK(s_Got.refs[*s_Index].hash == s_Res->blueprint);
            }
        }

        bool s_Read = false;
        const std::vector<uint8_t> s_Reference = pkg::ReadFileBytes(
            s_Root / "reference" / ("chunk" + std::to_string(s_Partition) + "patch290.rpkg"), &s_Read);
        REQUIRE(s_Read);
        CHECK(s_Build.package == s_Reference);

        // Installed and cached, the next start's plan finds nothing to decode.
        std::string s_Error;
        REQUIRE(pkg::WriteFileAtomically(s_Own, s_Build.package, &s_Error));
        const std::array<fr::PackageCacheLine, 1> s_Cache {pkg::BuiltCacheLine(
            s_Partition, s_Plan.sourceKey, s_Plan.recipeKey, s_Variants, s_Build.variants, s_Build.package)};
        pkg::RuntimeIndexes s_Again(s_Runtime);
        CHECK(pkg::PlanPartition(s_Tables, s_TableHash, s_Partition, s_Variants, "e2e", s_Again, s_Cache, s_Own,
                                 s_OwnRids, {})
                  .cached.has_value());
        std::printf("recipe e2e: partition %d: %zu sources decoded (%llu bytes) in %lld ms; package %zu bytes, %s\n",
                    s_Partition, s_Build.decoded, static_cast<unsigned long long>(s_Build.decodedBytes),
                    static_cast<long long>(s_Ms), s_Build.package.size(),
                    s_Build.package == s_Reference ? "identical to the reference" : "DIFFERENT");
    }
}
