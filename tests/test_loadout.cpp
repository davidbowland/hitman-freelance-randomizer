#include "doctest.h"

#include "Loadout.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

fr::Guid IdOf(uint32_t n) {
    fr::Guid id;
    id.data1 = 0x20000000u + n * 2654435761u;
    id.data2 = static_cast<uint16_t>(n);
    id.data4[7] = static_cast<uint8_t>(n);
    return id;
}

uint32_t g_Next = 1;

fr::LoadoutItem Item(std::string name, std::string icon, int32_t capacity, std::vector<std::string> traits = {},
                     int32_t rarity = 0, bool silenced = false) {
    fr::LoadoutItem item;
    item.id = IdOf(g_Next++);
    item.name = std::move(name);
    item.icon = std::move(icon);
    item.capacity = capacity;
    item.traits = std::move(traits);
    item.rarity = rarity;
    item.silenced = silenced;
    item.briefcase = item.icon == "sniperrifle" || item.icon == "assaultrifle" || item.icon == "shotgun";
    return item;
}

fr::LoadoutItem Mk3() {
    fr::LoadoutItem item = Item("Lockpick Mk III", "tool", 2, {"tool"});
    item.id = *fr::ParseGuid(fr::kLockpickMk3Id);
    item.source = fr::LoadoutSource::Transient;
    return item;
}

fr::LoadoutItem Collectors() {
    fr::LoadoutItem item = Item("Collector's Lockpick", "tool", 3, {"tool"});
    item.id = *fr::ParseGuid(fr::kCollectorsLockpickId);
    return item;
}

fr::LoadoutInput Input(std::vector<fr::LoadoutItem> owned, int32_t total, std::vector<fr::LoadoutItem> carried = {}) {
    fr::LoadoutInput input;
    input.owned = std::move(owned);
    input.carried = std::move(carried);
    input.totalSlots = total;
    input.seed = 540499871;
    input.scene = "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity";
    input.startCount = 3;
    return input;
}

std::vector<fr::LoadoutItem> Wall() {
    return {
        Item("Kitchen Knife", "melee", 1, {"melee_lethal"}),
        Item("Jaeger 7 Covert", "sniperrifle", 4, {"sniperrifle"}, 3, true),
        Item("Fusil G2", "assaultrifle", 4, {"assaultrifle"}, 2),
        Item("Tactical Bartoli 12G", "shotgun", 3, {"shotgun"}, 3),
        Item("ICA19 Shortballer", "pistol", 1, {"pistol"}, 3, false),
        Item("Hackl 9S", "pistol", 2, {"pistol"}, 2, true),
        Item("Emetic Pills", "poison", 1, {"poison", "consumable_poison"}),
        Item("Lethal Poison Vial", "poison", 1, {"poison", "consumable_poison"}),
        Item("Fiber Wire", "fiberwire", 1, {"fiberwire"}),
        Item("Coin", "distraction", 1, {"distraction"}),
        Item("Remote Explosive Devil Rubber Duck", "explosives", 2, {"explosive", "explosive_device"}),
        Item("DAK X2 Covert", "smg", 3, {"smg"}, 1, true),
        Item("Crowbar", "melee", 2, {"melee_nonlethal"}),
    };
}

const fr::LoadoutItem& Picked(const fr::LoadoutInput& input, const fr::LoadoutPick& pick) {
    return input.owned[pick.owned];
}

std::vector<std::string> PickedNames(const fr::LoadoutInput& input, const fr::LoadoutPlan& plan) {
    std::vector<std::string> names;
    for (const auto& pick : plan.picks)
        names.push_back(Picked(input, pick).name);
    return names;
}

int Briefcases(const fr::LoadoutInput& input, const fr::LoadoutPlan& plan) {
    int count = 0;
    for (const auto& item : input.carried)
        count += item.briefcase;
    for (const auto& pick : plan.picks)
        count += Picked(input, pick).briefcase;
    return count;
}

} // namespace

TEST_CASE("loadout: the Mk III lockpick comes first and counts as one slot") {
    auto owned = Wall();
    owned.push_back(Mk3());
    owned.push_back(Collectors());
    const auto input = Input(owned, 5);
    const auto plan = fr::PlanLoadout(input);

    REQUIRE(!plan.picks.empty());
    CHECK(plan.picks[0].rule == fr::LoadoutRule::Lockpick);
    CHECK(Picked(input, plan.picks[0]).name == "Lockpick Mk III");
    CHECK(plan.picks[0].cost == 1);
    // Never both lockpicks.
    const auto names = PickedNames(input, plan);
    CHECK(std::count(names.begin(), names.end(), "Collector's Lockpick") == 0);
}

TEST_CASE("loadout: the Collector's Lockpick when no Mk III is owned") {
    auto owned = Wall();
    owned.push_back(Collectors());
    const auto input = Input(owned, 3);
    const auto plan = fr::PlanLoadout(input);

    REQUIRE(!plan.picks.empty());
    CHECK(Picked(input, plan.picks[0]).name == "Collector's Lockpick");
    CHECK(plan.picks[0].cost == 1); // its capacity is 3, but lockpicks take 1
}

TEST_CASE("loadout: a packed lockpick satisfies the lockpick rule") {
    auto owned = Wall();
    owned.push_back(Mk3());
    const auto input = Input(owned, 10, {Collectors()});
    const auto plan = fr::PlanLoadout(input);

    for (const auto& pick : plan.picks)
        CHECK(pick.rule != fr::LoadoutRule::Lockpick);
    CHECK(plan.usedBefore == 1);
}

TEST_CASE("loadout: no lockpick owned is noted, not invented") {
    const auto input = Input(Wall(), 4);
    const auto plan = fr::PlanLoadout(input);
    for (const auto& pick : plan.picks)
        CHECK(!fr::IsLockpickId(Picked(input, pick).id));
    CHECK(std::any_of(plan.notes.begin(), plan.notes.end(),
                      [](const std::string& note) { return note.find("owns neither") != std::string::npos; }));
}

TEST_CASE("loadout: lockpick first off skips the lockpick rule") {
    auto owned = Wall();
    owned.push_back(Mk3());
    owned.push_back(Collectors());
    auto input = Input(owned, 5);
    input.lockpickFirst = false;
    const auto plan = fr::PlanLoadout(input);

    REQUIRE(!plan.picks.empty());
    for (const auto& pick : plan.picks)
        CHECK(pick.rule != fr::LoadoutRule::Lockpick);
    CHECK(plan.picks[0].rule != fr::LoadoutRule::Lockpick);
    CHECK(std::any_of(plan.notes.begin(), plan.notes.end(), [](const std::string& note) {
        return note.find("random_loadout_lockpick_first = false") != std::string::npos;
    }));
    // Default on: the same input plans the Mk III first.
    input.lockpickFirst = true;
    const auto on = fr::PlanLoadout(input);
    REQUIRE(!on.picks.empty());
    CHECK(on.picks[0].rule == fr::LoadoutRule::Lockpick);
    CHECK(fr::LoadoutInput {}.lockpickFirst);
}

TEST_CASE("loadout: priority order lockpick, prestige, client requirement, fill") {
    auto owned = Wall();
    owned.push_back(Mk3());
    auto input = Input(owned, 15);
    input.prestige = 103;        // Epic Sniper Kill
    input.clientReqs = {68, 24}; // Knife, Fiberwire
    const auto plan = fr::PlanLoadout(input);

    REQUIRE(plan.picks.size() >= 4);
    CHECK(plan.picks[0].rule == fr::LoadoutRule::Lockpick);
    CHECK(plan.picks[1].rule == fr::LoadoutRule::Prestige);
    CHECK(Picked(input, plan.picks[1]).name == "Jaeger 7 Covert");
    CHECK(plan.picks[2].rule == fr::LoadoutRule::ClientReq);
    CHECK(Picked(input, plan.picks[2]).name == "Kitchen Knife");
    CHECK(plan.picks[3].rule == fr::LoadoutRule::ClientReq);
    CHECK(Picked(input, plan.picks[3]).name == "Fiber Wire");

    for (size_t i = 4; i < plan.picks.size(); ++i)
        CHECK(plan.picks[i].rule == fr::LoadoutRule::Fill);
}

TEST_CASE("loadout: objective items off still keeps a no-firearms objective's fill free of firearms") {
    auto owned = Wall();
    owned.push_back(Mk3());
    auto input = Input(owned, 15);
    input.clientReqs = {72}; // No Firearms
    input.objectiveItems = false;
    const auto plan = fr::PlanLoadout(input);

    REQUIRE(!plan.picks.empty());
    for (const auto& pick : plan.picks) {
        CHECK((pick.rule == fr::LoadoutRule::Lockpick || pick.rule == fr::LoadoutRule::Fill));
        CHECK_FALSE(fr::IsFirearmItem(Picked(input, pick)));
    }
}

TEST_CASE("loadout: objective items off skips the prestige and client requirement rules") {
    auto owned = Wall();
    owned.push_back(Mk3());
    auto input = Input(owned, 15);
    input.prestige = 103;        // Epic Sniper Kill
    input.clientReqs = {68, 24}; // Knife, Fiberwire
    const auto on = fr::PlanLoadout(input);
    input.objectiveItems = false;
    const auto off = fr::PlanLoadout(input);

    REQUIRE(!off.picks.empty());
    CHECK(off.picks[0].rule == fr::LoadoutRule::Lockpick);
    for (const auto& pick : off.picks)
        CHECK((pick.rule == fr::LoadoutRule::Lockpick || pick.rule == fr::LoadoutRule::Fill));
    CHECK(std::any_of(off.notes.begin(), off.notes.end(), [](const std::string& note) {
        return note.find("random_loadout_objective_items = false") != std::string::npos;
    }));

    // Default on: the same input places the objective items.
    CHECK(fr::LoadoutInput {}.objectiveItems);
    CHECK(std::any_of(on.picks.begin(), on.picks.end(),
                      [](const auto& pick) { return pick.rule == fr::LoadoutRule::Prestige; }));
    CHECK(std::any_of(on.picks.begin(), on.picks.end(),
                      [](const auto& pick) { return pick.rule == fr::LoadoutRule::ClientReq; }));
}

TEST_CASE("loadout: objective items and lockpick first off leave only the random fill") {
    auto owned = Wall();
    owned.push_back(Mk3());
    auto input = Input(owned, 15);
    input.prestige = 103;
    input.clientReqs = {68, 24};
    input.lockpickFirst = false;
    input.objectiveItems = false;
    const auto plan = fr::PlanLoadout(input);

    REQUIRE(!plan.picks.empty());
    for (const auto& pick : plan.picks)
        CHECK(pick.rule == fr::LoadoutRule::Fill);
}

TEST_CASE("loadout: Silent Assassin needs nothing") {
    auto input = Input(Wall(), 0);
    input.prestige = 101;
    const auto plan = fr::PlanLoadout(input);
    CHECK(plan.picks.empty());
    CHECK(fr::PrestigeNeeds(101).empty());
    CHECK(fr::PrestigeName(101) == "Silent Assassin");
}

TEST_CASE("loadout: never more slots than the player has") {
    for (int32_t total = 0; total <= 20; ++total) {
        for (int32_t packed = 0; packed <= 2; ++packed) {
            std::vector<fr::LoadoutItem> carried;
            if (packed >= 1)
                carried.push_back(Item("Packed Wrench", "melee", 2, {"melee_nonlethal"}));
            if (packed >= 2)
                carried.push_back(Item("Packed Coin", "distraction", 1, {"distraction"}));
            auto owned = Wall();
            owned.push_back(Mk3());
            auto input = Input(owned, total, carried);
            input.prestige = 108;
            input.clientReqs = {12, 13, 14};
            const auto plan = fr::PlanLoadout(input);
            int32_t used = plan.usedBefore;
            for (const auto& pick : plan.picks)
                used += pick.cost;
            CHECK(used == plan.usedAfter);
            if (plan.usedBefore <= total)
                CHECK(plan.usedAfter <= total);
            else
                CHECK(plan.picks.empty());
        }
    }
}

TEST_CASE("loadout: partial and complete loadouts") {
    auto owned = Wall();
    owned.push_back(Mk3());

    SUBCASE("10 of 14 packed: at most 4 slots filled") {
        std::vector<fr::LoadoutItem> carried {Item("Packed A", "melee", 5, {"melee_nonlethal"}),
                                              Item("Packed B", "melee", 5, {"melee_nonlethal"})};
        const auto input = Input(owned, 14, carried);
        const auto plan = fr::PlanLoadout(input);
        CHECK(plan.usedBefore == 10);
        CHECK(plan.usedAfter <= 14);
        CHECK(plan.usedAfter >= 13); // fill stops only when nothing fits; 1-slot items exist
    }

    SUBCASE("0 of 10: filled") {
        const auto input = Input(owned, 10);
        const auto plan = fr::PlanLoadout(input);
        CHECK(plan.usedBefore == 0);
        CHECK(plan.usedAfter == 10);
    }

    SUBCASE("15 of 15: nothing") {
        std::vector<fr::LoadoutItem> carried {Item("Packed A", "melee", 15, {"melee_nonlethal"})};
        auto input = Input(owned, 15, carried);
        input.prestige = 103;
        const auto plan = fr::PlanLoadout(input);
        CHECK(plan.picks.empty());
    }

    SUBCASE("the game's GearSlotsUsed wins when larger") {
        auto input = Input(owned, 10);
        input.reportedUsed = 9;
        const auto plan = fr::PlanLoadout(input);
        CHECK(plan.usedBefore == 9);
        CHECK(plan.usedAfter <= 10);
    }
}

TEST_CASE("loadout: at most one briefcase item, packed ones included") {
    auto owned = Wall();
    owned.push_back(Item("Shashka A33", "assaultrifle", 1, {"assaultrifle"}, 3));
    owned.push_back(Item("Hackl Leviathan", "sniperrifle", 1, {"sniperrifle"}, 1));

    for (int32_t startCount = 0; startCount < 40; ++startCount) {
        auto input = Input(owned, 30);
        input.startCount = startCount;
        const auto plan = fr::PlanLoadout(input);
        CHECK(Briefcases(input, plan) <= 1);
    }

    // Packed sniper: no briefcase item is added, not even for the prestige.
    auto input = Input(owned, 30, {Item("Packed Sniper", "sniperrifle", 2, {"sniperrifle"}, 1)});
    input.prestige = 108; // epic assault rifle: none fits now
    const auto plan = fr::PlanLoadout(input);
    CHECK(Briefcases(input, plan) == 1);
    CHECK(std::any_of(plan.notes.begin(), plan.notes.end(), [](const std::string& note) {
        return note.find("briefcase item is already in the loadout") != std::string::npos;
    }));
}

TEST_CASE("loadout: never the same item twice, packed or picked; two different pistols are fine") {
    auto owned = Wall();
    owned.push_back(owned[0]); // the knife listed twice
    const auto knife = owned[0];
    auto input = Input(owned, 40, {owned[4]}); // the Shortballer is packed
    const auto plan = fr::PlanLoadout(input);

    std::set<fr::Guid> ids;
    for (const auto& item : input.carried)
        ids.insert(item.id);
    for (const auto& pick : plan.picks)
        CHECK(ids.insert(Picked(input, pick).id).second);

    // Hackl 9S (the other pistol) may still be picked: everything fits in 40.
    const auto names = PickedNames(input, plan);
    CHECK(std::count(names.begin(), names.end(), "Hackl 9S") == 1);
    CHECK(std::count(names.begin(), names.end(), "Kitchen Knife") == 1);
}

TEST_CASE("loadout: packed items satisfy the rules; the prestige being met does not skip a client requirement") {
    auto owned = Wall();
    std::vector<fr::LoadoutItem> carried {Item("Packed Epic AR", "assaultrifle", 3, {"assaultrifle"}, 3)};
    auto input = Input(owned, 8, carried);
    input.prestige = 108;    // met by the packed AR
    input.clientReqs = {68}; // knife: not met
    const auto plan = fr::PlanLoadout(input);

    for (const auto& pick : plan.picks)
        CHECK(pick.rule != fr::LoadoutRule::Prestige);
    REQUIRE(!plan.picks.empty());
    CHECK(plan.picks[0].rule == fr::LoadoutRule::ClientReq);
    CHECK(Picked(input, plan.picks[0]).name == "Kitchen Knife");
}

