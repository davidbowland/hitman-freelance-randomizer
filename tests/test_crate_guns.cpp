#include "doctest.h"

#include "CrateGunMath.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kScene = "assembly:/_pro/scenes/missions/edgy/scene_edgy_mild_meyer.entity";

// Game data: the weapon pool's 13 firearms and their costs; rarities from the
// same firearms' m_fA in the options outside the stash items (1 legendary,
// 9 epic, 20 rare, 70 common).
std::vector<fr::CrateGun> LoggedWeaponPool() {
    return {
        {4, 186.0}, // Krugermeier Silver
        {2, 186.0}, // Rude Ruby
        {1, 117.0}, // ICA 19 Classic Baller
        {4, 117.0}, // ICA 19 F/A
        {2, 117.0}, // HWK 21
        {3, 186.0}, // Concept 5
        {0, 186.0}, // Pistol Striker (no rarity seen)
        {4, 117.0}, // HWK 21 Covert
        {1, 186.0}, // ICA 19
        {2, 186.0}, // ICA 19 HomeMade
        {2, 186.0}, // Custom 5mm
        {2, 186.0}, // Sawed Off Bartoli 12G
        {3, 186.0}, // DAK SMG DTI Black Covert
    };
}

double Sum(const std::vector<double>& v) {
    return std::accumulate(v.begin(), v.end(), 0.0);
}

} // namespace

TEST_CASE("crate guns: pool by option state") {
    CHECK(fr::CratePoolOfState(0) == fr::CratePool::None);
    CHECK(fr::CratePoolOfState(1) == fr::CratePool::Tool);
    CHECK(fr::CratePoolOfState(3) == fr::CratePool::Weapon);
    CHECK(fr::CratePoolOfState(2) == fr::CratePool::Other);
}

TEST_CASE("crate guns: rarity values are the vanilla curve") {
    CHECK(fr::CrateRarityValue(1) == 70.0);
    CHECK(fr::CrateRarityValue(2) == 20.0);
    CHECK(fr::CrateRarityValue(3) == 9.0);
    CHECK(fr::CrateRarityValue(4) == 1.0);
    // No rarity counts as common.
    CHECK(fr::CrateRarityValue(0) == 70.0);
    CHECK(fr::CrateRarityValue(7) == 70.0);
    CHECK(std::string_view(fr::CrateRarityName(4)) == "legendary");
}

TEST_CASE("crate guns: sanitize") {
    fr::CrateGunsConfig cfg;
    cfg.poolChance = 1.5;
    cfg.gunShare = std::numeric_limits<double>::quiet_NaN();
    cfg.rarityMult = {-1.0, 5000.0, std::numeric_limits<double>::quiet_NaN(), 3.0};
    const fr::CrateGunsConfig s = fr::SanitizeCrateGunsConfig(cfg);
    CHECK(s.poolChance == 1.0);
    CHECK(s.gunShare == fr::CrateGunsConfig {}.gunShare);
    CHECK(s.rarityMult[0] == fr::CrateGunsConfig {}.rarityMult[0]);
    CHECK(s.rarityMult[1] == 1000.0);
    CHECK(s.rarityMult[2] == fr::CrateGunsConfig {}.rarityMult[2]);
    CHECK(s.rarityMult[3] == 3.0);

    cfg.poolChance = -0.2;
    CHECK(fr::SanitizeCrateGunsConfig(cfg).poolChance == 0.0);
}

TEST_CASE("crate guns: description names the effective values") {
    const std::string s = fr::DescribeCrateGunsConfig(fr::CrateGunsConfig {});
    CHECK(s.find("gun chance 0.05") != std::string::npos);
    CHECK(s.find("gun share 0.8") != std::string::npos);
    CHECK(s.find("legendary 25") != std::string::npos);
    CHECK(s.find("epic 45") != std::string::npos);
}

TEST_CASE("crate guns: branch draw is deterministic and follows the chance") {
    const fr::CrateBranchDraw a = fr::DrawCrateBranch(12345, kScene, 3, 0.8);
    const fr::CrateBranchDraw b = fr::DrawCrateBranch(12345, kScene, 3, 0.8);
    CHECK(a.u == b.u);
    CHECK(a.weaponPool == b.weaponPool);
    CHECK(a.u >= 0.0);
    CHECK(a.u < 1.0);

    // Different opens draw differently.
    bool s_Differs = false;

    for (uint32_t n = 0; n < 8; ++n)
        s_Differs = s_Differs || fr::DrawCrateBranch(12345, kScene, n, 0.8).u != a.u;

    CHECK(s_Differs);

    // Chance 0 and 1 are absolute.
    for (uint32_t n = 0; n < 50; ++n) {
        CHECK_FALSE(fr::DrawCrateBranch(7, kScene, n, 0.0).weaponPool);
        CHECK(fr::DrawCrateBranch(7, kScene, n, 1.0).weaponPool);
    }

    // Over many campaigns and opens the share is close to the chance.
    size_t s_Weapon = 0;
    const size_t s_Total = 20'000;

    for (size_t i = 0; i < s_Total; ++i)
        s_Weapon +=
            fr::DrawCrateBranch(static_cast<int32_t>(0x5EED0000u + i / 16), kScene, static_cast<uint32_t>(i % 16), 0.2)
                .weaponPool;

    CHECK(static_cast<double>(s_Weapon) / s_Total == doctest::Approx(0.2).epsilon(0.05));
}

