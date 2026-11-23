#include "Sampling.h"

#include "Rng.h"

#include <algorithm>
#include <cmath>

namespace fr {

uint64_t ResampleHash(uint64_t hash, int index) {
    if (index == 0)
        return hash;

    uint64_t st = hash ^ (static_cast<uint64_t>(static_cast<uint32_t>(index)) * 0xD1B54A32D192ED03ull);
    return SplitMix64(st);
}

TruncatedDraw DrawTruncatedLogNormal(uint64_t hash, double median, double sigma, double lo, double hi, int tries) {
    const bool valid = std::isfinite(median) && std::isfinite(sigma) && std::isfinite(lo) && std::isfinite(hi) &&
                       median > 0.0 && sigma >= 0.0 && lo > 0.0 && lo <= hi && tries >= 1;

    if (!valid) {
        TruncatedDraw out;
        out.fallback = true;
        out.tries = 0;
        out.value = std::isfinite(median) && median > 0.0 && std::isfinite(lo) && std::isfinite(hi) && lo <= hi
                        ? std::clamp(median, lo, hi)
                        : (std::isfinite(median) && median > 0.0 ? median : 1.0);
        return out;
    }

    for (int i = 0; i < tries; ++i) {
        const double value = median * std::exp(sigma * NormalFromHash(ResampleHash(hash, i)));

        if (value >= lo && value <= hi)
            return {value, false, i + 1, 0};
    }

    const double u = UniformFromHash(ResampleHash(hash, tries));
    const int span = 2 * kFallbackJitterSteps + 1;
    const int k = std::min(static_cast<int>(u * span), span - 1) - kFallbackJitterSteps;
    return {std::clamp(median, lo, hi), true, tries, k};
}

int64_t RoundToStep(double amount, int64_t step) {
    return static_cast<int64_t>(std::floor(amount / static_cast<double>(step) + 0.5)) * step;
}

std::optional<int64_t> TruncatedAmount(const TruncatedDraw& draw, double scale, double lo, double hi, int64_t step) {
    if (step <= 0 || !std::isfinite(scale) || scale <= 0.0 || !std::isfinite(draw.value) || !std::isfinite(lo) ||
        !std::isfinite(hi) || lo > hi)
        return std::nullopt;

    const double raw = draw.value * scale;
    const double low = lo * scale;
    const double high = hi * scale;

    if (std::abs(raw) > 1e15 || std::abs(low) > 1e15 || std::abs(high) > 1e15)
        return std::nullopt;

    int64_t amount = RoundToStep(raw, step) + (draw.fallback ? static_cast<int64_t>(draw.jitterSteps) * step : 0);
    const auto s = static_cast<double>(step);
    const int64_t first = static_cast<int64_t>(std::ceil(low / s)) * step;  // smallest multiple >= low
    const int64_t last = static_cast<int64_t>(std::floor(high / s)) * step; // largest multiple <= high

    if (first > last)
        return RoundToStep(low, step);

    return std::clamp(amount, first, last);
}

} // namespace fr
