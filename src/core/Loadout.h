#pragma once

// Pure logic of the random loadout (random_loadout): when a Freelancer
// mission starts, the gear slots the player left free in the safehouse are
// filled from the items they own, in priority order:
//   1. a lockpick (Lockpick Mk III, the Freelancer tool, else the Collector's
//      Lockpick), unless one is already packed (LoadoutInput::lockpickFirst:
//      random_loadout_lockpick_first; false skips this rule);
//   2. what the chosen prestige objective needs (OptinPayoutObjective: an epic
//      sniper rifle for "Epic Sniper Kill", nothing for Silent Assassin);
//   3. what the mission's client requirements (common objectives) need (a
//      knife for "Knife Kill"), the one with the highest payout in this
//      mission first (LoadoutInput::clientReqPayouts; equal payouts keep the
//      CPD's order, unknown payouts go last);
//      rules 2 and 3 place every needed item first, then the helpful ones
//      (a taser device for the water canister trap), then the least
//      important (a distraction item) (ItemNeed::order), the prestige
//      objective's ahead of the client requirements' within each; a need with
//      preferred items (ItemNeed::prefer: a syringe for a poison, a shotgun
//      or a piercing firearm for Collateral Kill - Firearms) draws from the
//      best of them that fits, else from any match; before those, one owned
//      item that meets two of the needs of rules 2 and 3 at once (a legendary
//      shotgun for a legendary firearm and a shotgun kill) is placed first,
//      for each pair in that order, when one fits, so a pair can take a slot
//      ahead of a single need placed earlier in the order;
//      LoadoutInput::objectiveItems (random_loadout_objective_items) false
//      skips rules 2 and 3;
//   4. random items, weighted by their slot cost, until nothing else fits;
//      items of a category (LoadoutCategory) not yet in the loadout, packed
//      items and rules 1-3 included, are drawn first; a category already
//      there only when no other fits (rules 1-3 keep theirs regardless).
// A firearm kill need (every prestige and client requirement that names a
// firearm) is never met by a dart gun (LoadoutItem::dartGun: it can't kill),
// packed or owned; the rarity is the item's live Evergreen_Rarity, the
// weapon rarity shuffle's when that is on (it writes pro.repo before the plan).
// With prestige 116 (Silent Assassin, No Firearms) no rule picks a firearm
// (IsFirearmItem; dart guns are pistols in pro.repo): a need only firearms
// meet is skipped and noted, and slots may stay empty; packed items are the
// player's and stay. Common objective 72 (No Firearms) keeps only the random
// fill off firearms.
// Briefcase items only with LoadoutInput::briefcaseItems (the inverse of
// random_loadout_holsterable_only: the mod delivers a large item inside an ICA Briefcase, the way
// the game delivers a large item bought in a mission; false: no rule picks
// one and a need that only briefcase items meet is skipped and noted). Never
// more slots than the player has, at most one briefcase item in the
// whole loadout (packed items included), never an item twice, only items the
// player owns. Items already packed satisfy rules 1-3. Every draw depends on
// (campaign seed, scene, campaign step) and the items' ids, never their
// order, so a restart of the mission plans the same loadout. The one
// exception to "only items the player owns": the safehouse pickups of
// kSpawnedItems (grapes and a banana, which can never be owned), spawned for
// Make Target Slip alone (ItemNeed::spawned).

#include "Guid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// The Freelancer tool Lockpick Mk III and the Collector's Lockpick.
inline constexpr std::string_view kLockpickMk3Id = "d3387f28-866d-4262-88cb-6e5b1076bac0";
inline constexpr std::string_view kCollectorsLockpickId = "3c52b8c3-f4a5-4fcd-ae16-ff45b56d4351";

// DialHash's feature name of the plan's random stream.
inline constexpr std::string_view kLoadoutFeature = "random loadout";

bool IsLockpickId(const Guid& id);

// Where an owned item sits in the safehouse: the gear wall (CPD
// PersistentItems) or the Freelancer tools (CPD TransientItems); or Spawned: a
// safehouse pickup the player never owns (kSpawnedItems), in no list, so
// nothing is taken from one, lent or put back.
enum class LoadoutSource : uint8_t { Persistent, Transient, Spawned };

