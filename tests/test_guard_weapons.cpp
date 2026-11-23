#include "doctest.h"

#include "GuardWeapons.h"
#include "Rng.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

using fr::WeaponBucket;

fr::Guid Id(uint64_t low) {
    return fr::Guid::FromHalves(0x1111'2222'3333'4444ull, low);
}

// Two pistols, two SMGs, one assault rifle, one shotgun, one sniper rifle; ids
// in ascending order.
std::vector<fr::PoolWeapon> SamplePool() {
    return {
        {Id(1), WeaponBucket::Pistol}, {Id(2), WeaponBucket::Pistol},       {Id(3), WeaponBucket::Smg},
        {Id(4), WeaponBucket::Smg},    {Id(5), WeaponBucket::AssaultRifle}, {Id(6), WeaponBucket::Shotgun},
        {Id(7), WeaponBucket::Other},
    };
}

fr::StreamInFacts EligibleFacts() {
    fr::StreamInFacts facts;
    facts.seedResolved = true;
    facts.hasKit = true;
    facts.actorType = fr::kActorTypeGuard;
    facts.weaponsAllowed = true;
    facts.rolesKnown = true;
    facts.hasEntityId = true;
    facts.currentIsCandidate = true;
    return facts;
}

} // namespace

TEST_CASE("bucket_mapping") {
    CHECK(fr::BucketOf("pistol") == WeaponBucket::Pistol);
    CHECK(fr::BucketOf("smg") == WeaponBucket::Smg);
    CHECK(fr::BucketOf("assaultrifle") == WeaponBucket::AssaultRifle);
    CHECK(fr::BucketOf("shotgun") == WeaponBucket::Shotgun);
    CHECK(fr::BucketOf("Pistol") == WeaponBucket::Pistol);
    CHECK(fr::BucketOf("AssaultRifle") == WeaponBucket::AssaultRifle);
    CHECK(fr::BucketOf("SMG") == WeaponBucket::Smg);
    CHECK(fr::BucketOf("sniperrifle") == WeaponBucket::Other);
    CHECK(fr::BucketOf("melee") == WeaponBucket::Other);
    CHECK(fr::BucketOf("") == WeaponBucket::Other);
    CHECK(fr::BucketOf("pistols") == WeaponBucket::Other);
}

TEST_CASE("bucket_names") {
    CHECK(std::string(fr::BucketName(WeaponBucket::Pistol)) == "pistol");
    CHECK(std::string(fr::BucketName(WeaponBucket::Smg)) == "smg");
    CHECK(std::string(fr::BucketName(WeaponBucket::AssaultRifle)) == "assaultrifle");
    CHECK(std::string(fr::BucketName(WeaponBucket::Shotgun)) == "shotgun");
    CHECK(std::string(fr::BucketName(WeaponBucket::Other)) == "other");
}

TEST_CASE("pool_order_dedupes_sorts_and_drops_empty") {
    const std::vector<fr::Guid> ids {Id(5), Id(2), fr::Guid {}, Id(5), Id(1), Id(2)};
    CHECK(fr::PoolOrder(ids) == std::vector<size_t> {4, 1, 0});
    CHECK(fr::PoolOrder({}).empty());
}

TEST_CASE("candidate_buckets") {
    CHECK(fr::IsCandidateBucket(WeaponBucket::Pistol));
    CHECK(fr::IsCandidateBucket(WeaponBucket::Smg));
    CHECK(fr::IsCandidateBucket(WeaponBucket::AssaultRifle));
    CHECK(fr::IsCandidateBucket(WeaponBucket::Shotgun));
    CHECK_FALSE(fr::IsCandidateBucket(WeaponBucket::Other));
}

