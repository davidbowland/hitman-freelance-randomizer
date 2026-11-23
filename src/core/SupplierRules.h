#pragma once

// Pure parts of the supplier walk (src/mod/SupplierWalk.h) and the steering
// it feeds: which entities the walk keeps, the classification of offered
// items, the supplier visit event and the blocks of the options in no
// selector.

#include "GameIds.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace fr {

// A class an option member is kept for (its values are read): the option's
// evergreen, mastery, unlock, repository, comparator, condition, poll,
// selector-choice, math and basic int / bool / float value entities.
bool IsStockFlagClass(std::string_view className);

// A random selector that may roll an offer (class has "Selector", not "Choice").
bool IsStockSelectorClass(std::string_view className);

// --- Items ---

enum class StockItemKind {
    Merces, // CommonName or title Mercer_* / *Merces*
    Weapon, // a firearm by its InventoryCategoryIcon
    Other,
};

StockItemKind ClassifyStockItem(std::string_view commonName, std::string_view title, std::string_view icon);

// The supplier's stock is rolled when 47 first reaches the supplier: the game
// sends SupplierVisited then.
bool IsSupplierVisitEvent(std::string_view name);

// --- The supplier's non-gun slots ---

// The options the walk maps to no selector come in six blocks by their
// position among them, 279 options: A 0-64 gear (explosives, poisons, tools,
// distractions), B 65-88 tools / pistols, C 89-146 melee, D 147-158 tools /
// pistols, E 159-216 melee, F 217-278 gear (a copy of A). The block letter of
// a position, or '?' when there is another number of them (another layout) or
// the position is past it.
inline constexpr size_t kLooseOptionCount = 279;
char LooseBlockOf(size_t position, size_t count);

// "melee 57, tool 3, ? 1": each distinct key with its count, most first, then
// by key; an empty key counts as "?"; "none" when there are no keys.
std::string TallyText(std::span<const std::string> keys);

} // namespace fr
