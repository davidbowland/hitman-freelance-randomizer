#include "ConcealableMace.h"

#include "Scene.h"

#include <cctype>

namespace fr {

namespace {

bool IsLetter(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool HasWordMace(std::string_view text) {
    const std::string s_Lower = ToLower(text);
    constexpr std::string_view kWord = "mace";

    for (size_t s_Pos = s_Lower.find(kWord); s_Pos != std::string::npos; s_Pos = s_Lower.find(kWord, s_Pos + 1)) {
        const bool s_Before = s_Pos > 0 && IsLetter(s_Lower[s_Pos - 1]);
        const size_t s_End = s_Pos + kWord.size();
        const bool s_After = s_End < s_Lower.size() && IsLetter(s_Lower[s_End]);

        if (!s_Before && !s_After)
            return true;
    }

    return false;
}

int SizeRank(std::string_view itemSize) {
    if (itemSize == kItemSizeSmall)
        return 0;
    if (itemSize == kItemSizeMedium)
        return 1;
    return 2;
}

} // namespace

bool IsMaceName(std::string_view title, std::string_view commonName) {
    return HasWordMace(title) || HasWordMace(commonName);
}

bool IsMeleeIcon(std::string_view inventoryCategoryIcon) {
    return ToLower(inventoryCategoryIcon) == "melee";
}

bool IsLargeItemSize(std::string_view itemSize) {
    return ToLower(itemSize).find("large") != std::string::npos;
}

std::string PickConcealedItemSize(const std::map<std::string, size_t>& counts) {
    const std::string* s_Best = nullptr;
    size_t s_BestCount = 0;

    for (const auto& [s_Size, s_Count] : counts) {
        if (s_Count == 0 || s_Size.empty() || IsLargeItemSize(s_Size))
            continue;

        const bool s_Better =
            !s_Best || s_Count > s_BestCount || (s_Count == s_BestCount && SizeRank(s_Size) < SizeRank(*s_Best));

        if (s_Better) {
            s_Best = &s_Size;
            s_BestCount = s_Count;
        }
    }

    return s_Best ? *s_Best : std::string(kItemSizeSmall);
}

ConcealChange PlanConceal(const ConcealInput& entry, std::string_view targetItemSize) {
    ConcealChange s_Change;
    s_Change.itemSize = entry.hasItemSize && !targetItemSize.empty() && entry.itemSize != targetItemSize;
    s_Change.holster = entry.hasHolster && entry.holster != kHolsteringAllowed;
    return s_Change;
}

bool ShouldRestoreText(std::string_view current, std::string_view written, std::string_view vanilla) {
    return current == written && current != vanilla;
}

} // namespace fr