TEST_CASE("candidates_are_every_bucketed_weapon") {
    const auto pool = SamplePool();
    CHECK(fr::WeaponCandidates(pool) == std::vector<size_t> {0, 1, 2, 3, 4, 5});
    CHECK(fr::WeaponCandidates({}).empty());

    // Never an Other weapon, whatever the hash.
    for (uint64_t h = 0; h < 200; ++h) {
        const auto pick = fr::PickWeapon(pool, fr::SplitMix64(h));
        REQUIRE(pick);
        CHECK(pool[*pick].bucket != WeaponBucket::Other);
    }
}

TEST_CASE("no_bucketed_weapon_gives_no_candidate") {
    const std::vector<fr::PoolWeapon> otherOnly {{Id(7), WeaponBucket::Other}, {Id(8), WeaponBucket::Other}};
    CHECK_FALSE(fr::PickWeapon(otherOnly, 1));
    CHECK_FALSE(fr::PickWeapon({}, 1));
}

TEST_CASE("pick_may_keep_a_single_candidate") {
    const std::vector<fr::PoolWeapon> onePistol {{Id(1), WeaponBucket::Pistol}, {Id(7), WeaponBucket::Other}};

    for (uint64_t h = 0; h < 20; ++h)
        CHECK(fr::PickWeapon(onePistol, h) == 0u);
}

TEST_CASE("pick_is_deterministic") {
    const auto pool = SamplePool();
    const uint64_t h = fr::WeaponPickHash(42, "assembly:/_pro/scenes/x.entity", 0xB73D9349324F8074ull);
    CHECK(h == fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "weapons", uint64_t {0xB73D9349324F8074ull}));
    CHECK(h == fr::WeaponPickHash(42, "ASSEMBLY:/_PRO/Scenes/X.entity", 0xB73D9349324F8074ull));
    CHECK(fr::PickWeapon(pool, h) == fr::PickWeapon(pool, h));
    CHECK(fr::PickWeapon(pool, h) == fr::WeaponCandidates(pool)[h % 6]);
    CHECK(h != fr::WeaponPickHash(43, "assembly:/_pro/scenes/x.entity", 0xB73D9349324F8074ull));
    CHECK(h != fr::WeaponPickHash(42, "assembly:/_pro/scenes/y.entity", 0xB73D9349324F8074ull));
    CHECK(h != fr::WeaponPickHash(42, "assembly:/_pro/scenes/x.entity", 0xB73D9349324F8075ull));
}

TEST_CASE("pick_spreads_over_candidates") {
    const auto pool = SamplePool();
    std::map<size_t, int> counts;
    constexpr int n = 6000;

    for (uint64_t id = 0; id < n; ++id) {
        const auto pick = fr::PickWeapon(pool, fr::WeaponPickHash(7, "scene", 0xABCD0000 + id));
        REQUIRE(pick);
        ++counts[*pick];
    }

    CHECK(counts.size() == 6);

    for (const auto& [index, count] : counts)
        CHECK(count > n / 6 - n / 20);
}

TEST_CASE("role_flags_describe") {
    fr::RoleFlags roles;
    CHECK_FALSE(roles.Any());
    CHECK(fr::DescribeRoles(roles) == "none");

    roles.contractTarget = true;
    roles.potentialEnforcer = true;
    CHECK(roles.Any());
    CHECK(fr::DescribeRoles(roles) == "contract target, potential enforcer");

    fr::RoleFlags all {true, true, true, true, true, true};
    CHECK(fr::DescribeRoles(all) ==
          "contract target, contract target live, contract target hidden, active enforcer, potential enforcer, "
          "dynamic enforcer");
}

