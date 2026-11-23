#pragma once

// Crate items (setting crate_items): the pure part. The game part is in
// src/mod/CrateGuns.* (it owns the supply crates' options).
//
// How the game's supply crates behave:
// - A tool-pool crate draws its three items from three slots (selectors of
//   the shared stash items, about 20 crate options each). Each option holds
//   its item in eight ZItemRepositoryKeyEntity members (kCrateOptionKeyIds,
//   all the same id), and the spawned item follows them, even an item the map
//   has not loaded (it streams in when the crate opens).
// - The crate menu's name comes from the option's menu keys
//   (kOptionMenuKeyId and the headline add-ons), not from the item spawner's
//   key, so all eight keys are written.
// - An option re-spawns its item whenever its keys change while its item
//   sits in a crate (the item spawner has m_bDestroyItemOnKeyChange); an
//   option whose item was picked up does not.
// - An item nobody took goes along to the next crate: the stash items are one
//   shared instance, and an option's spawned item hangs under it. So an
//   option that still has an item out is written with the new crate's item
//   (it re-spawns as that item) and is the one its slot draws
//   (CrateSlotActions); and while a tool slot has an item out, the weapon pool
//   is never taken (it does not draw the tool slots, so their items would lie
//   beside the merces; CrateItemsBranch).
// - A slot offers nothing (OnPickFailed) when no option's choice condition
//   (kOptionConditionId, fed by the owned flags kUnownedFlagIds) holds. The
//   game sets an option's owned flags when an item its keys name is picked
//   up and never clears them when the keys change, so the mod sets the
//   condition and the owned flags of every option it offers.
// - Writes at the crate seed's output pin (the pin detour runs before the
//   engine passes the seed on) come before the slots' picks (a few ms later)
//   and the spawns (about 0.1 s later). Each crate's plan is written there.
// - A large item stands out of a small crate, and so does a long one pro.repo
//   calls medium (the Hatchet, 0.61 m). An item that does not fit a slot
//   (JudgeCrateItemFit: ItemSize large, longer than kCrateSlotLengthMeters by
//   its box or model in the game files, or with no size known a medium melee
//   item of a long grip) never shares a crate: it comes alone, in the weapon
//   pool's one-item crate (the vanilla "merces crate": the weapon pool offers
//   one item, its merces option, the only one with a weight; its keys get the
//   large item).
// - Items spawn as their option's item spawner places them, and that
//   transform is tuned for the option's own item. Which way up an item lies
//   belongs to the item ("an item's rest" below): at an open each written
//   item's rest is learned from the crate's own option for it (else a
//   built-in table, else +Z) and the spawners that spawn it are turned to it
//   and moved to its spot in the slot (FitSpawner); an option that cannot be
//   turned is kept out of the draw (KeepUnfitOut).
//
// crate_items, per crate (PlanCrate, seeded by the campaign seed, the scene
// and the crate's number in the load):
// - Pools (BuildCratePools) from the mission's candidates (CrateCandidate):
//   the regular pool is what the map has loaded: the items of the crate's own
//   options (what vanilla supply crates hold) and of every other option
//   helper of the mission (the supplier's, the reward crates': the items on
//   the map), and with crate_level_items the items lying around the level
//   (LevelItems below), each item once, no firearm, no merces. The gun pool
//   (firearms you don't own) and the tool pool (Freelancer tools you don't
//   own, IsFreelancerTool) also take items the map has not loaded with
//   CrateItemsConfig::unloaded (setting crate_items_unloaded); the regular
//   pool never does.
// - One roll picks the kind (CrateKindChancesOf: the three chances in this
//   order, scaled down together when they add up to more than 1): a gun
//   crate (crate_gun_chance: one unowned firearm by rarity, alone in the
//   weapon pool's one-item option, whatever its size), a tool crate
//   (crate_all_tools_chance: up to three unowned Freelancer tools, a slot left
//   over gets a regular item), a merces crate (crate_merces_chance: the weapon
//   pool's one-item crate with its own merces option, the vanilla "merces
//   crate"), else a regular crate (three regular items; when the first draw,
//   from the regular and large items together, is a large item, it comes
//   alone). A kind with nothing to offer falls through to a regular crate.
//   Every three-item crate forces the tool pool, so the game's merces crate
//   comes only from the merces chance.
// - No one-item crate (large item, gun, merces) is planned while a tool
//   slot or the one-item option has an item out (DropOneItemCrates).
// - Never the same item twice in a crate, never an unnamed item, never an
//   item you own (any kind: the CPD's PersistentItems, InventoryItems and
//   AwardedItems; firearms also by their owned flags); a gun or large item at
//   most once per load.
//
#include "CrateGunMath.h"
#include "Guid.h"
#include "LootMerces.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fr {

// The item keys of a crate option (ZItemRepositoryKeyEntity members of the
// option template, all holding the option's item), next to kOptionKeyId (a third headline add-on's) and
// kOptionKey2Id (three key comparators'):
inline constexpr uint64_t kOptionKey3Id = 0x6135c34b3c1c8f6full; // the item spawner's m_rMainItemKey
inline constexpr uint64_t kOptionMenuKeyId =
    0xac6acba75274fcd2ull; // the menu entry's headline (ebcb9f73) and name (ZRepository_String)
inline constexpr uint64_t kOptionHeadlineKeyId = 0x0bafd98c5f317a92ull; // the second headline add-on (a693000c)
inline constexpr uint64_t kOptionInfoKeyId = 0x6eb7aa2232670446ull;     // the info panel add-on (a1c0c245)
inline constexpr uint64_t kOptionCompareKeyId = 0xb4da20a580a21989ull;  // a key comparator's
inline constexpr uint64_t kOptionCompare2KeyId = 0xcf78fa909113e50full; // a key comparator's
inline constexpr std::array<uint64_t, 8> kCrateOptionKeyIds {
    kOptionKeyId,         kOptionKey2Id,    kOptionKey3Id,       kOptionMenuKeyId,
    kOptionHeadlineKeyId, kOptionInfoKeyId, kOptionCompareKeyId, kOptionCompare2KeyId};

// The option's choice condition: the ZValueBool_Operation_Signal its
// ZRandomSelectorChoice's m_Condition names (m_bValue). The slot draws only
// among options whose condition holds; it is fed by the owned flags
// (kUnownedFlagIds, ZInvertedCondition m_bValue: true while the player does
// not own the item).
inline constexpr uint64_t kOptionConditionId = 0x1ef814cce7de836aull;

// The option helper's ZEvergreenMenuEntryEntity "EvergreenMenuEntry" (helper_evergreen_selectableitem, chunk0): its
// m_boundary is the option's selector box ("selector boxes" below). The box itself is the item template's, beside
// the helper, so the walk does not reach it from the option.
inline constexpr uint64_t kOptionMenuEntryId = 0x54a19f285df200e5ull;

bool IsCrateOptionKeyId(uint64_t id);

// One selector of the stash items, as its m_Choices map to crate options.
struct CrateSlotSummary {
    size_t choices = 0;  // m_Choices elements
    size_t options = 0;  // of them mapped to a crate option
    size_t firearms = 0; // of those, firearms
    size_t merces = 0;   // of those, the merces option
};

// The tool pool's slots, in the given (walk) order: the selectors that map at
// least one crate option and neither a firearm nor merces (the weapon pool
// holds those; the branch selector maps none). Indices into selectors.
std::vector<size_t> CrateToolSlots(std::span<const CrateSlotSummary> selectors);

// A merces pickup (InventoryCategoryIcon evergreen_payout, or a
// Mercer_Pickup_* item).
bool IsMercesItem(std::string_view name, std::string_view icon);

// --- crate_items ------------------------------------------------------------------------------------------------

// A Freelancer tool: a pro.repo item with Evergreen_LoseOnWounded true (the
// supplier's consumables, the CPD TransientItems: lockpicks, poisons, coins,
// explosives ...), neither a firearm nor merces.
bool IsFreelancerTool(bool loseOnWounded, std::string_view icon, std::string_view name);

// pro.repo ItemSize containing "large" (any case; 86 of the player's 209
// items: rifles, shotguns, staffs, bats, swords ...), unless the crate's own
// tool slots hold it in vanilla (the game puts those in three-item crates
// already). One of JudgeCrateItemFit's reasons.
bool IsLargeCrateItem(std::string_view itemSize, bool vanillaCrateItem);