TEST_CASE("loadout: a need picked once satisfies a second objective") {
    auto input = Input(Wall(), 10);
    input.clientReqs = {12, 65}; // Sniper, Kill Guard Sniper
    const auto plan = fr::PlanLoadout(input);
    int snipers = 0;
    for (const auto& pick : plan.picks)
        snipers += Picked(input, pick).icon == "sniperrifle";
    CHECK(snipers == 1);
}

TEST_CASE("loadout: one gun for two firearm kills when it can do both") {
    // The objectives' picks only (the random fill comes after).
    const auto objectivePicks = [](const fr::LoadoutInput& input, const fr::LoadoutPlan& plan) {
        std::vector<std::string> names;
        for (const auto& pick : plan.picks)
            if (pick.rule == fr::LoadoutRule::Prestige || pick.rule == fr::LoadoutRule::ClientReq)
                names.push_back(Picked(input, pick).name);
        return names;
    };

    // Prestige 109 (a legendary firearm) and client requirement 13 (a shotgun): a legendary shotgun.
    std::vector<fr::LoadoutItem> owned = Wall();
    owned.push_back(Item("Legendary Pistol", "pistol", 1, {"pistol"}, 4));
    owned.push_back(Item("Legendary Shotgun", "shotgun", 3, {"shotgun"}, 4));
    auto input = Input(owned, 12);
    input.prestige = 109;
    input.clientReqs = {13};
    const auto names = objectivePicks(input, fr::PlanLoadout(input));
    CHECK(names == std::vector<std::string> {"Legendary Shotgun"});

    // No gun does both: one for each, as before.
    auto plain = Input(Wall(), 12);
    plain.prestige = 109;
    plain.clientReqs = {13};
    plain.owned.push_back(Item("Legendary Pistol", "pistol", 1, {"pistol"}, 4));
    const auto names2 = objectivePicks(plain, fr::PlanLoadout(plain));
    CHECK(names2.size() == 2);
    CHECK(std::count(names2.begin(), names2.end(), "Legendary Pistol") == 1);
    CHECK(std::count(names2.begin(), names2.end(), "Tactical Bartoli 12G") == 1);
}

TEST_CASE("loadout: one fiber wire for Melee Kill - Epic and Fiberwire Kill") {
    std::vector<fr::LoadoutItem> owned = Wall();
    owned.push_back(Item("Epic Sword", "melee", 2, {"melee_lethal"}, 3));
    owned.push_back(Item("(Fiberwire) Rope", "melee", 1, {"fiberwire"}, 3));
    auto input = Input(owned, 12);
    input.prestige = 102;    // an epic or legendary lethal melee weapon or fiberwire
    input.clientReqs = {24}; // a fiber wire
    const auto plan = fr::PlanLoadout(input);
    std::vector<std::string> names;
    for (const auto& pick : plan.picks)
        if (pick.rule == fr::LoadoutRule::Prestige || pick.rule == fr::LoadoutRule::ClientReq)
            names.push_back(Picked(input, pick).name);
    CHECK(names == std::vector<std::string> {"(Fiberwire) Rope"});
}

TEST_CASE("loadout: objective mapping") {
    const auto matchesAny = [](const std::vector<fr::ItemNeed>& needs, const fr::LoadoutItem& item) {
        return std::any_of(needs.begin(), needs.end(),
                           [&](const fr::ItemNeed& need) { return fr::Matches(need, item); });
    };

    const auto epicSniper = Item("Jaeger 7", "sniperrifle", 4, {"sniperrifle"}, 3, true);
    const auto rareSniper = Item("Jaeger 7 Rare", "sniperrifle", 4, {"sniperrifle"}, 2, true);
    const auto legendaryPistol = Item("Pale Baller", "pistol", 1, {"pistol"}, 4, false);
    const auto silencedPistol = Item("Hackl 9S", "pistol", 1, {"pistol"}, 3, true);
    const auto knife = Item("Kitchen Knife", "melee", 1, {"melee_lethal"});
    const auto emetic = Item("Emetic Pills", "poison", 1, {"poison", "consumable_poison"});
    const auto lethal = Item("Lethal Pills", "poison", 1, {"poison", "consumable_poison"});
    const auto syringe = Item("Modern Lethal Syringe", "poison", 1, {"poison", "syringe_poison"});
    const auto flash = Item("Flash Grenade Mk III", "explosives", 1, {"explosive", "explosive_device"});
    const auto frag = Item("Fragmentation Grenade", "explosives", 2, {"explosive", "explosive_device"});
    const auto remote = Item("Remote CX Demo Block", "explosives", 2, {"explosive", "explosive_device"});
    const auto dart = Item("Kalmer 2 - Tranquilizer", "pistol", 3, {"pistol"}, 1, true);
    auto katana = Item("Melee_Katana_Luxurious", "melee", 2, {"melee_lethal"}, 3);

    CHECK(matchesAny(fr::PrestigeNeeds(103), epicSniper));
    CHECK(!matchesAny(fr::PrestigeNeeds(103), rareSniper));
    CHECK(matchesAny(fr::PrestigeNeeds(107), legendaryPistol));
    CHECK(!matchesAny(fr::PrestigeNeeds(107), silencedPistol));
    CHECK(matchesAny(fr::PrestigeNeeds(106), silencedPistol));
    CHECK(matchesAny(fr::PrestigeNeeds(109), legendaryPistol));
    CHECK(!matchesAny(fr::PrestigeNeeds(109), epicSniper));
    CHECK(matchesAny(fr::PrestigeNeeds(111), katana));
    CHECK(matchesAny(fr::PrestigeNeeds(102), katana));
    CHECK(matchesAny(fr::PrestigeNeeds(123), frag));
    CHECK(!matchesAny(fr::PrestigeNeeds(123), flash));
    CHECK(fr::PrestigeNeeds(122).empty()); // accident: no item
    CHECK(fr::PrestigeNeeds(999).empty());

    CHECK(matchesAny(fr::ClientReqNeeds(68), knife));
    CHECK(!matchesAny(fr::ClientReqNeeds(68), katana));
    CHECK(matchesAny(fr::ClientReqNeeds(8), lethal));
    CHECK(!matchesAny(fr::ClientReqNeeds(8), emetic));
    CHECK(matchesAny(fr::ClientReqNeeds(39), emetic));
    CHECK(matchesAny(fr::ClientReqNeeds(41), emetic));
    CHECK(matchesAny(fr::ClientReqNeeds(43), syringe));
    CHECK(!matchesAny(fr::ClientReqNeeds(43), lethal));
    CHECK(matchesAny(fr::ClientReqNeeds(45), remote));
    CHECK(!matchesAny(fr::ClientReqNeeds(45), flash));
    CHECK(matchesAny(fr::ClientReqNeeds(52), frag));
    CHECK(!matchesAny(fr::ClientReqNeeds(52), flash));
    CHECK(matchesAny(fr::ClientReqNeeds(56), flash));
    CHECK(matchesAny(fr::ClientReqNeeds(59), dart));
    CHECK(matchesAny(fr::ClientReqNeeds(53), silencedPistol));
    CHECK(!matchesAny(fr::ClientReqNeeds(53), legendaryPistol));
    CHECK(matchesAny(fr::ClientReqNeeds(66), epicSniper));
    CHECK(matchesAny(fr::ClientReqNeeds(16), legendaryPistol));

    // Requirements that need no item.
    for (const int32_t req : {1, 2, 3, 4, 5, 6, 7, 18, 19, 20, 21, 22, 23, 30, 31, 34, 35, 36, 38, 55, 69, 72})
        CHECK(fr::ClientReqNeeds(req).empty());
    for (const int32_t prestige : {101, 115, 116, 117, 118, 119, 120, 122, 124, 126, 128, 129, 130, 132, 133, 135, 136})
        CHECK(fr::PrestigeNeeds(prestige).empty());
}

TEST_CASE("loadout: objective mapping per the game's definitions (audit)") {
    const auto matchesAny = [](const std::vector<fr::ItemNeed>& needs, const fr::LoadoutItem& item) {
        return std::any_of(needs.begin(), needs.end(),
                           [&](const fr::ItemNeed& need) { return fr::Matches(need, item); });
    };

    // From the live log's names and tags.
    const auto rareShuriken =
        Item("Shuriken / Melee_Shuriken", "melee", 1, {"melee_lethal", "throw_lethal_deprecated"}, 2);
    const auto rareBaton = Item("Melee aluminium baton / Melee_ConcealableBaton", "melee", 2,
                                {"melee_nonlethal", "throw_nonlethal_deprecated"}, 2);
    const auto epicTrophy =
        Item("Melee_SportsPack_Trophy_A", "melee", 2, {"melee_nonlethal", "throw_nonlethal_deprecated"}, 3);
    const auto legendaryCane =
        Item("Penicillin Cane", "melee", 2, {"melee_nonlethal", "throw_nonlethal_deprecated"}, 4);
    const auto commonBat =
        Item("Baseball bat / Melee_BaseballBat", "melee", 4, {"melee_nonlethal", "throw_nonlethal_deprecated"}, 1);
    const auto wrench = Item("Wrench / Tool_Wrench", "tool", 2, {"melee_nonlethal", "throw_nonlethal_deprecated"});
    const auto waterCanister = Item("Evergreen WaterSpill Canister", "tool", 2, {"melee_nonlethal"}, 1);
    const auto fiberwire = Item("(Fiberwire) Quickdraw", "melee", 1, {"fiberwire"}, 2);
    const auto epicFiberwire = Item("(Fiberwire) Rope", "melee", 1, {"fiberwire"}, 3);
    const auto knockdown =
        Item("ICA Device Sports A RubberDuck Remote Knockdown", "explosives", 2, {"explosive", "explosive_device"}, 2);
    const auto emeticGas = Item("(Device) Remote Emetic Gas", "explosives", 1, {"explosive", "explosive_device"}, 2);
    const auto frag = Item("Fragmentation Grenade", "explosives", 2, {"explosive", "explosive_device"});
    const auto flashGrenade = Item("Flash Grenade", "explosives", 1, {"explosive", "explosive_device"});
    const auto duck = Item("Explosive Rubber Duck", "explosives", 2, {"explosive", "explosive_device"});
    const auto proxDuck = Item("Proximity Explosive Rubber Duck", "explosives", 2, {"explosive", "explosive_device"});
    const auto pistol = Item("(Pistol) ICA 19", "pistol", 1, {"pistol"}, 1);
    const auto tanto = Item("(Melee) Tanto", "melee", 2, {"melee_lethal", "throw_lethal_deprecated"}, 3);
    const auto cleaver = Item("Cleaver", "melee", 2, {"melee_lethal", "throw_lethal_deprecated"});
    const auto soap = Item("Soap", "distraction", 1, {"distraction"});
    const auto banana = Item("Banana", "distraction", 1, {"distraction"});

    // 37 Silent Takedown Rare: a rare-or-better NON-LETHAL melee item.
    CHECK(!fr::ClientReqNeeds(37).empty());
    CHECK(!matchesAny(fr::ClientReqNeeds(37), rareShuriken)); // lethal: kills instead
    CHECK(matchesAny(fr::ClientReqNeeds(37), rareBaton));
    CHECK(matchesAny(fr::ClientReqNeeds(37), epicTrophy));
    CHECK(matchesAny(fr::ClientReqNeeds(37), legendaryCane));
    CHECK(!matchesAny(fr::ClientReqNeeds(37), commonBat));
    CHECK(!matchesAny(fr::ClientReqNeeds(37), wrench));        // no rarity
    CHECK(!matchesAny(fr::ClientReqNeeds(37), waterCanister)); // common

    // 26 Melee Kill and 102 Epic Melee Kill count fiberwire.
    CHECK(matchesAny(fr::ClientReqNeeds(26), fiberwire));
    CHECK(matchesAny(fr::ClientReqNeeds(26), rareShuriken));
    CHECK(!matchesAny(fr::ClientReqNeeds(26), rareBaton));
    CHECK(matchesAny(fr::PrestigeNeeds(102), epicFiberwire));
    CHECK(!matchesAny(fr::PrestigeNeeds(102), fiberwire));  // rare
    CHECK(!matchesAny(fr::PrestigeNeeds(102), epicTrophy)); // non-lethal

    // 10 Deadly Throw: lethal only.
    CHECK(matchesAny(fr::ClientReqNeeds(10), rareShuriken));
    CHECK(!matchesAny(fr::ClientReqNeeds(10), rareBaton));

    // 33 Destroy Cameras: no item (a camera can be broken without a firearm).
    CHECK(fr::ClientReqNeeds(33).empty());

    // 44 concussive; 45 remote lethal; 51 proximity; 52 frag only.
    CHECK(matchesAny(fr::ClientReqNeeds(44), knockdown));
    CHECK(!matchesAny(fr::ClientReqNeeds(44), emeticGas));
    CHECK(matchesAny(fr::ClientReqNeeds(45), duck));
    CHECK(!matchesAny(fr::ClientReqNeeds(45), knockdown));
    CHECK(!matchesAny(fr::ClientReqNeeds(45), emeticGas));
    CHECK(!matchesAny(fr::ClientReqNeeds(45), frag));
    CHECK(!matchesAny(fr::ClientReqNeeds(45), proxDuck));
    CHECK(matchesAny(fr::ClientReqNeeds(51), proxDuck));
    CHECK(matchesAny(fr::ClientReqNeeds(52), frag));
    CHECK(!matchesAny(fr::ClientReqNeeds(52), flashGrenade));
    CHECK(!matchesAny(fr::ClientReqNeeds(9), knockdown));

    // 68 knife includes the cleaver; 111 katana includes the tanto.
    CHECK(matchesAny(fr::ClientReqNeeds(68), cleaver));
    CHECK(matchesAny(fr::PrestigeNeeds(111), tanto));

    // 70 slip: a banana, not a canister or soap.
    CHECK(matchesAny(fr::ClientReqNeeds(70), banana));
    CHECK(!matchesAny(fr::ClientReqNeeds(70), waterCanister));
    CHECK(!matchesAny(fr::ClientReqNeeds(70), soap));
}

TEST_CASE("loadout: burn and electric kills need an oil canister and a taser device") {
    const auto matchesAny = [](const std::vector<fr::ItemNeed>& needs, const fr::LoadoutItem& item) {
        return std::any_of(needs.begin(), needs.end(),
                           [&](const fr::ItemNeed& need) { return fr::Matches(need, item); });
    };

    // Game data: names and tags (TransientItems).
    auto oil = Item("Evergreen OilSpill Canister", "tool", 2, {"detonator_explosive_device", "melee_nonlethal"}, 1);
    oil.briefcase = true;
    const auto water = Item("Evergreen WaterSpill Canister", "tool", 2, {"melee_nonlethal"}, 1);
    const auto remoteTaze = Item("Remote Taze / ICA Device Modular Remote Taze", "tool", 3, {"none"}, 1);
    const auto proxTaze = Item("Proximity Taze Mine / ICA Device Modular Proximity Taze", "tool", 3, {"none"}, 1);
    const auto microTaze = Item("Remote Micro Taze / ICA Device Modular Remote Micro Taze", "tool", 2, {"none"}, 2);
    const auto proxMicroTaze =
        Item("Proximity Taze Micro Mine / ICA Device Modular Proximity Micro Taze", "tool", 2, {"none"}, 2);
    const auto hacker = Item("Electrical Keycard Hacker / Tool_Keycard Hacker Electrical", "tool", 2, {"tool"}, 2);
    const auto emp = Item("Remote EMP Charge Mini / ICA Device Modular EMP Charge Mini", "tool", 2, {"tool"}, 2);
    const auto frag = Item("Fragmentation Grenade", "explosives", 2, {"explosive", "explosive_device"});

    // 29 Burn Accident Kill and 27 Oil Trap Kill: the oil canister, not the water one.
    REQUIRE(!fr::ClientReqNeeds(29).empty());
    CHECK(matchesAny(fr::ClientReqNeeds(29), oil));
    CHECK(!matchesAny(fr::ClientReqNeeds(29), water));
    CHECK(!matchesAny(fr::ClientReqNeeds(29), frag));
    CHECK(matchesAny(fr::ClientReqNeeds(27), oil));
    CHECK(!matchesAny(fr::ClientReqNeeds(27), water));

    // 11 Electric Kill: any taze device, not the electrical keycard hacker or the EMP.
    REQUIRE(!fr::ClientReqNeeds(11).empty());
    for (const auto* taze : {&remoteTaze, &proxTaze, &microTaze, &proxMicroTaze})
        CHECK(matchesAny(fr::ClientReqNeeds(11), *taze));
    CHECK(!matchesAny(fr::ClientReqNeeds(11), hacker));
    CHECK(!matchesAny(fr::ClientReqNeeds(11), emp));
    CHECK(!matchesAny(fr::ClientReqNeeds(11), water));

    // The canister is not a "useless alone" detonator (no "detonator" in its name).
    CHECK(fr::MissingPartner(oil, {}).empty());

    // Game data, Marrakesh (29 Burn Accident, 52 Grenade, 07 Unarmed), plus
    // 11: the owned oil canister and taze device are picked.
    {
        auto input = Input({oil, water, remoteTaze, frag, hacker}, 15);
        input.briefcaseItems = true;
        input.clientReqs = {29, 52, 7, 11};
        const auto plan = fr::PlanLoadout(input);
        std::vector<std::string> required;
        for (const auto& pick : plan.picks) {
            if (pick.rule == fr::LoadoutRule::ClientReq)
                required.push_back(Picked(input, pick).name);
        }
        CHECK(std::count(required.begin(), required.end(), oil.name) == 1);
        CHECK(std::count(required.begin(), required.end(), remoteTaze.name) == 1);
        CHECK(std::count(required.begin(), required.end(), frag.name) == 1);
    }

    // A packed taze device meets 11; no oil canister owned is noted.
    {
        auto input = Input({water}, 15, {proxTaze});
        input.clientReqs = {11, 29};
        const auto plan = fr::PlanLoadout(input);
        for (const auto& pick : plan.picks)
            CHECK(pick.rule != fr::LoadoutRule::ClientReq);
        const auto noted = [&](std::string_view part) {
            return std::any_of(plan.notes.begin(), plan.notes.end(),
                               [&](const std::string& note) { return note.find(part) != std::string::npos; });
        };
        CHECK(noted("a taser device: met by"));
        CHECK(noted("an oil canister (to set alight): no owned item matches"));
    }
}