const char* LoadoutSourceName(LoadoutSource source);

// One item as the planner sees it (from pro.repo; see RandomLoadout.cpp).
struct LoadoutItem {
    Guid id;
    std::string name; // CommonName / Title, for the log and the name keywords
    std::string
        kind; // ItemType ("eCC_Knife", ...: an animation class; quest / ammo items only, never a keyword), may be empty
    std::string icon;                // InventoryCategoryIcon, lower case
    std::vector<std::string> traits; // OnlineTraits, lower case ("melee_lethal", "poison", "case", ...)
    std::vector<std::string> perks;  // Perks, lower case ("suppressor", "piercing", "none", ...)
    int32_t capacity = 0;            // Evergreen_CapacityCost
    int32_t rarity = 0;              // Evergreen_Rarity (0: none)
    bool silenced = false;
    bool dartGun = false;   // a dart gun (fr::IsDartGun): a pistol that never kills
    std::string itemSize;   // ItemSize ("ITEMSIZE_LARGE", ...), empty when missing or not a string
    std::string holster;    // HolsterAbility ("eHolsteringAllowed", ...), likewise
    bool briefcase = false; // a briefcase item (IsBriefcaseItem), delivered in a briefcase (the caller decides how)
    bool known = true;      // found in pro.repo
    LoadoutSource source = LoadoutSource::Persistent;
};

// The safehouse pickups the mod spawns for a need that allows them
// (ItemNeed::spawned: Make Target Slip only), never for the random fill: no
// list of the player's ever holds them, so they could never be brought
// otherwise. Each takes its own fixed number of slots, whatever pro.repo says.
struct SpawnedItem {
    std::string_view id;
    std::string_view name; // pro.repo's, for the log
    int32_t cost = 0;
};

inline constexpr std::array<SpawnedItem, 2> kSpawnedItems {{
    {"3fbd6da4-c61c-40d6-9494-8277d2e172e4", "GrapeVine", 1},
    {"903d273c-c750-441d-916a-31557fea3382", "Banana", 2},
}};

// Slots the item takes: a lockpick always 1, a spawned item (LoadoutSource::
// Spawned) its kSpawnedItems cost, otherwise its capacity cost (at least 0).
int32_t SlotCost(const LoadoutItem& item);

// A firearm by icon or trait: pistol, smg, assaultrifle, shotgun, sniperrifle.
// Dart guns are pistols by both ("(Dartgun) Sick" and "(S3) (Dartgun) Kalmer 2
// - Tranquilizer" are [pistol; pistol]).
bool IsFirearmItem(const LoadoutItem& item);

// A HolsterAbility that keeps the item out of the pockets: any but
// eHolsteringAllowed (carried in the hands; packed, it goes in a briefcase; see
// ConcealableMace.h). eHolsterSecondaryOnly too: the (Pomada) Boombox
// Distraction has it, and 47 can't holster it; and
// eUndecided, so an item never arrives loose that might not fit a pocket.
// Empty is not.
bool IsHandsOnlyHolster(std::string_view holster);

// The briefcase guess when no item has an ItemSize key: a sniper rifle,
// assault rifle or shotgun icon (any case).
bool IsLargeIconGuess(std::string_view icon);

// Whether the item is a briefcase item (LoadoutItem::briefcase), from its
// pro.repo keys: the ICA Briefcase itself (trait "case"); else, when it has a
// HolsterAbility, that alone decides: one that keeps it out of the pockets
// (IsHandsOnlyHolster) whatever its ItemSize (the DAK SMG DTI Black Covert is
// not ITEMSIZE_LARGE, yet not concealable in vanilla: given loose it showed on
// 47's back beside a Sieger 300 in its briefcase), and any
// other is pocketable whatever its ItemSize (the Sawed Off Bartoli 12G is
// ITEMSIZE_LARGE and eHolsteringAllowed, and 47 holsters it). Only without a
// HolsterAbility: with useItemSize (the player's items have a string ItemSize
// key), an ItemSize containing "large" (IsLargeItemSize); without, the icon
// guess (IsLargeIconGuess).
bool IsBriefcaseItem(const LoadoutItem& item, bool useItemSize);

