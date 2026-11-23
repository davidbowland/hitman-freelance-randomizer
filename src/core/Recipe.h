#pragma once

// Recipes that rebuild the mod's package resources from vanilla resources the
// player already has, plus literal bytes (src/mod/LandslideRecipes.inc, made by
// tools/landslide/gen_package_inc.py --recipes).

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fr {

// One vanilla resource recipes copy from.
struct RecipeSource {
    uint64_t rid;
    std::string_view type; // "TEMP"
    std::string_view name; // for the log: "scenario_rat.brick", may be empty
    uint32_t size;         // decompressed size the recipe was made against
    uint64_t hash;         // fr::Fnv1a64 of the decompressed bytes
    uint8_t chunk;         // the chunk whose packages hold it
};

enum class OpKind : uint8_t { Copy, Literal };

// Copy: `source` indexes the resource's `sources`, `offset` is in that source.
// Literal: `offset` is in the literal bytes (kLiterals).
struct RecipeOp {
    OpKind kind;
    uint16_t source;
    uint32_t offset;
    uint32_t length;
};

// A package resource, its bytes given as a recipe.
struct RecipeResource {
    uint64_t rid;
    std::string_view type;
    std::string_view path;
    uint64_t sourceRid;      // the vanilla resource it was made from
    std::string_view source; // and its package
    uint64_t blueprint;      // a TEMP's blueprint reference
    uint32_t memSize;
    uint32_t vmemSize;
    std::span<const uint8_t> refFlags;
    std::span<const uint64_t> refHashes;
    int partition;
    std::span<const uint16_t> sources; // indices into kSources
    std::span<const RecipeOp> ops;
    uint32_t size; // the rebuilt bytes' size
    uint64_t hash; // and fr::Fnv1a64
};

enum class RecipeFailure {
    None,
    SourceMissing,
    SourceSize,
    SourceHash,
    OpOutOfRange,
    OutputSize,
    OutputHash,
    BaseNotBuildable, // a layer whose base failed; set by ResolveBuildable, never by ApplyRecipe
};

// The failure's name, as the enumerator is spelled ("SourceHash"), and back
// (exact; empty for any other text).
std::string_view RecipeFailureName(RecipeFailure p_Failure);
std::optional<RecipeFailure> RecipeFailureOfName(std::string_view p_Name);

// sourceRid: the source a failure is about (0 when none is).
struct RecipeResult {
    RecipeFailure failure = RecipeFailure::None;
    uint64_t sourceRid = 0;
    std::vector<uint8_t> bytes; // empty on failure
};

// None when p_Bytes have the size and hash p_Source was made against, else
// SourceSize or SourceHash.
RecipeFailure VerifyRecipeSource(const RecipeSource& p_Source, std::span<const uint8_t> p_Bytes);

// A source's decompressed bytes; verified: VerifyRecipeSource already accepted
// them (a large source shared by many resources is hashed once).
struct RecipeSourceBytes {
    std::span<const uint8_t> bytes;
    bool verified = false;
};

// Rebuilds p_Resource. p_Source gives a source's bytes by rid (nullopt when
// the game has none); unverified ones are checked with VerifyRecipeSource
// before use, and every op against its source's and p_Literals' bounds.
RecipeResult ApplyRecipe(const RecipeResource& p_Resource, std::span<const RecipeSource> p_AllSources,
                         std::span<const uint8_t> p_Literals,
                         const std::function<std::optional<RecipeSourceBytes>(uint64_t p_Rid)>& p_Source);

// fr::Fnv1a64 continued from p_Hash over p_Value's 8 bytes (little-endian),
// or over a text's length and then its bytes: the pieces of the hashes below.
uint64_t RecipeHashWord(uint64_t p_Hash, uint64_t p_Value);
uint64_t RecipeHashText(uint64_t p_Hash, std::string_view p_Text);

// fr::Fnv1a64 of everything in the recipe tables that reaches a rebuilt
// resource or its package entry (every field but the log-only source names
// and package): changes when any recipe does.
uint64_t RecipeTableHash(std::span<const RecipeSource> p_Sources, std::span<const uint8_t> p_Literals,
                         std::span<const RecipeResource> p_Resources);

// A resource's or a variant's build state.
struct RecipeOutcome {
    RecipeFailure failure = RecipeFailure::None;
    uint64_t sourceRid = 0;
};

// A full variant's resources, and the key of the variant it is a layer on
// (empty for none).
struct RecipeVariant {
    std::string_view key;
    std::span<const uint64_t> resources;
    std::string_view layerOf;
};

// Each variant's state, in p_Variants' order: the first failure among its
// resources (p_Resource gives each one's), else, for a layer, BaseNotBuildable
// with the failing base's sourceRid, or with 0 when its base is not listed,
// its key is shared by two variants, or the layers form a cycle.
std::vector<RecipeOutcome> ResolveBuildable(std::span<const RecipeVariant> p_Variants,
                                            const std::function<RecipeOutcome(uint64_t p_Rid)>& p_Resource);

} // namespace fr
