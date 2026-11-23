#include "doctest.h"
#include "support/DialTestUtil.h"

#include "Dials.h"
#include "ParamNames.h"
#include "Rng.h"

#include <algorithm>
#include <climits>
#include <limits>
#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int32_t kFootstepWalk = 95; // EDP_HearRangeFootstepWalk
constexpr const char* kScene = "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity";

std::string SceneName(int i) {
    return "assembly:/_pro/scenes/test/scene_" + std::to_string(i) + ".entity";
}

double Median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

bool EndsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

} // namespace

TEST_CASE("multiplier_deterministic") {
    const fr::RollConfig cfg;
    const double a = fr::RollMultiplier(1234, kScene, kFootstepWalk, 1, cfg).multiplier;
    const double b = fr::RollMultiplier(1234, kScene, kFootstepWalk, 1, cfg).multiplier;
    CHECK(a == b);

    // Re-rolls and fallbacks are deterministic too.
    fr::RollConfig narrow;
    narrow.clampMin = 0.99;
    narrow.clampMax = 1.01;
    for (int32_t s = 0; s < 500; ++s) {
        const fr::RollResult x = fr::RollMultiplier(s, kScene, kFootstepWalk, 0, narrow);
        const fr::RollResult y = fr::RollMultiplier(s, kScene, kFootstepWalk, 0, narrow);
        REQUIRE(x.multiplier == y.multiplier);
        REQUIRE(x.attempts == y.attempts);
        REQUIRE(x.fellBack == y.fellBack);
    }
}

TEST_CASE("seeds_differ") {
    const fr::RollConfig cfg;
    uint64_t state = 777;
    int differ = 0;
    for (int i = 0; i < 1000; ++i) {
        const auto s1 = static_cast<int32_t>(static_cast<uint32_t>(fr::SplitMix64(state)));
        auto s2 = static_cast<int32_t>(static_cast<uint32_t>(fr::SplitMix64(state)));
        if (s2 == s1)
            s2 ^= 1;
        if (fr::RollMultiplier(s1, kScene, kFootstepWalk, 0, cfg).multiplier !=
            fr::RollMultiplier(s2, kScene, kFootstepWalk, 0, cfg).multiplier)
            ++differ;
    }
    CHECK(differ >= 950);
}

TEST_CASE("lean_zero_mean_log") {
    fr::RollConfig cfg;
    cfg.lean = 0.0;
    cfg.clampMin = 1e-9;
    cfg.clampMax = 1e9;
    constexpr int n = 10000;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += std::log(fr::RollMultiplier(99, SceneName(i), kFootstepWalk, 1, cfg).multiplier);
    CHECK(std::abs(sum / n) < 0.02);
}

TEST_CASE("lean_direction") {
    const fr::RollConfig cfg;
    std::vector<double> up;
    std::vector<double> down;
    for (int i = 0; i < 2001; ++i) {
        up.push_back(fr::RollMultiplier(5, SceneName(i), kFootstepWalk, 1, cfg).multiplier);
        down.push_back(fr::RollMultiplier(5, SceneName(i), kFootstepWalk, -1, cfg).multiplier);
    }
    CHECK(Median(up) > 1.0);
    CHECK(Median(down) < 1.0);
}

TEST_CASE("within_clamps") {
    const fr::RollConfig cfg;
    for (int32_t i = 0; i < 2000; ++i) {
        for (const auto& dial : fr::test::AllDials()) {
            const fr::RollResult r = fr::RollMultiplier(i * 7919, kScene, dial.param, dial.dir, cfg);
            REQUIRE(r.attempts >= 1);
            REQUIRE(r.attempts <= fr::kMaxRollAttempts);
            if (r.fellBack) {
                REQUIRE(r.multiplier == 1.0);
                REQUIRE(r.attempts == fr::kMaxRollAttempts);
            }
            else {
                REQUIRE(r.multiplier >= 0.4);
                REQUIRE(r.multiplier <= 3.0);
            }
        }
    }
}

