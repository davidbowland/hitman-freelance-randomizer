#include "doctest.h"

#include "CrateItems.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kScene = "assembly:/_pro/scenes/missions/skunk/scene_skunk_mild_solitaire.entity";

fr::Guid G(uint64_t n) {
    return fr::Guid::FromHalves(0x1000 + n, n);
}

// A tool-slot sample with no source text.
fr::CrateRestSample Sample(std::optional<fr::WorldAxes> axes, std::optional<bool> attach, std::optional<double> lift) {
    return {axes, attach, lift, fr::CratePlacement::ToolSlot, ""};
}

} // namespace

TEST_CASE("crate items: the eight option keys, the menu name's among them") {
    CHECK(fr::IsCrateOptionKeyId(0x3303c63a6583a672ull));
    CHECK(fr::IsCrateOptionKeyId(0xacce7665084a7786ull));
    CHECK(fr::IsCrateOptionKeyId(0x6135c34b3c1c8f6full));
    CHECK(fr::IsCrateOptionKeyId(0xac6acba75274fcd2ull)); // the menu entry's headline and name
    CHECK(fr::IsCrateOptionKeyId(0x0bafd98c5f317a92ull));
    CHECK(fr::IsCrateOptionKeyId(0x6eb7aa2232670446ull));
    CHECK(fr::IsCrateOptionKeyId(0xb4da20a580a21989ull));
    CHECK(fr::IsCrateOptionKeyId(0xcf78fa909113e50full));
    CHECK(std::set<uint64_t>(fr::kCrateOptionKeyIds.begin(), fr::kCrateOptionKeyIds.end()).size() == 8);
    CHECK_FALSE(fr::IsCrateOptionKeyId(fr::kOptionChoiceId));
    CHECK_FALSE(fr::IsCrateOptionKeyId(fr::kOptionConditionId));
}

TEST_CASE("crate items: tool slots are the selectors with options, no firearm, no merces, in order") {
    // Game data: the branch selector (no options), slot 22, slot 25, the weapon pool (30, 13 firearms,
    // the merces option), slot 18.
    const std::vector<fr::CrateSlotSummary> s_Selectors {
        {2, 0, 0, 0}, {22, 22, 0, 0}, {25, 25, 0, 0}, {30, 30, 13, 1}, {18, 18, 0, 0},
    };
    const std::vector<size_t> s_Slots = fr::CrateToolSlots(s_Selectors);
    REQUIRE(s_Slots.size() == 3);
    CHECK(s_Slots[0] == 1);
    CHECK(s_Slots[1] == 2);
    CHECK(s_Slots[2] == 4);

    // A merces-only selector is the weapon pool's, not a tool slot.
    const std::vector<fr::CrateSlotSummary> s_Merces {{1, 1, 0, 1}};
    CHECK(fr::CrateToolSlots(s_Merces).empty());
    CHECK(fr::CrateToolSlots({}).empty());
}

TEST_CASE("crate items: merces items") {
    CHECK(fr::IsMercesItem("Mercer_Pickup_VerySmall_RewardCrate", "evergreen_payout"));
    CHECK(fr::IsMercesItem("Mercer_Pickup_Large", ""));
    CHECK(fr::IsMercesItem("Gold bar", "evergreen_payout"));
    CHECK_FALSE(fr::IsMercesItem("Scissors", "melee"));
}

// --- crate_items ------------------------------------------------------------------------------------------------

namespace {

fr::CrateCandidate Cand(uint64_t n, std::string name, std::string icon = "melee", bool loaded = true) {
    fr::CrateCandidate c;
    c.item = G(n);
    c.name = std::move(name);
    c.icon = std::move(icon);
    c.loaded = loaded;
    c.firearm = c.icon == "pistol" || c.icon == "sniperrifle" || c.icon == "smg";
    return c;
}

fr::CrateCandidate Tool(uint64_t n, bool loaded = true) {
    fr::CrateCandidate c = Cand(n, "Tool " + std::to_string(n), "tool", loaded);
    c.tool = true;
    return c;
}

fr::CrateCandidate Gun(uint64_t n, int32_t rarity, bool owned = false, bool large = false, bool loaded = true) {
    fr::CrateCandidate c = Cand(n, "Gun " + std::to_string(n), large ? "sniperrifle" : "pistol", loaded);
    c.rarity = rarity;
    c.owned = owned;
    c.large = large;
    return c;
}

// A mission's candidates: 10 small map items, 4 tools, 3 unowned guns (one large), 1 owned gun, 2 large melee,
// 1 merces, 1 unnamed, and 2 tools and 2 guns the map has not loaded.
std::vector<fr::CrateCandidate> Mission() {
    std::vector<fr::CrateCandidate> v;

    for (uint64_t n = 1; n <= 10; ++n)
        v.push_back(Cand(n, "Item " + std::to_string(n)));

    for (uint64_t n = 20; n < 24; ++n)
        v.push_back(Tool(n));

    v.push_back(Gun(30, 1));
    v.push_back(Gun(31, 4));
    v.push_back(Gun(32, 2, false, true));
    v.push_back(Gun(33, 1, true));

    fr::CrateCandidate staff = Cand(40, "Staff");
    staff.large = true;
    v.push_back(staff);
    fr::CrateCandidate sword = Cand(41, "Sword");
    sword.large = true;
    v.push_back(sword);

    fr::CrateCandidate merces = Cand(50, "Mercer_Pickup_VerySmall_RewardCrate", "evergreen_payout");
    merces.merces = true;
    v.push_back(merces);
    v.push_back(Cand(51, ""));

    v.push_back(Tool(60, false));
    v.push_back(Tool(61, false));
    v.push_back(Gun(70, 1, false, false, false));
    v.push_back(Gun(71, 3, false, false, false));
    return v;
}

// The pools of the map's loaded items only (crate_items_unloaded off).
fr::CrateItemsConfig LoadedOnly() {
    fr::CrateItemsConfig c;
    c.unloaded = false;
    return c;
}

std::set<fr::Guid> PlanItems(const fr::CratePlan& plan, const std::vector<fr::CrateCandidate>& c) {
    std::set<fr::Guid> s;

    for (const auto& slot : plan.slots) {
        if (slot)
            s.insert(c[*slot].item);
    }

    if (plan.single)
        s.insert(c[*plan.single].item);

    return s;
}

} // namespace

TEST_CASE("crate items: Freelancer tools, large items, runtime ids") {
    CHECK(fr::IsFreelancerTool(true, "tool", "(Tool) Lockpick S3"));
    CHECK(fr::IsFreelancerTool(true, "poison", "Emetic Poison Syringe"));
    CHECK_FALSE(fr::IsFreelancerTool(false, "tool", "Crowbar"));
    CHECK_FALSE(fr::IsFreelancerTool(true, "pistol", "ICA 19"));
    CHECK_FALSE(fr::IsFreelancerTool(true, "evergreen_payout", "Gold"));

    CHECK(fr::IsLargeCrateItem("ITEMSIZE_LARGE", false));
    CHECK(fr::IsLargeCrateItem("itemsize_large", false));
    CHECK_FALSE(fr::IsLargeCrateItem("ITEMSIZE_LARGE", true)); // the crate's own items fit
    CHECK_FALSE(fr::IsLargeCrateItem("ITEMSIZE_MEDIUM", false));
    CHECK_FALSE(fr::IsLargeCrateItem("", false));

    CHECK(fr::ParseRuntimeId("46316738499935331") == std::optional<uint64_t>(0x00A48CD409717C63ull));
    CHECK_FALSE(fr::ParseRuntimeId(""));
    CHECK_FALSE(fr::ParseRuntimeId("0"));
    CHECK_FALSE(fr::ParseRuntimeId("12a"));
    CHECK_FALSE(fr::ParseRuntimeId("99999999999999999999"));
}

TEST_CASE("crate items: a gun counts as owned unless something says it is not") {
    CHECK(fr::CrateGunOwned(std::nullopt, std::nullopt)); // nothing known: never offered
    CHECK_FALSE(fr::CrateGunOwned(true, std::nullopt));   // flags: not owned, entitled
    CHECK(fr::CrateGunOwned(false, std::nullopt));        // flags: owned or not entitled
    CHECK_FALSE(fr::CrateGunOwned(std::nullopt, false));  // not on the gear wall
    CHECK(fr::CrateGunOwned(std::nullopt, true));         // on the gear wall
    CHECK(fr::CrateGunOwned(true, true));                 // the CPD wins over stale flags
    CHECK(fr::CrateGunOwned(false, false));
}

TEST_CASE("crate items: item names fold trimmed and lower case") {
    CHECK(fr::FoldCrateItemName("  (Tool) Crowbar Professional\t") == "(tool) crowbar professional");
    CHECK(fr::FoldCrateItemName("ICA Titanium Crowbar") == "ica titanium crowbar");
    CHECK(fr::FoldCrateItemName("   ").empty());
    CHECK(fr::FoldCrateItemName("").empty());
}

TEST_CASE("crate items: owned by id, or by another owned item's name") {
    // The ICA Titanium Crowbar under two ids, 736132de-... (a Freelancer tool, in TransientItems)
    // and 4404fc8d-... (rarity 3, large, on the gear wall). Either one owned keeps both out of crates.
    const fr::Guid s_ToolCrowbar = *fr::ParseGuid("736132de-78f3-4366-b927-ed9a401dbe26");
    const fr::Guid s_WallCrowbar = *fr::ParseGuid("4404fc8d-ffc0-472d-a63a-6480be973e74");
    const fr::Guid s_Screwdriver = *fr::ParseGuid("92d9acf6-fd79-4818-bda6-c4c28b123d8c");
    const fr::Guid s_Wrench = G(40);
    const fr::Guid s_Nameless = G(41);
    const fr::Guid s_OtherNameless = G(42);
    const std::vector<std::pair<fr::Guid, std::string>> s_Repo = {
        {s_ToolCrowbar, "(Tool) Crowbar Professional"},
        {s_WallCrowbar, "(Tool) Crowbar Professional"},
        {s_Screwdriver, "(Tool) Screwdriver Professional"},
        {s_Wrench, "Wrench"},
        {s_Nameless, ""},
        {s_OtherNameless, "  "},
    };

    SUBCASE("the gear wall's crowbar owned: the tool crowbar counts as owned by name") {
        const std::vector<fr::Guid> s_Listed = {s_WallCrowbar, s_Nameless};
        const fr::CrateOwnedItems s_Owned = fr::MakeCrateOwnedItems(s_Listed, s_Repo);
        CHECK(s_Owned.ids.size() == 2);
        CHECK(s_Owned.names == std::vector<std::string> {"(tool) crowbar professional"}); // no empty name
        CHECK(fr::CrateItemListed(s_Owned, s_WallCrowbar, "(Tool) Crowbar Professional") == fr::CrateOwnedBy::Id);
        CHECK(fr::CrateItemListed(s_Owned, s_ToolCrowbar, "(Tool) Crowbar Professional") == fr::CrateOwnedBy::Name);
        CHECK(fr::CrateItemListed(s_Owned, s_ToolCrowbar, " (tool) CROWBAR professional ") == fr::CrateOwnedBy::Name);
        // Genuinely unowned items stay offerable; an unnamed item never matches an owned unnamed one.
        CHECK(fr::CrateItemListed(s_Owned, s_Screwdriver, "(Tool) Screwdriver Professional") == fr::CrateOwnedBy::None);
        CHECK(fr::CrateItemListed(s_Owned, s_Wrench, "Wrench") == fr::CrateOwnedBy::None);
        CHECK(fr::CrateItemListed(s_Owned, s_OtherNameless, "  ") == fr::CrateOwnedBy::None);
        CHECK(fr::CrateItemListed(s_Owned, s_OtherNameless, "") == fr::CrateOwnedBy::None);
    }

    SUBCASE("the tool crowbar in TransientItems: the id itself, and the wall's crowbar by name") {
        const std::vector<fr::Guid> s_Listed = {s_ToolCrowbar, s_ToolCrowbar, s_Screwdriver}; // a repeat counts once
        const fr::CrateOwnedItems s_Owned = fr::MakeCrateOwnedItems(s_Listed, s_Repo);
        CHECK(s_Owned.ids.size() == 2);
        CHECK(s_Owned.names.size() == 2);
        CHECK(fr::CrateItemListed(s_Owned, s_ToolCrowbar, "(Tool) Crowbar Professional") == fr::CrateOwnedBy::Id);
        CHECK(fr::CrateItemListed(s_Owned, s_WallCrowbar, "(Tool) Crowbar Professional") == fr::CrateOwnedBy::Name);
        CHECK(fr::CrateItemListed(s_Owned, s_Wrench, "Wrench") == fr::CrateOwnedBy::None);
    }

    SUBCASE("a listed id pro.repo does not name still counts, by id only") {
        const std::vector<fr::Guid> s_Listed = {G(99)};
        const fr::CrateOwnedItems s_Owned = fr::MakeCrateOwnedItems(s_Listed, s_Repo);
        CHECK(s_Owned.names.empty());
        CHECK(fr::CrateItemListed(s_Owned, G(99), "") == fr::CrateOwnedBy::Id);
        CHECK(fr::CrateItemListed(s_Owned, s_ToolCrowbar, "(Tool) Crowbar Professional") == fr::CrateOwnedBy::None);
    }

    SUBCASE("nothing listed: nothing owned") {
        const fr::CrateOwnedItems s_Owned = fr::MakeCrateOwnedItems({}, s_Repo);
        CHECK(fr::CrateItemListed(s_Owned, s_ToolCrowbar, "(Tool) Crowbar Professional") == fr::CrateOwnedBy::None);
    }
}

TEST_CASE("crate items: a crowbar owned under another id never reaches a pool") {
    const fr::Guid s_ToolCrowbar = *fr::ParseGuid("736132de-78f3-4366-b927-ed9a401dbe26");
    const fr::Guid s_WallCrowbar = *fr::ParseGuid("4404fc8d-ffc0-472d-a63a-6480be973e74");
    const std::vector<std::pair<fr::Guid, std::string>> s_Repo = {
        {s_ToolCrowbar, "(Tool) Crowbar Professional"},
        {s_WallCrowbar, "(Tool) Crowbar Professional"},
    };
    const std::vector<fr::Guid> s_Listed = {s_WallCrowbar};
    const fr::CrateOwnedItems s_Owned = fr::MakeCrateOwnedItems(s_Listed, s_Repo);

    // The log's crate #1: the tool crowbar as a large map item, alone in the large pool.
    std::vector<fr::CrateCandidate> c;
    fr::CrateCandidate s_Crowbar = Cand(1, "(Tool) Crowbar Professional", "tool");
    s_Crowbar.item = s_ToolCrowbar;
    s_Crowbar.tool = true;
    s_Crowbar.large = true;
    c.push_back(s_Crowbar);
    fr::CrateCandidate s_Wrench = Cand(2, "Wrench");
    s_Wrench.large = true;
    c.push_back(s_Wrench);

    for (fr::CrateCandidate& s_Item : c)
        s_Item.owned = fr::CrateItemListed(s_Owned, s_Item.item, s_Item.name) != fr::CrateOwnedBy::None;

    const fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    REQUIRE(p.large.size() == 1);
    CHECK(c[p.large[0]].name == "Wrench"); // the unowned item is still offered
    CHECK(p.ownedItems == 1);
    CHECK(p.ownedTools == 1);
}

TEST_CASE("crate items: config is sanitized") {
    fr::CrateItemsConfig cfg;
    cfg.toolsChance = 1.5;
    cfg.mercesChance = std::nan("");
    const fr::CrateItemsConfig s = fr::SanitizeCrateItemsConfig(cfg);
    CHECK(s.toolsChance == 1.0);
    CHECK(s.mercesChance == fr::CrateItemsConfig {}.mercesChance);
    CHECK(fr::DescribeCrateItemsConfig(LoadedOnly(), fr::CrateGunsConfig {}).find("loaded only") != std::string::npos);
    // Items the map has not loaded, by default (an unloaded gun streams in when the crate opens).
    CHECK(fr::CrateItemsConfig {}.unloaded);
    // The defaults: gun 5%, tools 10%, merces 5%, the rest regular.
    CHECK(fr::DescribeCrateItemsConfig(fr::CrateItemsConfig {}, fr::CrateGunsConfig {})
              .find("gun 5%, tools 10%, merces 5%, regular 80%, gun and tool crates take items the map has not "
                    "loaded too") == 0);
}

TEST_CASE("crate items: the kinds' chances, scaled down together when they add up to more than 100%") {
    const fr::CrateKindChances d = fr::CrateKindChancesOf(fr::CrateItemsConfig {}, fr::CrateGunsConfig {});
    CHECK(d.gun == doctest::Approx(0.05));
    CHECK(d.tools == doctest::Approx(0.10));
    CHECK(d.merces == doctest::Approx(0.05));
    CHECK(d.regular() == doctest::Approx(0.80));
    CHECK(d.scale == 1.0);

    // crate_guns off: no gun crate.
    fr::CrateGunsConfig off;
    off.on = false;
    CHECK(fr::CrateKindChancesOf(fr::CrateItemsConfig {}, off).gun == 0.0);

    // 60 + 60 + 30 = 150%: each times 1 / 1.5, nothing left for regular crates.
    fr::CrateItemsConfig over;
    over.toolsChance = 0.6;
    over.mercesChance = 0.3;
    fr::CrateGunsConfig guns;
    guns.poolChance = 0.6;
    const fr::CrateKindChances o = fr::CrateKindChancesOf(over, guns);
    CHECK(o.gun == doctest::Approx(0.4));
    CHECK(o.tools == doctest::Approx(0.4));
    CHECK(o.merces == doctest::Approx(0.2));
    CHECK(o.regular() == doctest::Approx(0.0));
    CHECK(o.scale == doctest::Approx(1.0 / 1.5));
    CHECK(fr::DescribeCrateItemsConfig(over, guns).find("(set chances add up to 150%: scaled by 0.667)") !=
          std::string::npos);

    // Exactly 100%: unscaled.
    over.toolsChance = 0.5;
    over.mercesChance = 0.0;
    guns.poolChance = 0.5;
    CHECK(fr::CrateKindChancesOf(over, guns).scale == 1.0);
}

TEST_CASE("crate items: pools sort the candidates") {
    const std::vector<fr::CrateCandidate> c = Mission();
    const fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    CHECK(p.regular.size() == 14); // 10 items and 4 tools
    CHECK(p.tools.size() == 4);
    CHECK(p.guns.size() == 3); // the owned one left out; the large one in
    CHECK(p.large.size() == 2);
    CHECK(p.owned == 1);
    CHECK(p.merces == 1);
    CHECK(p.unnamed == 1);
    CHECK(p.notLoaded == 4);
    CHECK(p.oneItem);

    // Sorted by GUID whatever the order given.
    std::vector<fr::CrateCandidate> reversed(c.rbegin(), c.rend());
    const fr::CratePools q = fr::BuildCratePools(reversed, LoadedOnly(), true);
    REQUIRE(q.regular.size() == p.regular.size());

    for (size_t i = 0; i < p.regular.size(); ++i)
        CHECK(c[p.regular[i]].item == reversed[q.regular[i]].item);

    // Items the map has not loaded only with unloaded on, and only in the tool and gun pools.
    fr::CrateItemsConfig unloaded;
    unloaded.unloaded = true;
    const fr::CratePools u = fr::BuildCratePools(c, unloaded, true);
    CHECK(u.notLoaded == 0);
    CHECK(u.tools.size() == 6);
    CHECK(u.guns.size() == 5);
    CHECK(u.regular.size() == 14);

    for (const size_t i : u.regular)
        CHECK(c[i].loaded);

    // crate_guns off: no gun; planned guns and large items are not planned twice.
    CHECK(fr::BuildCratePools(c, LoadedOnly(), false).guns.empty());
    const std::vector<fr::Guid> planned {G(30), G(40), G(1)};
    const fr::CratePools t = fr::BuildCratePools(c, LoadedOnly(), true, planned);
    CHECK(t.guns.size() == 2);
    CHECK(t.large.size() == 1);
    CHECK(t.regular.size() == 14); // regular items may come again
    CHECK(t.taken == 2);

    CHECK(fr::DescribeCratePools(p).find("regular 14, tools 4, guns 3, large 2; left out: 1 unnamed, 1 merces, 0 quest "
                                         "items, 4 not loaded") == 0);
}

