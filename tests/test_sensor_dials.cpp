#include "doctest.h"

#include "DialInfo.h"
#include "Dials.h"
#include "ParamNames.h"
#include "SensorDials.h"

#include <cmath>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kScene = "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity";

std::string SceneName(int i) {
    return "assembly:/_pro/scenes/test/scene_" + std::to_string(i) + ".entity";
}

} // namespace

TEST_CASE("sensor_table_valid") {
    const auto table = fr::SensorDialTable();
    REQUIRE(table.size() == 16);

    std::set<std::string_view> keys;
    std::set<std::string_view> names;
    std::set<std::string> labels;

    for (size_t i = 0; i < table.size(); ++i) {
        const fr::SensorDial& dial = table[i];
        CAPTURE(dial.key);
        CHECK(keys.insert(dial.key).second);
        CHECK(names.insert(dial.info.name).second);
        CHECK(labels.insert(fr::SensorDialLabel(dial)).second);
        CHECK(fr::FindSensorDial(dial.key) == &dial);
        CHECK(dial.info.param == fr::kSensorDialParamBase + static_cast<int32_t>(i));
        CHECK(dial.vanilla > 0.0);
        CHECK(dial.dir >= -1);
        CHECK(dial.dir <= 1);
        CHECK_FALSE(dial.info.description.empty());
        CHECK(dial.info.basis != fr::Basis::Named);
        CHECK(dial.property.rfind("m_f", 0) == 0);

        for (const char c : dial.info.description)
            CHECK(static_cast<unsigned char>(c) < 0x80);

        // Attention thresholds stay below 1 (at 1 a guard would never react).
        if (dial.info.unit == fr::DialUnit::Attention) {
            CHECK(dial.maxValue == fr::kMaxAttentionThreshold);
            CHECK(dial.dir == -1);
        }

        // The focus cones are per perception state.
        CHECK(dial.sensitivity.empty() == (dial.property != "m_fFocusConeRange"));
    }

    CHECK(fr::FindSensorDial("nope") == nullptr);
}

TEST_CASE("sensor_vanilla_values") {
    // The professional sensor 6b6889480b488495, as measured in game.
    CHECK(fr::FindSensorDial("force_look_at")->vanilla == 0.25);
    CHECK(fr::FindSensorDial("force_look_at_rising")->vanilla == 0.25);
    CHECK(fr::FindSensorDial("sight_reaction")->vanilla == 0.35);
    CHECK(fr::FindSensorDial("sight_investigation")->vanilla == 0.5);
    CHECK(fr::FindSensorDial("attention_decrease_time")->vanilla == 0.5);
    CHECK(fr::FindSensorDial("trespassing_run_penalty")->vanilla == 1.5);
    CHECK(fr::FindSensorDial("hunted_run_penalty")->vanilla == 1.5);
    CHECK(fr::FindSensorDial("disguise_follow_time")->vanilla == 15.0);
    CHECK(fr::FindSensorDial("crowd_distance_factor")->vanilla == 1.0);
    CHECK(fr::FindSensorDial("crowd_time_factor")->vanilla == 1.0);
    // Death Awaits' RemoveItemByValue entries (the vanilla elements).
    CHECK(fr::FindSensorDial("cone_ambient")->vanilla == 25.0);
    CHECK(fr::FindSensorDial("cone_alert_low")->vanilla == 25.0);
    CHECK(fr::FindSensorDial("cone_alert")->vanilla == 35.0);
    CHECK(fr::FindSensorDial("cone_combat")->vanilla == 50.0);
    CHECK(fr::FindSensorDial("cone_spider_sense")->vanilla == 30.0);
    CHECK(fr::FindSensorDial("cone_preoccupied")->vanilla == 6.0);
    CHECK(fr::SensorDialLabel(*fr::FindSensorDial("cone_ambient")) ==
          "m_VisibilitySetting[APS_AMBIENT].m_fFocusConeRange");
    CHECK(fr::SensorDialLabel(*fr::FindSensorDial("sight_reaction")) == "m_fSightReactionMinAttention");
}

TEST_CASE("sensor_roll_config_caps_attention") {
    const fr::RollConfig cfg;
    const fr::SensorDial& sight = *fr::FindSensorDial("sight_investigation"); // 0.5, at most 0.9
    const fr::RollConfig capped = fr::SensorRollConfig(sight, cfg);
    CHECK(capped.clampMax == doctest::Approx(1.8));
    CHECK(capped.clampMin == cfg.clampMin);
    CHECK(capped.sigma == cfg.sigma);
    CHECK(capped.lean == cfg.lean);

    // No limit: the config as is.
    const fr::RollConfig plain = fr::SensorRollConfig(*fr::FindSensorDial("cone_combat"), cfg);
    CHECK(plain.clampMax == cfg.clampMax);
    CHECK(plain.clampMin == cfg.clampMin);

    // A clampMin above the cap follows it down.
    fr::RollConfig high = cfg;
    high.clampMin = 2.5;
    const fr::RollConfig lowered = fr::SensorRollConfig(sight, high);
    CHECK(lowered.clampMax == doctest::Approx(1.8));
    CHECK(lowered.clampMin == lowered.clampMax);
}

