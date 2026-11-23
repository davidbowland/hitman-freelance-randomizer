#include "SupplierRules.h"

#include "TextNoCase.h"
#include "WeaponRarity.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <utility>

namespace fr {

namespace {

bool Contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

} // namespace

bool IsStockFlagClass(std::string_view c) {
    if (c.starts_with("ZEvergreen"))
        return true;

    for (const std::string_view part :
         {"Mastery", "Unlock", "_Repository", "Entitlement", "DLC", "Owned", "Inventory", "Available", "Comparator",
          "Condition", "Poll", "SelectorChoice", "MathMultiplyDivide", "ItemRepositoryKey"}) {
        if (Contains(c, part))
            return true;
    }

    return c == "ZValueInt_Basic" || c == "ZValueBool_Basic" || c == "ZValueFloat_Basic";
}

bool IsStockSelectorClass(std::string_view c) {
    return Contains(c, "Selector") && !Contains(c, "Choice");
}

StockItemKind ClassifyStockItem(std::string_view commonName, std::string_view title, std::string_view icon) {
    for (const std::string_view name : {commonName, title}) {
        if (ContainsNoCase(name, "mercer_") || ContainsNoCase(name, "merces"))
            return StockItemKind::Merces;
    }

    return IsFirearmIcon(icon) ? StockItemKind::Weapon : StockItemKind::Other;
}

bool IsSupplierVisitEvent(std::string_view name) {
    return name == "SupplierVisited";
}

char LooseBlockOf(size_t position, size_t count) {
    if (count != kLooseOptionCount || position >= count)
        return '?';

    // First position of blocks B to F.
    constexpr std::array<size_t, 5> kStarts {65, 89, 147, 159, 217};
    size_t s_Block = 0;

    while (s_Block < kStarts.size() && position >= kStarts[s_Block])
        ++s_Block;

    return static_cast<char>('A' + s_Block);
}

std::string TallyText(std::span<const std::string> keys) {
    std::map<std::string, size_t> s_Counts;

    for (const std::string& s_Key : keys)
        ++s_Counts[s_Key.empty() ? std::string("?") : s_Key];

    if (s_Counts.empty())
        return "none";

    std::vector<std::pair<std::string, size_t>> s_Sorted(s_Counts.begin(), s_Counts.end());
    std::stable_sort(s_Sorted.begin(), s_Sorted.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });

    std::string s_Text;

    for (const auto& [s_Key, s_Count] : s_Sorted)
        s_Text += (s_Text.empty() ? "" : ", ") + s_Key + " " + std::to_string(s_Count);

    return s_Text;
}

} // namespace fr
