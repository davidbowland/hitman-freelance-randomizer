#include "doctest.h"

#include "Config.h"

#include <array>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("off_list_parse") {
    std::vector<std::string> unknown;
    const auto off = fr::ParseOffList("EDP_PlayerHitPointsMax, EDP_HearRangeGunShot", unknown);
    CHECK(off == std::set<int32_t> {131, 107});
    CHECK(unknown.empty());
}

TEST_CASE("off_list_unknown_names") {
    std::vector<std::string> unknown;
    const auto off = fr::ParseOffList("EDP_HearRangeGunshot,,EDP_PlayerHitPointsMax", unknown);
    CHECK(off == std::set<int32_t> {131});
    CHECK(unknown == std::vector<std::string> {"EDP_HearRangeGunshot"});
}

TEST_CASE("config_defaults") {
    const fr::Config cfg;
    CHECK(cfg.enabled);
    CHECK(cfg.dials);
    CHECK(cfg.guardWeapons);
    CHECK(cfg.targetPool);
    CHECK(cfg.enforcers);
    CHECK(cfg.doorLocks);
    CHECK(cfg.variants);
    CHECK(cfg.variantChances == fr::DefaultVariantChances());
    CHECK_FALSE(cfg.randomLoadoutHolsterableOnly);
    CHECK(cfg.randomLoadoutObjectiveItems);
    CHECK(cfg.variantForce.empty());
    CHECK(cfg.crateItems.on);
    CHECK(cfg.crateItems.toolsChance == 0.10);
    CHECK(cfg.crateItems.mercesChance == 0.05);
    CHECK(cfg.crateItems.unloaded);
    CHECK(cfg.crateItems.levelItems);
}

TEST_CASE("variants_and_guard_weapons_are_independent_of_mode") {
    fr::Config cfg;
    cfg.variants = false;
    cfg.variantChances.fill(100);
    cfg.guardWeapons = true;
    CHECK(fr::ChooseMode(cfg, true) == fr::Mode::Dials);
}

TEST_CASE("choose_mode") {
    using fr::Mode;

    fr::Config cfg;
    CHECK(fr::ChooseMode(cfg, false) == Mode::NotMission);
    CHECK(fr::ChooseMode(cfg, true) == Mode::Dials);

    cfg.dials = false;
    CHECK(fr::ChooseMode(cfg, true) == Mode::DialsOff);
    CHECK(fr::ChooseMode(cfg, false) == Mode::NotMission);
}

TEST_CASE("mode_names") {
    using fr::Mode;
    CHECK(std::string_view(fr::ModeName(Mode::Dials)) == "Random AI values");
    CHECK(std::string_view(fr::ModeName(Mode::DialsOff)) == "AI values off");
    CHECK(std::string_view(fr::ModeName(Mode::NotMission)) == "No changes (not a Freelancer mission)");
}

TEST_CASE("mode_factor") {
    using fr::Mode;
    constexpr int32_t attention = 1;   // EDP_AIAttentionGainMultiplier
    constexpr int32_t hitPoints = 131; // EDP_PlayerHitPointsMax
    const fr::Config cfg;
    const std::string_view scene = "assembly:/_pro/scenes/missions/elegant/scene_elegant_mild_sapo.entity";

    CHECK_FALSE(fr::ModeFactor(Mode::DialsOff, cfg, 0, scene, attention, 0.6).has_value());
    CHECK_FALSE(fr::ModeFactor(Mode::NotMission, cfg, 0, scene, attention, 0.6).has_value());

    // Dials: the same multiplier as DecideDial, whatever the (non-zero) base.
    const auto dial = fr::DecideDial(42, scene, attention, 0.6f, cfg.roll, cfg.off);
    REQUIRE(dial.apply);
    CHECK(fr::ModeFactor(Mode::Dials, cfg, 42, scene, attention, 0.6) == dial.multiplier);
    CHECK(fr::ModeFactor(Mode::Dials, cfg, 42, scene, attention, 0.4) == dial.multiplier);
    CHECK_FALSE(fr::ModeFactor(Mode::Dials, cfg, 42, scene, attention, 0.0).has_value());

    fr::Config off = cfg;
    off.off.insert(hitPoints);
    CHECK_FALSE(fr::ModeFactor(Mode::Dials, off, 42, scene, hitPoints, 100.0).has_value());

    // Footstep gaits roll with footsteps' own roll, not the general one.
    const int32_t run = 96; // EDP_HearRangeFootstepRun
    REQUIRE(fr::FootstepGaitIndex(run) >= 0);
    const auto step = fr::DecideDial(42, scene, run, 18.f, cfg.footsteps.roll, cfg.off);
    REQUIRE(step.apply);
    CHECK(fr::ModeFactor(Mode::Dials, cfg, 42, scene, run, 18.0) == step.multiplier);
}

TEST_CASE("mode_writes") {
    using fr::Mode;
    CHECK(fr::ModeWrites(Mode::Dials));
    CHECK_FALSE(fr::ModeWrites(Mode::DialsOff));
    CHECK_FALSE(fr::ModeWrites(Mode::NotMission));
}

TEST_CASE("checked_params") {
    using fr::Mode;
    using P = std::array<int32_t, 3>;
    // EDP_AIAttentionGainMultiplier, EDP_HearRangeFootstepRun, EDP_HearRangeGunShot.
    CHECK(fr::CheckedParams(Mode::Dials) == P {1, 96, 107});
    CHECK(fr::CheckedParams(Mode::DialsOff) == P {1, 96, 107});
    CHECK(fr::CheckedParams(Mode::NotMission) == P {1, 96, 107});
}

TEST_CASE("invalid ini values fall back to the settings' defaults") {
    const fr::Config def;
    const double nan = std::numeric_limits<double>::quiet_NaN();

    fr::PayoutScaleConfig payout = def.payoutScale;
    payout.sigma = -1.0;
    payout.lean = nan;
    const fr::PayoutScaleConfig safePayout = fr::SanitizePayoutScaleConfig(payout, def.payoutScale);
    CHECK(safePayout.sigma == 0.5);
    CHECK(safePayout.lean == -0.1);

    fr::RollConfig roll = def.roll;
    roll.sigma = std::numeric_limits<double>::infinity();
    roll.lean = nan;
    const fr::RollConfig safeRoll = fr::SanitizeRollConfig(roll, def.roll);
    CHECK(safeRoll.sigma == 0.7);
    CHECK(safeRoll.lean == def.roll.lean);
}