// The longest item a three-item crate's slot holds, laid any way (m). From the
// vanilla crate's own options (tools/rpkg/crate_poses.py, "slot room"): the
// slots sit 0.46 and 0.52 m apart along the crate (stash X 0.52, 0.06, -0.46)
// and their items reach 0.43 m across it (slot Y -0.205 to +0.225); the
// longest item the game lays in a slot is the Lead Pipe, 0.452 m (the Rusty
// Crowbar, 0.614 m, lies along the crate across the middle slot into its
// neighbors' room, a fit only that one slot and yaw give). A mod-written item
// may land in any slot at its host option's yaw, so anything longer stands
// out: the Hatchet (ItemSize medium, no box in the game files), 0.04 x 0.21 x
// 0.61 m once spawned, sticks out of the crate.
inline constexpr double kCrateSlotLengthMeters = 0.46;

enum class CrateLengthSource : uint8_t {
    Box,   // its option template's box (kCrateItemBoxes: the card the menu draws over it, about its model's size)
    Model, // its model's bounding box (kCrateItemModelSizes: the PRIMs of its Runtime template's item art)
};

struct CrateItemLength {
    double meters = 0.0; // the longest side
    CrateLengthSource source = CrateLengthSource::Box;
};

// An item's longest side from the game files: its option template's box
// (CrateItemBoxSize), else its model's size (src/core/CrateItemSizes.inc,
// tools/rpkg/crate_poses.py --sizes: 46 items with no option template whose
// models the local packages hold). Empty: neither (most level items, such as
// the Hatchet: their models are in chunk0.rpkg, which the Deck copy holds only
// in part).
std::optional<CrateItemLength> CrateItemLongestSide(const Guid& item);

// Why an item comes alone in its crate (the weapon pool's one-item option),
// never sharing a three-item crate.
enum class CrateAlone : uint8_t {
    No,          // it fits a slot (or the vanilla crate holds it: the game lays it in a slot itself)
    ItemSize,    // IsLargeCrateItem: pro.repo ItemSize large
    TooLong,     // its longest side (box or model) is over kCrateSlotLengthMeters
    UnknownLong, // no size in the game files, and a medium melee item held like a stick, hammer, baton or sword
                 // (GripAnimType IGT_Melee_1H_Stick / _Hammer / _Baton / _Sword): of the 13 such items with a size,
                 // 7 are too long (Kali Stick 0.80, Mace 0.63, Police Baton 0.57, Tanto 0.57, Ninja Tonfa 0.53,
                 // Hammer (Mjolnir) 0.53, aluminum baton 0.47; the Lead Pipe 0.45, Trophy 0.43, Sickle 0.41,
                 // Meathook 0.39, Hammer 0.39, Modern Hammer 0.36 fit), and the Hatchet (0.61) is one of the rest.
                 // Knife grips (bottles, knives) and rock grips (busts, bricks, food) are left to fit
};

struct CrateItemFit {
    CrateAlone alone = CrateAlone::No;
    std::optional<CrateItemLength> length; // what the judgment read (empty: no size known)
};

// Whether an item comes alone: ItemSize large, or longer than a slot holds,
// or with no size known a medium melee item of a long grip (CrateAlone). A
// vanilla crate item never does (the game puts it in three-item crates);
// firearms are judged by ItemSize only (a gun always comes alone anyway).
// grip: pro.repo GripAnimType; icon: InventoryCategoryIcon.
CrateItemFit JudgeCrateItemFit(std::string_view itemSize, std::string_view grip, std::string_view icon,
                               std::optional<CrateItemLength> length, bool vanillaCrateItem);

// Why, for the log: "" (No), "ItemSize large", "too long for a slot: 0.62 m (its box in the game files)", "too
// long for a slot: 0.70 m (its model)", "no size in the game files; a medium melee item held like a stick".
std::string DescribeCrateItemFit(const CrateItemFit& fit, std::string_view grip = {});

// pro.repo's Runtime (the item's runtime resource id, a decimal ZString such
// as "46316738499935331" = 0x00A48CD409717C63), or empty when not a number.
std::optional<uint64_t> ParseRuntimeId(std::string_view text);

// Whether a firearm counts as owned (never offered): the option helper's
// owned / entitled flags (flagsEligible: not owned and entitled; empty: no
// flags read) and the CPD's owned lists (listed: CrateItemListed, empty: no
// list known). Owned when either says so; an item neither can tell about
// counts as owned (the supplier's rule: never an owned gun).
bool CrateGunOwned(std::optional<bool> flagsEligible, std::optional<bool> listed);

// What the player has, as crate_items judges it: the ids the CPD lists
// (PersistentItems, the gear wall; TransientItems, the Freelancer tools kept
// between missions, one of each; InventoryItems, packed; AwardedItems) and
// the names of those items (pro.repo Title, else CommonName; folded by
// FoldCrateItemName). pro.repo has items that are the same thing to the
// player under two ids, such as the "ICA Titanium Crowbar" (both "(Tool)
// Crowbar Professional": 736132de-..., a Freelancer tool, and 4404fc8d-...,
// rarity 3, large, on the gear wall), so a shared
// name counts as owned too.
struct CrateOwnedItems {
    std::vector<Guid> ids;          // sorted, each once
    std::vector<std::string> names; // folded, sorted, each once; no empty name
};

// Trimmed, ASCII lower case ("  (Tool) Crowbar Professional " ->
// "(tool) crowbar professional").
std::string FoldCrateItemName(std::string_view name);

// named: every (id, name) the repository knows (any order; an id without an
// entry here adds no name).
CrateOwnedItems MakeCrateOwnedItems(std::span<const Guid> listed, std::span<const std::pair<Guid, std::string>> named);

enum class CrateOwnedBy : uint8_t {
    None, // not owned (as far as the lists tell)
    Id,   // the lists name this id
    Name, // another item with this name is listed
};

// name: the candidate's (unfolded) name; empty matches no name.
CrateOwnedBy CrateItemListed(const CrateOwnedItems& owned, const Guid& item, std::string_view name);

// One item a crate may offer, as the mod knows it at scene playing.
struct CrateCandidate {
    Guid item;
    std::string name;   // pro.repo Title, else CommonName; empty: never offered
    std::string icon;   // InventoryCategoryIcon
    int32_t rarity = 0; // Evergreen_Rarity 1..4 (0: none)
    bool firearm = false;
    bool merces = false;
    bool tool = false;    // IsFreelancerTool
    bool large = false;   // comes alone (JudgeCrateItemFit; the name is older than the size rules)
    std::string aloneWhy; // DescribeCrateItemFit when large (the plan's log says why), else empty
    bool loaded = false;  // the map loaded it: inCrate, onMap or onLevel
    bool inCrate = false; // a vanilla supply crate option holds it (the stash items' own options)
    bool onMap = false;   // another option helper of the mission holds it (the supplier's, the reward crates', ...)
    bool onLevel = false; // it lies around the level (an item spawner outside every option helper: LevelItems)
    bool owned = false;   // never offered: firearms CrateGunOwned; other items CrateItemListed (by id or name)
    uint64_t runtime = 0; // ParseRuntimeId (0: unknown)
};

// Where a candidate came from, for the log: its sources joined ("vanilla crate", "map", "level", "vanilla crate and
// map", "map and level", "vanilla crate, map and level", ...), "loaded" (loaded with no source known) or "not
// loaded".
const char* CrateItemOrigin(const CrateCandidate& item);

struct CrateItemsConfig {
    bool on = true;             // crate_items
    double toolsChance = 0.10;  // crate_all_tools_chance: a tool crate (up to three unowned Freelancer tools)
    double mercesChance = 0.05; // crate_merces_chance: a merces crate (the game's own merces option, alone)
    bool unloaded = true;       // crate_items_unloaded: gun and tool crates take items the map has not loaded too
    bool evenBoxes = true;      // crate_even_boxes: every option's selector box one size, on its item
    bool levelItems = true;     // crate_level_items: the items lying around the level join the regular pool
};

// Chances clamped to [0, 1] (NaN: the default).
CrateItemsConfig SanitizeCrateItemsConfig(CrateItemsConfig cfg);

// The kinds' chances as the roll takes them: gun (crate_gun_chance; 0 with crate_guns off), tools, merces. When they
// add up to more than 1 each is scaled by 1 / sum (scaled: the factor, else 1), so the roll's bands stay exact.
struct CrateKindChances {
    double gun = 0.0;
    double tools = 0.0;
    double merces = 0.0;
    double scale = 1.0;
    double regular() const { return std::max(0.0, 1.0 - gun - tools - merces); }
};

