#include "TargetPool.h"

#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>

namespace fr {

namespace {

// Seeded Fisher-Yates over the whole vector.
template <class T>
void Shuffle(std::vector<T>& items, uint64_t& state) {
    for (size_t i = items.size(); i > 1; --i) {
        const size_t j = static_cast<size_t>(SplitMix64(state) % i);
        std::swap(items[i - 1], items[j]);
    }
}

// Gives each slot (in order) a distinct candidate from eligible (in order):
// one of its own kind when one is left, then whatever is left. Marks used.
void Assign(std::span<const size_t> slots, std::span<const size_t> eligible, std::span<const PoolEntry> pool,
            std::span<const TargetCandidate> candidates, CandidateTier tier, std::vector<TargetSwap>& out) {
    std::vector<bool> used(eligible.size(), false);
    std::vector<std::optional<TargetSwap>> picks(slots.size());

    // Like for like first.
    for (size_t s = 0; s < slots.size(); ++s) {
        const int32_t kind = pool[slots[s]].kind;

        if (kind == kUnknownActorKind)
            continue;

        for (size_t e = 0; e < eligible.size(); ++e) {
            if (!used[e] && candidates[eligible[e]].kind == kind) {
                used[e] = true;
                picks[s] = TargetSwap {slots[s], eligible[e], true, tier};
                break;
            }
        }
    }

    // Then whatever is left.
    for (size_t s = 0; s < slots.size(); ++s) {
        if (picks[s])
            continue;

        for (size_t e = 0; e < eligible.size(); ++e) {
            if (!used[e]) {
                used[e] = true;
                picks[s] = TargetSwap {slots[s], eligible[e], false, tier};
                break;
            }
        }
    }

    for (const auto& pick : picks) {
        if (pick)
            out.push_back(*pick);
    }
}

} // namespace

const char* CandidateTierName(CandidateTier tier) {
    switch (tier) {
        case CandidateTier::None: return "none";
        case CandidateTier::Guard: return "guard";
        case CandidateTier::Mover: return "mover";
    }
    return "unknown";
}

const char* CandidateRejectName(CandidateReject reject) {
    switch (reject) {
        case CandidateReject::None: return "eligible";
        case CandidateReject::NoHandle: return "no entity";
        case CandidateReject::Duplicate: return "duplicate";
        case CandidateReject::InPool: return "already in the pool";
        case CandidateReject::StartDisabled: return "start disabled";
        case CandidateReject::Unreadable: return "flags unreadable";
        case CandidateReject::Crowd: return "crowd character";
        case CandidateReject::NoRepoId: return "no repository id";
        case CandidateReject::Excluded: return "never a target here (main story target or map variant)";
        case CandidateReject::SharedRepoId: return "shared repository id";
        case CandidateReject::RoleActor: return "role actor";
        case CandidateReject::OtherBrick: return "not in a pool actors' brick";
        case CandidateReject::NotAMover: return "not a guard or a known mover";
    }
    return "unknown";
}

double ClampSwapShare(double share) {
    if (std::isnan(share) || share <= 0.0)
        return 0.0;
    return share >= 1.0 ? 1.0 : share;
}

double DrawSwapShare(int32_t seed, std::string_view scene, std::string_view feature) {
    // 53 bits over 2^53 - 1, so 0 and 1 are both reachable.
    uint64_t state = DialHash(seed, scene, feature, 0);
    return static_cast<double>(SplitMix64(state) >> 11) / static_cast<double>((uint64_t {1} << 53) - 1);
}

size_t TargetSwapCount(size_t poolSize, double share) {
    const double exact = ClampSwapShare(share) * static_cast<double>(poolSize);
    return std::min(poolSize, static_cast<size_t>(std::floor(exact + 0.5)));
}

bool IsStationaryEntry(const PoolEntry& entry, uint64_t missionBrickRid, const std::set<Guid>* movers, bool anyGuard) {
    // Without a table a civilian is unknown; the mission brick's dedicated
    // targets and actors of an unknown brick are never stationary here.
    if (!movers || !entry.replaceable || !entry.startEnabled.value_or(false) || entry.brickRid == 0 ||
        entry.brickRid == missionBrickRid)
        return false;

    if (anyGuard && entry.kind == kGuardActorKind)
        return false;

    return !(entry.repo && !entry.repo->IsEmpty() && movers->contains(*entry.repo));
}

std::vector<uint64_t> TargetSourceBricks(std::span<const PoolEntry> pool, uint64_t missionBrickRid) {
    std::set<uint64_t> bricks;

    for (const PoolEntry& entry : pool) {
        if (entry.startEnabled.value_or(false) && entry.brickRid != 0 && entry.brickRid != missionBrickRid)
            bricks.insert(entry.brickRid);
    }

    return {bricks.begin(), bricks.end()};
}

TargetPoolPlan PlanTargetSwaps(int32_t seed, std::string_view scene, std::span<const PoolEntry> pool,
                               std::span<const TargetCandidate> candidates, double share, uint64_t missionBrickRid,
                               const std::set<Guid>* movers, bool anyGuard, double guardShare,
                               const std::set<Guid>* excluded, StationaryRule rule) {
    const auto isExcluded = [&](const std::optional<Guid>& repo) {
        return excluded && repo && !repo->IsEmpty() && excluded->contains(*repo);
    };

    TargetPoolPlan plan;
    plan.moverTable = movers != nullptr;
    plan.anyGuard = anyGuard;
    plan.sourceBricks = TargetSourceBricks(pool, missionBrickRid);
    plan.verdicts.assign(candidates.size(), CandidateReject::None);
    plan.tiers.assign(candidates.size(), CandidateTier::None);

    std::set<uint64_t> poolHandles;
    std::map<Guid, size_t> repoUses; // over the pool and every distinct candidate

    for (const PoolEntry& entry : pool) {
        if (entry.handle != 0)
            poolHandles.insert(entry.handle);
    }

    for (const PoolEntry& entry : pool) {
        if (entry.repo && !entry.repo->IsEmpty())
            ++repoUses[*entry.repo];
    }

    std::set<uint64_t> seen;

    for (size_t i = 0; i < candidates.size(); ++i) {
        const TargetCandidate& c = candidates[i];

        if (c.handle == 0) {
            plan.verdicts[i] = CandidateReject::NoHandle;
            continue;
        }

        if (!seen.insert(c.handle).second) {
            plan.verdicts[i] = CandidateReject::Duplicate;
            continue;
        }

        // Pool members are counted with the pool already.
        if (!poolHandles.contains(c.handle) && c.repo && !c.repo->IsEmpty())
            ++repoUses[*c.repo];
    }

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (plan.verdicts[i] != CandidateReject::None)
            continue;

        const TargetCandidate& c = candidates[i];
        CandidateReject verdict = CandidateReject::None;

        if (poolHandles.contains(c.handle))
            verdict = CandidateReject::InPool;
        else if (!c.startEnabled || !c.crowd)
            verdict = CandidateReject::Unreadable;
        else if (!*c.startEnabled)
            verdict = CandidateReject::StartDisabled;
        else if (*c.crowd)
            verdict = CandidateReject::Crowd;
        else if (!c.repo || c.repo->IsEmpty())
            verdict = CandidateReject::NoRepoId;
        else if (isExcluded(c.repo))
            verdict = CandidateReject::Excluded;
        else if (repoUses[*c.repo] > 1)
            verdict = CandidateReject::SharedRepoId;
        else if (c.role)
            verdict = CandidateReject::RoleActor;
        else if (!std::binary_search(plan.sourceBricks.begin(), plan.sourceBricks.end(), c.brickRid))
            verdict = CandidateReject::OtherBrick;
        else if (anyGuard && c.kind == kGuardActorKind)
            plan.tiers[i] = CandidateTier::Guard;
        else if (movers && movers->contains(*c.repo))
            plan.tiers[i] = CandidateTier::Mover;
        else
            verdict = CandidateReject::NotAMover;

        plan.verdicts[i] = verdict;
    }

