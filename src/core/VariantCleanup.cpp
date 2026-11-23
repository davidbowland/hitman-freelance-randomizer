#include "VariantCleanup.h"

#include <algorithm>
#include <cstdio>

namespace fr {

std::vector<VariantRemoval> VariantRemovals(std::span<const uint64_t> loadedBrickRids, bool fullLandslide,
                                            std::span<const VariantOption* const> options) {
    const auto isLoaded = [&](const VariantBrick* brick) {
        return std::find(loadedBrickRids.begin(), loadedBrickRids.end(), brick->rid) != loadedBrickRids.end();
    };
    std::vector<VariantRemoval> out;
    std::set<Guid> seen;

    for (const VariantOption* option : options) {
        if (!option || option->removed.empty() || option->bricks.empty() || (fullLandslide && option == &kLandslide) ||
            !std::all_of(option->bricks.begin(), option->bricks.end(), isLoaded))
            continue;

        for (const StrandedNpc& npc : option->removed) {
            const auto guid = ParseGuid(npc.repo);

            if (guid && !guid->IsEmpty() && seen.insert(*guid).second)
                out.push_back({option, &npc});
        }
    }

    return out;
}

std::set<Guid> VariantRemovedRepos(std::span<const uint64_t> loadedBrickRids, bool fullLandslide,
                                   std::span<const VariantOption* const> options) {
    std::set<Guid> out;

    for (const VariantRemoval& removal : VariantRemovals(loadedBrickRids, fullLandslide, options))
        out.insert(*ParseGuid(removal.npc->repo));

    return out;
}

bool IsStrandedNpc(const StrandedNpc& npc, const Guid& repo, uint64_t brickRid) {
    const auto guid = ParseGuid(npc.repo);
    return guid && !guid->IsEmpty() && *guid == repo && (npc.brickRid == 0 || npc.brickRid == brickRid);
}

std::vector<size_t> CleanupBrickOrder(std::span<const uint64_t> loadedBrickRids,
                                      std::span<const VariantRemoval> removals) {
    const auto named = [&](uint64_t rid) {
        return std::any_of(removals.begin(), removals.end(),
                           [&](const VariantRemoval& removal) { return removal.npc->brickRid == rid; });
    };
    const bool anywhere = std::any_of(removals.begin(), removals.end(),
                                      [](const VariantRemoval& removal) { return removal.npc->brickRid == 0; });
    std::vector<size_t> order;

    for (size_t i = 0; i < loadedBrickRids.size(); ++i) {
        if (loadedBrickRids[i] != 0 && named(loadedBrickRids[i]))
            order.push_back(i);
    }

    if (anywhere) {
        for (size_t i = 0; i < loadedBrickRids.size(); ++i) {
            if (std::find(order.begin(), order.end(), i) == order.end())
                order.push_back(i);
        }
    }

    return order;
}

CleanupPool DecideCleanupPool(bool showdown, bool missionBrickLoaded, size_t pickers, size_t pickersRead) {
    if (!missionBrickLoaded)
        return CleanupPool::Unread;
    if (pickers == 0)
        return showdown ? CleanupPool::None : CleanupPool::Unread;
    return pickersRead < pickers ? CleanupPool::Unread : CleanupPool::Read;
}

CleanupAction DecideCleanup(const CleanupFacts& facts) {
    if (!facts.found)
        return CleanupAction::NotFound;
    if (facts.inPool)
        return CleanupAction::InPool;
    if (!facts.readable)
        return CleanupAction::Unreadable;
    return facts.startEnabled ? CleanupAction::SwitchOff : CleanupAction::AlreadyOff;
}

std::string_view CleanupActionText(CleanupAction action) {
    switch (action) {
        case CleanupAction::SwitchOff: return "switched off";
        case CleanupAction::InPool: return "left in: in the target pool (a possible target)";
        case CleanupAction::AlreadyOff: return "already start disabled";
        case CleanupAction::NotFound: return "not found in its brick";
        case CleanupAction::Unreadable: return "actor unreadable; left in";
    }
    return "unknown";
}

std::string StrandedNpcWhere(const StrandedNpc& npc) {
    if (!npc.seen)
        return {};

    char text[96];
    std::snprintf(text, sizeof(text), " (seen at (%.2f, %.2f, %.2f))", static_cast<double>(npc.seen->x),
                  static_cast<double>(npc.seen->y), static_cast<double>(npc.seen->z));
    return text;
}

} // namespace fr
