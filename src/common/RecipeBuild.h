#pragma once

// Building the mod's packages from the player's own game files: where each
// recipe source (src/core/Recipe.h) is in the Runtime folder's packages, and
// one partition's package rebuilt from them. Files only, no engine calls.

#include "PatchPackage.h"
#include "Recipe.h"
#include "RecipeCache.h"
#include "RpkgRead.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pkg {

struct RecipeTables {
    std::span<const fr::RecipeSource> sources;
    std::span<const uint8_t> literals;
    std::span<const fr::RecipeResource> resources;
};

// One chunk's IOI packages in a Runtime folder: chunk<C>.rpkg and its
// patches (chunk<C>patch<K>.rpkg, K < fr::kFirstModPatch), any case.
struct ChunkPackages {
    std::vector<std::pair<std::string, RpkgIndex>> indexes;     // by file name; not ok when it can't be read
    std::map<std::string, std::pair<uint64_t, int64_t>> stamps; // by file name: its size and last write time
    std::string error;                                          // the folder can't be listed
};

// The indexes of a Runtime folder's IOI packages, each chunk's read once.
class RuntimeIndexes {
public:
    explicit RuntimeIndexes(std::filesystem::path p_Runtime) : m_Runtime(std::move(p_Runtime)) {}

    const ChunkPackages& Chunk(int p_Chunk);

    // The packages whose index was read so far, and the time listing and reading them took.
    size_t PackagesRead() const { return m_PackagesRead; }
    std::chrono::nanoseconds ReadTime() const { return m_ReadTime; }

private:
    std::filesystem::path m_Runtime;
    std::map<int, ChunkPackages> m_Chunks;
    size_t m_PackagesRead = 0;
    std::chrono::nanoseconds m_ReadTime {0};
};

// The sources partition p_Partition's resources copy from (indices into
// p_Tables.sources), in table order, each once.
std::vector<uint16_t> PartitionSources(const RecipeTables& p_Tables, int p_Partition);

// Where each of p_Sources is: the copy the game mounts from its chunk's
// packages (LocateNewest), in p_Sources' order.
std::vector<fr::SourceSpot> LocateSources(const RecipeTables& p_Tables, std::span<const uint16_t> p_Sources,
                                          RuntimeIndexes& p_Indexes);

// A source that can't be used, for the log. readFailed: a package could not
// be listed, opened, read or decoded, which a later start may not repeat;
// else it is missing from the game's packages or its size or hash changed.
struct SourceProblem {
    uint16_t source = 0; // index into the tables' sources
    fr::RecipeFailure failure = fr::RecipeFailure::SourceMissing;
    std::string detail; // "chunk26patch4.rpkg holds 80301 bytes, the recipe needs 80300"
    bool readFailed = false;
};

struct PartitionBuild {
    std::vector<fr::RecipeOutcome> variants; // in p_Variants' order
    std::vector<uint8_t> package;            // the buildable variants' resources, in table order; empty: none is
    bool packageFailed = false;              // BuildPatchPackage refused them (a type not 4 characters)
    size_t decoded = 0;                      // sources read and decoded
    uint64_t decodedBytes = 0;
    std::vector<SourceProblem> problems;

    // The outcome may be kept in the cache: no problem was a failed read.
    bool Cacheable() const;
};

// Reads each of p_Sources once (at p_Spots, in p_Runtime) and checks it
// against its recipe, rebuilds each resource p_Variants name (each once),
// decides which variants are buildable (fr::ResolveBuildable: a layer needs its
// base) and builds the package of the buildable ones' resources. Every read is
// bounds-checked; the sources are freed on return.
PartitionBuild BuildPartition(const RecipeTables& p_Tables, int p_Partition,
                              std::span<const fr::RecipeVariant> p_Variants, std::span<const uint16_t> p_Sources,
                              std::span<const fr::SourceSpot> p_Spots, const std::filesystem::path& p_Runtime);

enum class FileOwner {
    Mod,        // a package of the mod's
    Other,      // anything else (over 64 MiB: never the mod's)
    Unreadable, // it isn't there, or can't be opened or read in full
};

// Whose the file at p_Path is (pkg::HoldsOnly on its index, nothing decoded).
FileOwner HoldsOnlyFile(const std::filesystem::path& p_Path, std::span<const uint64_t> p_Own,
                        std::span<const uint64_t> p_Former);

// What a start does for one partition, from package indexes alone: where its
// sources are, the source key, and the cache line when it still describes
// the partition (fr::PackageCacheHit, the file the mod's with the line's
// hash: then nothing needs decoding).
struct PartitionPlan {
    std::vector<uint16_t> sources; // PartitionSources
    std::vector<fr::SourceSpot> spots;
    uint64_t sourceKey = 0;
    uint64_t recipeKey = 0; // fr::PackageRecipeKey
    std::optional<fr::PackageCacheLine> cached;
};

// p_Own: the partition's package file (chunk<P>patch290.rpkg); p_OwnRids and
// p_FormerRids: the resources a file of the mod's holds (pkg::HoldsOnly).
PartitionPlan PlanPartition(const RecipeTables& p_Tables, uint64_t p_TableHash, int p_Partition,
                            std::span<const fr::RecipeVariant> p_Variants, std::string_view p_Version,
                            RuntimeIndexes& p_Indexes, std::span<const fr::PackageCacheLine> p_Cached,
                            const std::filesystem::path& p_Own, std::span<const uint64_t> p_OwnRids,
                            std::span<const uint64_t> p_FormerRids);

// The cache line for a partition built now: each variant's outcome, and the
// package installed (empty: none).
fr::PackageCacheLine BuiltCacheLine(int p_Partition, uint64_t p_SourceKey, uint64_t p_RecipeKey,
                                    std::span<const fr::RecipeVariant> p_Variants,
                                    std::span<const fr::RecipeOutcome> p_Outcomes,
                                    std::span<const uint8_t> p_Installed);

// fr::Fnv1a64 of p_Package's entries for p_Rids, in p_Rids' order: each
// one's id, type, sizes, references and bytes, or that it is not there.
uint64_t VariantPackageHash(const ParsedPackage& p_Package, std::span<const uint64_t> p_Rids);

} // namespace pkg
