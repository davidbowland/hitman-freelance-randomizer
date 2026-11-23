#include "PayoutScale.h"

#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace fr {

PayoutScaleConfig SanitizePayoutScaleConfig(PayoutScaleConfig cfg, const PayoutScaleConfig& def) {
    if (!std::isfinite(cfg.sigma) || cfg.sigma < 0.0)
        cfg.sigma = def.sigma;

    if (!std::isfinite(cfg.min) || !std::isfinite(cfg.max) || cfg.min <= 0.0 || cfg.max <= 0.0 || cfg.min > cfg.max) {
        cfg.min = def.min;
        cfg.max = def.max;
    }

    if (!std::isfinite(cfg.lean))
        cfg.lean = def.lean;

    if (!std::isfinite(cfg.jackpotChance) || cfg.jackpotChance < 0.0 || cfg.jackpotChance > 1.0)
        cfg.jackpotChance = def.jackpotChance;

    if (!std::isfinite(cfg.jackpotBonus) || cfg.jackpotBonus < 0.0 || cfg.jackpotBonus > kPayoutJackpotMax ||
        std::fmod(cfg.jackpotBonus, kPayoutStep) != 0.0)
        cfg.jackpotBonus = def.jackpotBonus;

    return cfg;
}

PayoutMode ChoosePayoutMode(bool payouts) {
    return payouts ? PayoutMode::Scale : PayoutMode::Off;
}

const char* PayoutModeName(PayoutMode mode) {
    switch (mode) {
        case PayoutMode::Off: return "off";
        case PayoutMode::Scale: return "random scale (payouts)";
    }

    return "off";
}

TruncatedDraw RollPayoutFactor(int32_t seed, uint64_t entryId, const PayoutScaleConfig& cfg) {
    const PayoutScaleConfig safe = SanitizePayoutScaleConfig(cfg);
    return DrawTruncatedLogNormal(DialHash(seed, "", kPayoutScaleStream, entryId), std::exp(safe.lean), safe.sigma,
                                  safe.min, safe.max);
}

int32_t PayoutJackpot(int32_t seed, uint64_t entryId, const PayoutScaleConfig& cfg) {
    const PayoutScaleConfig safe = SanitizePayoutScaleConfig(cfg);
    const double u = UniformFromHash(DialHash(seed, "", kPayoutJackpotStream, entryId));
    return u < safe.jackpotChance ? static_cast<int32_t>(safe.jackpotBonus) : 0;
}

int32_t JackpotFor(PayoutKind kind, int32_t jackpot) {
    return kind == PayoutKind::Normal ? jackpot : 0;
}

int64_t ScaledFrom(int32_t base, PayoutKind kind, int32_t bonus) {
    if (kind == PayoutKind::Prestige)
        return static_cast<int64_t>(base) + bonus;

    return std::max(base, kPayoutNormalBaseFloor);
}

std::optional<int64_t> RoundPayout(double amount) {
    if (!std::isfinite(amount) || std::abs(amount) > 1e15)
        return std::nullopt;

    // Halves round up (x.5 steps -> the next multiple).
    const double steps = std::floor(amount / kPayoutStep + 0.5);
    return std::max<int64_t>(kPayoutStep, static_cast<int64_t>(steps) * kPayoutStep);
}

namespace {

bool ValidPayoutInputs(int32_t base, PayoutKind kind, int32_t bonus, int32_t jackpot) {
    return kind != PayoutKind::Unknown && base > 0 && bonus >= 0 && jackpot >= 0 && jackpot % kPayoutStep == 0;
}

// The stored value for a scaled, rounded amount: at least kPayoutMinShown
// shown, plus the jackpot (normal only), minus the bonus (prestige).
std::optional<int32_t> StoredFor(const std::optional<int64_t>& rounded, PayoutKind kind, int32_t bonus,
                                 int32_t jackpot) {
    if (!rounded)
        return std::nullopt;

    const int64_t added = kind == PayoutKind::Prestige ? bonus : 0;
    const int64_t shown = std::max<int64_t>(*rounded, kPayoutMinShown) + JackpotFor(kind, jackpot);

    // Prestige: what is stored must stay positive (and a multiple of the step
    // when the bonus is), however low the factor.
    const int64_t stored = std::max<int64_t>(shown - added, kPayoutStep);

    if (stored > kPayoutLimit)
        return std::nullopt;

    return static_cast<int32_t>(stored);
}

} // namespace

std::optional<int32_t> RolledPayout(int32_t base, const TruncatedDraw& roll, const PayoutScaleConfig& cfg,
                                    PayoutKind kind, int32_t bonus, int32_t jackpot) {
    if (!ValidPayoutInputs(base, kind, bonus, jackpot) || !std::isfinite(roll.value) || roll.value <= 0.0)
        return std::nullopt;

    const PayoutScaleConfig safe = SanitizePayoutScaleConfig(cfg);
    const auto amount =
        TruncatedAmount(roll, static_cast<double>(ScaledFrom(base, kind, bonus)), safe.min, safe.max, kPayoutStep);
    return StoredFor(amount ? std::optional<int64_t>(std::max<int64_t>(*amount, kPayoutStep)) : std::nullopt, kind,
                     bonus, jackpot);
}

SharedPayoutBases ComputeSharedPayoutBases(std::span<const PayoutBaseSample> samples) {
    std::unordered_set<uint64_t> seen;
    int64_t prestigeSum = 0;
    int64_t normalSum = 0;
    SharedPayoutBases out;

    for (const PayoutBaseSample& sample : samples) {
        if (!seen.insert(sample.id).second || sample.kind == PayoutKind::Unknown || !PayoutAmountIndex(sample.base))
            continue;

        if (sample.kind == PayoutKind::Prestige) {
            prestigeSum += sample.base;
            ++out.prestigeEntries;
        }
        else {
            normalSum += sample.base;
            ++out.normalEntries;
        }
    }

    const auto mean = [](int64_t sum, size_t count) -> std::optional<int32_t> {
        if (count == 0)
            return std::nullopt;

        const auto rounded = RoundPayout(static_cast<double>(sum) / static_cast<double>(count));
        return rounded ? std::optional<int32_t>(static_cast<int32_t>(*rounded)) : std::nullopt;
    };

    out.prestige = mean(prestigeSum, out.prestigeEntries);
    out.normal = mean(normalSum, out.normalEntries);
    return out;
}

int32_t ScaleBaseFor(int32_t base, PayoutKind kind, const SharedPayoutBases& shared, bool sameBase) {
    if (!sameBase)
        return base;

    const std::optional<int32_t>& own = kind == PayoutKind::Prestige ? shared.prestige
                                        : kind == PayoutKind::Normal ? shared.normal
                                                                     : std::optional<int32_t>();
    return own.value_or(base);
}

int32_t VanillaBaseOf(int32_t current, PayoutKind kind, int32_t bonus, std::optional<int32_t> lastWritten,
                      std::optional<int32_t> lastBase) {
    if (!lastWritten || !lastBase || current == *lastBase)
        return current;

    const bool withBonus =
        kind == PayoutKind::Prestige && static_cast<int64_t>(current) == static_cast<int64_t>(*lastWritten) + bonus;
    return current == *lastWritten || withBonus ? *lastBase : current;
}

int64_t ShownPayout(int32_t value, PayoutKind kind, int32_t bonus) {
    return static_cast<int64_t>(value) + (kind == PayoutKind::Prestige ? bonus : 0);
}

} // namespace fr