// The item's category for the random fill's variety: its icon, lower case
// (pistol, smg, assaultrifle, shotgun, sniperrifle ("sniper" too), melee,
// explosives, poison, distraction, tool, ...); empty for none (never a
// duplicate).
std::string LoadoutCategory(const LoadoutItem& item);

// When a need is placed (PlanLoadout): every objective's items first
// (Required), then items that help with an objective but are not needed
// (Helper: a taser device for the water canister trap), then the least
// important (Last: a distraction item for Distract Target).
enum class NeedOrder : uint8_t { Required, Helper, Last };

// What an objective needs: an item matching every non-empty part.
struct ItemNeed {
    std::string label;                 // "an epic or legendary sniper rifle"
    std::vector<std::string> kinds;    // icon, trait or perk, any of (empty: any)
    std::vector<std::string> nameAny;  // name contains any (lower case; empty: any; never ItemType)
    std::vector<std::string> nameNone; // name contains none
    int32_t minRarity = 0;
    std::optional<bool> silenced; // firearms only
    bool kill = false;            // a firearm kill: never a dart gun
    // Preferred items, best first: the first one some fitting owned item
    // matching the need also matches is drawn from; any match only when none
    // is (a lethal poison syringe before other lethal poison). Matches ignores it.
    std::vector<ItemNeed> prefer;
    NeedOrder order = NeedOrder::Required;
    // Drawn only among the fitting items (the preferred ones, when any) that
    // take the fewest slots: item sizes change, so no fixed order says which
    // is smallest (Make Target Slip: grapes, SPF 47 or a banana).
    bool cheapest = false;
    // Spawned items (LoadoutSource::Spawned) may meet it too.
    bool spawned = false;
    // Every one of these must match too (one item for two objectives: an
    // epic fiber wire for Melee Kill - Epic and Fiberwire Kill).
    std::vector<ItemNeed> also;
};

bool Matches(const ItemNeed& need, const LoadoutItem& item);

// The prestige objective's name (OptinPayoutObjective 101 -> "Silent
// Assassin"), or empty for an unknown id.
std::string_view PrestigeName(int32_t objective);

// What the prestige objective needs; empty for one that needs no item (and
// for unknown ids, see PrestigeName).
std::vector<ItemNeed> PrestigeNeeds(int32_t objective);

// What the client requirement (fr::ClientReqName ids) needs; empty for one
// that needs no item.
std::vector<ItemNeed> ClientReqNeeds(int32_t requirement);

// The CPD key prefix of a mission scene's client requirements: "MildSicbo"
// for scene_tiger_mild_sicbo.entity (keys MildSicbo_ClientReq01..03), "Hot"
// for a showdown (Hot_ClientReq01..03); empty for an unknown scene.
std::optional<std::string> ClientReqPrefixForScene(std::string_view scene);

// Why the random fill never picks an item, or empty when it may: unknown in
// pro.repo, free (cost 0: weight 0), a quest item, money, ammo, a key.
std::string FillExclusion(const LoadoutItem& item);

// Pairing table: an item only useful together with another. A detonator
// ("Emetic Gas Detonator", trait detonator_explosive_device, named
// "detonator") needs an explosive device (trait explosive_device) in the
// loadout. Returns the partner's description, or empty when the item needs
// none or the loadout has a partner.
std::string MissingPartner(const LoadoutItem& item, std::span<const LoadoutItem* const> loadout);

enum class LoadoutRule : uint8_t { Lockpick, Prestige, ClientReq, Fill };