CrateKindChances CrateKindChancesOf(const CrateItemsConfig& cfg, const CrateGunsConfig& guns);

// "gun 5%, tools 10%, merces 5%, regular 80%, gun and tool crates take items the map has not loaded too, selector
// boxes one size, items around the level too" (with " (set chances add up to 120%: scaled by 0.833)" when scaled).
std::string DescribeCrateItemsConfig(const CrateItemsConfig& cfg, const CrateGunsConfig& guns);

// Indices into the candidates, each list sorted by GUID (a pick does not
// depend on the order the walk found them).
struct CratePools {
    std::vector<size_t>
        regular; // what the map has loaded (vanilla crates', the map's, the level's), not large, no firearm
    std::vector<size_t> tools; // Freelancer tools, not large (loaded, or with unloaded on any)
    std::vector<size_t> guns;  // firearms you don't own (any size; with guns on; loaded, or with unloaded on any)
    std::vector<size_t> large; // large items the map has loaded, not firearms
    bool oneItem = true;       // a one-item crate (a large item, a gun, merces) may be planned
    // Left out:
    size_t unnamed = 0;
    size_t merces = 0;
    size_t notLoaded = 0;   // the map has not loaded it (CrateItemsConfig::unloaded off)
    size_t owned = 0;       // firearms you own
    size_t ownedItems = 0;  // other items you own (the CPD's owned lists)
    size_t ownedTools = 0;  // of them, Freelancer tools
    size_t gunsOff = 0;     // firearms with crate_guns off
    size_t taken = 0;       // guns and large items already planned this load (exclude)
    size_t quest = 0;       // quest items (IsQuestItemIcon): a mission's story props, never a crate's
    size_t usableProps = 0; // quest items kept as usable props (IsUsableQuestProp): in the pools, counted for the log
};

// exclude: guns and large items planned for an earlier crate of this load.
CratePools BuildCratePools(std::span<const CrateCandidate> candidates, const CrateItemsConfig& cfg, bool guns,
                           std::span<const Guid> exclude = {});

// "regular 120, tools 31, guns 14, large 30; left out: 2 unnamed, 0 merces, 0 quest items, 400 not loaded, 95 owned
// guns, 12 owned items (1 Freelancer tools), 0 guns (crate_guns off), 0 already planned" (", no one-item crate" when
// oneItem is off).
std::string DescribeCratePools(const CratePools& pools);

enum class CratePlanKind : uint8_t {
    Vanilla, // nothing to offer: the crate as the game fills it
    Items,   // a regular crate: three regular items (tool pool forced)
    Tools,   // a tool crate: up to three unowned Freelancer tools, topped up with regular items (tool pool forced)
    Gun,     // a gun crate: one unowned firearm, alone in the one-item option (weapon pool forced)
    Merces,  // a merces crate: the weapon pool's own merces option (weapon pool forced)
    Large,   // a regular crate whose first draw was a large item: alone (weapon pool forced)
};

const char* CratePlanKindName(CratePlanKind kind);

// The pool a planned item was drawn from.
enum class CratePickPool : uint8_t { None, Regular, Tools, Gun };

const char* CratePickPoolName(CratePickPool pool);

struct CratePlan {
    CratePlanKind kind = CratePlanKind::Vanilla;
    std::array<std::optional<size_t>, 3> slots; // candidate per tool slot (Items, Tools)
    std::array<CratePickPool, 3> from {};       // the pool each slot's item came from
    std::optional<size_t> single;               // the one-item crate's item (Gun, Large; Merces: none)
    CratePickPool singleFrom = CratePickPool::None;
    double roll = 0.0; // the kind roll, uniform [0, 1)
    std::string why;   // how the roll fell ("roll 0.0312 < gun 0.05 ..."), or why vanilla
};

// The crate's contents for crate open openIndex (0-based, per load). The
// roll (CrateKindChancesOf): below gun, a gun crate (one gun alone, when a
// gun you don't own is left and pools.oneItem; picked by rarity:
// CrateRarityValue x rarityMult); then, below
// gun + tools, a tool crate (when an unowned tool is left; any slot left
// over takes a regular item); then, below that + merces, a merces crate
// (when pools.oneItem); else a regular crate: the first item drawn from the
// regular and large items together (a large one comes alone), the others
// from the regular items. A kind with nothing to offer falls through to a
// regular crate (the why says so). Fewer than 3 regular items to fill the
// crate: Vanilla. Deterministic.
CratePlan PlanCrate(int32_t seed, std::string_view scene, uint32_t openIndex,
                    std::span<const CrateCandidate> candidates, const CratePools& pools, const CrateItemsConfig& cfg,
                    const CrateGunsConfig& guns);

// "tools: slot 1 Lockpick (Freelancer tools, not loaded), slot 2 Coin (Freelancer tools, map), slot 3 Apple
// (regular, vanilla crate) (roll 0.0812 in tools [0.05, 0.15) ...)"; "merces: the crate's own merces option (...)".
std::string DescribeCratePlan(const CratePlan& plan, std::span<const CrateCandidate> candidates);

// --- items around the level ---------------------------------------------------------------------------------------
//
// With crate_level_items (on) the regular pool also takes the items lying
// around the level: what the player could pick up off a table or a shelf
// (keys, keycards, tools, props), so each map brings its own. Every item in
// the world is spawned by a ZItemSpawner (its m_rMainItemKey, a
// ZItemRepositoryKeyEntity, names the item: the fishing spot reads its six
// catches the same way). The crate walk already visits every entity of every
// loaded brick, so it keeps every ZItemSpawner outside the stash items and the
// mission's option helpers (the crate's, the supplier's, the reward crates':
// those are the "vanilla crate" and "map" sources). Of those, the ones with
// m_bSpawnOnStart true are the loose items: measured in game, it reads as a plain bool (no getter), true for some of
// the safehouse brick's spawners and false for the rest, and false for every
// spawner that spawns on demand that they dumped (the crate options'
// "selectable item" spawners, the safes' money stacks). ZWorldInventory::m_aItems
// would list the spawned items, but it reads 0 entries (LockpickFree.h); the
// spawners do not need it.
// The walk runs at scene playing, before crate #1 is planned, so the pool is
// complete then.
//
// Each spawner's item is named from pro.repo and kept unless (LevelItemVerdict):
// pro.repo does not name it, it has no inventory icon (not something the
// inventory holds), it is a firearm (gun crates only, from the gun pool),
// merces (a crate kind of its own), a container (a briefcase: it holds other
// items and is carried in the hands), ammunition, it has no Runtime id (no
// model to spawn), or it reads as story material (intel, evidence, a
// dossier, a document: a story that hangs on picking it up could break with
// a second copy; Freelancer runs no mission stories, so this is a guard, and
// each one left out is logged by name), or the player gets no pick-up for it
// (kNotPickupItems: a target's drink or food that 47 poisons where it stands,
// such as Berlin's "Glass (Red)", and the items pro.repo spawns with pick-up
// off; each one left out is logged by name). Spawners in evergreen_setup.brick
// (Freelancer's own systems: merces pickups, the hero item actions) are left
// out as well. Keys and keycards with the key icon are kept: a key from a
// crate opens its door early, which is the point. Items with the QuestItem
// icon are left out (Quest, each logged by name): a mission's story props (an
// NPC's ID card, an event pass, a note, an access code), not keys; Miami's
// Sierra Knox ID (a QuestItem with a keycard's name) lies invisible in a
// crate (a card with no thickness, hidden in the crate floor's plane). A few
// QuestItems are props 47 uses on the level (the
// Fuse Cell, Gun Powder): IsUsableQuestProp keeps them (logged as "kept:
// usable prop"). The pools leave out a QuestItem from the mission's option
// helpers too (CratePools::quest), the usable props again excepted. Items you own are left out
// by the pools (the CPD's owned lists), each item counts once whatever holds
// it, and a large item comes alone, as any regular item.

enum class LevelItemVerdict : uint8_t {
    Kept,
    Unnamed,   // pro.repo does not name it
    NotItem,   // no InventoryCategoryIcon (or an invalid one): nothing the inventory holds
    Firearm,   // gun crates only
    Merces,    // a crate kind of its own
    Container, // a briefcase or other container
    Ammo,      // ammunition
    NoModel,   // no Runtime id: nothing to spawn
    Story,     // intel, evidence, a dossier, a document
    NotPickup, // the player gets no pick-up for it (IsNotPickupItem): a target's drink or food to poison
    Quest,     // a mission's quest item (InventoryCategoryIcon QuestItem: IsQuestItemIcon)
};

