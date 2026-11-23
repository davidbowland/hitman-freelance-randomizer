#include "GuardWeapons.h"

#include "Rng.h"
#include "Scene.h"

#include <algorithm>
#include <string>
#include <utility>

namespace fr {

WeaponBucket BucketOf(std::string_view inventoryCategoryIcon) {
    const std::string icon = ToLower(inventoryCategoryIcon);

    if (icon == "pistol")
        return WeaponBucket::Pistol;
    if (icon == "smg")
        return WeaponBucket::Smg;
    if (icon == "assaultrifle")
        return WeaponBucket::AssaultRifle;
    if (icon == "shotgun")
        return WeaponBucket::Shotgun;
    return WeaponBucket::Other;
}

const char* BucketName(WeaponBucket bucket) {
    switch (bucket) {
        case WeaponBucket::Pistol: return "pistol";
        case WeaponBucket::Smg: return "smg";
        case WeaponBucket::AssaultRifle: return "assaultrifle";
        case WeaponBucket::Shotgun: return "shotgun";
        case WeaponBucket::Other: break;
    }
    return "other";
}

std::string DescribeBucketCounts(const BucketCounts& counts) {
    std::string text;

    for (size_t i = 0; i < counts.size(); ++i) {
        text +=
            (i ? ", " : "") + std::string(BucketName(static_cast<WeaponBucket>(i))) + " " + std::to_string(counts[i]);
    }

    return text;
}

size_t TransitionIndex(WeaponBucket from, WeaponBucket to) {
    return static_cast<size_t>(from) * kWeaponBucketCount + static_cast<size_t>(to);
}

std::string DescribeTransitions(const TransitionCounts& counts) {
    std::string text;

    for (size_t i = 0; i < counts.size(); ++i) {
        if (!counts[i])
            continue;

        const auto from = static_cast<WeaponBucket>(i / kWeaponBucketCount);
        const auto to = static_cast<WeaponBucket>(i % kWeaponBucketCount);
        text += (text.empty() ? "" : ", ") + std::string(BucketName(from)) + " -> " + BucketName(to) + " " +
                std::to_string(counts[i]);
    }

    return text.empty() ? "none" : text;
}

std::vector<size_t> PoolOrder(std::span<const Guid> ids) {
    std::vector<size_t> order;

    for (size_t i = 0; i < ids.size(); ++i) {
        if (!ids[i].IsEmpty())
            order.push_back(i);
    }

    // Stable: equal ids keep their first occurrence first.
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ids[a] < ids[b]; });
    order.erase(std::unique(order.begin(), order.end(), [&](size_t a, size_t b) { return ids[a] == ids[b]; }),
                order.end());
    return order;
}

WeaponPool BuildPool(std::span<const Guid> ids, const ItemCache* items) {
    WeaponPool pool;

    for (const size_t i : PoolOrder(ids)) {
        const ItemInfo* item = items ? items->Find(ids[i]) : nullptr;
        pool.weapons.push_back({ids[i], BucketOf(item ? item->icon : std::string_view())});
        pool.sources.push_back(i);
    }

    return pool;
}

bool IsCandidateBucket(WeaponBucket bucket) {
    return bucket != WeaponBucket::Other;
}

std::vector<size_t> WeaponCandidates(std::span<const PoolWeapon> pool) {
    std::vector<size_t> candidates;

    for (size_t i = 0; i < pool.size(); ++i) {
        if (IsCandidateBucket(pool[i].bucket))
            candidates.push_back(i);
    }

    return candidates;
}

uint64_t WeaponPickHash(int32_t seed, std::string_view scene, uint64_t entityId) {
    return DialHash(seed, scene, kWeaponsFeature, entityId);
}

std::optional<size_t> PickWeapon(std::span<const PoolWeapon> pool, uint64_t hash) {
    const std::vector<size_t> candidates = WeaponCandidates(pool);

    if (candidates.empty())
        return std::nullopt;

    return candidates[hash % candidates.size()];
}