TEST_CASE("loadout: rarity rules are minimums, for packed items and for picks") {
    const auto matchesAny = [](const std::vector<fr::ItemNeed>& needs, const fr::LoadoutItem& item) {
        return std::any_of(needs.begin(), needs.end(),
                           [&](const fr::ItemNeed& need) { return fr::Matches(need, item); });
    };

    const auto sniper = [](int32_t rarity) {
        return Item("Sniper r" + std::to_string(rarity), "sniperrifle", 3, {"sniperrifle"}, rarity, true);
    };
    const auto baton = [](int32_t rarity) {
        return Item("Baton r" + std::to_string(rarity), "melee", 2, {"melee_nonlethal"}, rarity);
    };

    // Rare: rare, epic, legendary. Epic: epic, legendary. Legendary: legendary.
    for (int32_t rarity = 0; rarity <= 4; ++rarity) {
        CHECK(matchesAny(fr::ClientReqNeeds(37), baton(rarity)) == (rarity >= 2));
        CHECK(matchesAny(fr::PrestigeNeeds(103), sniper(rarity)) == (rarity >= 3));
        CHECK(matchesAny(fr::PrestigeNeeds(109), sniper(rarity)) == (rarity >= 4));
    }

    // Packed: a packed legendary baton meets 37 (nothing picked for it); a
    // packed legendary sniper meets 103.
    {
        auto input = Input({baton(2), sniper(3)}, 20, {baton(4), sniper(4)});
        input.briefcaseItems = true;
        input.prestige = 103;
        input.clientReqs = {37};
        const auto plan = fr::PlanLoadout(input);
        for (const auto& pick : plan.picks) {
            CHECK(pick.rule != fr::LoadoutRule::ClientReq);
            CHECK(pick.rule != fr::LoadoutRule::Prestige);
        }
    }

    // Picked: only epic and legendary batons own, the rule picks one of them;
    // only a legendary sniper owned, 103 picks it.
    {
        auto input = Input({baton(1), baton(3), sniper(2), sniper(4)}, 20);
        input.briefcaseItems = true;
        input.prestige = 103;
        input.clientReqs = {37};
        const auto plan = fr::PlanLoadout(input);
        bool prestige = false;
        bool client = false;
        for (const auto& pick : plan.picks) {
            if (pick.rule == fr::LoadoutRule::Prestige) {
                prestige = true;
                CHECK(Picked(input, pick).rarity == 4);
            }
            if (pick.rule == fr::LoadoutRule::ClientReq) {
                client = true;
                CHECK(Picked(input, pick).rarity == 3);
            }
        }
        CHECK(prestige);
        CHECK(client);
    }
}

TEST_CASE("loadout: the random fill skips excluded items and lone detonators") {
    auto detonator = Item("Emetic Gas Detonator", "tool", 1, {"detonator_explosive_device"});
    auto free = Item("Free Thing", "tool", 0, {"tool"});
    auto quest = Item("Quest Thing", "QuestItem", 1);
    quest.icon = "questitem";
    auto unknown = Item("?", "", 1);
    unknown.known = false;

    auto input = Input({detonator, free, quest, unknown}, 10);
    auto plan = fr::PlanLoadout(input);
    CHECK(plan.picks.empty());

    // With an explosive device in the loadout, the detonator may come.
    input = Input({detonator, free, quest, unknown}, 10,
                  {Item("Packed Duck", "explosives", 2, {"explosive", "explosive_device"})});
    plan = fr::PlanLoadout(input);
    REQUIRE(plan.picks.size() == 1);
    CHECK(Picked(input, plan.picks[0]).name == "Emetic Gas Detonator");

    CHECK(fr::FillExclusion(free) == "free (cost 0: no weight)");
    CHECK(fr::FillExclusion(quest) == "quest item");
    CHECK(fr::FillExclusion(unknown) == "not in pro.repo");
}

TEST_CASE("loadout: the random fill is weighted by slot cost") {
    // One 4-slot item against four 1-slot items, 4 slots: the big one should
    // be the first pick about half the time (weight 4 of 8).
    std::vector<fr::LoadoutItem> owned {Item("Big", "melee", 4, {"melee_nonlethal"})};
    for (int i = 0; i < 4; ++i)
        owned.push_back(Item("Small " + std::to_string(i), "melee", 1, {"melee_nonlethal"}));

    int bigFirst = 0;
    const int runs = 2000;
    for (int run = 0; run < runs; ++run) {
        auto input = Input(owned, 4);
        input.startCount = run;
        const auto plan = fr::PlanLoadout(input);
        REQUIRE(!plan.picks.empty());
        bigFirst += Picked(input, plan.picks[0]).name == "Big";
    }

    CHECK(bigFirst > runs * 40 / 100);
    CHECK(bigFirst < runs * 60 / 100);
}

TEST_CASE("loadout: deterministic, independent of list order, different per mission") {
    auto owned = Wall();
    owned.push_back(Mk3());
    auto input = Input(owned, 12);
    input.prestige = 102;
    input.clientReqs = {41, 71};

    const auto a = PickedNames(input, fr::PlanLoadout(input));
    const auto b = PickedNames(input, fr::PlanLoadout(input));
    CHECK(a == b);

    auto reversed = input;
    std::reverse(reversed.owned.begin(), reversed.owned.end());
    CHECK(PickedNames(reversed, fr::PlanLoadout(reversed)) == a);

    // Other missions (start counts, scenes) give other loadouts at least sometimes.
    std::set<std::vector<std::string>> seen;
    for (int32_t count = 0; count < 20; ++count) {
        auto other = input;
        other.startCount = count;
        seen.insert(PickedNames(other, fr::PlanLoadout(other)));
    }
    CHECK(seen.size() > 1);
}

TEST_CASE("loadout: client requirement keys of a mission scene") {
    CHECK(fr::ClientReqPrefixForScene("assembly:/_pro/scenes/missions/colombia/scene_hippo_mild_trompo.entity") ==
          std::optional<std::string>("MildTrompo"));
    CHECK(fr::ClientReqPrefixForScene("assembly:/_PRO/Scenes/Missions/Ancestral/scene_ancestral_mild_bridge.entity") ==
          std::optional<std::string>("MildBridge"));
    CHECK(fr::ClientReqPrefixForScene("assembly:/_pro/scenes/missions/bangkok/scene_tiger_hot_sicbo.entity") ==
          std::optional<std::string>("Hot"));
    CHECK(!fr::ClientReqPrefixForScene("assembly:/_pro/scenes/missions/snug/scene_vanilla.entity"));
}

TEST_CASE("loadout: CpdSet merge and the state file") {
    const std::string json =
        R"({"Name":"CpdSet","ContractSessionId":"x","Value":{"InventoryItems":["d3387f28-866d-4262-88cb-6e5b1076bac0","bad"],)"
        R"("OptinPayoutObjective":123.000000,"PersistentItems":["3c52b8c3-f4a5-4fcd-ae16-ff45b56d4351"],)"
        R"("MildBridge_ClientReq01":21.0,"Hot_ClientReqsFailed":2,"CPD_CampaignStep":4,"MyMoney":5,"TransientItems":[]}})";

    fr::LoadoutState state;
    const fr::CpdMerge merge = fr::MergeCpdSet(state, json);
    CHECK(merge.badIds == 1);
    REQUIRE(state.List("InventoryItems"));
    CHECK(state.List("InventoryItems")->size() == 1);
    CHECK(state.List("PersistentItems")->size() == 1);
    REQUIRE(state.List("TransientItems"));
    CHECK(state.List("TransientItems")->empty());
    CHECK(state.Int("OptinPayoutObjective") == std::optional<int32_t>(123));
    CHECK(state.Int("MildBridge_ClientReq01") == std::optional<int32_t>(21));
    CHECK(state.Int("CPD_CampaignStep") == std::optional<int32_t>(4));
    CHECK(!state.Int("Hot_ClientReqsFailed"));
    CHECK(!state.Int("MyMoney"));

    // A later CpdSet without PersistentItems keeps the earlier list.
    fr::MergeCpdSet(state, R"({"Name":"CpdSet","Value":{"OptinPayoutObjective":0}})");
    CHECK(state.List("PersistentItems")->size() == 1);
    CHECK(state.Int("OptinPayoutObjective") == std::optional<int32_t>(0));

    CHECK(fr::GearSlotsValue(R"({"Name":"GearSlotsTotal","Value":"15","UserId":"u"})") == std::optional<int32_t>(15));

    state.slotsTotal = 15;
    state.updatedUnix = 1234;
    const std::string text = std::string(fr::LoadoutStateHeader()) + "\n" + fr::FormatLoadoutState(state, "live") +
                             fr::FormatLoadoutState(fr::LoadoutState {}, "departure");
    const auto back = fr::ParseLoadoutState(text, "live");
    REQUIRE(back);
    CHECK(back->lists == state.lists);
    CHECK(back->ints == state.ints);
    CHECK(back->slotsTotal == std::optional<int32_t>(15));
    CHECK(back->updatedUnix == 1234);
    CHECK(!fr::ParseLoadoutState(text, "departure")); // empty section
    CHECK(!fr::ParseLoadoutState("garbage", "live"));
}

TEST_CASE("loadout: JSON string arrays") {
    CHECK(fr::JsonStringArray(R"(["a", "b\"c"])") == std::vector<std::string> {"a", "b\"c"});
    CHECK(fr::JsonStringArray("[]").empty());
    CHECK(fr::JsonStringArray("[1,2]").empty());
    CHECK(fr::JsonStringArray("\"a\"").empty());
}

TEST_CASE("loadout: without briefcaseItems the random fill never draws a briefcase item") {
    auto owned = Wall();
    owned.push_back(Item("Shashka A33", "assaultrifle", 1, {"assaultrifle"}, 3));
    owned.push_back(Item("Hackl Leviathan", "sniperrifle", 1, {"sniperrifle"}, 1));

    bool sawFillBriefcase = false;
    for (int32_t startCount = 0; startCount < 40; ++startCount) {
        auto input = Input(owned, 30);
        input.startCount = startCount;
        input.briefcaseItems = false;
        const auto plan = fr::PlanLoadout(input);
        CHECK(Briefcases(input, plan) == 0);
        CHECK(std::any_of(plan.notes.begin(), plan.notes.end(), [](const std::string& note) {
            return note.find("briefcase items are not drawn") != std::string::npos;
        }));

        input.briefcaseItems = true;
        for (const auto& pick : fr::PlanLoadout(input).picks)
            sawFillBriefcase |= pick.rule == fr::LoadoutRule::Fill && Picked(input, pick).briefcase;
    }
    CHECK(sawFillBriefcase); // the option is what keeps them out
}

TEST_CASE("loadout: without briefcaseItems a need only briefcase items meet is skipped, its slots go to the fill") {
    // Epic Sniper Kill: the only epic sniper rifle (Jaeger 7 Covert) is a
    // briefcase item; Sniper Rifle client requirement likewise.
    for (int32_t startCount = 0; startCount < 20; ++startCount) {
        auto input = Input(Wall(), 30);
        input.startCount = startCount;
        input.briefcaseItems = false;
        input.prestige = 103;
        input.clientReqs = {12};
        const auto plan = fr::PlanLoadout(input);

        CHECK(Briefcases(input, plan) == 0);
        for (const auto& pick : plan.picks) {
            CHECK(pick.rule != fr::LoadoutRule::Prestige);
            CHECK(pick.rule != fr::LoadoutRule::ClientReq);
        }
        const auto noted = [&](const std::string& prefix) {
            return std::any_of(plan.notes.begin(), plan.notes.end(), [&](const std::string& note) {
                return note.starts_with(prefix) &&
                       note.find("needs a briefcase item; none delivered") != std::string::npos;
            });
        };
        CHECK(noted("prestige 103 Sniper Kill - Epic"));
        CHECK(noted("client requirement"));

        // The slots stay free for the random fill: everything else that fits comes.
        int32_t nonBriefcase = 0;
        for (const auto& item : input.owned)
            nonBriefcase += item.briefcase ? 0 : fr::SlotCost(item);
        CHECK(plan.usedAfter == nonBriefcase);
    }

    // With briefcase items allowed the same need brings the sniper rifle.
    auto input = Input(Wall(), 30);
    input.prestige = 103;
    const auto plan = fr::PlanLoadout(input);
    CHECK(std::any_of(plan.picks.begin(), plan.picks.end(), [&](const fr::LoadoutPick& pick) {
        return pick.rule == fr::LoadoutRule::Prestige && Picked(input, pick).name == "Jaeger 7 Covert";
    }));
}

TEST_CASE("loadout: without briefcaseItems a need met by a mix picks a non-briefcase item") {
    auto owned = Wall();
    // A small epic sniper rifle (no briefcase), beside the large Jaeger 7 Covert.
    auto small = Item("Sieger 300 Ghost Compact", "sniperrifle", 2, {"sniperrifle"}, 4);
    small.briefcase = false;
    owned.push_back(small);
    // A large SMG beside the small DAK X2 Covert.
    auto largeSmg = Item("TAC SMG", "smg", 2, {"smg"}, 3);
    largeSmg.briefcase = true;
    owned.push_back(largeSmg);

    for (int32_t startCount = 0; startCount < 40; ++startCount) {
        auto input = Input(owned, 8);
        input.startCount = startCount;
        input.briefcaseItems = false;
        input.prestige = 103;    // Epic Sniper Kill
        input.clientReqs = {14}; // SMG
        const auto plan = fr::PlanLoadout(input);

        CHECK(Briefcases(input, plan) == 0);
        REQUIRE(plan.picks.size() >= 2);
        CHECK(plan.picks[0].rule == fr::LoadoutRule::Prestige);
        CHECK(Picked(input, plan.picks[0]).name == "Sieger 300 Ghost Compact");
        CHECK(plan.picks[0].why.find("1 briefcase items skipped") != std::string::npos);
        CHECK(plan.picks[1].rule == fr::LoadoutRule::ClientReq);
        CHECK(Picked(input, plan.picks[1]).name == "DAK X2 Covert");
    }
}