inline constexpr size_t kLevelItemVerdicts = 11;

// InventoryCategoryIcon QuestItem (any case): a mission's prop (an NPC's ID card, an event pass, a fuse, ...). Merces
// with that icon are told apart before it (IsMercesItem).
bool IsQuestItemIcon(std::string_view icon);

// A QuestItem that is a usable prop, not a story item, by repository id (kUsableQuestProps): kept in crates. Fuse Cell
// (Dartmoor, Berlin, Mendoza, Chongqing: into a fuse box), Gun Powder (Sapienza, Isle of Sgail, Mendoza), Insecticide
// (Bangkok), Expired Canned Bolognese (Sick) (Mumbai, Isle of Sgail: a throwable, melee_nonlethal). Reviewed against
// the 42 QuestItems tools/rpkg/level_items.py --walk finds on the 15 maps; the rest are story items.
bool IsUsableQuestProp(const Guid& item);

const char* LevelItemVerdictName(LevelItemVerdict verdict);

// An item the player gets no pick-up for, by repository id (kNotPickupItems, src/core/NotPickupItems.inc, from
// tools/rpkg/level_items.py --inc): the 40 pro.repo items whose runtime template (Runtime) is built on one of the
// poisonable item cores (itemcore.template's itemcorepoisonable, _interruptible, freealign): a target's glass, cup,
// bottle or plate that the act hands the target and 47 can only poison where it stands (the core carries the act's
// item keywords and the poison interaction, and destroys the item a second after it is dropped), such as Berlin's
// "Glass (Red)" (Poisonable_RedwineGlass) and Sapienza's "Cup" (Poisonable_CoffeCup); and the 2 that pro.repo spawns
// with EnablePickUpOnSpawn false (an NPC's phone, an Android-only SMG). Every other item's runtime template is on a
// core with the pick-up interaction alone (itemcore, itemcorenophys, the weapon and ammo cores).
bool IsNotPickupItem(const Guid& item);

// The verdict from pro.repo's facts: name (Title, else CommonName; empty: not in pro.repo), CommonName,
// InventoryCategoryIcon, ItemType, whether it has a Runtime id, and whether the player can pick it up
// (IsNotPickupItem false).
LevelItemVerdict ClassifyLevelItem(std::string_view name, std::string_view commonName, std::string_view icon,
                                   std::string_view itemType, bool hasRuntime, bool canPickUp);

// The log's category: "key" for keys and keycards (icon or ItemType), else the icon in lower case ("?" for none).
std::string LevelItemCategory(std::string_view icon, std::string_view itemType);

// One ZItemSpawner outside the stash items and the mission's option helpers, as read at scene playing.
struct LevelSpawner {
    std::optional<Guid> item;         // its m_rMainItemKey's m_RepositoryId (empty: no key read)
    std::optional<bool> spawnOnStart; // m_bSpawnOnStart (empty: unread)
    bool freelancerBrick = false;     // in evergreen_setup.brick (Freelancer's own systems)
    // pro.repo's facts of its item (name empty: not there).
    std::string name; // Title, else CommonName
    std::string commonName;
    std::string icon;     // InventoryCategoryIcon
    std::string itemType; // ItemType
    bool hasRuntime = false;
};

struct LevelItems {
    std::vector<size_t> kept; // one spawner per kept item, sorted by the item's GUID
    size_t spawners = 0;
    size_t noItem = 0;                                  // no item key read
    size_t freelancer = 0;                              // in evergreen_setup.brick
    size_t startUnread = 0;                             // m_bSpawnOnStart unread
    size_t notAtStart = 0;                              // spawns on demand (m_bSpawnOnStart false)
    size_t atStart = 0;                                 // spawners left after those
    size_t distinct = 0;                                // distinct items among them
    std::array<size_t, kLevelItemVerdicts> verdicts {}; // distinct items by verdict
    std::vector<size_t> story;                          // one spawner per story item left out (for the log)
    std::vector<size_t> notPickup; // one spawner per item left out for having no pick-up (for the log)
    std::vector<size_t> quest;     // one spawner per quest item left out (for the log)
    std::vector<size_t> usable;    // one spawner per quest item kept as a usable prop (IsUsableQuestProp; for the log)
};

// The level's loose items: spawners with an item, outside evergreen_setup, that spawn at start; each item once,
// classified (ClassifyLevelItem).
LevelItems CollectLevelItems(std::span<const LevelSpawner> spawners);

// "412 item spawners outside the option helpers: 230 spawn at start (170 on demand, 2 unread, 4 in evergreen_setup,
// 6 without an item key); 85 distinct items: 52 kept (melee 20, distraction 11, key 6, ...): Crowbar, Keycard A,
// ... (+40 more); left out: 3 unnamed, 0 not inventory items, 9 firearms (gun crates only), ...; story items left
// out: Intel_Folder; no pick-up, left out: Glass (Red), Cup; quest items left out: Keycard (Sierra Knox ID); kept:
// usable prop: Fuse Cell".
std::string DescribeLevelItems(std::span<const LevelSpawner> spawners, const LevelItems& items, size_t maxNames = 12);

// --- world rotations ---------------------------------------------------------------------------------------------

// A world rotation: the X, Y and Z axes in world space (x, y, z each; world
// up is +Z), as ZSpatialEntity::m_mTransform's rows.
using WorldAxes = std::array<float, 9>;

// --- one crate's options -----------------------------------------------------------------------------------------

// The groups of options a crate's plan writes: the three tool slots, then the
// weapon pool's one-item (merces) option(s).
inline constexpr size_t kCrateGroups = 4;
inline constexpr size_t kCrateOneItemGroup = 3;

// What a crate's plan does with one option of a tool slot when that crate
// opens (written at its seed pin):
enum class CrateOptionAction : uint8_t {
    Offer,   // keys get the slot's item, the choice condition and owned flags set true
    Reuse,   // has another crate's item out (it came along to this crate): keys get the slot's item (it re-spawns as
             // that item), condition true: the slot draws it
    Exclude, // keys untouched, condition false (also an option whose item lies in this crate already)
    Rewrite, // another option with an item out: keys get the slot's item (no stale name), condition false
};

// The actions for the options of one tool slot. held[i]: the crate whose item
// option i still has out (offered there and not picked up; empty: free). An
// item nobody took goes along with the shared options to every later crate,
// so for crate `crate`:
// - an item was picked up from this slot of it (picked, a crate opened
//   again): every option is excluded (the slot stays empty);
// - else, when an option still has `crate`'s own item out (a crate opened
//   again): every option is kept out of the draw, the other options with an
//   item out rewritten. The item lies in the crate already, and an option
//   drawn again spawns its item a second time without removing the first
//   (see "items left behind" below);
// - else, when some option has an item out (from another crate): the first
//   is reused (its keys change, so it re-spawns as this crate's item) and
//   alone eligible, the other options with an item out are rewritten, the
//   free ones excluded;
// - else every option offers.
std::vector<CrateOptionAction> CrateSlotActions(std::span<const std::optional<size_t>> held, size_t crate, bool picked);

