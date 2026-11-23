#pragma once

// Loot amounts (setting loot_amounts):
// the merces in a Freelancer mission's money item (safe, supply crate, money
// mule, New York's bank vault) is rolled by the item's ZRandEntity_int32 in
// [m_nMin, m_nMax] (vanilla [1000, 1500] in the small item). The mod writes
// m_nMin = m_nMax =
// its own amount, drawn per item from the scene seed, the item's kind and
// its spawn number in the scene (so items differ and a restart repeats them):
// a truncated log-normal (Sampling.h: out-of-range draws are redrawn, never
// clamped), rounded to multiples of 50. This header is the pure part: the
// money-item blueprints, the kind of an item from the setpieces above it, and
// the draw.

#include "LootMerces.h"
#include "Sampling.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace fr {

// What the money came from; picks the amount range.
enum class MoneyKind {
    Safe,
    Crate, // a supply crate's merces option
    Mule,  // a money mule's money (also when the mule drops it)
    Vault, // the New York bank vault's own money (IsBankVaultMoney)
};

inline constexpr size_t kMoneyKinds = 4;

const char* MoneyKindName(MoneyKind kind);

// The size class of a prop_questitem_evergreen_moneyitem_*_amount_runtime
// blueprint (by its TEMP or TBLU runtime resource id), or RewardCrate for the
// item a supply crate's merces option spawns (TBLU 008DEEB5FD16A5B0, repository
// 55c1c45e "Mercer_Pickup_VerySmall_RewardCrate", vanilla roll 700-1,500);
// None for anything else.
enum class MoneyItemSize { None, Small, Medium, Large, RewardCrate };

MoneyItemSize MoneyItemSizeOf(uint64_t rid);

const char* MoneyItemSizeName(MoneyItemSize size);

// The amount range of a kind: median and sigma of the log-normal, and the
// bounds [lo, hi] (out-of-range draws are redrawn).
struct LootAmountRange {
    double median;
    double sigma;
    double lo;
    double hi;
};

// The ranges. Sigma per kind so the tails fit the bounds (percentiles of the
// kept draws):
// - safe: median 3,250, sigma 0.70, [1,500, 10,000]: a median 5,400 in
//   [2,500, 25,000] scaled by 0.6 with the bounds, so 1,500
//   is -1.10 sigma (13.5% redrawn, which lifts the kept median to about
//   3,600) and 15,000 is +2.18 sigma (1.4% redrawn); p10 1,900, p90 8,000,
//   about 2% above 12,000;
// - supply crate: median 1,250, sigma 0.60, [250, 5,000]: a wider sigma so the
//   low end reaches
//   toward 250 (-2.68 sigma, 0.4% redrawn; ~6% below 500); 5,000 is +2.31
//   sigma (1.0% redrawn: a thin tail, ~11% above 2,500, ~1.6% above
//   4,000); p10 600, p90 2,600;
// - mule: median 1,200, sigma 0.60, [250, 3,500]: a wider sigma so the low end
//   reaches toward 250 (-2.61
//   sigma, 0.4% redrawn; ~7% below 500); 3,500 is +1.78 sigma (3.7% redrawn,
//   ~8% above 2,500); p10 550, p90 2,350;
// - bank vault: median 8,000, sigma 0.60, [4,000, 25,000]: twice the floor, so
//   4,000 is -1.16 sigma (12.4% redrawn, kept median about 8,600) and 25,000
//   is +1.90 sigma (2.9% redrawn): reachable, but only ~4% of vaults hold
//   more than 20,000 and ~14% more than 15,000; p10 4,900, p90 16,500.
LootAmountRange LootAmountRangeOf(MoneyKind kind);

// The bounds a player sets per kind (loot_amount_<kind>_min / _max; the
// defaults are LootAmountRangeOf's lo and hi).
struct LootAmountBounds {
    int32_t min;
    int32_t max;

    bool operator==(const LootAmountBounds&) const = default;
};

// Settable bounds: [kMinLootAmount, kMaxLootAmount] (the bounds must stay
// above 0 for the log-normal).
inline constexpr int32_t kMinLootAmount = 50;
inline constexpr int32_t kMaxLootAmount = 100'000;