TEST_CASE("multiplier_golden") {
    // Pinned: first draws in range are unchanged.
    const fr::RollConfig cfg;
    const struct {
        int32_t seed;
        int8_t dir;
        double expected;
    } cases[] = {
        {1234, 1, 2.0457603261205506},
        {42, -1, 0.52847396804054636},
        {7, 0, 0.70610104750070457},
        {2024, 1, 1.2996969363749253},
    };
    for (const auto& c : cases) {
        INFO("seed ", c.seed, " dir ", int {c.dir});
        const fr::RollResult r = fr::RollMultiplier(c.seed, kScene, kFootstepWalk, c.dir, cfg);
        CHECK(std::abs(r.multiplier - c.expected) < 1e-12);
        CHECK(r.attempts == 1);
        CHECK_FALSE(r.fellBack);
    }
}

TEST_CASE("first_draw_in_range_is_kept") {
    // Attempt 0 is the first draw: whenever it is in range it is returned
    // as is; otherwise the roll is re-drawn (never clamped).
    fr::RollConfig cfg;
    cfg.sigma = 0.6; // wide enough that some first draws fall outside [0.4, 3.0]
    int rerolled = 0;
    for (int32_t s = 0; s < 5000; ++s) {
        const double z = fr::NormalFromHash(fr::DialHash(s, kScene, "ai", kFootstepWalk));
        // Tolerance: this file may be built with FP contraction, src/core is not.
        const double m0 = std::exp(cfg.sigma * z + cfg.lean * cfg.sigma);
        const fr::RollResult r = fr::RollMultiplier(s, kScene, kFootstepWalk, 1, cfg);
        if (m0 > cfg.clampMin * (1 + 1e-12) && m0 < cfg.clampMax * (1 - 1e-12)) {
            REQUIRE(r.attempts == 1);
            REQUIRE(std::abs(r.multiplier - m0) < 1e-12);
        }
        else if (m0 < cfg.clampMin * (1 - 1e-12) || m0 > cfg.clampMax * (1 + 1e-12)) {
            ++rerolled;
            REQUIRE(r.attempts > 1);
            REQUIRE(r.multiplier != cfg.clampMin);
            REQUIRE(r.multiplier != cfg.clampMax);
        }
    }
    CHECK(rerolled > 0);
}

TEST_CASE("narrow_range_rerolls_and_falls_back") {
    fr::RollConfig cfg;
    cfg.sigma = 0.35;
    cfg.clampMin = 0.99;
    cfg.clampMax = 1.01;
    int firstTry = 0;
    int rerolled = 0;
    int fellBack = 0;
    for (int32_t s = 0; s < 2000; ++s) {
        const fr::RollResult r = fr::RollMultiplier(s, kScene, kFootstepWalk, 0, cfg);
        if (r.fellBack) {
            ++fellBack;
            REQUIRE(r.multiplier == 1.0);
            REQUIRE(r.attempts == fr::kMaxRollAttempts);
            continue;
        }
        REQUIRE(r.multiplier >= 0.99);
        REQUIRE(r.multiplier <= 1.01);
        if (r.attempts == 1)
            ++firstTry;
        else
            ++rerolled;
    }
    CHECK(firstTry > 0);
    CHECK(rerolled > 0);
    CHECK(fellBack > 0);
}

TEST_CASE("roll_note") {
    CHECK(fr::RollNote({1.5, 1, false}).empty());
    CHECK(fr::RollNote({1.5, 3, false}) == "re-rolled 2 times");
    CHECK(fr::RollNote({1.5, 2, false}) == "re-rolled 1 time");
    CHECK(fr::RollNote({1.0, fr::kMaxRollAttempts, true}) == "no valid roll, kept default");
}