// --- an item's rest (which way up the game lays it) ---------------------------------------------------------------
//
// Each option's ZItemSpawner (m_bUsePlacementAttach true; its world transform
// the same as its m_eidParent's) is tuned for the option's own item:
// - The spawned item = the spawner's transform times the item's own placement
//   attach (ZItem m_PosPlacementAttach): the same item comes out turned the
//   same way against any spawner. Emetic Poison Syringe: local +X up on a +Z
//   spawner, -X up on Coin's -Z one; Electrical Keycard Hacker -Y up on three
//   +Z spawners; Icicle +Y up on its own +Z one. Items whose attach adds no
//   tilt (Pouch Flash, the Explosive Phone, Remote Taze; the RubberDuck
//   explosive a quarter turn of yaw) come out up as the spawner is.
// - So "which way up" belongs to the item, not to the option: item X lies
//   right when the spawner axis its own vanilla placements point up (its rest
//   axis) points up. The flat-only rule (local +Z up) was wrong: the Pouch
//   Flash and the RubberDuck explosive stand upright on +Z spawners (Fish's,
//   Icicle's) where the game lays them on their side; the options
//   with +X, -X, +Y or -Y up (0 deg off) are not tilted platforms but tuned
//   for items that rest on another axis.
// - Vanilla raises a spawner turned off +Z a little above the crate floor
//   (the flat ones' height): Napoleon figure's (+Y up) 4 cm, Coin's (-Z up)
//   3 cm.
// - The mission's other option helpers are no guide: Electrical Keycard
//   Hacker and Remote Taze would learn +Y, the Proximity Explosive S3 -Y from
//   them, and all three then lie on an edge; on the crate's +Z spawners they
//   show their top. Those helpers
//   are the supplier's stock and the like, which stand or hang items for
//   show, not crate floors.
// - Yaw: the crate's options are yawed for their own items (two families a
//   quarter turn apart). An item in another item's option comes out a
//   quarter turn off when its option is of the other family: the Icicle on
//   (Fiberwire) Phone Cord's option lies a quarter turn counter-clockwise from
//   its own option; Keycard Hacker and Remote Taze likewise on
//   Apple's / Fire Extinguisher's against Crystal Ball's / Iron's.
// - Where in its slot: each option's spawner also stands where its own item
//   sits right in the slot, not at one spot per slot (across the slot,
//   Bust_Colombi's slot 2 option stands 14 cm nearer the player than the
//   slot's others, Squeaky Toy Doll's 8 cm; along it the options of slot 1
//   differ by 3 cm).
// - An item with no option of its own lies a quarter turn off and too low
//   on a host option's spawner: Remote Taze on (Grenade) Emetic Gas's option
//   and ICA Device Remote Concussion on Kitchen Knife's lie flat (+Z) with
//   the host option's yaw and at the floor. The game files
//   (tools/rpkg/crate_poses.py) show why: neither is in the crate (setpiece_evergreen_stash_items), but
//   each has the box of a crate item (an option template's Boundary,
//   below): Remote Taze the ICA Proximity Taser's to half a millimeter, the
//   Remote Concussion Device within 5 mm. The Proximity Taser lies +Z up,
//   2.4 cm above the floor, in the Large items slot (the Remote Flash Device,
//   1.7 cm thick, 1.7 cm up). On the supplier's wall both hang with their
//   faces out, a quarter turn apart in their own axes, and the crate lays the
//   Proximity Taser as the wall shows it tipped back (the same map as the
//   Remote Flash Device and the Disposable Scrambler). So Remote Taze lies
//   as its twin, turned a quarter turn in its own axes: a quarter turn
//   clockwise from where Grenade Emetic Gas's, Apple's or Fire
//   Extinguisher's option yaws it. Electrical Keycard Hacker is the Disposable
//   Scrambler's twin (the same box, the same wall pose).
//
// At an open, each written item's rest is learned only from the crate's own
// tool-slot options whose own item it is (LearnCrateRest): such an option
// is the game laying that very item in this crate, so its spawner's whole
// vanilla rotation (rest axis and yaw) is copied onto the spawners that will
// spawn it (TurnToPose), with its height above the floor, and its spot in
// its slot (its offset from the slot's middle, CrateSlotMiddle) is kept in
// the slot written into. The mission's other option helpers are only
// logged. Else its twin's (CrateTwinOf, TwinCrateRest: the twin's own crate
// option, its pose turned to the item's axes, the twin's height only for the
// same model; the item's box centered in the slot), else the built-in table
// (or, not in it, its option template's box laid on its thin side: Box)
// (CrateRestFallback), else +Z (vanilla: the crate's flat
// options, right for most tools). Without a pose, each spawner that will
// spawn the item is turned (in its own frame, about its origin) so the rest
// axis points up, its yaw kept, raised or lowered to the item's height above
// the floor and moved to the middle of its slot (FitSpawner,
// ApplySpawnerTurn); with neither its own option nor a twin, its own box
// (CrateItemBoxCenter, from its option template in the game files) is
// centered in the slot as a twin's is: an item laid as its twin is centered by
// its box center (the twin's, turned) on the mean of the slot's box centers
// (CrateSlotBoxMiddle), not on the mean of the spawners (the side-lying
// items' spawners stand at one end of the slot). A turned twin's height is
// not taken. Heights are measured from each slot's own floor
// (CrateFloorHeight of its options): the transforms read at an open are where
// the stash stood last, possibly tilted. An item with neither its own option
// nor a twin keeps +Z (the map from the supplier's wall to the crate that
// most flat devices share lays it +Z up); one with no option template, such
// as a level's loose item, still goes to the slot's middle.

// A world position (m).
using WorldPoint = std::array<double, 3>;

// A spawner's local axis: 0 X, 1 Y, 2 Z, and its sign.
struct LocalAxis {
    uint8_t index = 2;
    bool negative = false;

    bool operator==(const LocalAxis&) const = default;
};

inline constexpr LocalAxis kLocalUp {2, false}; // +Z

// "+Z".
std::string LocalAxisText(LocalAxis axis);

// A spawner axis counts as pointing up within this many degrees (measured in
// game: 0 or 1, one spawner 9).
inline constexpr double kCrateRestMaxTiltDegrees = 20.0;
// The crate floor is the height of the stash spawners whose +Z is this close
// to world up.
inline constexpr double kCrateFlatMaxTiltDegrees = 15.0;
// m: an item resting on an axis other than +Z, with no height learned, lies
// this far above the floor (vanilla: Napoleon figure's 4 cm, Coin's 3 cm).
inline constexpr double kCrateSideLiftMeters = 0.03;

// The local axis of a rotation that points up within maxTiltDegrees (empty:
// none does, or a zero / NaN axis).
std::optional<LocalAxis> UpLocalAxis(const WorldAxes& axes, double maxTiltDegrees = kCrateRestMaxTiltDegrees);

enum class CrateRestSource : uint8_t {
    Learned,  // the crate's own tool-slot option(s) for the item
    Twin,     // none in this crate: its twin's (CrateTwinOf), turned to the item's axes
    Table,    // neither: the built-in table
    Box,      // none of those: its box in the game files has one clearly thin side, laid up (CrateBoxRestAxis)
    Default,  // none of those: +Z (vanilla: lying as the flat options lay their own items)
    Measured, // seen standing at an earlier spawn this load: laid on its thinnest side as measured (UseMeasuredRest)
};

// Where a vanilla placement of an item is.
enum class CratePlacement : uint8_t {
    ToolSlot, // a spawner of an option of the crate's three tool slots: learned from
    Other,    // any other option helper (the mission's: supplier stock, reward crates; the crate's one-item
              // option): logged, never learned from
};

struct CrateRest {
    LocalAxis axis;             // the spawner axis that points up when the game lays the item
    std::optional<double> lift; // m: its spawner above the crate floor (empty: not learned)
    // Learned: the vanilla world rotation of the crate option it is learned from (rest and yaw), to copy
    // (TurnToPose; a world rotation, so only valid in the crate it was read in: re-read at each open), the
    // sample it came from (index into the samples) and that sample's source.
    std::optional<WorldAxes> pose;
    size_t poseSample = SIZE_MAX;
    std::string poseFrom;
    // Learned, with the pose: that option's spawner from the middle of its own slot (CrateSlotMiddle; world, so
    // re-read at each open as the pose is). The item goes to the same spot in the slot it is written into.
    std::optional<WorldPoint> offset;
    // Twin: the twin's name (for the log) and the turn from its axes to the item's (CrateTwin::turn; the pose is
    // the twin's option's rotation times it).
    std::string twin;
    std::optional<std::array<int8_t, 9>> twinTurn;
    // Twin: the item's box center from its spawner, in the spawner's axes (the twin's box, turned to the item's axes;
    // empty: unread). Table, Default: its own box's (CrateItemBoxCenter; empty: no option template for it). With it
    // the item is centered in the slot it is written into (FitSpawner).
    std::optional<std::array<double, 3>> center;
    // Table, Box, Default: its option template's box size along the spawner's axes (CrateItemBoxSize; empty: none).
    std::optional<std::array<double, 3>> boxSize;
    // Table, Default: the center is the item's geometry measured where it lay at an earlier spawn this load
    // (UseMeasuredCenter), not its option template's box.
    bool centerMeasured = false;
    CrateRestSource source = CrateRestSource::Default;
    size_t agree = 0;   // Learned: tool-slot placements naming the axis
    size_t usable = 0;  // Learned: tool-slot placements read (attach on, an axis up)
    size_t samples = 0; // tool-slot placements found
    size_t others = 0;  // other placements found (not learned from)
};

