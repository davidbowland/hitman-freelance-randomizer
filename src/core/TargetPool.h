#pragma once

// Pure part of the target pool shuffle: which
// entries of a mild mission's target pool (the ZVsTargetPicker's m_aActors)
// are replaced by which other scene actors. Deterministic in (seed, scene),
// independent of engine pointers and walk order. No actor is ever in the pool
// twice (a duplicate RepositoryId breaks the target count),
// so stationary entries no eligible actor can replace are dropped instead
// (StationaryRule::trim), never below kMinTargetPoolLength (excluded ones down
// to kMinTargetPoolLengthExcluded).

#include "Guid.h"
#include "ShareRange.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Each mission load draws its own share (DrawSwapShare) between these
// (target_pool_swap_min, target_pool_swap_max).
constexpr double kDefaultTargetPoolSwapMin = 0.35;
constexpr double kDefaultTargetPoolSwapMax = 1.0;
// By default any guard may join the pool (target_pool_any_guard), mover table or not.
constexpr bool kDefaultTargetPoolAnyGuard = true;

// The Rng feature tag of the shuffle's rolls (its own stream: other
// features' rolls never move when this one changes).
constexpr std::string_view kTargetPoolFeature = "target_pool";
// The per-load share's roll: a tag of its own, so the share never correlates
// with the slot and actor picks (kTargetPoolFeature).
constexpr std::string_view kTargetPoolShareFeature = "target_pool_share";
// The guard tier's share (target_pool_any_guard): a second, independent roll.
constexpr std::string_view kTargetPoolGuardShareFeature = "target_pool_guard_share";

// An actor's kind for like-for-like swaps: its outfit kit's actor type
// (eAT_Civilian 0, eAT_Guard 1, ...), or kUnknownActorKind when unreadable.
constexpr int32_t kUnknownActorKind = -1;
// EActorType::eAT_Guard.
constexpr int32_t kGuardActorKind = 1;

// One element of the pool, in pool order.
struct PoolEntry {
    uint64_t handle = 0;      // identity within this load (the mod passes the entity pointer); 0 = none
    bool replaceable = false; // a readable ZActor element (others stay where they are)
    int32_t kind = kUnknownActorKind;
    std::optional<Guid> repo;         // its repository id, when read
    uint64_t brickRid = 0;            // the brick the walk found it in; 0 when unknown
    std::optional<bool> startEnabled; // m_bStartEnabled, when read
};

// One scene actor that could join the pool. Every field the rules need is
// passed in; unreadable facts are empty and reject the actor.
struct TargetCandidate {
    uint64_t handle = 0;              // identity within this load (entity pointer)
    uint64_t brickRid = 0;            // the brick the walk found it in: sort key
    uint64_t entityId = 0;            // its sub-entity id: sort key
    std::optional<bool> startEnabled; // m_bStartEnabled
    std::optional<bool> crowd;        // m_bCrowdCharacter
    std::optional<Guid> repo;         // RepositoryId
    std::optional<std::string> role;  // a Freelancer role (supplier, mule, ...) when it has one
    int32_t kind = kUnknownActorKind;
};

enum class CandidateReject {
    None,
    NoHandle,      // no entity
    Duplicate,     // the same entity was passed twice (only the first counts)
    InPool,        // already one of the pool's elements
    StartDisabled, // not always present: m_bStartEnabled false
    Unreadable,    // start enabled or crowd flag not read
    Crowd,         // a crowd character
    NoRepoId,      // no readable, non-empty repository id (target events carry it)
    Excluded,      // never a target here: the main story's targets, this load's map variant's (TargetPoolExclusions)
    SharedRepoId,  // its repository id is also another actor's (pool or candidate)
    RoleActor,     // a supplier, mule, lookout, assassin or suspect
    OtherBrick,    // not in a brick that supplies start-enabled pool actors (TargetSourceBricks)
    NotAMover,     // neither a guard (with anyGuard) nor a mover in the scene's mover table
};

const char* CandidateRejectName(CandidateReject reject);

// Why an eligible candidate is eligible, and which tier of swaps it serves.
enum class CandidateTier {
    None,  // not eligible
    Guard, // a guard (kGuardActorKind), mover table or not: anyGuard only
    Mover, // listed in the scene's mover table (with anyGuard: never a guard, so a civilian mover)
};

const char* CandidateTierName(CandidateTier tier);

