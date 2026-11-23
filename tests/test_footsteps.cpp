#include "doctest.h"

#include "Dials.h"
#include "Footsteps.h"
#include "ParamNames.h"

#include <cmath>
#include <limits>
#include <set>
#include <string_view>

namespace {

constexpr const char* kScene = "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity";

int32_t P(std::string_view name) {
    return *fr::ParamFromName(name);
}

} // namespace

TEST_CASE("footstep_gaits_defaults") {
    const auto gaits = fr::FootstepGaits();
    REQUIRE(gaits.size() == fr::kFootstepGaits);

    // Death Awaits' ranges; standing gaits on, crouched gaits off.
    CHECK(gaits[0].param == P("EDP_HearRangeFootstepWalk"));
    CHECK(gaits[0].key == "walk");
    CHECK(gaits[0].defaultOn);
    CHECK(gaits[0].defaultRange == 10.0);
    CHECK(gaits[1].param == P("EDP_HearRangeFootstepWalkSlow"));
    CHECK(gaits[1].defaultOn);
    CHECK(gaits[1].defaultRange == 6.0);
    CHECK(gaits[2].param == P("EDP_HearRangeFootstepRun"));
    CHECK(gaits[2].defaultOn);
    CHECK(gaits[2].defaultRange == 18.0);
    CHECK(gaits[3].param == P("EDP_HearRangeFootstepCrouchWalk"));
    CHECK_FALSE(gaits[3].defaultOn);
    CHECK(gaits[3].defaultRange == 4.0);
    CHECK(gaits[4].param == P("EDP_HearRangeFootstepCrouchWalkSlow"));
    CHECK_FALSE(gaits[4].defaultOn);
    CHECK(gaits[4].defaultRange == 2.0);
    CHECK(gaits[5].param == P("EDP_HearRangeFootstepCrouchRun"));
    CHECK_FALSE(gaits[5].defaultOn);
    CHECK(gaits[5].defaultRange == 8.0);

    const fr::FootstepConfig cfg = fr::DefaultFootstepConfig();
    CHECK(cfg.on);

    for (size_t i = 0; i < gaits.size(); ++i) {
        CHECK(fr::FootstepGaitIndex(gaits[i].param) == static_cast<int>(i));
        CHECK(cfg.enabled[i] == gaits[i].defaultOn);
        CHECK(cfg.range[i] == gaits[i].defaultRange);
        // Every gait is an AI dial too (the roll is the dial's).
        CHECK(fr::FindDial(gaits[i].param) != nullptr);
    }

    // Vegetation variants and other parameters are not gaits.
    CHECK(fr::FootstepGaitIndex(P("EDP_HearRangeFootstepVegetationLVARun")) == -1);
    CHECK(fr::FootstepGaitIndex(P("EDP_HearRangeGunShot")) == -1);
}

TEST_CASE("footstep_sanitize_ranges") {
    fr::FootstepConfig cfg = fr::DefaultFootstepConfig();
    cfg.range[0] = std::numeric_limits<double>::quiet_NaN();
    cfg.range[1] = 0.0;
    cfg.range[2] = -3.0;
    cfg.range[3] = std::numeric_limits<double>::infinity();
    cfg.range[4] = 7.5;
    const fr::FootstepConfig out = fr::SanitizeFootstepConfig(cfg);
    CHECK(out.range[0] == 10.0);
    CHECK(out.range[1] == 6.0);
    CHECK(out.range[2] == 18.0);
    CHECK(out.range[3] == 4.0);
    CHECK(out.range[4] == 7.5);
    CHECK(out.range[5] == 8.0);
}

TEST_CASE("footstep_decisions") {
    const fr::RollConfig roll = fr::kDefaultFootstepRoll;
    const std::set<int32_t> none;
    const int32_t run = P("EDP_HearRangeFootstepRun");
    const int32_t crouch = P("EDP_HearRangeFootstepCrouchWalk");
    fr::FootstepConfig cfg = fr::DefaultFootstepConfig();

    SUBCASE("enabled gait rolls the dial on the configured base") {
        const fr::FootstepDecision d = fr::DecideFootstep(1234, kScene, run, cfg, none);
        CHECK(d.skip == fr::FootstepSkip::None);
        CHECK(d.base == 18.0);
        CHECK(d.rolled);
        // The same multiplier as the AI dial roll for this parameter.
        CHECK(d.multiplier == fr::RollMultiplier(1234, kScene, run, +1, roll).multiplier);
        CHECK(d.value == doctest::Approx(18.0 * d.multiplier));
        CHECK(d.multiplier >= roll.clampMin);
        CHECK(d.multiplier <= roll.clampMax);
    }

    SUBCASE("configured base") {
        cfg.range[2] = 30.0;
        const fr::FootstepDecision d = fr::DecideFootstep(1234, kScene, run, cfg, none);
        CHECK(d.base == 30.0);
        CHECK(d.value == doctest::Approx(30.0 * d.multiplier));
    }

    SUBCASE("crouched gaits are off by default") {
        CHECK(fr::DecideFootstep(1234, kScene, crouch, cfg, none).skip == fr::FootstepSkip::GaitOff);
        cfg.enabled[3] = true;
        const fr::FootstepDecision d = fr::DecideFootstep(1234, kScene, crouch, cfg, none);
        CHECK(d.skip == fr::FootstepSkip::None);
        CHECK(d.base == 4.0);
    }

    SUBCASE("master switch") {
        cfg.on = false;
        CHECK(fr::DecideFootstep(1234, kScene, run, cfg, none).skip == fr::FootstepSkip::SwitchOff);
    }

    SUBCASE("off list writes the base unrolled") {
        const fr::FootstepDecision d = fr::DecideFootstep(1234, kScene, run, cfg, {run});
        CHECK(d.skip == fr::FootstepSkip::None);
        CHECK_FALSE(d.rolled);
        CHECK(d.multiplier == 1.0);
        CHECK(d.value == 18.0);
    }

    SUBCASE("not a gait") {
        CHECK(fr::DecideFootstep(1234, kScene, P("EDP_HearRangeFootstepVegetationLVAWalk"), cfg, none).skip ==
              fr::FootstepSkip::NotFootstep);
        CHECK(fr::DecideFootstep(1234, kScene, P("EDP_HearRangeGunShot"), cfg, none).skip ==
              fr::FootstepSkip::NotFootstep);
    }

    SUBCASE("rerolls per mission") {
        const double a = fr::DecideFootstep(1234, kScene, run, cfg, none).multiplier;
        const double b = fr::DecideFootstep(1234, "assembly:/_pro/scenes/other.entity", run, cfg, none).multiplier;
        const double c = fr::DecideFootstep(99, kScene, run, cfg, none).multiplier;
        CHECK(a != b);
        CHECK(a != c);
    }
}

