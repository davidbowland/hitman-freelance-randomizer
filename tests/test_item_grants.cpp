#include "doctest.h"

#include "Config.h"
#include "ItemGrants.h"
#include "LockpickFree.h"
#include "SettingsTable.h"

#include <string_view>

namespace {

fr::Guid Id(std::string_view text) {
    return *fr::ParseGuid(text);
}

const fr::Guid kExplosive = Id(fr::kMakeshiftExplosiveId);
const fr::Guid kVial = Id(fr::kLethalPoisonVialId);
const fr::Guid kClassicCoin = Id(fr::kClassicCoinId);
const fr::Guid kSafehouseCoin = Id("dda002e9-02b1-4208-82a5-cf059f3c79cf");
const fr::Guid kServiceCoin = Id("b9b0c5f2-41d1-4087-a57b-48a67731b699");
const fr::Guid kFatalidomide = Id("67637973-ff21-4b00-88c3-304f8405dbb7");
const fr::Guid kLethalPills = Id("49765e76-dea7-4ad4-b502-2bad7727a15f");
const fr::Guid kLethalSyringe = Id("af9ad679-6a7c-4f8e-9700-ceb5e6887666");
const fr::Guid kEmeticVial = Id("999e005f-d49f-4606-a929-6387bf511c72");
const fr::Guid kExplosiveRemote = Id("1e02f373-d6c8-4dfe-9d82-845f4ecd50d3");
const fr::Guid kFish = Id("4d0d6b2a-dd81-474c-a412-3bf19af8233d");

} // namespace

TEST_CASE("item grants: the given items, names and settings") {
    CHECK(fr::GrantItemId(fr::GrantItem::MakeshiftExplosive) == kExplosive);
    CHECK(fr::GrantItemId(fr::GrantItem::LethalPoison) == kVial);
    CHECK(fr::GrantItemId(fr::GrantItem::Coin) == kClassicCoin);
    CHECK(fr::GrantItemName(fr::GrantItem::Coin) == std::string_view("Classic Coin"));
    CHECK(fr::GrantItemSetting(fr::GrantItem::MakeshiftExplosive) == std::string_view("grant_makeshift_explosive"));
    CHECK(fr::GrantItemSetting(fr::GrantItem::LethalPoison) == std::string_view("grant_lethal_poison"));
    CHECK(fr::GrantItemSetting(fr::GrantItem::Coin) == std::string_view("grant_coin"));

    for (const fr::GrantItem item : fr::kGrantItems) {
        CAPTURE(fr::GrantItemSetting(item));
        CHECK(fr::kGrantItems[fr::GrantItemIndex(item)] == item);
        // The given id always counts as its own kind, and only as it.
        for (const fr::GrantItem other : fr::kGrantItems)
            CHECK(fr::IsGrantKindId(other, fr::GrantItemId(item)) == (other == item));
    }
}

TEST_CASE("item grants: any coin is a coin; the known ids count without pro.repo") {
    for (const fr::Guid& coin : {kClassicCoin, kSafehouseCoin, kServiceCoin})
        CHECK(fr::IsGrantKindId(fr::GrantItem::Coin, coin));
    CHECK_FALSE(fr::IsGrantKindId(fr::GrantItem::Coin, kFish));

    for (const fr::Guid& poison : {kVial, kFatalidomide, kLethalPills})
        CHECK(fr::IsGrantKindId(fr::GrantItem::LethalPoison, poison));
    CHECK_FALSE(fr::IsGrantKindId(fr::GrantItem::LethalPoison, kLethalSyringe));
    CHECK_FALSE(fr::IsGrantKindId(fr::GrantItem::LethalPoison, kEmeticVial));

    CHECK(fr::IsGrantKindId(fr::GrantItem::MakeshiftExplosive, kExplosive));
    CHECK_FALSE(fr::IsGrantKindId(fr::GrantItem::MakeshiftExplosive, kExplosiveRemote));
}