TEST_CASE("loadout: with briefcaseItems Epic Sniper Kill brings a large sniper rifle, at most one briefcase item") {
    // random_loadout_holsterable_only = false (the default): the objective needs are met
    // by briefcase items again, but never more than one briefcase in the loadout.
    for (int32_t startCount = 0; startCount < 40; ++startCount) {
        auto input = Input(Wall(), 30);
        input.startCount = startCount;
        input.briefcaseItems = true;
        input.prestige = 103;        // Epic Sniper Kill
        input.clientReqs = {13, 25}; // Shotgun, Assault Rifle: both large in Wall()
        const auto plan = fr::PlanLoadout(input);

        CHECK(Briefcases(input, plan) == 1);
        REQUIRE(!plan.picks.empty());
        const auto prestige = std::find_if(plan.picks.begin(), plan.picks.end(), [](const fr::LoadoutPick& pick) {
            return pick.rule == fr::LoadoutRule::Prestige;
        });
        REQUIRE(prestige != plan.picks.end());
        CHECK(Picked(input, *prestige).name == "Jaeger 7 Covert");
        CHECK(Picked(input, *prestige).briefcase);
        // The shotgun and assault rifle needs find no room for a second briefcase.
        CHECK(std::count_if(plan.notes.begin(), plan.notes.end(), [](const std::string& note) {
                  return note.find("a briefcase item is already in the loadout") != std::string::npos;
              }) == 2);
        CHECK(plan.briefcasesAfter == 1);
    }

    // A packed briefcase item: none is added (vanilla carries one briefcase).
    auto owned = Wall();
    auto packed = Item("Sieger 300 Summer", "sniperrifle", 2, {"sniperrifle"}, 1, true);
    auto input = Input(owned, 30, {packed});
    input.prestige = 103;
    const auto plan = fr::PlanLoadout(input);
    CHECK(Briefcases(input, plan) == 1);
    CHECK(plan.briefcasesBefore == 1);
    CHECK(plan.briefcasesAfter == 1);
}

TEST_CASE("loadout: the ICA Briefcase id") {
    const auto id = fr::ParseGuid(fr::kIcaBriefcaseId);
    REQUIRE(id);
    CHECK(fr::FormatGuid(*id) == "15e6200e-2000-4966-9771-923ab13c6a1c");
}

TEST_CASE("loadout: briefcase delivery verdicts") {
    using fr::BriefcaseVerdict;
    fr::BriefcaseProgress p;
    p.itemSpawned = true;
    p.setItemSent = true;
    p.seconds = 1;
    CHECK(fr::JudgeBriefcase(p) == BriefcaseVerdict::Pending);

    // The game's purchase path: pocket fails, a briefcase spawns, the item is placed.
    p.pocketFailed = true;
    p.briefcaseSpawned = true;
    p.containerSet = true;
    p.placeSent = true;
    p.secondsSincePlace = 0.5;
    CHECK(fr::JudgeBriefcase(p) == BriefcaseVerdict::Pending); // settling
    p.secondsSincePlace = fr::kBriefcaseSettleSeconds;
    CHECK(fr::JudgeBriefcase(p) == BriefcaseVerdict::InBriefcase);
    p.briefcasePickedUp = true;
    CHECK(fr::JudgeBriefcase(p) == BriefcaseVerdict::InBriefcase);

    // A pickup of the item right after the place, owner unknown: never trusted.
    p.itemPickedUp = true;
    p.itemPickedUpAfterPlace = true;
    CHECK(fr::JudgeBriefcase(p) == BriefcaseVerdict::Loose);
    // ... unless the owner read shows the briefcase holds it.
    p.ownerIsBriefcase = true;
    p.ownerIsHero = false;
    CHECK(fr::JudgeBriefcase(p) == BriefcaseVerdict::InBriefcase);

    // The owner read decides as soon as it says 47.
    fr::BriefcaseProgress hero;
    hero.itemSpawned = hero.setItemSent = true;
    hero.ownerIsHero = true;
    CHECK(fr::JudgeBriefcase(hero) == BriefcaseVerdict::Loose);

    // Once placed, 47 as the owner (he owns the briefcase) does not decide.
    fr::BriefcaseProgress placed;
    placed.itemSpawned = placed.setItemSent = placed.briefcaseSpawned = placed.placeSent = true;
    placed.ownerIsHero = true;
    placed.secondsSincePlace = fr::kBriefcaseSettleSeconds;
    CHECK(fr::JudgeBriefcase(placed) == BriefcaseVerdict::InBriefcase);

    // Picked up loose before any briefcase (the item could be pocketed).
    fr::BriefcaseProgress pocketed;
    pocketed.itemSpawned = pocketed.setItemSent = true;
    pocketed.itemPickedUp = true;
    CHECK(fr::JudgeBriefcase(pocketed) == BriefcaseVerdict::Loose);

    // Nothing happens: failed at the timeout.
    fr::BriefcaseProgress stuck;
    stuck.itemSpawned = stuck.setItemSent = true;
    stuck.seconds = fr::kBriefcaseTimeoutSeconds - 0.1;
    CHECK(fr::JudgeBriefcase(stuck) == BriefcaseVerdict::Pending);
    stuck.seconds = fr::kBriefcaseTimeoutSeconds;
    CHECK(fr::JudgeBriefcase(stuck) == BriefcaseVerdict::Failed);
    // A briefcase came but the item was never placed: failed too.
    stuck.briefcaseSpawned = true;
    stuck.containerSet = true;
    CHECK(fr::JudgeBriefcase(stuck) == BriefcaseVerdict::Failed);

    // Game data: the game placed the item itself; its owner became the
    // briefcase's ZItemStorageEntity, with no place seen: in the briefcase.
    fr::BriefcaseProgress stored;
    stored.itemSpawned = stored.setItemSent = stored.briefcaseSpawned = stored.containerSet = true;
    stored.ownerIsHero = false;
    stored.ownerIsBriefcase = false;
    stored.ownerIsStorage = true;
    stored.seconds = 0.6;
    CHECK(fr::JudgeBriefcase(stored) == BriefcaseVerdict::InBriefcase);
    // ... but not before a briefcase and its container exist.
    stored.containerSet = false;
    CHECK(fr::JudgeBriefcase(stored) == BriefcaseVerdict::Pending);

    const std::string text = fr::BriefcaseProgressText(p);
    CHECK(text.find("place sent yes") != std::string::npos);
    CHECK(text.find("item picked up loose after the place") != std::string::npos);
    CHECK(text.find("owner the briefcase yes") != std::string::npos);
    CHECK(std::string(fr::BriefcaseVerdictName(BriefcaseVerdict::Loose)).starts_with("LOOSE"));
}

TEST_CASE("loadout: the create type choice") {
    CHECK(std::string(fr::CreateItemTypeName(3)) == "carried");
    CHECK(std::string(fr::CreateItemTypeName(9)) == "unknown");

    // The packed items' type, else contract.
    CHECK(fr::ChooseLoadoutItemType(std::nullopt).type == fr::kCreateItemTypeContract);
    CHECK(fr::ChooseLoadoutItemType(3).type == 3);
    CHECK(fr::ChooseLoadoutItemType(0).type == 0);

    CHECK(fr::NeedsDelivery(false, false));
    CHECK(!fr::NeedsDelivery(true, false)); // 47 already has that id: never a second one
    CHECK(!fr::NeedsDelivery(false, true)); // requested once in this scene
}

TEST_CASE("loadout: end-of-mission save verdicts") {
    using fr::SaveVerdict;
    // Delivered, confirmed, removed: carried out, or lost on a failure.
    CHECK(fr::JudgeSave(true, false, true, true) == SaveVerdict::Carried);
    CHECK(fr::JudgeSave(true, std::nullopt, true, std::nullopt) == SaveVerdict::Carried);
    CHECK(fr::JudgeSave(false, false, true, false) == SaveVerdict::Lost);
    CHECK(fr::JudgeSave(false, std::nullopt, true, std::nullopt) == SaveVerdict::Lost);
    // 47 holds it but the save does not have it.
    CHECK(fr::JudgeSave(false, false, true, true) == SaveVerdict::MissingFromSave);
    // Both carried and owned.
    CHECK(fr::JudgeSave(true, true, true, true) == SaveVerdict::Duplicated);
    CHECK(fr::JudgeSave(true, true, false, std::nullopt) == SaveVerdict::Duplicated);
    // Never confirmed: stays owned.
    CHECK(fr::JudgeSave(false, true, false, false) == SaveVerdict::StaysOwned);
    CHECK(fr::JudgeSave(false, std::nullopt, false, false) == SaveVerdict::NotDelivered);
    CHECK(fr::JudgeSave(false, false, false, false) == SaveVerdict::Vanished);
    CHECK(fr::JudgeSave(false, true, true, false) == SaveVerdict::RemovalLost);
    CHECK(fr::JudgeSave(std::nullopt, false, true, true) == SaveVerdict::Partial);
    CHECK(std::string(fr::SaveVerdictText(SaveVerdict::Duplicated)).starts_with("DUPLICATED"));
}

namespace {

std::vector<std::string> FillCategories(const fr::LoadoutInput& input, const fr::LoadoutPlan& plan) {
    std::vector<std::string> categories;
    for (const auto& pick : plan.picks) {
        if (pick.rule == fr::LoadoutRule::Fill)
            categories.push_back(fr::LoadoutCategory(Picked(input, pick)));
    }
    return categories;
}

} // namespace

TEST_CASE("loadout: categories are the lower-case icon, sniper and sniperrifle alike") {
    CHECK(fr::LoadoutCategory(Item("A", "Pistol", 1)) == "pistol");
    CHECK(fr::LoadoutCategory(Item("B", "sniper", 1)) == "sniperrifle");
    CHECK(fr::LoadoutCategory(Item("C", "sniperrifle", 1)) == "sniperrifle");
    CHECK(fr::LoadoutCategory(Item("D", "", 1)).empty());
}

TEST_CASE("loadout: the random fill avoids a second item of a category while others fit") {
    // Three pistols, three melee weapons, a poison, an SMG; 4 slots: four
    // categories to fill them from.
    std::vector<fr::LoadoutItem> owned {
        Item("Pistol A", "pistol", 1, {"pistol"}),      Item("Pistol B", "pistol", 1, {"pistol"}),
        Item("Pistol C", "pistol", 1, {"pistol"}),      Item("Knife", "melee", 1, {"melee_lethal"}),
        Item("Shuriken", "melee", 1, {"melee_lethal"}), Item("Axe", "melee", 1, {"melee_lethal"}),
        Item("Poison", "poison", 1, {"poison"}),        Item("SMG", "smg", 1, {"smg"}),
    };

    for (int32_t step = 0; step < 60; ++step) {
        auto input = Input(owned, 4);
        input.startCount = step;
        const auto plan = fr::PlanLoadout(input);
        const auto categories = FillCategories(input, plan);
        CHECK(categories.size() == 4);
        const std::set<std::string> distinct(categories.begin(), categories.end());
        CHECK(distinct.size() == categories.size());
    }
}

TEST_CASE("loadout: a packed pistol keeps the random fill off pistols") {
    // Game data, a mission: ICA 19 DTI packed, the fill drew two more pistols.
    std::vector<fr::LoadoutItem> owned {
        Item("ICA 19 HomeMade", "pistol", 2, {"pistol"}), Item("HWK 21", "pistol", 3, {"pistol"}),
        Item("Shuriken", "melee", 1, {"melee_lethal"}),   Item("Coin", "distraction", 1, {"distraction"}),
        Item("Emetic Pills", "poison", 1, {"poison"}),    Item("Duck", "explosives", 2, {"explosive"}),
    };
    std::vector<fr::LoadoutItem> carried {Item("ICA 19 DTI", "pistol", 1, {"pistol"}, 4, true)};

    for (int32_t step = 0; step < 60; ++step) {
        auto input = Input(owned, 6, carried); // 5 free: the four other categories fit exactly
        input.startCount = step;
        const auto plan = fr::PlanLoadout(input);
        for (const auto& category : FillCategories(input, plan))
            CHECK(category != "pistol");
        CHECK(plan.usedAfter == 6);
    }
}

TEST_CASE("loadout: objectives keep their category even when it repeats; the fill avoids it until last") {
    std::vector<fr::LoadoutItem> owned {
        Item("Epic Unsilenced", "pistol", 1, {"pistol"}, 3, false),
        Item("Silenced Pistol", "pistol", 1, {"pistol"}, 1, true),
        Item("Other Pistol", "pistol", 1, {"pistol"}),
        Item("Knife", "melee", 1, {"melee_lethal"}),
        Item("Coin", "distraction", 1, {"distraction"}),
    };
    // A packed (common, unsilenced) pistol, the prestige's epic unsilenced
    // pistol and the client requirement's silenced pistol: three pistols.
    auto input = Input(owned, 6, {Item("Packed Pistol", "pistol", 1, {"pistol"}, 1, false)});
    input.prestige = 107;    // Epic Unsilenced Pistol Kill
    input.clientReqs = {15}; // a silenced pistol
    const auto plan = fr::PlanLoadout(input);

    const auto names = PickedNames(input, plan);
    CHECK(std::count(names.begin(), names.end(), "Epic Unsilenced") == 1);
    CHECK(std::count(names.begin(), names.end(), "Silenced Pistol") == 1);
    // 1 packed + 2 required + knife + coin = 5 of 6; the last slot goes to the
    // one item left, a pistol, rather than stay empty.
    const auto fill = FillCategories(input, plan);
    REQUIRE(fill.size() == 3);
    CHECK(fill[0] != "pistol");
    CHECK(fill[1] != "pistol");
    CHECK(fill[2] == "pistol");
    CHECK(plan.usedAfter == 6);
}

TEST_CASE("loadout: a no-firearms objective keeps the fill off firearms; a required firearm still comes") {
    // Game data, a Dubai mission: Silent Assassin, No Firearms, and the fill
    // drew a shotgun, a pistol and an SMG.
    std::vector<fr::LoadoutItem> owned {
        Item("Shotgun", "shotgun", 2, {"shotgun"}),
        Item("Pistol", "pistol", 1, {"pistol"}),
        Item("SMG", "smg", 1, {"smg"}),
        Item("Knife", "melee", 1, {"melee_lethal"}),
        Item("Coin", "distraction", 1, {"distraction"}),
        Item("Poison", "poison", 1, {"poison"}),
        Item("Epic Shotgun", "shotgun", 2, {"shotgun"}, 3),
    };

    for (const auto& [prestige, reqs] : std::vector<std::pair<int32_t, std::vector<int32_t>>> {{116, {}}, {0, {72}}}) {
        for (int32_t step = 0; step < 30; ++step) {
            auto input = Input(owned, 8);
            input.prestige = prestige;
            input.clientReqs = reqs;
            input.startCount = step;
            const auto plan = fr::PlanLoadout(input);
            for (const auto& pick : plan.picks)
                CHECK_FALSE(fr::IsFirearmItem(input.owned[pick.owned]));
            CHECK(plan.usedAfter == 3); // knife, coin, poison: firearm slots stay empty
        }
    }

    // Contradicting objectives: a shotgun kill too. With common objective 72
    // the player gets the shotgun and chooses which objective to do; with
    // prestige 116 no firearm comes at all (the shotgun need is skipped).
    for (const int32_t prestige : {0, 116}) {
        auto input = Input(owned, 8);
        input.prestige = prestige;
        input.clientReqs = {72, 13}; // No Firearms, Shotgun Kill
        const auto plan = fr::PlanLoadout(input);
        size_t firearms = 0;
        for (const auto& pick : plan.picks)
            firearms += fr::IsFirearmItem(input.owned[pick.owned]) ? 1 : 0;
        CHECK(firearms == (prestige == 116 ? 0u : 1u));
    }
}

TEST_CASE("loadout: when one category is left, the fill takes duplicates rather than leave slots empty") {
    std::vector<fr::LoadoutItem> owned {
        Item("Knife", "melee", 1, {"melee_lethal"}),
        Item("Shuriken", "melee", 1, {"melee_lethal"}),
        Item("Axe", "melee", 1, {"melee_lethal"}),
    };
    const auto input = Input(owned, 3, {Item("Packed Bat", "melee", 1, {"melee_nonlethal"})});
    const auto plan = fr::PlanLoadout(input);
    CHECK(plan.picks.size() == 2);
    CHECK(plan.usedAfter == 3);
    for (const auto& pick : plan.picks)
        CHECK(pick.why.find("already in the loadout") != std::string::npos);
}