TEST_CASE("footstep_skip_names") {
    CHECK(std::string_view(fr::FootstepSkipName(fr::FootstepSkip::None)) == "written");
    CHECK(std::string_view(fr::FootstepSkipName(fr::FootstepSkip::GaitOff)) == "gait off");
    CHECK(std::string_view(fr::FootstepSkipName(fr::FootstepSkip::SwitchOff)) == "footsteps = false");
    CHECK(std::string_view(fr::FootstepSkipName(fr::FootstepSkip::NotFootstep)) == "not a footstep gait");
}

TEST_CASE("footstep_own_roll") {
    const fr::FootstepConfig def = fr::DefaultFootstepConfig();
    CHECK(def.roll.sigma == 0.15);
    CHECK(def.roll.lean == 0.0);
    CHECK(def.roll.clampMin == 0.7);
    CHECK(def.roll.clampMax == 1.4);

    const std::set<int32_t> none;
    const int32_t run = P("EDP_HearRangeFootstepRun");

    // Every mission's run range stays within 0.7 to 1.4 x 18 m, never the general dial's x0.4 to x3.
    double lo = 1e9, hi = 0.0;
    for (int32_t seed = 0; seed < 2000; ++seed) {
        const fr::FootstepDecision d = fr::DecideFootstep(seed, kScene, run, def, none);
        REQUIRE(d.skip == fr::FootstepSkip::None);
        CHECK(d.multiplier >= 0.7);
        CHECK(d.multiplier <= 1.4);
        lo = std::min(lo, d.value);
        hi = std::max(hi, d.value);
    }
    CHECK(lo >= 18.0 * 0.7 - 1e-9);
    CHECK(hi <= 18.0 * 1.4 + 1e-9);
    CHECK(hi - lo > 2.0); // it still varies

    // The general roll does not reach footsteps.
    fr::FootstepConfig custom = def;
    custom.roll = {0.0, 0.0, 0.7, 1.4}; // sigma 0: always x1
    CHECK(fr::DecideFootstep(7, kScene, run, custom, none).multiplier == 1.0);
}

TEST_CASE("footstep_roll_sanitize") {
    fr::FootstepConfig cfg = fr::DefaultFootstepConfig();
    cfg.roll = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 2.0, 1.0};
    const fr::RollConfig out = fr::SanitizeFootstepConfig(cfg).roll;
    CHECK(out.sigma == 0.15);
    CHECK(out.lean == 0.0);
    CHECK(out.clampMin == 0.7);
    CHECK(out.clampMax == 1.4);

    cfg.roll = {0.3, 0.5, 0.5, 2.0};
    const fr::RollConfig kept = fr::SanitizeFootstepConfig(cfg).roll;
    CHECK(kept.sigma == 0.3);
    CHECK(kept.lean == 0.5);
    CHECK(kept.clampMin == 0.5);
    CHECK(kept.clampMax == 2.0);

    cfg.roll = {-1.0, 0.0, 0.0, 1.4};
    const fr::RollConfig reset = fr::SanitizeFootstepConfig(cfg).roll;
    CHECK(reset.sigma == 0.15);
    CHECK(reset.clampMin == 0.7);
    CHECK(reset.clampMax == 1.4);
}

TEST_CASE("footstep_always_emit") {
    fr::FootstepConfig cfg = fr::DefaultFootstepConfig();
    CHECK(fr::DecideAlwaysEmit(cfg).write);

    cfg.on = false;
    CHECK_FALSE(fr::DecideAlwaysEmit(cfg).write);
    CHECK(std::string_view(fr::DecideAlwaysEmit(cfg).why) == "footsteps = false");

    cfg = fr::DefaultFootstepConfig();
    cfg.enabled.fill(false);
    CHECK_FALSE(fr::DecideAlwaysEmit(cfg).write);
    CHECK(std::string_view(fr::DecideAlwaysEmit(cfg).why) == "every gait off");
    cfg.enabled[4] = true; // a crouched gait alone counts
    CHECK(fr::DecideAlwaysEmit(cfg).write);
}