struct LoadoutInput {
    std::vector<LoadoutItem> owned;      // gear wall and Freelancer tools (not the packed items), and the
                                         // kSpawnedItems found in pro.repo (source Spawned: only a need
                                         // with ItemNeed::spawned takes one)
    std::vector<LoadoutItem> carried;    // packed in the safehouse (CPD InventoryItems)
    int32_t totalSlots = 0;              // GearSlotsTotal
    std::optional<int32_t> reportedUsed; // GearSlotsUsed, if seen
    std::optional<int32_t> prestige;     // OptinPayoutObjective (0 or absent: none)
    std::vector<int32_t> clientReqs;     // the mission's requirement ids (0s ignored)
    // The payout the player sees for each client requirement in this mission
    // (id -> amount: the mod's random payout with payouts on, else vanilla);
    // an id missing here has an unknown payout. Rule 3 orders by it.
    std::map<int32_t, int32_t> clientReqPayouts;
    int32_t seed = 0;
    std::string scene;
    int32_t startCount = 0;     // CPD_CampaignStep at departure
    bool briefcaseItems = true; // false: no rule ever picks a briefcase item (a need only they meet is skipped)
                                // (!random_loadout_holsterable_only)
    bool lockpickFirst = true;  // false: no lockpick rule (rule 1 skipped; the random fill may still draw one)
                                // (random_loadout_lockpick_first)
    bool objectiveItems = true; // false: no objective rules (rules 2 and 3 skipped; the random fill is unchanged)
                                // (random_loadout_objective_items)
};

struct LoadoutPick {
    size_t owned = 0; // index into LoadoutInput::owned
    LoadoutRule rule = LoadoutRule::Fill;
    std::string why;
    int32_t cost = 0;
};

struct LoadoutPlan {
    std::vector<LoadoutPick> picks;
    std::vector<std::string> notes; // per rule: met by a packed item, unmet and why
    int32_t total = 0;
    int32_t usedBefore = 0;   // packed items' slots (or GearSlotsUsed when larger)
    int32_t computedUsed = 0; // packed items' slots by SlotCost
    int32_t usedAfter = 0;
    int briefcasesBefore = 0;
    int briefcasesAfter = 0;
};

// Plans the loadout (see the file comment). Owned items are taken once each
// (duplicate ids merged); owned items also packed are not picked again.
LoadoutPlan PlanLoadout(const LoadoutInput& input);

// --- What the mod keeps from the CPD (CpdSet events) ------------------------

// The CPD values the plan needs, merged from every CpdSet (later values win)
// and the gear slot events; kept in a small file in the mods folder.
struct LoadoutState {
    std::map<std::string, std::vector<Guid>> lists; // every "...Items" key: InventoryItems, PersistentItems, ...
    std::map<std::string, int32_t> ints;            // OptinPayoutObjective, CPD_CampaignStep, *_ClientReq0N
    std::optional<int32_t> slotsTotal;              // GearSlotsTotal event
    std::optional<int32_t> slotsUsed;               // GearSlotsUsed event
    int64_t updatedUnix = 0;

    bool Empty() const;
    const std::vector<Guid>* List(std::string_view key) const;
    std::optional<int32_t> Int(std::string_view key) const;
};

// What a CpdSet changed (for the log).
struct CpdMerge {
    std::vector<std::string> lists; // "PersistentItems (152)"
    std::vector<std::string> ints;  // "OptinPayoutObjective 123"
    size_t badIds = 0;              // list entries that are not GUIDs
};

// Merges a CpdSet event's JSON: its "Value" members whose key ends in
// "Items" and holds an array of GUID strings, and the integer members
// OptinPayoutObjective, CPD_CampaignStep and every *_ClientReq0N.
CpdMerge MergeCpdSet(LoadoutState& state, std::string_view eventJson);

// A GearSlotsTotal / GearSlotsUsed event's integer Value ("15"), or empty.
std::optional<int32_t> GearSlotsValue(std::string_view eventJson);

// The state file's first line.
std::string_view LoadoutStateHeader();

// The state as text (one "<section>.key=value" per line, no header) and
// back. Parse ignores
// unknown and malformed lines; empty when the text is not a state file.
std::string FormatLoadoutState(const LoadoutState& state, std::string_view section);
std::optional<LoadoutState> ParseLoadoutState(std::string_view text, std::string_view section);

// The string elements of a JSON array ("[\"a\", \"b\"]"); empty for
// anything else.
std::vector<std::string> JsonStringArray(std::string_view json);