// The default bounds of a kind: 1,500-10,000, 250-5,000, 250-3,500,
// 4,000-25,000.
LootAmountBounds DefaultLootAmountBounds(MoneyKind kind);

// "safe", "crate", "mule", "vault": the kind in the setting keys.
const char* LootAmountKey(MoneyKind kind);

// The bounds as the draw takes them: each rounded to the nearest multiple of
// kLootAmountStep (halves up, so an amount on the step stays in range),
// clamped to [kMinLootAmount, kMaxLootAmount], then swapped when min > max.
// rounded / clamped / swapped say what changed (for the log).
struct LootBoundsFix {
    LootAmountBounds bounds;
    bool rounded = false;
    bool clamped = false;
    bool swapped = false;
};
LootBoundsFix SanitizeLootAmountBounds(int32_t min, int32_t max);

// The range of a kind with these bounds: exactly LootAmountRangeOf(kind) for
// the default bounds; otherwise the default shape moved onto the new bounds
// in log space (the median keeps its place between lo and hi, sigma scales
// with ln(hi / lo)), so the same share of draws is redrawn at each end. Equal
// bounds give sigma 0 (every amount is the bound). The bounds must be
// sanitized (SanitizeLootAmountBounds).
LootAmountRange LootAmountRangeOf(MoneyKind kind, LootAmountBounds bounds);

// Amounts are multiples of this.
inline constexpr int64_t kLootAmountStep = 50;

// DialHash's feature tag, per kind (so the kinds' draws never correlate).
std::string_view LootAmountStream(MoneyKind kind);

// New York's bank vault (scene_raccoon_*): its own money is
// a small item (001A503482CA411F, repository da746498 "money mule", vanilla
// 1,000-1,500) spawned by ZItemSpawner 5b58bfa3222173fa, outside every loot
// setpiece (the spawned item's entity id is 5b58bfa322200002).
// The other money in the vault sits in a money-mule setpiece and stays Mule.
inline constexpr uint64_t kBankVaultSpawnerId = 0x5b58bfa3222173faull;
inline constexpr uint64_t kBankVaultItemId = 0x5b58bfa322200002ull;

// The vault's money: the item's own id, or the spawner's among the ids of
// the entities above it (logical parents and owning entities).
bool IsBankVaultMoney(uint64_t itemId, std::span<const uint64_t> aboveIds);

// The kind of a money item. bankVault (IsBankVaultMoney) gives Vault first;
// otherwise the loot setpieces it sits in decide (innermost first, as
// climbing its logical parents and their owning entities finds them): the
// first safe gives Safe, money mule Mule, stash / stash items / supply
// crate / selectable merces / selectable item Crate; money stacks, money
// items, wallet helpers and money-earn tokens are skipped. With none of
// those, a guess by size (known = false): small -> Mule, medium or large ->
// Safe, reward crate -> Crate, none -> Mule.
struct MoneyKindGuess {
    MoneyKind kind;
    bool known;
};

MoneyKindGuess ClassifyMoneyItem(MoneyItemSize size, std::span<const LootKind> owners, bool bankVault = false);

// The draw's hash: DialHash(seed, scene, LootAmountStream(kind), spawnIndex),
// spawnIndex counting the kind's items in the scene from 0.
uint64_t LootAmountHash(int32_t seed, std::string_view scene, MoneyKind kind, uint32_t spawnIndex);

struct LootAmountRoll {
    TruncatedDraw draw;
    int32_t amount; // a multiple of kLootAmountStep in [lo, hi] of the kind
};

// The amount for one item: DrawTruncatedLogNormal over the kind's range, then
// TruncatedAmount with step kLootAmountStep. Deterministic in its arguments.
LootAmountRoll RollLootAmount(int32_t seed, std::string_view scene, MoneyKind kind, uint32_t spawnIndex);
// The same over a given range (LootAmountRangeOf(kind, bounds)).
LootAmountRoll RollLootAmount(int32_t seed, std::string_view scene, MoneyKind kind, uint32_t spawnIndex,
                              const LootAmountRange& range);

} // namespace fr