// How many of poolSize replaceable entries to replace: share x poolSize
// rounded to nearest (halves up), share clamped to [0, 1] (NaN counts as 0).
size_t TargetSwapCount(size_t poolSize, double share);

// The share actually used: clamped to [0, 1], NaN as 0.
double ClampSwapShare(double share);

// A mission load's roll, uniform over [0, 1] (both ends included),
// deterministic in (seed, scene): a restart draws the same share (ShareAt
// puts it in the settings' range). feature
// picks the roll: kTargetPoolShareFeature for the (civilian) mover tier,
// kTargetPoolGuardShareFeature for the guard tier, independent of each other.
double DrawSwapShare(int32_t seed, std::string_view scene, std::string_view feature = kTargetPoolShareFeature);

struct TargetSwap {
    size_t slot = 0;                          // pool index replaced
    size_t candidate = 0;                     // index into the candidates passed in
    bool sameKind = false;                    // like for like (both kinds known and equal)
    CandidateTier tier = CandidateTier::None; // the replacement's tier
    bool fill = false;                        // a stationary fill (StationaryRule::fill), beyond the shares
};

// The shortest pool a trim leaves: the picker draws a few targets (at most
// five) from it, so it must never run short.
constexpr size_t kMinTargetPoolLength = 12;
// Excluded entries (never targets here: the main story mission's targets, a
// map variant's own story targets, the NPCs it strands or shuts in) are
// dropped below kMinTargetPoolLength,
// down to this: an unreachable target makes the contract impossible to
// finish, a short pool only repeats targets more often. The picker draws at
// most five targets.
constexpr size_t kMinTargetPoolLengthExcluded = 5;

// What happens to the pool's stationary entries: start-enabled scene NPCs
// (not the mission brick's dedicated targets, not an unknown brick) that are
// neither a guard (with anyGuard) nor in the scene's mover table. Only with a
// mover table: without one a civilian is unknown, not stationary. Off (the
// default) leaves the plan exactly as the shares make it.
struct StationaryRule {
    // Tier 1 takes stationary slots first; then every stationary (or
    // excluded) slot left gets an eligible actor left (like for like first),
    // before tier 2, whatever the shares.
    bool fill = false;
    // The stationary (or excluded) entries still left after the fill are
    // dropped: kept entries from the tail move into their slots and the pool
    // gets shorter, never below minLength (then a seeded few stay), but the
    // excluded ones down to min(minLength, excludedMinLength), first.
    bool trim = false;
    size_t minLength = kMinTargetPoolLength;
    size_t excludedMinLength = kMinTargetPoolLengthExcluded;
};

// A kept entry moved from the tail into a dropped entry's slot (StationaryRule::trim).
struct PoolMove {
    size_t from = 0;
    size_t to = 0;

    bool operator==(const PoolMove&) const = default;
};

// The bricks candidates may come from: those holding a start-enabled pool
// actor (the scene's own NPC bricks), never the mission brick (its dedicated
// targets) and never brick 0 (unknown). Sorted ascending.
std::vector<uint64_t> TargetSourceBricks(std::span<const PoolEntry> pool, uint64_t missionBrickRid);