TEST_CASE("crate items: a mission's quest item is never a crate's, wherever the map holds it") {
    // Miami: Keycard (Sierra Knox ID), a QuestItem lying on the level, lies invisible in a crate. Quest items from the level, the mission's option helpers or a vanilla crate are left
    // out; a key (icon key) and the merces (QuestItem merces by name) are told apart.
    fr::CrateCandidate level = Cand(1, "Keycard (Sierra Knox ID)", "QuestItem");
    level.onLevel = true;
    fr::CrateCandidate map = Cand(2, "Event Pass Invitation", "questitem");
    map.onMap = true;
    fr::CrateCandidate key = Cand(3, "Keycard (Expo)", "key");
    key.onLevel = true;
    fr::CrateCandidate merces = Cand(4, "Mercer_Pickup_Medium", "QuestItem");
    merces.merces = true;
    const std::vector<fr::CrateCandidate> c {level, map, key, merces, Cand(5, "Bust")};

    const fr::CratePools p = fr::BuildCratePools(c, fr::CrateItemsConfig {}, true);
    CHECK(p.quest == 2);
    CHECK(p.merces == 1);
    REQUIRE(p.regular.size() == 2);
    CHECK(c[p.regular[0]].item == G(3));
    CHECK(c[p.regular[1]].item == G(5));
    CHECK(fr::DescribeCratePools(p).find("left out: 0 unnamed, 1 merces, 2 quest items, 0 not loaded") !=
          std::string::npos);

    CHECK(fr::IsQuestItemIcon("QuestItem"));
    CHECK(fr::IsQuestItemIcon("questitem"));
    CHECK_FALSE(fr::IsQuestItemIcon("key"));
    CHECK_FALSE(fr::IsQuestItemIcon(""));
    CHECK_FALSE(fr::IsQuestItemIcon("QuestItems"));
}

TEST_CASE("crate items: a usable quest prop (Fuse Cell, Gun Powder, ...) stays in crates; story quest items do not") {
    const auto Id = [](const char* text) { return *fr::ParseGuid(text); };
    const fr::Guid fuse = Id("daa20d0d-5da4-4ab0-9c7b-39bcee192241");
    CHECK(fr::IsUsableQuestProp(fuse));
    CHECK(fr::IsUsableQuestProp(Id("e65953cb-f954-4d21-9f11-52b454cac15e"))); // Gun Powder
    CHECK(fr::IsUsableQuestProp(Id("01048280-0358-4f0a-95b7-1f9f665c1648"))); // Insecticide
    CHECK(fr::IsUsableQuestProp(Id("ac77e98d-4ffa-4755-80fc-cd6e7adc63fb"))); // Expired Canned Bolognese (Sick)
    // Story items: Sierra Knox ID, Event Pass Invitation, a technician's note, an access code, the faulty microphone.
    CHECK_FALSE(fr::IsUsableQuestProp(Id("5ad2ea8f-d21a-4432-a891-25fcea11fd83")));
    CHECK_FALSE(fr::IsUsableQuestProp(Id("5f9481cf-3cbf-4f9c-9394-c60d23753681")));
    CHECK_FALSE(fr::IsUsableQuestProp(Id("91493547-bf42-41d6-8a1d-a4f812110719")));
    CHECK_FALSE(fr::IsUsableQuestProp(Id("ba9a6580-e7b9-4210-ab72-4a0d5a9329c0")));
    CHECK_FALSE(fr::IsUsableQuestProp(Id("049ee3c9-b504-4e2d-9e7e-674e57d8e4ec")));

    // In the pools, by id whatever its name: the Fuse Cell (on the level) joins the regular pool, the ID card not.
    fr::CrateCandidate fuseCell = Cand(1, "Fuse Cell", "questitem");
    fuseCell.item = fuse;
    fuseCell.onLevel = true;
    fr::CrateCandidate card = Cand(2, "Keycard (Sierra Knox ID)", "QuestItem");
    card.item = Id("5ad2ea8f-d21a-4432-a891-25fcea11fd83");
    card.onLevel = true;
    const std::vector<fr::CrateCandidate> c {fuseCell, card};
    const fr::CratePools p = fr::BuildCratePools(c, fr::CrateItemsConfig {}, true);
    CHECK(p.quest == 1);
    CHECK(p.usableProps == 1);
    REQUIRE(p.regular.size() == 1);
    CHECK(c[p.regular[0]].item == fuse);
    CHECK(fr::DescribeCratePools(p).find("1 quest items, 0 not loaded") != std::string::npos);
    CHECK(fr::DescribeCratePools(p).find("; kept: 1 usable prop (quest items)") != std::string::npos);

    // On the level: kept, logged as a usable prop.
    std::vector<fr::LevelSpawner> s;
    fr::LevelSpawner fuseSpawner;
    fuseSpawner.item = fuse;
    fuseSpawner.spawnOnStart = true;
    fuseSpawner.name = "Fuse Cell";
    fuseSpawner.commonName = "Fuse_Cell";
    fuseSpawner.icon = "questitem";
    fuseSpawner.itemType = "eOther_GenericPickup";
    fuseSpawner.hasRuntime = true;
    s.push_back(fuseSpawner);
    fr::LevelSpawner cardSpawner = fuseSpawner;
    cardSpawner.item = card.item;
    cardSpawner.name = "Keycard (Sierra Knox ID)";
    cardSpawner.commonName = "Keycard_Miami_SierraKnoxID";
    cardSpawner.icon = "QuestItem";
    s.push_back(cardSpawner);
    const fr::LevelItems items = fr::CollectLevelItems(s);
    REQUIRE(items.kept.size() == 1);
    CHECK(s[items.kept[0]].name == "Fuse Cell");
    REQUIRE(items.usable.size() == 1);
    REQUIRE(items.quest.size() == 1);
    CHECK(s[items.quest[0]].name == "Keycard (Sierra Knox ID)");
    const std::string text = fr::DescribeLevelItems(s, items);
    CHECK(text.find("; quest items left out: Keycard (Sierra Knox ID); kept: usable prop: Fuse Cell") !=
          std::string::npos);
}

TEST_CASE("crate items: the regular pool is the union of the vanilla crates' and the map's items, each once") {
    // Item 1 only in a vanilla crate, item 2 only on the map, item 3 in both (two option helpers: two candidates
    // with the same GUID), item 4 a tool the map has not loaded, item 5 a loaded firearm.
    fr::CrateCandidate crate = Cand(1, "Crate item");
    crate.inCrate = true;
    fr::CrateCandidate map = Cand(2, "Map item");
    map.onMap = true;
    fr::CrateCandidate both = Cand(3, "Both");
    both.inCrate = true;
    both.onMap = true;
    fr::CrateCandidate bothAgain = both;
    const std::vector<fr::CrateCandidate> c {crate, map, both, bothAgain, Tool(4, false), Gun(5, 1)};

    CHECK(std::string_view(fr::CrateItemOrigin(c[0])) == "vanilla crate");
    CHECK(std::string_view(fr::CrateItemOrigin(c[1])) == "map");
    CHECK(std::string_view(fr::CrateItemOrigin(c[2])) == "vanilla crate and map");
    CHECK(std::string_view(fr::CrateItemOrigin(c[4])) == "not loaded");

    fr::CrateItemsConfig unloaded;
    unloaded.unloaded = true;
    const fr::CratePools p = fr::BuildCratePools(c, unloaded, true);
    REQUIRE(p.regular.size() == 3); // no duplicate, no unloaded tool, no gun
    CHECK(c[p.regular[0]].item == G(1));
    CHECK(c[p.regular[1]].item == G(2));
    CHECK(c[p.regular[2]].item == G(3));
    CHECK(p.tools.size() == 1);
    CHECK(p.guns.size() == 1);

    // A regular crate holds exactly those three, each once, named with the pool and origin.
    fr::CrateGunsConfig noGun;
    noGun.on = false;
    fr::CrateItemsConfig regular;
    regular.toolsChance = 0.0;
    regular.mercesChance = 0.0;
    const fr::CratePlan plan = fr::PlanCrate(1, kScene, 0, c, p, regular, noGun);
    REQUIRE(plan.kind == fr::CratePlanKind::Items);
    CHECK(PlanItems(plan, c) == std::set<fr::Guid> {G(1), G(2), G(3)});

    for (size_t s = 0; s < 3; ++s)
        CHECK(plan.from[s] == fr::CratePickPool::Regular);

    const std::string text = fr::DescribeCratePlan(plan, c);
    CHECK(text.find("regular: slot 1 ") == 0);
    CHECK(text.find("Both (regular, vanilla crate and map)") != std::string::npos);
    CHECK(text.find("(roll ") != std::string::npos);
}

TEST_CASE("crate items: which items found around the level a crate may offer") {
    using V = fr::LevelItemVerdict;
    CHECK(fr::ClassifyLevelItem("Crowbar", "Melee_Crowbar", "melee", "eCC_Crowbar", true, true) == V::Kept);
    // Keys and keycards (icon key) are kept; quest items (icon QuestItem: an NPC's ID card, a fuse) are a mission's
    // props, left out (Miami's Sierra Knox ID lies invisible in a crate).
    CHECK(fr::ClassifyLevelItem("Keycard", "Key_Keycard_Security", "key", "eOther_Keycard_A", true, true) == V::Kept);
    CHECK(fr::ClassifyLevelItem("Fuse", "QuestItem_Fuse", "QuestItem", "eOther_GenericPickup", true, true) == V::Quest);
    CHECK(fr::ClassifyLevelItem("Keycard (Sierra Knox ID)", "Keycard_Miami_SierraKnoxID", "QuestItem",
                                "eOther_GenericPickup", true, true) == V::Quest);
    CHECK(fr::ClassifyLevelItem("Coin", "Tool_Coin", "distraction", "", true, true) == V::Kept);

    CHECK(fr::ClassifyLevelItem("", "", "melee", "", true, true) == V::Unnamed);
    CHECK(fr::ClassifyLevelItem("Thing", "Thing", "", "", true, true) == V::NotItem);
    CHECK(fr::ClassifyLevelItem("Thing", "Thing", "INVALID_CATEGORY_ICON", "", true, true) == V::NotItem);
    CHECK(fr::ClassifyLevelItem("ICA 19", "FIREARMS_HERO_PISTOL", "pistol", "eItemPistol", true, true) == V::Firearm);
    CHECK(fr::ClassifyLevelItem("Sniper", "S", "sniperrifle", "", true, true) == V::Firearm);
    // Merces: its own crate kind (also the reward crates' QuestItem merces, by CommonName).
    CHECK(fr::ClassifyLevelItem("Merces", "Mercer_Pickup_Medium", "QuestItem", "", true, true) == V::Merces);
    CHECK(fr::ClassifyLevelItem("Gold", "Gold", "evergreen_payout", "", true, true) == V::Merces);
    CHECK(fr::ClassifyLevelItem("ICA Briefcase", "Briefcase", "Container", "eItemContainer", true, true) ==
          V::Container);
    CHECK(fr::ClassifyLevelItem("Case", "Prop_Suitcase", "melee", "", true, true) == V::Container);
    CHECK(fr::ClassifyLevelItem("Bullets", "Ammo", "tool", "eItemAmmo", true, true) == V::Ammo);
    CHECK(fr::ClassifyLevelItem("Crowbar", "Melee_Crowbar", "melee", "", false, true) == V::NoModel);
    CHECK(fr::ClassifyLevelItem("Secret Intel", "QuestItem_Intel_Folder", "QuestItem", "", true, true) == V::Story);
    CHECK(fr::ClassifyLevelItem("Folder", "QuestItem_Evidence", "QuestItem", "", true, true) == V::Story);
    CHECK(fr::ClassifyLevelItem("Dossier", "QuestItem_Dossier", "QuestItem", "", true, true) == V::Story);
    // No pick-up: a target's drink or food to poison (the game data's table, by repository id; not by name).
    CHECK(fr::ClassifyLevelItem("Glass (Red)", "Poisonable_RedwineGlass", "poison", "eCC_Bottle", true, false) ==
          V::NotPickup);
    CHECK(fr::ClassifyLevelItem("Rat Poison (Emetic)", "Rat Poison", "poison", "eCC_Bottle", true, true) == V::Kept);
    // The rules before it still come first: a firearm with no pick-up is a firearm.
    CHECK(fr::ClassifyLevelItem("ANDROID ONLY SMG Fast", "Firearms_AndoirdONLY_SMG_Fast_Base", "smg", "eSMG_HK_UMP",
                                true, false) == V::Firearm);

    // The table, from tools/rpkg/level_items.py (pro.repo and the packages' reference tables), real ids.
    const auto Id = [](const char* text) { return *fr::ParseGuid(text); };
    // Poisonable props (runtime template on itemcorepoisonable), as tools/rpkg/level_items.py finds them on the
    // maps: "Glass (Red)" (Berlin's crate offer; Sapienza, Paris, ...), "Glass (White)" (Sapienza, Bangkok, ...), "Cup"
    // (Poisonable_CoffeCup, on 14 of the 15 maps), "Bottle" (Poisonable_BeerBottle: Berlin, Bangkok, Colorado, ...),
    // "Plastic Cup" (Poisonable_WaterCup: Berlin, Miami, Whittleton Creek), the mate cup and Berlin's Styrofoam Box.
    CHECK(fr::IsNotPickupItem(Id("1374a658-91d2-4abd-b339-28c7835467e8")));
    CHECK(fr::IsNotPickupItem(Id("90f54148-45dd-4959-9d80-7764b452e85e")));
    CHECK(fr::IsNotPickupItem(Id("4f2f4e60-d7f0-407b-b624-de880f9e3015")));
    CHECK(fr::IsNotPickupItem(Id("812bd23b-4998-4fb8-8625-1b7badec2746")));
    CHECK(fr::IsNotPickupItem(Id("a729d2c1-d89c-4da8-a628-816ba87dc349")));
    CHECK(fr::IsNotPickupItem(Id("3920b0cf-035a-481b-a9f6-e100676bebdd")));
    CHECK(fr::IsNotPickupItem(Id("0fae61c5-6410-417f-bdbe-b9e3605b53da")));
    // The free-align glass (itemcorepoisonablefreealign) and the NPC's phone (EnablePickUpOnSpawn false).
    CHECK(fr::IsNotPickupItem(Id("947b96d5-4be2-4805-855c-4d9b5a53a440")));
    CHECK(fr::IsNotPickupItem(Id("ef3ba32e-93bf-4858-8eb9-2576406b4946")));
    // Things you pick up stay: Lethal Pills' "Pill Glass (Lethal Poison)" (the old name filter dropped it), the
    // Glass of Champagne (melee), Rat Poison, a keycard, the Fuse Cell, Francesca's DNA sample, a brick.
    CHECK_FALSE(fr::IsNotPickupItem(Id("a1f89118-d345-4367-9423-620c3ef5dfba")));
    CHECK_FALSE(fr::IsNotPickupItem(Id("34d7bfbe-18a7-47f8-89e2-44f23f1482dc")));
    CHECK_FALSE(fr::IsNotPickupItem(Id("8b37a3a8-8a20-4262-81c5-0fcd15f4bba9")));
    CHECK_FALSE(fr::IsNotPickupItem(Id("584d3e2e-a278-4cfa-9d8d-f8777d3f46f2")));
    CHECK_FALSE(fr::IsNotPickupItem(Id("daa20d0d-5da4-4ab0-9c7b-39bcee192241")));
    CHECK_FALSE(fr::IsNotPickupItem(Id("93a7d4ac-c8a8-433f-b7c4-267916a4f436")));
    CHECK_FALSE(fr::IsNotPickupItem(Id("5cc4d1ea-b4fa-4667-ba3a-b6e859f03059")));

    CHECK(fr::LevelItemCategory("key", "") == "key");
    CHECK(fr::LevelItemCategory("QuestItem", "eOther_Keycard_A") == "key");
    CHECK(fr::LevelItemCategory("QuestItem", "") == "questitem");
    CHECK(fr::LevelItemCategory("", "") == "?");
}

namespace {

fr::LevelSpawner Spawner(uint64_t n, std::string name, std::string icon = "melee", bool atStart = true) {
    fr::LevelSpawner s;
    s.item = G(n);
    s.spawnOnStart = atStart;
    s.name = name;
    s.commonName = std::move(name);
    s.icon = std::move(icon);
    s.hasRuntime = true;
    return s;
}

} // namespace

TEST_CASE("crate items: the level's loose items, each once, sorted, the rest left out and counted") {
    std::vector<fr::LevelSpawner> s;
    s.push_back(Spawner(9, "Wrench"));
    s.push_back(Spawner(3, "Keycard", "key"));
    s.push_back(Spawner(9, "Wrench")); // a second wrench on the level: one item
    s.push_back(Spawner(5, "Coin", "distraction"));
    s.push_back(Spawner(6, "Spawned on demand", "melee", false));
    fr::LevelSpawner unread = Spawner(7, "Unread");
    unread.spawnOnStart.reset();
    s.push_back(unread);
    fr::LevelSpawner noKey = Spawner(8, "No key");
    noKey.item.reset();
    s.push_back(noKey);
    fr::LevelSpawner setup = Spawner(10, "Freelancer's own");
    setup.freelancerBrick = true;
    s.push_back(setup);
    s.push_back(Spawner(11, "Pistol", "pistol"));
    s.push_back(Spawner(12, "Merces", "evergreen_payout"));
    s.push_back(Spawner(13, "Secret Intel", "QuestItem"));
    s.push_back(Spawner(14, "", "melee"));
    s.push_back(Spawner(16, "Keycard (Sierra Knox ID)", "QuestItem"));
    // Berlin's "Glass (Red)": a target's drink, no pick-up (by its id, whatever its names say).
    fr::LevelSpawner glass = Spawner(15, "Glass (Red)", "poison");
    glass.item = fr::ParseGuid("1374a658-91d2-4abd-b339-28c7835467e8");
    s.push_back(glass);

    const fr::LevelItems items = fr::CollectLevelItems(s);
    CHECK(items.spawners == 14);
    CHECK(items.noItem == 1);
    CHECK(items.freelancer == 1);
    CHECK(items.startUnread == 1);
    CHECK(items.notAtStart == 1);
    CHECK(items.atStart == 10);
    CHECK(items.distinct == 9);
    REQUIRE(items.kept.size() == 3);
    // Sorted by GUID whatever the walk order: keycard (3), coin (5), wrench (9).
    CHECK(*s[items.kept[0]].item == G(3));
    CHECK(*s[items.kept[1]].item == G(5));
    CHECK(*s[items.kept[2]].item == G(9));
    CHECK(items.verdicts[static_cast<size_t>(fr::LevelItemVerdict::Firearm)] == 1);
    CHECK(items.verdicts[static_cast<size_t>(fr::LevelItemVerdict::Merces)] == 1);
    CHECK(items.verdicts[static_cast<size_t>(fr::LevelItemVerdict::Story)] == 1);
    CHECK(items.verdicts[static_cast<size_t>(fr::LevelItemVerdict::Unnamed)] == 1);
    REQUIRE(items.story.size() == 1);
    CHECK(s[items.story[0]].name == "Secret Intel");
    CHECK(items.verdicts[static_cast<size_t>(fr::LevelItemVerdict::NotPickup)] == 1);
    REQUIRE(items.notPickup.size() == 1);
    CHECK(s[items.notPickup[0]].name == "Glass (Red)");
    CHECK(items.verdicts[static_cast<size_t>(fr::LevelItemVerdict::Quest)] == 1);
    REQUIRE(items.quest.size() == 1);
    CHECK(s[items.quest[0]].name == "Keycard (Sierra Knox ID)");

    const std::string text = fr::DescribeLevelItems(s, items, 2);
    CHECK(text.find("14 item spawners outside the option helpers: 10 spawn at start (1 on demand, 1 unread, 1 in "
                    "evergreen_setup, 1 without an item key); 9 distinct items: 3 kept (") == 0);
    CHECK(text.find("distraction 1, key 1, melee 1): Keycard, Coin (+1 more); left out: 1 unnamed, 0 not inventory "
                    "items, 1 firearms (gun crates only), 1 merces,") != std::string::npos);
    CHECK(text.find("; story items left out: Secret Intel") != std::string::npos);
    CHECK(text.find(", 1 with no pick-up (drinks and food to poison), 1 quest items (a mission's props); story items "
                    "left out: Secret Intel; no pick-up, left out: Glass (Red); quest items left out: Keycard (Sierra "
                    "Knox ID)") != std::string::npos);

    // Nothing found: nothing kept.
    const fr::LevelItems none = fr::CollectLevelItems({});
    CHECK(none.kept.empty());
    CHECK(fr::DescribeLevelItems({}, none).find("0 item spawners") == 0);
}