// One vanilla placement of an item: a spawner of an option helper whose own
// item it is.
struct CrateRestSample {
    std::optional<WorldAxes> axes; // its vanilla world rotation (empty: unread)
    std::optional<bool> attach;    // m_bUsePlacementAttach (empty: unread)
    std::optional<double> lift;    // m above the crate floor (the crate's options only; empty: unknown)
    CratePlacement where = CratePlacement::ToolSlot;
    std::string source; // for the log: "slot 1 option 0fce7991058edbed, spawner b77691e22dd91fc8"
};

// The rest axis most usable tool-slot samples name (attach not false, an
// axis up: see UpLocalAxis; a tie: the one named first); its lift the median
// of those samples' lifts; its pose the first of them. Other samples are
// counted, never used. Empty when no tool-slot sample is usable.
std::optional<CrateRest> LearnCrateRest(std::span<const CrateRestSample> samples);

// An item with no option of its own in the crate: the table checked in game
// (Pouch Flash +Y; the RubberDuck explosives (one model: the same attach) and
// the (Grenade) Emetic +Y, on their side; Napoleon figure +Y; (Tool) Coin
// Classic -Z), else its box's thin axis (CrateBoxRestAxis of
// CrateItemBoxSize: source Box), else +Z; each with its own box center from
// the game files (CrateItemBoxCenter; empty: no option template for it), by
// which it is centered in the slot.
//
// The Explosive Phone's option template box is 14.4 x 7.5 x 1.0 cm along its
// spawner's X, Y, Z: X is its length, Z its thickness. The box is the card the
// menu draws over the item, a centimeter thin along the axis it is looked at
// from; in 91 of the crate's 94 options with a box that is the axis the
// crate lays up (the other three: two balls and the phone cord), so with no
// better guide an item lies with it up: on its widest face.
CrateRest CrateRestFallback(const Guid& item);

// A box's side counts as thin when it is at most this share of the next
// smallest (the cards are about 1 cm against 4 cm and more; a ball's sides
// are within a few millimeters).
inline constexpr double kCrateBoxThinRatio = 0.5;

// The axis of a box (sizes along the spawner's axes, m) that is clearly its
// thinnest (kCrateBoxThinRatio), positive; empty when none is (a ball, a
// cube) or a size is NaN or negative.
std::optional<LocalAxis> CrateBoxRestAxis(const std::array<double, 3>& size);

// An item with no crate option of its own that has a twin: a crate item with the same box in the game files (the
// same model), and the turn from the twin's axes to the item's: turn[3 * j + i] = (twin axis j) . (item axis i), so
// TurnAxes(twin's pose, turn) is the item's pose. Derived offline by tools/rpkg/crate_poses.py --twins (kCrateTwins).
struct CrateTwin {
    Guid twin;
    std::array<int8_t, 9> turn {};
};

std::optional<CrateTwin> CrateTwinOf(const Guid& item);

// The item's rest from its twin's learned one (twinRest: LearnCrateRest of the twin's own crate options): the twin's
// pose turned to the item's axes (its rest axis the one that then points up; without a pose, the twin's rest axis
// in the item's axes). Its height is the twin's only when the turn is none (the same model: the ducks, the
// grenades, the syringes); a turned twin is another model (Remote Taze, the Remote Concussion Device), so it gets no
// height (FitSpawner's default: a flat one at the floor). Never the twin's spot (no offset): the item is centered in
// its slot by its box (CrateRest::center). Source Twin; twinName for the log.
CrateRest TwinCrateRest(const CrateRest& twinRest, const std::array<int8_t, 9>& turn, std::string twinName);

// An item's box center from its spawner, in the spawner's axes (m): the Boundary of its option template
// (helper_evergreen_selectableitem_<item>) in the game files, from tools/rpkg/crate_poses.py --boxes
// (kCrateItemBoxes, src/core/CrateItemBoxes.inc). Empty: no option template for it.
std::optional<std::array<double, 3>> CrateItemBoxCenter(const Guid& item);

// The same box's size along the spawner's axes (m; one side about a centimeter: the card the menu draws). Empty: no
// option template for it.
std::optional<std::array<double, 3>> CrateItemBoxSize(const Guid& item);

// Learned from the samples, else from the twin's samples (twinSamples: the crate's options whose own item is the
// item's twin, CrateTwinOf; empty when it has none), else CrateRestFallback (with its own box center; others counted
// either way).
CrateRest CrateRestFor(const Guid& item, std::span<const CrateRestSample> samples,
                       std::span<const CrateRestSample> twinSamples = {}, std::string twinName = {});

// "+X up (learned from the crate's own option: 1 of 1 agree; pose and yaw of
// slot 1 option ..., spawner ...; 0.04 m above the floor)".
std::string DescribeCrateRest(const CrateRest& rest);

// A turn that only swaps and flips axes (a multiple of 90 degrees), as a
// row-major 3x3 matrix acting on local column vectors.
using AxisTurn = std::array<int8_t, 9>;
inline constexpr AxisTurn kNoTurn {1, 0, 0, 0, 1, 0, 0, 0, 1};

// The smallest turn T with T * rest = up: none when equal, 180 degrees about
// local Z (about X for a rest on Z) when opposite, else 90 degrees about
// rest x up.
AxisTurn TurnRestToUp(LocalAxis rest, LocalAxis up);

// The inverse (the transpose).
AxisTurn InverseTurn(const AxisTurn& turn);

// A world transform: rotation rows (WorldAxes) and position.
struct SpatialTransform {
    WorldAxes axes {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<float, 3> at {};
};

// The mod's change to one spawner: the turn in its own frame, a world height
// change (m), and a move along the floor (m), kept along the vanilla
// spawner's own axes: the crate stands yawed differently at each open, so a
// world x, y move would not undo right at the next one.
struct SpawnerTurn {
    AxisTurn turn = kNoTurn;
    double lift = 0.0;
    std::array<double, 3> slide {};