TEST_CASE("sensor_decisions") {
    const fr::RollConfig cfg;
    const std::set<std::string> none;

    for (const fr::SensorDial& dial : fr::SensorDialTable()) {
        CAPTURE(dial.key);
        const fr::RollConfig bounds = fr::SensorRollConfig(dial, cfg);

        for (int i = 0; i < 200; ++i) {
            const fr::SensorDecision d = fr::DecideSensorDial(1234 + i, SceneName(i), dial, cfg, none);
            CHECK(d.apply);
            CHECK(d.base == dial.vanilla);
            CHECK(d.multiplier >= bounds.clampMin);
            CHECK(d.multiplier <= bounds.clampMax);
            CHECK(d.value == static_cast<float>(dial.vanilla * d.multiplier));

            if (dial.maxValue > 0.0)
                CHECK(d.value <= dial.maxValue + 1e-6);
        }
    }

    const fr::SensorDial& cone = *fr::FindSensorDial("cone_ambient");
    const fr::SensorDecision a = fr::DecideSensorDial(1234, kScene, cone, cfg, none);
    // Deterministic, per mission, per dial.
    CHECK(a.multiplier == fr::DecideSensorDial(1234, kScene, cone, cfg, none).multiplier);
    CHECK(a.multiplier != fr::DecideSensorDial(1234, SceneName(1), cone, cfg, none).multiplier);
    CHECK(a.multiplier != fr::DecideSensorDial(77, kScene, cone, cfg, none).multiplier);
    CHECK(a.multiplier != fr::DecideSensorDial(1234, kScene, *fr::FindSensorDial("cone_alert"), cfg, none).multiplier);

    // Off: left alone.
    const fr::SensorDecision off = fr::DecideSensorDial(1234, kScene, cone, cfg, {"cone_ambient"});
    CHECK_FALSE(off.apply);
    CHECK(off.multiplier == 1.0);
    CHECK(off.value == 25.0f);
}

TEST_CASE("sensor_off_wins") {
    const fr::RollConfig cfg;
    const fr::SensorDial& cone = *fr::FindSensorDial("cone_combat");
    CHECK(fr::DecideSensorDial(1234, kScene, cone, cfg, {}).apply);
    CHECK_FALSE(fr::DecideSensorDial(1234, kScene, cone, cfg, {"cone_combat"}).apply);
}

TEST_CASE("sensor_lean_direction") {
    // Harder-when-smaller thresholds lean down, larger-is-harder cones lean up.
    const fr::RollConfig cfg;
    const std::set<std::string> none;
    double logThreshold = 0.0;
    double logCone = 0.0;
    constexpr int kRolls = 2000;

    for (int i = 0; i < kRolls; ++i) {
        logThreshold += std::log(
            fr::DecideSensorDial(i, SceneName(i), *fr::FindSensorDial("sight_reaction"), cfg, none).multiplier);
        logCone +=
            std::log(fr::DecideSensorDial(i, SceneName(i), *fr::FindSensorDial("cone_alert"), cfg, none).multiplier);
    }

    CHECK(logThreshold / kRolls < -0.05);
    CHECK(logCone / kRolls > 0.05);
}

TEST_CASE("sensor_off_list") {
    std::vector<std::string> unknown;
    const auto off = fr::ParseSensorOffList(" cone_combat, ,sight_reaction ,bogus", unknown);
    CHECK(off == std::set<std::string> {"cone_combat", "sight_reaction"});
    REQUIRE(unknown.size() == 1);
    CHECK(unknown[0] == "bogus");
    CHECK(fr::ParseSensorOffList("", unknown).empty());
}

TEST_CASE("sensor_own_bounds") {
    const fr::RollConfig cfg; // shared clamps [0.4, 3]
    const std::pair<std::string_view, std::pair<double, double>> kBounds[] = {
        {"cone_alert_low", {0.5, 1.8}},
        {"cone_alert", {0.7, 2.0}},
        {"attention_decrease_time", {0.5, 2.0}},
        {"crowd_distance_factor", {0.5, 1.5}},
    };

    for (const auto& [key, bounds] : kBounds) {
        const fr::SensorDial* dial = fr::FindSensorDial(key);
        REQUIRE(dial);
        const fr::RollConfig rolled = fr::SensorRollConfig(*dial, cfg);
        CHECK(rolled.clampMin == doctest::Approx(bounds.first));
        CHECK(rolled.clampMax == doctest::Approx(bounds.second));

        bool below = false;
        bool above = false;
        for (int32_t seed = 1; seed <= 400; ++seed) {
            const fr::SensorDecision d = fr::DecideSensorDial(seed, "scene", *dial, cfg, {});
            CHECK(d.multiplier >= bounds.first - 1e-9);
            CHECK(d.multiplier <= bounds.second + 1e-9);
            below = below || d.multiplier < 1.0;
            above = above || d.multiplier > 1.0;
        }
        CHECK(below); // slow guards happen too
        CHECK(above);
    }

    // A dial without its own bounds keeps the shared clamps.
    const fr::RollConfig ambient = fr::SensorRollConfig(*fr::FindSensorDial("cone_ambient"), cfg);
    CHECK(ambient.clampMin == doctest::Approx(cfg.clampMin));
    CHECK(ambient.clampMax == doctest::Approx(cfg.clampMax));
}

TEST_CASE("sensor_dial_param: the sight thresholds stand in for their AI dials") {
    const auto param = [](std::string_view key) { return fr::SensorDialParam(*fr::FindSensorDial(key)); };
    CHECK(param("force_look_at") == fr::ParamFromName("EDP_MinAttentionToForceLookAt"));
    CHECK(param("force_look_at_rising") == fr::ParamFromName("EDP_MinAttentionToForceLookAtIfIncreasing"));
    CHECK(param("sight_reaction") == fr::ParamFromName("EDP_SightReactionMinAttention"));
    CHECK(param("sight_investigation") == fr::ParamFromName("EDP_SightInvestigationMinAttention"));

    size_t covering = 0;

    for (const fr::SensorDial& dial : fr::SensorDialTable())
        covering += fr::SensorDialParam(dial) ? 1 : 0;

    CHECK(covering == 4);
}
