#pragma once

// Truncated random draws without pile-ups at the bounds: an out-of-range draw
// is redrawn from the next sub-hash, up to a few tries; when every try fails,
// the result is the center plus a small random jitter in amount steps, so it
// stays random. Never clamps a draw; the final amount is kept to step
// multiples inside the bounds. Used by payouts (PayoutScale.h) and loot
// amounts (LootAmounts.h).

#include <cstdint>
#include <optional>

namespace fr {

// Draws before the fallback.
inline constexpr int kResampleTries = 8;

// The fallback's jitter: k uniform in [-kFallbackJitterSteps, kFallbackJitterSteps] steps.
inline constexpr int kFallbackJitterSteps = 5;

// One draw of median * exp(sigma * z), z ~ N(0, 1), truncated to [lo, hi].
struct TruncatedDraw {
    double value = 1.0;    // in [lo, hi]; for a fallback the median, clamped into [lo, hi]
    bool fallback = false; // every try fell outside [lo, hi]
    int tries = 1;         // draws made (0 for invalid inputs, else 1..tries; all of them for a fallback)
    int jitterSteps = 0;   // fallback only: k in [-kFallbackJitterSteps, kFallbackJitterSteps]
};

// The hash of try index (0-based) from the roll's hash (itself derived from
// seed and entry): try 0 uses hash itself, later tries a SplitMix64 mix of
// hash and the index. Deterministic.
uint64_t ResampleHash(uint64_t hash, int index);

// Draws z from ResampleHash(hash, i) for i = 0, 1, ... until median *
// exp(sigma * z) lies in [lo, hi], at most tries times; otherwise a fallback
// with jitterSteps from ResampleHash(hash, tries). Deterministic in its
// arguments. Inputs that are not finite, median or lo <= 0, sigma < 0,
// lo > hi or tries < 1 give a fallback at the clamped median with no jitter.
TruncatedDraw DrawTruncatedLogNormal(uint64_t hash, double median, double sigma, double lo, double hi,
                                     int tries = kResampleTries);

// amount rounded to the nearest multiple of step (halves up).
int64_t RoundToStep(double amount, int64_t step);

// The amount for a draw applied to scale (a base amount such as a vanilla
// payout; 1 when the draw is the amount itself): RoundToStep(value * scale),
// plus jitterSteps * step for a fallback, then kept inside the multiples of
// step within [lo * scale, hi * scale] (when none fits, the multiple nearest
// lo * scale). Empty for a step <= 0, a scale <= 0 or values that are not finite.
std::optional<int64_t> TruncatedAmount(const TruncatedDraw& draw, double scale, double lo, double hi, int64_t step);

} // namespace fr
