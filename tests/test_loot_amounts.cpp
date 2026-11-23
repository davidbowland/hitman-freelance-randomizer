#include "doctest.h"

#include "LootAmounts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kScene = "assembly:/_pro/scenes/missions/elegant/scene_elegant_mild_sapo.entity";
constexpr std::array<fr::MoneyKind, fr::kMoneyKinds> kKinds {fr::MoneyKind::Safe, fr::MoneyKind::Crate,
                                                             fr::MoneyKind::Mule, fr::MoneyKind::Vault};

std::vector<int32_t> Amounts(fr::MoneyKind kind, size_t n) {
    std::vector<int32_t> out;
    out.reserve(n);

    // Many seeds and spawn numbers, as many campaigns would give.
    for (size_t i = 0; i < n; ++i)
        out.push_back(
            fr::RollLootAmount(static_cast<int32_t>(0x5EED0000u + i / 16), kScene, kind, static_cast<uint32_t>(i % 16))
                .amount);

    return out;
}

double Share(const std::vector<int32_t>& amounts, int32_t above) {
    return static_cast<double>(std::count_if(amounts.begin(), amounts.end(), [&](int32_t a) { return a > above; })) /
           static_cast<double>(amounts.size());
}

int32_t Median(std::vector<int32_t> amounts) {
    std::nth_element(amounts.begin(), amounts.begin() + static_cast<std::ptrdiff_t>(amounts.size() / 2), amounts.end());
    return amounts[amounts.size() / 2];
}

} // namespace

TEST_CASE("loot_amount_money_item_sizes") {
    CHECK(fr::MoneyItemSizeOf(0x001A503482CA411Full) == fr::MoneyItemSize::Small);
    CHECK(fr::MoneyItemSizeOf(0x004CFD04E4EAC420ull) == fr::MoneyItemSize::Small);
    CHECK(fr::MoneyItemSizeOf(0x001153C8FE0BC805ull) == fr::MoneyItemSize::Medium);
    CHECK(fr::MoneyItemSizeOf(0x005B48FA527599F8ull) == fr::MoneyItemSize::Medium);
    CHECK(fr::MoneyItemSizeOf(0x00C6CD7FA1A8DC8Aull) == fr::MoneyItemSize::Large);
    CHECK(fr::MoneyItemSizeOf(0x00999B1FC9296C28ull) == fr::MoneyItemSize::Large);
    CHECK(fr::MoneyItemSizeOf(0x008DEEB5FD16A5B0ull) == fr::MoneyItemSize::RewardCrate); // crate merces option's item
    CHECK(std::string_view(fr::MoneyItemSizeName(fr::MoneyItemSize::RewardCrate)) == "reward crate");
    CHECK(fr::MoneyItemSizeOf(0) == fr::MoneyItemSize::None);
    CHECK(fr::MoneyItemSizeOf(0x00B09C0C1A885059ull) == fr::MoneyItemSize::None); // the mules' ZSpatialEntity spawns
    CHECK(fr::MoneyItemSizeOf(0x007F98BF08B2FF1Bull) == fr::MoneyItemSize::None); // a money stack, not an item
}

TEST_CASE("loot_amount_classify_by_owner") {
    using K = fr::LootKind;
    const auto classify = [](fr::MoneyItemSize size, std::initializer_list<K> owners) {
        const std::vector<K> list(owners);
        return fr::ClassifyMoneyItem(size, list);
    };

    // Mule: money stack inside the money-mule setpiece (item -> stack #39 -> mule #8).
    auto g = classify(fr::MoneyItemSize::Small, {K::MoneyStack, K::MoneyMule});
    CHECK(g.kind == fr::MoneyKind::Mule);
    CHECK(g.known);

    // Safe: the large stack inside the safe setpiece, whatever the item size.
    g = classify(fr::MoneyItemSize::Small, {K::MoneyStack, K::Safe});
    CHECK(g.kind == fr::MoneyKind::Safe);
    CHECK(g.known);

    // Crate merces: any crate setpiece.
    for (const K crate : {K::Stash, K::StashItems, K::SupplyCrate, K::SelectableMerces, K::SelectableItem}) {
        g = classify(fr::MoneyItemSize::Small, {K::GlobalMoney, crate, K::Safe});
        CHECK(g.kind == fr::MoneyKind::Crate);
        CHECK(g.known);
    }

    // The innermost deciding owner wins.
    CHECK(classify(fr::MoneyItemSize::Large, {K::MoneyMule, K::Safe}).kind == fr::MoneyKind::Mule);

    // No deciding owner: by size, unknown.
    g = classify(fr::MoneyItemSize::Small, {});
    CHECK(g.kind == fr::MoneyKind::Mule);
    CHECK_FALSE(g.known);
    g = classify(fr::MoneyItemSize::Medium, {K::MoneyStack, K::GlobalMoney, K::MoneyEarnToken, K::MoneyItem});
    CHECK(g.kind == fr::MoneyKind::Safe);
    CHECK_FALSE(g.known);
    CHECK(classify(fr::MoneyItemSize::Large, {}).kind == fr::MoneyKind::Safe);
    CHECK(classify(fr::MoneyItemSize::None, {}).kind == fr::MoneyKind::Mule);
    // The crate merces item: Crate from its selectable-item owner, and by size when no owner is found.
    g = classify(fr::MoneyItemSize::RewardCrate, {K::SelectableItem});
    CHECK(g.kind == fr::MoneyKind::Crate);
    CHECK(g.known);
    g = classify(fr::MoneyItemSize::RewardCrate, {});
    CHECK(g.kind == fr::MoneyKind::Crate);
    CHECK_FALSE(g.known);
}