TEST_CASE("table_valid") {
    std::set<int32_t> seen;
    for (const auto& dial : fr::test::AllDials()) {
        const std::string name(fr::ParamName(dial.param));
        INFO("param ", dial.param, " name ", name);
        REQUIRE(!name.empty());
        CHECK(seen.insert(dial.param).second);
        CHECK((dial.dir >= -1 && dial.dir <= 1));
        for (const char* suffix : {"Number", "Amount", "Count", "PressCount", "Enabled"})
            CHECK_FALSE(EndsWith(name, suffix));
        for (const char* prefix :
             {"EDP_CloseCombat", "EDP_ShootSpread", "EDP_PlayerDamagePerSecond", "EDP_ArrestEscalate"})
            CHECK_FALSE(name.starts_with(prefix));
        // Player-weapon damage multipliers (no _NPC suffix) are excluded.
        if (name.starts_with("EDP_DamageMultiplier"))
            CHECK(EndsWith(name, "_NPC"));
    }

    int hear = 0;
    int social = 0;
    for (const auto& [name, value] : fr::test::AllParams()) {
        INFO(std::string(name));
        if (name.starts_with("EDP_HearRange")) {
            ++hear;
            CHECK(fr::FindDial(value) != nullptr);
        }
        if (name.starts_with("EDP_Social")) {
            ++social;
            CHECK(fr::FindDial(value) != nullptr);
        }
    }
    CHECK(hear == 37);
    CHECK(social == 16);
    CHECK(fr::test::AllDials().size() == 106);

    // Toggles and other parameters that must never be rolled.
    for (const char* name :
         {"EDP_SecurityCameras", "EDP_StrictItemRules", "EDP_AllowInstinctGlow", "EDP_AILongTermMemory_DisableArrest",
          "EDP_DisguiseRuiningEnabled", "EDP_GameOpportunitiesEnabled", "EDP_GameMissionSaveLimit",
          "EDP_ArrestEscalateIllegalWeapon", "EDP_ArrestEscalateMaximum", "EDP_ArrestEscalateMoving",
          "EDP_ArrestEscalateRunning", "EDP_AIAudioPerceptionAlwaysEmitFootstepSoundEvents",
          "EDP_AIInvestigateCautiousAttentionToPersonEnabled", "EDP_AISuspiciousMovementSensorEnabled"}) {
        INFO(name);
        const auto value = fr::ParamFromName(name);
        REQUIRE(value.has_value());
        CHECK(fr::FindDial(*value) == nullptr);
    }
}

TEST_CASE("table_directions") {
    // One representative per group of the dial table.
    const std::pair<const char*, int8_t> expected[] = {
        {"EDP_HearRangeGunShot", +1},
        {"EDP_AIAttentionGainMultiplier", +1},
        {"EDP_SightReactionMinAttention", -1},
        {"EDP_ManhuntGroupSearchAreaRadius", +1},
        {"EDP_AISentry_WarningDelay", -1},
        {"EDP_AICombat_FlashGrenadeThrow_GlobalCooldown", -1},
        {"EDP_AICombat_ShootingAccuracy_MinimumDistance", 0},
        {"EDP_DamageMultiplierHandgun_NPC", +1},
        {"EDP_NPCDamageReceivedMultiplier", -1},
        {"EDP_PlayerHitPointsMax", -1},
        {"EDP_PlayerHitPointsRegenerationDelay", +1},
        {"EDP_InstinctModeBurnRate", +1},
        {"EDP_InstinctRegenRate", -1},
        {"EDP_SocialGreetingDistance", 0},
    };
    for (const auto& [name, dir] : expected) {
        INFO(name);
        const auto value = fr::ParamFromName(name);
        REQUIRE(value.has_value());
        const fr::DialSpec* dial = fr::FindDial(*value);
        REQUIRE(dial != nullptr);
        CHECK(dial->dir == dir);
    }
}