TEST_CASE("item grants: pro.repo names and traits (as the logs show them)") {
    using fr::GrantItem;

    // Coins.
    CHECK(fr::IsGrantKindItem(GrantItem::Coin, "(Tool) Coin Classic", "", "distraction"));
    CHECK(fr::IsGrantKindItem(GrantItem::Coin, "Coin", "Tool_Coin", "distraction"));
    CHECK(fr::IsGrantKindItem(GrantItem::Coin, "Outstanding Service Coin", "Tool_Outstanding_Coin_Reward", ""));
    CHECK_FALSE(fr::IsGrantKindItem(GrantItem::Coin, "Coconut", "", "melee_nonlethal"));

    // Lethal poisons: vials, pills, frogs and flowers; never a syringe or an emetic one.
    CHECK(fr::IsGrantKindItem(GrantItem::LethalPoison, "Lethal Poison", "", "poison, consumable_poison"));
    CHECK(fr::IsGrantKindItem(GrantItem::LethalPoison, "Lethal Poison FataLIDOMIDE", "", "poison,consumable_poison"));
    CHECK(fr::IsGrantKindItem(GrantItem::LethalPoison, "Lethal Pills S3", "", "poison, consumable_poison"));
    CHECK(fr::IsGrantKindItem(GrantItem::LethalPoison, "Dugong Lethal Poison Frog", "", "poison, consumable_poison"));
    CHECK(fr::IsGrantKindItem(GrantItem::LethalPoison, "Poison Lethal Flower", "", "poison, consumable_poison"));
    CHECK_FALSE(fr::IsGrantKindItem(GrantItem::LethalPoison, "Lethal Poison Syringe", "", "poison, syringe_poison"));
    CHECK_FALSE(fr::IsGrantKindItem(GrantItem::LethalPoison, "Emetic Poison", "", "poison, consumable_poison"));
    // Without traits, by the names alone.
    CHECK(fr::IsGrantKindItem(GrantItem::LethalPoison, "Lethal Pills", "", ""));
    CHECK_FALSE(fr::IsGrantKindItem(GrantItem::LethalPoison, "Lethal Poison Syringe", "", ""));

    // Makeshift explosives, not their remote.
    CHECK(fr::IsGrantKindItem(GrantItem::MakeshiftExplosive, "Makeshift Remote Explosive", "",
                              "NONE, explosive, explosive_device"));
    CHECK_FALSE(fr::IsGrantKindItem(GrantItem::MakeshiftExplosive, "Makeshift Explosive Remote", "",
                                    "NONE, detonator_explosive_device"));
    CHECK_FALSE(fr::IsGrantKindItem(GrantItem::MakeshiftExplosive, "The Makeshift Scrap Shotgun", "", "shotgun"));
    CHECK_FALSE(fr::IsGrantKindItem(GrantItem::MakeshiftExplosive, "ICA Remote Explosive Mk III", "",
                                    "explosive, explosive_device"));
}

TEST_CASE("item grants: traits are whole entries") {
    CHECK(fr::HasTrait("poison, consumable_poison", "consumable_poison"));
    CHECK(fr::HasTrait(" Distraction ", "distraction"));
    CHECK_FALSE(fr::HasTrait("detonator_explosive_device", "explosive_device"));
    CHECK_FALSE(fr::HasTrait("", "poison"));
}

TEST_CASE("item grants: given only when on and 47 carries none of his own") {
    CHECK(fr::DecideGrant(false, 0) == fr::GrantVerdict::Off);
    CHECK(fr::DecideGrant(false, 2) == fr::GrantVerdict::Off);
    CHECK(fr::DecideGrant(true, 0) == fr::GrantVerdict::Give);
    CHECK(fr::DecideGrant(true, 1) == fr::GrantVerdict::Carried);
}