TEST_CASE("loadout: the varied fill stays deterministic and keeps the one-briefcase limit") {
    auto owned = Wall();
    owned.push_back(Item("Shashka A33", "assaultrifle", 1, {"assaultrifle"}, 3));
    owned.push_back(Item("Hackl Leviathan", "sniperrifle", 1, {"sniperrifle"}, 1));
    for (int32_t step = 0; step < 40; ++step) {
        auto input = Input(owned, 30);
        input.startCount = step;
        const auto a = fr::PlanLoadout(input);
        CHECK(PickedNames(input, a) == PickedNames(input, fr::PlanLoadout(input)));
        CHECK(Briefcases(input, a) <= 1);
    }
}

namespace {

fr::LoadoutItem Sized(fr::LoadoutItem item, std::string itemSize, std::string holster, bool useItemSize = true) {
    item.itemSize = std::move(itemSize);
    item.holster = std::move(holster);
    item.briefcase = fr::IsBriefcaseItem(item, useItemSize);
    return item;
}

} // namespace

TEST_CASE("loadout: the briefcase rule: HolsterAbility decides; ItemSize only without one") {
    // Game data: the DAK SMG DTI Black Covert is not
    // concealable in vanilla, yet not ITEMSIZE_LARGE; given loose it showed on
    // 47's back. Its HolsterAbility decides.
    const auto dak = Item("(SMG) DAK SMG DTI Black COVERT", "smg", 2, {"smg"}, 3, true);
    CHECK(Sized(dak, "ITEMSIZE_MEDIUM", "eCanNotBeHolstered").briefcase);
    CHECK(Sized(dak, "ITEMSIZE_MEDIUM", "eHolsterTemporarilyOnly").briefcase);
    // A concealable SMG (most are) and shotgun (a couple are): not.
    CHECK_FALSE(Sized(dak, "ITEMSIZE_MEDIUM", "eHolsteringAllowed").briefcase);
    CHECK_FALSE(Sized(Item("Sawed-off", "shotgun", 2, {"shotgun"}), "ITEMSIZE_MEDIUM", "eHolsteringAllowed").briefcase);

    // ItemSize large with a HolsterAbility that allows holstering: pocketable. Ground truth
    // Game data: the Sawed Off Bartoli 12G is ITEMSIZE_LARGE and eHolsteringAllowed,
    // and 47 holsters it in game.
    const auto sawedOff = Item("(Shotgun) Sawed Off Bartoli 12G", "shotgun", 3, {"shotgun"}, 2);
    CHECK_FALSE(Sized(sawedOff, "ITEMSIZE_LARGE", "eHolsteringAllowed").briefcase);
    CHECK_FALSE(Sized(sawedOff, "ITEMSIZE_LARGE", "eHolsteringAllowed", false).briefcase);
    const auto sniper = Item("Sniper Medium Archetype (Classic)", "sniperrifle", 3, {"sniperrifle"}, 2, true);
    CHECK(Sized(sniper, "ITEMSIZE_LARGE", "eHolsterTemporarilyOnly").briefcase);
    CHECK_FALSE(Sized(sniper, "ITEMSIZE_LARGE", "eHolsteringAllowed").briefcase);

    // No HolsterAbility: ItemSize alone.
    CHECK(Sized(Item("TAC SMG", "smg", 2, {"smg"}), "ITEMSIZE_LARGE", "").briefcase);
    CHECK_FALSE(Sized(dak, "ITEMSIZE_MEDIUM", "").briefcase);
    // Every HolsterAbility but eHolsteringAllowed makes a briefcase item.
    CHECK(Sized(dak, "ITEMSIZE_MEDIUM", "eUndecided").briefcase);
    CHECK(Sized(dak, "ITEMSIZE_SMALL", "eHolsterSecondaryOnly").briefcase);

    // Pistols and dartguns: pocketable.
    const auto pistol = Item("ICA19 Shortballer", "pistol", 1, {"pistol"}, 3);
    CHECK_FALSE(Sized(pistol, "ITEMSIZE_MEDIUM", "eHolsteringAllowed").briefcase);
    CHECK_FALSE(
        Sized(Item("(Dartgun) Sick", "pistol", 3, {"pistol"}, 2), "ITEMSIZE_SMALL", "eHolsteringAllowed").briefcase);

    // A large melee item: by its HolsterAbility too; and the ICA Briefcase itself.
    CHECK(
        Sized(Item("Baseball bat", "melee", 2, {"melee_nonlethal"}), "ITEMSIZE_LARGE", "eCanNotBeHolstered").briefcase);
    CHECK(Sized(Item("Katana", "melee", 2, {"melee_lethal"}), "ITEMSIZE_LARGE", "").briefcase);
    // Game data (pro.repo): the Pomada Boombox is ITEMSIZE_LARGE and
    // eHolsterSecondaryOnly, and 47 can't holster it; the Crowbar Professional is ITEMSIZE_LARGE
    // and eHolsteringAllowed, and fits in his pocket.
    CHECK(Sized(Item("(Pomada) Boombox Distraction", "distraction", 2, {"distraction"}, 2), "ITEMSIZE_LARGE",
                "eHolsterSecondaryOnly")
              .briefcase);
    CHECK_FALSE(
        Sized(Item("(Tool) Crowbar Professional", "tool", 2, {}, 2), "ITEMSIZE_LARGE", "eHolsteringAllowed").briefcase);
    CHECK_FALSE(
        Sized(Item("Kitchen Knife", "melee", 1, {"melee_lethal"}), "ITEMSIZE_SMALL", "eHolsteringAllowed").briefcase);
    CHECK(Sized(Item("ICA Briefcase", "", 0, {"case"}), "ITEMSIZE_SMALL", "").briefcase);

    // No ItemSize key on any item: HolsterAbility, else the icon guess.
    CHECK(Sized(dak, "", "eCanNotBeHolstered", false).briefcase);
    CHECK_FALSE(Sized(dak, "", "", false).briefcase);
    CHECK(Sized(sniper, "", "", false).briefcase);
    CHECK(fr::IsLargeIconGuess("SniperRifle"));
    CHECK_FALSE(fr::IsLargeIconGuess("smg"));

    CHECK(fr::IsHandsOnlyHolster("eHolsterTemporarilyOnly"));
    CHECK(fr::IsHandsOnlyHolster("eCanNotBeHolstered"));
    CHECK(fr::IsHandsOnlyHolster("eHolsterSecondaryOnly"));
    CHECK(fr::IsHandsOnlyHolster("eUndecided"));
    CHECK_FALSE(fr::IsHandsOnlyHolster("eHolsteringAllowed"));
    CHECK_FALSE(fr::IsHandsOnlyHolster(""));
}

TEST_CASE("loadout: the pocket path takes the large holsterable items, never a briefcase item") {
    // Game data: CreateItem gives every Sawed Off
    // Bartoli 12G (ITEMSIZE_LARGE, eHolsteringAllowed) into 47's hands.
    const auto sawedOff = Item("(Shotgun) Sawed Off Bartoli 12G MakeshiftScrap", "shotgun", 2, {"shotgun"}, 3);
    CHECK(fr::IsPocketDeliveryItem(Sized(sawedOff, "ITEMSIZE_LARGE", "eHolsteringAllowed")));
    CHECK(fr::IsPocketDeliveryItem(
        Sized(Item("(Tool) Crowbar Professional", "tool", 2, {}, 2), "ITEMSIZE_LARGE", "eHolsteringAllowed")));
    // The pistols and SMGs CreateItem gives arrive in the pockets.
    CHECK_FALSE(fr::IsPocketDeliveryItem(
        Sized(Item("ICA19", "pistol", 1, {"pistol"}), "ITEMSIZE_MEDIUM", "eHolsteringAllowed")));
    CHECK_FALSE(
        fr::IsPocketDeliveryItem(Sized(Item("DAK X2", "smg", 2, {"smg"}), "ITEMSIZE_SMALL", "eHolsteringAllowed")));
    // Briefcase items keep the briefcase path; no HolsterAbility is not holsterable here.
    CHECK_FALSE(fr::IsPocketDeliveryItem(Sized(sawedOff, "ITEMSIZE_LARGE", "eHolsterTemporarilyOnly")));
    CHECK_FALSE(fr::IsPocketDeliveryItem(Sized(sawedOff, "ITEMSIZE_LARGE", "")));
    auto packedCase = Sized(sawedOff, "ITEMSIZE_LARGE", "eHolsteringAllowed");
    packedCase.briefcase = true;
    CHECK_FALSE(fr::IsPocketDeliveryItem(packedCase));
}

TEST_CASE("loadout: HoldingIllegalWeapon events") {
    // Game data: a HoldingIllegalWeapon event.
    const std::string held =
        R"({"Timestamp":2.269424,"Name":"HoldingIllegalWeapon","ContractSessionId":"<redacted>","Value":)"
        R"({"IsHoldingIllegalWeapon":true,"WeaponEquipped":{"IsPerceivedAsWeapon":true,"InstanceId":"",)"
        R"("ItemType":"Shotgun_M500","ItemName":"The Makeshift Scrap Shotgun",)"
        R"("RepositoryId":"9fcf5400-0784-4e71-ad57-a3e17cf88bc3","OnlineTraits":["shotgun"]}}})";
    const auto parsed = fr::ParseHoldingIllegalWeapon(held);
    REQUIRE(parsed);
    CHECK(parsed->holding);
    REQUIRE(parsed->id);
    CHECK(fr::FormatGuid(*parsed->id) == "9fcf5400-0784-4e71-ad57-a3e17cf88bc3");

    const auto empty = fr::ParseHoldingIllegalWeapon(
        R"({"Timestamp":2.514308,"Name":"HoldingIllegalWeapon","Value":{"IsHoldingIllegalWeapon":false}})");
    REQUIRE(empty);
    CHECK_FALSE(empty->holding);
    CHECK_FALSE(empty->id);

    CHECK_FALSE(fr::ParseHoldingIllegalWeapon(R"({"Name":"ItemPickedUp","Value":{"RepositoryId":"x"}})"));
    CHECK_FALSE(fr::ParseHoldingIllegalWeapon(R"({"Value":{"IsHoldingIllegalWeapon":1}})"));
}

TEST_CASE("loadout: pocket delivery verdicts") {
    using fr::PocketVerdict;
    fr::PocketProgress p;
    p.setItemSent = true;
    p.seconds = 0.1;
    CHECK(fr::JudgePocket(p) == PocketVerdict::Pending);

    // 47 got it and no HoldingIllegalWeapon named it: pocketed after the settle.
    p.itemPickedUp = true;
    p.secondsSinceHeld = 0.5;
    CHECK(fr::JudgePocket(p) == PocketVerdict::Pending);
    p.secondsSinceHeld = fr::kPocketSettleSeconds;
    CHECK(fr::JudgePocket(p) == PocketVerdict::Pocketed);
    fr::PocketProgress owned;
    owned.setItemSent = true;
    owned.ownerIsHero = true;
    owned.secondsSinceHeld = fr::kPocketSettleSeconds;
    CHECK(fr::JudgePocket(owned) == PocketVerdict::Pocketed);
    owned.ownerIsHero = false;
    CHECK(fr::JudgePocket(owned) == PocketVerdict::Pending);

    // In the hands (what CreateItem did): one retry, then wrong.
    fr::PocketProgress hands;
    hands.setItemSent = hands.itemPickedUp = true;
    hands.inHands = hands.everInHands = true;
    hands.secondsSinceHeld = 5;
    CHECK(fr::JudgePocket(hands) == PocketVerdict::Retry);
    hands.retrySent = true;
    hands.secondsSinceRetry = 0.5;
    CHECK(fr::JudgePocket(hands) == PocketVerdict::Pending);
    hands.secondsSinceRetry = fr::kPocketSettleSeconds;
    CHECK(fr::JudgePocket(hands) == PocketVerdict::InHands);
    // The retry cleared the hands (HoldingIllegalWeapon false): pocketed after the settle.
    hands.inHands = false;
    hands.secondsSinceHeld = 0.2;
    CHECK(fr::JudgePocket(hands) == PocketVerdict::Pending);
    hands.secondsSinceHeld = fr::kPocketSettleSeconds;
    CHECK(fr::JudgePocket(hands) == PocketVerdict::Pocketed);

    // OnFailed with nothing given: refused; with the item given, it does not decide.
    fr::PocketProgress refused;
    refused.setItemSent = refused.pocketFailed = true;
    CHECK(fr::JudgePocket(refused) == PocketVerdict::Refused);
    refused.itemPickedUp = true;
    CHECK(fr::JudgePocket(refused) == PocketVerdict::Pending);

    // Nothing at all: failed at the timeout.
    fr::PocketProgress stuck;
    stuck.setItemSent = true;
    stuck.seconds = fr::kPocketTimeoutSeconds - 0.1;
    CHECK(fr::JudgePocket(stuck) == PocketVerdict::Pending);
    stuck.seconds = fr::kPocketTimeoutSeconds;
    CHECK(fr::JudgePocket(stuck) == PocketVerdict::Failed);

    const std::string text = fr::PocketProgressText(hands);
    CHECK(text.find("SetItem sent yes") != std::string::npos);
    CHECK(text.find("in 47's hands no (it was)") != std::string::npos);
    CHECK(text.find("pocket call sent again yes") != std::string::npos);
    CHECK(std::string(fr::PocketVerdictName(PocketVerdict::InHands)).starts_with("WRONG"));
}

TEST_CASE("loadout: the sniper rifle and the DAK SMG are never both brought") {
    // A sniper rifle (ITEMSIZE_LARGE) and the DAK SMG (ITEMSIZE_MEDIUM, not
    // pocketable): both were drawn and the SMG given loose. Now at most one.
    std::vector<fr::LoadoutItem> owned {
        Sized(Item("Sniper Medium Archetype (Classic)", "sniperrifle", 3, {"sniperrifle"}, 2, true), "ITEMSIZE_LARGE",
              "eHolsterTemporarilyOnly"),
        Sized(Item("(SMG) DAK SMG DTI Black COVERT", "smg", 2, {"smg"}, 3, true), "ITEMSIZE_MEDIUM",
              "eCanNotBeHolstered"),
        Sized(Item("(Melee) Ice Pick", "melee", 1, {"melee_lethal"}, 2), "ITEMSIZE_SMALL", "eHolsteringAllowed"),
        Sized(Item("Micro Proximity Explosive", "explosives", 1, {"explosive", "explosive_device"}, 2),
              "ITEMSIZE_SMALL", "eHolsteringAllowed"),
        Sized(Item("ICA19 Shortballer", "pistol", 1, {"pistol"}, 3), "ITEMSIZE_SMALL", "eHolsteringAllowed"),
    };

    bool sawSniper = false;
    bool sawSmg = false;

    for (int32_t startCount = 0; startCount < 60; ++startCount) {
        auto input = Input(owned, 30);
        input.startCount = startCount;
        input.briefcaseItems = true;
        input.lockpickFirst = false;
        const auto plan = fr::PlanLoadout(input);
        const auto names = PickedNames(input, plan);
        const bool sniper = std::count(names.begin(), names.end(), "Sniper Medium Archetype (Classic)") > 0;
        const bool smg = std::count(names.begin(), names.end(), "(SMG) DAK SMG DTI Black COVERT") > 0;

        CHECK_FALSE((sniper && smg));
        CHECK(Briefcases(input, plan) <= 1);
        CHECK(plan.briefcasesAfter <= 1);
        sawSniper |= sniper;
        sawSmg |= smg;
    }

    CHECK(sawSniper);
    CHECK(sawSmg);

    // A pocketable SMG (eHolsteringAllowed, not large) may come beside the sniper rifle.
    owned[1] = Sized(owned[1], "ITEMSIZE_MEDIUM", "eHolsteringAllowed");
    auto input = Input(owned, 30);
    input.lockpickFirst = false;
    const auto names = PickedNames(input, fr::PlanLoadout(input));
    CHECK(std::count(names.begin(), names.end(), "Sniper Medium Archetype (Classic)") == 1);
    CHECK(std::count(names.begin(), names.end(), "(SMG) DAK SMG DTI Black COVERT") == 1);
}