// --- delivery ---------------------------------------------------------------------
//
// The planned items are given straight to 47 with the engine's
// ZCharacterSubcontrollerInventory::CreateItem (what the SDK's Assets mod
// does for "Add to inventory"), after the mission's "Inventory items" list
// has handed out the packed items. (Pushing ids into that list does not work:
// the list only iterates the packed items.)

// ZCharacterSubcontrollerInventory::ECreateItemType values: 0 support, 1
// debug, 2 contract, 3 carried, 4 container.
inline constexpr int32_t kCreateItemTypeContract = 2;
const char* CreateItemTypeName(int32_t type); // "support", ..., "unknown"

struct LoadoutItemTypeChoice {
    int32_t type = kCreateItemTypeContract;
    std::string why;
};

// The create type for the planned items: the type seen for a packed item
// (packedType), else contract.
LoadoutItemTypeChoice ChooseLoadoutItemType(std::optional<int32_t> packedType);

// A planned item is requested only when 47 does not already carry that id and
// it was not requested before in this scene (a restart is a new scene: the
// item is given again, once).
bool NeedsDelivery(bool heroHasIt, bool requested);

// --- briefcase delivery ---------------------------------------------------------------
//
// A large item (a briefcase item) is not created with CreateItem: that gives
// it to 47 loose (ItemPickedUp of the rifle, no briefcase). The mod uses the
// game's own path for a large item bought in a mission, in the
// evergreen_setup brick:
//   "Purchased item actions".SetItem(item) -> OnItemChanged -> PickupIntoPocket;
//   a large item cannot be pocketed -> OnFailed -> "Drop existing briefcase and
//   equip new briefcase with item" -> "Existing briefcase container actions"
//   .FindEquippedContainerItem -> NoContainerItem (or the old briefcase is
//   dropped) -> "Briefcase" (an ItemSpawner of the ICA Briefcase).SpawnItem ->
//   ItemSpawned -> "Briefcase actions".SetItem -> PickupIntoHands and "Briefcase
//   container actions".SetContainerItem -> OnContainerItemSet -> "Purchased
//   item actions".GetItem -> Item -> "Briefcase container actions"
//   .PlaceIntoContainerItemInstant.
// The item itself is spawned unowned with ZWorldInventory::RequestNewItem (as
// the SDK's Editor mod spawns weapons) and handed to "Purchased item actions"
// .SetItem. (Packed large items take another path: "EvergreenGearPersistence
// Helper" packs them at mission start, which cannot be re-run.) In vanilla only
// one briefcase is carried (the planner allows one briefcase item in the whole
// loadout, packed ones included).

// The ICA Briefcase (pro.repo, OnlineTraits "case"; the briefcase the game
// gives 47 with a packed large item at mission start, seen in ItemPickedUp).
inline constexpr std::string_view kIcaBriefcaseId = "15e6200e-2000-4966-9771-923ab13c6a1c";

// What the mod saw of one briefcase delivery.
struct BriefcaseProgress {
    bool itemSpawned = false;       // RequestNewItem's callback gave the item entity
    bool setItemSent = false;       // "Purchased item actions".SetItem signaled with it
    bool pocketFailed = false;      // "Purchased item actions".OnFailed (cannot be pocketed: large)
    bool briefcaseSpawned = false;  // "Briefcase".ItemSpawned
    bool containerSet = false;      // "Briefcase container actions".OnContainerItemSet
    bool placeSent = false;         // "Purchased item actions".Item with the item (-> PlaceIntoContainerItemInstant)
    bool briefcasePickedUp = false; // ItemPickedUp of the ICA Briefcase after the request
    bool itemPickedUp = false;      // ItemPickedUp of the item itself (loose in 47's inventory)
    bool itemPickedUpAfterPlace = false;  // that ItemPickedUp came after placeSent
    std::optional<bool> ownerIsHero;      // the item's ZHM5Item::m_pOwner is 47 (empty: not read)
    std::optional<bool> ownerIsBriefcase; // ... is the spawned briefcase (empty: not read)
    std::optional<bool> ownerIsStorage;   // ... is a ZItemStorageEntity (a container's storage; empty: not read)
    double seconds = 0;                   // game time since the request
    double secondsSincePlace = 0;         // game time since placeSent
};

