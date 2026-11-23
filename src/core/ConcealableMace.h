#pragma once

// Pure logic of the concealable mace (concealable_mace, with or without weapon_rarity).
// Freelancer packs an item in an ICA Briefcase when 47 cannot pocket it; an
// item's pro.repo entry says whether he can: "HolsterAbility"
// (eHolsteringAllowed: into the pockets; eHolsterTemporarilyOnly /
// eCanNotBeHolstered: carried in the hands, a briefcase when packed) and
// "ItemSize" (ITEMSIZE_LARGE items are the briefcase items; this mod's
// loadout planner reads it too). Both feed the item's config
// (SItemConfig::m_ItemSize, ZHM5Item's holster ability) when it is built from
// the repository. The mace (CommonName "Melee_Mace") becomes concealable by
// taking the HolsterAbility and ItemSize a pocketable melee weapon has: the
// ItemSize most common among the melee items that allow holstering (never a
// large one).

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace fr {

inline constexpr std::string_view kHolsteringAllowed = "eHolsteringAllowed";
inline constexpr std::string_view kItemSizeSmall = "ITEMSIZE_SMALL";
inline constexpr std::string_view kItemSizeMedium = "ITEMSIZE_MEDIUM";

// A mace by its Title or CommonName: the word "mace" (any case) with no
// letter right before or after it ("Mace", "Melee_Mace", "Mace Gold"; not
// "Grimace", "Macedonian").
bool IsMaceName(std::string_view title, std::string_view commonName);

// A melee item by its InventoryCategoryIcon (any case).
bool IsMeleeIcon(std::string_view inventoryCategoryIcon);

// An ItemSize that makes a briefcase item: it contains "large" (any case),
// the rule of the loadout planner.
bool IsLargeItemSize(std::string_view itemSize);

// The ItemSize a concealable mace gets: the most common one in p_Counts (the
// ItemSize of each melee item that allows holstering; large ones are ignored),
// ties broken toward the smaller size (SMALL, then MEDIUM, then by name);
// ITEMSIZE_SMALL when there is none.
std::string PickConcealedItemSize(const std::map<std::string, size_t>& counts);

// What to write into one mace entry: each key whose current text differs from
// its target. A key the entry lacks (or that is not a string) is never written.
struct ConcealChange {
    bool itemSize = false;
    bool holster = false;
};

struct ConcealInput {
    bool hasItemSize = false; // a ZString "ItemSize"
    std::string itemSize;
    bool hasHolster = false; // a ZString "HolsterAbility"
    std::string holster;
};

ConcealChange PlanConceal(const ConcealInput& entry, std::string_view targetItemSize);

// One key this wrote, at a later scan: put back only while it still holds
// what this wrote (else someone else changed it, which wins); a key whose
// vanilla text is what this would write was never changed.
bool ShouldRestoreText(std::string_view current, std::string_view written, std::string_view vanilla);

} // namespace fr