TEST_CASE("crate items: the level's items join the regular pool, each once, never one you own") {
    // Item 1 in a vanilla crate and on the level, item 2 on the map and the level, item 3 only on the level, item 4
    // on the level but owned, item 5 a large item on the level, item 6 a key only on the level.
    fr::CrateCandidate crateLevel = Cand(1, "Crate and level");
    crateLevel.inCrate = true;
    crateLevel.onLevel = true;
    fr::CrateCandidate mapLevel = Cand(2, "Map and level");
    mapLevel.onMap = true;
    mapLevel.onLevel = true;
    fr::CrateCandidate level = Cand(3, "Level only");
    level.onLevel = true;
    fr::CrateCandidate owned = Cand(4, "Owned");
    owned.onLevel = true;
    owned.owned = true;
    fr::CrateCandidate large = Cand(5, "Sledgehammer");
    large.onLevel = true;
    large.large = true;
    fr::CrateCandidate key = Cand(6, "Keycard", "key");
    key.onLevel = true;
    fr::CrateCandidate all = Cand(7, "Everywhere");
    all.inCrate = true;
    all.onMap = true;
    all.onLevel = true;
    const std::vector<fr::CrateCandidate> c {crateLevel, mapLevel, level, level, owned, large, key, all};

    CHECK(std::string_view(fr::CrateItemOrigin(c[0])) == "vanilla crate and level");
    CHECK(std::string_view(fr::CrateItemOrigin(c[1])) == "map and level");
    CHECK(std::string_view(fr::CrateItemOrigin(c[2])) == "level");
    CHECK(std::string_view(fr::CrateItemOrigin(c[7])) == "vanilla crate, map and level");

    const fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    REQUIRE(p.regular.size() == 5); // 1, 2, 3 (once), 6, 7; not the owned one, not the large one
    CHECK(c[p.regular[0]].item == G(1));
    CHECK(c[p.regular[1]].item == G(2));
    CHECK(c[p.regular[2]].item == G(3));
    CHECK(c[p.regular[3]].item == G(6));
    CHECK(c[p.regular[4]].item == G(7));
    CHECK(p.ownedItems == 1);
    REQUIRE(p.large.size() == 1); // a large item from the level comes alone
    CHECK(c[p.large[0]].item == G(5));

    // A regular crate of them names the level as the origin.
    fr::CrateGunsConfig noGun;
    noGun.on = false;
    fr::CrateItemsConfig regular;
    regular.toolsChance = 0.0;
    regular.mercesChance = 0.0;
    bool levelNamed = false;

    for (int32_t seed = 0; seed < 50 && !levelNamed; ++seed) {
        const fr::CratePlan plan = fr::PlanCrate(seed, kScene, 0, c, p, regular, noGun);
        CHECK(PlanItems(plan, c).count(G(4)) == 0);
        levelNamed = fr::DescribeCratePlan(plan, c).find("Level only (regular, level)") != std::string::npos;
    }

    CHECK(levelNamed);

    // The config line says whether the level's items are in.
    CHECK(fr::CrateItemsConfig {}.levelItems);
    CHECK(fr::DescribeCrateItemsConfig(fr::CrateItemsConfig {}, fr::CrateGunsConfig {})
              .find("items around the level "
                    "too") != std::string::npos);
    fr::CrateItemsConfig off;
    off.levelItems = false;
    CHECK(fr::DescribeCrateItemsConfig(off, fr::CrateGunsConfig {}).find("no items from around the level") !=
          std::string::npos);
}

TEST_CASE("crate items: any item you own is left out of every pool") {
    std::vector<fr::CrateCandidate> c = Mission();
    // Owned (the CPD's lists): a map item (a comic book), a tool, a large item, an unloaded tool.
    for (fr::CrateCandidate& item : c) {
        if (item.item == G(1) || item.item == G(20) || item.item == G(40) || item.item == G(60))
            item.owned = true;
    }

    fr::CrateItemsConfig unloaded;
    unloaded.unloaded = true;
    const fr::CratePools p = fr::BuildCratePools(c, unloaded, true);
    CHECK(p.ownedItems == 4);
    CHECK(p.ownedTools == 2);
    CHECK(p.owned == 1);           // the owned gun still counts as a gun
    CHECK(p.guns.size() == 5);     // guns you don't own unchanged (3 loaded, 2 not)
    CHECK(p.regular.size() == 12); // 9 items and 3 loaded tools
    CHECK(p.tools.size() == 4);    // 21-23, 61
    CHECK(p.large.size() == 1);

    for (const auto* pool : {&p.regular, &p.tools, &p.guns, &p.large}) {
        for (const size_t i : *pool)
            CHECK_FALSE(c[i].owned);
    }

    const std::string text = fr::DescribeCratePools(p);
    CHECK(text.find("1 owned guns, 4 owned items (2 Freelancer tools)") != std::string::npos);

    // No plan ever offers an owned item, whatever the kind.
    fr::CrateItemsConfig cfg;
    cfg.toolsChance = 0.3;
    fr::CrateGunsConfig guns;
    guns.poolChance = 0.3;

    for (int32_t seed = 0; seed < 200; ++seed) {
        for (uint32_t open = 0; open < 3; ++open) {
            const fr::CratePlan plan = fr::PlanCrate(seed, kScene, open, c, p, cfg, guns);

            for (const fr::Guid& g : PlanItems(plan, c)) {
                const auto found =
                    std::find_if(c.begin(), c.end(), [&](const fr::CrateCandidate& x) { return x.item == g; });
                REQUIRE(found != c.end());
                CHECK_FALSE(found->owned);
            }
        }
    }
}

TEST_CASE("crate items: the roll picks the kind by its band, chances exact and in order") {
    const std::vector<fr::CrateCandidate> c = Mission();
    const fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    fr::CrateItemsConfig cfg;
    cfg.toolsChance = 0.2;
    cfg.mercesChance = 0.3;
    fr::CrateGunsConfig guns;
    guns.poolChance = 0.1;
    std::array<size_t, 4> counts {}; // gun, tools, merces, regular (with large)
    const size_t total = 2000;

    for (uint32_t open = 0; open < total; ++open) {
        const fr::CratePlan plan = fr::PlanCrate(11, kScene, open, c, p, cfg, guns);
        CAPTURE(open);
        CAPTURE(plan.roll);

        // Every pool has items: the kind is the roll's band.
        if (plan.roll < 0.1) {
            CHECK(plan.kind == fr::CratePlanKind::Gun);
            ++counts[0];
        }
        else if (plan.roll < 0.3) {
            CHECK(plan.kind == fr::CratePlanKind::Tools);
            ++counts[1];
        }
        else if (plan.roll < 0.6) {
            CHECK(plan.kind == fr::CratePlanKind::Merces);
            ++counts[2];
        }
        else {
            CHECK((plan.kind == fr::CratePlanKind::Items || plan.kind == fr::CratePlanKind::Large));
            ++counts[3];
        }
    }

    CHECK(counts[0] == doctest::Approx(0.1 * total).epsilon(0.25));
    CHECK(counts[1] == doctest::Approx(0.2 * total).epsilon(0.15));
    CHECK(counts[2] == doctest::Approx(0.3 * total).epsilon(0.15));
    CHECK(counts[3] == doctest::Approx(0.4 * total).epsilon(0.15));
}

TEST_CASE("crate items: kind chances at 0% and 100%, and over 100% in sum") {
    const std::vector<fr::CrateCandidate> c = Mission();
    const fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    fr::CrateItemsConfig none;
    none.toolsChance = 0.0;
    none.mercesChance = 0.0;
    fr::CrateGunsConfig noGun;
    noGun.poolChance = 0.0;

    // All at 0%: only regular crates (three items, or a large item alone).
    for (uint32_t open = 0; open < 300; ++open) {
        const fr::CratePlan plan = fr::PlanCrate(4, kScene, open, c, p, none, noGun);
        CHECK((plan.kind == fr::CratePlanKind::Items || plan.kind == fr::CratePlanKind::Large));
    }

    // Each at 100% alone: always that kind.
    fr::CrateGunsConfig allGun;
    allGun.poolChance = 1.0;
    fr::CrateItemsConfig allTools = none;
    allTools.toolsChance = 1.0;
    fr::CrateItemsConfig allMerces = none;
    allMerces.mercesChance = 1.0;

    for (uint32_t open = 0; open < 100; ++open) {
        CHECK(fr::PlanCrate(4, kScene, open, c, p, none, allGun).kind == fr::CratePlanKind::Gun);
        CHECK(fr::PlanCrate(4, kScene, open, c, p, allTools, noGun).kind == fr::CratePlanKind::Tools);
        CHECK(fr::PlanCrate(4, kScene, open, c, p, allMerces, noGun).kind == fr::CratePlanKind::Merces);
    }

    // All three at 100%: scaled to a third each, no regular crate; the why says so.
    fr::CrateItemsConfig all = allTools;
    all.mercesChance = 1.0;
    std::array<size_t, 3> counts {};
    const size_t total = 900;

    for (uint32_t open = 0; open < total; ++open) {
        const fr::CratePlan plan = fr::PlanCrate(4, kScene, open, c, p, all, allGun);
        CAPTURE(open);
        REQUIRE((plan.kind == fr::CratePlanKind::Gun || plan.kind == fr::CratePlanKind::Tools ||
                 plan.kind == fr::CratePlanKind::Merces));
        counts[plan.kind == fr::CratePlanKind::Gun ? 0 : plan.kind == fr::CratePlanKind::Tools ? 1 : 2] += 1;
        CHECK(plan.why.find("chances scaled by 0.333333 to fit 100%") != std::string::npos);
    }

    for (const size_t n : counts)
        CHECK(n == doctest::Approx(total / 3.0).epsilon(0.15));
}

TEST_CASE("crate items: a gun crate is one gun you don't own, alone, by rarity") {
    const std::vector<fr::CrateCandidate> c = Mission();
    const fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    fr::CrateItemsConfig none;
    none.toolsChance = 0.0;
    none.mercesChance = 0.0;
    fr::CrateGunsConfig always;
    always.poolChance = 1.0;
    always.rarityMult = {1.0, 1.0, 1.0, 1e6};
    size_t legendary = 0;

    for (int32_t seed = 0; seed < 200; ++seed) {
        const fr::CratePlan plan = fr::PlanCrate(seed, kScene, 0, c, p, none, always);
        REQUIRE(plan.kind == fr::CratePlanKind::Gun);
        REQUIRE(plan.single);
        CHECK(plan.singleFrom == fr::CratePickPool::Gun);
        CHECK(c[*plan.single].firearm);
        CHECK_FALSE(c[*plan.single].owned);
        CHECK_FALSE(plan.slots[0]);
        CHECK_FALSE(plan.slots[1]);
        CHECK_FALSE(plan.slots[2]);
        legendary += c[*plan.single].rarity == 4 ? 1 : 0;
    }

    CHECK(legendary >= 195);

    // Small or large, it comes alone.
    fr::CrateGunsConfig rifles = always;
    rifles.rarityMult = {0.0, 1.0, 0.0, 0.0}; // only the rare one: Gun 32, large
    const fr::CratePlan rifle = fr::PlanCrate(3, kScene, 0, c, p, none, rifles);
    CHECK(rifle.kind == fr::CratePlanKind::Gun);
    REQUIRE(rifle.single);
    CHECK(c[*rifle.single].item == G(32));
    fr::CrateGunsConfig pistols = always;
    pistols.rarityMult = {1.0, 0.0, 0.0, 0.0}; // only the common one: Gun 30, small
    const fr::CratePlan pistol = fr::PlanCrate(3, kScene, 0, c, p, none, pistols);
    REQUIRE(pistol.single);
    CHECK(c[*pistol.single].item == G(30));
    CHECK(fr::DescribeCratePlan(pistol, c).find("gun: alone: Gun 30 (gun, loaded) (roll ") == 0);
}

TEST_CASE("crate items: a gun crate with no gun you don't own is a regular crate") {
    std::vector<fr::CrateCandidate> c = Mission();

    for (fr::CrateCandidate& item : c) {
        if (item.firearm)
            item.owned = true;
    }

    const fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    REQUIRE(p.guns.empty());
    fr::CrateItemsConfig none;
    none.toolsChance = 0.0;
    none.mercesChance = 0.0;
    fr::CrateGunsConfig always;
    always.poolChance = 1.0;

    for (uint32_t open = 0; open < 50; ++open) {
        const fr::CratePlan plan = fr::PlanCrate(2, kScene, open, c, p, none, always);
        CAPTURE(open);
        CHECK((plan.kind == fr::CratePlanKind::Items || plan.kind == fr::CratePlanKind::Large));
        CHECK(plan.why.find("gun: no gun you don't own left to offer, a regular crate instead; ") == 0);

        for (const fr::Guid& g : PlanItems(plan, c)) {
            const auto found =
                std::find_if(c.begin(), c.end(), [&](const fr::CrateCandidate& x) { return x.item == g; });
            REQUIRE(found != c.end());
            CHECK_FALSE(found->firearm);
        }
    }

    // A gun left but no one-item crate possible (an item out): a regular crate too.
    fr::CratePools q = fr::BuildCratePools(Mission(), LoadedOnly(), true);
    fr::DropOneItemCrates(q);
    const fr::CratePlan held = fr::PlanCrate(2, kScene, 0, Mission(), q, none, always);
    CHECK(held.kind == fr::CratePlanKind::Items);
    CHECK(held.why.find("gun: no one-item crate now, a regular crate instead; ") == 0);
}

TEST_CASE("crate items: a tool crate takes the tools you don't own, topped up from the regular pool") {
    const std::vector<fr::CrateCandidate> c = Mission();
    fr::CrateItemsConfig tools;
    tools.toolsChance = 1.0;
    tools.mercesChance = 0.0;
    fr::CrateGunsConfig noGun;
    noGun.poolChance = 0.0;

    // Four tools: three tools.
    const fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    const fr::CratePlan full = fr::PlanCrate(1, kScene, 0, c, p, tools, noGun);
    REQUIRE(full.kind == fr::CratePlanKind::Tools);

    for (size_t s = 0; s < 3; ++s) {
        REQUIRE(full.slots[s]);
        CHECK(c[*full.slots[s]].tool);
        CHECK(full.from[s] == fr::CratePickPool::Tools);
    }

    CHECK(fr::DescribeCratePlan(full, c).find("tools: slot 1 Tool ") == 0);
    CHECK(full.why.find("3 Freelancer tools (4 you don't own, 0 owned left out), 0 topped up") != std::string::npos);

    // One tool you don't own: it, and two regular items, all different.
    std::vector<fr::CrateCandidate> fewTools = c;

    for (fr::CrateCandidate& item : fewTools) {
        if (item.tool && item.item != G(20))
            item.owned = true;
    }

    const fr::CratePools few = fr::BuildCratePools(fewTools, LoadedOnly(), true);
    REQUIRE(few.tools.size() == 1);

    for (uint32_t open = 0; open < 50; ++open) {
        const fr::CratePlan plan = fr::PlanCrate(1, kScene, open, fewTools, few, tools, noGun);
        CAPTURE(open);
        REQUIRE(plan.kind == fr::CratePlanKind::Tools);
        CHECK(PlanItems(plan, fewTools).size() == 3);
        CHECK(PlanItems(plan, fewTools).count(G(20)) == 1);
        size_t fromTools = 0;

        for (size_t s = 0; s < 3; ++s) {
            REQUIRE(plan.slots[s]);
            CHECK_FALSE(fewTools[*plan.slots[s]].owned);
            fromTools += plan.from[s] == fr::CratePickPool::Tools ? 1 : 0;
            CHECK((plan.from[s] == fr::CratePickPool::Tools || plan.from[s] == fr::CratePickPool::Regular));
        }

        CHECK(fromTools == 1);
        CHECK(plan.why.find(
                  "1 Freelancer tools (1 you don't own, 3 owned left out), 2 topped up from the regular pool") !=
              std::string::npos);
    }

    // No tool you don't own: a regular crate, logged.
    for (fr::CrateCandidate& item : fewTools) {
        if (item.tool)
            item.owned = true;
    }

    const fr::CratePools noTools = fr::BuildCratePools(fewTools, LoadedOnly(), true);
    const fr::CratePlan fell = fr::PlanCrate(1, kScene, 0, fewTools, noTools, tools, noGun);
    CHECK((fell.kind == fr::CratePlanKind::Items || fell.kind == fr::CratePlanKind::Large));
    CHECK(fell.why.find("tools: no Freelancer tool you don't own (4 owned left out), a regular crate instead; ") == 0);
}

TEST_CASE("crate items: a merces crate, and none while no one-item crate is possible") {
    const std::vector<fr::CrateCandidate> c = Mission();
    fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    fr::CrateItemsConfig merces;
    merces.toolsChance = 0.0;
    merces.mercesChance = 1.0;
    fr::CrateGunsConfig noGun;
    noGun.poolChance = 0.0;

    const fr::CratePlan plan = fr::PlanCrate(1, kScene, 0, c, p, merces, noGun);
    CHECK(plan.kind == fr::CratePlanKind::Merces);
    CHECK_FALSE(plan.single); // the merces option keeps its own item
    CHECK(PlanItems(plan, c).empty());
    CHECK(fr::DescribeCratePlan(plan, c).find("merces: the crate's own merces option, alone (roll ") == 0);

    fr::DropOneItemCrates(p);
    CHECK_FALSE(p.oneItem);
    const fr::CratePlan held = fr::PlanCrate(1, kScene, 0, c, p, merces, noGun);
    CHECK(held.kind == fr::CratePlanKind::Items);
    CHECK(held.why.find("merces: no one-item crate now, a regular crate instead; ") == 0);
}

TEST_CASE(
    "crate items: a regular crate never repeats an item, a large item comes alone, and the plan is deterministic") {
    const std::vector<fr::CrateCandidate> c = Mission();
    const fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    const fr::CrateItemsConfig items;
    const fr::CrateGunsConfig guns;
    std::set<fr::CratePlanKind> kinds;
    size_t differ = 0;

    for (int32_t seed = 0; seed < 300; ++seed) {
        for (uint32_t open = 0; open < 4; ++open) {
            const fr::CratePlan plan = fr::PlanCrate(seed, kScene, open, c, p, items, guns);
            const fr::CratePlan again = fr::PlanCrate(seed, kScene, open, c, p, items, guns);
            const fr::CratePlan other = fr::PlanCrate(seed + 1000, kScene, open, c, p, items, guns);
            CAPTURE(seed);
            CAPTURE(open);
            CHECK(plan.kind == again.kind);
            CHECK(plan.slots == again.slots);
            CHECK(plan.single == again.single);
            CHECK(plan.roll == again.roll);
            CHECK(plan.why == again.why);
            differ += plan.slots != other.slots || plan.single != other.single ? 1 : 0;
            kinds.insert(plan.kind);

            if (plan.kind == fr::CratePlanKind::Large) {
                REQUIRE(plan.single);
                CHECK(c[*plan.single].large);
                CHECK_FALSE(c[*plan.single].firearm);
                CHECK(plan.singleFrom == fr::CratePickPool::Regular);
                CHECK_FALSE(plan.slots[0]);
                continue;
            }

            if (plan.kind != fr::CratePlanKind::Items)
                continue;

            CHECK(PlanItems(plan, c).size() == 3); // three distinct items

            for (const auto& slot : plan.slots) {
                REQUIRE(slot);
                const fr::CrateCandidate& item = c[*slot];
                CHECK(item.loaded);
                CHECK_FALSE(item.large);
                CHECK_FALSE(item.merces);
                CHECK_FALSE(item.firearm);
                CHECK_FALSE(item.name.empty());
            }
        }
    }

    CHECK(differ > 1000); // another seed, another crate

    // With the defaults every kind turns up.
    CHECK(kinds.count(fr::CratePlanKind::Items) == 1);
    CHECK(kinds.count(fr::CratePlanKind::Tools) == 1);
    CHECK(kinds.count(fr::CratePlanKind::Gun) == 1);
    CHECK(kinds.count(fr::CratePlanKind::Merces) == 1);
    CHECK(kinds.count(fr::CratePlanKind::Large) == 1);

    // Fewer than three regular items: vanilla.
    const std::vector<fr::CrateCandidate> two {Cand(1, "A"), Cand(2, "B")};
    const fr::CratePools p2 = fr::BuildCratePools(two, LoadedOnly(), true);
    fr::CrateItemsConfig none;
    none.toolsChance = 0.0;
    none.mercesChance = 0.0;
    const fr::CratePlan vanilla = fr::PlanCrate(3, kScene, 0, two, p2, none, guns);
    CHECK(vanilla.kind == fr::CratePlanKind::Vanilla);
    CHECK(vanilla.why.find("only 2") != std::string::npos);
}