TEST_CASE("sanitize_defaults_pass_through") {
    const fr::RollConfig def;
    fr::RollConfig in;
    in.sigma = 0.2;
    in.lean = -1.0;
    in.clampMin = 0.5;
    in.clampMax = 2.0;
    auto out = fr::SanitizeRollConfig(in);
    CHECK(out.sigma == 0.2);
    CHECK(out.lean == -1.0);
    CHECK(out.clampMin == 0.5);
    CHECK(out.clampMax == 2.0);

    in.sigma = 0.0; // zero spread is allowed
    in.clampMin = in.clampMax = 1.0;
    out = fr::SanitizeRollConfig(in);
    CHECK(out.sigma == 0.0);
    CHECK(out.clampMin == 1.0);
    CHECK(out.clampMax == 1.0);

    out = fr::SanitizeRollConfig(def);
    CHECK(out.sigma == def.sigma);
    CHECK(out.lean == def.lean);
    CHECK(out.clampMin == def.clampMin);
    CHECK(out.clampMax == def.clampMax);
}

TEST_CASE("sanitize_sigma_lean_fieldwise") {
    const fr::RollConfig def;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (double bad : {nan, inf, -inf, -0.1}) {
        fr::RollConfig in;
        in.sigma = bad;
        in.lean = 1.5;
        const auto out = fr::SanitizeRollConfig(in);
        CHECK(out.sigma == def.sigma);
        CHECK(out.lean == 1.5);
    }
    for (double bad : {nan, inf, -inf}) {
        fr::RollConfig in;
        in.sigma = 0.1;
        in.lean = bad;
        const auto out = fr::SanitizeRollConfig(in);
        CHECK(out.sigma == 0.1);
        CHECK(out.lean == def.lean);
    }
}

TEST_CASE("sanitize_clamps_as_pair") {
    const fr::RollConfig def;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const std::pair<double, double> bad[] = {
        {nan, 2.0}, {0.5, nan},  {inf, 2.0}, {0.5, inf},  {-inf, 2.0}, {0.5, -inf},
        {0.0, 2.0}, {-0.5, 2.0}, {0.5, 0.0}, {0.5, -1.0}, {2.0, 0.5},
    };
    for (const auto& [lo, hi] : bad) {
        INFO("clampMin ", lo, " clampMax ", hi);
        fr::RollConfig in;
        in.sigma = 0.1;
        in.lean = 1.5;
        in.clampMin = lo;
        in.clampMax = hi;
        const auto out = fr::SanitizeRollConfig(in);
        CHECK(out.clampMin == def.clampMin);
        CHECK(out.clampMax == def.clampMax);
        CHECK(out.sigma == 0.1);
        CHECK(out.lean == 1.5);
    }
}

TEST_CASE("skips") {
    const fr::RollConfig cfg;
    const std::set<int32_t> none;

    // EDP_CloseCombatChokePressCount_Guard (52) is not in the table.
    auto d = fr::DecideDial(1, kScene, 52, 3.f, cfg, none);
    CHECK_FALSE(d.apply);
    CHECK(d.reason == fr::SkipReason::NotInTable);

    d = fr::DecideDial(1, kScene, kFootstepWalk, 3.f, cfg, {kFootstepWalk});
    CHECK_FALSE(d.apply);
    CHECK(d.reason == fr::SkipReason::Off);

    d = fr::DecideDial(1, kScene, kFootstepWalk, 0.f, cfg, none);
    CHECK_FALSE(d.apply);
    CHECK(d.reason == fr::SkipReason::ZeroBase);

    d = fr::DecideDial(1, kScene, kFootstepWalk, 3.f, cfg, none);
    CHECK(d.apply);
    CHECK(d.reason == fr::SkipReason::None);
    CHECK(d.value == static_cast<float>(3.f * d.multiplier));
    const fr::RollResult r = fr::RollMultiplier(1, kScene, kFootstepWalk, fr::FindDial(kFootstepWalk)->dir, cfg);
    CHECK(d.multiplier == r.multiplier);
    CHECK(d.attempts == r.attempts);
    CHECK(d.fellBack == r.fellBack);

    // A fallback is applied as the default (x1), and reported.
    fr::RollConfig narrow;
    narrow.clampMin = 0.99;
    narrow.clampMax = 1.01;
    bool sawFallback = false;
    for (int32_t s = 0; s < 2000 && !sawFallback; ++s) {
        d = fr::DecideDial(s, kScene, kFootstepWalk, 3.f, narrow, none);
        if (d.fellBack) {
            sawFallback = true;
            CHECK(d.apply);
            CHECK(d.multiplier == 1.0);
            CHECK(d.value == 3.f);
            CHECK(d.attempts == fr::kMaxRollAttempts);
        }
    }
    CHECK(sawFallback);
}