TEST_CASE("crate guns: branch weights: a hit forces the weapon pool, a miss is vanilla") {
    const fr::CrateBranchWeights w = fr::CrateBranchWeightsFor(true);
    CHECK(w.junk == 0.0f);
    CHECK(w.weapon == 1.0f);
    const fr::CrateBranchWeights j = fr::CrateBranchWeightsFor(false);
    CHECK(j.junk == fr::kVanillaJunkBranchWeight);
    CHECK(j.weapon == fr::kVanillaWeaponBranchWeight);
}

TEST_CASE("crate guns: merces weight is 100 / cost") {
    CHECK(fr::CrateMercesWeight(44.0) == doctest::Approx(2.2727273));
    CHECK(fr::CrateMercesWeight(1.0) == 100.0);
    CHECK(fr::CrateMercesWeight(0.0) == 100.0);
}

TEST_CASE("crate guns: weights hold the configured share against the merces option") {
    const std::vector<fr::CrateGun> guns = LoggedWeaponPool();
    const double merces = fr::CrateMercesWeight(44.0);
    fr::CrateGunsConfig cfg;

    for (const double share : {0.25, 0.5, 0.75, 0.9}) {
        cfg.gunShare = share;
        const std::vector<double> w = fr::CrateGunWeights(guns, merces, cfg);
        REQUIRE(w.size() == guns.size());
        CHECK(Sum(w) / (Sum(w) + merces) == doctest::Approx(share));
        const fr::CrateGunShares s = fr::CrateGunSharesOf(guns, w, merces);
        CHECK(s.gunShare == doctest::Approx(share));
        CHECK(std::accumulate(s.rarityShare.begin(), s.rarityShare.end(), 0.0) == doctest::Approx(1.0));
    }
}

TEST_CASE("crate guns: default multipliers flatten the rarity curve") {
    const std::vector<fr::CrateGun> guns = LoggedWeaponPool();
    const double merces = fr::CrateMercesWeight(44.0);

    fr::CrateGunsConfig vanilla;
    vanilla.rarityMult = {1.0, 1.0, 1.0, 1.0};
    const fr::CrateGunShares v = fr::CrateGunSharesOf(guns, fr::CrateGunWeights(guns, merces, vanilla), merces);
    const fr::CrateGunShares d =
        fr::CrateGunSharesOf(guns, fr::CrateGunWeights(guns, merces, fr::CrateGunsConfig {}), merces);

    const double s_VanillaTop = v.rarityShare[2] + v.rarityShare[3];
    const double s_DefaultTop = d.rarityShare[2] + d.rarityShare[3];
    // Vanilla curve: epic + legendary about 8% of the firearms' weight; the
    // defaults lift it to about a third without making them the favorites.
    CHECK(s_VanillaTop < 0.10);
    CHECK(s_DefaultTop > 0.25);
    CHECK(s_DefaultTop < 0.50);

    // A single legendary stays less likely than a single common of the same cost.
    const std::vector<fr::CrateGun> pair {{1, 117.0}, {4, 117.0}};
    const std::vector<double> w = fr::CrateGunWeights(pair, merces, fr::CrateGunsConfig {});
    CHECK(w[1] < w[0]);
    CHECK(w[1] / w[0] == doctest::Approx(25.0 / 70.0));
}

TEST_CASE("crate guns: cheaper firearms weigh more, as in vanilla") {
    const std::vector<fr::CrateGun> guns {{2, 117.0}, {2, 186.0}};
    const std::vector<double> w = fr::CrateGunWeights(guns, 1.0, fr::CrateGunsConfig {});
    CHECK(w[0] / w[1] == doctest::Approx(186.0 / 117.0));
    // An unknown cost counts as 100.
    const std::vector<fr::CrateGun> unknown {{2, 0.0}, {2, 100.0}};
    const std::vector<double> u = fr::CrateGunWeights(unknown, 1.0, fr::CrateGunsConfig {});
    CHECK(u[0] == doctest::Approx(u[1]));
}

