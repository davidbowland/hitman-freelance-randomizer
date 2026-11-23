#pragma once

// Payout helpers: a roll's factor, and the payout a factor gives on the
// shipped path (fr::RolledPayout with the draw pinned to that factor and
// bounds wide enough never to clamp it).

#include "PayoutScale.h"
#include "Sampling.h"

#include <cstdint>
#include <optional>

namespace fr::test {

// RollPayoutFactor's value.
inline double PayoutFactor(int32_t seed, uint64_t entryId, const PayoutScaleConfig& cfg) {
    return RollPayoutFactor(seed, entryId, cfg).value;
}

inline std::optional<int32_t> PinnedPayout(int32_t base, double factor, PayoutKind kind, int32_t bonus,
                                           int32_t jackpot = 0) {
    PayoutScaleConfig cfg;
    cfg.min = 1e-9;
    cfg.max = 1e4;
    TruncatedDraw draw;
    draw.value = factor;
    return RolledPayout(base, draw, cfg, kind, bonus, jackpot);
}

// PlanPayoutWriteTo with PinnedPayout at factor as the target.
inline PayoutPlan PinnedPlan(int32_t current, PayoutKind kind, int32_t bonus, double factor,
                             std::optional<int32_t> lastWritten, std::optional<int32_t> lastBase = std::nullopt) {
    return PlanPayoutWriteTo(
        current, kind, bonus, lastWritten, lastBase,
        [factor](int32_t base, PayoutKind k, int32_t b) { return PinnedPayout(base, factor, k, b); });
}

} // namespace fr::test
