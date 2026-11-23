#include "LootAmounts.h"

#include "GameIds.h"
#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fr {

const char* MoneyKindName(MoneyKind kind) {
    switch (kind) {
        case MoneyKind::Safe: return "safe";
        case MoneyKind::Crate: return "supply crate";
        case MoneyKind::Mule: return "money mule";
        case MoneyKind::Vault: return "bank vault";
    }

    return "?";
}

MoneyItemSize MoneyItemSizeOf(uint64_t rid) {
    // The money items (GameIds.h), and the supply crate's merces item (TBLU
    // only; its TEMP id is unknown).
    switch (rid) {
        case kMoneyItemSmallTemp:
        case kMoneyItemSmallTblu: return MoneyItemSize::Small;
        case kMoneyItemMediumTemp:
        case kMoneyItemMediumTblu: return MoneyItemSize::Medium;
        case kMoneyItemLargeTemp:
        case kMoneyItemLargeTblu: return MoneyItemSize::Large;
        case 0x008DEEB5FD16A5B0ull: return MoneyItemSize::RewardCrate;
        default: return MoneyItemSize::None;
    }
}

const char* MoneyItemSizeName(MoneyItemSize size) {
    switch (size) {
        case MoneyItemSize::None: return "none";
        case MoneyItemSize::Small: return "small";
        case MoneyItemSize::Medium: return "medium";
        case MoneyItemSize::Large: return "large";
        case MoneyItemSize::RewardCrate: return "reward crate";
    }

    return "?";
}

LootAmountRange LootAmountRangeOf(MoneyKind kind) {
    switch (kind) {
        case MoneyKind::Safe: return {3250.0, 0.70, 1500.0, 10000.0};
        case MoneyKind::Crate: return {1250.0, 0.60, 250.0, 5000.0};
        case MoneyKind::Mule: return {1200.0, 0.60, 250.0, 3500.0};
        case MoneyKind::Vault: return {8000.0, 0.60, 4000.0, 25000.0};
    }

    return {1200.0, 0.60, 250.0, 3500.0};
}

LootAmountBounds DefaultLootAmountBounds(MoneyKind kind) {
    const LootAmountRange range = LootAmountRangeOf(kind);
    return {static_cast<int32_t>(range.lo), static_cast<int32_t>(range.hi)};
}

const char* LootAmountKey(MoneyKind kind) {
    switch (kind) {
        case MoneyKind::Safe: return "safe";
        case MoneyKind::Crate: return "crate";
        case MoneyKind::Mule: return "mule";
        case MoneyKind::Vault: return "vault";
    }

    return "?";
}

LootBoundsFix SanitizeLootAmountBounds(int32_t min, int32_t max) {
    const auto onStep = [](int32_t value) {
        const int64_t step = kLootAmountStep;
        const int64_t v = value;
        // Floor division, so negative values round the same way.
        const int64_t down = (v >= 0 ? v / step : -((-v + step - 1) / step)) * step;
        return v - down >= step - step / 2 ? down + step : down;
    };
    const int64_t roundMin = onStep(min);
    const int64_t roundMax = onStep(max);

    LootBoundsFix fix;
    fix.rounded = roundMin != min || roundMax != max;
    fix.bounds.min = static_cast<int32_t>(std::clamp<int64_t>(roundMin, kMinLootAmount, kMaxLootAmount));
    fix.bounds.max = static_cast<int32_t>(std::clamp<int64_t>(roundMax, kMinLootAmount, kMaxLootAmount));
    fix.clamped = fix.bounds.min != roundMin || fix.bounds.max != roundMax;

    if (fix.bounds.min > fix.bounds.max) {
        std::swap(fix.bounds.min, fix.bounds.max);
        fix.swapped = true;
    }

    return fix;
}

LootAmountRange LootAmountRangeOf(MoneyKind kind, LootAmountBounds bounds) {
    const LootAmountRange def = LootAmountRangeOf(kind);

    if (bounds == DefaultLootAmountBounds(kind))
        return def;

    const double lo = bounds.min;
    const double hi = bounds.max;

    if (lo >= hi)
        return {lo, 0.0, lo, hi};

    // Where the median sits between the bounds (log scale), and how wide the bounds are.
    const double defSpan = std::log(def.hi / def.lo);
    const double place = std::log(def.median / def.lo) / defSpan;
    const double span = std::log(hi / lo);
    return {lo * std::exp(place * span), def.sigma * span / defSpan, lo, hi};
}

std::string_view LootAmountStream(MoneyKind kind) {
    switch (kind) {
        case MoneyKind::Safe: return "loot_amount_safe";
        case MoneyKind::Crate: return "loot_amount_crate";
        case MoneyKind::Mule: return "loot_amount_mule";
        case MoneyKind::Vault: return "loot_amount_vault";
    }

    return "loot_amount";
}

bool IsBankVaultMoney(uint64_t itemId, std::span<const uint64_t> aboveIds) {
    if (itemId == kBankVaultItemId)
        return true;

    for (const uint64_t id : aboveIds) {
        if (id == kBankVaultSpawnerId)
            return true;
    }

    return false;
}

MoneyKindGuess ClassifyMoneyItem(MoneyItemSize size, std::span<const LootKind> owners, bool bankVault) {
    if (bankVault)
        return {MoneyKind::Vault, true};

    for (const LootKind owner : owners) {
        switch (owner) {
            case LootKind::Safe: return {MoneyKind::Safe, true};
            case LootKind::MoneyMule: return {MoneyKind::Mule, true};
            case LootKind::Stash:
            case LootKind::StashItems:
            case LootKind::SupplyCrate:
            case LootKind::SelectableMerces:
            case LootKind::SelectableItem: return {MoneyKind::Crate, true};
            case LootKind::MoneyItem:
            case LootKind::MoneyStack:
            case LootKind::GlobalMoney:
            case LootKind::MoneyEarnToken: break;
        }
    }

    switch (size) {
        case MoneyItemSize::Medium:
        case MoneyItemSize::Large: return {MoneyKind::Safe, false};
        case MoneyItemSize::RewardCrate: return {MoneyKind::Crate, false};
        case MoneyItemSize::Small:
        case MoneyItemSize::None: break;
    }

    return {MoneyKind::Mule, false};
}

uint64_t LootAmountHash(int32_t seed, std::string_view scene, MoneyKind kind, uint32_t spawnIndex) {
    return DialHash(seed, scene, LootAmountStream(kind), static_cast<uint64_t>(spawnIndex));
}

LootAmountRoll RollLootAmount(int32_t seed, std::string_view scene, MoneyKind kind, uint32_t spawnIndex) {
    return RollLootAmount(seed, scene, kind, spawnIndex, LootAmountRangeOf(kind));
}

LootAmountRoll RollLootAmount(int32_t seed, std::string_view scene, MoneyKind kind, uint32_t spawnIndex,
                              const LootAmountRange& range) {
    const TruncatedDraw draw = DrawTruncatedLogNormal(LootAmountHash(seed, scene, kind, spawnIndex), range.median,
                                                      range.sigma, range.lo, range.hi);
    // The ranges have bounds above 0 with lo <= hi, so TruncatedAmount always has a value;
    // the fallback is the median rounded to the step.
    const int64_t amount = TruncatedAmount(draw, 1.0, range.lo, range.hi, kLootAmountStep)
                               .value_or(RoundToStep(range.median, kLootAmountStep));
    return {draw, static_cast<int32_t>(amount)};
}

} // namespace fr
