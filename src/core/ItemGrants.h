#pragma once

// Pure part of the item grants (grant_makeshift_explosive, grant_lethal_poison,
// grant_coin; each on by default; glue in mod/RandomLoadout.cpp, beside the
// lockpick grant, whose delivery and save guard they share).
//
// The three items free for the taking in the safehouse (a Makeshift
// Explosive, a Lethal Poison Vial, the bird's nest Coin): picked up in
// snug/scene_vanilla, they come again at the next mission's start as packed
// items. With a grant on, 47 gets the item at the start of
// each Freelancer mission when he carries none of its kind, created in his
// inventory with the random loadout's CreateItem (no gear slot is used). None
// of the three has a rarity, and the game drops every item or weapon without
// one before the safehouse, so the player never keeps a given item; the slow
// lockpick's guard (kept out of the mission's saved InventoryItems) runs for
// them too, as a second check.
//
// The given items (ids from their ItemPickedUp events; pro.repo names):
//   Makeshift Explosive 8f1bae41 ("Makeshift Remote Explosive" in pro.repo),
//     the safehouse's own;
//   Lethal Poison Vial 7d668011 ("Lethal Poison"), the safehouse's own (the
//     gear wall's "Lethal Poison FataLIDOMIDE" 67637973 shows as the same
//     name in game);
//   Classic Coin 98bf7fc1 ("(Tool) Coin Classic", from Freelancer's stash
//     crates), not the safehouse's plain Coin dda002e9.
//
// What counts as carrying the kind: his own (not given) items, by the item
// ledger (LockpickFree.h), whose id is a known one of the kind
// (IsGrantKindId) or a pro.repo item whose names and traits say so
// (IsGrantKindItem): any coin; any lethal consumable poison (vials, pills,
// the lethal frogs and flowers; never a syringe); any makeshift explosive
// (never its remote).

#include "Guid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fr {

enum class GrantItem : uint8_t { MakeshiftExplosive, LethalPoison, Coin };

inline constexpr size_t kGrantItemCount = 3;
inline constexpr std::array<GrantItem, kGrantItemCount> kGrantItems = {GrantItem::MakeshiftExplosive,
                                                                       GrantItem::LethalPoison, GrantItem::Coin};

inline constexpr std::string_view kMakeshiftExplosiveId = "8f1bae41-3570-40cc-be87-77cb6a4af86c";
inline constexpr std::string_view kLethalPoisonVialId = "7d668011-77f9-4cae-97f1-e3eda5e0c8b2";
inline constexpr std::string_view kClassicCoinId = "98bf7fc1-7857-4999-bc99-586c49f24017";

// "grant_makeshift_explosive", "grant_lethal_poison", "grant_coin".
const char* GrantItemSetting(GrantItem item);
// The given item, as the game names it: "Makeshift Explosive", "Lethal Poison Vial", "Classic Coin".
const char* GrantItemName(GrantItem item);
// The kind, for the log: "makeshift explosive", "lethal poison", "coin".
const char* GrantKindName(GrantItem item);
// The given item's pro.repo id.
Guid GrantItemId(GrantItem item);
size_t GrantItemIndex(GrantItem item);

// Ids of the kind that count without pro.repo: the given one and the others
// the logs show (coins: the safehouse Coin, the Classic Coin, the Outstanding
// Service Coin; lethal poison: both Lethal Poison Vials, Lethal Pills, the
// Lethal Poison Pill Jar).
bool IsGrantKindId(GrantItem item, const Guid& id);

// A pro.repo item of the kind by its Title / CommonName (any case) and
// OnlineTraits (comma-separated): coin: "coin" in a name; lethal poison:
// "lethal" in a name and the consumable_poison trait (with no traits: "poison"
// or "pill" and not "syringe"); makeshift explosive: "makeshift" and
// "explosive" in a name, not the detonator_explosive_device trait.
bool IsGrantKindItem(GrantItem item, std::string_view title, std::string_view commonName, std::string_view traits);

// Whether a comma-separated trait list holds the trait (trimmed, any case).
bool HasTrait(std::string_view traits, std::string_view trait);

enum class GrantVerdict : uint8_t { Off, Carried, Give };

// Give when the grant is on and 47 carries none of the kind of his own.
GrantVerdict DecideGrant(bool on, size_t ownOfKind);

// When a waiting grant (lockpick_grant and the item grants) gives, counted in
// game time from eLoading_ScenePlaying: never before kGrantDelaySeconds; then
// once the packed items are in (listDone: "Inventory items" OnDone, or the
// caller's list wait ran out) and the random loadout is no longer before its
// CreateItems (loadoutPending: its WaitList, Delay or Deliver phase). Its
// CreateItems count in the item ledger at once, so a lockpick or grant kind
// it brings is seen without waiting out its 15 s confirm window (gating on
// that window could keep the grant from coming at all: a short mission ends,
// or the game sits paused, before it runs out). At
// kGrantLoadoutWaitSeconds it gives anyway (GoTimedOut, logged).
inline constexpr double kGrantDelaySeconds = 3.0;
inline constexpr double kGrantLoadoutWaitSeconds = 20.0;

enum class GrantStartVerdict : uint8_t { Wait, Go, GoTimedOut };

struct GrantStartInput {
    double timer = 0;            // game seconds since eLoading_ScenePlaying
    bool listDone = false;       // the packed items are in
    bool loadoutPending = false; // the random loadout has not created its items yet
};

GrantStartVerdict DecideGrantStart(const GrantStartInput& input);
const char* GrantStartVerdictName(GrantStartVerdict verdict);

} // namespace fr
