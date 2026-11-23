#pragma once

// The full map variants' packages and their safety net (core/LandslideFull.h).
// One package per map partition holds that map's full variants' bricks:
// Runtime/chunk<partition>patch290.rpkg (the partitions: fr::FullVariants()),
// with the partition raised to patch level 290 so the game mounts it. Each
// variant has its own tables (its resources, dependencies and fingerprints),
// its own gate and its own crash sentinel.
//
// Install, at Init: each package is built from the player's own game files
// (the recipes in LandslideRecipes.inc rebuild every resource from vanilla
// resources in Runtime's packages) and written, unless the cache
// (fr::kPackageCacheFileName next to the DLL) says nothing changed since the
// last start. A variant whose sources changed or are missing can't be built:
// it (and a layer on it) is left out of its package and out of rotation, as
// is every variant of a partition whose package was not installed. A
// file there that is not the mod's is never touched (the mod's: it holds only
// the recipes' resources, or ones an earlier build wrote). With Full map
// variants off (full_map_variants) the mod's packages and the cache are
// removed instead, and nothing is installed. The partition detour raises only
// a partition whose package is installed, and refuses when another patch of
// that chunk between the game's level and 290 would mount too.
//
// At LoadScene the checks walk the resource container: the package's own
// resources resolve to its file, every resource its reference tables name is
// there, and the fingerprinted vanilla resources have their build-time sizes.
// The crash sentinel (freelancerrandomizer-<key>-loading.txt next to the DLL)
// is written just before a swapped load and removed when it reaches play;
// found at game start, that full version stays off (its picks play the normal
// map) until Rearm (the Settings tab's "Try ... again") or other entries for
// it in its package.

#include "../core/LandslideFull.h"
#include "../core/RecipeCache.h"
#include "RecipeBuild.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class LandslidePackage {
public:
    static constexpr int kPatch = 290;

    LandslidePackage();

    // A package's file name ("chunk26patch290.rpkg").
    static std::string FileName(int p_Partition);

    // The DLL has the packages' recipes.
    static bool BuiltIn();

    // ... and a recipe table for p_Variant (its name, scene and partition match).
    static bool BuiltIn(const fr::FullVariant& p_Variant);

    // Install built p_Variant from the game's files, or found it in the cache
    // (true until Install says otherwise).
    bool Buildable(const fr::FullVariant& p_Variant) const noexcept;

    // Full map variants is off this session (Install's p_On).
    bool SwitchedOff() const noexcept { return m_SwitchedOff.load(); }

    // Why p_Variant can't be picked this session (fr::JudgeFullAvailability:
    // the DLL has its recipe, Full map variants is on, it is buildable and its
    // partition's package is installed, which counts as true until Install
    // ran), or Available.
    fr::FullAvailability Availability(const fr::FullVariant& p_Variant) const noexcept;

    bool Available(const fr::FullVariant& p_Variant) const noexcept {
        return Availability(p_Variant) == fr::FullAvailability::Available;
    }

    // The packages carry the Freelancer layer (targets, supplier, exits).
    static bool FreelancerLayer();

    // The bricks p_Variant's swap loads instead of the contract's (empty without its recipe).
    static std::vector<std::string_view> SwapBricks(const fr::FullVariant& p_Variant);

    // p_Variant's bricks in its package (its TEMPs): resource id and path.
    struct OwnBrick {
        uint64_t rid = 0;
        std::string_view path;
    };
    static std::vector<OwnBrick> OwnBricks(const fr::FullVariant& p_Variant);

    // Init, before the game creates its partitions: installs each partition's
    // package (p_On: Full map variants is on; off removes the mod's packages
    // and the cache), then reads the crash sentinels. p_Version: the mod's
    // version, for the cache and the sentinels' build ids. Every outcome is
    // logged.
    void Install(std::string_view p_Version, bool p_On) noexcept;

    // A package was installed this session (the partition detour is added).
    bool Installed() const noexcept;

    // Partition p_Index's package was installed this session (the detour raises it).
    bool Handles(int32_t p_Index) const noexcept;

    // The partition detour: the patch level to create partition p_Index with
    // (p_Level when unchanged). Logs the partitions it has a package for.
    int32_t OnPartition(int32_t p_Index, std::string_view p_Id, int p_Type, int32_t p_Level) noexcept;

    // Game thread, at LoadScene: the gate's inputs this session knows for p_Variant.
    fr::LandslideGate Gate(const fr::FullVariant& p_Variant, bool p_Forced) const noexcept;

    // Game thread, at LoadScene after p_Variant's chunk is mounted: its own
    // resources, its dependencies and its fingerprints, through the resource
    // container (logged).
    fr::LandslideVerdict CheckResources(const fr::FullVariant& p_Variant) noexcept;

    // Game thread: just before p_Variant's swapped load (p_Scene: the scene it loads).
    void WriteSentinel(const fr::FullVariant& p_Variant, std::string_view p_Scene) noexcept;

    // Game thread: p_Variant's swapped load reached play.
    void ClearSentinel(const fr::FullVariant& p_Variant) noexcept;

    // p_Variant's full version was turned off by its sentinel this session.
    bool TurnedOff(const fr::FullVariant& p_Variant) const noexcept;

    // Render thread (Settings): removes p_Variant's sentinel; its next load
    // tries the full version again.
    void Rearm(const fr::FullVariant& p_Variant) noexcept;