TEST_CASE("clamp_handles_negative_base") {
    const fr::RollConfig cfg;
    const std::set<int32_t> none;
    for (int32_t seed = -500; seed < 500; ++seed) {
        const auto d = fr::DecideDial(seed, kScene, kFootstepWalk, -10.f, cfg, none);
        REQUIRE(d.apply);
        REQUIRE(d.value >= -30.f);
        REQUIRE(d.value <= -4.f);
    }
}

TEST_CASE("scene_case_insensitive_rolls") {
    const fr::RollConfig cfg;
    for (int32_t s : {0, 1, 42, -7}) {
        CHECK(fr::RollMultiplier(s, "A/_PRO/X", kFootstepWalk, 1, cfg).multiplier ==
              fr::RollMultiplier(s, "a/_pro/x", kFootstepWalk, 1, cfg).multiplier);
    }
}

TEST_CASE("edge_seeds_finite") {
    const fr::RollConfig cfg;
    for (int32_t s : {0, INT32_MIN, INT32_MAX}) {
        for (const auto& dial : fr::test::AllDials()) {
            const fr::RollResult r = fr::RollMultiplier(s, kScene, dial.param, dial.dir, cfg);
            REQUIRE(std::isfinite(r.multiplier));
            REQUIRE(((r.multiplier >= 0.4 && r.multiplier <= 3.0) || (r.fellBack && r.multiplier == 1.0)));
        }
    }
}

TEST_CASE("dial_bands_from_clamps") {
    const fr::RollConfig cfg; // clamps 0.4..3.0
    const fr::DialBands b = fr::BandsFor(cfg);
    CHECK(b.notableHigh == doctest::Approx(1.3161).epsilon(1e-4));
    CHECK(b.notableLow == doctest::Approx(0.7953).epsilon(1e-4));
    CHECK(b.extremeHigh == doctest::Approx(2.2795).epsilon(1e-4));
    CHECK(b.extremeLow == doctest::Approx(0.5030).epsilon(1e-4));

    // The bands follow the live clamps.
    fr::RollConfig wide;
    wide.clampMin = 0.25;
    wide.clampMax = 4.0;
    const fr::DialBands w = fr::BandsFor(wide);
    CHECK(w.extremeHigh == doctest::Approx(std::pow(4.0, 0.75)));
    CHECK(w.extremeLow == doctest::Approx(std::pow(0.25, 0.75)));
    CHECK(w.notableHigh == doctest::Approx(std::sqrt(std::sqrt(4.0))));

    // Invalid clamps fall back to the defaults, as for rolls.
    fr::RollConfig bad;
    bad.clampMin = 2.0;
    bad.clampMax = 0.5;
    CHECK(fr::BandsFor(bad).extremeHigh == b.extremeHigh);
}

TEST_CASE("rate_levels") {
    using fr::DialLevel;
    const fr::RollConfig cfg;
    const fr::DialBands b = fr::BandsFor(cfg);
    const std::pair<double, DialLevel> cases[] = {
        {1.0, DialLevel::Normal},
        {std::nextafter(b.notableHigh, 0.0), DialLevel::Normal},
        {std::nextafter(b.notableLow, 2.0), DialLevel::Normal},
        {b.notableHigh, DialLevel::Notable},
        {b.notableLow, DialLevel::Notable},
        {2.0, DialLevel::Notable}, // under x2.28
        {std::nextafter(b.extremeHigh, 0.0), DialLevel::Notable},
        {std::nextafter(b.extremeLow, 2.0), DialLevel::Notable},
        {b.extremeHigh, DialLevel::Extreme},
        {b.extremeLow, DialLevel::Extreme},
        {0.5, DialLevel::Extreme},
        {3.0, DialLevel::Extreme},  // clampMax
        {0.4, DialLevel::Extreme},  // clampMin
        {50.0, DialLevel::Extreme}, // far past extremeHigh
        {0.05, DialLevel::Extreme}, // far under extremeLow
    };
    for (const auto& [m, level] : cases) {
        INFO("multiplier ", m);
        for (int8_t dir : {int8_t {-1}, int8_t {0}, int8_t {1}})
            CHECK(fr::RateMultiplier(m, dir, cfg).level == level);
    }
}