enum class BriefcaseVerdict : uint8_t {
    Pending,     // still waiting
    InBriefcase, // placed into the briefcase and 47 has the briefcase: confirmed
    Loose,       // the item reached 47 without the briefcase: undo it
    Failed,      // timed out before the item was placed: undo it
};

// Seconds of game time after the place before InBriefcase (a loose
// ItemPickedUp would come within it) and the overall time limit.
inline constexpr double kBriefcaseSettleSeconds = 1.5;
inline constexpr double kBriefcaseTimeoutSeconds = 20.0;

// The verdict: Loose when the item's owner is 47 or it was picked up before
// the place (after it, a pickup without the briefcase as the owner is loose
// too); InBriefcase when its owner is the briefcase, or a container's storage
// (ZItemStorageEntity: the briefcase holds its items there) once a briefcase
// was spawned and its container set, or the place was
// sent, the briefcase was spawned and the settle time passed without a loose
// pickup; Failed after the timeout; else Pending.
BriefcaseVerdict JudgeBriefcase(const BriefcaseProgress& progress, double settleSeconds = kBriefcaseSettleSeconds,
                                double timeoutSeconds = kBriefcaseTimeoutSeconds);
const char* BriefcaseVerdictName(BriefcaseVerdict verdict);

// The steps seen and missing, in order, for the log ("item spawned yes, SetItem
// sent yes, pocket failed no, ...").
std::string BriefcaseProgressText(const BriefcaseProgress& progress);

// --- holding the briefcase while 47 is in a blend spot or an act ---------------------------
//
// The purchase path ends with "Briefcase".OnPickup: the ICA Briefcase goes
// into 47's hands. Taking something into his hands ends a blend or an act at
// once, without the animation that gets him out: in Patient Zero's morgue
// start (HeroSpawn_Morgue, 47 lying on a gurney) a briefcase arriving early
// in play makes 47 stand up inside the gurney, stuck. So the briefcase item is held while 47 is in a blend spot
// or an act, and given as soon as he is out of it; on a normal start it comes
// at once, as always.
//
// What is read: ZHitman5 keeps a 64-bit mask of EHeroGameState bits (bit n set
// = state n on; 47 states, eHGS_NUM) at offset 0xE28. In the game's exe
// (HITMAN3.exe):
//  - ZHeroStateConditionEntity (the game's own "is 47 in state X" condition)
//    evaluates exactly (hitman->[0xE28] & (1 << m_eState)) != 0 each frame
//    (its update, exe 0x140377A50), hitman being its m_rHitmanCharacter.
//  - a ZHitman5 method (exe 0x1402EB6C0, called from ZHitman5's own vtable)
//    rewrites the Mv* bits from 47's current movement (ZHitman5+0xAA0, its
//    EBaseMovementType at +0x28): eMovementDisguiseSafeZone (13) sets bit 45
//    eHGS_MvDisguiseSafeZone, eMovementNewFullBody (1) bit 44, eMovementPlace
//    (28) bit 46, eMovementLocomotion (36) bit 33, and so on, one bit per
//    movement.
//  - the Freelancer start director (herospawnsequencedirector.entitytemplate)
//    decides "IsStillInLoopAnimation" (the start pose 47 holds until the
//    player moves; its suitcase pickup then places the briefcase instead of
//    giving it) from two such conditions: eHGS_MvDisguiseSafeZone (45) and
//    eHGS_FocusedInteraction (19).
// The mod checks both code sites by their bytes before trusting the offsets
// (a game update that moves them makes the state unreadable).
//
// Held while any of kBlendHoldStates is on: the blend spot itself
// (MvDisguiseSafeZone), a full-body act (MvNewFullBody), the director's other
// loop state (FocusedInteraction), a sequence playing on 47 (InSequence) and
// hiding in a closet (InsideCloset). Once none is on for kBlendClearSeconds
// (a frame between two movements is not leaving), it is given. Unreadable
// (no 47, a fault, the code check failed, bits past eHGS_NUM): at the
// delivery it is given at once, as before; while held, after
// kBlendUnreadSeconds in a row. The pocket items are not held: they go into
// the pockets with the game's own pocket call (the path the packed items take
// at every start) or CreateItem, never into his hands.