TEST_CASE("eligibility_order_follows_the_skip_reasons") {
    CHECK_FALSE(fr::WeaponEligibility(EligibleFacts()));

    auto facts = EligibleFacts();
    facts.seedResolved = false;
    facts.hasKit = false;
    CHECK(fr::WeaponEligibility(facts) == fr::WeaponSkip::SeedNotResolved);

    facts = EligibleFacts();
    facts.hasKit = false;
    CHECK(fr::WeaponEligibility(facts) == fr::WeaponSkip::NotAGuard);

    facts = EligibleFacts();
    facts.actorType = 0; // eAT_Civilian
    CHECK(fr::WeaponEligibility(facts) == fr::WeaponSkip::NotAGuard);

    facts = EligibleFacts();
    facts.weaponsAllowed = false;
    CHECK(fr::WeaponEligibility(facts) == fr::WeaponSkip::WeaponsNotAllowed);

    facts = EligibleFacts();
    facts.roles.dynamicEnforcer = true;
    facts.hasEntityId = false;
    CHECK(fr::WeaponEligibility(facts) == fr::WeaponSkip::RoleFlagSet);

    // Unreadable role flags count as set.
    facts = EligibleFacts();
    facts.rolesKnown = false;
    CHECK(fr::WeaponEligibility(facts) == fr::WeaponSkip::RoleFlagSet);

    facts = EligibleFacts();
    facts.hasEntityId = false;
    facts.currentIsCandidate = false;
    CHECK(fr::WeaponEligibility(facts) == fr::WeaponSkip::EntityIdUnknown);

    facts = EligibleFacts();
    facts.currentIsCandidate = false;
    CHECK(fr::WeaponEligibility(facts) == fr::WeaponSkip::CurrentNotCandidate);
}

TEST_CASE("skip_names") {
    std::set<std::string> names;

    for (size_t i = 0; i < fr::kWeaponSkipCount; ++i)
        names.insert(fr::WeaponSkipName(static_cast<fr::WeaponSkip>(i)));

    CHECK(names == std::set<std::string> {"seed not resolved", "not a guard", "weapons not allowed", "role flag set",
                                          "entity id unknown", "current weapon not a candidate type", "no candidate",
                                          "key changed"});
}

TEST_CASE("bucket_counts_text") {
    CHECK(fr::DescribeBucketCounts({1, 2, 3, 4, 5}) == "pistol 1, smg 2, assaultrifle 3, shotgun 4, other 5");
}

TEST_CASE("kit_enum_names") {
    CHECK(fr::ActorTypeName(0) == "eAT_Civilian");
    CHECK(fr::ActorTypeName(1) == "eAT_Guard");
    CHECK(fr::ActorTypeName(9) == "actor type 9");
    CHECK(fr::ActorRankName(1) == "AR_Elite");
    CHECK(fr::ActorRankName(-1) == "actor rank -1");
    CHECK(fr::OutfitTypeName(2) == "eOT_Guard");
    CHECK(fr::OutfitTypeName(7) == "outfit type 7");
}

TEST_CASE("item_cache_lookup") {
    const fr::ItemCache cache({
        {Id(3), "Bartoli 75R", "pistol", "eItemFirearm"},
        {Id(1), "Shashka A33", "smg", "eItemFirearm"},
        {Id(3), "duplicate", "smg", "eItemFirearm"},
    });

    CHECK(cache.Size() == 2);
    REQUIRE(cache.Find(Id(3)));
    CHECK(cache.Find(Id(3))->title == "Bartoli 75R");
    REQUIRE(cache.Find(Id(1)));
    CHECK(cache.Find(Id(1))->icon == "smg");
    CHECK_FALSE(cache.Find(Id(2)));
}

namespace {

// A guard kit's items: one of each bucket plus a second SMG; Id(9) is not in
// the cache.
fr::ItemCache KitItems() {
    return fr::ItemCache({
        {Id(1), "Bartoli 75R", "pistol", "eItemFirearm"},
        {Id(3), "HX-10", "smg", "eItemFirearm"},
        {Id(4), "Shashka A33", "SMG", "eItemFirearm"},
        {Id(5), "Fusil G1-4", "assaultrifle", "eItemFirearm"},
        {Id(6), "Tactical Bartoli 12G", "shotgun", "eItemFirearm"},
        {Id(7), "Jaeger 7", "sniperrifle", "eItemFirearm"},
    });
}

} // namespace