TEST_CASE("rate_levels_follow_clamps") {
    fr::RollConfig wide;
    wide.clampMin = 0.25;
    wide.clampMax = 4.0;
    CHECK(fr::RateMultiplier(2.5, 1, fr::RollConfig {}).level == fr::DialLevel::Extreme);
    CHECK(fr::RateMultiplier(2.5, 1, wide).level == fr::DialLevel::Notable);

    // No room above x1: any increase is beyond what a roll can do.
    fr::RollConfig noUp;
    noUp.clampMin = 0.5;
    noUp.clampMax = 1.0;
    const fr::DialRating r = fr::RateMultiplier(1.01, 1, noUp);
    CHECK(r.level == fr::DialLevel::Extreme);
    CHECK(r.reach == std::numeric_limits<double>::infinity());
    CHECK(fr::RateMultiplier(1.0, 1, noUp).level == fr::DialLevel::Normal);
    CHECK(fr::RateMultiplier(0.9, 1, noUp).level == fr::DialLevel::Normal);
}

TEST_CASE("rate_sides") {
    using fr::DialSide;
    const fr::RollConfig cfg;
    CHECK(fr::RateMultiplier(2.5, +1, cfg).side == DialSide::Harder);
    CHECK(fr::RateMultiplier(0.45, +1, cfg).side == DialSide::Easier);
    CHECK(fr::RateMultiplier(2.5, -1, cfg).side == DialSide::Easier);
    CHECK(fr::RateMultiplier(0.45, -1, cfg).side == DialSide::Harder);
    CHECK(fr::RateMultiplier(2.5, 0, cfg).side == DialSide::Neutral);
    CHECK(fr::RateMultiplier(0.45, 0, cfg).side == DialSide::Neutral);
    // Small changes still have a side; exactly x1 has none.
    CHECK(fr::RateMultiplier(1.01, +1, cfg).side == DialSide::Harder);
    CHECK(fr::RateMultiplier(1.0, +1, cfg).side == DialSide::Neutral);
    CHECK(fr::RateMultiplier(1.0, -1, cfg).side == DialSide::Neutral);

    // Reach is the fraction of the way to the clamp on that side.
    CHECK(fr::RateMultiplier(3.0, 1, cfg).reach == doctest::Approx(1.0));
    CHECK(fr::RateMultiplier(0.4, 1, cfg).reach == doctest::Approx(1.0));
    CHECK(fr::RateMultiplier(std::sqrt(3.0), 1, cfg).reach == doctest::Approx(0.5));
    CHECK(fr::RateMultiplier(1.0, 1, cfg).reach == 0.0);
}

TEST_CASE("rate_dial_uses_table") {
    const fr::RollConfig cfg;
    const auto hp = fr::ParamFromName("EDP_PlayerHitPointsMax");         // smaller is harder
    const auto social = fr::ParamFromName("EDP_SocialGreetingDistance"); // symmetric
    REQUIRE(hp.has_value());
    REQUIRE(social.has_value());

    CHECK(fr::RateDial(kFootstepWalk, 2.4, cfg).side == fr::DialSide::Harder);
    CHECK(fr::RateDial(*hp, 0.45, cfg).side == fr::DialSide::Harder);
    CHECK(fr::RateDial(*hp, 2.4, cfg).side == fr::DialSide::Easier);
    CHECK(fr::RateDial(*social, 2.4, cfg).side == fr::DialSide::Neutral);
    // EDP_CloseCombatChokePressCount_Guard (52) is not in the table: magnitude only.
    const fr::DialRating r = fr::RateDial(52, 2.4, cfg);
    CHECK(r.level == fr::DialLevel::Extreme);
    CHECK(r.side == fr::DialSide::Neutral);
}