TEST_CASE("crate guns: edge cases") {
    const std::vector<fr::CrateGun> guns = LoggedWeaponPool();
    fr::CrateGunsConfig cfg;

    // Share 0: vanilla (no firearm weight).
    cfg.gunShare = 0.0;
    CHECK(Sum(fr::CrateGunWeights(guns, 2.0, cfg)) == 0.0);

    // Share 1 or no other weight: firearms only, sum 100.
    cfg.gunShare = 1.0;
    CHECK(Sum(fr::CrateGunWeights(guns, 2.0, cfg)) == doctest::Approx(100.0));
    cfg.gunShare = 0.5;
    CHECK(Sum(fr::CrateGunWeights(guns, 0.0, cfg)) == doctest::Approx(100.0));

    // Every multiplier 0: equal weights.
    cfg.rarityMult = {0.0, 0.0, 0.0, 0.0};
    const std::vector<double> eq = fr::CrateGunWeights(guns, 2.0, cfg);
    CHECK(eq.front() == doctest::Approx(eq.back()));
    CHECK(Sum(eq) == doctest::Approx(2.0));

    // No firearms.
    CHECK(fr::CrateGunWeights({}, 2.0, fr::CrateGunsConfig {}).empty());

    // Every weight is finite and not negative.
    for (const double w : fr::CrateGunWeights(guns, 2.27, fr::CrateGunsConfig {})) {
        CHECK(std::isfinite(w));
        CHECK(w > 0.0);
    }
}

TEST_CASE("crate guns: defaults give a gun offer every few missions") {
    // A hit crate offers from the weapon pool by weight (vanilla: the merces
    // option alone); a miss is vanilla. So the chance an opened crate offers a
    // firearm through the mod is about the gun chance times the firearms' share.
    const fr::CrateGunsConfig cfg;
    const double perCrate = cfg.poolChance * cfg.gunShare;
    CHECK(perCrate >= 0.03);
    CHECK(perCrate <= 0.06);

    // 5 to 10 crates opened per mission: one gun offer every ~2.5-5 missions.
    for (const int crates : {5, 10}) {
        const double perMission = 1.0 - std::pow(1.0 - perCrate, crates);
        CHECK(1.0 / perMission >= 2.0);
        CHECK(1.0 / perMission <= 5.5);
    }
}

TEST_CASE("crate guns: a hit is steered only when a firearm can be offered") {
    using V = fr::CrateHitVerdict;
    // A miss stays a miss whatever the flags say.
    CHECK(fr::JudgeCrateHit(false, 13, 13, 0) == V::Miss);
    CHECK(fr::JudgeCrateHit(false, 13, 13, 5) == V::Miss);
    // All 13 firearms of the crates' weapon pool owned: the forced weapon pool
    // offers merces every time, so the hit is not steered.
    CHECK(fr::JudgeCrateHit(true, 13, 13, 0) == V::NoGunToOffer);
    // One gun you do not own is enough.
    CHECK(fr::JudgeCrateHit(true, 13, 13, 1) == V::Steer);
    // Flags not read (or not all): steered.
    CHECK(fr::JudgeCrateHit(true, 13, 0, 0) == V::Steer);
    CHECK(fr::JudgeCrateHit(true, 13, 12, 0) == V::Steer);
    // No firearm known at all: nothing to judge by, steered.
    CHECK(fr::JudgeCrateHit(true, 0, 0, 0) == V::Steer);
    CHECK(std::string_view(fr::CrateHitVerdictText(V::NoGunToOffer)).find("own every gun") != std::string_view::npos);
    CHECK(std::string_view(fr::CrateHitVerdictText(V::Steer)).starts_with("HIT"));
}

TEST_CASE("crate guns: the pool taken, by the offers first") {
    using P = fr::CratePool;
    // A hit: the merces option alone, 30 options in an "other" state.
    CHECK(fr::CratePoolTaken({1, 1, 0, 0}) == P::Weapon);
    // A miss: Scissors, a rusty rod, a coconut with 65 options in state 3.
    CHECK(fr::CratePoolTaken({3, 0, 0, 65}) == P::Tool);
    // A crate offering a firearm.
    CHECK(fr::CratePoolTaken({1, 1, 0, 30}) == P::Weapon);
    // No offer seen: the states decide.
    CHECK(fr::CratePoolTaken({0, 0, 65, 0}) == P::Tool);
    CHECK(fr::CratePoolTaken({0, 0, 0, 30}) == P::Weapon);
    CHECK(fr::CratePoolTaken({0, 0, 0, 0}) == P::None);
}
