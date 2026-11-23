#include "doctest.h"

#include "Rng.h"
#include "Sampling.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <set>

namespace {

uint64_t Hash(uint64_t id) {
    return fr::DialHash(424242, "", "sampling_test", id);
}

} // namespace

TEST_CASE("sampling_resample_hash") {
    CHECK(fr::ResampleHash(0x1234, 0) == 0x1234);
    std::set<uint64_t> seen;

    for (int i = 0; i <= fr::kResampleTries; ++i) {
        CHECK(fr::ResampleHash(0x1234, i) == fr::ResampleHash(0x1234, i));
        seen.insert(fr::ResampleHash(0x1234, i));
    }

    CHECK(seen.size() == fr::kResampleTries + 1);
}

TEST_CASE("sampling_truncated_lognormal_deterministic_and_in_bounds") {
    for (uint64_t id = 0; id < 5000; ++id) {
        const fr::TruncatedDraw a = fr::DrawTruncatedLogNormal(Hash(id), 1.1, 0.35, 0.75, 2.5);
        const fr::TruncatedDraw b = fr::DrawTruncatedLogNormal(Hash(id), 1.1, 0.35, 0.75, 2.5);
        CHECK(a.value == b.value);
        CHECK(a.tries == b.tries);
        CHECK(a.fallback == b.fallback);
        CHECK(a.jitterSteps == b.jitterSteps);
        CHECK(a.value >= 0.75);
        CHECK(a.value <= 2.5);
        CHECK(a.tries >= 1);
        CHECK(a.tries <= fr::kResampleTries);
    }

    // A first draw in bounds is kept as is.
    const double first = 1.1 * std::exp(0.35 * fr::NormalFromHash(Hash(9)));

    if (first >= 0.75 && first <= 2.5) {
        const fr::TruncatedDraw d = fr::DrawTruncatedLogNormal(Hash(9), 1.1, 0.35, 0.75, 2.5);
        CHECK(d.tries == 1);
        CHECK(d.value == doctest::Approx(first).epsilon(1e-12));
    }
}

TEST_CASE("sampling_no_pile_up_at_the_bounds") {
    // Loot-like: median 2000, sigma 0.5, truncated to [1000, 3000].
    constexpr size_t n = 20'000;
    size_t atLo = 0;
    size_t atHi = 0;
    size_t lowestStep = 0;

    for (uint64_t id = 0; id < n; ++id) {
        const fr::TruncatedDraw d = fr::DrawTruncatedLogNormal(Hash(id), 2000.0, 0.5, 1000.0, 3000.0);
        atLo += d.value == 1000.0 ? 1 : 0;
        atHi += d.value == 3000.0 ? 1 : 0;
        const auto amount = fr::TruncatedAmount(d, 1.0, 1000.0, 3000.0, 50);
        REQUIRE(amount);
        CHECK(*amount >= 1000);
        CHECK(*amount <= 3000);
        CHECK(*amount % 50 == 0);
        lowestStep += *amount == 1000 ? 1 : 0;
    }

    CHECK(atLo < n / 100);
    CHECK(atHi < n / 100);
    // Clamping would put about 8% at 1000; one 50-wide bin there holds well under 2%.
    CHECK(lowestStep < n * 0.02);
}

TEST_CASE("sampling_fallback") {
    // Bounds the distribution almost never reaches: every draw falls back.
    std::set<int> jitters;
    size_t fallbacks = 0;

    for (uint64_t id = 0; id < 2000; ++id) {
        const fr::TruncatedDraw d = fr::DrawTruncatedLogNormal(Hash(id), 1.0, 0.1, 5.0, 6.0);
        REQUIRE(d.fallback);
        ++fallbacks;
        CHECK(d.tries == fr::kResampleTries);
        CHECK(d.value == 5.0); // the median kept inside the bounds
        CHECK(d.jitterSteps >= -fr::kFallbackJitterSteps);
        CHECK(d.jitterSteps <= fr::kFallbackJitterSteps);
        jitters.insert(d.jitterSteps);
        // Deterministic.
        CHECK(fr::DrawTruncatedLogNormal(Hash(id), 1.0, 0.1, 5.0, 6.0).jitterSteps == d.jitterSteps);
    }

    CHECK(fallbacks == 2000);
    CHECK(jitters.size() == 2 * fr::kFallbackJitterSteps + 1); // every k from -5 to 5 occurs

    // Fallback amount: round(center * scale) + k * step, kept in the bounds' multiples.
    fr::TruncatedDraw fb;
    fb.value = 1.1;
    fb.fallback = true;
    fb.jitterSteps = -2;
    CHECK(fr::TruncatedAmount(fb, 1000.0, 0.75, 2.5, 50) == 1000);
    fb.jitterSteps = 5;
    CHECK(fr::TruncatedAmount(fb, 1000.0, 0.75, 2.5, 50) == 1350);
    // Kept inside: center at the low bound, negative jitter.
    fb.value = 0.75;
    fb.jitterSteps = -5;
    CHECK(fr::TruncatedAmount(fb, 1000.0, 0.75, 2.5, 50) == 750);
    // A non-fallback draw ignores jitterSteps.
    fr::TruncatedDraw plain;
    plain.value = 1.234;
    plain.jitterSteps = 5;
    CHECK(fr::TruncatedAmount(plain, 1000.0, 0.75, 2.5, 50) == 1250);
    // Bounds that hold no multiple of step: the multiple nearest the low bound.
    CHECK(fr::TruncatedAmount(plain, 1000.0, 1.01, 1.04, 50) == 1000);
    // Rounding never leaves the bounds: 0.7501 * 1010 = 757.6 rounds to 750, below the low bound: 800.
    plain.value = 0.7501;
    CHECK(fr::TruncatedAmount(plain, 1010.0, 0.7501, 2.5, 50) == 800);

    // Invalid inputs.
    CHECK_FALSE(fr::TruncatedAmount(plain, 0.0, 0.75, 2.5, 50));
    CHECK_FALSE(fr::TruncatedAmount(plain, 1000.0, 0.75, 2.5, 0));
    CHECK_FALSE(fr::TruncatedAmount(plain, 1000.0, 3.0, 2.5, 50));
    const fr::TruncatedDraw bad =
        fr::DrawTruncatedLogNormal(Hash(1), std::numeric_limits<double>::quiet_NaN(), 0.3, 0.5, 2.0);
    CHECK(bad.fallback);
    CHECK(std::isfinite(bad.value));
    CHECK(fr::DrawTruncatedLogNormal(Hash(1), 1.0, -1.0, 0.5, 2.0).fallback);
    CHECK(fr::DrawTruncatedLogNormal(Hash(1), 1.0, 0.3, 2.0, 0.5).fallback);
    // Sigma 0 with the median inside: the median, first try.
    const fr::TruncatedDraw flat = fr::DrawTruncatedLogNormal(Hash(1), 1.2, 0.0, 0.5, 2.0);
    CHECK_FALSE(flat.fallback);
    CHECK(flat.value == 1.2);
}