TEST_CASE("rate_invalid_multiplier") {
    const fr::RollConfig cfg;
    const double inf = std::numeric_limits<double>::infinity();
    for (double bad : {std::numeric_limits<double>::quiet_NaN(), inf, -inf, 0.0, -1.0}) {
        INFO("multiplier ", bad);
        const fr::DialRating r = fr::RateMultiplier(bad, +1, cfg);
        CHECK(r.level == fr::DialLevel::Extreme);
        CHECK(r.side == fr::DialSide::Neutral);
        CHECK(r.reach == inf);
    }
}

TEST_CASE("dial_marker") {
    const fr::RollConfig cfg;
    CHECK(fr::DialMarker(fr::RateMultiplier(1.1, +1, cfg)) == "");
    CHECK(fr::DialMarker(fr::RateMultiplier(1.5, +1, cfg)) == "! harder");
    CHECK(fr::DialMarker(fr::RateMultiplier(1.5, -1, cfg)) == "! easier");
    CHECK(fr::DialMarker(fr::RateMultiplier(1.5, 0, cfg)) == "!");
    CHECK(fr::DialMarker(fr::RateMultiplier(0.45, +1, cfg)) == "!! easier");
    CHECK(fr::DialMarker(fr::RateMultiplier(0.45, -1, cfg)) == "!! harder");
    CHECK(fr::DialMarker(fr::RateMultiplier(2.5, 0, cfg)) == "!!");
}

TEST_CASE("wilder_first") {
    const fr::RollConfig cfg;
    const fr::DialRating normal = fr::RateMultiplier(1.1, 1, cfg);
    const fr::DialRating notableBig = fr::RateMultiplier(0.55, 1, cfg);  // easier
    const fr::DialRating notableSmall = fr::RateMultiplier(1.4, 1, cfg); // harder
    const fr::DialRating extremeHarder = fr::RateMultiplier(2.3, 1, cfg);
    const fr::DialRating extremeHarderBig = fr::RateMultiplier(2.9, 1, cfg);
    const fr::DialRating extremeNeutral = fr::RateMultiplier(3.0, 0, cfg);
    const fr::DialRating extremeEasier = fr::RateMultiplier(0.4, 1, cfg);

    // Level first, then harder / neutral / easier, then reach.
    CHECK(fr::WilderFirst(extremeEasier, notableBig));
    CHECK(fr::WilderFirst(extremeHarder, extremeNeutral)); // smaller reach, but harder
    CHECK(fr::WilderFirst(extremeNeutral, extremeEasier));
    CHECK(fr::WilderFirst(extremeHarderBig, extremeHarder));
    CHECK(fr::WilderFirst(notableSmall, notableBig)); // harder before easier
    CHECK(fr::WilderFirst(notableBig, normal));
    CHECK_FALSE(fr::WilderFirst(normal, notableSmall));
    CHECK_FALSE(fr::WilderFirst(extremeHarder, extremeHarder)); // strict

    std::vector<fr::DialRating> v = {normal,       extremeEasier,  notableBig,      extremeHarder,
                                     notableSmall, extremeNeutral, extremeHarderBig};
    std::stable_sort(v.begin(), v.end(), fr::WilderFirst);
    const fr::DialRating want[] = {extremeHarderBig, extremeHarder, extremeNeutral, extremeEasier,
                                   notableSmall,     notableBig,    normal};
    for (size_t i = 0; i < v.size(); ++i) {
        INFO("position ", i);
        CHECK(v[i].level == want[i].level);
        CHECK(v[i].side == want[i].side);
        CHECK(v[i].reach == want[i].reach);
    }
}
