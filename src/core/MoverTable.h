#pragma once

// The per-scene mover table (src/core/MoverTable.inc, measured in game and
// maintained by hand as data): for each Freelancer mild mission scene
// recorded so far, the repository ids of the actors that moved in every
// recording of the scene, each with a tier: MoverTier::Good for those that
// moved enough to make a target worth hunting ("good movers"),
// MoverTier::Mover for the rest. The target
// pool shuffle swaps in civilians listed for the scene (either tier) and,
// with target_pool_any_guard, any guard; a scene without a table gets guards
// only. Stationary actors are never listed (party guests standing in groups).
//
// A map variant (VariantOption, Variants.h) brings its own crowd, so its
// recordings build rows of their own, tagged with the option's name (The Drop:
// "the_drop"): they apply only while that variant is loaded, and the normal
// map's rows come only from recordings of the normal map. A variant recorded only
// on the showdown gets mild rows of the mild scene's normal rows plus the
// showdown recording's movers on the variant's own bricks, until a mild recording of
// it replaces them. A variant with no rows
// of its own falls back to the normal map's (ChooseMoverTable). A lighting-only
// variant (fr::IsLightingOnly: Night, The Icon's lighting)
// keeps the normal map's crowd: it never has rows of its own, its recordings
// build the normal rows, and while it is loaded the normal rows are used.
// A full variant's own scene has its rows as the scene's normal rows; a full
// variant that is a layer on another's scene (the full Bad Boy on the full
// Icon's _scene_copperhead) adds people to it, so its rows are tagged with its
// key ("badboy", fr::MoverTableVariants) and fall back to the base's.

#include "Guid.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>

namespace fr {

// A row's tier; higher is stricter.
enum class MoverTier : uint8_t {
    Mover = 1, // moved in every recording of the scene
    Good = 2,  // and moved far enough to be worth hunting in every one
};

const char* MoverTierName(MoverTier tier);

// The scene's listed movers with their tiers (scene path compared without
// case) for a map variant (VariantOption::name, exactly; empty: the normal
// map's rows), or empty when the table has no entry for that scene and
// variant. No fallback here: see ChooseMoverTable.
std::optional<std::map<Guid, MoverTier>> SceneMovers(std::string_view scene, std::string_view variant = {});

// The scene's movers of at least minTier (by default the good movers) for a
// map variant (empty: the normal map), or empty when the table has no entry
// for that scene and variant.
std::optional<std::set<Guid>> KnownMovers(std::string_view scene, MoverTier minTier = MoverTier::Good,
                                          std::string_view variant = {});

// Whose rows the target pool uses in a scene loaded with loadedVariants
// (option names, most specific first: fr::LoadedVariants): the first loaded
// variant with rows of its own for the scene, otherwise the normal map's.
// Lighting-only variants (fr::IsLightingOnlyVariant) are the normal map here.
struct MoverTableChoice {
    std::string_view variant;  // the variant whose rows are used, or empty: the normal map's
    std::string_view loaded;   // the first loaded variant that changes the crowd, else the first loaded (empty: the
                               // normal map is loaded)
    bool fallback = false;     // a variant that changes the crowd is loaded but none has rows here: the normal map's
                               // rows are used
    bool lightingOnly = false; // variants are loaded, all lighting only: the normal map's rows are used, as its crowd
};

MoverTableChoice ChooseMoverTable(std::string_view scene, std::span<const std::string_view> loadedVariants);

} // namespace fr