private:
    // One partition's package.
    struct Part {
        int index = 0;                 // 26
        std::string_view id;           // "coastaltown"
        std::string log;               // "landslide package": its log lines' prefix
        std::atomic<int> target = 0;   // kPatch once installed
        std::vector<int> otherPatches; // Init only: other patch numbers of its chunk in Runtime
        std::filesystem::path own;     // Init only: the package file
        uintmax_t ownSize = 0;         // Init only: its size once written or found up to date
        std::atomic<bool> partitionSeen = false;
        std::atomic<int32_t> raisedTo = -1; // the level the partition was created with
    };

    // Each partition's package from the cache or built, then the cache file.
    void InstallAll(std::string_view p_Version);
    // One partition's: the cache line to keep for it, or none (not installed, or nothing to remember).
    std::optional<fr::PackageCacheLine> InstallUnguarded(Part& p_Part, std::string_view p_Version, uint64_t p_Tables,
                                                         pkg::RuntimeIndexes& p_Indexes,
                                                         std::span<const fr::PackageCacheLine> p_Cached);
    // Full map variants off: removes the mod's packages and the cache.
    void Uninstall();
    // p_Part's package p_Size bytes long is in place: the detour raises its partition.
    void MarkInstalled(Part& p_Part, const std::filesystem::path& p_Own, uintmax_t p_Size);
    void ReadSentinel(const fr::FullVariant& p_Variant);
    std::filesystem::path SentinelPath(const fr::FullVariant& p_Variant) const;
    std::filesystem::path CachePath() const;
    Part* PartOf(int p_Index) noexcept;
    const Part* PartOf(int p_Index) const noexcept;

    std::deque<Part> m_Parts;     // a deque: Part holds atomics and never moves
    bool m_MaxPatchLevel = false; // Init only: the SDK's MaxPatchLevel mod (mods.ini) raises every partition
    std::atomic<bool> m_FaultLogged = false;
    std::atomic<bool> m_SwitchedOff = false;
    std::atomic<bool> m_InstallRan = false;

    std::mutex m_SentinelMutex; // the sentinel files (game and render threads)
    // Init only, by fr::FullVariants() index: pkg::VariantPackageHash of the installed entries, and fr::VariantBuildId.
    std::array<std::optional<uint64_t>, fr::kFullVariantCount> m_VariantHashes {};
    std::array<std::string, fr::kFullVariantCount> m_Builds {};
    std::array<std::atomic<bool>, fr::kFullVariantCount> m_TurnedOff {};   // by fr::FullVariants() index
    std::array<std::atomic<bool>, fr::kFullVariantCount> m_Unbuildable {}; // by fr::FullVariants() index
    std::array<std::atomic<bool>, fr::kFullVariantCount> m_ReadFailed {};  // unbuildable: a game file was unreadable
};