    std::vector<size_t> eligible;

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (plan.verdicts[i] == CandidateReject::None)
            eligible.push_back(i);
        else
            ++plan.rejected[plan.verdicts[i]];
    }

    // A canonical order first: the seeded shuffle then does not depend on walk order.
    // (Every eligible candidate has a repository id.)
    const auto canonical = [&](std::vector<size_t>& list) {
        std::stable_sort(list.begin(), list.end(), [&](size_t a, size_t b) {
            return std::tie(candidates[a].brickRid, candidates[a].entityId, *candidates[a].repo) <
                   std::tie(candidates[b].brickRid, candidates[b].entityId, *candidates[b].repo);
        });
    };
    canonical(eligible);

    std::vector<size_t> slots;

    for (size_t i = 0; i < pool.size(); ++i) {
        if (pool[i].replaceable)
            slots.push_back(i);
    }

    plan.replaceable = slots.size();
    plan.eligible = eligible.size();
    plan.length = pool.size();

    for (const size_t e : eligible)
        ++(plan.tiers[e] == CandidateTier::Guard ? plan.eligibleGuards : plan.eligibleMovers);

    // Stationary entries (StationaryRule), before any swap.
    std::vector<bool> stationary(pool.size(), false);

    for (size_t i = 0; i < pool.size(); ++i) {
        stationary[i] = IsStationaryEntry(pool[i], missionBrickRid, movers, anyGuard);
        plan.stationaryInPool += stationary[i] ? 1 : 0;
    }

    // An entry that should not stay: stationary, or never a target here.
    const auto unwanted = [&](size_t slot) {
        return pool[slot].replaceable && (stationary[slot] || isExcluded(pool[slot].repo));
    };

    // With the fill: a tier's shuffled slots, stationary ones first.
    const auto stationaryFirst = [&](std::vector<size_t>& list) {
        if (rule.fill)
            std::stable_partition(list.begin(), list.end(), [&](size_t slot) { return stationary[slot]; });
    };

    const auto taken = [&] {
        std::pair<std::set<size_t>, std::set<size_t>> out; // slots, candidates

        for (const TargetSwap& swap : plan.swaps) {
            out.first.insert(swap.slot);
            out.second.insert(swap.candidate);
        }

        return out;
    };

    // The fill: every unwanted slot left gets an eligible actor left,
    // excluded slots first, then stationary ones (like for like first).
    const auto fillUnwanted = [&] {
        const auto [takenSlots, takenCandidates] = taken();
        std::vector<size_t> open;

        for (size_t i = 0; i < pool.size(); ++i) {
            if (unwanted(i) && !takenSlots.contains(i))
                open.push_back(i);
        }

        std::vector<size_t> left;

        for (const size_t e : eligible) {
            if (!takenCandidates.contains(e))
                left.push_back(e);
        }

        if (open.empty() || left.empty())
            return;

        canonical(left); // the tiers shuffled their own copies
        uint64_t fillState = DialHash(seed, scene, kTargetPoolFeature, 3);
        Shuffle(open, fillState);
        Shuffle(left, fillState);
        const auto firstStationary =
            std::stable_partition(open.begin(), open.end(), [&](size_t slot) { return isExcluded(pool[slot].repo); });
        std::vector<TargetSwap> filled;
        Assign(std::span(open.begin(), firstStationary), left, pool, candidates, CandidateTier::None, filled);

        std::set<size_t> used;

        for (const TargetSwap& swap : filled)
            used.insert(swap.candidate);

        std::erase_if(left, [&](size_t e) { return used.contains(e); });
        Assign(std::span(firstStationary, open.end()), left, pool, candidates, CandidateTier::None, filled);

        for (TargetSwap& swap : filled) {
            swap.tier = plan.tiers[swap.candidate];
            swap.fill = true;
            plan.stationaryFilled += stationary[swap.slot] ? 1 : 0;
            plan.excludedForced += isExcluded(pool[swap.slot].repo) ? 1 : 0;
            plan.swaps.push_back(swap);
        }
    };

    if (!anyGuard) {
        // One tier: every listed mover onto every replaceable slot.
        plan.moverSlots = plan.replaceable;
        plan.moverWanted = TargetSwapCount(plan.moverSlots, share);
        // Never more than one slot per eligible actor: no actor appears twice.
        const size_t count = std::min(plan.moverWanted, plan.eligible);

        if (count > 0) {
            uint64_t state = DialHash(seed, scene, kTargetPoolFeature, 0);
            Shuffle(slots, state);
            stationaryFirst(slots);
            slots.resize(count);
            Shuffle(eligible, state);
            Assign(slots, eligible, pool, candidates, CandidateTier::Mover, plan.swaps);
        }

        plan.moverSwapped = plan.swaps.size();

        if (rule.fill)
            fillUnwanted();
    }
    else {
        std::vector<size_t> movers;
        std::vector<size_t> guards;

        for (const size_t e : eligible)
            (plan.tiers[e] == CandidateTier::Guard ? guards : movers).push_back(e);

        std::vector<size_t> guardSlots;
        std::vector<size_t> otherSlots;

        for (const size_t slot : slots)
            (pool[slot].kind == kGuardActorKind ? guardSlots : otherSlots).push_back(slot);

        // Tier 1: civilian movers onto the non-guard slots.
        plan.moverSlots = otherSlots.size();
        plan.moverWanted = TargetSwapCount(plan.moverSlots, share);
        const size_t moverCount = std::min(plan.moverWanted, movers.size());
        uint64_t state = DialHash(seed, scene, kTargetPoolFeature, 0);
        Shuffle(otherSlots, state);
        Shuffle(movers, state);
        stationaryFirst(otherSlots);
        Assign(std::span(otherSlots).first(moverCount), movers, pool, candidates, CandidateTier::Mover, plan.swaps);
        plan.moverSwapped = plan.swaps.size();
        otherSlots.erase(otherSlots.begin(), otherSlots.begin() + static_cast<std::ptrdiff_t>(moverCount));
        const bool anyMover = !movers.empty();

        // The fill comes before tier 2, so the guards go to stationary slots
        // before they take other guards' places.
        if (rule.fill) {
            fillUnwanted();
            const auto now = taken();
            std::erase_if(guardSlots, [&](size_t slot) { return now.first.contains(slot); });
            std::erase_if(otherSlots, [&](size_t slot) { return now.first.contains(slot); });
            std::erase_if(guards, [&](size_t e) { return now.second.contains(e); });
        }

        // Tier 2: guards onto what tier 1 (and the fill) left; guard slots
        // first, the others only when the scene has no eligible civilian mover at all.
        plan.guardRemaining = guardSlots.size() + otherSlots.size();
        plan.guardWanted = TargetSwapCount(plan.guardRemaining, guardShare);
        uint64_t guardState = DialHash(seed, scene, kTargetPoolFeature, 1);
        Shuffle(guardSlots, guardState);
        Shuffle(otherSlots, guardState);
        Shuffle(guards, guardState);

        std::vector<size_t> open = guardSlots;

        if (!anyMover)
            open.insert(open.end(), otherSlots.begin(), otherSlots.end());

        const size_t guardCount = std::min({plan.guardWanted, guards.size(), open.size()});
        const size_t before = plan.swaps.size();
        Assign(std::span(open).first(guardCount), guards, pool, candidates, CandidateTier::Guard, plan.swaps);
        plan.guardSwapped = plan.swaps.size() - before;
    }

    // Excluded pool entries the tiers left: forced swaps with what the tiers left.
    {
        std::set<size_t> takenSlots;
        std::set<size_t> takenCandidates;

        for (const TargetSwap& swap : plan.swaps) {
            takenSlots.insert(swap.slot);
            takenCandidates.insert(swap.candidate);
        }

        std::vector<size_t> forcedSlots;

        for (size_t i = 0; i < pool.size(); ++i) {
            if (!isExcluded(pool[i].repo))
                continue;

            ++plan.excludedInPool;

            if (pool[i].replaceable && !takenSlots.contains(i))
                forcedSlots.push_back(i);
            else if (!pool[i].replaceable)
                ++plan.excludedKept;
        }

        if (!forcedSlots.empty()) {
            // The canonical order again (the tiers shuffled their own copies).
            std::vector<size_t> left;

            for (const size_t e : eligible) {
                if (!takenCandidates.contains(e))
                    left.push_back(e);
            }

            canonical(left);
            uint64_t forcedState = DialHash(seed, scene, kTargetPoolFeature, 2);
            Shuffle(left, forcedState);
            std::vector<TargetSwap> forced;
            Assign(forcedSlots, left, pool, candidates, CandidateTier::None, forced);

            for (TargetSwap& swap : forced) {
                swap.tier = plan.tiers[swap.candidate];
                plan.swaps.push_back(swap);
            }

            plan.excludedForced += forced.size();
            plan.excludedKept += forcedSlots.size() - forced.size();
        }
    }

    // The trim: the unwanted entries still left are dropped; kept entries
    // from the tail move into their slots.
    if (rule.trim) {
        const std::set<size_t> takenSlots = taken().first;
        std::vector<size_t> drop;

        for (size_t i = 0; i < pool.size(); ++i) {
            if (unwanted(i) && !takenSlots.contains(i))
                drop.push_back(i);
        }

        const auto roomAbove = [&](size_t length) { return pool.size() > length ? pool.size() - length : 0; };
        const size_t room = roomAbove(rule.minLength);

        if (drop.size() > room) {
            // Excluded entries go first, down to the shorter excluded minimum
            // if need be; a seeded few stationary ones stay (and excluded ones
            // only below that minimum).
            uint64_t trimState = DialHash(seed, scene, kTargetPoolFeature, 4);
            Shuffle(drop, trimState);
            const auto firstStationary = std::stable_partition(
                drop.begin(), drop.end(), [&](size_t slot) { return isExcluded(pool[slot].repo); });
            const size_t excludedCount = static_cast<size_t>(firstStationary - drop.begin());
            const size_t excludedRoom = std::max(room, roomAbove(std::min(rule.minLength, rule.excludedMinLength)));
            const size_t excludedDropped = std::min(excludedCount, excludedRoom);
            const size_t stationaryDropped = room > excludedDropped ? room - excludedDropped : 0;
            std::vector<size_t> chosen(drop.begin(), drop.begin() + static_cast<std::ptrdiff_t>(excludedDropped));
            chosen.insert(chosen.end(), firstStationary,
                          firstStationary + static_cast<std::ptrdiff_t>(std::min(
                                                stationaryDropped, static_cast<size_t>(drop.end() - firstStationary))));
            drop = std::move(chosen);
            std::sort(drop.begin(), drop.end());
        }

        plan.drops = drop;
        plan.length = pool.size() - drop.size();
        const std::set<size_t> dropped(drop.begin(), drop.end());
        size_t from = plan.length;

        for (const size_t slot : drop) {
            plan.stationaryDropped += stationary[slot] ? 1 : 0;
            plan.excludedKept -= isExcluded(pool[slot].repo) ? 1 : 0;

            if (slot >= plan.length)
                continue;

            while (dropped.contains(from))
                ++from;

            plan.moves.push_back({from++, slot});
        }
    }

    for (const TargetSwap& swap : plan.swaps)
        plan.stationaryByShares += stationary[swap.slot] && !swap.fill ? 1 : 0;

    plan.stationaryKept =
        plan.stationaryInPool - plan.stationaryByShares - plan.stationaryFilled - plan.stationaryDropped;
    plan.wanted = plan.moverWanted + plan.guardWanted;
    plan.capped = plan.moverSwapped < plan.moverWanted || plan.guardSwapped < plan.guardWanted;
    std::sort(plan.swaps.begin(), plan.swaps.end(),
              [](const TargetSwap& a, const TargetSwap& b) { return a.slot < b.slot; });
    return plan;
}

} // namespace fr
