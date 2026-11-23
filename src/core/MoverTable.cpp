#include "MoverTable.h"

#include "Scene.h"    // ToLower
#include "Variants.h" // IsLightingOnlyVariant

#include <map>
#include <string>
#include <utility>

namespace fr {

namespace {

struct MoverRow {
    const char* scene; // lower case
    const char* repo;  // dashed GUID
    MoverTier tier;
    const char* variant {""}; // the map variant's option name; empty for the normal map
};

constexpr MoverRow kMoverRows[] = {
#include "MoverTable.inc"
};

// (lower-case scene, variant) -> its movers.
using TableKey = std::pair<std::string, std::string>;

// Built once. Malformed ids are skipped (the generator writes only parsed
// ones); an id listed twice in one table keeps its higher tier.
const std::map<TableKey, std::map<Guid, MoverTier>>& Table() {
    static const std::map<TableKey, std::map<Guid, MoverTier>> table = [] {
        std::map<TableKey, std::map<Guid, MoverTier>> out;

        for (const MoverRow& row : kMoverRows) {
            if (const auto guid = ParseGuid(row.repo); guid && !guid->IsEmpty()) {
                auto [it, added] = out[{ToLower(row.scene), row.variant}].emplace(*guid, row.tier);

                if (!added && row.tier > it->second)
                    it->second = row.tier;
            }
        }

        return out;
    }();
    return table;
}

} // namespace

const char* MoverTierName(MoverTier tier) {
    switch (tier) {
        case MoverTier::Mover: return "mover";
        case MoverTier::Good: return "good mover";
    }
    return "unknown";
}

std::optional<std::map<Guid, MoverTier>> SceneMovers(std::string_view scene, std::string_view variant) {
    const auto& table = Table();
    const auto it = table.find({ToLower(scene), std::string(variant)});

    if (it == table.end())
        return std::nullopt;

    return it->second;
}

std::optional<std::set<Guid>> KnownMovers(std::string_view scene, MoverTier minTier, std::string_view variant) {
    const auto movers = SceneMovers(scene, variant);

    if (!movers)
        return std::nullopt;

    std::set<Guid> out;

    for (const auto& [repo, tier] : *movers) {
        if (tier >= minTier)
            out.insert(repo);
    }

    return out;
}

MoverTableChoice ChooseMoverTable(std::string_view scene, std::span<const std::string_view> loadedVariants) {
    MoverTableChoice out;

    if (loadedVariants.empty())
        return out;

    out.loaded = loadedVariants.front();
    bool crowdChanged = false;

    for (const std::string_view variant : loadedVariants) {
        // A lighting-only variant has the normal map's crowd (and no rows of its own).
        if (variant.empty() || IsLightingOnlyVariant(variant))
            continue;

        if (!crowdChanged)
            out.loaded = variant;

        crowdChanged = true;

        if (Table().contains({ToLower(scene), std::string(variant)})) {
            out.variant = variant;
            return out;
        }
    }

    out.fallback = crowdChanged;
    out.lightingOnly = !crowdChanged;
    return out;
}

} // namespace fr