    bool operator==(const SpawnerTurn&) const = default;
};

// The world rotation W times the turn (the new local axis i points where
// sum_j turn[j][i] * W's axis j did).
WorldAxes TurnAxes(const WorldAxes& axes, const AxisTurn& turn);

// vanilla with the change applied, and back (the change undone from now).
SpatialTransform ApplySpawnerTurn(const SpatialTransform& vanilla, const SpawnerTurn& change);
SpatialTransform UndoSpawnerTurn(const SpatialTransform& now, const SpawnerTurn& change);

// world relative to parent (what ZSpatialEntity's m_mTransform property
// holds: the transform under m_eidParent); a scaled parent axis is divided out.
SpatialTransform LocalTransform(const SpatialTransform& parent, const SpatialTransform& world);

// The other way: local (under parent) in the world.
SpatialTransform WorldTransform(const SpatialTransform& parent, const SpatialTransform& local);

// Within 0.02 per axis component and 1 cm.
bool SameTransform(const SpatialTransform& a, const SpatialTransform& b);

// The crate floor: the median height of the spawners whose +Z points up
// (kCrateFlatMaxTiltDegrees); empty when none. Given one slot's options'
// spawners, that slot's floor: heights are measured from it, so a crate
// standing tilted along its slots gives each slot's items their own height.
std::optional<double> CrateFloorHeight(std::span<const SpatialTransform> spawners);

// The middle of a crate slot: the mean position of its options' vanilla
// spawners (each tuned for its own item, so the mean is where a typical item
// sits); empty when none is read (a NaN position is skipped).
std::optional<WorldPoint> CrateSlotMiddle(std::span<const SpatialTransform> spawners);

// m: a slot's box middle farther than this from its spawners' middle (along the floor) is not of the same read.
inline constexpr double kCrateBoxMiddleMaxMeters = 0.25;

// Where items sit in a crate slot: the mean of its options' vanilla box centers (boxes: their world transforms),
// along the floor; empty when none is read or it is farther than kCrateBoxMiddleMaxMeters from spawnerMiddle (the
// slot's CrateSlotMiddle; empty: no check). Its height is spawnerMiddle's (the floor move only is used).
std::optional<WorldPoint> CrateSlotBoxMiddle(std::span<const SpatialTransform> boxes,
                                             std::optional<WorldPoint> spawnerMiddle);

// A spawner move below this (m, per axis) is left out: the spawners stand to
// the centimeter, and a change of a few millimeters is not worth a write.
inline constexpr double kCrateMinSlideMeters = 0.005;

// The turn (in the spawner's own frame) that gives a spawner rotated
// `vanilla` the world rotation `pose`: TurnAxes(vanilla, turn) == pose, when
// the two differ only by swapped and flipped axes (each within
// kCrateRestMaxTiltDegrees: the crate's options, read in the same crate).
// Empty otherwise (a zero or NaN axis, or no such turn).
std::optional<AxisTurn> TurnToPose(const WorldAxes& vanilla, const WorldAxes& pose);

// One spawner against an item's rest:
struct SpawnerFit {
    std::optional<LocalAxis> up; // its vanilla up axis (empty: none within kCrateRestMaxTiltDegrees)
    SpawnerTurn change;          // what to write (kNoTurn, 0: vanilla)
    bool fits = false;           // vanilla already lays the item so (nothing to change)
    bool posed = false;          // turned to the rest's pose (rest axis and yaw), not just its axis
    bool ownSpot = false;        // moved to the item's own spot in the slot (the rest's offset), not the middle
    bool centered = false;       // moved so the item's box center (the rest's center) is on the slot's box middle
};

// vanilla: the spawner's vanilla transform; floor: the floor of the slot the
// spawner's option is in (empty: unknown); slotMiddle: the middle of that
// slot (CrateSlotMiddle; empty: unknown, the spot is left); slotBoxMiddle:
// its box middle (CrateSlotBoxMiddle; empty: unknown). With a pose that
// TurnToPose reaches, the turn gives the spawner that pose, and it goes to
// the item's own spot in the slot (the slot's middle plus the rest's offset).
// Else a spawner with no up axis is left vanilla; one whose up axis is the
// rest axis keeps its rotation and height, any other is turned so the rest
// axis takes its vanilla up axis's place. Without an own spot, an item with
// a box center (the rest's center: a twin's, or its own from the game files)
// and the slot's box middle known is centered: its box center, carried by the
// spawner's new rotation, goes on the box middle; else the spawner goes to the slot's middle (the option's spot
// is tuned for its own item). A posed or turned one, with the floor known,
// goes from the spawner's vanilla lift to the rest's (unknown: 0 for +Z,
// else kCrateSideLiftMeters). To the millimeter; a move below
// kCrateMinSlideMeters is left out. fits: nothing to change.
SpawnerFit FitSpawner(const SpatialTransform& vanilla, std::optional<double> floor, const CrateRest& rest,
                      std::optional<WorldPoint> slotMiddle = std::nullopt,
                      std::optional<WorldPoint> slotBoxMiddle = std::nullopt);

// An option's spawners for its written item:
enum class CrateFit : uint8_t {
    Fits,   // vanilla, it already lays the item on its rest axis
    Turned, // turned to the rest axis (the write read back)
    Unfit,  // neither (no up axis, or the write did not take)
    Unread, // no spawner, or its rotation could not be read: offered as before
};

// From its spawners: Unfit when any is, else Turned when any is, else Fits
// when any does, else Unread.
CrateFit OptionFit(std::span<const CrateFit> spawners);

struct CrateFitPick {
    size_t keptOut = 0;     // offering options turned to Exclude (unfit)
    size_t unread = 0;      // offering options unread (still offered)
    size_t turned = 0;      // offering or reused options turned to the item's rest
    size_t unfitReused = 0; // a reused option (forced, left as it is) that is unfit
    bool fallback = false;  // every offering option unfit: all still offer
};

// A planned slot's actions (CrateSlotActions) against its options' fits
// (same order; missing: unread): every Offer that is Unfit becomes Exclude,
// unless no Offer would be left (fallback: unchanged). Reuse, Rewrite and
// Exclude stay as they are (an unfit reused option is counted).
CrateFitPick KeepUnfitOut(std::span<CrateOptionAction> actions, std::span<const CrateFit> fits);

// The branch weights crate_items writes for a crate.
enum class CrateBranchSteer : uint8_t { Vanilla, Tool, Weapon };

enum class CrateBranchReason : uint8_t {
    NoPlan,          // vanilla: no plan (the crate as the game fills it)
    OneItem,         // weapon pool: a one-item crate (a gun or a large item in the merces option)
    Merces,          // weapon pool: a merces crate (the merces option with its own item)
    ToolPlan,        // tool pool: three items (regular, tools or a gun): no merces crate unless planned
    OneItemHeld,     // tool pool: the one-item option still has another crate's item out (a vanilla plan)
    ToolSlotItemOut, // tool pool: a tool slot still has an item out (a vanilla plan; it would lie beside the merces)
    Other,           // vanilla: a vanilla plan (too few items), nothing out
};

struct CrateBranchPick {
    CrateBranchSteer steer = CrateBranchSteer::Vanilla;
    CrateBranchReason reason = CrateBranchReason::NoPlan;
};

// plan: the crate's plan (null: none). oneItemHeld: the one-item option has
// another crate's item out. toolItemOut: an option of a tool slot has an
// item out (after this crate's plan was written).
CrateBranchPick CrateItemsBranch(const CratePlan* plan, bool oneItemHeld, bool toolItemOut);

// --- selector boxes ----------------------------------------------------------------------------------------------
//
// The box drawn over the item you look at in a crate keeps the option's own
// item's size after a swap. The game files
// (tools/rpkg/crate_poses.py) show where it comes from: every crate option
// template (helper_evergreen_selectableitem_<item>, 296 of them) holds the
// shared option helper and exactly one ZBoxVolumeEntity, "Boundary",
// m_vGlobalSize (SVector3) sized for its item, its m_mTransform under the
// helper's "Pose when resting" (the exposed ItemTransform; the item spawner
// hangs under the same pose). The template sets the helper's m_boundary to
// it, which the helper aliases to its ZEvergreenMenuEntryEntity
// "EvergreenMenuEntry" (54a19f285df200e5): the menu entry's boundary. That
// the highlight follows a write at run time is not confirmed.
//
// With crate_even_boxes (on), every option of the three tool slots that may
// be drawn in a crate crate_items writes gets one size, the crate's own
// medians (CommonCrateBoxSize; the 65 tool-slot options' vanilla boxes in
// the game files give 0.12 m across, 0.01 m high: most are flat), turned to the
// box's axes (CrateBoxExtent), and is centered where its item now lies: the
// written item's own box center carried to the spawner as the mod placed it
// (CrateBoxCenter). The box's rotation is left as it is.

// A crate option's box: its world transform (rows: its axes) and m_vGlobalSize (m, along its own axes).
struct CrateBox {
    SpatialTransform world;
    std::array<double, 3> size {};
};

struct CrateBoxSize {
    double side = 0.0;   // m, across the floor
    double height = 0.0; // m, up
};

// The common size: side the median of every box's two sides across (its axes other than the one nearest world up),
// height the median of their heights. Boxes with a NaN or negative size are skipped; empty when none is left.
std::optional<CrateBoxSize> CommonCrateBoxSize(std::span<const CrateBox> boxes);

// m_vGlobalSize for a box rotated so: its axis nearest world up gets the height, the other two the side.
std::array<double, 3> CrateBoxExtent(const WorldAxes& axes, const CrateBoxSize& size);

// A point in a spawner's own axes (from its origin), and back.
std::array<double, 3> PointInSpawner(const SpatialTransform& spawner, const WorldPoint& point);
WorldPoint PointFromSpawner(const SpatialTransform& spawner, const std::array<double, 3>& local);

// A point in a twin's spawner axes in the item's (CrateTwin::turn): local_i = sum_j turn[3 * j + i] twin_j.
std::array<double, 3> TurnPoint(const std::array<double, 3>& twinLocal, const std::array<int8_t, 9>& turn);

// Where a box goes: the written item's box center in its spawner's axes (itemCenter, from the item's own crate
// option, or its twin's) from the spawner as the mod placed it (now); unknown: the box's vanilla center when the
// spawner is vanilla, else the spawner's origin (where the item lies).
WorldPoint CrateBoxCenter(const SpatialTransform& spawnerVanilla, const SpatialTransform& spawnerNow,
                          const WorldPoint& boxVanilla, const std::optional<std::array<double, 3>>& itemCenter);

// "0.12 x 0.12 x 0.01 m".
std::string DescribeCrateBoxSize(const CrateBoxSize& size);

// --- the spawned item's middle -----------------------------------------------------------------------------------
//
// A spawned item need not lie at its spawner: the Fuse Cell, a level item
// (crate_level_items) with no option template and so no box, lies 0.28 m from
// its spawner, turned (-Y up), almost in the next slot: its runtime template (prop_tool_fuse_cells_runtime) hangs the model
// from a PropHelper_Item_Placement 0.28 m off the model's origin, its axes
// turned, and a spawner with m_bUsePlacementAttach lays the item by that
// helper. Any item may carry such a helper. The boxes in the game files
// cover only the 296 option templates, and no more can be made offline: of
// the 1509 repository items with a runtime template, 941 templates lie in
// chunk0.rpkg, of which only a few ranges are copied, and of the other
// 568 only 2 models could be read.
//
// So each item a crate option spawns is measured where it lies: its
// geometry's bounds (its ZGeomEntity's primitive: the model's own min and
// max) carried by its world transform give its middle, compared with where
// the mod meant the middle to be (the option's center target: the item's box
// center from its spawner as placed, or with no box the slot's box middle).
// An item more than kCrateCenterSlackMeters off along the floor (placed by a
// box from the game files: kCrateCenterBoxSlackMeters, so the crates checked
// in game stay as they are) is moved by the difference (its selector box
// with it). Its measured middle, in its spawner's axes (the helper holds the
// model fixed there), is kept when it had no box or its box missed, and
// centers it at its next placement this load as a box from the game files
// would (UseMeasuredCenter). A miss beyond kCrateCenterMaxMissMeters, or
// bounds that do not read as a small item's, are not trusted: logged, left as
// it lies.

// A model's bounds in its own axes (m).
struct ItemBounds {
    std::array<double, 3> min {};
    std::array<double, 3> max {};
};

// m: an item's middle this close to its target along the floor is left (the boxes the crate's items are centered by
// are the game's selector boxes, a centimeter or two off their models).
inline constexpr double kCrateCenterSlackMeters = 0.02;
// m: the same for an item placed by a box from the game files (its own option's, a twin's, its option template's): a
// selector box is drawn for the hand, not fitted to the model, so only a clear miss counts (the Proximity Explosive
// S3's 4.8 cm was seen in game).
inline constexpr double kCrateCenterBoxSlackMeters = 0.04;
// m: a miss farther than this is not trusted (a crate slot is about 0.4 m across).
inline constexpr double kCrateCenterMaxMissMeters = 0.5;
// m: bounds with a side longer than this are not an item's.
inline constexpr double kCrateItemMaxSideMeters = 1.5;

enum class CrateCentering : uint8_t {
    Centered, // within the slack: left as it lies
    Moved,    // farther: to be moved by the shift
    Unsized,  // the bounds are not a small item's (NaN, inverted, empty, or a side over kCrateItemMaxSideMeters): left
    TooFar,   // farther than kCrateCenterMaxMissMeters: not trusted, left
};

struct CrateCenterCheck {
    CrateCentering verdict = CrateCentering::Unsized;
    std::array<double, 3> size {};        // m, along its own axes
    WorldPoint center {};                 // its middle (world)
    std::array<double, 3> centerLocal {}; // its middle in its spawner's axes (from the spawner's origin)
    std::array<double, 3> shift {};       // world, along the floor (z 0): the target minus its middle
    double miss = 0.0;                    // m: the shift's length
};

// item: the spawned item's world transform; bounds: its model's, in its own axes; spawner: its spawner's world
// transform now; target: where its middle should be (world; only the move along the floor counts); slack:
// kCrateCenterSlackMeters, or kCrateCenterBoxSlackMeters for a target from a box.
CrateCenterCheck CheckItemCenter(const SpatialTransform& item, const ItemBounds& bounds,
                                 const SpatialTransform& spawner, const WorldPoint& target,
                                 double slack = kCrateCenterSlackMeters);

// "0.10 x 0.06 x 0.06 m, its middle (+0.00, +0.28, +0.03) m from its spawner, 0.28 m off along the floor: moved".
std::string DescribeCenterCheck(const CrateCenterCheck& check);

// A rest with no option of its own and no twin (Table, Box, Default, Measured) takes the item's measured middle as
// its center (centerMeasured), in place of its box from the game files or none. False (unchanged) for Learned and
// Twin: their spot and box are the game's own placement of that model.
bool UseMeasuredCenter(CrateRest& rest, const std::array<double, 3>& center);

// --- an item not on its widest face --------------------------------------------------------------------------------
//
// The rests above come from the game's own placements and its option
// templates' boxes; a level item (crate_level_items) or a mission's item has
// none, and lies +Z up as vanilla, which stands an item whose model is long
// along its Z upright. The same measurement that centers it (its model's
// bounds, CenterOffers) shows it: when its thinnest model side is not the
// one pointing up and it stands taller than that side (kCrateLayRatio,
// kCrateLaySlackMeters: any real difference, so a cube or a ball stays as it
// lies), it is laid on its widest face where it lies (LayItemFlat: its
// thinnest axis turned up by the smallest quarter turn, its middle kept along
// the floor, its bottom kept at the floor's height) and at its next
// placement this load its spawners are turned for it (UseMeasuredRest), so
// it is seen whole from above. A rest the game gave (Learned, Twin: the
// crate's own placement of that model; the guns too, which lie in the
// one-item option as vanilla) is never changed. (A Soda can, 0.08 x 0.08 x
// 0.14 m, and a Bust, 0.10 x 0.12 x 0.21 m, both level items lying +Z up as
// vanilla, are laid down by this.)

// An item lies on its widest face already when its height is at most this many times its thinnest side, or at most
// kCrateLaySlackMeters more (a cube, a ball, a can already on its side: no turn).
inline constexpr double kCrateLayRatio = 1.15;
inline constexpr double kCrateLaySlackMeters = 0.01;

// The spawner axis that should point up for the item to lie on its widest face: its model's thinnest axis (bounds,
// in its own axes; item: its world transform) as the spawner's local axis nearest it (within
// kCrateRestMaxTiltDegrees; spawner: its world transform now). Empty when it lies so already (that axis within
// kCrateRestMaxTiltDegrees of up, or its height within kCrateLayRatio and kCrateLaySlackMeters of that side), when
// the bounds do not read as a small item's, or when no spawner axis lies along it.
std::optional<LocalAxis> MeasuredRestAxis(const SpatialTransform& item, const ItemBounds& bounds,
                                          const SpatialTransform& spawner);

// The item's transform laid on its widest face where it lies: its thinnest model axis turned up (the quarter turn in
// its own frame that takes it to the local axis now pointing up; its sign kept: a turn of a quarter, never a half), its
// middle kept along the floor and its bottom at the same height. Empty when MeasuredRestAxis would be (nothing to do,
// or unreadable) or no local axis points up (kCrateRestMaxTiltDegrees).
std::optional<SpatialTransform> LayItemFlat(const SpatialTransform& item, const ItemBounds& bounds);

// A rest with no option of its own and no twin (Table, Box, Default) takes the axis measured (MeasuredRestAxis) at an
// earlier spawn: source Measured, no height (a turned item: kCrateSideLiftMeters, or the floor for +Z), and no
// center (the box or middle was for the other side up; it is centered by its geometry when it spawns). False
// (unchanged) for Learned, Twin and Measured.
bool UseMeasuredRest(CrateRest& rest, LocalAxis axis);

// --- items left behind -------------------------------------------------------------------------------------------
//
// A crate opened twice in a row draws its slots again with the same seed and
// every drawn option spawns its item a second time; the first items stay and
// come along with the shared options to the next crates. A key change
// replaces an option's item (its spawner's m_bDestroyItemOnKeyChange); a
// draw of an option whose item is out spawns another beside it. So nothing
// is drawn in a slot whose item still lies in the crate (CrateSlotActions).

// One option holding an item that was just picked up: its crate and when that
// crate last opened (an open counter).
struct CrateHold {
    size_t crate = 0;
    uint32_t lastOpen = 0;
};

// Which of the holders gave the pickup: the one whose crate opened last (the
// first such on a tie; empty: none).
std::optional<size_t> CratePickupSource(std::span<const CrateHold> holds);

// No one-item crate is possible (the one-item option holds another crate's
// item, a tool slot has an item out, or the crate has no one-item option):
// large items leave the pools, and no gun or merces crate is planned (oneItem
// off: the roll falls through to a regular crate).
void DropOneItemCrates(CratePools& pools);

} // namespace fr