TEST_CASE("build_pool_dedupes_orders_and_buckets") {
    const fr::ItemCache items = KitItems();
    // Kit order, with an unreadable key (empty id), a duplicate and an unknown id.
    const std::vector<fr::Guid> ids {Id(6), Id(1), fr::Guid {}, Id(9), Id(3), Id(6), Id(5)};
    const fr::WeaponPool pool = fr::BuildPool(ids, &items);

    REQUIRE(pool.weapons.size() == 5);
    REQUIRE(pool.sources.size() == 5);
    CHECK(pool.sources == std::vector<size_t> {1, 4, 6, 0, 3});

    const std::vector<WeaponBucket> buckets {WeaponBucket::Pistol, WeaponBucket::Smg, WeaponBucket::AssaultRifle,
                                             WeaponBucket::Shotgun, WeaponBucket::Other};

    for (size_t i = 0; i < pool.weapons.size(); ++i) {
        CHECK(pool.weapons[i].id == ids[pool.sources[i]]);
        CHECK(pool.weapons[i].bucket == buckets[i]);
    }

    CHECK(fr::BuildPool({}, &items).weapons.empty());
}

TEST_CASE("build_pool_without_item_cache_is_all_other") {
    const std::vector<fr::Guid> ids {Id(3), Id(1)};
    const fr::WeaponPool pool = fr::BuildPool(ids, nullptr);

    REQUIRE(pool.weapons.size() == 2);
    CHECK(pool.weapons[0].bucket == WeaponBucket::Other);
    CHECK(pool.weapons[1].bucket == WeaponBucket::Other);
    CHECK_FALSE(fr::PickWeapon(pool.weapons, 1));
}

TEST_CASE("kit_pick_ignores_kit_order") {
    const fr::ItemCache items = KitItems();
    const std::vector<fr::Guid> a {Id(1), Id(3), Id(4), Id(5), Id(6), Id(7), Id(9)};
    const std::vector<fr::Guid> b {Id(9), Id(7), Id(5), Id(4), Id(1), Id(6), Id(3), Id(5)};
    const fr::WeaponPool poolA = fr::BuildPool(a, &items);
    const fr::WeaponPool poolB = fr::BuildPool(b, &items);
    std::set<uint64_t> picked;

    for (uint64_t entity = 0; entity < 100; ++entity) {
        const uint64_t h = fr::WeaponPickHash(42, "scene", 0xB73D0000 + entity);
        const auto pickA = fr::PickWeapon(poolA.weapons, h);
        const auto pickB = fr::PickWeapon(poolB.weapons, h);
        REQUIRE(pickA);
        REQUIRE(pickB);
        CHECK(poolA.weapons[*pickA].id == poolB.weapons[*pickB].id);
        CHECK(a[poolA.sources[*pickA]] == b[poolB.sources[*pickB]]);
        CHECK(poolA.weapons[*pickA].bucket != WeaponBucket::Other);
        picked.insert(poolA.weapons[*pickA].id.Low());
    }

    // The sniper rifle (Id(7)) and the uncached Id(9) are never picked.
    CHECK(picked == std::set<uint64_t> {1, 3, 4, 5, 6});
}

TEST_CASE("transition_counts_text") {
    fr::TransitionCounts counts {};
    CHECK(fr::DescribeTransitions(counts) == "none");

    CHECK(fr::TransitionIndex(WeaponBucket::Pistol, WeaponBucket::Pistol) == 0);
    CHECK(fr::TransitionIndex(WeaponBucket::Other, WeaponBucket::Other) == counts.size() - 1);

    counts[fr::TransitionIndex(WeaponBucket::Shotgun, WeaponBucket::AssaultRifle)] = 2;
    counts[fr::TransitionIndex(WeaponBucket::Pistol, WeaponBucket::Smg)] = 40;
    CHECK(fr::DescribeTransitions(counts) == "pistol -> smg 40, shotgun -> assaultrifle 2");
}