namespace {

// An item whose briefcase flag comes from fr::IsBriefcaseItem, as in the mod.
fr::LoadoutItem Keyed(std::string name, std::string icon, int32_t capacity, std::vector<std::string> traits,
                      int32_t rarity, std::string itemSize, std::string holster, bool silenced = false) {
    fr::LoadoutItem item = Item(std::move(name), std::move(icon), capacity, std::move(traits), rarity, silenced);
    item.itemSize = std::move(itemSize);
    item.holster = std::move(holster);
    item.briefcase = fr::IsBriefcaseItem(item, true);
    return item;
}

std::vector<fr::LoadoutItem> KeyedWall() {
    return {
        Keyed("Kitchen Knife", "melee", 1, {"melee_lethal"}, 1, "ITEMSIZE_SMALL", "eHolsteringAllowed"),
        Keyed("Katana", "melee", 2, {"melee_lethal"}, 3, "ITEMSIZE_LARGE", "eCanNotBeHolstered"),
        Keyed("Jaeger 7 Covert", "sniperrifle", 4, {"sniperrifle"}, 3, "ITEMSIZE_LARGE", "eHolsterTemporarilyOnly",
              true),
        Keyed("Sieger 300 Ghost Compact", "sniperrifle", 2, {"sniperrifle"}, 4, "ITEMSIZE_SMALL", "eHolsteringAllowed"),
        Keyed("Fusil G2", "assaultrifle", 4, {"assaultrifle"}, 2, "ITEMSIZE_LARGE", "eHolsterTemporarilyOnly"),
        Keyed("Tactical Bartoli 12G", "shotgun", 3, {"shotgun"}, 3, "ITEMSIZE_LARGE", "eHolsterTemporarilyOnly"),
        Keyed("Sawed Off Bartoli", "shotgun", 1, {"shotgun"}, 4, "ITEMSIZE_SMALL", "eHolsteringAllowed"),
        Keyed("DAK SMG DTI Black Covert", "smg", 3, {"smg"}, 3, "ITEMSIZE_MEDIUM", "eHolsterTemporarilyOnly", true),
        Keyed("DAK X2 Covert", "smg", 3, {"smg"}, 1, "ITEMSIZE_SMALL", "eHolsteringAllowed", true),
        Keyed("ICA19 Shortballer", "pistol", 1, {"pistol"}, 3, "ITEMSIZE_SMALL", "eHolsteringAllowed"),
        Keyed("Hackl 9S", "pistol", 2, {"pistol"}, 2, "ITEMSIZE_SMALL", "eHolsteringAllowed", true),
        Keyed("Lethal Poison Vial", "poison", 1, {"poison", "consumable_poison"}, 1, "ITEMSIZE_SMALL", "eUndecided"),
        Keyed("Fiber Wire", "fiberwire", 1, {"fiberwire"}, 1, "ITEMSIZE_SMALL", "eHolsteringAllowed"),
        Keyed("Crowbar", "melee", 2, {"melee_nonlethal"}, 1, "ITEMSIZE_LARGE", "eHolsteringAllowed"),
        Keyed("Remote Explosive Duck", "explosives", 2, {"explosive", "explosive_device"}, 2, "ITEMSIZE_SMALL",
              "eHolsteringAllowed"),
    };
}

} // namespace

TEST_CASE("loadout: without briefcaseItems no path ever plans an item IsBriefcaseItem calls a briefcase item") {
    auto owned = KeyedWall();
    owned.push_back(Mk3());
    const std::vector<int32_t> prestiges {0, 102, 103, 104, 105, 106, 107, 108, 109, 111, 114, 121, 125};
    const std::vector<std::vector<int32_t>> reqs {{}, {12}, {13}, {14}, {25}, {65, 66}, {62, 63, 64}, {26, 68}};

    size_t plans = 0;
    size_t prestigeHolsterable = 0;
    bool sawBriefcaseWhenOn = false;
    for (int32_t startCount = 0; startCount < 12; ++startCount) {
        for (const int32_t prestige : prestiges) {
            for (const auto& req : reqs) {
                for (const int32_t total : {6, 15, 40}) {
                    auto input = Input(owned, total);
                    input.startCount = startCount;
                    input.prestige = prestige;
                    input.clientReqs = req;
                    input.briefcaseItems = false;
                    const auto plan = fr::PlanLoadout(input);
                    ++plans;
                    CHECK(plan.briefcasesAfter == 0);
                    for (const auto& pick : plan.picks) {
                        const auto& item = Picked(input, pick);
                        CHECK_FALSE(fr::IsBriefcaseItem(item, true));
                        if (pick.rule == fr::LoadoutRule::Prestige)
                            ++prestigeHolsterable;
                    }

                    input.briefcaseItems = true;
                    for (const auto& pick : fr::PlanLoadout(input).picks)
                        sawBriefcaseWhenOn |= fr::IsBriefcaseItem(Picked(input, pick), true);
                }
            }
        }
    }
    CHECK(plans > 1000);
    CHECK(prestigeHolsterable > 0); // prestige needs are still met by holsterable items
    CHECK(sawBriefcaseWhenOn);      // the setting is what keeps them out
}

TEST_CASE("loadout: without briefcaseItems a prestige need picks a holsterable match, or is skipped and noted") {
    // Epic sniper rifle: the compact Sieger 300 (holsterable), never the Jaeger 7 Covert.
    for (int32_t startCount = 0; startCount < 20; ++startCount) {
        auto input = Input(KeyedWall(), 30);
        input.startCount = startCount;
        input.briefcaseItems = false;
        input.prestige = 103;
        const auto plan = fr::PlanLoadout(input);
        REQUIRE_FALSE(plan.picks.empty());
        CHECK(plan.picks[0].rule == fr::LoadoutRule::Prestige);
        CHECK(Picked(input, plan.picks[0]).name == "Sieger 300 Ghost Compact");
    }

    // A katana (prestige 111) is hands-only (ITEMSIZE_LARGE): skipped and noted.
    auto input = Input(KeyedWall(), 30);
    input.briefcaseItems = false;
    input.prestige = 111;
    const auto plan = fr::PlanLoadout(input);
    for (const auto& pick : plan.picks)
        CHECK(pick.rule != fr::LoadoutRule::Prestige);
    CHECK(std::any_of(plan.notes.begin(), plan.notes.end(), [](const std::string& note) {
        return note.starts_with("prestige 111") &&
               note.find("needs a briefcase item; none delivered") != std::string::npos;
    }));
}

TEST_CASE("loadout: a name need ignores ItemType (eCC_BaseballBat is the Mace's too); the bat wins the briefcase") {
    // Game data, Sapienza (prestige 125): the Mace
    // would be picked as "a baseball bat (1 of 8 fitting)" because its ItemType is
    // eCC_BaseballBat, and the random fill then took the briefcase slot with an
    // assault rifle.
    const auto withType = [](fr::LoadoutItem item, std::string type) {
        item.kind = std::move(type);
        return item;
    };
    const std::vector<std::string> bludgeon {"melee_nonlethal", "throw_nonlethal_deprecated"};
    const auto mace = withType(
        Keyed("Mace / Melee_Mace", "melee", 2, bludgeon, 2, "ITEMSIZE_SMALL", "eHolsteringAllowed"), "eCC_BaseballBat");
    const auto bat = withType(
        Keyed("Baseball bat / Melee_BaseballBat", "melee", 4, bludgeon, 1, "ITEMSIZE_LARGE", "eHolsterTemporarilyOnly"),
        "eCC_BaseballBat");
    const auto spooky = withType(Keyed("Baseball bat (spooky) / Melee_BaseballBat_Spooky", "melee", 2, bludgeon, 3,
                                       "ITEMSIZE_LARGE", "eHolsterTemporarilyOnly"),
                                 "eCC_BaseballBat");
    const auto japanese = withType(Keyed("Baseball bat (japanese) / Melee_BaseballBat_Japanese", "melee", 2, bludgeon,
                                         2, "ITEMSIZE_LARGE", "eHolsterTemporarilyOnly"),
                                   "eCC_BaseballBat");
    const auto mallet = withType(Item("Croquet Mallet", "melee", 2, {"melee_nonlethal"}, 2), "eCC_BaseballBat");
    const auto baton = withType(Item("Melee aluminium baton", "melee", 2, {"melee_nonlethal"}, 2), "eCC_BaseballBat");
    const auto sabre = withType(Item("Sabre", "melee", 2, {"melee_lethal"}, 2), "eCC_Katana");
    const auto katana = withType(Item("Katana", "melee", 2, {"melee_lethal"}, 2), "eCC_Katana");
    const auto rifle = Keyed("(IG) AR_Hero_ConcreteArt", "assaultrifle", 3, {"assaultrifle"}, 2, "ITEMSIZE_LARGE",
                             "eHolsterTemporarilyOnly", true);

    const auto matchesAny = [](const std::vector<fr::ItemNeed>& needs, const fr::LoadoutItem& item) {
        return std::any_of(needs.begin(), needs.end(),
                           [&](const fr::ItemNeed& need) { return fr::Matches(need, item); });
    };

    CHECK(matchesAny(fr::PrestigeNeeds(125), bat));
    CHECK(matchesAny(fr::PrestigeNeeds(125), spooky));
    CHECK(matchesAny(fr::PrestigeNeeds(125), japanese));
    CHECK_FALSE(matchesAny(fr::PrestigeNeeds(125), mace));
    CHECK_FALSE(matchesAny(fr::PrestigeNeeds(125), mallet));
    CHECK_FALSE(matchesAny(fr::PrestigeNeeds(125), baton));
    CHECK(matchesAny(fr::PrestigeNeeds(111), katana));
    CHECK_FALSE(matchesAny(fr::PrestigeNeeds(111), sabre));

    for (int32_t startCount = 0; startCount < 20; ++startCount) {
        CAPTURE(startCount);
        const std::vector<fr::LoadoutItem> packed {
            Keyed("(S3) (Dartgun) Kalmer 2 - Tranquilizer", "pistol", 3, {"pistol"}, 2, "ITEMSIZE_SMALL",
                  "eHolsteringAllowed"),
            Keyed("(Dartgun) Sick", "pistol", 1, {"pistol"}, 4, "ITEMSIZE_SMALL", "eHolsteringAllowed")};
        auto input = Input({mace, bat, spooky, japanese, mallet, baton, rifle}, 15, packed);
        input.startCount = startCount;
        input.prestige = 125;
        const auto plan = fr::PlanLoadout(input);

        REQUIRE_FALSE(plan.picks.empty());
        CHECK(plan.picks[0].rule == fr::LoadoutRule::Prestige);
        CHECK(Picked(input, plan.picks[0]).name.starts_with("Baseball bat"));
        CHECK(plan.picks[0].why.find("(1 of 3 fitting") != std::string::npos);
        // The bat holds the one briefcase: the assault rifle stays home.
        for (const auto& pick : plan.picks)
            CHECK(Picked(input, pick).name != rifle.name);
        CHECK(plan.briefcasesAfter == 1);
    }
}

TEST_CASE("loadout: slots and the one briefcase go lockpick, then prestige, then common objectives, then random") {
    const std::vector<std::string> bludgeon {"melee_nonlethal", "throw_nonlethal_deprecated"};
    const auto bat =
        Keyed("Baseball bat / Melee_BaseballBat", "melee", 4, bludgeon, 1, "ITEMSIZE_LARGE", "eHolsterTemporarilyOnly");
    const auto rifle = Keyed("(IG) AR_Hero_ConcreteArt", "assaultrifle", 3, {"assaultrifle"}, 2, "ITEMSIZE_LARGE",
                             "eHolsterTemporarilyOnly", true);
    const auto syringe =
        Keyed("Sedative Syringe", "poison", 1, {"poison", "syringe_poison"}, 1, "ITEMSIZE_SMALL", "eHolsteringAllowed");
    const auto filler = [](std::string name) {
        return Keyed(std::move(name), "distraction", 1, {"distraction"}, 1, "ITEMSIZE_SMALL", "eHolsteringAllowed");
    };

    for (int32_t startCount = 0; startCount < 20; ++startCount) {
        CAPTURE(startCount);
        // 6 slots: the lockpick (1), the bat (4), the poison (1); nothing left for the fill.
        auto input = Input({filler("Coin A"), filler("Coin B"), rifle, syringe, bat, Mk3(), filler("Coin C")}, 6);
        input.startCount = startCount;
        input.lockpickFirst = true;
        input.prestige = 125;
        input.clientReqs = {25, 42}; // assault rifle (a briefcase item), a poison
        const auto plan = fr::PlanLoadout(input);

        REQUIRE(plan.picks.size() == 3);
        CHECK(plan.picks[0].rule == fr::LoadoutRule::Lockpick);
        CHECK(Picked(input, plan.picks[0]).name == "Lockpick Mk III");
        CHECK(plan.picks[1].rule == fr::LoadoutRule::Prestige);
        CHECK(Picked(input, plan.picks[1]).name == bat.name);
        CHECK(plan.picks[2].rule == fr::LoadoutRule::ClientReq);
        CHECK(Picked(input, plan.picks[2]).name == "Sedative Syringe");
        CHECK(plan.usedAfter == 6);

        // One slot fewer: the common objective's poison loses its slot, and the fill takes nothing either.
        input.totalSlots = 5;
        const auto tight = fr::PlanLoadout(input);
        REQUIRE(tight.picks.size() == 2);
        CHECK(tight.picks[0].rule == fr::LoadoutRule::Lockpick);
        CHECK(tight.picks[1].rule == fr::LoadoutRule::Prestige);

        // With slots to spare, the random fill comes only after every objective.
        input.totalSlots = 30;
        const auto roomy = fr::PlanLoadout(input);
        REQUIRE(roomy.picks.size() >= 4);
        CHECK(roomy.picks[0].rule == fr::LoadoutRule::Lockpick);
        CHECK(roomy.picks[1].rule == fr::LoadoutRule::Prestige);
        CHECK(roomy.picks[2].rule == fr::LoadoutRule::ClientReq);
        // The prestige bat holds the briefcase over the common objective's rifle.
        CHECK(std::any_of(roomy.notes.begin(), roomy.notes.end(), [](const std::string& note) {
            return note.starts_with("client requirement 25") &&
                   note.find("a briefcase item is already in the loadout") != std::string::npos;
        }));
        for (size_t i = 3; i < roomy.picks.size(); ++i) {
            CHECK(roomy.picks[i].rule == fr::LoadoutRule::Fill);
            CHECK(Picked(input, roomy.picks[i]).name != rifle.name); // the briefcase is the bat's
        }
    }
}

// --- preferred items, late needs and Silent Assassin, No Firearms -------------
//
// Names, icons, traits and perks from the departure lists and the pro.repo key
// census.

namespace {

std::vector<std::string> RulePicks(const fr::LoadoutInput& input, const fr::LoadoutPlan& plan) {
    std::vector<std::string> names;
    for (const auto& pick : plan.picks) {
        if (pick.rule == fr::LoadoutRule::Prestige || pick.rule == fr::LoadoutRule::ClientReq)
            names.push_back(Picked(input, pick).name);
    }
    return names;
}

bool Noted(const fr::LoadoutPlan& plan, std::string_view part) {
    return std::any_of(plan.notes.begin(), plan.notes.end(),
                       [&](const std::string& note) { return note.find(part) != std::string::npos; });
}

fr::LoadoutItem Pocketable(fr::LoadoutItem item) {
    item.briefcase = false;
    return item;
}

fr::LoadoutItem Perked(fr::LoadoutItem item, std::vector<std::string> perks) {
    item.perks = std::move(perks);
    return item;
}

} // namespace