struct TargetPoolPlan {
    std::vector<TargetSwap> swaps;      // by slot, ascending
    std::vector<uint64_t> sourceBricks; // TargetSourceBricks; no swaps when empty
    bool moverTable = false;            // the scene has a mover table
    bool anyGuard = false;              // the guard tier ran (anyGuard passed in)
    size_t wanted = 0;                  // moverWanted + guardWanted
    size_t replaceable = 0;             // pool entries that may be replaced
    size_t eligible = 0;                // candidates passing every rule (eligibleGuards + eligibleMovers)
    size_t eligibleGuards = 0;          // CandidateTier::Guard
    size_t eligibleMovers = 0;          // CandidateTier::Mover
    bool capped = false;                // some tier swapped fewer than it wanted
    // Tier 1, the mover table's movers. With anyGuard: civilian movers onto
    // the replaceable non-guard slots (moverSlots); without: every listed mover
    // onto every replaceable slot, like for like where possible.
    size_t moverSlots = 0;
    size_t moverWanted = 0; // TargetSwapCount(moverSlots, share)
    size_t moverSwapped = 0;
    // Tier 2 (anyGuard only), guards onto the replaceable slots tier 1 left
    // (guardRemaining): guard slots first, non-guard slots only when the scene
    // has no eligible civilian mover at all.
    size_t guardRemaining = 0;
    size_t guardWanted = 0; // TargetSwapCount(guardRemaining, guardShare)
    size_t guardSwapped = 0;
    // Excluded actors (the excluded set passed in) already in the pool: each
    // replaceable one is replaced, by the tiers or else by a forced swap
    // beyond the shares (any eligible actor left, like for like first).
    size_t excludedInPool = 0; // pool entries whose repository id is excluded
    size_t excludedForced = 0; // of those, replaced by a forced swap
    size_t excludedKept = 0;   // of those, left in the pool (unreplaceable, or no eligible actor left)
    // Stationary entries (StationaryRule): counted before any swap, then
    // where each went. inPool = byShares + filled + dropped + kept.
    size_t stationaryInPool = 0;
    size_t stationaryByShares = 0;              // replaced by the tiers or an excluded swap
    size_t stationaryFilled = 0;                // replaced by the fill
    size_t stationaryDropped = 0;               // dropped by the trim
    size_t stationaryKept = 0;                  // left in (no eligible actor left, no trim, or the minimum length)
    std::vector<size_t> drops;                  // slots the trim dropped (stationary or excluded), ascending
    std::vector<PoolMove> moves;                // by the trim, applied after the swaps
    size_t length = 0;                          // the pool's length after the plan (its size without a trim)
    std::vector<CandidateReject> verdicts;      // per candidate passed in
    std::vector<CandidateTier> tiers;           // per candidate passed in (None when rejected)
    std::map<CandidateReject, size_t> rejected; // counts, rejected candidates only
};

// Picks pool slots and a distinct eligible candidate for each. Only
// candidates in TargetSourceBricks pass; of those, a guard (kGuardActorKind)
// is eligible when anyGuard is set, and any other one only when its
// repository id is in movers (the scene's mover table, core/MoverTable.h,
// either tier); movers null (no table) leaves guards only (none without anyGuard).
//
// Without anyGuard (the old rule): min(TargetSwapCount(replaceable, share),
// eligible) slots, each taking a candidate of its own kind when one is left,
// the rest whatever is left. With anyGuard, two tiers with independent shares:
// 1. civilian movers onto min(TargetSwapCount(non-guard slots, share), movers)
//    of the replaceable non-guard slots;
// 2. guards onto min(TargetSwapCount(slots tier 1 left, guardShare), guards)
//    of the slots tier 1 left: guard slots first, the others only when no
//    civilian mover is eligible here (a scene without a mover table).
// Candidates are ordered by (brickRid, entityId, repository id) before the
// seeded shuffles, so neither pointer values nor walk order matter (input
// order only separates exact ties, which the duplicate-repo rule excludes).
//
// excluded (repository ids; null or empty: no effect, the plan is exactly as
// without it): a candidate listed there is rejected (Excluded), and a pool
// entry listed there that the tiers did not replace gets a forced swap with
// an eligible candidate the tiers left (its own kind first; a third seeded
// shuffle, so the tiers' picks never move), whatever the shares.
//
// rule (StationaryRule): with fill, tier 1 takes stationary slots first and
// the stationary or excluded slots it left get the eligible actors left (a
// fourth seeded shuffle) before tier 2; with trim, the ones still left are
// dropped (a fifth seeded shuffle picks which stay when minLength binds).
TargetPoolPlan PlanTargetSwaps(int32_t seed, std::string_view scene, std::span<const PoolEntry> pool,
                               std::span<const TargetCandidate> candidates, double share, uint64_t missionBrickRid,
                               const std::set<Guid>* movers, bool anyGuard = false, double guardShare = 0.0,
                               const std::set<Guid>* excluded = nullptr, StationaryRule rule = {});

// Whether a pool entry is stationary under the rule's definition
// (StationaryRule): never without a mover table.
bool IsStationaryEntry(const PoolEntry& entry, uint64_t missionBrickRid, const std::set<Guid>* movers, bool anyGuard);

// The pool after the plan: each swap puts candidateAt(swap.candidate) in its
// slot, then the trim's moves are applied and the pool cut to plan.length.
// pool must be the pool the plan was made for.
template <class T, class CandidateAt>
std::vector<T> ApplyTargetSwaps(std::vector<T> pool, const TargetPoolPlan& plan, CandidateAt candidateAt) {
    for (const TargetSwap& swap : plan.swaps)
        pool[swap.slot] = candidateAt(swap.candidate);

    for (const PoolMove& move : plan.moves)
        pool[move.to] = pool[move.from];

    pool.resize(plan.length);
    return pool;
}

} // namespace fr
