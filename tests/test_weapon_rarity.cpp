#include "doctest.h"

#include "WeaponRarity.h"

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

namespace {

constexpr int32_t kSeed = 540499871;

fr::Guid IdOf(uint32_t n) {
    fr::Guid id;
    id.data1 = 0x10000000u + n * 2654435761u;
    id.data2 = static_cast<uint16_t>(n);
    id.data4[7] = static_cast<uint8_t>(n);
    return id;
}

// count weapons with rarities cycling common, rare, epic, legendary; the
// pistols listed are unsilenced.
std::vector<fr::RarityWeapon> Weapons(size_t count, std::set<size_t> pistols = {}) {
    std::vector<fr::RarityWeapon> weapons;
    for (size_t i = 0; i < count; ++i)
        weapons.push_back({IdOf(static_cast<uint32_t>(i)), static_cast<int32_t>(i % 4) + 1, pistols.count(i) > 0});
    return weapons;
}

std::vector<int32_t> Before(const std::vector<fr::RarityWeapon>& weapons) {
    std::vector<int32_t> out;
    for (const auto& w : weapons)
        out.push_back(w.rarity);
    return out;
}

std::vector<int32_t> After(const fr::RarityPlan& plan) {
    std::vector<int32_t> out;
    for (const auto& c : plan.changes)
        out.push_back(c.rarity);
    return out;
}

bool AnyEpicPistol(const std::vector<fr::RarityWeapon>& weapons, const fr::RarityPlan& plan) {
    for (size_t i = 0; i < weapons.size(); ++i) {
        if (weapons[i].unsilencedPistol && plan.changes[i].rarity == fr::kRarityEpic)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("rarity values and names") {
    CHECK(fr::RarityFromValue(1.0) == 1);
    CHECK(fr::RarityFromValue(4.0) == 4);
    CHECK_FALSE(fr::RarityFromValue(0.0));
    CHECK_FALSE(fr::RarityFromValue(5.0));
    CHECK_FALSE(fr::RarityFromValue(2.5));
    CHECK(fr::RarityName(fr::kRarityEpic) == "epic");
    CHECK(fr::RarityName(9) == "rarity 9");
}

TEST_CASE("firearm icons and silenced audio") {
    CHECK(fr::IsFirearmIcon("pistol"));
    CHECK(fr::IsFirearmIcon("AssaultRifle"));
    CHECK(fr::IsFirearmIcon("sniperrifle"));
    CHECK_FALSE(fr::IsFirearmIcon("melee"));
    CHECK_FALSE(fr::IsFirearmIcon("evergreen_payout"));
    CHECK(fr::IsPistolIcon("Pistol"));
    CHECK_FALSE(fr::IsPistolIcon("smg"));
    CHECK(fr::IsSilencedAudio("eWBC_AudioHeadTailType_Silenced", ""));
    CHECK(fr::IsSilencedAudio("", "eWBC_AudioHeadTailType_SuperSilenced"));
    CHECK_FALSE(fr::IsSilencedAudio("eWBC_AudioHeadTailType_Normal", "eWBC_AudioHeadTailType_NormalSweetener"));
    CHECK_FALSE(fr::IsSilencedAudio("", ""));
}

TEST_CASE("dart guns are no unsilenced pistols") {
    constexpr const char* kDart = "eWBC_AudioExoticType_TranquilizerDartPistol";
    constexpr const char* kNone = "eWBC_AudioExoticType_None";
    constexpr const char* kNormal = "eWBC_AudioHeadTailType_Normal";

    // By AudioExoticType: the Sick (emetic) has the tranquilizer's too.
    CHECK(fr::IsDartGun(kDart, "(Dartgun) Sick"));
    CHECK(fr::IsDartGun("ewbc_audioexotictype_tranquilizerdartpistol", "(S3) (Dartgun) Kalmer 2 - Tranquilizer"));
    CHECK_FALSE(fr::IsDartGun(kNone, "(Pistol) Concept 5"));
    CHECK_FALSE(fr::IsDartGun("eWBC_AudioExoticType_MiniPistol", "(Pistol) Krugermeier .22"));
    // The key decides when present, the name only without it.
    CHECK_FALSE(fr::IsDartGun(kNone, "(Dartgun) Renamed"));
    CHECK(fr::IsDartGun("", "(S3) (Dartgun) Kalmer 2 - Tranquilizer"));
    CHECK(fr::IsDartGun("", "Dart Gun"));
    CHECK_FALSE(fr::IsDartGun("", "(Pistol) ICA 19 F/A"));

    // The departure lists' unsilenced dart guns (AudioHeadType Normal) and pistols.
    CHECK_FALSE(fr::IsUnsilencedPistol("pistol", kNormal, kNormal, kDart, "(Dartgun) Sick"));
    CHECK_FALSE(fr::IsUnsilencedPistol("pistol", kNormal, kNormal, "", "(S3) (Dartgun) Kalmer 2 - Tranquilizer"));
    CHECK(fr::IsUnsilencedPistol("pistol", kNormal, kNormal, kNone, "(Pistol) Concept 5"));
    CHECK_FALSE(
        fr::IsUnsilencedPistol("pistol", "eWBC_AudioHeadTailType_Silenced", kNormal, kNone, "(pistol) Custom 5mm"));
    CHECK_FALSE(fr::IsUnsilencedPistol("smg", kNormal, kNormal, kNone, "(SMG) TAC SMG"));
}

TEST_CASE("keep range is sanitized") {
    const auto r = fr::SanitizeKeepRange(0.2, 0.1);
    CHECK(r.min == doctest::Approx(0.1));
    CHECK(r.max == doctest::Approx(0.2));
    const auto c = fr::SanitizeKeepRange(-1.0, 3.0);
    CHECK(c.min == 0.0);
    CHECK(c.max == 1.0);
}

TEST_CASE("rarity counts are preserved and the keep share is in range") {
    for (int32_t seed : {kSeed, 1, -7, 123456}) {
        const auto weapons = Weapons(60, {3, 7}); // two unsilenced pistols, both legendary
        const auto plan = fr::PlanWeaponRarity(weapons, seed, {});
        CHECK(plan.keepShare >= 0.05);
        CHECK(plan.keepShare <= 0.10);
        // 3 to 6 kept; one fewer when the Epic rule had to take a kept pistol.
        const bool tookKept = plan.epicPistol && plan.keepCount + plan.moverCount == 59;
        CHECK(plan.keepCount >= (tookKept ? 2u : 3u));
        CHECK(plan.keepCount <= 6);
        CHECK(plan.keepCount + plan.moverCount == (tookKept ? 59u : 60u));
        CHECK(fr::CountRarities(After(plan)) == fr::CountRarities(Before(weapons)));
        CHECK((plan.epic == fr::EpicFix::SwappedWithMover || plan.epic == fr::EpicFix::AlreadyEpic));
        CHECK(AnyEpicPistol(weapons, plan));
        size_t kept = 0;
        for (size_t i = 0; i < weapons.size(); ++i) {
            if (plan.changes[i].kept) {
                ++kept;
                CHECK(plan.changes[i].rarity == weapons[i].rarity);
            }
        }
        CHECK(kept == plan.keepCount);
    }
}

TEST_CASE("the plan is deterministic and order-independent") {
    auto weapons = Weapons(40, {2, 5});
    const auto plan = fr::PlanWeaponRarity(weapons, kSeed, {});
    CHECK(After(fr::PlanWeaponRarity(weapons, kSeed, {})) == After(plan));

    auto reversed = weapons;
    std::reverse(reversed.begin(), reversed.end());
    const auto plan2 = fr::PlanWeaponRarity(reversed, kSeed, {});

    for (size_t i = 0; i < weapons.size(); ++i) {
        CHECK(plan2.changes[weapons.size() - 1 - i].rarity == plan.changes[i].rarity);
        CHECK(plan2.changes[weapons.size() - 1 - i].kept == plan.changes[i].kept);
    }

    CHECK(plan2.keepShare == plan.keepShare);
}

TEST_CASE("different seeds give different plans") {
    const auto weapons = Weapons(40);
    CHECK(After(fr::PlanWeaponRarity(weapons, 1, {})) != After(fr::PlanWeaponRarity(weapons, 2, {})));
}

TEST_CASE("every mover takes another weapon's rarity in one cycle") {
    // Distinct rarities would hide nothing; use a pattern where values differ.
    std::vector<fr::RarityWeapon> weapons;
    for (uint32_t i = 0; i < 30; ++i)
        weapons.push_back({IdOf(i), static_cast<int32_t>(i % 4) + 1, false});

    const auto plan = fr::PlanWeaponRarity(weapons, kSeed, {});
    CHECK(plan.moverCount >= 2);
    // Movers' new rarities are a permutation of the movers' old ones.
    std::vector<int32_t> from;
    std::vector<int32_t> to;
    for (size_t i = 0; i < weapons.size(); ++i) {
        if (!plan.changes[i].kept) {
            from.push_back(weapons[i].rarity);
            to.push_back(plan.changes[i].rarity);
        }
    }
    std::sort(from.begin(), from.end());
    std::sort(to.begin(), to.end());
    CHECK(from == to);
    CHECK(plan.changedCount > 0);
}

TEST_CASE("at least one is kept, and fewer than two movers means nothing moves") {
    SUBCASE("two weapons: one kept, one would move, so none move") {
        const auto weapons = Weapons(2);
        const auto plan = fr::PlanWeaponRarity(weapons, kSeed, {});
        CHECK(plan.moverCount == 0);
        CHECK(plan.keepCount == 2);
        CHECK(After(plan) == Before(weapons));
    }
    SUBCASE("three weapons: one kept, two swap") {
        const auto weapons = Weapons(3);
        const auto plan = fr::PlanWeaponRarity(weapons, kSeed, {});
        CHECK(plan.keepCount == 1);
        CHECK(plan.moverCount == 2);
        CHECK(fr::CountRarities(After(plan)) == fr::CountRarities(Before(weapons)));
    }
    SUBCASE("keep share 0 still keeps one") {
        const auto plan = fr::PlanWeaponRarity(Weapons(20), kSeed, {0.0, 0.0});
        CHECK(plan.keepCount == 1);
        CHECK(plan.moverCount == 19);
    }
    SUBCASE("no weapons") {
        const auto plan = fr::PlanWeaponRarity({}, kSeed, {});
        CHECK(plan.changes.empty());
        CHECK(plan.keepCount == 0);
        CHECK(plan.epic == fr::EpicFix::NoUnsilencedPistol);
    }
}

TEST_CASE("epic pistol rule") {
    SUBCASE("a vanilla epic pistol that is kept satisfies it") {
        // keep everything (share 1): only the rule may change anything.
        std::vector<fr::RarityWeapon> weapons {
            {IdOf(1), fr::kRarityEpic, true}, {IdOf(2), fr::kRarityCommon, true}, {IdOf(3), fr::kRarityRare, false}};
        const auto plan = fr::PlanWeaponRarity(weapons, kSeed, {1.0, 1.0});
        CHECK(plan.epic == fr::EpicFix::AlreadyEpic);
        CHECK(plan.changedCount == 0);
    }
    SUBCASE("no epic mover to swap with: forced, counts change") {
        std::vector<fr::RarityWeapon> weapons {{IdOf(1), fr::kRarityCommon, true},
                                               {IdOf(2), fr::kRarityRare, false},
                                               {IdOf(3), fr::kRarityLegendary, false}};
        const auto plan = fr::PlanWeaponRarity(weapons, kSeed, {1.0, 1.0});
        CHECK(plan.epic == fr::EpicFix::Forced);
        REQUIRE(plan.epicPistol);
        CHECK(*plan.epicPistol == 0);
        CHECK(plan.changes[0].rarity == fr::kRarityEpic);
        CHECK(plan.changes[0].epicFix);
        CHECK_FALSE(plan.changes[0].kept);
        CHECK(plan.keepCount == 2);
    }
    SUBCASE("swap with an epic non-pistol mover keeps the counts") {
        // Many weapons, one unsilenced pistol (common), epics only among non-pistols.
        std::vector<fr::RarityWeapon> weapons;
        weapons.push_back({IdOf(100), fr::kRarityCommon, true});
        for (uint32_t i = 0; i < 40; ++i)
            weapons.push_back({IdOf(i), static_cast<int32_t>(i % 4) + 1, false});

        for (int32_t seed = 1; seed <= 50; ++seed) {
            const auto plan = fr::PlanWeaponRarity(weapons, seed, {});
            CHECK(AnyEpicPistol(weapons, plan));
            CHECK(fr::CountRarities(After(plan)) == fr::CountRarities(Before(weapons)));
            if (plan.epic == fr::EpicFix::SwappedWithMover) {
                REQUIRE(plan.epicPartner);
                CHECK_FALSE(weapons[*plan.epicPartner].unsilencedPistol);
                CHECK(plan.changes[*plan.epicPartner].epicFix);
                CHECK_FALSE(plan.changes[*plan.epicPartner].kept);
            }
            else {
                CHECK(plan.epic == fr::EpicFix::AlreadyEpic);
            }
        }
    }
    SUBCASE("an epic dart gun does not satisfy it; a real pistol is made epic") {
        // An epic "(Dartgun) Sick" must not count as the
        // epic unsilenced pistol. A dart gun is a pistol but no unsilenced
        // pistol (fr::IsUnsilencedPistol), so the rule passes it by.
        const char* kNormal = "eWBC_AudioHeadTailType_Normal";
        const bool dart = fr::IsUnsilencedPistol("pistol", kNormal, kNormal,
                                                 "eWBC_AudioExoticType_TranquilizerDartPistol", "(Dartgun) Sick");
        const bool pistol =
            fr::IsUnsilencedPistol("pistol", kNormal, kNormal, "eWBC_AudioExoticType_None", "(Pistol) Concept 5");
        std::vector<fr::RarityWeapon> weapons {{IdOf(1), fr::kRarityEpic, dart},
                                               {IdOf(2), fr::kRarityLegendary, dart},
                                               {IdOf(3), fr::kRarityCommon, pistol},
                                               {IdOf(4), fr::kRarityRare, false}};
        const auto plan = fr::PlanWeaponRarity(weapons, kSeed, {1.0, 1.0});
        CHECK(plan.epic == fr::EpicFix::Forced);
        REQUIRE(plan.epicPistol);
        CHECK(*plan.epicPistol == 2);
        CHECK(plan.changes[2].rarity == fr::kRarityEpic);
        CHECK(plan.changes[0].rarity == fr::kRarityEpic); // the dart gun keeps its own

        // With movers: whatever the seed, the epic unsilenced pistol is a real one.
        std::vector<fr::RarityWeapon> many {{IdOf(200), fr::kRarityEpic, dart}, {IdOf(201), fr::kRarityCommon, pistol}};
        for (uint32_t i = 0; i < 40; ++i)
            many.push_back({IdOf(i), static_cast<int32_t>(i % 4) + 1, false});
        for (int32_t seed = 1; seed <= 50; ++seed) {
            CAPTURE(seed);
            const auto plan2 = fr::PlanWeaponRarity(many, seed, {});
            CHECK(plan2.changes[1].rarity == fr::kRarityEpic);
            CHECK(plan2.epicPistol != std::optional<size_t>(0));
        }
    }
    SUBCASE("no unsilenced pistol") {
        const auto plan = fr::PlanWeaponRarity(Weapons(10), kSeed, {});
        CHECK(plan.epic == fr::EpicFix::NoUnsilencedPistol);
        CHECK_FALSE(plan.epicPistol);
    }
}

TEST_CASE("capacity follows rarity steps") {
    CHECK(fr::CapacityAfter(3.0, 1, 1) == 3.0);
    CHECK(fr::CapacityAfter(3.0, 1, 3) == 1.0);
    CHECK(fr::CapacityAfter(3.0, 1, 4) == 1.0); // clamped to 1
    CHECK(fr::CapacityAfter(2.0, 4, 1) == 5.0);
    CHECK(fr::CapacityAfter(5.0, 1, 2) == 4.0);
    CHECK(fr::CapacityAfter(5.0, 2, 1) == 5.0); // 6 is above max(5, 5)
    CHECK(fr::CapacityAfter(6.0, 2, 1) == 6.0);
    CHECK(fr::CapacityAfter(0.0, 1, 4) == 0.0);
}

TEST_CASE("rarity counts text") {
    const std::vector<int32_t> rarities {1, 1, 2, 3, 4, 0};
    CHECK(fr::DescribeRarityCounts(fr::CountRarities(rarities)) == "common 2, rare 1, epic 1, legendary 1, other 1");
}

TEST_CASE("the rarity seed at load: rarity_seed, else the persisted campaign seed") {
    CHECK(fr::InitialRaritySeed("308605890", 2113713893) == 308605890);
    CHECK(fr::InitialRaritySeed("  -251358526 \r\n", std::nullopt) == -251358526);
    // An ini of an earlier build (no rarity_seed), blank, bad or 0: the campaign seed.
    CHECK(fr::InitialRaritySeed("", 2113713893) == 2113713893);
    CHECK(fr::InitialRaritySeed("abc", 2113713893) == 2113713893);
    CHECK(fr::InitialRaritySeed("12x", 2113713893) == 2113713893);
    CHECK(fr::InitialRaritySeed("0", 2113713893) == 2113713893);
    CHECK(fr::InitialRaritySeed("99999999999", 7) == 7);
    // Nothing known.
    CHECK_FALSE(fr::InitialRaritySeed("", std::nullopt).has_value());
    CHECK_FALSE(fr::InitialRaritySeed("", 0).has_value());
}

TEST_CASE("rarity_live parses like rarity_seed") {
    CHECK(fr::InitialRaritySeed("216926673", std::nullopt) == 216926673);
    CHECK_FALSE(fr::InitialRaritySeed("", std::nullopt).has_value());
    CHECK_FALSE(fr::InitialRaritySeed("0", std::nullopt).has_value());
}

namespace {

// A campaign seen in game: picked as the generated seed -951706414, its missions read the live seed 216926673.
constexpr int32_t kPicked = -951706414;
constexpr int32_t kLive = 216926673;
constexpr int32_t kOldSeed = 228851961;

bool Same(const fr::RarityCampaign& a, const fr::RarityCampaign& b) {
    return a.seed == b.seed && a.live == b.live;
}

} // namespace

TEST_CASE("weapon rarity campaign: a pick seen rolls once") {
    const fr::RarityCampaign old {kOldSeed, kOldSeed};
    const auto pick = fr::RarityOnPick(old, kPicked);
    CHECK(pick.step == fr::RarityStep::Reroll);
    CHECK(Same(pick.next, {kPicked, std::nullopt}));

    // The watch reporting the same pick again: nothing.
    const auto again = fr::RarityOnPick(pick.next, kPicked);
    CHECK(again.step == fr::RarityStep::Keep);
    CHECK(Same(again.next, pick.next));

    // Unknown seed: nothing.
    CHECK(fr::RarityOnPick(old, 0).step == fr::RarityStep::Keep);
    // Nothing known before: the pick rolls.
    CHECK(fr::RarityOnPick({}, kPicked).step == fr::RarityStep::Reroll);
}

TEST_CASE("weapon rarity campaign: its missions' different live seed does not roll again") {
    fr::RarityCampaign state = fr::RarityOnPick({kOldSeed, kOldSeed}, kPicked).next;

    // First mission's LoadScene: resolved from the generated seed (the pick's).
    auto step = fr::RarityOnMission(state, kPicked, false);
    CHECK(step.step == fr::RarityStep::Keep);

    // Its live seed at scene playing: bound, the rarities stay the pick's.
    step = fr::RarityOnMission(state, kLive, true);
    CHECK(step.step == fr::RarityStep::Bind);
    CHECK(Same(step.next, {kPicked, kLive}));
    state = step.next;

    // Later reads and the next mission (LoadScene from the late-persisted live seed): nothing.
    CHECK(fr::RarityOnMission(state, kLive, true).step == fr::RarityStep::Keep);
    CHECK(fr::RarityOnMission(state, kLive, false).step == fr::RarityStep::Keep);
    CHECK(fr::RarityOnMission(state, kPicked, false).step == fr::RarityStep::Keep);
    CHECK(fr::RarityOnMission(state, 0, true).step == fr::RarityStep::Keep);
    CHECK(fr::RarityOnMission(state, 0, false).step == fr::RarityStep::Keep);

    // Before the bind, a resolved seed that is neither (a stale persisted one) waits for the live read.
    CHECK(fr::RarityOnMission({kPicked, std::nullopt}, kLive, false).step == fr::RarityStep::Keep);
}

TEST_CASE("weapon rarity campaign: a new pick rolls again") {
    const fr::RarityCampaign state {kPicked, kLive};
    const auto pick = fr::RarityOnPick(state, 308605890);
    CHECK(pick.step == fr::RarityStep::Reroll);
    CHECK(Same(pick.next, {308605890, std::nullopt}));
}

TEST_CASE("weapon rarity campaign: a missed pick rolls at the other campaign's mission") {
    const fr::RarityCampaign state {kPicked, kLive};

    // LoadScene resolved from the new campaign's generated seed: rolled, live bound later.
    const auto load = fr::RarityOnMission(state, 308605890, false);
    CHECK(load.step == fr::RarityStep::Reroll);
    CHECK(Same(load.next, {308605890, std::nullopt}));
    const auto bind = fr::RarityOnMission(load.next, 2113713893, true);
    CHECK(bind.step == fr::RarityStep::Bind);
    CHECK(Same(bind.next, {308605890, 2113713893}));

    // Only a live read of another campaign: rolled from it.
    const auto live = fr::RarityOnMission(state, 2113713893, true);
    CHECK(live.step == fr::RarityStep::Reroll);
    CHECK(Same(live.next, {2113713893, 2113713893}));

    // Nothing known before: the mission's seed.
    CHECK(Same(fr::RarityOnMission({}, kLive, true).next, {kLive, kLive}));
    CHECK(Same(fr::RarityOnMission({}, kPicked, false).next, {kPicked, std::nullopt}));
}

TEST_CASE("weapon rarity campaign: a restart keeps the same rarities") {
    // The state round-trips through rarity_seed / rarity_live.
    const fr::RarityCampaign loaded {fr::InitialRaritySeed("-951706414", kLive),
                                     fr::InitialRaritySeed("216926673", std::nullopt)};
    CHECK(Same(loaded, {kPicked, kLive}));
    // After a restart the persisted seed is the live one; the generated one is gone.
    CHECK(fr::RarityOnMission(loaded, kLive, false).step == fr::RarityStep::Keep);
    CHECK(fr::RarityOnMission(loaded, kLive, true).step == fr::RarityStep::Keep);

    // An ini of an earlier build (no rarity_live), mid-campaign: the first live read binds, no roll.
    const fr::RarityCampaign upgraded {kLive, fr::InitialRaritySeed("", std::nullopt)};
    CHECK(fr::RarityOnMission(upgraded, kLive, false).step == fr::RarityStep::Keep);
    CHECK(fr::RarityOnMission(upgraded, kLive, true).step == fr::RarityStep::Bind);
    const fr::RarityCampaign upgradedPick {kPicked, std::nullopt};
    CHECK(fr::RarityOnMission(upgradedPick, kLive, true).step == fr::RarityStep::Bind);
}