TEST_CASE("loadout: Collateral Kill - Firearms prefers a shotgun or a piercing firearm, else any firearm") {
    const auto pistol = Item("(Pistol) ICA 19", "pistol", 1, {"pistol"}, 1);
    const auto smg = Item("DAK X2 Covert", "smg", 1, {"smg"}, 1, true);
    const auto shotgun = Pocketable(Item("(Shotgun) Sawed Off Bartoli 12G", "shotgun", 1, {"shotgun"}, 4));
    // pro.repo Perks "[piercing,damage]" (2 items in pro.repo).
    const auto piercingSniper =
        Perked(Item("(Sniper) Druzhina 34 DTI", "sniperrifle", 2, {"sniperrifle"}, 4), {"piercing", "damage"});
    const auto piercingPistol = Perked(Item("Pistol Piercing", "pistol", 1, {"pistol"}, 3), {"piercing", "damage"});
    const auto suppressed = Perked(Item("Silenced Pistol", "pistol", 1, {"pistol"}, 3, true), {"suppressor"});

    const auto needs = fr::PrestigeNeeds(121);
    REQUIRE(needs.size() == 1);
    REQUIRE(!needs[0].prefer.empty());
    for (const auto* item : {&shotgun, &piercingSniper, &piercingPistol})
        CHECK(fr::Matches(needs[0].prefer[0], *item));
    for (const auto* item : {&pistol, &smg, &suppressed})
        CHECK_FALSE(fr::Matches(needs[0].prefer[0], *item));
    CHECK(fr::Matches(needs[0], pistol)); // any firearm still meets it

    std::set<std::string> chosen;
    for (int32_t step = 0; step < 40; ++step) {
        auto input = Input({pistol, smg, shotgun, piercingSniper, piercingPistol, suppressed}, 12);
        input.startCount = step;
        input.prestige = 121;
        const auto plan = fr::PlanLoadout(input);
        const auto picked = RulePicks(input, plan);
        REQUIRE(picked.size() == 1);
        CHECK((picked[0] == shotgun.name || picked[0] == piercingSniper.name || picked[0] == piercingPistol.name));
        chosen.insert(picked[0]);
    }
    CHECK(chosen.size() > 1); // drawn among the preferred, not always the same

    // Neither a shotgun nor a piercing firearm owned: any firearm.
    auto input = Input({pistol, smg, Item("Kitchen Knife", "melee", 1, {"melee_lethal"})}, 12);
    input.prestige = 121;
    const auto plan = fr::PlanLoadout(input);
    const auto picked = RulePicks(input, plan);
    REQUIRE(picked.size() == 1);
    CHECK((picked[0] == pistol.name || picked[0] == smg.name));

    // A preferred one that does not fit falls back to one that does.
    auto tight = Input({pistol, Pocketable(Item("Big Shotgun", "shotgun", 5, {"shotgun"}, 4))}, 2);
    tight.prestige = 121;
    CHECK(RulePicks(tight, fr::PlanLoadout(tight)) == std::vector<std::string> {pistol.name});
}

TEST_CASE("loadout: Shoot Target into Water does not exist: no loadout need") {
    CHECK(fr::PrestigeNeeds(124).empty());
    auto input = Input(Wall(), 6);
    input.prestige = 124;
    const auto plan = fr::PlanLoadout(input);
    for (const auto& pick : plan.picks)
        CHECK(pick.rule != fr::LoadoutRule::Prestige);
}

TEST_CASE("loadout: Silent Assassin, No Firearms brings no firearm from any rule, dart guns included") {
    // Dart guns are pistols by icon and trait in pro.repo.
    const auto sick = Item("(Dartgun) Sick", "pistol", 1, {"pistol"}, 4);
    const auto kalmer = Item("(S3) (Dartgun) Kalmer 2 - Tranquilizer", "pistol", 1, {"pistol"}, 4);
    CHECK(fr::IsFirearmItem(sick));
    CHECK(fr::IsFirearmItem(kalmer));

    const auto shotgun = Pocketable(Item("(Shotgun) Sawed Off Bartoli 12G", "shotgun", 1, {"shotgun"}, 4));
    const auto pistol = Item("(Pistol) ICA 19", "pistol", 1, {"pistol"}, 1);
    const auto knife = Item("Kitchen Knife", "melee", 1, {"melee_lethal"});
    const auto coin = Item("Coin / Tool_Coin", "distraction", 1, {"distraction"});
    const auto packed = Item("(Pistol) ICA 19 Classic Baller_Summer", "pistol", 1, {"pistol"}, 4, true);

    for (int32_t step = 0; step < 30; ++step) {
        CAPTURE(step);
        auto input = Input({sick, kalmer, shotgun, pistol, knife, coin}, 12, {packed});
        input.startCount = step;
        input.prestige = 116;
        input.clientReqs = {13, 59, 68}; // Shotgun Kill, Dart Gun, Knife Kill
        const auto plan = fr::PlanLoadout(input);
        for (const auto& pick : plan.picks)
            CHECK_FALSE(fr::IsFirearmItem(Picked(input, pick)));
        // The knife still comes; the firearm needs are skipped and noted.
        CHECK(RulePicks(input, plan) == std::vector<std::string> {knife.name});
        CHECK(Noted(plan, "a shotgun: needs a firearm; none brought (Silent Assassin, No Firearms)"));
        CHECK(Noted(plan, "a dart gun: needs a firearm; none brought (Silent Assassin, No Firearms)"));
        // The packed pistol is the player's: left alone, its slot counted.
        CHECK(plan.usedBefore == 1);
        CHECK(plan.usedAfter == 3); // packed pistol, knife, coin: the other slots stay empty
    }

    // Common objective 72 No Firearms alone keeps only the fill off firearms:
    // a firearm another objective needs still comes (the player chooses).
    auto input = Input({shotgun, pistol, knife, coin}, 12);
    input.clientReqs = {72, 13};
    const auto plan = fr::PlanLoadout(input);
    CHECK(RulePicks(input, plan) == std::vector<std::string> {shotgun.name});
}

TEST_CASE("loadout: Poison Kill prefers a lethal poison syringe") {
    const auto pills = Item("Lethal Pills S3", "poison", 1, {"poison", "consumable_poison"}, 2);
    const auto vial = Item("Lethal Poison FataLIDOMIDE", "poison", 2, {"poison", "consumable_poison"});
    const auto syringe = Item("Lethal Poison Syringe Antique", "poison", 1, {"poison", "syringe_poison"}, 2);
    const auto emeticSyringe = Item("Emetic Poison Syringe", "poison", 2, {"poison", "syringe_poison"});

    for (int32_t step = 0; step < 30; ++step) {
        auto input = Input({pills, vial, syringe, emeticSyringe}, 10);
        input.startCount = step;
        input.clientReqs = {8};
        CHECK(RulePicks(input, fr::PlanLoadout(input)) == std::vector<std::string> {syringe.name});
    }

    // No lethal syringe: other lethal poison, never the emetic syringe.
    auto input = Input({pills, vial, emeticSyringe}, 10);
    input.clientReqs = {8};
    const auto picked = RulePicks(input, fr::PlanLoadout(input));
    REQUIRE(picked.size() == 1);
    CHECK((picked[0] == pills.name || picked[0] == vial.name));
}

TEST_CASE("loadout: Poison Target and Poison Guard prefer a syringe of the right kind") {
    const auto emeticPills = Item("Emetic Pills", "poison", 1, {"poison", "consumable_poison"}, 2);
    const auto emeticSyringe = Item("Emetic Poison Syringe Antique", "poison", 1, {"poison", "syringe_poison"}, 2);
    const auto sedativePills = Item("Sedative Pills", "poison", 1, {"poison", "consumable_poison"}, 2);
    const auto sedativeSyringe = Item("Sedative Syringe Antique", "poison", 1, {"poison", "syringe_poison"}, 2);
    const auto lethalPills = Item("Lethal Pills S3", "poison", 1, {"poison", "consumable_poison"}, 2);

    const std::vector<fr::LoadoutItem> all {emeticPills, emeticSyringe, sedativePills, sedativeSyringe, lethalPills};
    for (int32_t step = 0; step < 30; ++step) {
        CAPTURE(step);
        const auto planFor = [&](std::vector<fr::LoadoutItem> owned, int32_t req) {
            auto input = Input(std::move(owned), 10);
            input.startCount = step;
            input.clientReqs = {req};
            return RulePicks(input, fr::PlanLoadout(input));
        };
        CHECK(planFor(all, 39) == std::vector<std::string> {emeticSyringe.name});
        CHECK(planFor(all, 40) == std::vector<std::string> {sedativeSyringe.name});
        for (const int32_t req : {41, 42}) {
            const auto picked = planFor(all, req);
            REQUIRE(picked.size() == 1);
            CHECK((picked[0] == emeticSyringe.name || picked[0] == sedativeSyringe.name));
        }
        // No syringe of the kind: other poison of that kind, not a syringe of another kind.
        CHECK(planFor({emeticPills, sedativeSyringe}, 39) == std::vector<std::string> {emeticPills.name});
        CHECK(planFor({sedativePills, emeticSyringe}, 40) == std::vector<std::string> {sedativePills.name});
        CHECK(planFor({lethalPills}, 42) == std::vector<std::string> {lethalPills.name});
    }
}

TEST_CASE("loadout: Trap Kill - Water Canister also brings a taser device, after every other objective item") {
    const auto water = Item("Evergreen WaterSpill Canister", "tool", 2, {"melee_nonlethal"}, 1);
    const auto taze = Item("Remote Taze / ICA Device Modular Remote Taze", "tool", 3, {"none"}, 1);
    const auto knife = Item("Kitchen Knife", "melee", 1, {"melee_lethal"});
    const auto duck = Item("Explosive Rubber Duck", "explosives", 2, {"explosive", "explosive_device"});

    // 28 listed first: the canister after the prestige's explosive, then the knife, then the taser.
    auto input = Input({water, taze, knife, duck}, 20);
    input.prestige = 123; // a lethal explosive
    input.clientReqs = {28, 68};
    const auto plan = fr::PlanLoadout(input);
    CHECK(RulePicks(input, plan) == std::vector<std::string> {duck.name, water.name, knife.name, taze.name});

    // Only room for the objective items: the taser stays home, noted.
    input.totalSlots = 6;
    const auto tight = fr::PlanLoadout(input);
    CHECK(RulePicks(input, tight) == std::vector<std::string> {duck.name, water.name, knife.name});
    CHECK(Noted(tight, "a taser device: 1 owned items match but none fits"));

    // No taser owned: noted, nothing else changes.
    auto none = Input({water, knife}, 20);
    none.clientReqs = {28};
    const auto nonePlan = fr::PlanLoadout(none);
    CHECK(RulePicks(none, nonePlan) == std::vector<std::string> {water.name});
    CHECK(Noted(nonePlan, "a taser device: no owned item matches"));
}

TEST_CASE("loadout: Blind Target and Blind Guard prefer a flash device over the flash grenade") {
    const auto phone = Item("Flash Phone / ICA Device Flash Phone", "tool", 2, {"tool"}, 2);
    const auto robot = Item("(Flash) Robot Lil' Flashy / (Flash) Robot Lil' Flashy Sambuca", "explosives", 2,
                            {"explosive", "explosive_device"}, 4);
    const auto remote =
        Item("Remote Flash / ICA Device Modular Remote Flash", "explosives", 3, {"explosive", "explosive_device"}, 1);
    const auto grenade = Item("(Grenade) Flash S3", "explosives", 2, {"explosive", "explosive_device"}, 1);

    for (const int32_t req : {56, 57}) {
        CAPTURE(req);
        std::set<std::string> chosen;
        for (int32_t step = 0; step < 40; ++step) {
            auto input = Input({phone, robot, remote, grenade}, 10);
            input.startCount = step;
            input.clientReqs = {req};
            const auto picked = RulePicks(input, fr::PlanLoadout(input));
            REQUIRE(picked.size() == 1);
            CHECK(picked[0] != grenade.name);
            chosen.insert(picked[0]);
        }
        CHECK(chosen.size() > 1);

        // The grenade only when no other flash item is owned, or none fits.
        auto input = Input({grenade, Item("Coin", "distraction", 1, {"distraction"})}, 10);
        input.clientReqs = {req};
        CHECK(RulePicks(input, fr::PlanLoadout(input)) == std::vector<std::string> {grenade.name});
        auto tight = Input({remote, grenade}, 2);
        tight.clientReqs = {req};
        CHECK(RulePicks(tight, fr::PlanLoadout(tight)) == std::vector<std::string> {grenade.name});
    }
}

namespace {

// A safehouse pickup the mod spawns (fr::kSpawnedItems), with a pro.repo
// capacity unlike its fixed cost.
fr::LoadoutItem SpawnedPickup(size_t index, std::string name, int32_t capacity) {
    fr::LoadoutItem item = Item(std::move(name), "melee", capacity, {"melee_nonlethal"}, 2);
    item.id = *fr::ParseGuid(fr::kSpawnedItems[index].id);
    item.source = fr::LoadoutSource::Spawned;
    return item;
}

fr::LoadoutItem SpawnedGrapes() {
    return SpawnedPickup(0, "GrapeVine / Melee_GrapeVine", 3);
}

fr::LoadoutItem SpawnedBanana() {
    return SpawnedPickup(1, "Banana / Melee_Banana", 1);
}

} // namespace

TEST_CASE("loadout: spawned grapes and banana take their fixed slots, whatever pro.repo says") {
    CHECK(fr::SlotCost(SpawnedGrapes()) == 1);
    CHECK(fr::SlotCost(SpawnedBanana()) == 2);
    CHECK(std::string(fr::LoadoutSourceName(fr::LoadoutSource::Spawned)).find("not owned") != std::string::npos);

    // The same id from a list of the player's goes by its capacity.
    fr::LoadoutItem owned = SpawnedGrapes();
    owned.source = fr::LoadoutSource::Persistent;
    CHECK(fr::SlotCost(owned) == 3);
}

TEST_CASE("loadout: Make Target Slip takes the smallest of grapes, SPF 47 and a banana") {
    const auto grapes = SpawnedGrapes();
    const auto banana = SpawnedBanana();
    // The game's own name for SPF 47 (no "spf" in it).
    const auto spf = Item("(SUMMER) Melee_Sun_Cream", "melee", 1, {"none"});
    const auto bigSpf = Item("(SUMMER) Melee_Sun_Cream", "melee", 3, {"none"});
    const auto water = Item("Evergreen WaterSpill Canister", "tool", 2, {"melee_nonlethal"}, 1);
    const auto oil =
        Item("Evergreen OilSpill Canister", "tool", 2, {"detonator_explosive_device", "melee_nonlethal"}, 1);
    const auto grapeKnife = Item("(S3) Melee_GrapePicking_Knife", "melee", 1, {"melee_lethal"});

    const auto planFor = [](std::vector<fr::LoadoutItem> owned, int32_t total = 10) {
        auto input = Input(std::move(owned), total);
        input.clientReqs = {70};
        return RulePicks(input, fr::PlanLoadout(input));
    };
    for (int32_t i = 0; i < 2; ++i) {
        // Nothing owned to slip on: the spawned grapes (1 slot), never the banana (2).
        CHECK(planFor({water, oil, grapeKnife, grapes, banana}) == std::vector<std::string> {grapes.name});
        CHECK(planFor({bigSpf, grapes, banana}) == std::vector<std::string> {grapes.name});
        const auto tie = planFor({spf, grapes, banana});
        REQUIRE(tie.size() == 1);
        CHECK((tie[0] == grapes.name || tie[0] == spf.name));
        // Without the grapes (not in pro.repo): the banana, unless SPF 47 is smaller.
        CHECK(planFor({bigSpf, water, banana}) == std::vector<std::string> {banana.name});
        CHECK(planFor({spf, banana}) == std::vector<std::string> {spf.name});
        CHECK(planFor({bigSpf}) == std::vector<std::string> {bigSpf.name});
    }
    CHECK(planFor({banana}, 1).empty());  // 2 slots, 1 left
    CHECK(planFor({water, oil}).empty()); // spills make no one slip
    CHECK(planFor({grapeKnife}).empty()); // the grape picking knife is nothing to slip on

    // The pick is the spawned item at its fixed cost.
    auto input = Input({water, grapes, banana}, 10);
    input.clientReqs = {70};
    const auto plan = fr::PlanLoadout(input);
    REQUIRE(!plan.picks.empty());
    CHECK(plan.picks[0].rule == fr::LoadoutRule::ClientReq);
    CHECK(Picked(input, plan.picks[0]).source == fr::LoadoutSource::Spawned);
    CHECK(plan.picks[0].cost == 1);
}

TEST_CASE("loadout: spawned grapes and banana serve Make Target Slip only") {
    const auto grapes = SpawnedGrapes();
    const auto banana = SpawnedBanana();
    const auto coin = Item("Coin", "distraction", 1, {"distraction"});

    // Never in the random fill, whatever the seed.
    for (int32_t seed = 0; seed < 32; ++seed) {
        auto input = Input({grapes, banana, coin}, 10);
        input.seed = seed;
        input.lockpickFirst = false;
        CHECK(PickedNames(input, fr::PlanLoadout(input)) == std::vector<std::string> {coin.name});
    }

    // Never for another need they match (37 Silent Takedown Rare: a rare
    // non-lethal melee item, as these test pickups are).
    auto input = Input({grapes, banana}, 10);
    input.clientReqs = {37};
    input.lockpickFirst = false;
    CHECK(fr::PlanLoadout(input).picks.empty());

    fr::LoadoutItem ownedGrapes = grapes;
    ownedGrapes.source = fr::LoadoutSource::Persistent;
    input.owned = {ownedGrapes};
    CHECK(RulePicks(input, fr::PlanLoadout(input)) == std::vector<std::string> {grapes.name});

    // With Make Target Slip too, the grapes go to it alone.
    input.owned = {grapes, banana};
    input.clientReqs = {37, 70};
    const auto both = fr::PlanLoadout(input);
    CHECK(PickedNames(input, both) == std::vector<std::string> {grapes.name});
    CHECK(Noted(both, "non-lethal melee item: no owned item matches"));
}

