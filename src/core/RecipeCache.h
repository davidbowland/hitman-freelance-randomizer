#pragma once

// The packages' cache (freelancerrandomizer-packages.txt next to the DLL): one
// line per partition saying what the last start installed and from which
// sources, so a start with nothing changed decodes no source and builds no
// package (src/mod/LandslidePackage.h).

#include "Recipe.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fr {

inline constexpr std::string_view kPackageCacheFileName = "freelancerrandomizer-packages.txt";

// Where the game's packages hold one recipe source (found false: in none of
// them; error says why, for the log, and readFailed whether a package could
// not be listed or read, which a later start may not repeat).
struct SourceSpot {
    uint64_t rid = 0;
    bool found = false;
    std::string package; // "chunk26patch4.rpkg"
    uint64_t offset = 0;
    uint32_t stored = 0;
    bool xored = false;
    uint32_t finalSize = 0;
    uint64_t packageSize = 0; // the package file's size and last write time (a repair in place changes them)
    int64_t packageTime = 0;
    bool readFailed = false;
    std::string error;
};

// 16 hex digits, upper case.
std::string Hex16(uint64_t p_Value);

// fr::Fnv1a64 of the recipes a partition's build follows: p_Tables
// (RecipeTableHash), its variants (keys, resources, layers) and the mod's
// version. Changes when any of them does.
uint64_t PackageRecipeKey(uint64_t p_Tables, std::span<const RecipeVariant> p_Variants, std::string_view p_Version);

// The recipe key continued over where each of the partition's sources is
// (package, entry, and the package file's size and time) or that it is
// missing: everything its build depends on. Changes when any of them does;
// computed from package indexes alone.
uint64_t PackageSourceKey(uint64_t p_Tables, std::span<const RecipeVariant> p_Variants, std::string_view p_Version,
                          std::span<const SourceSpot> p_Sources);

struct CachedVariant {
    std::string key; // "landslide"
    RecipeOutcome outcome;
};

struct PackageCacheLine {
    int partition = 0;
    uint64_t sourceKey = 0;
    uint64_t recipeKey = 0;
    uint64_t fileSize = 0;               // 0 when no file is installed
    std::optional<uint64_t> packageHash; // fr::Fnv1a64 of the installed package; empty: none is installed
    std::vector<CachedVariant> variants; // the partition's, in table order
};

// "26 0123456789ABCDEF 1111111111111111 1234 FEDCBA9876543210 landslide=ok icon=SourceHash:00CAFC0C93568981":
// partition, source key, recipe key, file size, partition id
// (PartitionIdText), then one key=state per variant (ok, or failure:sourceRid).
std::string FormatPackageCacheLine(const PackageCacheLine& p_Line);
// Empty for any other text.
std::optional<PackageCacheLine> ParsePackageCacheLine(std::string_view p_Text);

// One line each, '\n' after each.
std::string FormatPackageCache(std::span<const PackageCacheLine> p_Lines);
// The lines that parse (any line end), the first one per partition.
std::vector<PackageCacheLine> ParsePackageCache(std::string_view p_Text);

// A partition's id: the 16 hex digits of its installed package's hash, or "empty".
std::string PartitionIdText(const std::optional<uint64_t>& p_PackageHash);

// The build a full variant's crash sentinel names (fr::LandslideBuildId of
// its PartitionIdText): the mod's version and the hash of the variant's own
// entries in its installed package (empty: none installed), so another
// variant's package changing leaves it as it is.
std::string VariantBuildId(std::string_view p_Version, const std::optional<uint64_t>& p_VariantHash);

// A build in which a source could not be read (p_ReadFailed, by variant)
// keeps the mod's package p_File as it is when it is the one p_Previous
// installed (the same recipe key, variant keys, size and hash) and every
// variant that was read failed or built as p_Previous says: then every
// variant keeps p_Previous's outcome. Empty when not.
std::optional<std::vector<RecipeOutcome>> KeptOutcomes(const PackageCacheLine* p_Previous, uint64_t p_RecipeKey,
                                                       std::span<const std::string_view> p_Keys,
                                                       std::span<const RecipeOutcome> p_Outcomes,
                                                       const std::vector<bool>& p_ReadFailed,
                                                       std::span<const uint8_t> p_File);

// The partition's package file, as found at game start.
struct InstalledPackage {
    bool exists = false;
    uint64_t size = 0;
};

// p_Line still describes the partition: the same source key and variant keys
// (in order), and no file for a line without one, else a file of the line's
// size that p_Matches accepts (the mod's own, with the line's package hash;
// asked only when all else matches).
bool PackageCacheHit(const PackageCacheLine& p_Line, uint64_t p_SourceKey, std::span<const std::string_view> p_Keys,
                     const InstalledPackage& p_File, const std::function<bool()>& p_Matches);

} // namespace fr
