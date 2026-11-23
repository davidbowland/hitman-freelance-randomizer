#include "DoorLocks.h"

#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

namespace fr {

namespace {

// Streams of their own: the share, which locked doors unlock, which unlocked doors lock.
constexpr std::string_view kDoorShareFeature = "door locks share";
constexpr std::string_view kDoorUnlockFeature = "door locks unlock";
constexpr std::string_view kDoorLockPickFeature = "door locks lock";

// Seeded Fisher-Yates over the whole vector.
void Shuffle(std::vector<size_t>& items, uint64_t& state) {
    for (size_t i = items.size(); i > 1; --i) {
        const size_t j = static_cast<size_t>(SplitMix64(state) % i);
        std::swap(items[i - 1], items[j]);
    }
}

// A brick's own stream id.
uint64_t BrickStream(uint64_t brickRid) {
    uint64_t state = brickRid ^ 0x6A09'E667'F3BC'C909ull;
    return SplitMix64(state);
}

DoorReject Verdict(const DoorInput& d, uint64_t missionBrickRid) {
    if (!d.doorClass)
        return DoorReject::NotDoorClass;
    if (d.lockHandle == 0 || d.brickRid == 0 || !d.locked || !d.lockpickable || !d.closed)
        return DoorReject::Unreadable;
    if (missionBrickRid != 0 && d.brickRid == missionBrickRid)
        return DoorReject::MissionBrick;
    if (!*d.lockpickable)
        return DoorReject::NotLockpickable;
    if (!*d.closed)
        return DoorReject::Open;
    if (d.openMode && *d.openMode == kDoorOpenDisabled)
        return DoorReject::Disabled;
    if (d.openMode && *d.openMode != kDoorOpenTwoWay)
        return DoorReject::OneWay;
    return DoorReject::None;
}

} // namespace

const char* DoorRejectName(DoorReject reject) {
    switch (reject) {
        case DoorReject::None: return "eligible";
        case DoorReject::NoHandle: return "no entity";
        case DoorReject::Duplicate: return "duplicate";
        case DoorReject::NotDoorClass: return "not a door class";
        case DoorReject::Unreadable: return "unreadable";
        case DoorReject::MissionBrick: return "mission brick";
        case DoorReject::NotLockpickable: return "not lockpickable (keycard, keypad or scripted)";
        case DoorReject::Open: return "open";
        case DoorReject::OneWay: return "one-way";
        case DoorReject::Disabled: return "disabled";
        case DoorReject::SharedLock: return "shared lock entity";
    }
    return "unknown";
}

const char* DoorTierName(DoorTier tier) {
    switch (tier) {
        case DoorTier::Brick: return "same brick";
        case DoorTier::Other: return "other brick";
    }
    return "unknown";
}

ShareRange ClampDoorLockShares(double minShare, double maxShare) {
    return ClampShareRange(minShare, maxShare, {kDefaultDoorLockShareMin, kDefaultDoorLockShareMax});
}

double DrawDoorLockShare(int32_t seed, std::string_view scene, ShareRange range) {
    // 53 bits over 2^53 - 1, so both ends are reachable.
    uint64_t state = DialHash(seed, scene, kDoorShareFeature, 0);
    const double u = static_cast<double>(SplitMix64(state) >> 11) / static_cast<double>((uint64_t {1} << 53) - 1);
    return ShareAt(ClampDoorLockShares(range.min, range.max), u);
}

DoorPlan PlanDoorLocks(int32_t seed, std::string_view scene, std::span<const DoorInput> doors, uint64_t missionBrickRid,
                       double share) {
    DoorPlan plan;
    plan.verdicts.assign(doors.size(), DoorReject::None);

    // Lock entities used by more than one distinct door: neither may move.
    std::map<uint64_t, std::set<uint64_t>> lockUsers;

    for (const DoorInput& d : doors) {
        if (d.handle != 0 && d.lockHandle != 0)
            lockUsers[d.lockHandle].insert(d.handle);
    }

    std::set<uint64_t> seen;

    for (size_t i = 0; i < doors.size(); ++i) {
        const DoorInput& d = doors[i];
        DoorReject verdict = DoorReject::None;

        if (d.handle == 0)
            verdict = DoorReject::NoHandle;
        else if (!seen.insert(d.handle).second)
            verdict = DoorReject::Duplicate;
        else if (d.lockHandle != 0 && lockUsers[d.lockHandle].size() > 1)
            verdict = DoorReject::SharedLock;
        else
            verdict = Verdict(d, missionBrickRid);

        plan.verdicts[i] = verdict;

        if (verdict != DoorReject::None) {
            ++plan.rejected[verdict];

            if (verdict != DoorReject::Duplicate && d.locked.value_or(false))
                ++plan.untouchedLocked;
        }
    }

    const auto canonical = [&](size_t a, size_t b) {
        return std::tie(doors[a].brickRid, doors[a].entityId) < std::tie(doors[b].brickRid, doors[b].entityId);
    };

    std::vector<size_t> locked;
    std::vector<size_t> unlocked;

    for (size_t i = 0; i < doors.size(); ++i) {
        if (plan.verdicts[i] != DoorReject::None)
            continue;

        ++plan.eligible;
        (*doors[i].locked ? locked : unlocked).push_back(i);
    }

    // Canonical order: neither pointers nor walk order matter (input order only separates exact ties).
    std::stable_sort(locked.begin(), locked.end(), canonical);
    std::stable_sort(unlocked.begin(), unlocked.end(), canonical);
    plan.eligibleLocked = locked.size();
    plan.eligibleUnlocked = unlocked.size();

    plan.share = std::isnan(share) ? 0.0 : std::clamp(share, 0.0, 1.0);
    const size_t byShare =
        std::min(locked.size(), static_cast<size_t>(std::floor(plan.share * static_cast<double>(locked.size()) + 0.5)));
    plan.wanted = std::min(byShare, unlocked.size());
    plan.capped = byShare > unlocked.size();

    if (plan.wanted == 0)
        return plan;

    // Which locked doors unlock: a seeded subset, then back in canonical order.
    std::vector<size_t> unlocking = locked;
    uint64_t unlockState = DialHash(seed, scene, kDoorUnlockFeature, 0);
    Shuffle(unlocking, unlockState);
    unlocking.resize(plan.wanted);
    std::stable_sort(unlocking.begin(), unlocking.end(), canonical);

    // The unlocked doors to lock: per brick, each brick's own seeded order; and one seeded order of all.
    std::map<uint64_t, std::vector<size_t>> byBrick;

    for (const size_t i : unlocked)
        byBrick[doors[i].brickRid].push_back(i);

    for (auto& [brick, members] : byBrick) {
        uint64_t state = DialHash(seed, scene, kDoorLockPickFeature, BrickStream(brick));
        Shuffle(members, state);
    }

    std::vector<size_t> anyBrick = unlocked;
    uint64_t anyState = DialHash(seed, scene, kDoorLockPickFeature, 0);
    Shuffle(anyBrick, anyState);

    std::set<size_t> used;
    std::map<uint64_t, size_t> nextInBrick;
    size_t nextAny = 0;

    // Same-brick partners first for every unlocking door, so an early door's
    // fallback never takes a later door's same-brick partner.
    std::vector<std::optional<size_t>> partner(unlocking.size());

    for (size_t k = 0; k < unlocking.size(); ++k) {
        const auto it = byBrick.find(doors[unlocking[k]].brickRid);

        if (it == byBrick.end())
            continue;

        size_t& next = nextInBrick[it->first];

        if (next < it->second.size()) {
            partner[k] = it->second[next++];
            used.insert(*partner[k]);
        }
    }

    for (size_t k = 0; k < unlocking.size(); ++k) {
        DoorTier tier = DoorTier::Brick;

        if (!partner[k]) {
            while (nextAny < anyBrick.size() && used.contains(anyBrick[nextAny]))
                ++nextAny;

            if (nextAny == anyBrick.size())
                break; // cannot happen: wanted <= eligible unlocked

            partner[k] = anyBrick[nextAny];
            used.insert(anyBrick[nextAny]);
            tier = DoorTier::Other;
        }

        plan.moves.push_back({unlocking[k], *partner[k], tier});
        ++plan.movesByTier[tier];
    }

    std::sort(plan.moves.begin(), plan.moves.end(),
              [](const DoorMove& a, const DoorMove& b) { return a.unlock < b.unlock; });
    return plan;
}

bool DoorWatchClock::Due(double seconds) {
    if (std::isfinite(seconds) && seconds > 0.0)
        m_Elapsed += seconds;

    if (m_Elapsed < kDoorWatchPollSeconds)
        return false;

    m_Elapsed = 0.0;
    return true;
}

DoorWatchAction DecideWatchedDoor(const DoorWatchRead& read) {
    if (!read.open.value_or(false))
        return DoorWatchAction::Keep;

    // An unreadable lock counts as locked: unlocking an unlocked door is harmless.
    return read.locked == std::optional<bool>(false) ? DoorWatchAction::Drop : DoorWatchAction::Unlock;
}

std::vector<DoorWatchAction> DecideWatchedDoors(std::span<const DoorWatchRead> reads) {
    std::vector<DoorWatchAction> out;
    out.reserve(reads.size());

    for (const DoorWatchRead& read : reads)
        out.push_back(DecideWatchedDoor(read));

    return out;
}

} // namespace fr
