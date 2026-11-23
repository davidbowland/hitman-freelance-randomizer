#pragma once

// Random per-objective payout scale (setting `payouts`): every payout entry of
// the Freelancer payout table (Merces.h) gets its own factor, drawn from the
// campaign seed and the entry's sub-entity id (not the map: the safehouse
// board previews the same table every mission uses), and the amount the
// player sees is scaled and rounded to a multiple of kPayoutStep.

#include "Merces.h"
#include "Sampling.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace fr {

// Amounts the player sees are multiples of this.
inline constexpr int32_t kPayoutStep = 50;

// No objective shows less than this (after rounding).
inline constexpr int32_t kPayoutMinShown = 500;

// A normal entry whose vanilla amount is below this is scaled as if it were
// this (cheap objectives are scaled from 1,000).
inline constexpr int32_t kPayoutNormalBaseFloor = 1000;

// The largest payout_jackpot_bonus accepted.
inline constexpr int32_t kPayoutJackpotMax = 10'000;

// The hash stream tags (Rng.h DialHash's feature), so payout factors and
// jackpots never correlate with dial rolls of the same seed, nor with each other.
inline constexpr std::string_view kPayoutScaleStream = "merces_payout_scale";
inline constexpr std::string_view kPayoutJackpotStream = "merces_payout_jackpot";

struct PayoutScaleConfig {
    double sigma = 0.35; // spread of ln(factor)
    double min = 0.5;    // factor bounds (out-of-range draws are redrawn, Sampling.h)
    double max = 2.5;
    double lean = 0.0;           // mean of ln(factor): the median factor is about exp(lean)
    double jackpotChance = 0.08; // chance per normal entry of a jackpot
    double jackpotBonus = 5000;  // added to a jackpot entry's shown amount
    bool sameBase = true;        // payout_same_base: every entry of a kind scales from its kind's shared base
};

// cfg with invalid fields replaced by def's: sigma (non-finite or negative)
// alone; min and max as a pair if either is non-finite or <= 0, or min > max;
// lean if not finite; jackpotChance if not finite or outside [0, 1];
// jackpotBonus if not finite, outside [0, kPayoutJackpotMax] or not a
// multiple of kPayoutStep.
// The mod passes the settings' defaults (fr::Config) as def.
PayoutScaleConfig SanitizePayoutScaleConfig(PayoutScaleConfig cfg, const PayoutScaleConfig& def = {});

// What the payout table gets.
enum class PayoutMode {
    Off,
    Scale, // payouts
};

PayoutMode ChoosePayoutMode(bool payouts);

const char* PayoutModeName(PayoutMode mode);

// The entry's factor draw: exp(lean + sigma * N(0, 1)) truncated to
// [min, max] by redrawing (fr::DrawTruncatedLogNormal from
// DialHash(seed, "", kPayoutScaleStream, entryId), median exp(lean)); never
// clamped, so no pile-up at the bounds. Deterministic in (seed, entryId,
// cfg); cfg is sanitized first.
TruncatedDraw RollPayoutFactor(int32_t seed, uint64_t entryId, const PayoutScaleConfig& cfg);

// The entry's jackpot: jackpotBonus when U < jackpotChance, U uniform in
// [0, 1) from DialHash(seed, "", kPayoutJackpotStream, entryId), else 0.
// Deterministic in (seed, entryId, cfg); cfg is sanitized first. The kind is
// not looked at: RolledPayout ignores the jackpot for prestige entries.
int32_t PayoutJackpot(int32_t seed, uint64_t entryId, const PayoutScaleConfig& cfg);

// The jackpot that applies to an entry of kind: jackpot for normal entries, 0
// otherwise.
int32_t JackpotFor(PayoutKind kind, int32_t jackpot);

// The vanilla amount the factor is applied to: a normal base below
// kPayoutNormalBaseFloor is raised to it; base + bonus for prestige.
int64_t ScaledFrom(int32_t base, PayoutKind kind, int32_t bonus);

// amount rounded to the nearest multiple of kPayoutStep (halves up), at least
// kPayoutStep; empty for an amount that is not finite.
std::optional<int64_t> RoundPayout(double amount);

// The value payouts stores at scene start. The amount the player sees is the
// draw applied to ScaledFrom(base, kind, bonus) (fr::TruncatedAmount within
// [min, max] of cfg, step kPayoutStep; a fallback draw shows the center's
// amount plus its jitter steps), at least kPayoutMinShown, plus jackpot for a
// normal entry (never for prestige). Prestige: the game adds the bonus at
// eLoading_SceneActivated, so the stored value is that minus bonus, never
// below kPayoutStep (so it shows at least bonus + kPayoutStep). Empty for an
// unknown kind, base <= 0, a draw that is not finite and positive, a negative
// bonus, a jackpot that is negative or not a multiple of kPayoutStep, or a
// result above kPayoutLimit.
std::optional<int32_t> RolledPayout(int32_t base, const TruncatedDraw& roll, const PayoutScaleConfig& cfg,
                                    PayoutKind kind, int32_t bonus, int32_t jackpot = 0);

// One payout table entry's vanilla stored amount (before any prestige bonus)
// and its kind, for ComputeSharedPayoutBases.
struct PayoutBaseSample {
    uint64_t id;
    int32_t base;
    PayoutKind kind;
};

// payout_same_base: one base per kind, so no objective is the most profitable
// just because the game pays more for it.
struct SharedPayoutBases {
    std::optional<int32_t> prestige; // stored amount (the game adds the prestige bonus on top)
    std::optional<int32_t> normal;
    size_t prestigeEntries = 0; // distinct entries each mean was taken over
    size_t normalEntries = 0;
};

// The mean vanilla base of each kind, rounded to a multiple of kPayoutStep
// (halves up, at least kPayoutStep): each id counts once (its first sample),
// only bases that are payout amounts (PayoutAmountIndex) count, and unknown
// entries are skipped. A kind with no entries has no shared base.
SharedPayoutBases ComputeSharedPayoutBases(std::span<const PayoutBaseSample> samples);

// The base the factor is applied to: the shared base of kind when sameBase
// is on and that kind has one, else base itself.
int32_t ScaleBaseFor(int32_t base, PayoutKind kind, const SharedPayoutBases& shared, bool sameBase);

// The vanilla base of an entry holding current, given this mod's last write
// for its id and the base that write was computed from (PlanPayoutWriteTo's
// AlreadyWritten rule): lastBase when current is that write (or, for
// prestige, that write plus bonus) and not lastBase itself; else current.
int32_t VanillaBaseOf(int32_t current, PayoutKind kind, int32_t bonus, std::optional<int32_t> lastWritten,
                      std::optional<int32_t> lastBase);

// What the player sees for a stored value: value, plus bonus for prestige.
int64_t ShownPayout(int32_t value, PayoutKind kind, int32_t bonus);

} // namespace fr