TEST_CASE("item grants: the ledger's count of a kind (packed, picked up, given)") {
    const auto isCoin = [](const fr::Guid& id) { return fr::IsGrantKindId(fr::GrantItem::Coin, id); };
    fr::HeroItemLedger ledger;

    // He packed the safehouse coin: a coin of his own, so no Classic Coin.
    ledger.Expect(kSafehouseCoin);
    ledger.Expect(kFish);
    CHECK(ledger.Carry(isCoin).own == 1);
    CHECK(fr::DecideGrant(true, ledger.Carry(isCoin).own) == fr::GrantVerdict::Carried);

    // He threw it before the grant: none of his own now.
    ledger.Event(fr::HeroItemEvent::Removed, kSafehouseCoin, 5.0);
    ledger.Event(fr::HeroItemEvent::Thrown, kSafehouseCoin, 5.0);
    CHECK(fr::DecideGrant(true, ledger.Carry(isCoin).own) == fr::GrantVerdict::Give);

    // The given Classic Coin is given, not his own; thrown and picked up again it stays given.
    ledger.Expect(kClassicCoin, true);
    ledger.Event(fr::HeroItemEvent::PickedUp, kClassicCoin, 8.0);
    CHECK(ledger.Carry(isCoin).own == 0);
    CHECK(ledger.Carry(isCoin).given == 1);
    ledger.Event(fr::HeroItemEvent::Thrown, kClassicCoin, 30.0);
    ledger.Event(fr::HeroItemEvent::PickedUp, kClassicCoin, 40.0);
    CHECK(ledger.Own(kClassicCoin) == 0);
    CHECK(ledger.Given(kClassicCoin) == 1);
}

TEST_CASE("item grants: another kind's given items are not lockpicks") {
    const fr::Guid kSlow = Id(fr::kSlowLockpickId);
    const auto isPick = [&](const fr::Guid& id) { return id == kSlow; };
    fr::HeroItemLedger ledger;
    ledger.Expect(kClassicCoin, true);
    ledger.Expect(kVial, true);
    CHECK(ledger.Carry(isPick).given == 0);
    CHECK(fr::IsFreePick(ledger.Carry(isPick)) == false);

    ledger.Expect(kSlow, true);
    CHECK(ledger.Carry(isPick).given == 1);
    CHECK(fr::IsFreePick(ledger.Carry(isPick)) == true);
}

TEST_CASE("settings table: the item grants are free item switches, on by default") {
    for (const fr::GrantItem item : fr::kGrantItems) {
        CAPTURE(fr::GrantItemSetting(item));
        const fr::SettingInfo* info = fr::FindSetting(fr::GrantItemSetting(item));
        REQUIRE(info != nullptr);
        CHECK(info->group == fr::SettingGroup::FreeItems);
        CHECK(info->type == fr::SettingType::Bool);
        CHECK(info->def == "true");
        CHECK(info->parent.empty());
        CHECK(info->current(fr::Config {}) == "true");
    }

    const fr::Config config;
    CHECK(config.grantMakeshiftExplosive);
    CHECK(config.grantLethalPoison);
    CHECK(config.grantCoin);
}

TEST_CASE("item grants: the start gate waits for the loadout's CreateItems, not its 15 s confirm window") {
    using V = fr::GrantStartVerdict;

    // Before the delay: wait, whatever else.
    CHECK(fr::DecideGrantStart({0.5, true, false}) == V::Wait);

    // The packed items are in and the random loadout created its items (its
    // Confirm phase, still watching): go, without waiting out the confirm
    // window (a paused game or a short mission would never see it end).
    CHECK(fr::DecideGrantStart({fr::kGrantDelaySeconds, true, false}) == V::Go);

    // The random loadout still before its CreateItems: wait ...
    CHECK(fr::DecideGrantStart({5.0, true, true}) == V::Wait);
    // ... and the packed items not in: wait.
    CHECK(fr::DecideGrantStart({5.0, false, false}) == V::Wait);

    // ... but at most kGrantLoadoutWaitSeconds: then go anyway (logged).
    CHECK(fr::DecideGrantStart({fr::kGrantLoadoutWaitSeconds - 0.1, true, true}) == V::Wait);
    CHECK(fr::DecideGrantStart({fr::kGrantLoadoutWaitSeconds, true, true}) == V::GoTimedOut);
    CHECK(fr::DecideGrantStart({fr::kGrantLoadoutWaitSeconds, false, false}) == V::GoTimedOut);
    CHECK(fr::DecideGrantStart({fr::kGrantLoadoutWaitSeconds + 30, false, true}) == V::GoTimedOut);

    // Bounded well below the old worst case (list 20 s + delay + 15 s confirm).
    CHECK(fr::kGrantLoadoutWaitSeconds <= 20.0);
    CHECK(fr::kGrantDelaySeconds < fr::kGrantLoadoutWaitSeconds);
    CHECK(std::string_view(fr::GrantStartVerdictName(V::GoTimedOut)).find("timed out") != std::string_view::npos);
}