bool RoleFlags::Any() const {
    return contractTarget || contractTargetLive || contractTargetHidden || activeEnforcer || potentialEnforcer ||
           dynamicEnforcer;
}

std::string DescribeRoles(const RoleFlags& roles) {
    std::string text;
    const auto add = [&](bool set, const char* name) {
        if (set)
            text += (text.empty() ? "" : ", ") + std::string(name);
    };

    add(roles.contractTarget, "contract target");
    add(roles.contractTargetLive, "contract target live");
    add(roles.contractTargetHidden, "contract target hidden");
    add(roles.activeEnforcer, "active enforcer");
    add(roles.potentialEnforcer, "potential enforcer");
    add(roles.dynamicEnforcer, "dynamic enforcer");
    return text.empty() ? "none" : text;
}

const char* WeaponSkipName(WeaponSkip skip) {
    switch (skip) {
        case WeaponSkip::SeedNotResolved: return "seed not resolved";
        case WeaponSkip::NotAGuard: return "not a guard";
        case WeaponSkip::WeaponsNotAllowed: return "weapons not allowed";
        case WeaponSkip::RoleFlagSet: return "role flag set";
        case WeaponSkip::EntityIdUnknown: return "entity id unknown";
        case WeaponSkip::CurrentNotCandidate: return "current weapon not a candidate type";
        case WeaponSkip::NoCandidate: return "no candidate";
        case WeaponSkip::KeyChanged: return "key changed";
    }
    return "unknown";
}

std::optional<WeaponSkip> WeaponEligibility(const StreamInFacts& facts) {
    if (!facts.seedResolved)
        return WeaponSkip::SeedNotResolved;
    if (!facts.hasKit || facts.actorType != kActorTypeGuard)
        return WeaponSkip::NotAGuard;
    if (!facts.weaponsAllowed)
        return WeaponSkip::WeaponsNotAllowed;
    if (!facts.rolesKnown || facts.roles.Any())
        return WeaponSkip::RoleFlagSet;
    if (!facts.hasEntityId)
        return WeaponSkip::EntityIdUnknown;
    if (!facts.currentIsCandidate)
        return WeaponSkip::CurrentNotCandidate;
    return std::nullopt;
}

std::string ActorTypeName(int32_t type) {
    switch (type) {
        case 0: return "eAT_Civilian";
        case 1: return "eAT_Guard";
        case 2: return "eAT_Hitman";
        case 3: return "eAT_Last";
        default: break;
    }
    return "actor type " + std::to_string(type);
}

std::string ActorRankName(int32_t rank) {
    switch (rank) {
        case 0: return "AR_Default";
        case 1: return "AR_Elite";
        case 2: return "AR_Cop";
        case 3: return "AR_Goon";
        default: break;
    }
    return "actor rank " + std::to_string(rank);
}

std::string OutfitTypeName(int32_t type) {
    switch (type) {
        case 0: return "eOT_None";
        case 1: return "eOT_Suit";
        case 2: return "eOT_Guard";
        case 3: return "eOT_Worker";
        case 4: return "eOT_Waiter";
        case 5: return "eOT_LucasGrey";
        default: break;
    }
    return "outfit type " + std::to_string(type);
}

ItemCache::ItemCache(std::vector<ItemInfo> items) : m_Items(std::move(items)) {
    std::stable_sort(m_Items.begin(), m_Items.end(), [](const ItemInfo& a, const ItemInfo& b) { return a.id < b.id; });
    m_Items.erase(
        std::unique(m_Items.begin(), m_Items.end(), [](const ItemInfo& a, const ItemInfo& b) { return a.id == b.id; }),
        m_Items.end());
}

const ItemInfo* ItemCache::Find(const Guid& id) const {
    const auto it = std::lower_bound(m_Items.begin(), m_Items.end(), id,
                                     [](const ItemInfo& item, const Guid& key) { return item.id < key; });
    return it != m_Items.end() && it->id == id ? &*it : nullptr;
}

size_t ItemCache::Size() const {
    return m_Items.size();
}

} // namespace fr
