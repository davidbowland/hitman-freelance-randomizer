#include "Enforcers.h"

#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

namespace fr {

namespace {

// Streams of their own: the share and the subset kept when fewer moves are wanted than possible.
constexpr std::string_view kEnforcerShareFeature = "enforcers share";
constexpr std::string_view kEnforcerPickFeature = "enforcers pick";

// Seeded Fisher-Yates over the whole vector.
void Shuffle(std::vector<size_t>& items, uint64_t& state) {
    for (size_t i = items.size(); i > 1; --i) {
        const size_t j = static_cast<size_t>(SplitMix64(state) % i);
        std::swap(items[i - 1], items[j]);
    }
}

// A group's own stream id: its brick, kind, start state and outfit, mixed
// (start-enabled groups keep the ids they had before start state was a key).
uint64_t GroupId(uint64_t brickRid, int32_t kind, bool late, const Guid& outfit) {
    uint64_t state = brickRid ^ 0xD1B5'4A32'D192'ED03ull;
    uint64_t id = SplitMix64(state);
    id ^= outfit.High() ^ (outfit.Low() * 0x9E37'79B9'7F4A'7C15ull) ^
          (static_cast<uint64_t>(static_cast<uint32_t>(kind)) << 32);
    return late ? id ^ 0x6A09'E667'F3BC'C909ull : id;
}

// (brickRid, kind, late, outfit)
using GroupKey = std::tuple<uint64_t, int32_t, bool, Guid>;

EnforcerReject Verdict(const EnforcerActor& a, uint64_t missionBrickRid, const EnforcerInclude& include) {
    if (a.removed)
        return EnforcerReject::Removed;
    if (!a.startEnabled || !a.crowd || !a.kind || !a.outfit || !a.enforced)
        return EnforcerReject::Unreadable;
    if (!*a.startEnabled && !include.late)
        return EnforcerReject::StartDisabled;
    if (*a.crowd)
        return EnforcerReject::Crowd;
    if (a.brickRid == 0)
        return EnforcerReject::Unreadable;
    if (missionBrickRid != 0 && a.brickRid == missionBrickRid)
        return EnforcerReject::MissionBrick;
    if (a.role)
        return EnforcerReject::RoleActor;
    if (a.inTargetPool && !include.targets)
        return EnforcerReject::TargetPool;
    if (a.sentry && !include.sentries)
        return EnforcerReject::Sentry;
    if (a.outfit->IsEmpty())
        return EnforcerReject::NoOutfit;
    if (a.sharedList)
        return EnforcerReject::SharedList;
    return EnforcerReject::None;
}

} // namespace

const char* EnforcerRejectName(EnforcerReject reject) {
    switch (reject) {
        case EnforcerReject::None: return "eligible";
        case EnforcerReject::NoHandle: return "no entity";
        case EnforcerReject::Duplicate: return "duplicate";
        case EnforcerReject::Unreadable: return "unreadable";
        case EnforcerReject::StartDisabled: return "start disabled";
        case EnforcerReject::Crowd: return "crowd character";
        case EnforcerReject::MissionBrick: return "mission brick";
        case EnforcerReject::RoleActor: return "role actor";
        case EnforcerReject::TargetPool: return "possible target";
        case EnforcerReject::Sentry: return "sentry";
        case EnforcerReject::NoOutfit: return "no outfit id";
        case EnforcerReject::SharedList: return "shared list storage";
        case EnforcerReject::Removed: return "switched off by the map variant";
    }
    return "unknown";
}

const char* EnforcerInclusionName(EnforcerInclusion inclusion) {
    switch (inclusion) {
        case EnforcerInclusion::Target: return "possible targets";
        case EnforcerInclusion::Late: return "start disabled";
        case EnforcerInclusion::Sentry: return "sentries";
    }
    return "unknown";
}

const char* EnforcerTierName(EnforcerTier tier) {
    switch (tier) {
        case EnforcerTier::Group: return "same outfit";
        case EnforcerTier::Brick: return "same brick, other outfit";
        case EnforcerTier::Kind: return "other brick";
    }
    return "unknown";
}

ShareRange ClampEnforcerShares(double minShare, double maxShare) {
    return ClampShareRange(minShare, maxShare, {kDefaultEnforcerShareMin, kDefaultEnforcerShareMax});
}

double DrawEnforcerShare(int32_t seed, std::string_view scene, ShareRange range) {
    // 53 bits over 2^53 - 1, so both ends are reachable.
    uint64_t state = DialHash(seed, scene, kEnforcerShareFeature, 0);
    const double u = static_cast<double>(SplitMix64(state) >> 11) / static_cast<double>((uint64_t {1} << 53) - 1);
    return ShareAt(ClampEnforcerShares(range.min, range.max), u);
}

EnforcerPlan PlanEnforcerShuffle(int32_t seed, std::string_view scene, std::span<const EnforcerActor> actors,
                                 uint64_t missionBrickRid, double share, bool anyBrick,
                                 const EnforcerInclude& include) {
    EnforcerPlan plan;
    plan.verdicts.assign(actors.size(), EnforcerReject::None);

    std::set<uint64_t> seen;

    for (size_t i = 0; i < actors.size(); ++i) {
        const EnforcerActor& a = actors[i];
        EnforcerReject verdict = EnforcerReject::None;

        if (a.handle == 0)
            verdict = EnforcerReject::NoHandle;
        else if (!seen.insert(a.handle).second)
            verdict = EnforcerReject::Duplicate;
        else
            verdict = Verdict(a, missionBrickRid, include);

        plan.verdicts[i] = verdict;

        if (verdict != EnforcerReject::NoHandle && verdict != EnforcerReject::Duplicate && missionBrickRid != 0 &&
            a.brickRid == missionBrickRid)
            ++plan.missionBrickActors;

        if (verdict != EnforcerReject::None) {
            ++plan.rejected[verdict];

            if (verdict != EnforcerReject::Duplicate && a.enforced.value_or(0) > 0)
                ++plan.untouchedEnforcers;
        }
    }

    // Eligible actors by group; std::map keeps the groups in a fixed order.
    std::map<GroupKey, std::vector<size_t>> groups;

    // The inclusions an eligible actor needed (only those switched on can apply).
    const auto inclusions = [&](size_t i) {
        std::vector<EnforcerInclusion> out;
        if (actors[i].inTargetPool)
            out.push_back(EnforcerInclusion::Target);
        if (!*actors[i].startEnabled)
            out.push_back(EnforcerInclusion::Late);
        if (actors[i].sentry)
            out.push_back(EnforcerInclusion::Sentry);
        return out;
    };

    for (size_t i = 0; i < actors.size(); ++i) {
        if (plan.verdicts[i] != EnforcerReject::None)
            continue;

        ++plan.eligible;
        groups[{actors[i].brickRid, *actors[i].kind, !*actors[i].startEnabled, *actors[i].outfit}].push_back(i);

        for (const EnforcerInclusion inclusion : inclusions(i)) {
            EnforcerInclusionCount& count = plan.included[inclusion];
            ++count.actors;
            count.enforcers += *actors[i].enforced > 0 ? 1 : 0;
        }
    }

    plan.groups = groups.size();

    const auto canonical = [&](size_t a, size_t b) {
        return std::tie(actors[a].brickRid, actors[a].entityId) < std::tie(actors[b].brickRid, actors[b].entityId);
    };

    // Pairs one bucket's enforcers and others (shuffled from `state`) into `out`; the unpaired go to the leftovers.
    std::vector<size_t> leftEnforcers;
    std::vector<size_t> leftOthers;

    const auto pair = [&](std::vector<size_t> members, uint64_t state, EnforcerTier tier,
                          std::vector<EnforcerMove>& out) {
        // Canonical order: neither pointers nor walk order matter (input order only separates exact ties).
        std::stable_sort(members.begin(), members.end(), canonical);

        std::vector<size_t> enforcers;
        std::vector<size_t> others;

        for (const size_t i : members)
            (*actors[i].enforced > 0 ? enforcers : others).push_back(i);

        Shuffle(enforcers, state);
        Shuffle(others, state);

        const size_t count = std::min(enforcers.size(), others.size());

        for (size_t k = 0; k < count; ++k)
            out.push_back({enforcers[k], others[k], tier});

        leftEnforcers.insert(leftEnforcers.end(), enforcers.begin() + static_cast<long>(count), enforcers.end());
        leftOthers.insert(leftOthers.end(), others.begin() + static_cast<long>(count), others.end());
        return count;
    };

    std::map<EnforcerTier, std::vector<EnforcerMove>> tiers;

    for (const auto& [key, members] : groups) {
        for (const size_t i : members)
            plan.eligibleEnforcers += *actors[i].enforced > 0 ? 1 : 0;

        const uint64_t state =
            DialHash(seed, scene, kEnforcerFeature,
                     GroupId(std::get<0>(key), std::get<1>(key), std::get<2>(key), std::get<3>(key)));

        pair(members, state, EnforcerTier::Group, tiers[EnforcerTier::Group]);
    }

    // The Brick tier: leftovers by (brick, kind, start state); then the Kind tier: what is still left, by (kind,
    // start state).
    const auto regroup = [&](bool byBrick, EnforcerTier tier) {
        std::map<std::tuple<uint64_t, int32_t, bool>, std::vector<size_t>> buckets;

        for (const auto* list : {&leftEnforcers, &leftOthers}) {
            for (const size_t i : *list)
                buckets[{byBrick ? actors[i].brickRid : 0, *actors[i].kind, !*actors[i].startEnabled}].push_back(i);
        }

        leftEnforcers.clear();
        leftOthers.clear();

        for (const auto& [key, members] : buckets) {
            const uint64_t id = GroupId(std::get<0>(key), std::get<1>(key), std::get<2>(key), Guid {}) ^
                                (static_cast<uint64_t>(tier) + 1) * 0xA24B'AED4'963E'E407ull;
            pair(members, DialHash(seed, scene, kEnforcerFeature, id), tier, tiers[tier]);
        }
    };

    regroup(true, EnforcerTier::Brick);
    if (anyBrick)
        regroup(false, EnforcerTier::Kind);

    for (const auto& [tier, moves] : tiers)
        plan.available += moves.size();

    plan.share = std::isnan(share) ? 0.0 : std::clamp(share, 0.0, 1.0);
    const size_t all = plan.eligibleEnforcers + plan.untouchedEnforcers;
    plan.wanted = std::min(all, static_cast<size_t>(std::floor(plan.share * static_cast<double>(all) + 0.5)));

    // Closest tiers first; a seeded random subset of the last tier needed.
    size_t room = plan.wanted;

    for (auto& [tier, moves] : tiers) {
        if (room == 0)
            break;

        if (moves.size() > room) {
            uint64_t state = DialHash(seed, scene, kEnforcerPickFeature, static_cast<int32_t>(tier));
            std::vector<size_t> order(moves.size());
            for (size_t k = 0; k < order.size(); ++k)
                order[k] = k;
            Shuffle(order, state);
            order.resize(room);
            std::sort(order.begin(), order.end());

            std::vector<EnforcerMove> kept;
            for (const size_t k : order)
                kept.push_back(moves[k]);
            moves = std::move(kept);
        }

        room -= moves.size();

        if (!moves.empty())
            plan.movesByTier[tier] = moves.size();

        if (tier == EnforcerTier::Group) {
            std::set<GroupKey> withMoves;
            for (const EnforcerMove& move : moves)
                withMoves.insert({actors[move.from].brickRid, *actors[move.from].kind, !*actors[move.from].startEnabled,
                                  *actors[move.from].outfit});
            plan.groupsWithMoves = withMoves.size();
        }

        plan.moves.insert(plan.moves.end(), moves.begin(), moves.end());
    }

    plan.keptEnforcers = plan.eligibleEnforcers - plan.moves.size();

    for (const EnforcerMove& move : plan.moves) {
        for (const EnforcerInclusion inclusion : inclusions(move.from))
            ++plan.included[inclusion].movedFrom;
        for (const EnforcerInclusion inclusion : inclusions(move.to))
            ++plan.included[inclusion].movedTo;
    }

    std::sort(plan.moves.begin(), plan.moves.end(),
              [](const EnforcerMove& a, const EnforcerMove& b) { return a.from < b.from; });
    return plan;
}

} // namespace fr