TEST_CASE("loot_amount_bank_vault") {
    using K = fr::LootKind;

    // New York: the vault's own money (item 5b58bfa322200002 under
    // ZItemSpawner 5b58bfa3222173fa, no loot setpiece above it).
    const std::array<uint64_t, 3> above {0x5b58bfa3222173faull, 0x1234ull, 0x006f1b125c1acd3bull};
    CHECK(fr::IsBankVaultMoney(0x5b58bfa322200002ull, {}));
    CHECK(fr::IsBankVaultMoney(0x77ull, above));
    // The other money in the vault (a4eb51f995ac0002, in a money-mule setpiece) is not.
    const std::array<uint64_t, 2> mule {0x01e018a77e7655caull, 0x07605304064b5adbull};
    CHECK_FALSE(fr::IsBankVaultMoney(0xa4eb51f995ac0002ull, mule));
    CHECK_FALSE(fr::IsBankVaultMoney(0, {}));

    const std::vector<K> none;
    auto g = fr::ClassifyMoneyItem(fr::MoneyItemSize::Small, none, true);
    CHECK(g.kind == fr::MoneyKind::Vault);
    CHECK(g.known);
    // The vault rule is by id, so it wins over anything found above.
    const std::vector<K> stack {K::MoneyStack};
    CHECK(fr::ClassifyMoneyItem(fr::MoneyItemSize::Small, stack, true).kind == fr::MoneyKind::Vault);
    // Without it, the mule-like item in the vault stays a mule.
    const std::vector<K> inMule {K::MoneyStack, K::MoneyMule};
    g = fr::ClassifyMoneyItem(fr::MoneyItemSize::Small, inMule, false);
    CHECK(g.kind == fr::MoneyKind::Mule);
    CHECK(g.known);
}