// EHeroGameState bits (ZHMModSDK Enums.h).
inline constexpr int kHeroStateCount = 47; // eHGS_NUM
inline constexpr int kHgsInSequence = 5;
inline constexpr int kHgsInsideCloset = 17;
inline constexpr int kHgsFocusedInteraction = 19;
inline constexpr int kHgsMvNewFullBody = 44;
inline constexpr int kHgsMvDisguiseSafeZone = 45;

inline constexpr uint64_t HeroStateBit(int state) {
    return uint64_t {1} << state;
}

// The states that hold the briefcase back.
inline constexpr uint64_t kBlendHoldStates = HeroStateBit(kHgsMvDisguiseSafeZone) | HeroStateBit(kHgsMvNewFullBody) |
                                             HeroStateBit(kHgsFocusedInteraction) | HeroStateBit(kHgsInSequence) |
                                             HeroStateBit(kHgsInsideCloset);

// Game time with none of them on (read each frame) before a held briefcase is given.
inline constexpr double kBlendClearSeconds = 0.5;

// Game time in a row without a reading after which a held briefcase is given anyway.
inline constexpr double kBlendUnreadSeconds = 2.0;

// A mask with a bit at or past eHGS_NUM is not this field.
bool IsHeroStateMaskSane(uint64_t mask);

// "eHGS_MvLocomotion, eHGS_Sneaking" (lowest bit first), "none", or with an
// unknown bit "bit 52".
std::string HeroStateText(uint64_t mask);

// EBaseMovementType's name ("eMovementDisguiseSafeZone"), or "type n".
std::string MovementTypeName(int32_t type);

enum class BlendHoldVerdict : uint8_t {
    InBlend,  // a hold state is on: keep the briefcase back
    Settling, // none on, but not for clearNeeded yet: keep it back
    Clear,    // none on for clearNeeded: give it now
    Unread,   // no reading, not for unreadLimit yet: keep it back
    Unknown,  // no reading for unreadLimit: give it as before
};

// mask: 47's state bits this frame (empty: not readable; a mask that fails
// IsHeroStateMaskSane counts as not readable); clearSeconds: game time in a
// row, this frame included, read with no hold state on; unreadSeconds: game
// time in a row, this frame included, without a reading. At the delivery the
// limits are 0: clear or unreadable gives at once.
BlendHoldVerdict JudgeBlendHold(const std::optional<uint64_t>& mask, double clearSeconds, double unreadSeconds,
                                double clearNeeded = kBlendClearSeconds, double unreadLimit = kBlendUnreadSeconds);

// Clear or Unknown: give the briefcase now.
bool BlendHoldGives(BlendHoldVerdict verdict);
const char* BlendHoldVerdictName(BlendHoldVerdict verdict);

// --- pocket delivery ------------------------------------------------------------------
//
// A large holsterable item is not created with CreateItem either: that gives
// it to 47 in his hands. Every delivery of a Sawed Off Bartoli 12G (ItemSize
// ITEMSIZE_LARGE, HolsterAbility eHolsteringAllowed) is followed within 0.1 s
// by a HoldingIllegalWeapon event with IsHoldingIllegalWeapon true and the
// shotgun as WeaponEquipped; the pistols and SMGs CreateItem gives never are.
// (It goes into the pockets only when a briefcase item's ICA Briefcase takes
// the hands a moment later.) The mod uses the
// game's own pocket call instead, as for a packed item: the item is spawned
// unowned with ZWorldInventory::RequestNewItem and handed to evergreen_setup's
// "HeroItemAction" (e15f202bdc5860fd, the same entity class as "Loaded item
// actions"): SetItem(item) -> OnItemChanged -> PickupIntoPocket (its OnFailed
// goes nowhere, so a refusal is the mod's to handle).