TEST_CASE("crate items: a slot's options when a crate opens") {
    using A = fr::CrateOptionAction;
    using H = std::optional<size_t>;

    // A new crate (#2) with nothing out: every option offers the slot's item.
    const std::vector<H> free(4);
    CHECK(fr::CrateSlotActions(free, 2, false) == std::vector<A> {A::Offer, A::Offer, A::Offer, A::Offer});

    // Crate #0 left option 1's item (nobody took it), and it comes along to crate #1, where its menu entry names
    // it over crate #1's item. Now crate #1 reuses that option: it gets crate #1's item
    // and is alone drawn, so one item lies in the slot and the menu names it.
    const std::vector<H> left {H(), H(0), H(), H()};
    CHECK(fr::CrateSlotActions(left, 1, false) == std::vector<A> {A::Exclude, A::Reuse, A::Exclude, A::Exclude});

    // Two options with an item out (from before the fix, or a crate opened again): the first is reused, the other
    // rewritten too (no stale name) but kept out of the draw.
    const std::vector<H> held {H(), H(0), H(), H(1)};
    CHECK(fr::CrateSlotActions(held, 2, false) == std::vector<A> {A::Exclude, A::Reuse, A::Exclude, A::Rewrite});

    // Crate #1 opened again with its own item still lying in it: nothing is drawn (options drawn again spawn their
    // items a second time, and the first ones stay and come along to the next crates). The other item out is rewritten, kept out.
    CHECK(fr::CrateSlotActions(held, 1, false) == std::vector<A> {A::Exclude, A::Rewrite, A::Exclude, A::Exclude});
    // The crate opened last, opened again: every item out is its own.
    const std::vector<H> again {H(), H(4), H(), H()};
    CHECK(fr::CrateSlotActions(again, 4, false) == std::vector<A> {A::Exclude, A::Exclude, A::Exclude, A::Exclude});

    // A crate opened again after its item of this slot was taken: the slot stays empty, other items out untouched.
    CHECK(fr::CrateSlotActions(held, 0, true) == std::vector<A> {A::Exclude, A::Exclude, A::Exclude, A::Exclude});
    CHECK(fr::CrateSlotActions(held, 2, true) == std::vector<A> {A::Exclude, A::Exclude, A::Exclude, A::Exclude});

    // Every option out: one reused, never a slot that draws nothing.
    const std::vector<H> full {H(0), H(1)};
    CHECK(fr::CrateSlotActions(full, 2, false) == std::vector<A> {A::Reuse, A::Rewrite});
    CHECK(fr::CrateSlotActions(std::vector<H> {}, 0, false).empty());

    // Something out, not taken from: exactly one option drawn for a crate none of them holds; none for a crate one
    // of them holds (its item lies there already). Never an option that spawns over an item it has out unchanged.
    for (size_t crate = 0; crate < 3; ++crate) {
        for (const std::vector<H>& h : {left, held, full}) {
            const std::vector<A> a = fr::CrateSlotActions(h, crate, false);
            const bool own = std::find(h.begin(), h.end(), H(crate)) != h.end();
            CHECK(std::count(a.begin(), a.end(), A::Reuse) == (own ? 0 : 1));
            CHECK(std::count(a.begin(), a.end(), A::Offer) == 0);

            for (size_t i = 0; i < h.size(); ++i) {
                if (h[i] == H(crate))
                    CHECK(a[i] == A::Exclude);
            }
        }
    }
}

namespace {

// Spawners measured in game: Fish's (+Z up, yawed a little), Napoleon figure's (+Y up), Coin's
// (-Z up, 1 degree off), and one 45 degrees off every axis.
const fr::WorldAxes kFish {1, -0.03f, 0, 0.03f, 1, 0, 0, 0, 1};
const fr::WorldAxes kNapoleon {1, 0, 0, 0, 0, 1, 0, -1, 0};
const fr::WorldAxes kCoin {-1, 0, 0.01f, 0, 1, 0, -0.01f, 0, -1};

fr::WorldAxes Leaning45() {
    const float h = std::sqrt(0.5f);
    return {1, 0, 0, 0, h, h, 0, -h, h};
}

constexpr fr::LocalAxis kX {0, false};
constexpr fr::LocalAxis kY {1, false};
constexpr fr::LocalAxis kMinusZ {2, true};

std::vector<fr::LocalAxis> AllAxes() {
    std::vector<fr::LocalAxis> s_All;

    for (uint8_t i = 0; i < 3; ++i) {
        s_All.push_back({i, false});
        s_All.push_back({i, true});
    }

    return s_All;
}

// T * v for a local axis v.
std::array<int, 3> Times(const fr::AxisTurn& t, fr::LocalAxis v) {
    std::array<int, 3> s_V {0, 0, 0};
    s_V[v.index] = v.negative ? -1 : 1;
    std::array<int, 3> s_Out {};

    for (size_t r = 0; r < 3; ++r)
        s_Out[r] = t[3 * r] * s_V[0] + t[3 * r + 1] * s_V[1] + t[3 * r + 2] * s_V[2];

    return s_Out;
}

int Determinant(const fr::AxisTurn& t) {
    return t[0] * (t[4] * t[8] - t[5] * t[7]) - t[1] * (t[3] * t[8] - t[5] * t[6]) + t[2] * (t[3] * t[7] - t[4] * t[6]);
}

const char* kPouchFlash = "f04bd2e9-3f3a-42cd-9150-b7845e952d67";
const char* kNapoleonFigure = "0a5bebc8-0148-4745-90b2-f54b3c71116c";
const char* kCoinClassic = "98bf7fc1-7857-4999-bc99-586c49f24017";

fr::Guid Parsed(const char* text) {
    return fr::ParseGuid(text).value_or(fr::Guid {});
}

} // namespace

TEST_CASE("crate items: a rotation's local axis pointing up") {
    CHECK(fr::UpLocalAxis(fr::WorldAxes {1, 0, 0, 0, 1, 0, 0, 0, 1}) == std::optional<fr::LocalAxis>(fr::kLocalUp));
    CHECK(fr::UpLocalAxis(kFish) == std::optional<fr::LocalAxis>(fr::kLocalUp));
    CHECK(fr::UpLocalAxis(kNapoleon) == std::optional<fr::LocalAxis>(kY));
    CHECK(fr::UpLocalAxis(kCoin) == std::optional<fr::LocalAxis>(kMinusZ));

    // 45 degrees off: none; 9 degrees off (a real spawner) still counts.
    CHECK_FALSE(fr::UpLocalAxis(Leaning45()));
    const auto r = static_cast<float>(9.0 * 3.14159265358979323846 / 180.0);
    CHECK(fr::UpLocalAxis(fr::WorldAxes {0, -std::sin(r), -std::cos(r), 0, std::cos(r), -std::sin(r), 1, 0, 0}) ==
          std::optional<fr::LocalAxis>(fr::LocalAxis {0, true}));

    // A zero or NaN axis: unread.
    CHECK_FALSE(fr::UpLocalAxis(fr::WorldAxes {}));
    CHECK_FALSE(fr::UpLocalAxis(fr::WorldAxes {1, 0, 0, 0, 1, 0, 0, 0, std::nanf("")}));

    CHECK(fr::LocalAxisText(kMinusZ) == "-Z");
    CHECK(fr::LocalAxisText(kY) == "+Y");
}

TEST_CASE("crate items: an item's rest is learned from its vanilla placements") {
    // Two stash placements (+Y up, 4 and 2 cm above the floor) and one elsewhere (+Y, height unknown) against one +Z.
    std::vector<fr::CrateRestSample> s {Sample(kNapoleon, true, 0.04),
                                        Sample(fr::WorldAxes {1, 0, 0, 0, 1, 0, 0, 0, 1}, true, 0.0),
                                        Sample(kNapoleon, true, 0.02), Sample(kNapoleon, std::nullopt, std::nullopt)};
    std::optional<fr::CrateRest> r = fr::LearnCrateRest(s);
    REQUIRE(r);
    CHECK(r->axis == kY);
    CHECK(r->source == fr::CrateRestSource::Learned);
    CHECK(r->agree == 3);
    CHECK(r->usable == 4);
    CHECK(r->samples == 4);
    REQUIRE(r->lift);
    CHECK(*r->lift == doctest::Approx(0.03)); // the median of the agreeing ones' heights

    // A spawner without placement attach, an unread one and one with no axis up are not counted.
    s = {Sample(kNapoleon, false, 0.04), Sample(std::nullopt, true, std::nullopt), Sample(Leaning45(), true, 0.0),
         Sample(kCoin, true, 0.03)};
    r = fr::LearnCrateRest(s);
    REQUIRE(r);
    CHECK(r->axis == kMinusZ);
    CHECK(r->agree == 1);
    CHECK(r->usable == 1);
    CHECK(r->samples == 4);
    CHECK(fr::DescribeCrateRest(*r) ==
          "-Z up (learned from the crate's own option for it: 1 of 1 agree, 3 more unread or not upright; pose and yaw "
          "of that option; 0.03 m above the floor)");

    // The pose: the first agreeing placement's whole rotation, and where it came from.
    REQUIRE(r->pose);
    CHECK(*r->pose == kCoin);
    CHECK(r->poseSample == 3);

    // A tie: the axis named first.
    s = {Sample(kCoin, true, std::nullopt), Sample(kNapoleon, true, std::nullopt)};
    r = fr::LearnCrateRest(s);
    REQUIRE(r);
    CHECK(r->axis == kMinusZ);
    CHECK_FALSE(r->lift);

    // Nothing usable: not learned.
    CHECK_FALSE(fr::LearnCrateRest(std::vector<fr::CrateRestSample> {}));
    s = {Sample(kNapoleon, false, std::nullopt)};
    CHECK_FALSE(fr::LearnCrateRest(s));
}

TEST_CASE("crate items: only the crate's own tool-slot options teach an item's rest") {
    using P = fr::CratePlacement;
    // Game data: the Electrical Keycard Hacker, two placements outside the crate, both +Y up (the supplier's stand):
    // not learned from, so it keeps the crate's flat +Z.
    const fr::CrateRestSample supplier {kNapoleon, true, std::nullopt, P::Other, "mission option helper 1, spawner 2"};
    std::vector<fr::CrateRestSample> s {supplier, supplier};
    CHECK_FALSE(fr::LearnCrateRest(s));
    fr::CrateRest r = fr::CrateRestFor(G(7), s);
    CHECK(r.source == fr::CrateRestSource::Default);
    CHECK(r.axis == fr::kLocalUp);
    CHECK(r.samples == 0);
    CHECK(r.others == 2);
    CHECK_FALSE(r.pose);
    CHECK(fr::DescribeCrateRest(r) ==
          "+Z up (no option of its own in the crate, not in the built-in table; lying flat as vanilla; no box in the "
          "game files (at the slot's middle; centered by its geometry once it spawns); 2 other placements not learned "
          "from; height not learned)");

    // Its own crate option outvoted by more placements elsewhere: the crate's option alone counts.
    const fr::WorldAxes icicleOwn {-0.03f, -1, 0, 1, -0.03f, 0, 0, 0, 1};
    s = {supplier,
         {icicleOwn, true, 0.0, P::ToolSlot, "crate slot 1 option 0fce7991058edbed, spawner b77691e22dd91fc8"},
         supplier};
    std::optional<fr::CrateRest> l = fr::LearnCrateRest(s);
    REQUIRE(l);
    CHECK(l->axis == fr::kLocalUp);
    CHECK(l->agree == 1);
    CHECK(l->usable == 1);
    CHECK(l->samples == 1);
    CHECK(l->others == 2);
    REQUIRE(l->pose);
    CHECK(*l->pose == icicleOwn);
    CHECK(l->poseSample == 1);
    CHECK(l->poseFrom == "crate slot 1 option 0fce7991058edbed, spawner b77691e22dd91fc8");
    CHECK(fr::DescribeCrateRest(*l) ==
          "+Z up (learned from the crate's own option for it: 1 of 1 agree; pose and yaw of crate slot 1 option "
          "0fce7991058edbed, spawner b77691e22dd91fc8; 2 other placements not learned from; 0.00 m above the floor)");

    // The crate's one-item option is not a tool slot either: a table item keeps its table rest.
    s = {{kCoin, true, 0.03, P::Other, "crate one-item option 3, spawner 4"}};
    r = fr::CrateRestFor(Parsed(kPouchFlash), s);
    CHECK(r.source == fr::CrateRestSource::Table);
    CHECK(r.axis == kY);
    CHECK(r.others == 1);
}

TEST_CASE("crate items: an item with no option of its own in the crate rests by the built-in table, else on +Z") {
    // Pouch Flash: on end on +Z, upside down on -Y: +Y.
    fr::CrateRest r = fr::CrateRestFallback(Parsed(kPouchFlash));
    CHECK(r.source == fr::CrateRestSource::Table);
    CHECK(r.axis == kY);
    CHECK_FALSE(r.lift);
    CHECK_FALSE(r.pose);
    REQUIRE(r.center); // its own box from the game files (magnesiumpouch's template): centered in the slot by it
    CHECK((*r.center)[2] == doctest::Approx(0.075));
    CHECK(fr::DescribeCrateRest(r).find("+Y up (no option of its own in the crate; the built-in table; centered by its "
                                        "box (center (+0.00, +0.00, ") == 0);
    CHECK(fr::DescribeCrateRest(r).find("m from its spawner, from its option template); height not learned)") !=
          std::string::npos);

    r = fr::CrateRestFallback(Parsed(kNapoleonFigure));
    CHECK(r.axis == kY);
    REQUIRE(r.lift);
    CHECK(*r.lift == doctest::Approx(0.04));

    r = fr::CrateRestFallback(Parsed(kCoinClassic));
    CHECK(r.axis == kMinusZ);

    // RubberDuck explosive (its attach a quarter turn of yaw: spawner +Y is its own X): on its side.
    CHECK(fr::CrateRestFallback(Parsed("0bc37bb7-dcd8-4348-a338-22fd8676a416")).axis == kY);

    // The Remote Explosive Devil RubberDuck (the Classic's model and attach) and the (Grenade) Emetic sit upright on
    // +Z; on their side, as the Classic duck.
    for (const char* item : {"7e52d861-481c-4f7c-87d2-6211d90586bf", "4ca96340-ae60-427b-a011-9e296cd67fd9",
                             "2bdf5016-e70b-4ac9-a3d5-f35b6743c09a", "67b6eb96-89c6-43ce-ba7a-526b092a55f9",
                             "4e6da33e-acc0-4f76-8158-c7b2cb7bb9b4", "b988422a-02a6-499e-b796-302a782be3d1"}) {
        const fr::CrateRest t = fr::CrateRestFallback(Parsed(item));
        CHECK(t.source == fr::CrateRestSource::Table);
        CHECK(t.axis == kY);
        CHECK_FALSE(t.lift);
    }

    // Anything else with no box in the game files (a level's loose item): lying as the flat options lay their own
    // items, as vanilla.
    r = fr::CrateRestFallback(G(5));
    CHECK(r.source == fr::CrateRestSource::Default);
    CHECK(r.axis == fr::kLocalUp);
    CHECK(fr::DescribeCrateRest(r).find("+Z up (no option of its own in the crate, not in the built-in table") == 0);

    // Its own crate option overrides the table; unusable ones leave it.
    const std::vector<fr::CrateRestSample> s {Sample(kCoin, true, std::nullopt)};
    CHECK(fr::CrateRestFor(Parsed(kPouchFlash), s).axis == kMinusZ);
    CHECK(fr::CrateRestFor(Parsed(kPouchFlash), s).source == fr::CrateRestSource::Learned);
    const std::vector<fr::CrateRestSample> u {Sample(std::nullopt, true, std::nullopt)};
    r = fr::CrateRestFor(Parsed(kPouchFlash), u);
    CHECK(r.source == fr::CrateRestSource::Table);
    CHECK(r.samples == 1);
    CHECK(fr::DescribeCrateRest(r).find("its 1 options in the crate were unreadable; the built-in table") !=
          std::string::npos);
}

TEST_CASE("crate items: the turn from an item's rest axis to a spawner's up axis") {
    CHECK(fr::TurnRestToUp(fr::kLocalUp, fr::kLocalUp) == fr::kNoTurn);

    for (const fr::LocalAxis rest : AllAxes()) {
        for (const fr::LocalAxis up : AllAxes()) {
            const fr::AxisTurn t = fr::TurnRestToUp(rest, up);
            std::array<int, 3> want {0, 0, 0};
            want[up.index] = up.negative ? -1 : 1;
            CHECK(Times(t, rest) == want);
            CHECK(Determinant(t) == 1); // a turn, never a mirror

            // The smallest: none, a half turn for opposite axes, else a quarter turn (trace 3, -1, 1).
            const int trace = t[0] + t[4] + t[8];
            CHECK(trace == (rest == up ? 3 : rest.index == up.index ? -1 : 1));

            // The inverse undoes it.
            CHECK(Times(fr::InverseTurn(t), up) == std::array<int, 3> {rest.index == 0 ? (rest.negative ? -1 : 1) : 0,
                                                                       rest.index == 1 ? (rest.negative ? -1 : 1) : 0,
                                                                       rest.index == 2 ? (rest.negative ? -1 : 1) : 0});
        }
    }
}