TEST_CASE("loot_amount_bounds_and_step") {
    for (const fr::MoneyKind kind : kKinds) {
        const fr::LootAmountRange range = fr::LootAmountRangeOf(kind);

        for (const int32_t a : Amounts(kind, 20'000)) {
            CHECK(a % fr::kLootAmountStep == 0);
            CHECK(a >= range.lo);
            CHECK(a <= range.hi);
        }
    }

    // The ranges.
    const auto range = [](fr::MoneyKind kind) { return fr::LootAmountRangeOf(kind); };
    CHECK(range(fr::MoneyKind::Safe).lo == 1500.0);
    CHECK(range(fr::MoneyKind::Safe).hi == 10000.0);
    CHECK(range(fr::MoneyKind::Safe).median == 3250.0);
    CHECK(range(fr::MoneyKind::Safe).sigma == 0.70);
    CHECK(range(fr::MoneyKind::Crate).lo == 250.0);
    CHECK(range(fr::MoneyKind::Crate).hi == 5000.0);
    CHECK(range(fr::MoneyKind::Crate).median == 1250.0);
    CHECK(range(fr::MoneyKind::Crate).sigma == 0.60);
    CHECK(range(fr::MoneyKind::Mule).lo == 250.0);
    CHECK(range(fr::MoneyKind::Mule).hi == 3500.0);
    CHECK(range(fr::MoneyKind::Mule).median == 1200.0);
    CHECK(range(fr::MoneyKind::Mule).sigma == 0.60);
    CHECK(range(fr::MoneyKind::Vault).lo == 4000.0);
    CHECK(range(fr::MoneyKind::Vault).hi == 25000.0);
    CHECK(range(fr::MoneyKind::Vault).median == 8000.0);
    CHECK(range(fr::MoneyKind::Vault).sigma == 0.60);
}

TEST_CASE("loot_amount_deterministic") {
    for (const fr::MoneyKind kind : kKinds) {
        for (uint32_t i = 0; i < 50; ++i) {
            const fr::LootAmountRoll a = fr::RollLootAmount(-1081469652, kScene, kind, i);
            const fr::LootAmountRoll b = fr::RollLootAmount(-1081469652, kScene, kind, i);
            CHECK(a.amount == b.amount);
            CHECK(a.draw.value == b.draw.value);
            CHECK(a.draw.tries == b.draw.tries);
        }
    }

    // The scene resource is hashed case-insensitively (as dials).
    CHECK(fr::RollLootAmount(7, "Assembly:/X.entity", fr::MoneyKind::Safe, 3).amount ==
          fr::RollLootAmount(7, "assembly:/x.entity", fr::MoneyKind::Safe, 3).amount);
}

TEST_CASE("loot_amount_items_differ") {
    // Eight mules in one scene: not all the same amount (and most differ).
    for (const fr::MoneyKind kind : kKinds) {
        std::set<int32_t> distinct;

        for (uint32_t i = 0; i < 8; ++i)
            distinct.insert(fr::RollLootAmount(-1081469652, kScene, kind, i).amount);

        CHECK(distinct.size() >= 5);
    }

    // The hash depends on every input.
    const uint64_t h = fr::LootAmountHash(42, kScene, fr::MoneyKind::Mule, 0);
    CHECK(h != fr::LootAmountHash(43, kScene, fr::MoneyKind::Mule, 0));
    CHECK(h != fr::LootAmountHash(42, "assembly:/other.entity", fr::MoneyKind::Mule, 0));
    CHECK(h != fr::LootAmountHash(42, kScene, fr::MoneyKind::Safe, 0));
    CHECK(h != fr::LootAmountHash(42, kScene, fr::MoneyKind::Mule, 1));

    // Over many seeds, spawn #0 and spawn #1 rarely agree.
    size_t same = 0;

    for (int32_t seed = 1; seed <= 1000; ++seed) {
        same += fr::RollLootAmount(seed, kScene, fr::MoneyKind::Mule, 0).amount ==
                fr::RollLootAmount(seed, kScene, fr::MoneyKind::Mule, 1).amount;
    }

    CHECK(same < 100);
}

TEST_CASE("loot_amount_shape") {
    constexpr size_t n = 40'000;

    // Safe: center ~3,600, a few percent above 8,000, reaching 10,000.
    const std::vector<int32_t> safe = Amounts(fr::MoneyKind::Safe, n);
    CHECK(Median(safe) >= 3400);
    CHECK(Median(safe) <= 3850);
    CHECK(Share(safe, 8000) >= 0.008);
    CHECK(Share(safe, 8000) <= 0.12);
    CHECK(*std::max_element(safe.begin(), safe.end()) >= 9600);
    CHECK(*std::min_element(safe.begin(), safe.end()) <= 1650);

    // Crate: center ~1,250, a thin tail toward 5,000.
    const std::vector<int32_t> crate = Amounts(fr::MoneyKind::Crate, n);
    CHECK(Median(crate) >= 1150);
    CHECK(Median(crate) <= 1350);
    CHECK(Share(crate, 2500) >= 0.07);
    CHECK(Share(crate, 2500) <= 0.16);
    CHECK(Share(crate, 4000) > 0.0);
    CHECK(Share(crate, 4000) <= 0.03);
    CHECK(*std::max_element(crate.begin(), crate.end()) >= 4700);
    CHECK(*std::min_element(crate.begin(), crate.end()) <= 350);

    // Mule: center ~1,150, a few percent below 500 and above 2,500.
    const std::vector<int32_t> mule = Amounts(fr::MoneyKind::Mule, n);
    CHECK(Median(mule) >= 1075);
    CHECK(Median(mule) <= 1250);
    CHECK(Share(mule, 2500) >= 0.04);
    CHECK(Share(mule, 2500) <= 0.12);
    CHECK(1.0 - Share(mule, 499) >= 0.04);
    CHECK(1.0 - Share(mule, 499) <= 0.11);
    CHECK(*std::max_element(mule.begin(), mule.end()) >= 3300);
    CHECK(*std::min_element(mule.begin(), mule.end()) <= 350);

    // Bank vault: center ~8,600, ~4% above 20,000, reaching 25,000, never below 4,000.
    const std::vector<int32_t> vault = Amounts(fr::MoneyKind::Vault, n);
    CHECK(Median(vault) >= 8200);
    CHECK(Median(vault) <= 9000);
    CHECK(Share(vault, 20000) >= 0.025);
    CHECK(Share(vault, 20000) <= 0.06);
    CHECK(Share(vault, 15000) >= 0.10);
    CHECK(Share(vault, 15000) <= 0.18);
    CHECK(*std::max_element(vault.begin(), vault.end()) >= 24500);
    CHECK(*std::min_element(vault.begin(), vault.end()) <= 4200);

    // No pile-up at the bounds (redrawn, not clamped): the end steps hold no
    // more than a few times their neighbors.
    for (const fr::MoneyKind kind : kKinds) {
        const fr::LootAmountRange range = fr::LootAmountRangeOf(kind);
        const std::vector<int32_t>& a = kind == fr::MoneyKind::Safe    ? safe
                                        : kind == fr::MoneyKind::Crate ? crate
                                        : kind == fr::MoneyKind::Mule  ? mule
                                                                       : vault;
        const auto count = [&](int32_t v) { return std::count(a.begin(), a.end(), v); };
        const auto lo = static_cast<int32_t>(range.lo);
        const auto hi = static_cast<int32_t>(range.hi);
        CHECK(count(lo) <= 3 * count(lo + 50) + 5);
        CHECK(count(hi) <= 3 * count(hi - 50) + 5);
    }
}

TEST_CASE("loot_amount_names") {
    CHECK(std::string_view(fr::MoneyKindName(fr::MoneyKind::Safe)) == "safe");
    CHECK(std::string_view(fr::MoneyKindName(fr::MoneyKind::Crate)) == "supply crate");
    CHECK(std::string_view(fr::MoneyKindName(fr::MoneyKind::Mule)) == "money mule");
    CHECK(std::string_view(fr::MoneyKindName(fr::MoneyKind::Vault)) == "bank vault");
    CHECK(std::string_view(fr::MoneyItemSizeName(fr::MoneyItemSize::Medium)) == "medium");
    CHECK(fr::LootAmountStream(fr::MoneyKind::Safe) != fr::LootAmountStream(fr::MoneyKind::Mule));
    CHECK(fr::LootAmountStream(fr::MoneyKind::Crate) != fr::LootAmountStream(fr::MoneyKind::Mule));
    CHECK(fr::LootAmountStream(fr::MoneyKind::Vault) != fr::LootAmountStream(fr::MoneyKind::Mule));
    CHECK(fr::LootAmountStream(fr::MoneyKind::Vault) != fr::LootAmountStream(fr::MoneyKind::Safe));
}

TEST_CASE("loot_amount_bounds: defaults, sanitizing and the settings rows") {
    CHECK(fr::DefaultLootAmountBounds(fr::MoneyKind::Vault) == fr::LootAmountBounds {4000, 25000});
    CHECK(fr::DefaultLootAmountBounds(fr::MoneyKind::Safe) == fr::LootAmountBounds {1500, 10000});
    CHECK(fr::DefaultLootAmountBounds(fr::MoneyKind::Crate) == fr::LootAmountBounds {250, 5000});
    CHECK(fr::DefaultLootAmountBounds(fr::MoneyKind::Mule) == fr::LootAmountBounds {250, 3500});
    CHECK(std::string_view(fr::LootAmountKey(fr::MoneyKind::Vault)) == "vault");

    const fr::LootBoundsFix ok = fr::SanitizeLootAmountBounds(250, 5000);
    CHECK(ok.bounds == fr::LootAmountBounds {250, 5000});
    CHECK_FALSE(ok.rounded);
    CHECK_FALSE(ok.clamped);
    CHECK_FALSE(ok.swapped);

    const fr::LootBoundsFix swapped = fr::SanitizeLootAmountBounds(9000, 2000);
    CHECK(swapped.bounds == fr::LootAmountBounds {2000, 9000});
    CHECK(swapped.swapped);
    CHECK_FALSE(swapped.clamped);

    const fr::LootBoundsFix clamped = fr::SanitizeLootAmountBounds(-5, 2'000'000);
    CHECK(clamped.bounds == fr::LootAmountBounds {fr::kMinLootAmount, fr::kMaxLootAmount});
    CHECK(clamped.clamped);
    CHECK_FALSE(clamped.swapped);

    const fr::LootBoundsFix both = fr::SanitizeLootAmountBounds(500'000, 0);
    CHECK(both.bounds == fr::LootAmountBounds {fr::kMinLootAmount, fr::kMaxLootAmount});
    CHECK(both.clamped);
    CHECK(both.swapped);
}

TEST_CASE("loot_amount_bounds: bounds off the step are rounded, so every amount stays in range") {
    const fr::LootBoundsFix odd = fr::SanitizeLootAmountBounds(260, 290);
    CHECK(odd.bounds == fr::LootAmountBounds {250, 300});
    CHECK(odd.rounded);
    CHECK_FALSE(odd.clamped);
    CHECK_FALSE(odd.swapped);

    CHECK(fr::SanitizeLootAmountBounds(275, 324).bounds == fr::LootAmountBounds {300, 300}); // halves up
    CHECK(fr::SanitizeLootAmountBounds(10, 20).bounds == fr::LootAmountBounds {50, 50});
    CHECK(fr::SanitizeLootAmountBounds(99'990, 100'024).bounds == fr::LootAmountBounds {100'000, 100'000});

    // Every snapped amount of every kind lies within the sanitized bounds.
    for (const auto& [min, max] : {std::pair {260, 290}, std::pair {1010, 1040}, std::pair {77, 123}}) {
        const fr::LootAmountBounds bounds = fr::SanitizeLootAmountBounds(min, max).bounds;

        for (const fr::MoneyKind kind : kKinds) {
            const fr::LootAmountRange range = fr::LootAmountRangeOf(kind, bounds);

            for (int32_t seed = 0; seed < 200; ++seed) {
                const int64_t amount = fr::RollLootAmount(seed, "scene_x", kind, 0x1234 + seed, range).amount;
                CHECK(amount >= bounds.min);
                CHECK(amount <= bounds.max);
                CHECK(amount % fr::kLootAmountStep == 0);
            }
        }
    }
}

TEST_CASE("loot_amount_bounds: the default bounds draw exactly as before") {
    for (const fr::MoneyKind kind : kKinds) {
        const fr::LootAmountRange a = fr::LootAmountRangeOf(kind);
        const fr::LootAmountRange b = fr::LootAmountRangeOf(kind, fr::DefaultLootAmountBounds(kind));
        CHECK(a.median == b.median);
        CHECK(a.sigma == b.sigma);
        CHECK(a.lo == b.lo);
        CHECK(a.hi == b.hi);

        for (uint32_t i = 0; i < 200; ++i)
            CHECK(fr::RollLootAmount(-1081469652, kScene, kind, i).amount ==
                  fr::RollLootAmount(-1081469652, kScene, kind, i, b).amount);
    }
}

TEST_CASE("loot_amount_bounds: other bounds hold every amount and keep the shape") {
    // Safes at 10,000-60,000: the same place of the median (log scale), the same share redrawn.
    const fr::LootAmountRange def = fr::LootAmountRangeOf(fr::MoneyKind::Safe);
    const fr::LootAmountRange wide = fr::LootAmountRangeOf(fr::MoneyKind::Safe, {10000, 60000});
    CHECK(wide.lo == 10000.0);
    CHECK(wide.hi == 60000.0);
    CHECK(wide.median > 10000.0);
    CHECK(wide.median < 60000.0);
    CHECK(std::log(wide.median / wide.lo) / std::log(wide.hi / wide.lo) ==
          doctest::Approx(std::log(def.median / def.lo) / std::log(def.hi / def.lo)));
    CHECK((std::log(wide.lo / wide.median) / wide.sigma) == doctest::Approx(std::log(def.lo / def.median) / def.sigma));

    std::set<int32_t> distinct;

    for (uint32_t i = 0; i < 2000; ++i) {
        const int32_t amount = fr::RollLootAmount(77, kScene, fr::MoneyKind::Safe, i, wide).amount;
        CHECK(amount >= 10000);
        CHECK(amount <= 60000);
        CHECK(amount % fr::kLootAmountStep == 0);
        distinct.insert(amount);
    }

    CHECK(distinct.size() > 100);

    // Equal bounds: always that amount.
    const fr::LootAmountRange fixed = fr::LootAmountRangeOf(fr::MoneyKind::Mule, {2000, 2000});
    CHECK(fixed.sigma == 0.0);

    for (uint32_t i = 0; i < 50; ++i)
        CHECK(fr::RollLootAmount(5, kScene, fr::MoneyKind::Mule, i, fixed).amount == 2000);

    // A bound off the step: amounts stay inside it, in steps of 50.
    const fr::LootAmountRange odd = fr::LootAmountRangeOf(fr::MoneyKind::Crate, {275, 1025});

    for (uint32_t i = 0; i < 500; ++i) {
        const int32_t amount = fr::RollLootAmount(9, kScene, fr::MoneyKind::Crate, i, odd).amount;
        CHECK(amount >= 300);
        CHECK(amount <= 1000);
    }
}