// An item delivered by the pocket path: not a briefcase item, HolsterAbility
// eHolsteringAllowed and an ItemSize containing "large" (IsLargeItemSize).
bool IsPocketDeliveryItem(const LoadoutItem& item);

// A HoldingIllegalWeapon event: does 47 hold an illegal weapon, and which
// (WeaponEquipped's RepositoryId; empty when not holding or not readable).
struct HeldWeapon {
    bool holding = false;
    std::optional<Guid> id;
};

// Empty when the JSON has no IsHoldingIllegalWeapon boolean.
std::optional<HeldWeapon> ParseHoldingIllegalWeapon(std::string_view eventJson);

// What the mod saw of one pocket delivery, from its SetItem on.
struct PocketProgress {
    bool setItemSent = false;        // "HeroItemAction".SetItem signaled with the item
    bool pocketFailed = false;       // "HeroItemAction".OnFailed after the SetItem (the game would not pocket it)
    bool itemPickedUp = false;       // ItemPickedUp of the item
    std::optional<bool> ownerIsHero; // the item's ZHM5Item::m_pOwner is 47 (empty: not read)
    bool inHands = false;            // the last HoldingIllegalWeapon named it as held
    bool everInHands = false;        // one did, at any time
    bool retrySent = false;          // "HeroItemAction".PickupIntoPocket sent once more after it was in the hands
    double seconds = 0;              // game time since the SetItem
    double secondsSinceHeld = 0;     // game time since 47 got it (owner or pickup) or its hands were last cleared
    double secondsSinceRetry = 0;    // game time since the retry
};

enum class PocketVerdict : uint8_t {
    Pending,  // still waiting
    Pocketed, // 47 has it and holds no such weapon in his hands: confirmed
    Retry,    // in his hands: send PickupIntoPocket once more
    InHands,  // still in his hands after the retry
    Refused,  // the game would not pocket it (OnFailed) and 47 does not have it
    Failed,   // nothing within the timeout
};

// Seconds of game time with the item held and no HoldingIllegalWeapon naming
// it before Pocketed (the event came within 0.1 s of the pickup in every log),
// and the overall limit from the SetItem.
inline constexpr double kPocketSettleSeconds = 1.5;
inline constexpr double kPocketTimeoutSeconds = 10.0;

// The verdict: Retry when it is in the hands and no retry was sent, InHands
// when it still is kPocketSettleSeconds after the retry (Pending before);
// Refused on OnFailed while 47 does not have it; Pocketed once 47 has it
// (owner or ItemPickedUp) for settleSeconds out of his hands; Failed at the
// timeout without 47 having it; else Pending.
PocketVerdict JudgePocket(const PocketProgress& progress, double settleSeconds = kPocketSettleSeconds,
                          double timeoutSeconds = kPocketTimeoutSeconds);
const char* PocketVerdictName(PocketVerdict verdict);

// The steps seen, for the log ("SetItem sent yes, pocket failed no, ...; 1.2 s").
std::string PocketProgressText(const PocketProgress& progress);

// One planned item in a mission CpdSet (the end-of-mission save): is it in
// InventoryItems / in its source list (empty: the list is not in this
// CpdSet), was it removed from its source by the mod, does 47 hold it now
// (empty: unknown).
enum class SaveVerdict : uint8_t {
    Carried,         // in InventoryItems, not in its list
    Lost,            // removed, in neither (right for a failure)
    StaysOwned,      // never removed, still in its list
    NotDelivered,    // never removed, not carried, its list not in the CpdSet
    Duplicated,      // in InventoryItems and its list
    MissingFromSave, // 47 holds it, InventoryItems lacks it
    RemovalLost,     // removed, back in its list, not carried
    Vanished,        // never removed, not in its list, not carried
    Partial,         // no InventoryItems in this CpdSet
};

SaveVerdict JudgeSave(std::optional<bool> inInventory, std::optional<bool> inSource, bool removedFromSource,
                      std::optional<bool> heroHolds);
const char* SaveVerdictText(SaveVerdict verdict);

} // namespace fr
