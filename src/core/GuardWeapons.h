#pragma once

// Pure logic of guard weapons: weapon buckets, pools, deterministic picks,
// eligibility and log names.

#include "Guid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Feature name of weapon picks in DialHash.
inline constexpr std::string_view kWeaponsFeature = "weapons";

// EActorType::eAT_Guard.
inline constexpr int32_t kActorTypeGuard = 1;

// A weapon's bucket, from its repository entry's InventoryCategoryIcon.
enum class WeaponBucket { Pistol, Smg, AssaultRifle, Shotgun, Other };

inline constexpr size_t kWeaponBucketCount = 5;

// Per-bucket counts, indexed by WeaponBucket.
using BucketCounts = std::array<uint32_t, kWeaponBucketCount>;

// "pistol", "smg", "assaultrifle" and "shotgun" (any case) map to their
// buckets; everything else is Other.
WeaponBucket BucketOf(std::string_view inventoryCategoryIcon);

const char* BucketName(WeaponBucket bucket);

// "pistol a, smg b, assaultrifle c, shotgun d, other e".
std::string DescribeBucketCounts(const BucketCounts& counts);

// Per (from, to) bucket pair counts, indexed by TransitionIndex.
using TransitionCounts = std::array<uint32_t, kWeaponBucketCount * kWeaponBucketCount>;

size_t TransitionIndex(WeaponBucket from, WeaponBucket to);

// The non-zero pairs in index order, "pistol -> smg a, shotgun -> smg b", or
// "none".
std::string DescribeTransitions(const TransitionCounts& counts);

// One weapon of a pool. Pools are ordered by id.
struct PoolWeapon {
    Guid id;
    WeaponBucket bucket;
};

// Indices of the first occurrence of each distinct non-empty id, ordered by id
// (the pool order).
std::vector<size_t> PoolOrder(std::span<const Guid> ids);

class ItemCache;

// Weapons in pool order; sources[i] is the index into the ids weapons[i] came
// from.
struct WeaponPool {
    std::vector<PoolWeapon> weapons;
    std::vector<size_t> sources;
};

// ids (empty for an unreadable key) as a pool: PoolOrder, each weapon bucketed
// by its item's InventoryCategoryIcon (Other when it is not in items or items
// is null).
WeaponPool BuildPool(std::span<const Guid> ids, const ItemCache* items);

// True for the buckets a guard may carry and be given: pistol, SMG, assault
// rifle and shotgun (not Other).
bool IsCandidateBucket(WeaponBucket bucket);

// Indices into pool of the weapons a guard may be given (IsCandidateBucket),
// in pool order.
std::vector<size_t> WeaponCandidates(std::span<const PoolWeapon> pool);

// DialHash(seed, scene, "weapons", entityId).
uint64_t WeaponPickHash(int32_t seed, std::string_view scene, uint64_t entityId);

// The candidate hash selects (hash modulo the candidate count), or empty when
// there is none. It may be the guard's current weapon.
std::optional<size_t> PickWeapon(std::span<const PoolWeapon> pool, uint64_t hash);

// ZActor's runtime role flags.
struct RoleFlags {
    bool contractTarget = false;
    bool contractTargetLive = false;
    bool contractTargetHidden = false;
    bool activeEnforcer = false;
    bool potentialEnforcer = false;
    bool dynamicEnforcer = false;

    bool Any() const;
};

// The set flags, comma-separated ("contract target, active enforcer"), or "none".
std::string DescribeRoles(const RoleFlags& roles);

// Why an actor with a weapon key keeps it, in the order checked.
enum class WeaponSkip {
    SeedNotResolved,
    NotAGuard,
    WeaponsNotAllowed,
    RoleFlagSet,
    EntityIdUnknown,
    CurrentNotCandidate, // the current weapon is unreadable, uncached or Other
    NoCandidate,         // the kit has no candidate weapon (or its keys can't be read)
    KeyChanged,          // the picked kit key's repository id no longer reads as it did
};

inline constexpr size_t kWeaponSkipCount = 8;

const char* WeaponSkipName(WeaponSkip skip);

// What one stream-in call knows about its actor.
struct StreamInFacts {
    bool seedResolved = false;
    bool hasKit = false; // the actor's outfit kit was read
    int32_t actorType = 0;
    bool weaponsAllowed = false;
    bool rolesKnown = false; // the role flags were read
    RoleFlags roles;
    bool hasEntityId = false;
    // The current weapon's id was read, is in the item cache and its bucket
    // is a candidate bucket (so snipers and mission-specific guns are left
    // alone).
    bool currentIsCandidate = false;
};

// The first reason (in WeaponSkip order, before NoCandidate) the actor is not
// eligible, or empty when it is. Unreadable role flags count as set.
std::optional<WeaponSkip> WeaponEligibility(const StreamInFacts& facts);

// Enumerator names of EActorType, EActorRank and EOutfitType, or
// "actor type <n>", "actor rank <n>", "outfit type <n>" for other values.
std::string ActorTypeName(int32_t type);
std::string ActorRankName(int32_t rank);
std::string OutfitTypeName(int32_t type);

// One repository item: its Title, InventoryCategoryIcon and ItemType.
struct ItemInfo {
    Guid id;
    std::string title;
    std::string icon;
    std::string itemType;
};

// Repository items by id. The first item of an id wins.
class ItemCache {
public:
    explicit ItemCache(std::vector<ItemInfo> items);

    const ItemInfo* Find(const Guid& id) const;
    size_t Size() const;

private:
    std::vector<ItemInfo> m_Items; // ordered by id, unique
};

} // namespace fr