TEST_CASE("loadout: Distract Target comes last of every need, after the water canister's taser") {
    const auto water = Item("Evergreen WaterSpill Canister", "tool", 2, {"melee_nonlethal"}, 1);
    const auto taze = Item("Remote Taze / ICA Device Modular Remote Taze", "tool", 3, {"none"}, 1);
    const auto knife = Item("Kitchen Knife", "melee", 1, {"melee_lethal"});
    const auto coin = Item("Coin / Tool_Coin", "distraction", 1, {"distraction"});

    auto input = Input({coin, water, taze, knife}, 20);
    input.clientReqs = {71, 28, 68};
    const auto plan = fr::PlanLoadout(input);
    CHECK(RulePicks(input, plan) == std::vector<std::string> {water.name, knife.name, taze.name, coin.name});

    // Slots for the objective items only: no taser, no distraction item.
    input.totalSlots = 3;
    const auto tight = fr::PlanLoadout(input);
    CHECK(RulePicks(input, tight) == std::vector<std::string> {water.name, knife.name});

    // One slot left after them: the coin (1) fits, the taser (3) does not.
    input.totalSlots = 4;
    const auto oneLeft = fr::PlanLoadout(input);
    CHECK(RulePicks(input, oneLeft) == std::vector<std::string> {water.name, knife.name, coin.name});
}

TEST_CASE("loadout: preferred picks stay deterministic and independent of list order") {
    const auto pills = Item("Lethal Pills S3", "poison", 1, {"poison", "consumable_poison"}, 2);
    const auto syringeA = Item("Lethal Poison Syringe Antique", "poison", 1, {"poison", "syringe_poison"}, 2);
    const auto syringeB =
        Item("Lethal Poison Syringe / Lethal Poison Syringe Modern", "poison", 2, {"poison", "syringe_poison"});
    const auto phone = Item("Flash Phone / ICA Device Flash Phone", "tool", 2, {"tool"}, 2);
    const auto grenade = Item("(Grenade) Flash S3", "explosives", 2, {"explosive", "explosive_device"}, 1);
    for (int32_t step = 0; step < 20; ++step) {
        auto input = Input({pills, syringeA, syringeB, phone, grenade}, 12);
        input.startCount = step;
        input.clientReqs = {8, 56, 71};
        const auto plan = fr::PlanLoadout(input);
        CHECK(plan.picks.size() >= 2);
        const auto a = PickedNames(input, plan);
        auto reversed = input;
        std::reverse(reversed.owned.begin(), reversed.owned.end());
        CHECK(PickedNames(reversed, fr::PlanLoadout(reversed)) == a);
    }
}

// --- dart guns and the shuffled rarity ------------------------------------------

namespace {

fr::LoadoutItem Dart(std::string name, int32_t rarity) {
    fr::LoadoutItem item = Item(std::move(name), "pistol", 1, {"pistol"}, rarity);
    item.dartGun = true;
    return item;
}

} // namespace

TEST_CASE("loadout: a dart gun never meets a firearm kill; a real epic pistol is brought instead") {
    const auto kalmer = Dart("(S3) (Dartgun) Kalmer 2 - Tranquilizer", 4);
    const auto sick = Dart("(Dartgun) Sick", 3);
    const auto concept5 = Item("(Pistol) Concept 5", "pistol", 2, {"pistol"}, 3);
    const auto hackl = Item("(Pistol) Hackl 9S", "pistol", 2, {"pistol"}, 4, true);

    // 106/107 Silenced / Unsilenced Pistol Kill - Epic, 109 Legendary Firearm
    // Kill, 121 Collateral Kill - Firearms; 15/16 Silenced / Unsilenced Pistol
    // Kill, 53 Silenced Firearm Kill.
    for (const int32_t prestige : {106, 107, 109, 121}) {
        CAPTURE(prestige);
        for (const fr::ItemNeed& need : fr::PrestigeNeeds(prestige)) {
            CHECK_FALSE(fr::Matches(need, kalmer));
            CHECK_FALSE(fr::Matches(need, sick));
        }
    }
    for (const int32_t req : {15, 16, 53}) {
        CAPTURE(req);
        for (const fr::ItemNeed& need : fr::ClientReqNeeds(req)) {
            CHECK_FALSE(fr::Matches(need, kalmer));
            CHECK_FALSE(fr::Matches(need, sick));
        }
    }
    CHECK(fr::Matches(fr::PrestigeNeeds(107).front(), concept5));
    CHECK(fr::Matches(fr::ClientReqNeeds(16).front(), concept5));
    CHECK(fr::Matches(fr::PrestigeNeeds(106).front(), hackl));
    // Poison Target - Dart Gun still wants one.
    CHECK(fr::Matches(fr::ClientReqNeeds(59).front(), kalmer));

    // Game data, a departure: both dart guns packed (legendary and epic),
    // prestige 107; the owned epic Concept 5 is brought for it.
    auto input = Input({concept5, hackl, Item("Kitchen Knife", "melee", 1, {"melee_lethal"})}, 15, {kalmer, sick});
    input.prestige = 107;
    const auto plan = fr::PlanLoadout(input);
    CHECK(RulePicks(input, plan) == std::vector<std::string> {concept5.name});
    CHECK_FALSE(Noted(plan, "met by " + kalmer.name));

    // No real unsilenced pistol owned: unmet and noted, never a dart gun.
    auto none = Input({hackl, Dart("(Dartgun) Sick Gold", 4)}, 15, {kalmer});
    none.prestige = 107;
    const auto unmet = fr::PlanLoadout(none);
    CHECK(RulePicks(none, unmet).empty());
    CHECK(Noted(unmet, "an epic or legendary unsilenced pistol: no owned item matches"));
}

TEST_CASE("loadout: the rarity is the item's shuffled one: a vanilla-epic, now common pistol does not meet 107") {
    // LoadoutItem::rarity is the live Evergreen_Rarity, which the weapon
    // rarity shuffle writes before the plan: the ICA 19 Birth Baller, epic in
    // vanilla, is common in the campaign; the ICA 19 F/A, common in
    // vanilla, is epic.
    const auto birth = Item("(Pre-order) ICA 19 Birth Baller", "pistol", 4, {"pistol"}, 1);
    const auto fa = Item("(Pistol) ICA 19 F/A", "pistol", 1, {"pistol"}, 3);
    CHECK_FALSE(fr::Matches(fr::PrestigeNeeds(107).front(), birth));
    CHECK(fr::Matches(fr::PrestigeNeeds(107).front(), fa));

    // Packed, it does not meet the need: the epic one is added.
    auto input = Input({fa}, 15, {birth});
    input.prestige = 107;
    const auto plan = fr::PlanLoadout(input);
    CHECK(RulePicks(input, plan) == std::vector<std::string> {fa.name});
    CHECK_FALSE(Noted(plan, "met by " + birth.name));
}

TEST_CASE("loadout: client requirements go highest payout first") {
    // One free slot, two client requirements that need different items.
    const auto owned = std::vector<fr::LoadoutItem> {Item("Kitchen Knife", "melee", 1, {"melee_lethal"}),
                                                     Item("Fiber Wire", "fiberwire", 1, {"fiberwire"}),
                                                     Item("Ice Pick", "melee", 1, {"melee_lethal"})};
    const auto only = [&](const fr::LoadoutInput& input) {
        const auto plan = fr::PlanLoadout(input);
        REQUIRE(plan.picks.size() == 1);
        return plan;
    };

    SUBCASE("the higher payout gets the slot whatever the input order") {
        for (const auto& reqs : {std::vector<int32_t> {68, 24}, std::vector<int32_t> {24, 68}}) {
            auto input = Input(owned, 1);
            input.clientReqs = reqs;
            input.clientReqPayouts = {{68, 2000}, {24, 3500}}; // Knife Kill 2000, Fiber Wire Kill 3500
            const auto plan = only(input);
            CHECK(plan.picks[0].rule == fr::LoadoutRule::ClientReq);
            CHECK(Picked(input, plan.picks[0]).name == "Fiber Wire");
            CHECK(plan.picks[0].why.find("pays 3500") != std::string::npos);
            CHECK(std::any_of(plan.notes.begin(), plan.notes.end(), [](const std::string& note) {
                return note.starts_with("client requirements, highest payout first: 24") &&
                       note.find("(pays 3500), 68") != std::string::npos &&
                       note.find("(pays 2000)") != std::string::npos;
            }));
        }
    }

    SUBCASE("equal payouts keep the input order") {
        for (const auto& [reqs, first] : {std::pair {std::vector<int32_t> {68, 24}, "Kitchen Knife"},
                                          std::pair {std::vector<int32_t> {24, 68}, "Fiber Wire"}}) {
            auto input = Input(owned, 1);
            input.clientReqs = reqs;
            input.clientReqPayouts = {{68, 2500}, {24, 2500}};
            CHECK(Picked(input, only(input).picks[0]).name == first);
            // No payout known at all: the input order too.
            input.clientReqPayouts.clear();
            CHECK(Picked(input, only(input).picks[0]).name == first);
        }
    }

    SUBCASE("the prestige objective still comes before a higher paying client requirement") {
        auto input = Input(owned, 1);
        input.prestige = 110; // Ice Pick Kill
        input.clientReqs = {24};
        input.clientReqPayouts = {{24, 9000}};
        const auto plan = only(input);
        CHECK(plan.picks[0].rule == fr::LoadoutRule::Prestige);
        CHECK(Picked(input, plan.picks[0]).name == "Ice Pick");
    }

    SUBCASE("an unknown payout goes after a known one") {
        for (const auto& reqs : {std::vector<int32_t> {68, 24}, std::vector<int32_t> {24, 68}}) {
            auto input = Input(owned, 1);
            input.clientReqs = reqs;
            input.clientReqPayouts = {{68, 500}}; // Fiber Wire Kill's payout unknown
            const auto plan = only(input);
            CHECK(Picked(input, plan.picks[0]).name == "Kitchen Knife");
            CHECK(std::any_of(plan.notes.begin(), plan.notes.end(), [](const std::string& note) {
                return note.find("(pays 500), 24") != std::string::npos && note.ends_with("(payout unknown)");
            }));
        }
    }
}

TEST_CASE("loadout: the briefcase is held while 47 is in a blend spot or an act") {
    using fr::BlendHoldVerdict;
    using fr::HeroStateBit;

    // ZHitman5's movement-bits update sets one Mv bit per movement:
    // eMovementLocomotion -> eHGS_MvLocomotion (33), eMovementDisguiseSafeZone
    // -> eHGS_MvDisguiseSafeZone (45).
    const uint64_t walking = HeroStateBit(33);
    const uint64_t sneaking = walking | HeroStateBit(4);
    const uint64_t blend = HeroStateBit(fr::kHgsMvDisguiseSafeZone);

    SUBCASE("at the delivery (limits 0): a normal start gets it at once") {
        CHECK(fr::JudgeBlendHold(walking, 0, 0, 0, 0) == BlendHoldVerdict::Clear);
        CHECK(fr::JudgeBlendHold(sneaking, 0, 0, 0, 0) == BlendHoldVerdict::Clear);
        CHECK(fr::JudgeBlendHold(uint64_t {0}, 0, 0, 0, 0) == BlendHoldVerdict::Clear);
        CHECK(fr::BlendHoldGives(BlendHoldVerdict::Clear));
    }

    SUBCASE("at the delivery: in a blend spot or an act, it is held") {
        CHECK(fr::JudgeBlendHold(blend, 0, 0, 0, 0) == BlendHoldVerdict::InBlend);
        CHECK(fr::JudgeBlendHold(HeroStateBit(fr::kHgsMvNewFullBody), 0, 0, 0, 0) == BlendHoldVerdict::InBlend);
        CHECK(fr::JudgeBlendHold(HeroStateBit(fr::kHgsFocusedInteraction), 0, 0, 0, 0) == BlendHoldVerdict::InBlend);
        CHECK(fr::JudgeBlendHold(HeroStateBit(fr::kHgsInSequence) | walking, 0, 0, 0, 0) == BlendHoldVerdict::InBlend);
        CHECK(fr::JudgeBlendHold(HeroStateBit(fr::kHgsInsideCloset), 0, 0, 0, 0) == BlendHoldVerdict::InBlend);
        CHECK_FALSE(fr::BlendHoldGives(BlendHoldVerdict::InBlend));
    }

    SUBCASE("at the delivery: unreadable gives it as before") {
        CHECK(fr::JudgeBlendHold(std::nullopt, 0, 0, 0, 0) == BlendHoldVerdict::Unknown);
        // A bit past eHGS_NUM: not that field.
        CHECK(fr::JudgeBlendHold(blend | HeroStateBit(50), 0, 0, 0, 0) == BlendHoldVerdict::Unknown);
        CHECK(fr::BlendHoldGives(BlendHoldVerdict::Unknown));
    }

    SUBCASE("while held: in the blend, however long") {
        CHECK(fr::JudgeBlendHold(blend, 0, 0) == BlendHoldVerdict::InBlend);
        // A stale clear count does not matter while a hold state is on.
        CHECK(fr::JudgeBlendHold(blend | walking, 99, 0) == BlendHoldVerdict::InBlend);
    }

    SUBCASE("while held: given once out of it for kBlendClearSeconds") {
        CHECK(fr::JudgeBlendHold(walking, 0.1, 0) == BlendHoldVerdict::Settling);
        CHECK_FALSE(fr::BlendHoldGives(BlendHoldVerdict::Settling));
        CHECK(fr::JudgeBlendHold(walking, fr::kBlendClearSeconds, 0) == BlendHoldVerdict::Clear);
        CHECK(fr::JudgeBlendHold(walking, 0.3, 0, 0.25) == BlendHoldVerdict::Clear);
    }

    SUBCASE("while held: unreadable for a while, then given as before") {
        CHECK(fr::JudgeBlendHold(std::nullopt, 0, 1.0) == BlendHoldVerdict::Unread);
        CHECK_FALSE(fr::BlendHoldGives(BlendHoldVerdict::Unread));
        CHECK(fr::JudgeBlendHold(std::nullopt, 0, fr::kBlendUnreadSeconds) == BlendHoldVerdict::Unknown);
        CHECK(fr::JudgeBlendHold(HeroStateBit(63), 0, 0.5) == BlendHoldVerdict::Unread);
    }

    SUBCASE("the hold states") {
        CHECK(fr::kBlendHoldStates ==
              (HeroStateBit(5) | HeroStateBit(17) | HeroStateBit(19) | HeroStateBit(44) | HeroStateBit(45)));
        CHECK(fr::IsHeroStateMaskSane(HeroStateBit(46)));
        CHECK_FALSE(fr::IsHeroStateMaskSane(HeroStateBit(47)));
    }

    SUBCASE("log names") {
        CHECK(fr::HeroStateText(0) == "none");
        CHECK(fr::HeroStateText(sneaking) == "eHGS_Sneaking, eHGS_MvLocomotion");
        CHECK(fr::HeroStateText(blend) == "eHGS_MvDisguiseSafeZone");
        CHECK(fr::HeroStateText(HeroStateBit(46) | HeroStateBit(52)) == "eHGS_MvPlaceItem, bit 52");
        CHECK(fr::MovementTypeName(13) == "eMovementDisguiseSafeZone");
        CHECK(fr::MovementTypeName(36) == "eMovementLocomotion");
        CHECK(fr::MovementTypeName(-1) == "eMovementNone");
        CHECK(fr::MovementTypeName(38) == "eMovementPickupItem");
        CHECK(fr::MovementTypeName(99) == "type 99");
        CHECK(std::string(fr::BlendHoldVerdictName(BlendHoldVerdict::InBlend)) == "47 is in a blend spot or an act");
    }
}
