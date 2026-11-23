#include "doctest.h"

#include "Merces.h"
#include "PayoutScale.h"
#include "Rng.h"
#include "support/PayoutTestUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <vector>

namespace {

constexpr int32_t kBonus = 2000;

// The payout table's ids look like these.
const std::vector<uint64_t> kIds {0x1c8a3cd106f999feull, 0xd9788bdba0da37ccull, 0x0000000000000001ull,
                                  0x44c9861c1c99341eull, 0xffffffffffffffffull, 0x86d859b13def2673ull};

using fr::test::PinnedPayout;

} // namespace

TEST_CASE("payout_scale_round_to_50") {
    CHECK(fr::RoundPayout(486.0) == 500);
    CHECK(fr::RoundPayout(474.9) == 450);
    CHECK(fr::RoundPayout(475.0) == 500); // halves up
    CHECK(fr::RoundPayout(1000.0) == 1000);
    CHECK(fr::RoundPayout(1024.0) == 1000);
    CHECK(fr::RoundPayout(1025.0) == 1050);
    // Never below 50.
    CHECK(fr::RoundPayout(24.0) == 50);
    CHECK(fr::RoundPayout(0.0) == 50);
    CHECK(fr::RoundPayout(-300.0) == 50);
    CHECK_FALSE(fr::RoundPayout(std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(fr::RoundPayout(std::numeric_limits<double>::infinity()));
}

TEST_CASE("payout_scale_normal_amounts") {
    using fr::PayoutKind;
    CHECK(PinnedPayout(1000, 1.0, PayoutKind::Normal, kBonus) == 1000);
    CHECK(PinnedPayout(1000, 1.37, PayoutKind::Normal, kBonus) == 1350);
    CHECK(PinnedPayout(1000, 1.38, PayoutKind::Normal, kBonus) == 1400);
    CHECK(PinnedPayout(500, 0.972, PayoutKind::Normal, kBonus) == 950); // scaled from 1000: 972 -> 950
    CHECK(PinnedPayout(200, 0.1, PayoutKind::Normal, kBonus) == 500);   // 100 -> at least 500
    CHECK(PinnedPayout(1500, 2.5, PayoutKind::Normal, kBonus) == 3750);

    // The bonus is ignored for normal entries.
    CHECK(PinnedPayout(1000, 1.37, PayoutKind::Normal, 0) == 1350);

    // Invalid inputs.
    CHECK_FALSE(PinnedPayout(1000, 1.2, PayoutKind::Unknown, kBonus));
    CHECK_FALSE(PinnedPayout(0, 1.2, PayoutKind::Normal, kBonus));
    CHECK_FALSE(PinnedPayout(-500, 1.2, PayoutKind::Normal, kBonus));
    CHECK_FALSE(PinnedPayout(1000, 0.0, PayoutKind::Normal, kBonus));
    CHECK_FALSE(PinnedPayout(1000, -1.0, PayoutKind::Normal, kBonus));
    CHECK_FALSE(PinnedPayout(1000, std::numeric_limits<double>::quiet_NaN(), PayoutKind::Normal, kBonus));
    CHECK_FALSE(PinnedPayout(1000, 1.2, PayoutKind::Normal, -1));
    CHECK_FALSE(PinnedPayout(1'000'000, 2.0, PayoutKind::Normal, kBonus)); // above kPayoutLimit
}

TEST_CASE("payout_scale_prestige_scales_what_is_shown") {
    using fr::PayoutKind;
    // Silent Assassin-like: base 5000 + bonus 2000 shows 7000.
    // x1.3: shows 9100 -> 9100 is a multiple of 50: store 7100.
    CHECK(PinnedPayout(5000, 1.3, PayoutKind::Prestige, kBonus) == 7100);
    // x1.234: 8638 -> 8650 shown, store 6650.
    CHECK(PinnedPayout(5000, 1.234, PayoutKind::Prestige, kBonus) == 6650);
    CHECK(fr::ShownPayout(6650, PayoutKind::Prestige, kBonus) == 8650);
    // x2 equals the doubling formula 2(base + B) - B.
    CHECK(PinnedPayout(2500, 2.0, PayoutKind::Prestige, kBonus) == 2 * (2500 + kBonus) - kBonus);
    // x1: unchanged.
    CHECK(PinnedPayout(2000, 1.0, PayoutKind::Prestige, kBonus) == 2000);

    // Lowest default factor on the smallest prestige base: shows 4000 * 0.5 =
    // 2000, which would store 0; the stored value stays at least 50.
    CHECK(PinnedPayout(2000, 0.5, PayoutKind::Prestige, kBonus) == 50);
    CHECK(fr::ShownPayout(50, PayoutKind::Prestige, kBonus) == 2050);
    // A factor far below what the bonus allows: never negative.
    CHECK(PinnedPayout(2000, 0.05, PayoutKind::Prestige, kBonus) == 50);

    // A bonus of 0 (the game stopped adding it): like a normal entry.
    CHECK(PinnedPayout(2500, 1.234, PayoutKind::Prestige, 0) == PinnedPayout(2500, 1.234, PayoutKind::Normal, 0));
}

TEST_CASE("payout_scale_shown_is_always_a_multiple_of_50") {
    for (const fr::PayoutKind kind : {fr::PayoutKind::Normal, fr::PayoutKind::Prestige}) {
        for (const int32_t base : fr::kPayoutAmounts) {
            for (double factor = 0.5; factor <= 2.5; factor += 0.0137) {
                const auto stored = PinnedPayout(base, factor, kind, kBonus);
                REQUIRE(stored);
                const int64_t shown = fr::ShownPayout(*stored, kind, kBonus);
                CHECK(shown % fr::kPayoutStep == 0);
                CHECK(shown >= fr::kPayoutMinShown);
                CHECK(*stored >= fr::kPayoutStep);
                // Within half a step of the exact scaled amount, unless a floor applied.
                const double exact = factor * static_cast<double>(fr::ScaledFrom(base, kind, kBonus));

                if (*stored > fr::kPayoutStep && shown > fr::kPayoutMinShown)
                    CHECK(std::abs(static_cast<double>(shown) - exact) <= fr::kPayoutStep / 2.0);
            }
        }
    }
}

TEST_CASE("payout_scale_factor_clamped_and_deterministic") {
    const fr::PayoutScaleConfig cfg;
    std::set<double> factors;

    for (const uint64_t id : kIds) {
        const double f = fr::test::PayoutFactor(12345, id, cfg);
        CHECK(f >= cfg.min);
        CHECK(f <= cfg.max);
        CHECK(f == fr::test::PayoutFactor(12345, id, cfg)); // same seed + id: same factor
        factors.insert(f);
    }

    // Different ids differ.
    CHECK(factors.size() == kIds.size());

    // A different seed changes the factors.
    size_t same = 0;

    for (const uint64_t id : kIds)
        same += fr::test::PayoutFactor(12345, id, cfg) == fr::test::PayoutFactor(-777, id, cfg) ? 1 : 0;

    CHECK(same < kIds.size());

    // The factor is exp(lean + sigma * N) from the payout stream, redrawn when out of bounds.
    for (const uint64_t id : kIds) {
        const double first =
            std::exp(cfg.lean + cfg.sigma * fr::NormalFromHash(fr::DialHash(12345, "", fr::kPayoutScaleStream, id)));

        if (first >= cfg.min && first <= cfg.max)
            CHECK(fr::test::PayoutFactor(12345, id, cfg) == doctest::Approx(first).epsilon(1e-12));
        else
            CHECK(fr::RollPayoutFactor(12345, id, cfg).tries > 1);
    }

    // Not the dial stream.
    CHECK(fr::DialHash(12345, "", fr::kPayoutScaleStream, kIds[0]) != fr::DialHash(12345, "", "ai", kIds[0]));

    // Sigma 0 and lean 0: every factor is 1.
    fr::PayoutScaleConfig flat;
    flat.sigma = 0.0;
    flat.lean = 0.0;

    for (const uint64_t id : kIds)
        CHECK(fr::test::PayoutFactor(12345, id, flat) == 1.0);

    // Bounds no draw can meet: the fallback center, kept in bounds.
    fr::PayoutScaleConfig pinned;
    pinned.min = 1.5;
    pinned.max = 1.5;

    for (const uint64_t id : kIds)
        CHECK(fr::test::PayoutFactor(12345, id, pinned) == 1.5);
}

TEST_CASE("payout_scale_amounts_deterministic_per_id") {
    const fr::PayoutScaleConfig cfg;
    std::set<int32_t> amounts;

    for (uint64_t id = 1; id <= 200; ++id) {
        const double f = fr::test::PayoutFactor(4242, id * 0x9E3779B97F4A7C15ull, cfg);
        const auto a = PinnedPayout(1000, f, fr::PayoutKind::Normal, kBonus);
        REQUIRE(a);
        CHECK(a == PinnedPayout(1000, fr::test::PayoutFactor(4242, id * 0x9E3779B97F4A7C15ull, cfg),
                                fr::PayoutKind::Normal, kBonus));
        CHECK(*a >= 500);
        CHECK(*a <= 2500);
        CHECK(*a % 50 == 0);
        amounts.insert(*a);
    }

    // A bell curve over 200 ids: many different amounts, centered near 1000.
    CHECK(amounts.size() > 10);
}

TEST_CASE("payout_scale_bell_curve") {
    // The curve's shape around x1 (no lean, a floor of 0.5), truncated by redrawing.
    fr::PayoutScaleConfig cfg;
    cfg.lean = 0.0;
    cfg.min = 0.5;
    double sumLog = 0.0;
    size_t inner = 0; // within one sigma
    size_t atMin = 0;
    size_t atMax = 0;
    constexpr size_t n = 20'000;

    for (uint64_t id = 0; id < n; ++id) {
        const double f = fr::test::PayoutFactor(99, id, cfg);
        sumLog += std::log(f);
        inner += std::abs(std::log(f)) <= cfg.sigma ? 1 : 0;
        atMin += f == cfg.min ? 1 : 0;
        atMax += f == cfg.max ? 1 : 0;
    }

    // Truncated at -1.98 and 2.62 sigma: the mean of ln moves up a little (about 0.016).
    CHECK(std::abs(sumLog / n) < 0.03);
    // About 70% within one sigma of 1 (log scale): 68.3% of the 97.2% kept.
    CHECK(inner > n * 0.66);
    CHECK(inner < n * 0.74);
    // Redrawn, not clamped: nothing sits exactly at a bound.
    CHECK(atMin < n / 1000);
    CHECK(atMax < n / 1000);
}

TEST_CASE("payout_scale_sanitize") {
    const fr::PayoutScaleConfig def;
    CHECK(fr::SanitizePayoutScaleConfig({0.5, 0.25, 4.0}).sigma == 0.5);
    CHECK(fr::SanitizePayoutScaleConfig({0.5, 0.25, 4.0}).min == 0.25);
    CHECK(fr::SanitizePayoutScaleConfig({0.5, 0.25, 4.0}).max == 4.0);
    CHECK(fr::SanitizePayoutScaleConfig({-1.0, 0.25, 4.0}).sigma == def.sigma);
    CHECK(fr::SanitizePayoutScaleConfig({std::numeric_limits<double>::infinity(), 0.25, 4.0}).sigma == def.sigma);

    // Bounds fall back as a pair.
    for (const auto& bad : {fr::PayoutScaleConfig {0.3, 0.0, 2.0}, fr::PayoutScaleConfig {0.3, 3.0, 2.0},
                            fr::PayoutScaleConfig {0.3, 0.5, -1.0},
                            fr::PayoutScaleConfig {0.3, 0.5, std::numeric_limits<double>::infinity()}}) {
        const auto safe = fr::SanitizePayoutScaleConfig(bad);
        CHECK(safe.sigma == 0.3);
        CHECK(safe.min == def.min);
        CHECK(safe.max == def.max);
    }
}

TEST_CASE("payout_scale_mode") {
    CHECK(fr::ChoosePayoutMode(false) == fr::PayoutMode::Off);
    CHECK(fr::ChoosePayoutMode(true) == fr::PayoutMode::Scale);
}

TEST_CASE("payout_scale_plan_never_compounds") {
    using fr::PayoutAction;
    using fr::PayoutKind;
    const double f = 1.37;
    const auto target = [f](int32_t base, PayoutKind kind, int32_t bonus) {
        return PinnedPayout(base, f, kind, bonus);
    };

    // First load: written from the vanilla base.
    const auto first = fr::PlanPayoutWriteTo(1000, PayoutKind::Normal, kBonus, std::nullopt, std::nullopt, target);
    CHECK(first.action == PayoutAction::Write);
    CHECK(first.target == 1350);

    // Rebuilt table (vanilla again): the same target, not compounded.
    const auto rebuilt = fr::PlanPayoutWriteTo(1000, PayoutKind::Normal, kBonus, 1350, 1000, target);
    CHECK(rebuilt.action == PayoutAction::Write);
    CHECK(rebuilt.target == 1350);

    // Kept table (this mod's value): not scaled again.
    const auto kept = fr::PlanPayoutWriteTo(1350, PayoutKind::Normal, kBonus, 1350, 1000, target);
    CHECK(kept.action == PayoutAction::AlreadyWritten);

    // Kept prestige table with the bonus already added: not scaled again.
    const auto keptBonus = fr::PlanPayoutWriteTo(5550 + kBonus, PayoutKind::Prestige, kBonus, 5550, 3500, target);
    CHECK(keptBonus.action == PayoutAction::AlreadyWritten);

    // Small counts (not payout amounts) are never written.
    for (const int32_t count : {10, 15, 20})
        CHECK(fr::PlanPayoutWriteTo(count, PayoutKind::Normal, kBonus, std::nullopt, std::nullopt, target).action ==
              PayoutAction::NotAnAmount);

    // Unknown kinds wait for observation.
    CHECK(fr::PlanPayoutWriteTo(2500, PayoutKind::Unknown, kBonus, std::nullopt, std::nullopt, target).action ==
          PayoutAction::Unknown);
}

TEST_CASE("payout_scale_low_prestige_factor_rebuilt_is_not_taken_as_written") {
    using fr::PayoutAction;
    using fr::PayoutKind;
    // base 2500 + bonus 2000 shows 4500; x0.556 shows 2500, stored 500. The
    // rebuilt table's vanilla 2500 equals that write plus the bonus.
    const auto target = [](int32_t base, PayoutKind kind, int32_t bonus) {
        return PinnedPayout(base, 0.556, kind, bonus);
    };
    const auto first = fr::PlanPayoutWriteTo(2500, PayoutKind::Prestige, kBonus, std::nullopt, std::nullopt, target);
    REQUIRE(first.action == PayoutAction::Write);
    REQUIRE(first.target == 500);

    // Without the remembered base it would look already written.
    CHECK(fr::PlanPayoutWriteTo(2500, PayoutKind::Prestige, kBonus, 500, std::nullopt, target).action ==
          PayoutAction::AlreadyWritten);
    // With it, the vanilla value is planned again, to the same target.
    const auto again = fr::PlanPayoutWriteTo(2500, PayoutKind::Prestige, kBonus, 500, 2500, target);
    CHECK(again.action == PayoutAction::Write);
    CHECK(again.target == 500);
}

TEST_CASE("payout_scale_defaults") {
    const fr::PayoutScaleConfig def;
    CHECK(def.sigma == 0.35);
    CHECK(def.min == 0.5);
    CHECK(def.max == 2.5);
    CHECK(def.lean == 0.0);
    CHECK(def.jackpotChance == 0.08);
    CHECK(def.jackpotBonus == 5000);
    CHECK(fr::kPayoutJackpotStream != fr::kPayoutScaleStream);
}

TEST_CASE("payout_scale_lean_shifts_the_median") {
    constexpr size_t n = 20'001;
    const auto median = [](const fr::PayoutScaleConfig& cfg) {
        std::vector<double> factors;
        factors.reserve(n);

        for (uint64_t id = 0; id < n; ++id)
            factors.push_back(fr::test::PayoutFactor(7, id * 0x9E3779B97F4A7C15ull, cfg));

        std::nth_element(factors.begin(), factors.begin() + n / 2, factors.end());
        return factors[n / 2];
    };

    // Wide bounds (no draw redrawn): the median is exp(lean).
    fr::PayoutScaleConfig wide;
    wide.min = 0.01;
    wide.max = 100.0;
    wide.lean = 0.1;
    CHECK(std::abs(median(wide) - std::exp(0.1)) < 0.02);

    fr::PayoutScaleConfig noLean = wide;
    noLean.lean = 0.0;
    CHECK(std::abs(median(noLean) - 1.0) < 0.02);

    fr::PayoutScaleConfig down = wide;
    down.lean = -0.2;
    CHECK(std::abs(median(down) - std::exp(-0.2)) < 0.02);

    // Per id, a larger lean never gives a smaller factor (wide bounds: the first draw is kept).
    fr::PayoutScaleConfig up = wide;
    up.lean = 0.3;

    for (uint64_t id = 0; id < 500; ++id)
        CHECK(fr::test::PayoutFactor(7, id, up) >= fr::test::PayoutFactor(7, id, noLean));

    // Defaults: centered on the vanilla amount; the bounds cut about 2.4% of
    // draws below and 0.4% above, which moves the median by well under 1%.
    const fr::PayoutScaleConfig def;
    CHECK(std::abs(median(def) - 1.0) < 0.02);
}

TEST_CASE("payout_scale_no_pile_up_at_the_bounds") {
    const fr::PayoutScaleConfig def;
    constexpr size_t n = 20'000;
    size_t atMin = 0;
    size_t atMax = 0;
    size_t redrawn = 0;
    size_t fallbacks = 0;

    for (uint64_t id = 0; id < n; ++id) {
        const fr::TruncatedDraw roll = fr::RollPayoutFactor(7, id, def);
        CHECK(roll.value >= def.min);
        CHECK(roll.value <= def.max);
        atMin += roll.value == def.min ? 1 : 0;
        atMax += roll.value == def.max ? 1 : 0;
        redrawn += roll.tries > 1 ? 1 : 0;
        fallbacks += roll.fallback ? 1 : 0;
    }

    CHECK(atMin < n / 100);
    CHECK(atMax < n / 100);
    // About 2.8% of first draws fall outside [0.5, 2.5] and are redrawn.
    CHECK(redrawn > n * 0.015);
    CHECK(redrawn < n * 0.045);
    CHECK(fallbacks < n / 1000);

    // The amounts the defaults write: none piles up at the lowest amount.
    size_t lowest = 0;

    for (uint64_t id = 0; id < n; ++id) {
        const auto stored = fr::RolledPayout(1000, fr::RollPayoutFactor(7, id, def), def, fr::PayoutKind::Normal, 0);
        REQUIRE(stored);
        CHECK(*stored >= 500);
        CHECK(*stored <= 2500);
        lowest += *stored == 500 ? 1 : 0;
    }

    // 500 is one 50-wide bin (factors 0.5 to 0.525): well under 1% of entries, not 2.8%.
    CHECK(lowest < n * 0.03);
}

TEST_CASE("payout_scale_rolled_payout_fallback") {
    // Narrow bounds far from the center: most entries fall back.
    fr::PayoutScaleConfig narrow;
    narrow.lean = 0.0;
    narrow.min = 1.4;
    narrow.max = 1.6;
    size_t fallbacks = 0;

    for (uint64_t id = 0; id < 2000; ++id) {
        const fr::TruncatedDraw roll = fr::RollPayoutFactor(3, id, narrow);
        const auto stored = fr::RolledPayout(1000, roll, narrow, fr::PayoutKind::Normal, 0);
        REQUIRE(stored);
        // Always inside [1400, 1600].
        CHECK(*stored >= 1400);
        CHECK(*stored <= 1600);
        CHECK(*stored % 50 == 0);

        if (roll.fallback) {
            ++fallbacks;
            CHECK(roll.value == 1.4); // the center x1 kept in bounds
            CHECK(*stored == std::clamp<int64_t>(1400 + roll.jitterSteps * 50, 1400, 1600));
        }
        else {
            CHECK(*stored == PinnedPayout(1000, roll.value, fr::PayoutKind::Normal, 0));
        }
    }

    CHECK(fallbacks > 500);

    // A wide range around the center: a fallback shows round50(center * base) + k * 50.
    fr::TruncatedDraw fb;
    fb.value = std::exp(0.1);
    fb.fallback = true;
    fb.jitterSteps = -3;
    const fr::PayoutScaleConfig def;
    CHECK(fr::RolledPayout(1000, fb, def, fr::PayoutKind::Normal, 0) == 1100 - 150);
    fb.jitterSteps = 5;
    CHECK(fr::RolledPayout(1000, fb, def, fr::PayoutKind::Normal, 0, 5000) == 1100 + 250 + 5000);
    // Prestige: the same on base + bonus, then the bonus taken off.
    CHECK(fr::RolledPayout(2000, fb, def, fr::PayoutKind::Prestige, kBonus) == 4400 + 250 - kBonus);
    // Invalid inputs.
    CHECK_FALSE(fr::RolledPayout(1000, fb, def, fr::PayoutKind::Unknown, 0));
    CHECK_FALSE(fr::RolledPayout(0, fb, def, fr::PayoutKind::Normal, 0));
}

TEST_CASE("payout_scale_cheap_normal_entries_scale_from_1000") {
    using fr::PayoutKind;

    for (const int32_t base : {200, 250, 300, 450, 500}) {
        CHECK(fr::ScaledFrom(base, PayoutKind::Normal, kBonus) == 1000);
        CHECK(PinnedPayout(base, 1.0, PayoutKind::Normal, kBonus) == 1000);
        CHECK(PinnedPayout(base, 1.37, PayoutKind::Normal, kBonus) ==
              PinnedPayout(1000, 1.37, PayoutKind::Normal, kBonus));
    }

    // 1000 and above: their own amount.
    CHECK(fr::ScaledFrom(1000, PayoutKind::Normal, kBonus) == 1000);
    CHECK(fr::ScaledFrom(1500, PayoutKind::Normal, kBonus) == 1500);
    CHECK(PinnedPayout(1500, 1.0, PayoutKind::Normal, kBonus) == 1500);

    // Prestige entries are not raised: base + bonus.
    CHECK(fr::ScaledFrom(2000, PayoutKind::Prestige, kBonus) == 4000);
    CHECK(fr::ScaledFrom(500, PayoutKind::Prestige, 0) == 500);
}

TEST_CASE("payout_scale_never_shows_below_500") {
    using fr::PayoutKind;
    CHECK(PinnedPayout(1000, 0.3, PayoutKind::Normal, kBonus) == 500);
    CHECK(PinnedPayout(1000, 0.474, PayoutKind::Normal, kBonus) == 500); // 474 -> 450 -> 500
    CHECK(PinnedPayout(1000, 0.525, PayoutKind::Normal, kBonus) == 550); // above the floor: rounded as before
    // Prestige with no bonus: the shown amount is floored too.
    CHECK(PinnedPayout(2000, 0.1, PayoutKind::Prestige, 0) == 500);

    // Every kind, base and factor (down to factors far below payout_min) shows at least 500 in steps of 50.
    for (const fr::PayoutKind kind : {fr::PayoutKind::Normal, fr::PayoutKind::Prestige}) {
        for (const int32_t bonus : {0, kBonus}) {
            for (const int32_t base : fr::kPayoutAmounts) {
                for (double factor = 0.01; factor <= 3.0; factor += 0.0173) {
                    const auto stored = PinnedPayout(base, factor, kind, bonus);
                    REQUIRE(stored);
                    const int64_t shown = fr::ShownPayout(*stored, kind, bonus);
                    CHECK(shown >= fr::kPayoutMinShown);
                    CHECK(shown % fr::kPayoutStep == 0);
                }
            }
        }
    }
}

TEST_CASE("payout_scale_jackpot_amounts") {
    using fr::PayoutKind;
    // Added to a normal entry's shown amount after scaling and rounding.
    CHECK(PinnedPayout(1000, 1.37, PayoutKind::Normal, kBonus, 5000) == 6350);
    CHECK(PinnedPayout(500, 1.0, PayoutKind::Normal, kBonus, 5000) == 6000);
    // After the 500 floor.
    CHECK(PinnedPayout(1000, 0.3, PayoutKind::Normal, kBonus, 5000) == 5500);
    // Never on prestige entries.
    CHECK(PinnedPayout(5000, 1.3, PayoutKind::Prestige, kBonus, 5000) ==
          PinnedPayout(5000, 1.3, PayoutKind::Prestige, kBonus));
    CHECK(fr::JackpotFor(PayoutKind::Prestige, 5000) == 0);
    CHECK(fr::JackpotFor(PayoutKind::Unknown, 5000) == 0);
    CHECK(fr::JackpotFor(PayoutKind::Normal, 5000) == 5000);
    // Invalid jackpots.
    CHECK_FALSE(PinnedPayout(1000, 1.0, PayoutKind::Normal, kBonus, -50));
    CHECK_FALSE(PinnedPayout(1000, 1.0, PayoutKind::Normal, kBonus, 75));

    // A jackpot write never compounds either: the kept table's value is recognized.
    const auto target = [](int32_t base, PayoutKind kind, int32_t bonus) {
        return PinnedPayout(base, 1.37, kind, bonus, 5000);
    };
    const auto first = fr::PlanPayoutWriteTo(500, PayoutKind::Normal, kBonus, std::nullopt, std::nullopt, target);
    REQUIRE(first.action == fr::PayoutAction::Write);
    CHECK(first.target == 6350);
    CHECK(fr::PlanPayoutWriteTo(6350, PayoutKind::Normal, kBonus, 6350, 500, target).action ==
          fr::PayoutAction::AlreadyWritten);
    const auto rebuilt = fr::PlanPayoutWriteTo(500, PayoutKind::Normal, kBonus, 6350, 500, target);
    CHECK(rebuilt.action == fr::PayoutAction::Write);
    CHECK(rebuilt.target == 6350);
}

TEST_CASE("payout_scale_jackpot_deterministic_and_rate") {
    const fr::PayoutScaleConfig cfg;
    constexpr size_t n = 20'000;
    size_t hits = 0;
    size_t hitsHighFactor = 0;
    size_t highFactor = 0;
    const double medianFactor = std::exp(cfg.lean);

    for (uint64_t id = 0; id < n; ++id) {
        const uint64_t entry = id * 0x9E3779B97F4A7C15ull + 17;
        const int32_t j = fr::PayoutJackpot(31337, entry, cfg);
        CHECK(j == fr::PayoutJackpot(31337, entry, cfg)); // same seed + id: same jackpot
        CHECK((j == 0 || j == 5000));
        const bool high = fr::test::PayoutFactor(31337, entry, cfg) > medianFactor;
        hits += j ? 1 : 0;
        highFactor += high ? 1 : 0;
        hitsHighFactor += j && high ? 1 : 0;
    }

    // About 8% of entries.
    CHECK(hits > n * 0.07);
    CHECK(hits < n * 0.09);
    // Its own stream: jackpots are about as likely on high factors as on low ones.
    CHECK(static_cast<double>(hitsHighFactor) / static_cast<double>(hits) > 0.42);
    CHECK(static_cast<double>(hitsHighFactor) / static_cast<double>(hits) < 0.58);
    CHECK(highFactor > n * 0.45);
    // From the jackpot stream.
    const double u = fr::UniformFromHash(fr::DialHash(31337, "", fr::kPayoutJackpotStream, uint64_t {17}));
    CHECK(fr::PayoutJackpot(31337, 17, cfg) == (u < 0.08 ? 5000 : 0));

    // A different seed changes which entries hit.
    size_t differ = 0;

    for (uint64_t id = 0; id < 2000; ++id)
        differ += fr::PayoutJackpot(1, id, cfg) != fr::PayoutJackpot(2, id, cfg) ? 1 : 0;

    CHECK(differ > 100);

    // Chance 0: never; chance 1: always; another bonus.
    fr::PayoutScaleConfig never;
    never.jackpotChance = 0.0;
    fr::PayoutScaleConfig always;
    always.jackpotChance = 1.0;
    always.jackpotBonus = 2500;

    for (uint64_t id = 0; id < 500; ++id) {
        CHECK(fr::PayoutJackpot(5, id, never) == 0);
        CHECK(fr::PayoutJackpot(5, id, always) == 2500);
    }
}

TEST_CASE("payout_scale_sanitize_lean_and_jackpot") {
    const fr::PayoutScaleConfig def;
    const auto with = [](auto set) {
        fr::PayoutScaleConfig cfg;
        set(cfg);
        return fr::SanitizePayoutScaleConfig(cfg);
    };
    constexpr double inf = std::numeric_limits<double>::infinity();
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    CHECK(with([](auto& c) { c.lean = -0.3; }).lean == -0.3);
    CHECK(with([](auto& c) { c.lean = 0.0; }).lean == 0.0);
    CHECK(with([&](auto& c) { c.lean = inf; }).lean == def.lean);
    CHECK(with([&](auto& c) { c.lean = nan; }).lean == def.lean);

    CHECK(with([](auto& c) { c.jackpotChance = 0.0; }).jackpotChance == 0.0);
    CHECK(with([](auto& c) { c.jackpotChance = 1.0; }).jackpotChance == 1.0);
    CHECK(with([](auto& c) { c.jackpotChance = 0.25; }).jackpotChance == 0.25);

    for (const double bad : {-0.01, 1.5, inf, nan})
        CHECK(with([&](auto& c) { c.jackpotChance = bad; }).jackpotChance == def.jackpotChance);

    CHECK(with([](auto& c) { c.jackpotBonus = 0; }).jackpotBonus == 0);
    CHECK(with([](auto& c) { c.jackpotBonus = 2500; }).jackpotBonus == 2500);
    CHECK(with([](auto& c) { c.jackpotBonus = 10'000; }).jackpotBonus == 10'000);

    for (const double bad : {-50.0, 75.0, 2500.5, 10'050.0, 200'000.0, inf, nan})
        CHECK(with([&](auto& c) { c.jackpotBonus = bad; }).jackpotBonus == def.jackpotBonus);

    // One bad field leaves the others alone.
    const auto mixed = with([&](auto& c) {
        c.lean = nan;
        c.jackpotChance = 0.2;
        c.jackpotBonus = 1000;
        c.sigma = 0.5;
    });
    CHECK(mixed.lean == def.lean);
    CHECK(mixed.jackpotChance == 0.2);
    CHECK(mixed.jackpotBonus == 1000);
    CHECK(mixed.sigma == 0.5);
}

TEST_CASE("payout_same_base: shared bases are the rounded means of each kind") {
    using fr::PayoutKind;
    // Prestige 2000, 3000, 6000: mean 3666.7 -> 3650. Normal 500, 1000, 1500, 1000: mean 1000.
    const std::vector<fr::PayoutBaseSample> samples {
        {1, 2000, PayoutKind::Prestige}, {2, 3000, PayoutKind::Prestige}, {3, 6000, PayoutKind::Prestige},
        {4, 500, PayoutKind::Normal},    {5, 1000, PayoutKind::Normal},   {6, 1500, PayoutKind::Normal},
        {7, 1000, PayoutKind::Normal},   {1, 8000, PayoutKind::Prestige}, // a second instance of id 1: counted once (its first sample)
        {8, 15, PayoutKind::Normal},                                      // not a payout amount
        {9, 7000, PayoutKind::Unknown},                                   // unknown kind
    };
    const fr::SharedPayoutBases shared = fr::ComputeSharedPayoutBases(samples);
    CHECK(shared.prestige == 3650);
    CHECK(shared.prestigeEntries == 3);
    CHECK(shared.normal == 1000);
    CHECK(shared.normalEntries == 4);
}

TEST_CASE("payout_same_base: means round to 50, halves up") {
    using fr::PayoutKind;
    // 1000, 1500 -> 1250.
    const std::vector<fr::PayoutBaseSample> a {{1, 1000, PayoutKind::Normal}, {2, 1500, PayoutKind::Normal}};
    CHECK(fr::ComputeSharedPayoutBases(a).normal == 1250);
    // 1000, 1000, 1500 -> 1166.7 -> 1150.
    const std::vector<fr::PayoutBaseSample> b {
        {1, 1000, PayoutKind::Normal}, {2, 1000, PayoutKind::Normal}, {3, 1500, PayoutKind::Normal}};
    CHECK(fr::ComputeSharedPayoutBases(b).normal == 1150);
    // 500, 1000, 1000, 1000 -> 875 -> 900 (half up).
    const std::vector<fr::PayoutBaseSample> c {{1, 500, PayoutKind::Normal},
                                               {2, 1000, PayoutKind::Normal},
                                               {3, 1000, PayoutKind::Normal},
                                               {4, 1000, PayoutKind::Normal}};
    CHECK(fr::ComputeSharedPayoutBases(c).normal == 900);
    // No entries of a kind: no shared base for it.
    CHECK_FALSE(fr::ComputeSharedPayoutBases(c).prestige);
    CHECK_FALSE(fr::ComputeSharedPayoutBases({}).normal);
}

TEST_CASE("payout_same_base: the base each kind scales from") {
    using fr::PayoutKind;
    fr::SharedPayoutBases shared;
    shared.prestige = 2950;
    shared.normal = 1050;
    CHECK(fr::ScaleBaseFor(6000, PayoutKind::Prestige, shared, true) == 2950);
    CHECK(fr::ScaleBaseFor(2000, PayoutKind::Prestige, shared, true) == 2950);
    CHECK(fr::ScaleBaseFor(500, PayoutKind::Normal, shared, true) == 1050);
    CHECK(fr::ScaleBaseFor(1500, PayoutKind::Normal, shared, true) == 1050);
    CHECK(fr::ScaleBaseFor(3000, PayoutKind::Unknown, shared, true) == 3000);
    // Off: every entry keeps its own base.
    CHECK(fr::ScaleBaseFor(6000, PayoutKind::Prestige, shared, false) == 6000);
    CHECK(fr::ScaleBaseFor(500, PayoutKind::Normal, shared, false) == 500);
    // A kind with no shared base keeps its own.
    const fr::SharedPayoutBases none;
    CHECK(fr::ScaleBaseFor(6000, PayoutKind::Prestige, none, true) == 6000);
}

TEST_CASE("payout_same_base: off gives exactly the earlier payouts; on evens out prestige") {
    fr::PayoutScaleConfig cfg;
    cfg.sameBase = false;
    fr::SharedPayoutBases shared;
    shared.prestige = 2950;
    shared.normal = 1050;

    for (const uint64_t id : kIds) {
        const fr::TruncatedDraw roll = fr::RollPayoutFactor(12345, id, cfg);

        for (const int32_t base : {500, 1000, 1500, 2000, 3000, 6000}) {
            const fr::PayoutKind kind = base > fr::kNormalPayoutMax ? fr::PayoutKind::Prestige : fr::PayoutKind::Normal;
            CHECK(fr::RolledPayout(fr::ScaleBaseFor(base, kind, shared, cfg.sameBase), roll, cfg, kind, kBonus) ==
                  fr::RolledPayout(base, roll, cfg, kind, kBonus));
        }
    }

    // On: Silent Assassin (6000) and a 2000 prestige objective with the same draw pay the same.
    cfg.sameBase = true;
    const fr::TruncatedDraw roll = fr::RollPayoutFactor(12345, kIds[0], cfg);
    const auto sa = fr::RolledPayout(fr::ScaleBaseFor(6000, fr::PayoutKind::Prestige, shared, true), roll, cfg,
                                     fr::PayoutKind::Prestige, kBonus);
    REQUIRE(sa);
    CHECK(sa == fr::RolledPayout(fr::ScaleBaseFor(2000, fr::PayoutKind::Prestige, shared, true), roll, cfg,
                                 fr::PayoutKind::Prestige, kBonus));
    CHECK(sa == fr::RolledPayout(2950, roll, cfg, fr::PayoutKind::Prestige, kBonus));
}

TEST_CASE("payout_same_base: the vanilla base of an entry this mod already wrote") {
    using fr::PayoutKind;
    // Nothing written before: the value is vanilla.
    CHECK(fr::VanillaBaseOf(3000, PayoutKind::Prestige, kBonus, std::nullopt, std::nullopt) == 3000);
    // A kept table holding this mod's write (with or without the bonus).
    CHECK(fr::VanillaBaseOf(4200, PayoutKind::Prestige, kBonus, 4200, 3000) == 3000);
    CHECK(fr::VanillaBaseOf(6200, PayoutKind::Prestige, kBonus, 4200, 3000) == 3000);
    CHECK(fr::VanillaBaseOf(1350, PayoutKind::Normal, kBonus, 1350, 1500) == 1500);
    // A rebuilt table: back to vanilla.
    CHECK(fr::VanillaBaseOf(3000, PayoutKind::Prestige, kBonus, 1000, 3000) == 3000);
    // Neither: the value as it is.
    CHECK(fr::VanillaBaseOf(2500, PayoutKind::Normal, kBonus, 1350, 1500) == 2500);
}

TEST_CASE("payout_same_base: on by default, kept by sanitizing") {
    CHECK(fr::PayoutScaleConfig {}.sameBase);
    fr::PayoutScaleConfig off;
    off.sameBase = false;
    CHECK_FALSE(fr::SanitizePayoutScaleConfig(off).sameBase);
}
