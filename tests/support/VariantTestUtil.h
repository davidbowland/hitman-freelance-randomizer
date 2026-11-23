#pragma once

// Variant test helpers: two vanilla bricks no option loads (for made-up
// options), and the exclusions of an option that do not parse.

#include "Guid.h"
#include "Variants.h"

#include <string_view>
#include <vector>

namespace fr::test {

// The Reflection's diner and stage (scenario_bellini loads it).
inline constexpr VariantBrick kScenarioBelliniEaBrick {
    "scenario_bellini_ea", "assembly:/_pro/scenes/missions/hokkaido/scenario_bellini_ea.brick", 0x00034828E26EFCC6ull};
// The Harbinger's lab and party dressing (scenario_filur loads it).
inline constexpr VariantBrick kScenarioFilurEaBrick {
    "scenario_filur_ea", "assembly:/_pro/scenes/missions/ancestral/scenario_filur_ea.brick", 0x007F3515FFDFA46Eull};

// The notTargets and removed repository ids of option that do not parse as a
// nonzero GUID (a table mistake).

inline std::vector<std::string_view> MalformedVariantExclusions(const VariantOption& option) {
    std::vector<std::string_view> out;

    const auto check = [&](std::string_view text) {
        const auto guid = ParseGuid(text);

        if (!guid || guid->IsEmpty())
            out.push_back(text);
    };

    for (const std::string_view text : option.notTargets)
        check(text);

    for (const StrandedNpc& npc : option.removed)
        check(npc.repo);

    return out;
}

} // namespace fr::test