TEST_CASE("crate items: a spawner turned so the item's rest axis points up") {
    // A flat spawner, an item resting on +X: X up now, Y (across) kept, Z where -X was.
    const fr::WorldAxes identity {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const fr::WorldAxes turned = fr::TurnAxes(identity, fr::TurnRestToUp(kX, fr::kLocalUp));
    const fr::WorldAxes expected {0, 0, 1, 0, 1, 0, -1, 0, 0};

    for (size_t i = 0; i < 9; ++i)
        CHECK(turned[i] == doctest::Approx(expected[i]));

    // Every spawner up axis and every rest: afterwards the rest axis points up, the vanilla up axis's place kept.
    for (const fr::LocalAxis up : AllAxes()) {
        const fr::WorldAxes vanilla = fr::TurnAxes(identity, fr::TurnRestToUp(up, fr::kLocalUp));
        REQUIRE(fr::UpLocalAxis(vanilla) == std::optional<fr::LocalAxis>(up));

        for (const fr::LocalAxis rest : AllAxes())
            CHECK(fr::UpLocalAxis(fr::TurnAxes(vanilla, fr::TurnRestToUp(rest, up))) ==
                  std::optional<fr::LocalAxis>(rest));
    }

    // The yawed Fish spawner keeps its yaw: the axis across (Y) stays where it was.
    const fr::WorldAxes fish = fr::TurnAxes(kFish, fr::TurnRestToUp(kX, fr::kLocalUp));
    CHECK(fr::UpLocalAxis(fish) == std::optional<fr::LocalAxis>(kX));
    CHECK(fish[3] == doctest::Approx(kFish[3]));
    CHECK(fish[4] == doctest::Approx(kFish[4]));

    // Applied and undone.
    const fr::SpatialTransform v {kNapoleon, {-32.31f, 57.06f, 4.19f}};
    const fr::SpawnerTurn c {fr::TurnRestToUp(fr::kLocalUp, kY), -0.04};
    const fr::SpatialTransform a = fr::ApplySpawnerTurn(v, c);
    CHECK(fr::UpLocalAxis(a.axes) == std::optional<fr::LocalAxis>(fr::kLocalUp));
    CHECK(a.at[0] == doctest::Approx(-32.31));
    CHECK(a.at[2] == doctest::Approx(4.15));
    CHECK(fr::SameTransform(fr::UndoSpawnerTurn(a, c), v));
    CHECK_FALSE(fr::SameTransform(a, v));
}

TEST_CASE("crate items: a transform under its parent") {
    // Parent yawed 90 degrees at (10, 20, 5); the spawner under it turned +Y up at (1, 2, 0.04).
    const fr::SpatialTransform parent {{0, 1, 0, -1, 0, 0, 0, 0, 1}, {10, 20, 5}};
    const fr::SpatialTransform world {{0, 1, 0, 0, 0, 1, 1, 0, 0}, {8, 21, 5.04f}};
    const fr::SpatialTransform local = fr::LocalTransform(parent, world);
    CHECK(fr::SameTransform(local, fr::SpatialTransform {kNapoleon, {1, 2, 0.04f}}));

    // No parent (identity at the origin): the world transform itself.
    CHECK(fr::SameTransform(fr::LocalTransform(fr::SpatialTransform {}, world), world));

    // A parent scaled 2: the scale is divided out.
    const fr::SpatialTransform big {{0, 2, 0, -2, 0, 0, 0, 0, 2}, {10, 20, 5}};
    const fr::SpatialTransform bigWorld {{0, 2, 0, 0, 0, 2, 2, 0, 0}, {6, 22, 5.08f}};
    CHECK(fr::SameTransform(fr::LocalTransform(big, bigWorld), fr::SpatialTransform {kNapoleon, {1, 2, 0.04f}}));
}

TEST_CASE("crate items: the crate floor and a spawner's fit") {
    // The 17:59 crate: flat spawners at 4.15 (one at 4.16), Napoleon figure's at 4.19, Coin's (upside down) at 4.18.
    const fr::WorldAxes flat {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const std::vector<fr::SpatialTransform> stash {{flat, {0, 0, 4.15f}},  {kNapoleon, {0, 0, 4.19f}},
                                                   {flat, {0, 0, 4.15f}},  {kCoin, {0, 0, 4.18f}},
                                                   {kFish, {0, 0, 4.16f}}, {flat, {0, 0, 4.15f}}};
    const std::optional<double> floor = fr::CrateFloorHeight(stash);
    REQUIRE(floor);
    CHECK(*floor == doctest::Approx(4.15).epsilon(1e-6));
    CHECK_FALSE(fr::CrateFloorHeight(std::vector<fr::SpatialTransform> {{kNapoleon, {0, 0, 1}}}));

    // Pouch Flash (table: +Y, height not learned) on Fish's flat spawner: turned, 3 cm up, its yaw kept.
    const fr::CrateRest pouch = fr::CrateRestFallback(Parsed(kPouchFlash));
    fr::SpawnerFit f = fr::FitSpawner({kFish, {0, 0, 4.15f}}, floor, pouch);
    CHECK_FALSE(f.fits);
    CHECK_FALSE(f.posed);
    REQUIRE(f.up);
    CHECK(*f.up == fr::kLocalUp);
    CHECK(f.change.lift == doctest::Approx(fr::kCrateSideLiftMeters));
    const fr::SpatialTransform placed = fr::ApplySpawnerTurn({kFish, {0, 0, 4.15f}}, f.change);
    CHECK(fr::UpLocalAxis(placed.axes) == std::optional<fr::LocalAxis>(kY));
    CHECK(placed.axes[0] == doctest::Approx(kFish[0]));
    CHECK(placed.axes[1] == doctest::Approx(kFish[1]));
    CHECK(placed.at[2] == doctest::Approx(4.18));

    // An item lying flat (+Z, learned at the floor) in Napoleon figure's raised +Y option: turned and lowered.
    fr::CrateRest flatItem;
    flatItem.axis = fr::kLocalUp;
    flatItem.lift = 0.0;
    f = fr::FitSpawner({kNapoleon, {0, 0, 4.19f}}, floor, flatItem);
    CHECK(f.change.lift == doctest::Approx(-0.04));
    CHECK(fr::UpLocalAxis(fr::ApplySpawnerTurn({kNapoleon, {0, 0, 4.19f}}, f.change).axes) ==
          std::optional<fr::LocalAxis>(fr::kLocalUp));

    // A +Z item with no height learned goes to the floor too.
    flatItem.lift.reset();
    CHECK(fr::FitSpawner({kNapoleon, {0, 0, 4.19f}}, floor, flatItem).change.lift == doctest::Approx(-0.04));

    // The floor unknown: turned, the height left.
    f = fr::FitSpawner({kFish, {0, 0, 4.15f}}, std::nullopt, pouch);
    CHECK(f.change.turn != fr::kNoTurn);
    CHECK(f.change.lift == 0.0);

    // The Coin in its own option: fits, nothing changes.
    f = fr::FitSpawner({kCoin, {0, 0, 4.18f}}, floor, fr::CrateRestFallback(Parsed(kCoinClassic)));
    CHECK(f.fits);
    CHECK(f.change == fr::SpawnerTurn {});

    // No axis up: left vanilla (cannot be turned).
    f = fr::FitSpawner({Leaning45(), {0, 0, 4.15f}}, floor, pouch);
    CHECK_FALSE(f.up);
    CHECK_FALSE(f.fits);
    CHECK(f.change == fr::SpawnerTurn {});
}

TEST_CASE("crate items: a spawner turned to the pose of the item's own crate option") {
    const fr::WorldAxes identity {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const auto same = [](const fr::WorldAxes& a, const fr::WorldAxes& b) {
        for (size_t i = 0; i < 9; ++i) {
            if (std::fabs(a[i] - b[i]) > 0.1f)
                return false;
        }

        return true;
    };

    // The same rotation: no turn.
    CHECK(fr::TurnToPose(identity, identity) == std::optional<fr::AxisTurn>(fr::kNoTurn));
    CHECK(fr::TurnToPose(kFish, kFish) == std::optional<fr::AxisTurn>(fr::kNoTurn));

    // Every axis-swapping turn of a flat spawner is reached, and is a turn.
    for (const fr::LocalAxis rest : AllAxes()) {
        for (const fr::LocalAxis up : AllAxes()) {
            const fr::WorldAxes pose = fr::TurnAxes(kFish, fr::TurnRestToUp(rest, up));
            const std::optional<fr::AxisTurn> t = fr::TurnToPose(kFish, pose);
            REQUIRE(t);
            CHECK(Determinant(*t) == 1);
            CHECK(same(fr::TurnAxes(kFish, *t), pose));
        }
    }

    // The 23:11 crate: the Icicle's own slot 1 option (+Z up, yawed a quarter turn) and (Fiberwire) Phone Cord's, where
    // it lay a quarter turn counter-clockwise from it. The turn gives Phone Cord's spawner the Icicle's own pose.
    const fr::WorldAxes icicleOwn {-0.03f, -1, 0, 1, -0.03f, 0, 0, 0, 1};
    const fr::WorldAxes phoneCord {1, -0.09f, 0, 0.09f, 1, 0, 0, 0, 1};
    std::optional<fr::AxisTurn> t = fr::TurnToPose(phoneCord, icicleOwn);
    REQUIRE(t);
    CHECK(*t != fr::kNoTurn);
    CHECK(same(fr::TurnAxes(phoneCord, *t), icicleOwn));
    CHECK(fr::UpLocalAxis(fr::TurnAxes(phoneCord, *t)) == std::optional<fr::LocalAxis>(fr::kLocalUp));

    // Napoleon figure's pose (+Y up) onto a flat spawner.
    t = fr::TurnToPose(identity, kNapoleon);
    REQUIRE(t);
    CHECK(same(fr::TurnAxes(identity, *t), kNapoleon));

    // 45 degrees off, a mirror, or a zero / NaN axis: no such turn.
    CHECK_FALSE(fr::TurnToPose(identity, Leaning45()));
    CHECK_FALSE(fr::TurnToPose(identity, fr::WorldAxes {-1, 0, 0, 0, 1, 0, 0, 0, 1}));
    CHECK_FALSE(fr::TurnToPose(identity, fr::WorldAxes {}));
    CHECK_FALSE(fr::TurnToPose(fr::WorldAxes {1, 0, 0, 0, 1, 0, 0, 0, std::nanf("")}, identity));

    // FitSpawner with the pose: the rest axis and the yaw; height to the learned lift.
    const std::optional<double> floor = 109.15;
    fr::CrateRest icicle;
    icicle.axis = fr::kLocalUp;
    icicle.lift = 0.0;
    icicle.source = fr::CrateRestSource::Learned;
    icicle.pose = icicleOwn;
    fr::SpawnerFit f = fr::FitSpawner({phoneCord, {0, 0, 109.17f}}, floor, icicle);
    CHECK(f.posed);
    CHECK_FALSE(f.fits);
    CHECK(f.change.turn == *fr::TurnToPose(phoneCord, icicleOwn));
    CHECK(f.change.lift == doctest::Approx(-0.02));

    // Its own option: has the pose already, left vanilla.
    f = fr::FitSpawner({icicleOwn, {0, 0, 109.15f}}, floor, icicle);
    CHECK(f.posed);
    CHECK(f.fits);
    CHECK(f.change == fr::SpawnerTurn {});

    // Without the pose (axis only) the same +Z spawner fits: the yaw is left as the option has it.
    icicle.pose.reset();
    f = fr::FitSpawner({phoneCord, {0, 0, 109.17f}}, floor, icicle);
    CHECK_FALSE(f.posed);
    CHECK(f.fits);

    // A pose no turn reaches (the spawner 45 degrees off): the axis rule, which cannot turn it.
    icicle.pose = icicleOwn;
    f = fr::FitSpawner({Leaning45(), {0, 0, 109.15f}}, floor, icicle);
    CHECK_FALSE(f.posed);
    CHECK_FALSE(f.up);
    CHECK(f.change == fr::SpawnerTurn {});
}

TEST_CASE("crate items: an item goes to its own spot in the slot, else to the slot's middle") {
    // The middle: the mean of the slot's vanilla spawners; a NaN one is skipped.
    const fr::WorldAxes flat {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::vector<fr::SpatialTransform> slot {
        {flat, {0, 0, 1}}, {flat, {2, 0, 1}}, {flat, {1, 3, 1}}, {flat, {std::nanf(""), 0, 1}}};
    std::optional<fr::WorldPoint> middle = fr::CrateSlotMiddle(slot);
    REQUIRE(middle);
    CHECK((*middle)[0] == doctest::Approx(1.0));
    CHECK((*middle)[1] == doctest::Approx(1.0));
    CHECK((*middle)[2] == doctest::Approx(1.0));
    CHECK_FALSE(fr::CrateSlotMiddle(std::vector<fr::SpatialTransform> {}));

    // Game data: a Mendoza crate, simplified: the crate yawed a quarter turn (its X across the slots,
    // the player on its -X side, world +Y), slot 2's middle at (-11.80, -62.95, 6.24) on the floor. Bust_Colombi's
    // option stands +Y up 14 cm nearer the player and 4 cm up; the Devil duck and the Rusty Nail lay too near the
    // player in it.
    const fr::WorldAxes crate {0, -1, 0, 1, 0, 0, 0, 0, 1};
    const fr::WorldAxes bust {1, 0, 0, 0, 0, 1, 0, -1, 0};
    const fr::SpatialTransform bustSpawner {bust, {-11.80f, -62.81f, 6.28f}};
    const fr::WorldPoint slot2 {-11.80, -62.95, 6.24};
    const std::optional<double> floor = 6.24;

    // The duck (the table: +Y, no option of its own): the option's rotation and height fit, it goes to the middle.
    const fr::CrateRest duck = fr::CrateRestFallback(Parsed("7e52d861-481c-4f7c-87d2-6211d90586bf"));
    fr::SpawnerFit f = fr::FitSpawner(bustSpawner, floor, duck, slot2);
    CHECK_FALSE(f.fits);
    CHECK_FALSE(f.posed);
    CHECK_FALSE(f.ownSpot);
    CHECK(f.change.turn == fr::kNoTurn);
    CHECK(f.change.lift == 0.0);
    fr::SpatialTransform placed = fr::ApplySpawnerTurn(bustSpawner, f.change);
    CHECK(placed.at[0] == doctest::Approx(-11.80));
    CHECK(placed.at[1] == doctest::Approx(-62.95));
    CHECK(placed.at[2] == doctest::Approx(6.28));
    CHECK(fr::SameTransform(fr::UndoSpawnerTurn(placed, f.change), bustSpawner));

    // The Rusty Nail: its own slot 2 option lies flat 2 cm along the slot from the middle. It gets that option's
    // pose, height and spot.
    fr::CrateRest nail;
    nail.axis = fr::kLocalUp;
    nail.lift = 0.0;
    nail.source = fr::CrateRestSource::Learned;
    nail.pose = crate;
    nail.offset = fr::WorldPoint {0.02, 0.0, 0.0};
    f = fr::FitSpawner(bustSpawner, floor, nail, slot2);
    CHECK(f.posed);
    CHECK(f.ownSpot);
    CHECK(f.change.lift == doctest::Approx(-0.04));
    placed = fr::ApplySpawnerTurn(bustSpawner, f.change);
    CHECK(fr::SameTransform(placed, fr::SpatialTransform {crate, {-11.78f, -62.95f, 6.24f}}));
    CHECK(fr::SameTransform(fr::UndoSpawnerTurn(placed, f.change), bustSpawner));
    CHECK(fr::DescribeCrateRest(nail) ==
          "+Z up (learned from the crate's own option for it: 0 of 0 agree; pose and yaw of that option, its spawner "
          "(+0.02, +0.00) m from its slot's middle; 0.00 m above the floor)");

    // The next crate stands half a turn round: the same change moves the spawner the same way in the crate (the
    // move is kept along the spawner's own axes), and undoes right there.
    const auto halfTurn = [](const fr::SpatialTransform& t) {
        fr::SpatialTransform r = t;

        for (size_t i = 0; i < 3; ++i) {
            r.axes[3 * i] = -t.axes[3 * i];
            r.axes[3 * i + 1] = -t.axes[3 * i + 1];
        }

        r.at = {-t.at[0], -t.at[1], t.at[2]};
        return r;
    };
    const fr::SpatialTransform turnedCrate = halfTurn(bustSpawner);
    const fr::SpatialTransform there = fr::ApplySpawnerTurn(turnedCrate, f.change);
    CHECK(fr::SameTransform(there, halfTurn(placed)));
    CHECK(fr::SameTransform(fr::UndoSpawnerTurn(there, f.change), turnedCrate));

    // Its own option, in its own slot: nothing changes. A move under 5 mm is left out.
    f = fr::FitSpawner({crate, {-11.78f, -62.95f, 6.24f}}, floor, nail, slot2);
    CHECK(f.fits);
    CHECK(f.change == fr::SpawnerTurn {});
    f = fr::FitSpawner({crate, {-11.777f, -62.95f, 6.24f}}, floor, nail, slot2);
    CHECK(f.fits);

    // Without the slot's middle the spot is left (the height and the turn as before).
    f = fr::FitSpawner(bustSpawner, floor, nail);
    CHECK(f.change.slide == std::array<double, 3> {});
    CHECK_FALSE(f.ownSpot);

    // A posed item with no offset (its option's slot middle unread) goes to the middle.
    nail.offset.reset();
    f = fr::FitSpawner(bustSpawner, floor, nail, slot2);
    CHECK_FALSE(f.ownSpot);
    placed = fr::ApplySpawnerTurn(bustSpawner, f.change);
    CHECK(placed.at[0] == doctest::Approx(-11.80));
    CHECK(placed.at[1] == doctest::Approx(-62.95));
}

TEST_CASE("crate items: the twins are quarter turns between items the game files pair") {
    const char* kRemoteTaze = "021ed731-eebc-400a-9658-8f6fc5af9da6";
    const char* kProximityTaser = "0209f0b7-f6de-45c2-a730-4802abe35a75";

    std::optional<fr::CrateTwin> t = fr::CrateTwinOf(Parsed(kRemoteTaze));
    REQUIRE(t);
    CHECK(t->twin == Parsed(kProximityTaser));
    CHECK(t->turn == fr::AxisTurn {0, 1, 0, -1, 0, 0, 0, 0, 1});

    // ICA Device Remote Concussion: the same twin and turn; the Keycard Hacker the Disposable Scrambler's, unturned.
    t = fr::CrateTwinOf(Parsed("8afed6c5-a730-4f47-b02c-1e4608f2ae81"));
    REQUIRE(t);
    CHECK(t->twin == Parsed(kProximityTaser));
    t = fr::CrateTwinOf(Parsed("6dadf34c-00f2-43f9-b4e8-6763502aa2c8"));
    REQUIRE(t);
    CHECK(t->twin == Parsed("b970a355-4296-4acc-9ec9-584e69a79eed"));
    CHECK(t->turn == fr::kNoTurn);

    // The RubberDuck explosives: the crate's Proximity Explosive RubberDuck.
    for (const char* duck : {"0bc37bb7-dcd8-4348-a338-22fd8676a416", "7e52d861-481c-4f7c-87d2-6211d90586bf",
                             "2bdf5016-e70b-4ac9-a3d5-f35b6743c09a", "67b6eb96-89c6-43ce-ba7a-526b092a55f9",
                             "4e6da33e-acc0-4f76-8158-c7b2cb7bb9b4"}) {
        t = fr::CrateTwinOf(Parsed(duck));
        REQUIRE(t);
        CHECK(t->twin == Parsed("4ca96340-ae60-427b-a011-9e296cd67fd9"));
    }

    // A crate item (its own option teaches it) and an unknown one: none. The twin itself has none.
    CHECK_FALSE(fr::CrateTwinOf(Parsed(kProximityTaser)));
    CHECK_FALSE(fr::CrateTwinOf(G(5)));

    // Every turn is a turn: each row and column one +-1, determinant 1.
    for (const char* item :
         {kRemoteTaze, "8afed6c5-a730-4f47-b02c-1e4608f2ae81", "8e77e2c5-caa6-45fe-8594-6a75c21569ec",
          "8b114fce-586b-4b06-b446-75d0bb4a4cfb", "b988422a-02a6-499e-b796-302a782be3d1"}) {
        t = fr::CrateTwinOf(Parsed(item));
        REQUIRE(t);
        CHECK(Determinant(t->turn) == 1);

        for (size_t k = 0; k < 3; ++k) {
            CHECK(std::abs(t->turn[3 * k]) + std::abs(t->turn[3 * k + 1]) + std::abs(t->turn[3 * k + 2]) == 1);
            CHECK(std::abs(t->turn[k]) + std::abs(t->turn[3 + k]) + std::abs(t->turn[6 + k]) == 1);
        }
    }
}

TEST_CASE("crate items: Remote Taze lies as its twin, the Proximity Taser, turned to its own axes") {
    using P = fr::CratePlacement;
    const char* kRemoteTaze = "021ed731-eebc-400a-9658-8f6fc5af9da6";

    // Game data: a Mendoza crate: the crate square to the world, the floor at -13.25. The Proximity Taze
    // Mine's own slot 3 option lies +Z up, its X along the crate's Y, 2.4 cm up; (Grenade) Emetic Gas's slot 2
    // option, where Remote Taze was written, stands -X up 5 cm above the floor.
    const fr::WorldAxes taser {0.02f, 1, 0, -1, 0.02f, 0, 0, 0, 1};
    const fr::WorldAxes emeticGas {0, 0, -1, -1, 0.02f, 0, 0.02f, 1, 0};
    const std::optional<double> floor = -13.25;

    // No option of its own: learned from its twin's, the pose turned a quarter turn about up.
    const std::vector<fr::CrateRestSample> own {};
    const std::vector<fr::CrateRestSample> twin {
        {taser, true, 0.024, P::ToolSlot, "crate slot 3 option c9e5fb4a0dfc8c07, spawner b77691e22dd91fc8"}};
    fr::CrateRest r = fr::CrateRestFor(Parsed(kRemoteTaze), own, twin, "ICA Proximity Taser");
    CHECK(r.source == fr::CrateRestSource::Twin);
    CHECK(r.axis == fr::kLocalUp);
    // A turned twin is another model: not the twin's 2.4 cm (at the floor it lies right).
    CHECK_FALSE(r.lift);
    CHECK_FALSE(r.offset);
    CHECK(r.poseSample == 0);
    REQUIRE(r.pose);

    // Its X along the crate's X now (where Grenade Emetic Gas's option had turned it along Y: a quarter turn
    // counter-clockwise), its top up.
    CHECK((*r.pose)[0] == doctest::Approx(1.0));
    CHECK((*r.pose)[1] == doctest::Approx(-0.02).epsilon(0.01));
    CHECK((*r.pose)[4] == doctest::Approx(1.0));
    CHECK((*r.pose)[8] == doctest::Approx(1.0));
    CHECK(fr::DescribeCrateRest(r) ==
          "+Z up (no option of its own in the crate; laid as its twin ICA Proximity Taser (the same box in the game "
          "files), learned from the crate's own option for the twin: 1 of 1 agree; pose and yaw of crate slot 3 option "
          "c9e5fb4a0dfc8c07, spawner b77691e22dd91fc8, turned to the item's axes; another model (turned): not the "
          "twin's height; height not learned)");

    // Grenade Emetic Gas's spawner gets that pose, lying flat at the floor.
    fr::SpawnerFit f = fr::FitSpawner({emeticGas, {3.82f, -24.11f, -13.20f}}, floor, r);
    CHECK(f.posed);
    CHECK(f.change.lift == doctest::Approx(-0.05));
    const fr::SpatialTransform placed = fr::ApplySpawnerTurn({emeticGas, {3.82f, -24.11f, -13.20f}}, f.change);
    CHECK(fr::UpLocalAxis(placed.axes) == std::optional<fr::LocalAxis>(fr::kLocalUp));
    CHECK(placed.axes[0] == doctest::Approx(1.0).epsilon(0.03));
    CHECK(placed.at[2] == doctest::Approx(-13.25));

    // Its own option in the crate wins over the twin.
    const std::vector<fr::CrateRestSample> mine {{kNapoleon, true, 0.04, P::ToolSlot, "crate slot 1 option a"}};
    CHECK(fr::CrateRestFor(Parsed(kRemoteTaze), mine, twin, "ICA Proximity Taser").source ==
          fr::CrateRestSource::Learned);

    // The twin's option unread: Remote Taze is not in the table, so flat by its box (thin along Z, as vanilla); a duck
    // keeps its table rest.
    const std::vector<fr::CrateRestSample> unread {{std::nullopt, true, std::nullopt, P::ToolSlot, "crate slot 3"}};
    r = fr::CrateRestFor(Parsed(kRemoteTaze), own, unread, "ICA Proximity Taser");
    CHECK(r.source == fr::CrateRestSource::Box);
    CHECK(r.axis == fr::kLocalUp);
    r = fr::CrateRestFor(Parsed("7e52d861-481c-4f7c-87d2-6211d90586bf"), own, unread, "x");
    CHECK(r.source == fr::CrateRestSource::Table);
    CHECK(r.axis == kY);

    // An item without a twin ignores twin samples.
    CHECK(fr::CrateRestFor(G(5), own, twin, "x").source == fr::CrateRestSource::Default);
}

TEST_CASE("crate items: a twin's rest axis in the item's axes") {
    // Without a pose the twin's rest axis is carried over by the turn: the Keycard Hacker MkIII's turn takes the
    // twin's +Z to the item's -Y.
    fr::CrateRest twin;
    twin.axis = fr::kLocalUp;
    twin.lift = 0.0;
    fr::CrateRest r = fr::TwinCrateRest(twin, fr::AxisTurn {1, 0, 0, 0, 0, 1, 0, -1, 0}, "Disposable Scrambler");
    CHECK(r.source == fr::CrateRestSource::Twin);
    CHECK(r.axis == fr::LocalAxis {1, true});
    CHECK(r.twin == "Disposable Scrambler");
    CHECK_FALSE(r.pose);
    CHECK_FALSE(r.lift); // turned: another model, not the twin's height
    CHECK_FALSE(r.offset);

    // With a pose: the turned pose names the axis up, and agrees with the carried one.
    twin.pose = kFish;
    r = fr::TwinCrateRest(twin, fr::AxisTurn {1, 0, 0, 0, 0, 1, 0, -1, 0}, "Disposable Scrambler");
    REQUIRE(r.pose);
    CHECK(fr::UpLocalAxis(*r.pose) == std::optional<fr::LocalAxis>(fr::LocalAxis {1, true}));
    CHECK(r.axis == fr::LocalAxis {1, true});

    // Unturned (the same model): the twin's own rest and height, never its spot.
    twin.axis = kMinusZ;
    twin.pose = kCoin;
    twin.lift = 0.022;
    twin.offset = fr::WorldPoint {0.01, 0.07, 0.0};
    r = fr::TwinCrateRest(twin, fr::kNoTurn, "Coin");
    CHECK(r.axis == kMinusZ);
    CHECK(*r.pose == kCoin);
    REQUIRE(r.lift);
    CHECK(*r.lift == doctest::Approx(0.022));
    CHECK_FALSE(r.offset);
}

TEST_CASE("crate items: Remote Taze and the Remote Concussion Device centered in the slot, yaw and height kept") {
    using P = fr::CratePlacement;

    // Game data: a Mendoza crate (Iconoclast), the crate square to the world (its slots along X, the player
    // on its -Y side), the floor at -13.25. The tested build laid Remote Taze flat on Crystal Ball's slot 2 option,
    // its spawner moved to the slot's middle: angle right, "slightly too low" (nearer the player). Slot 2's middle
    // (its 25 spawners' mean) lies 3.7 cm nearer the player than its 25 boxes' centers (crate_poses.py: spawners
    // (-0.008, -0.044), boxes (-0.005, -0.007) in the slot).
    const fr::WorldAxes flat {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const fr::WorldPoint middle {3.820, -24.000, -13.25};
    const fr::WorldPoint boxMiddle {3.824, -23.963, -13.25};
    const fr::SpatialTransform crystalBall {flat, {3.809f, -23.973f, -13.25f}};
    const std::optional<double> floor = -13.25;

    // Its twin, the Proximity Taze Mine's own slot 3 option: +Z up, yawed a quarter turn (its X along the slot's Y),
    // 2.4 cm up; its box centered on its spawner, 1.3 cm up.
    const fr::WorldAxes taser {0, 1, 0, -1, 0, 0, 0, 0, 1};
    const std::vector<fr::CrateRestSample> twin {{taser, true, 0.024, P::ToolSlot, "crate slot 3 option"}};

    for (const char* item : {"021ed731-eebc-400a-9658-8f6fc5af9da6", "8afed6c5-a730-4f47-b02c-1e4608f2ae81"}) {
        fr::CrateRest r = fr::CrateRestFor(Parsed(item), {}, twin, "ICA Proximity Taser");
        REQUIRE(r.source == fr::CrateRestSource::Twin);
        r.center = fr::TurnPoint({0, 0, 0.013}, fr::CrateTwinOf(Parsed(item))->turn); // as the game part reads it
        REQUIRE(r.center);
        CHECK((*r.center)[2] == doctest::Approx(0.013));

        // The twin's pose turned to the item's lies as the tested build laid it on that option (no turn: the yaw
        // was right), at the floor (no height change), centered: 3.7 cm farther from the player than the middle.
        fr::SpawnerFit f = fr::FitSpawner(crystalBall, floor, r, middle, boxMiddle);
        CHECK(f.posed);
        CHECK(f.centered);
        CHECK_FALSE(f.ownSpot);
        CHECK(f.change.turn == fr::kNoTurn);
        CHECK(f.change.lift == 0.0);
        const fr::SpatialTransform placed = fr::ApplySpawnerTurn(crystalBall, f.change);
        CHECK(placed.at[0] == doctest::Approx(3.824));
        CHECK(placed.at[1] == doctest::Approx(-23.963));
        CHECK(placed.at[2] == doctest::Approx(-13.25));

        // The tested build's spot, for comparison: the slot's middle (no box middle known, or no center).
        f = fr::FitSpawner(crystalBall, floor, r, middle);
        CHECK_FALSE(f.centered);
        CHECK(fr::ApplySpawnerTurn(crystalBall, f.change).at[1] == doctest::Approx(-24.000));
    }

    // An item with no twin and no option template in the game files keeps the slot's middle: no center to place by.
    const fr::CrateRest plain = fr::CrateRestFallback(G(5));
    CHECK_FALSE(plain.center);
    const fr::SpawnerFit f = fr::FitSpawner(crystalBall, floor, plain, middle, boxMiddle);
    CHECK_FALSE(f.centered);
    CHECK(fr::ApplySpawnerTurn(crystalBall, f.change).at[1] == doctest::Approx(-24.000));
}

TEST_CASE("crate items: an item with neither its own option nor a twin is centered by its own box") {
    using P = fr::CratePlacement;
    const char* kProximityS3 = "fb1c7db4-2a41-4f3f-a17d-e93b205de481";

    // The game files' box centers (crate_poses.py --boxes), in the spawner's axes: the Proximity Explosive S3's 4 mm
    // off its spawner, Sieger 300 Advanced's (its Boundary beside the helper, not under its pose) 6 cm along its X.
    std::optional<std::array<double, 3>> c = fr::CrateItemBoxCenter(Parsed(kProximityS3));
    REQUIRE(c);
    CHECK((*c)[0] == doctest::Approx(0.0));
    CHECK((*c)[1] == doctest::Approx(0.004));
    CHECK((*c)[2] == doctest::Approx(0.008));
    c = fr::CrateItemBoxCenter(Parsed("907e0277-7806-42a4-b4b2-338cf8dd9391"));
    REQUIRE(c);
    CHECK((*c)[0] == doctest::Approx(-0.060));
    CHECK_FALSE(fr::CrateItemBoxCenter(G(5)));

    // Game data: a Hokkaido crate, the crate square to the world here (its slots along X, the player on its -Y
    // side), the floor at -4.70. The S3 was written into Rusty Metal Rod's Large items option (+Z up, yawed half a
    // turn, at the slot's origin) and lay "toward the player and to the right": flat as vanilla, its spawner moved
    // 4 cm to the slot's middle. The Large items slot in the game files: its 18 spawners' mean (0.007, -0.038), its
    // boxes' (0.005, 0.009) from the slot's origin.
    const fr::WorldAxes rod {-1, 0, 0, 0, -1, 0, 0, 0, 1};
    const fr::SpatialTransform host {rod, {0, 0, -4.70f}};
    const fr::WorldPoint middle {0.007, -0.038, -4.70};
    const fr::WorldPoint boxMiddle {0.005, 0.009, -4.70};
    const std::optional<double> floor = -4.70;

    // Two placements at the supplier (-Y up on its wall): not learned from. Flat as vanilla, its own box center.
    const fr::CrateRestSample supplier {fr::WorldAxes {1, 0, 0, 0, 0, -1, 0, 1, 0}, true, std::nullopt, P::Other,
                                        "mission option helper 823ec45f9241a05e, spawner b77691e22dd91fc8"};
    const fr::CrateRest r =
        fr::CrateRestFor(Parsed(kProximityS3), std::vector<fr::CrateRestSample> {supplier, supplier});
    CHECK(r.source == fr::CrateRestSource::Box); // its box (12.8 x 13.4 x 1.6 cm) is thin along Z: flat, as vanilla
    CHECK(r.axis == fr::kLocalUp);
    REQUIRE(r.center);
    CHECK(fr::DescribeCrateRest(r) ==
          "+Z up (no option of its own in the crate, not in the built-in table; on its widest face by its box in the "
          "game files (0.128 x 0.134 x 0.016 m along its spawner's axes); centered by its box (center (+0.00, +0.00, "
          "+0.01) m from its spawner, from its option template); 2 other placements not learned from; height not "
          "learned)");

    // Its pose as before (+Z, the host's yaw, at the floor); its box center on the slot's box middle: 5.1 cm farther
    // from the player and 0.2 cm left of the slot's middle where it lay.
    fr::SpawnerFit f = fr::FitSpawner(host, floor, r, middle, boxMiddle);
    CHECK(f.centered);
    CHECK_FALSE(f.posed);
    CHECK_FALSE(f.ownSpot);
    CHECK(f.change.turn == fr::kNoTurn);
    CHECK(f.change.lift == 0.0);
    const fr::SpatialTransform placed = fr::ApplySpawnerTurn(host, f.change);
    CHECK(placed.at[0] == doctest::Approx(0.005));
    CHECK(placed.at[1] == doctest::Approx(0.013)); // the box 4 mm along its Y: toward the player, the host yawed
    CHECK(placed.at[2] == doctest::Approx(-4.70));
    const fr::WorldPoint box = fr::PointFromSpawner(placed, *r.center);
    CHECK(box[0] == doctest::Approx(0.005));
    CHECK(box[1] == doctest::Approx(0.009));
    CHECK(fr::SameTransform(fr::UndoSpawnerTurn(placed, f.change), host));

    // The selector box goes on it too (crate_even_boxes): the item's box center from the spawner as placed.
    const fr::WorldPoint selector = fr::CrateBoxCenter(host, placed, fr::WorldPoint {-0.011, -0.024, -4.67}, r.center);
    CHECK(selector[0] == doctest::Approx(0.005));
    CHECK(selector[1] == doctest::Approx(0.009));

    // The tested build: the slot's middle (no box middle read).
    f = fr::FitSpawner(host, floor, r, middle);
    CHECK_FALSE(f.centered);
    CHECK(fr::ApplySpawnerTurn(host, f.change).at[1] == doctest::Approx(-0.038));

    // An item with its own option keeps its own spot (no center: the confirmed ones, the Baseball, the Muffin, ...);
    // a twin's center is its twin's box, read in game (none from the table).
    const fr::WorldAxes flat {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const std::vector<fr::CrateRestSample> own {{flat, true, 0.0, P::ToolSlot, "crate slot 2 option a"}};
    CHECK_FALSE(fr::CrateRestFor(Parsed("ce633778-7424-4784-8bc2-f9d717a23709"), own).center);
    const std::vector<fr::CrateRestSample> twin {{flat, true, 0.024, P::ToolSlot, "crate slot 3 option"}};
    const fr::CrateRest taze = fr::CrateRestFor(Parsed("021ed731-eebc-400a-9658-8f6fc5af9da6"), {}, twin, "taser");
    CHECK(taze.source == fr::CrateRestSource::Twin);
    CHECK_FALSE(taze.center);

    // A table item is centered by its own box as well, carried by its spawner's turn: the Pouch Flash (+Y up) on a flat
    // host, its box 7.5 cm along its Z, which now lies along the floor.
    const fr::CrateRest pouch = fr::CrateRestFallback(Parsed("f04bd2e9-3f3a-42cd-9150-b7845e952d67"));
    REQUIRE(pouch.center);
    const fr::SpatialTransform flatHost {flat, {0, 0, -4.70f}};
    f = fr::FitSpawner(flatHost, floor, pouch, middle, boxMiddle);
    CHECK(f.centered);
    const fr::SpatialTransform pouchAt = fr::ApplySpawnerTurn(flatHost, f.change);
    const fr::WorldPoint pouchBox = fr::PointFromSpawner(pouchAt, *pouch.center);
    CHECK(pouchBox[0] == doctest::Approx(0.005).epsilon(0.001));
    CHECK(pouchBox[1] == doctest::Approx(0.009).epsilon(0.001));
    CHECK(std::hypot(pouchAt.at[0] - 0.005, pouchAt.at[1] - 0.009) == doctest::Approx(0.075).epsilon(0.002));
}

TEST_CASE("crate items: the ducks and the emetic grenade lie with their top away from the player") {
    using P = fr::CratePlacement;

    // Game data, a Mendoza crate: the table laid the Devil duck (Crystal Ball's slot 2 option) and the
    // (Grenade) Emetic (Explosive Golf Ball's slot 1 option) by the table, +Y up: a quarter turn about the host's X,
    // its Z (the item's top: the duck's head, the grenade's cap) toward the player, "exactly upside down". The crate
    // square to the world here, the player on its -Y side; both hosts flat, yaw 0.
    const fr::WorldAxes flat {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const fr::SpatialTransform host {flat, {0, 0, 0}};
    const std::optional<double> floor = 0.0;
    const fr::WorldPoint middle {0, 0, 0};

    struct Case {
        const char* item;
        fr::WorldAxes twinAxes; // the twin's own crate option (crate_poses.py)
        double twinLift;
        std::array<double, 3> twinCenter; // its box center from its spawner, in its axes
    };
    // The Proximity Explosive RubberDuck: -Y up, yaw 0, at the floor, its box 5 cm along its Z. The Concussion
    // Grenade: -X up, its Y at 180 degrees, 2.2 cm up, its box 6.1 cm along its Z.
    const Case cases[] {
        {"7e52d861-481c-4f7c-87d2-6211d90586bf", {1, 0, 0, 0, 0, -1, 0, 1, 0}, 0.0, {0, 0, 0.05}},
        {"b988422a-02a6-499e-b796-302a782be3d1", {0, 0, -1, -1, 0, 0, 0, 1, 0}, 0.022, {0, -0.008, 0.061}},
    };

    for (const Case& c : cases) {
        // The tested build: the table's +Y, its Z toward the player.
        const fr::SpawnerFit table = fr::FitSpawner(host, floor, fr::CrateRestFallback(Parsed(c.item)), middle);
        const fr::SpatialTransform tested = fr::ApplySpawnerTurn(host, table.change);
        CHECK(tested.axes[7] == doctest::Approx(-1.0)); // Z: (0, -1, 0)

        // Now its twin's pose: the same models as the game lays them, Z away from the player; still on its side.
        const std::vector<fr::CrateRestSample> twin {{c.twinAxes, true, c.twinLift, P::ToolSlot, "crate slot 3"}};
        fr::CrateRest r = fr::CrateRestFor(Parsed(c.item), {}, twin, "twin");
        REQUIRE(r.source == fr::CrateRestSource::Twin);
        REQUIRE(r.lift); // unturned: the same model, the twin's height
        CHECK(*r.lift == doctest::Approx(c.twinLift));
        r.center = c.twinCenter;
        const fr::SpawnerFit f = fr::FitSpawner(host, floor, r, middle, fr::WorldPoint {0, 0.037, 0});
        CHECK(f.posed);
        const fr::SpatialTransform placed = fr::ApplySpawnerTurn(host, f.change);
        CHECK(placed.axes[6] == doctest::Approx(0.0)); // Z: (0, 1, 0), the toward-player end turned away
        CHECK(placed.axes[7] == doctest::Approx(1.0));
        CHECK(placed.axes[8] == doctest::Approx(0.0));
        CHECK(fr::UpLocalAxis(placed.axes) != std::optional<fr::LocalAxis>(fr::kLocalUp));
        CHECK(placed.at[2] == doctest::Approx(c.twinLift));

        // Centered: its box center (along its Z) on the slot's box middle.
        CHECK(f.centered);
        const fr::WorldPoint box = fr::PointFromSpawner(placed, c.twinCenter);
        CHECK(box[0] == doctest::Approx(0.0));
        CHECK(box[1] == doctest::Approx(0.037));
    }
}

TEST_CASE("crate items: a slot's box middle, where its items sit") {
    const fr::WorldAxes flat {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const std::vector<fr::SpatialTransform> boxes {
        {flat, {0.0, 0.03, 0.05}}, {flat, {0.02, 0.05, 0.01}}, {flat, {std::nanf(""), 0, 0}}};
    std::optional<fr::WorldPoint> m = fr::CrateSlotBoxMiddle(boxes, fr::WorldPoint {0.0, 0.0, 6.24});
    REQUIRE(m);
    CHECK((*m)[0] == doctest::Approx(0.01));
    CHECK((*m)[1] == doctest::Approx(0.04));
    CHECK((*m)[2] == doctest::Approx(6.24)); // the spawners' middle's height
    m = fr::CrateSlotBoxMiddle(boxes, std::nullopt);
    REQUIRE(m);
    CHECK((*m)[2] == doctest::Approx(0.03));

    // Not of the same read (the boxes elsewhere than the spawners), or none: empty.
    CHECK_FALSE(fr::CrateSlotBoxMiddle(boxes, fr::WorldPoint {1.0, 0.0, 0.0}));
    CHECK_FALSE(fr::CrateSlotBoxMiddle(std::vector<fr::SpatialTransform> {}, fr::WorldPoint {}));
}

TEST_CASE("crate items: heights from each slot's own floor in a crate read tilted") {
    using P = fr::CratePlacement;

    // Game data, a Mendoza crate opened after another: the transforms read at its open were where the stash stood in crate #3, tilted 2
    // degrees along the slots (its X 0.03 down). Slot 1 (Small lethal) stands 0.46 m along X from slot 3 (Large),
    // slot 2 0.52 m the other way: slot 1's floor 1.4 cm lower, slot 2's 1.6 cm higher.
    const fr::WorldAxes tilted {1, 0, -0.03f, 0, 1, 0, 0.03f, 0, 1};
    const auto at = [&](double x) {
        return fr::SpatialTransform {tilted, {static_cast<float>(x), 0, static_cast<float>(-0.03 * x)}};
    };
    const std::vector<fr::SpatialTransform> slot1 {at(0.44), at(0.46), at(0.48)};
    const std::vector<fr::SpatialTransform> slot3 {at(-0.01), at(0.01)};
    const std::vector<fr::SpatialTransform> slot2 {at(-0.50), at(-0.52), at(-0.54)};
    std::vector<fr::SpatialTransform> all = slot1;
    all.insert(all.end(), slot3.begin(), slot3.end());
    all.insert(all.end(), slot2.begin(), slot2.end());

    // The Kitchen Knife's own slot 1 option lies flat at its floor (crate_poses.py: 0.000); it is written into
    // Iron's slot 3 option, flat at its floor too.
    const fr::SpatialTransform knife = at(0.46);
    const fr::SpatialTransform iron = at(0.0);
    const auto knifeRest = [&](std::optional<double> knifeFloor) {
        const std::vector<fr::CrateRestSample> s {{knife.axes, true, knife.at[2] - *knifeFloor, P::ToolSlot, "slot 1"}};
        return fr::LearnCrateRest(s);
    };

    // One floor for the crate (the tested build): the knife learned -1.4 cm and sank that far into Iron's slot.
    const std::optional<double> crateFloor = fr::CrateFloorHeight(all);
    REQUIRE(crateFloor);
    std::optional<fr::CrateRest> r = knifeRest(crateFloor);
    REQUIRE(r);
    CHECK(fr::FitSpawner(iron, crateFloor, *r).change.lift == doctest::Approx(-0.014));

    // Each slot's own floor: the knife at its floor, and in Iron's option no height change.
    const std::optional<double> floor1 = fr::CrateFloorHeight(slot1);
    const std::optional<double> floor3 = fr::CrateFloorHeight(slot3);
    REQUIRE(floor1);
    REQUIRE(floor3);
    r = knifeRest(floor1);
    REQUIRE(r);
    REQUIRE(r->lift);
    CHECK(*r->lift == doctest::Approx(0.0));
    CHECK(fr::FitSpawner(iron, floor3, *r).change.lift == 0.0);

    // A flat crate: the slots' floors are the crate's, so the
    // heights of the items found right there (Emetic Pills 1.4 cm, the Emetic Poison Vial 2.4 cm, the Squeaky Toy
    // 5.4 cm, the Apple and the Tripwire Explosive at the floor) are as they were.
    const fr::WorldAxes flat {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const std::vector<fr::SpatialTransform> flat1 {{flat, {0.46f, 0, 0}}};
    const std::vector<fr::SpatialTransform> flatAll {{flat, {0.46f, 0, 0}}, {flat, {0, 0, 0}}, {flat, {-0.52f, 0, 0}}};
    REQUIRE(fr::CrateFloorHeight(flatAll));
    CHECK(*fr::CrateFloorHeight(flatAll) == doctest::Approx(*fr::CrateFloorHeight(flat1)));
}

TEST_CASE("crate items: items with an option of their own keep their own spot when the slot's box middle is known") {
    using P = fr::CratePlacement;

    // Game data (found right in game): Emetic Pills (its own slot 2 option +X up, 1.4 cm,
    // (+0.01, -0.00) from slot 2's middle) into Iron's slot 3 option, and the Squeaky Toy Kiwi (its own slot 2
    // option +X up, 5.4 cm, (-0.05, -0.04)) into the same. The box middle is not used for them.
    const fr::WorldAxes flat {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const fr::WorldAxes onX {0, 0, 1, 0, 1, 0, -1, 0, 0}; // +X up
    const fr::SpatialTransform iron {flat, {4.35f, -23.96f, -13.25f}};
    const fr::WorldPoint slot3 {4.343, -23.998, -13.25};
    const fr::WorldPoint slot3Boxes {4.341, -23.950, -13.25};
    const std::optional<double> floor = -13.25;

    struct Case {
        double lift;
        fr::WorldPoint offset;
    };

    for (const Case& c : {Case {0.014, {0.008, -0.005, 0.0}}, Case {0.054, {-0.051, -0.043, 0.0}}}) {
        const std::vector<fr::CrateRestSample> own {{onX, true, c.lift, P::ToolSlot, "crate slot 2 option"}};
        fr::CrateRest r = fr::CrateRestFor(G(5), own);
        REQUIRE(r.source == fr::CrateRestSource::Learned);
        r.offset = c.offset;
        const fr::SpawnerFit f = fr::FitSpawner(iron, floor, r, slot3, slot3Boxes);
        CHECK(f.ownSpot);
        CHECK_FALSE(f.centered);
        CHECK(f.change.lift == doctest::Approx(c.lift));
        const fr::SpatialTransform placed = fr::ApplySpawnerTurn(iron, f.change);
        CHECK(placed.at[0] == doctest::Approx(slot3[0] + c.offset[0]));
        CHECK(placed.at[1] == doctest::Approx(slot3[1] + c.offset[1]));
        CHECK(fr::UpLocalAxis(placed.axes) == std::optional<fr::LocalAxis>(fr::LocalAxis {0, false}));
    }
}

TEST_CASE("crate items: every selector box one size, on its item") {
    const fr::WorldAxes flat {1, 0, 0, 0, 1, 0, 0, 0, 1};
    // (Grenade) Emetic Gas's box stands on its side in its template (its X up); the Kitchen Knife's and the
    // Proximity Taser's lie flat (crate_poses.py: 0.241 x 0.132 x 0.010, 0.377 x 0.078 x 0.010, 0.120 x 0.119 x 0.024).
    const fr::WorldAxes onSide {0, 0, 1, 0, 1, 0, -1, 0, 0};
    const std::vector<fr::CrateBox> boxes {{{onSide, {0, 0, 0}}, {0.010, 0.132, 0.241}},
                                           {{flat, {1, 0, 0}}, {0.377, 0.078, 0.010}},
                                           {{flat, {2, 0, 0}}, {0.120, 0.119, 0.024}},
                                           {{flat, {3, 0, 0}}, {std::nan(""), 0.1, 0.1}}};
    const std::optional<fr::CrateBoxSize> size = fr::CommonCrateBoxSize(boxes);
    REQUIRE(size);
    CHECK(size->side == doctest::Approx((0.132 + 0.120) / 2)); // sides 0.078, 0.119, 0.120, 0.132, 0.241, 0.377
    CHECK(size->height == doctest::Approx(0.010));             // heights 0.010, 0.010, 0.024
    CHECK(fr::DescribeCrateBoxSize(*size) == "0.13 x 0.13 x 0.01 m");
    CHECK_FALSE(fr::CommonCrateBoxSize(std::vector<fr::CrateBox> {}));
    CHECK_FALSE(fr::CommonCrateBoxSize(std::vector<fr::CrateBox> {{{fr::WorldAxes {}, {0, 0, 0}}, {1, 1, 1}}}));

    // The size along each box's own axes: the axis nearest up gets the height.
    CHECK(fr::CrateBoxExtent(flat, {0.12, 0.01}) == std::array<double, 3> {0.12, 0.12, 0.01});
    CHECK(fr::CrateBoxExtent(onSide, {0.12, 0.01}) == std::array<double, 3> {0.01, 0.12, 0.12});
    CHECK(fr::CrateBoxExtent(kNapoleon, {0.12, 0.01}) == std::array<double, 3> {0.12, 0.01, 0.12});

    // A point in a spawner's axes and back; a twin's point turned to the item's axes.
    const fr::SpatialTransform spawner {fr::WorldAxes {0, 1, 0, -1, 0, 0, 0, 0, 1}, {10, 20, 5}};
    const std::array<double, 3> local = fr::PointInSpawner(spawner, {10.5, 20.25, 5.013});
    CHECK(local[0] == doctest::Approx(0.25));
    CHECK(local[1] == doctest::Approx(-0.5));
    CHECK(local[2] == doctest::Approx(0.013));
    const fr::WorldPoint back = fr::PointFromSpawner(spawner, local);
    CHECK(back[0] == doctest::Approx(10.5));
    CHECK(back[1] == doctest::Approx(20.25));
    CHECK(back[2] == doctest::Approx(5.013));
    const std::array<double, 3> turned = fr::TurnPoint({0.01, 0.02, 0.013}, fr::AxisTurn {0, 1, 0, -1, 0, 0, 0, 0, 1});
    CHECK(turned[0] == doctest::Approx(-0.02)); // item X = -twin Y
    CHECK(turned[1] == doctest::Approx(0.01));  // item Y = twin X
    CHECK(turned[2] == doctest::Approx(0.013));

    // Where the box goes: the item's box center from the spawner as placed; unknown: the vanilla spot when the
    // spawner is vanilla, else its origin.
    const fr::SpatialTransform vanilla {flat, {0, 0, 0}};
    const fr::SpatialTransform moved {flat, {0.1, 0.2, 0.024}};
    const fr::WorldPoint boxVanilla {0.05, 0.0, 0.12};
    fr::WorldPoint at = fr::CrateBoxCenter(vanilla, moved, boxVanilla, std::array<double, 3> {0, 0, 0.013});
    CHECK(at[0] == doctest::Approx(0.1));
    CHECK(at[1] == doctest::Approx(0.2));
    CHECK(at[2] == doctest::Approx(0.037));
    at = fr::CrateBoxCenter(vanilla, vanilla, boxVanilla, std::nullopt);
    CHECK(at == boxVanilla);
    at = fr::CrateBoxCenter(vanilla, moved, boxVanilla, std::nullopt);
    CHECK(at[0] == doctest::Approx(0.1));
    CHECK(at[2] == doctest::Approx(0.024));
}

TEST_CASE("crate items: a transform under its parent and back") {
    // The box under its option's "Pose when resting": yawed and turned, back and forth.
    const fr::SpatialTransform parent {{0, 1, 0, -1, 0, 0, 0, 0, 1}, {10, 20, 5}};
    const fr::SpatialTransform world {{0, 1, 0, 0, 0, 1, 1, 0, 0}, {8, 21, 5.04f}};
    CHECK(fr::SameTransform(fr::WorldTransform(parent, fr::LocalTransform(parent, world)), world));
    CHECK(fr::SameTransform(fr::WorldTransform(parent, fr::SpatialTransform {kNapoleon, {1, 2, 0.04f}}), world));
    CHECK(fr::SameTransform(fr::WorldTransform(fr::SpatialTransform {}, world), world));
}

TEST_CASE("crate items: options that cannot be turned are kept out of a planned slot's draw") {
    using A = fr::CrateOptionAction;
    using F = fr::CrateFit;

    // Fits, turned and unread offer; unfit ones are kept out.
    std::vector<A> a {A::Offer, A::Offer, A::Offer, A::Offer, A::Offer};
    fr::CrateFitPick p = fr::KeepUnfitOut(a, std::vector<F> {F::Fits, F::Unfit, F::Unread, F::Turned, F::Unfit});
    CHECK(a == std::vector<A> {A::Offer, A::Exclude, A::Offer, A::Offer, A::Exclude});
    CHECK(p.keptOut == 2);
    CHECK(p.unread == 1);
    CHECK(p.turned == 1);
    CHECK_FALSE(p.fallback);

    // Every offering option unfit: the slot offers as before (never a slot that draws nothing).
    a = {A::Offer, A::Offer};
    p = fr::KeepUnfitOut(a, std::vector<F> {F::Unfit, F::Unfit});
    CHECK(a == std::vector<A> {A::Offer, A::Offer});
    CHECK(p.fallback);
    CHECK(p.keptOut == 0);

    // A reused option stays forced even when unfit (counted); a turned reused one is counted as turned.
    a = {A::Exclude, A::Reuse, A::Rewrite};
    p = fr::KeepUnfitOut(a, std::vector<F> {F::Unfit, F::Unfit, F::Unfit});
    CHECK(a == std::vector<A> {A::Exclude, A::Reuse, A::Rewrite});
    CHECK(p.unfitReused == 1);
    CHECK_FALSE(p.fallback);
    a = {A::Reuse};
    CHECK(fr::KeepUnfitOut(a, std::vector<F> {F::Turned}).turned == 1);

    // Fewer fits than options: the rest are unread (offered).
    a = {A::Offer, A::Offer, A::Offer};
    p = fr::KeepUnfitOut(a, std::vector<F> {F::Unfit});
    CHECK(a == std::vector<A> {A::Exclude, A::Offer, A::Offer});
    CHECK(p.unread == 2);

    // Exactly the offers that are not unfit remain drawn, for every mix of four options.
    for (unsigned mask = 0; mask < 256; ++mask) {
        std::vector<F> fits;
        unsigned m = mask;

        for (int i = 0; i < 4; ++i, m /= 4)
            fits.push_back(static_cast<F>(m % 4));

        std::vector<A> all(4, A::Offer);
        const fr::CrateFitPick r = fr::KeepUnfitOut(all, fits);
        const auto unfit = static_cast<std::ptrdiff_t>(std::count(fits.begin(), fits.end(), F::Unfit));
        CHECK(std::count(all.begin(), all.end(), A::Offer) == (unfit == 4 ? 4 : 4 - unfit));
        CHECK(r.fallback == (unfit == 4));
    }

    // An option's from its spawners: any unfit, else any turned, else any fitting, else unread.
    CHECK(fr::OptionFit(std::vector<F> {}) == F::Unread);
    CHECK(fr::OptionFit(std::vector<F> {F::Unread, F::Fits}) == F::Fits);
    CHECK(fr::OptionFit(std::vector<F> {F::Fits, F::Turned}) == F::Turned);
    CHECK(fr::OptionFit(std::vector<F> {F::Turned, F::Unfit}) == F::Unfit);
}

TEST_CASE("crate items: the branch a crate's plan writes") {
    using S = fr::CrateBranchSteer;
    using R = fr::CrateBranchReason;
    const auto Pick = [](const fr::CratePlan* plan, bool oneItemHeld, bool toolOut) {
        const fr::CrateBranchPick p = fr::CrateItemsBranch(plan, oneItemHeld, toolOut);
        return std::pair(p.steer, p.reason);
    };

    CHECK(Pick(nullptr, false, false) == std::pair(S::Vanilla, R::NoPlan));

    fr::CratePlan items;
    items.kind = fr::CratePlanKind::Items;
    items.slots = {0, 1, 2};
    // Three items always force the tool pool: the game's merces crate (vanilla weights tool 0.85 / weapon 0.15) comes only from the merces chance now. Items out change nothing.
    CHECK(Pick(&items, false, false) == std::pair(S::Tool, R::ToolPlan));
    CHECK(Pick(&items, false, true) == std::pair(S::Tool, R::ToolPlan));
    CHECK(Pick(&items, true, false) == std::pair(S::Tool, R::ToolPlan));

    fr::CratePlan tools = items;
    tools.kind = fr::CratePlanKind::Tools;
    CHECK(Pick(&tools, false, false) == std::pair(S::Tool, R::ToolPlan));

    // One gun alone, a large item alone: the weapon pool's one-item option.
    fr::CratePlan gun;
    gun.kind = fr::CratePlanKind::Gun;
    gun.single = 3;
    CHECK(Pick(&gun, false, false) == std::pair(S::Weapon, R::OneItem));
    fr::CratePlan large;
    large.kind = fr::CratePlanKind::Large;
    large.single = 4;
    CHECK(Pick(&large, false, false) == std::pair(S::Weapon, R::OneItem));

    // Merces: the weapon pool, its option with its own item.
    fr::CratePlan merces;
    merces.kind = fr::CratePlanKind::Merces;
    CHECK(Pick(&merces, false, false) == std::pair(S::Weapon, R::Merces));

    // A vanilla plan (too few items): vanilla weights unless something is out.
    fr::CratePlan vanilla;
    CHECK(Pick(&vanilla, false, false) == std::pair(S::Vanilla, R::Other));
    CHECK(Pick(&vanilla, false, true) == std::pair(S::Tool, R::ToolSlotItemOut));
    CHECK(Pick(&vanilla, true, false) == std::pair(S::Tool, R::OneItemHeld));
}

TEST_CASE("crate items: a pickup comes from the crate opened last") {
    CHECK_FALSE(fr::CratePickupSource({}));
    const std::vector<fr::CrateHold> one {{3, 7}};
    CHECK(fr::CratePickupSource(one) == std::optional<size_t>(0));
    const std::vector<fr::CrateHold> two {{0, 2}, {1, 5}, {2, 4}};
    CHECK(fr::CratePickupSource(two) == std::optional<size_t>(1));
    const std::vector<fr::CrateHold> tie {{0, 5}, {1, 5}};
    CHECK(fr::CratePickupSource(tie) == std::optional<size_t>(0));
}

TEST_CASE("crate items: no one-item crate while its option holds another crate's item") {
    const std::vector<fr::CrateCandidate> c = Mission();
    fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    REQUIRE_FALSE(p.large.empty());
    fr::DropOneItemCrates(p);
    CHECK(p.large.empty());
    CHECK_FALSE(p.oneItem);
    CHECK(fr::DescribeCratePools(p).find(", no one-item crate") != std::string::npos);

    // Every roll now gives a crate of three: no gun, no merces, no large item.
    fr::CrateGunsConfig guns;
    guns.on = true;
    guns.poolChance = 0.4;
    fr::CrateItemsConfig cfg;
    cfg.mercesChance = 0.4;

    for (uint32_t open = 0; open < 40; ++open) {
        const fr::CratePlan plan = fr::PlanCrate(9, kScene, open, c, p, cfg, guns);
        CHECK_FALSE(plan.single);
        CHECK(plan.kind != fr::CratePlanKind::Merces);
        CHECK(plan.kind != fr::CratePlanKind::Gun);
    }
}

TEST_CASE("crate items: a spawned item is measured by its model's bounds and centered where its middle should lie") {
    // Game data, a Berlin crate: the Fuse Cell (no box) in slot 3, its spawner at the slot's middle, the
    // spawned item 0.28 m along the spawner's +Y and turned -Y up (its placement helper's doing). Its model taken as
    // 10 x 6 x 6 cm about its origin.
    const fr::SpatialTransform spawner {{-1.0f, -0.03f, 0, 0.03f, -1.0f, 0, 0, 0, 1}, {-47.71f, 116.70f, 7.11f}};
    const fr::SpatialTransform fuse {{1.0f, 0.03f, 0, 0, 0, -1.0f, -0.03f, 1.0f, 0}, {-47.71f, 116.42f, 7.11f}};
    const fr::ItemBounds model {{-0.05, -0.03, -0.03}, {0.05, 0.03, 0.03}};
    const fr::WorldPoint target {-47.71, 116.70, 7.11};
    fr::CrateCenterCheck c = fr::CheckItemCenter(fuse, model, spawner, target);
    CHECK(c.verdict == fr::CrateCentering::Moved);
    CHECK(c.miss == doctest::Approx(0.28).epsilon(0.01));
    CHECK(c.shift[0] == doctest::Approx(0.0).epsilon(0.001));
    CHECK(c.shift[1] == doctest::Approx(0.28).epsilon(0.01));
    CHECK(c.shift[2] == 0.0);
    CHECK(c.centerLocal[1] == doctest::Approx(0.28).epsilon(0.01)); // along the spawner's +Y
    CHECK(fr::DescribeCenterCheck(c) ==
          "0.10 x 0.06 x 0.06 m, its middle (+0.01, +0.28, +0.00) m from its spawner, 0.28 m off along the floor: "
          "moved");

    // Moved by the shift, it lies centered.
    fr::SpatialTransform moved = fuse;
    moved.at[0] += static_cast<float>(c.shift[0]);
    moved.at[1] += static_cast<float>(c.shift[1]);
    CHECK(fr::CheckItemCenter(moved, model, spawner, target).verdict == fr::CrateCentering::Centered);

    // The middle is the model's, not its origin: bounds off its origin along its own Z (world +Y here) carry it.
    const fr::ItemBounds offset {{-0.05, -0.03, 0.25}, {0.05, 0.03, 0.31}};
    c = fr::CheckItemCenter(fuse, offset, spawner, target);
    CHECK(c.verdict == fr::CrateCentering::Centered);
    CHECK(c.miss < 0.02);

    // Only the move along the floor counts: 10 cm above the target is centered.
    const fr::SpatialTransform above {spawner.axes, {-47.71f, 116.70f, 7.21f}};
    c = fr::CheckItemCenter(above, model, spawner, target);
    CHECK(c.verdict == fr::CrateCentering::Centered);
    CHECK(c.miss == doctest::Approx(0.0).epsilon(0.001));
    CHECK(c.centerLocal[2] == doctest::Approx(0.10).epsilon(0.01));

    // 3 cm off: moved; placed by a box from the game files, within its slack.
    const fr::SpatialTransform close {spawner.axes, {-47.71f, 116.67f, 7.11f}};
    CHECK(fr::CheckItemCenter(close, model, spawner, target).verdict == fr::CrateCentering::Moved);
    CHECK(fr::CheckItemCenter(close, model, spawner, target, fr::kCrateCenterBoxSlackMeters).verdict ==
          fr::CrateCentering::Centered);

    // Farther than half a meter: not trusted.
    const fr::SpatialTransform distant {spawner.axes, {-47.71f, 116.10f, 7.11f}};
    c = fr::CheckItemCenter(distant, model, spawner, target);
    CHECK(c.verdict == fr::CrateCentering::TooFar);
    CHECK(fr::DescribeCenterCheck(c).ends_with("0.60 m off along the floor: too far to trust, left as it lies"));

    // Bounds that are not a small item's: left.
    const double nan = std::nan("");
    for (const fr::ItemBounds& b :
         {fr::ItemBounds {{nan, 0, 0}, {0.1, 0.1, 0.1}}, fr::ItemBounds {{0.1, 0, 0}, {0.0, 0.1, 0.1}},
          fr::ItemBounds {{-1.0, 0, 0}, {1.0, 0.1, 0.1}}, fr::ItemBounds {}}) {
        c = fr::CheckItemCenter(fuse, b, spawner, target);
        CHECK(c.verdict == fr::CrateCentering::Unsized);
        CHECK(fr::DescribeCenterCheck(c) == "its bounds do not read as an item's (left as it lies)");
    }
}

TEST_CASE("crate items: an item with no option and no twin is centered by its middle measured this load") {
    fr::CrateRest r = fr::CrateRestFallback(G(5));
    CHECK(r.source == fr::CrateRestSource::Default);
    CHECK_FALSE(r.center);
    CHECK(fr::DescribeCrateRest(r) ==
          "+Z up (no option of its own in the crate, not in the built-in table; lying flat as vanilla; no box in the "
          "game files (at the slot's middle; centered by its geometry once it spawns); height not learned)");

    CHECK(fr::UseMeasuredCenter(r, {0.008, 0.28, 0.0}));
    REQUIRE(r.center);
    CHECK(r.centerMeasured);
    CHECK((*r.center)[1] == doctest::Approx(0.28));
    CHECK(fr::DescribeCrateRest(r) ==
          "+Z up (no option of its own in the crate, not in the built-in table; lying flat as vanilla; centered by its "
          "geometry (center (+0.01, +0.28, +0.00) m from its spawner, measured where it spawned earlier this load); "
          "height not learned)");

    // Centered in the slot by it as by a box: its middle on the slot's box middle.
    const fr::SpatialTransform host {{1, 0, 0, 0, 1, 0, 0, 0, 1}, {0, 0, -4.70f}};
    const fr::SpawnerFit f =
        fr::FitSpawner(host, -4.70, r, fr::WorldPoint {0.0, 0.0, -4.70}, fr::WorldPoint {0.0, 0.05, -4.70});
    CHECK(f.centered);
    const fr::WorldPoint middle = fr::PointFromSpawner(fr::ApplySpawnerTurn(host, f.change), *r.center);
    CHECK(middle[0] == doctest::Approx(0.0).epsilon(0.002));
    CHECK(middle[1] == doctest::Approx(0.05).epsilon(0.002));

    // A table item's box from the game files gives way to it too; a learned or twin rest keeps the game's placement.
    fr::CrateRest table;
    table.source = fr::CrateRestSource::Table;
    table.center = std::array<double, 3> {0.0, 0.0, 0.01};
    CHECK(fr::UseMeasuredCenter(table, {0.0, 0.02, 0.01}));
    CHECK((*table.center)[1] == doctest::Approx(0.02));
    fr::CrateRest learned;
    learned.source = fr::CrateRestSource::Learned;
    CHECK_FALSE(fr::UseMeasuredCenter(learned, {0.0, 0.02, 0.01}));
    CHECK_FALSE(learned.center);
    fr::CrateRest twin;
    twin.source = fr::CrateRestSource::Twin;
    CHECK_FALSE(fr::UseMeasuredCenter(twin, {0.0, 0.02, 0.01}));
}

TEST_CASE("crate items: an item with no option, no twin and no table row lies on its box's thin side") {
    // The Explosive Phone: +X up would stand it on its end. Its box,
    // 14.4 x 7.5 x 1.0 cm along its spawner's X, Y, Z, lays it on its back.
    const char* kExplosivePhone = "74a22451-8920-488f-883c-b5246ba0f9f3";
    const std::optional<std::array<double, 3>> size = fr::CrateItemBoxSize(Parsed(kExplosivePhone));
    REQUIRE(size);
    CHECK((*size)[0] == doctest::Approx(0.144));
    CHECK((*size)[2] == doctest::Approx(0.010));
    fr::CrateRest r = fr::CrateRestFallback(Parsed(kExplosivePhone));
    CHECK(r.source == fr::CrateRestSource::Box);
    CHECK(r.axis == fr::kLocalUp);
    CHECK_FALSE(r.lift);
    REQUIRE(r.center);
    CHECK(fr::DescribeCrateRest(r).find("+Z up (no option of its own in the crate, not in the built-in table; on its "
                                        "widest face by its box in the game files (0.144 x 0.075 x 0.010 m") == 0);

    // Its spawner in another item's option (the OilSpill Canister's, +X up): turned so its Z is up, at the floor.
    const fr::SpatialTransform canister {{0, 0, 1, 0, 1, 0, -1, 0, 0}, {0, 0, -4.70f}};
    const fr::SpawnerFit f = fr::FitSpawner(canister, -4.70, r);
    CHECK(f.change.turn != fr::kNoTurn);
    CHECK(fr::UpLocalAxis(fr::ApplySpawnerTurn(canister, f.change).axes) == std::optional<fr::LocalAxis>(fr::kLocalUp));

    // The Chloroform Flask (a box 0 x 7.4 x 22 cm: thin along X) lies on its side, no longer upright on +Z.
    r = fr::CrateRestFallback(Parsed("3cf48e44-6e0f-4e4d-9d21-6a4af476118c"));
    CHECK(r.source == fr::CrateRestSource::Box);
    CHECK(r.axis == kX);

    // The table still comes first (Napoleon figure +Y, Coin Classic -Z: their boxes agree on the axis).
    CHECK(fr::CrateRestFallback(Parsed(kNapoleonFigure)).source == fr::CrateRestSource::Table);
    CHECK(fr::CrateRestFallback(Parsed(kCoinClassic)).axis == kMinusZ);
}

TEST_CASE("crate items: a box's clearly thin side") {
    CHECK(fr::CrateBoxRestAxis({0.144, 0.075, 0.010}) == std::optional<fr::LocalAxis>(fr::kLocalUp));
    CHECK(fr::CrateBoxRestAxis({0.010, 0.058, 0.126}) == std::optional<fr::LocalAxis>(kX));
    CHECK(fr::CrateBoxRestAxis({0.159, 0.010, 0.212}) == std::optional<fr::LocalAxis>(kY));
    CHECK(fr::CrateBoxRestAxis({0.000, 0.074, 0.220}) == std::optional<fr::LocalAxis>(kX));
    // A ball or a cube: no side is thin.
    CHECK_FALSE(fr::CrateBoxRestAxis({0.110, 0.111, 0.112}));
    CHECK_FALSE(fr::CrateBoxRestAxis({0.061, 0.061, 0.061}));
    CHECK_FALSE(fr::CrateBoxRestAxis({0.050, 0.080, 0.120})); // 5 cm against 8: not a card
    // Unreadable.
    CHECK_FALSE(fr::CrateBoxRestAxis({0.0, 0.0, 0.0}));
    CHECK_FALSE(fr::CrateBoxRestAxis({-0.1, 0.2, 0.3}));
    CHECK_FALSE(fr::CrateBoxRestAxis({std::nan(""), 0.2, 0.01}));
}

TEST_CASE("crate items: an item seen standing on its end is laid on its widest face") {
    // The Explosive Phone as it stood (Garden Show, log 16:22): its spawner and the item +X up, X (-0.00, 0.00, 1.00)
    // Y (0.04, 1.00, 0.00) Z (-1.00, 0.04, -0.00); a model 14.4 x 7.5 x 1.0 cm along its X, Y, Z.
    const fr::WorldAxes standing {0, 0, 1, 0.04f, 1, 0, -1, 0.04f, 0};
    const fr::SpatialTransform item {standing, {-33.59f, 18.36f, 3.23f}};
    const fr::ItemBounds phone {{-0.072, -0.0375, 0.0}, {0.072, 0.0375, 0.010}};

    const std::optional<fr::LocalAxis> axis = fr::MeasuredRestAxis(item, phone, item);
    REQUIRE(axis);
    CHECK(axis->index == 2); // the spawner's Z: its thin side

    const std::optional<fr::SpatialTransform> flat = fr::LayItemFlat(item, phone);
    REQUIRE(flat);
    CHECK(fr::UpLocalAxis(flat->axes) == std::optional<fr::LocalAxis>(fr::kLocalUp));
    // Its middle stays over the same spot, its bottom at the same height (it stood from 3.23 - 0.072 up).
    const auto middleOf = [&](const fr::SpatialTransform& t) { return fr::PointFromSpawner(t, {0.0, 0.0, 0.005}); };
    const fr::WorldPoint was = middleOf(item);
    const fr::WorldPoint now = middleOf(*flat);
    CHECK(now[0] == doctest::Approx(was[0]).epsilon(0.0001));
    CHECK(now[1] == doctest::Approx(was[1]).epsilon(0.0001));
    CHECK(now[2] - 0.005 == doctest::Approx(was[2] - 0.072).epsilon(0.0001));
    // Laid flat, it is not seen standing again.
    CHECK_FALSE(fr::MeasuredRestAxis(*flat, phone, *flat));
    CHECK_FALSE(fr::LayItemFlat(*flat, phone));

    // A spawner turned against the item (the item a quarter turn of yaw off it): its own local axis along the thin side.
    const fr::SpatialTransform spawner {{0, 0, 1, -1, 0.04f, 0, -0.04f, -1, 0}, item.at};
    CHECK(fr::MeasuredRestAxis(item, phone, spawner) == std::optional<fr::LocalAxis>(fr::LocalAxis {1, false}));

    // Lying flat (+Z up), a cube, an item on its long side barely taller than thick, unreadable bounds: left.
    const fr::SpatialTransform lying {{1, 0, 0, 0, 1, 0, 0, 0, 1}, {0, 0, 0}};
    CHECK_FALSE(fr::MeasuredRestAxis(lying, phone, lying));
    CHECK_FALSE(fr::MeasuredRestAxis(item, fr::ItemBounds {{0, 0, 0}, {0.1, 0.1, 0.1}}, item));
    CHECK_FALSE(fr::MeasuredRestAxis(item, fr::ItemBounds {{0, 0, 0}, {0.02, 0.1, 0.012}}, item)); // 2 cm tall
    CHECK_FALSE(fr::MeasuredRestAxis(item, fr::ItemBounds {{0, 0, 0}, {3.0, 0.1, 0.01}}, item));
    CHECK_FALSE(fr::LayItemFlat(item, fr::ItemBounds {}));
}

TEST_CASE("crate items: an item upright off its widest face is laid on it, whatever its proportions") {
    // Game data, Miami (A Silver Tongue): both level items with no crate option, no twin, no
    // table row and no box (source Default: +Z up as vanilla) stood upright. The old rule (twice as tall as the
    // thinnest side, that side under 0.6 of the next) left both.
    // Soda can (slot 3): 0.08 x 0.08 x 0.14 m, its middle 0.07 m above its spawner; the item and its spawner +Z up,
    // X (-0.78, -0.62, 0.00) Y (0.62, -0.78, 0.00).
    const fr::SpatialTransform can {{-0.78f, -0.62f, 0, 0.62f, -0.78f, 0, 0, 0, 1}, {-124.86f, -113.96f, -4.81f}};
    const fr::ItemBounds canModel {{-0.04, -0.04, 0.0}, {0.04, 0.04, 0.14}};
    CHECK(fr::MeasuredRestAxis(can, canModel, can) == std::optional<fr::LocalAxis>(kX));
    const std::optional<fr::SpatialTransform> canFlat = fr::LayItemFlat(can, canModel);
    REQUIRE(canFlat);
    CHECK(fr::UpLocalAxis(canFlat->axes) == std::optional<fr::LocalAxis>(kX));
    // On its side now: 8 cm tall, its bottom still on the floor, its middle over the same spot.
    const fr::WorldPoint canWas = fr::PointFromSpawner(can, {0.0, 0.0, 0.07});
    const fr::WorldPoint canNow = fr::PointFromSpawner(*canFlat, {0.0, 0.0, 0.07});
    CHECK(canNow[0] == doctest::Approx(canWas[0]).epsilon(0.0001));
    CHECK(canNow[1] == doctest::Approx(canWas[1]).epsilon(0.0001));
    CHECK(canNow[2] - 0.04 == doctest::Approx(-4.81).epsilon(0.0001));
    // Laid, it is not turned again (its two equal sides: either one up is its widest face).
    CHECK_FALSE(fr::MeasuredRestAxis(*canFlat, canModel, *canFlat));
    CHECK_FALSE(fr::LayItemFlat(*canFlat, canModel));

    // Bust (slot 1): 0.10 x 0.12 x 0.21 m, its middle 0.12 m above its spawner; the item X (0.78, 0.62, 0.00) Y (-0.62,
    // 0.78, 0.00) Z up, its spawner a quarter turn of yaw off it: X (0.62, -0.78, 0.00) Y (0.78, 0.62, 0.00) Z up. Its
    // thinnest side (its X, 10 cm) is the spawner's Y.
    const fr::SpatialTransform bust {{0.78f, 0.62f, 0, -0.62f, 0.78f, 0, 0, 0, 1}, {-125.23f, -114.23f, -4.69f}};
    const fr::SpatialTransform bustSpawner {{0.62f, -0.78f, 0, 0.78f, 0.62f, 0, 0, 0, 1}, {-125.24f, -114.20f, -4.81f}};
    const fr::ItemBounds bustModel {{-0.04, -0.06, 0.015}, {0.06, 0.06, 0.225}};
    CHECK(fr::MeasuredRestAxis(bust, bustModel, bustSpawner) == std::optional<fr::LocalAxis>(kY));
    const std::optional<fr::SpatialTransform> bustFlat = fr::LayItemFlat(bust, bustModel);
    REQUIRE(bustFlat);
    CHECK(fr::UpLocalAxis(bustFlat->axes) == std::optional<fr::LocalAxis>(kX));

    // Lying on a middling face (2 x 5 x 10 cm, the 5 cm up): turned to the 2 cm up.
    const fr::SpatialTransform lying {{1, 0, 0, 0, 1, 0, 0, 0, 1}, {0, 0, 0}};
    const fr::ItemBounds slab {{0, 0, 0}, {0.10, 0.02, 0.05}};
    CHECK(fr::MeasuredRestAxis(lying, slab, lying) == std::optional<fr::LocalAxis>(kY));

    // Left as it lies: a cube or a squat can (sides within 15% or 1 cm of each other), a card already flat.
    CHECK_FALSE(fr::MeasuredRestAxis(lying, fr::ItemBounds {{0, 0, 0}, {0.08, 0.08, 0.085}}, lying));
    CHECK_FALSE(fr::MeasuredRestAxis(lying, fr::ItemBounds {{0, 0, 0}, {0.30, 0.30, 0.33}}, lying));
    CHECK_FALSE(fr::MeasuredRestAxis(lying, fr::ItemBounds {{0, 0, 0}, {0.09, 0.15, 0.002}}, lying));
}

TEST_CASE("crate items: a measured rest replaces a guessed one only") {
    fr::CrateRest r = fr::CrateRestFallback(G(5));
    REQUIRE(r.source == fr::CrateRestSource::Default);
    CHECK(fr::UseMeasuredRest(r, kX));
    CHECK(r.source == fr::CrateRestSource::Measured);
    CHECK(r.axis == kX);
    CHECK_FALSE(r.lift);
    CHECK_FALSE(r.center);
    CHECK(fr::DescribeCrateRest(r).find("+X up (no option of its own in the crate; it stood upright at an earlier "
                                        "spawn this load: laid on its widest face as its model measured; no box") == 0);
    // Once measured, not again; its middle measured on that side centers it.
    CHECK_FALSE(fr::UseMeasuredRest(r, kY));
    CHECK(fr::UseMeasuredCenter(r, {0.0, 0.01, 0.0}));

    fr::CrateRest table = fr::CrateRestFallback(Parsed(kNapoleonFigure));
    CHECK(fr::UseMeasuredRest(table, fr::kLocalUp));
    CHECK_FALSE(table.lift); // the table's 4 cm was for its other side

    fr::CrateRest learned;
    learned.source = fr::CrateRestSource::Learned;
    CHECK_FALSE(fr::UseMeasuredRest(learned, kX));
    CHECK(learned.axis == fr::kLocalUp);
    fr::CrateRest twin;
    twin.source = fr::CrateRestSource::Twin;
    CHECK_FALSE(fr::UseMeasuredRest(twin, kX));
}

namespace {

// pro.repo ids (chunk0patch4): the Hatchet (a level item, no option template, its model in chunk0.rpkg), the Kali
// Stick (an option template's box, 0.80 m), the Dugong Torch (a model size, 0.70 m), the Cleaver (a box, 0.36 m) and
// the Rusty Metal Rod (the vanilla crate's Large items slot, 0.614 m).
const char* kHatchet = "3a8207bb-84f5-438f-8f30-5c83aef2af80";
const char* kKaliStick = "06f5a374-306b-4b79-b611-50c710fdb5c1";
const char* kDugongTorch = "94c2b206-d011-4358-a6b3-c8a6042ab2c2";
const char* kCleaver = "1bbf0ed5-0515-4599-a4c9-454ce59cff44";
const char* kRustyRod = "6294f1c4-68db-477f-b7c9-8c9825c077a1";

fr::CrateItemLength Meters(double m, fr::CrateLengthSource source = fr::CrateLengthSource::Box) {
    return {m, source};
}

} // namespace

TEST_CASE("crate items: an item's longest side from the game files") {
    CHECK_FALSE(fr::CrateItemLongestSide(Parsed(kHatchet))); // neither a box nor a model size
    CHECK_FALSE(fr::CrateItemLongestSide(G(5)));

    std::optional<fr::CrateItemLength> l = fr::CrateItemLongestSide(Parsed(kKaliStick));
    REQUIRE(l);
    CHECK(l->meters == doctest::Approx(0.800));
    CHECK(l->source == fr::CrateLengthSource::Box);

    l = fr::CrateItemLongestSide(Parsed(kDugongTorch));
    REQUIRE(l);
    CHECK(l->meters == doctest::Approx(0.699));
    CHECK(l->source == fr::CrateLengthSource::Model);

    l = fr::CrateItemLongestSide(Parsed(kCleaver));
    REQUIRE(l);
    CHECK(l->meters == doctest::Approx(0.356));
    CHECK(l->meters < fr::kCrateSlotLengthMeters);
}

TEST_CASE("crate items: an item too big for a slot comes alone, and says why") {
    using fr::CrateAlone;
    const auto Judge = [](std::string_view size, std::string_view grip, std::string_view icon,
                          std::optional<fr::CrateItemLength> length,
                          bool vanilla = false) { return fr::JudgeCrateItemFit(size, grip, icon, length, vanilla); };

    // Game data, a Bangkok crate: the Hatchet, medium, no size in the game files, held like a stick.
    fr::CrateItemFit f =
        Judge("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Stick", "melee", fr::CrateItemLongestSide(Parsed(kHatchet)));
    CHECK(f.alone == CrateAlone::UnknownLong);
    CHECK_FALSE(f.length);
    CHECK(fr::DescribeCrateItemFit(f, "IGT_Melee_1H_Stick") ==
          "no size in the game files; a medium melee item held like a stick, which mostly are longer than a slot's "
          "0.46 m");
    CHECK(fr::DescribeCrateItemFit(Judge("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Hammer", "melee", std::nullopt),
                                   "IGT_Melee_1H_Hammer")
              .find("held like a hammer") != std::string::npos);

    // ItemSize large, whatever its length.
    f = Judge("ITEMSIZE_LARGE", "", "melee", Meters(0.22));
    CHECK(f.alone == CrateAlone::ItemSize);
    CHECK(fr::DescribeCrateItemFit(f) == "ItemSize large");

    // Too long by its box or its model.
    f = Judge("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Hammer", "melee", fr::CrateItemLongestSide(Parsed(kKaliStick)));
    CHECK(f.alone == CrateAlone::TooLong);
    CHECK(fr::DescribeCrateItemFit(f) == "too long for a slot: 0.80 m (its box in the game files)");
    f = Judge("ITEMSIZE_SMALL", "IGT_Melee_1H_Stick", "melee", fr::CrateItemLongestSide(Parsed(kDugongTorch)));
    CHECK(f.alone == CrateAlone::TooLong);
    CHECK(fr::DescribeCrateItemFit(f) == "too long for a slot: 0.70 m (its model)");

    // The slot's length is still in; a known size wins over the grip guess.
    CHECK(Judge("ITEMSIZE_SMALL", "", "melee", Meters(fr::kCrateSlotLengthMeters)).alone == CrateAlone::No);
    CHECK(Judge("ITEMSIZE_SMALL", "", "melee", Meters(0.461)).alone == CrateAlone::TooLong);
    CHECK(Judge("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Stick", "melee", Meters(0.41)).alone == CrateAlone::No); // Sickle
    CHECK(Judge("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Cleaver", "melee", fr::CrateItemLongestSide(Parsed(kCleaver))).alone ==
          CrateAlone::No);
    CHECK(Judge("ITEMSIZE_SMALL", "", "tool", Meters(std::nan(""))).alone == CrateAlone::No);

    // No size known: only a medium melee item of a long grip comes alone.
    CHECK(Judge("ITEMSIZE_MEDIUM", "igt_melee_1h_baton", "melee", std::nullopt).alone == CrateAlone::UnknownLong);
    CHECK(Judge("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Sword", "melee", std::nullopt).alone == CrateAlone::UnknownLong);
    CHECK(Judge("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Knife", "melee", std::nullopt).alone == CrateAlone::No); // a bottle
    CHECK(Judge("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Rock", "melee", std::nullopt).alone == CrateAlone::No);  // a bust
    CHECK(Judge("ITEMSIZE_SMALL", "IGT_Melee_1H_Stick", "melee", std::nullopt).alone == CrateAlone::No);  // a wrench
    CHECK(Judge("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Stick", "tool", std::nullopt).alone == CrateAlone::No);
    CHECK(Judge("", "", "melee", std::nullopt).alone == CrateAlone::No);
    CHECK(fr::DescribeCrateItemFit(Judge("", "", "melee", std::nullopt)).empty());

    // The vanilla crate's own items fit (the game lays them in a slot), the Rusty Metal Rod's 0.614 m too.
    CHECK(Judge("ITEMSIZE_LARGE", "", "tool", fr::CrateItemLongestSide(Parsed(kRustyRod)), true).alone ==
          CrateAlone::No);
    CHECK(Judge("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Stick", "melee", std::nullopt, true).alone == CrateAlone::No);

    // Firearms by ItemSize only (a gun comes alone anyway).
    CHECK(Judge("ITEMSIZE_SMALL", "", "pistol", Meters(0.49)).alone == CrateAlone::No);
    CHECK(Judge("ITEMSIZE_LARGE", "", "sniperrifle", Meters(1.2)).alone == CrateAlone::ItemSize);
}

TEST_CASE("crate items: a long item drawn first comes alone, the plan saying why") {
    std::vector<fr::CrateCandidate> c;

    for (uint64_t n = 1; n <= 4; ++n)
        c.push_back(Cand(n, "Item " + std::to_string(n)));

    fr::CrateCandidate hatchet = Cand(9, "Hatchet");
    hatchet.onLevel = true;
    const fr::CrateItemFit fit =
        fr::JudgeCrateItemFit("ITEMSIZE_MEDIUM", "IGT_Melee_1H_Stick", "melee", std::nullopt, false);
    hatchet.large = fit.alone != fr::CrateAlone::No;
    hatchet.aloneWhy = fr::DescribeCrateItemFit(fit, "IGT_Melee_1H_Stick");
    c.push_back(hatchet);

    const fr::CratePools p = fr::BuildCratePools(c, LoadedOnly(), true);
    REQUIRE(p.large.size() == 1);
    CHECK(p.regular.size() == 4); // never in a three-item crate

    fr::CrateItemsConfig items = LoadedOnly();
    items.toolsChance = 0.0;
    items.mercesChance = 0.0;
    fr::CrateGunsConfig guns;
    guns.on = false;
    bool seen = false;

    for (int32_t seed = 0; seed < 200 && !seen; ++seed) {
        const fr::CratePlan plan = fr::PlanCrate(seed, kScene, 0, c, p, items, guns);

        if (plan.kind == fr::CratePlanKind::Items) {
            CHECK_FALSE(PlanItems(plan, c).contains(G(9)));
            continue;
        }

        REQUIRE(plan.kind == fr::CratePlanKind::Large);
        seen = true;
        CHECK(fr::DescribeCratePlan(plan, c).find("large item: alone: Hatchet (regular, level; no size in the game "
                                                  "files; a medium melee item held like a stick") == 0);
    }

    CHECK(seen);
}
