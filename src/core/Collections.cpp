#include "Collections.h"

namespace fr {

std::optional<size_t> EntityRefSize(std::string_view typeName) {
    if (typeName == "ZEntityRef")
        return 8;

    constexpr std::string_view prefix = "TEntityRef<";

    if (typeName.size() > prefix.size() && typeName.starts_with(prefix) && typeName.ends_with('>'))
        return 16;

    return std::nullopt;
}

std::optional<size_t> EntityRefArrayElementSize(std::string_view typeName) {
    constexpr std::string_view prefix = "TArray<";

    if (!typeName.starts_with(prefix) || !typeName.ends_with('>'))
        return std::nullopt;

    std::string_view element = typeName.substr(prefix.size(), typeName.size() - prefix.size() - 1);

    while (!element.empty() && element.back() == ' ')
        element.remove_suffix(1);

    return EntityRefSize(element);
}

std::optional<size_t> ArrayCount(uintptr_t begin, uintptr_t end, size_t elementSize, size_t maxCount) {
    if (elementSize == 0 || end < begin)
        return std::nullopt;

    const uintptr_t bytes = end - begin;

    if (bytes == 0)
        return 0;

    if (begin == 0 || bytes % elementSize != 0 || bytes / elementSize > maxCount)
        return std::nullopt;

    return static_cast<size_t>(bytes / elementSize);
}

std::optional<ArraySpan> ArrayElements(uintptr_t arrayAddress, uintptr_t begin, uintptr_t end,
                                       uint64_t allocationEndOrFlags, size_t elementSize, size_t maxCount) {
    constexpr size_t kInlineBytes = 2 * sizeof(uint64_t); // begin and end hold the elements

    if (elementSize == 0)
        return std::nullopt;

    if (elementSize <= kInlineBytes && ((allocationEndOrFlags >> 62) & 1) != 0) {
        const size_t count = static_cast<size_t>(allocationEndOrFlags & 0xFF);

        if (arrayAddress == 0 || count * elementSize > kInlineBytes || count > maxCount)
            return std::nullopt;

        return ArraySpan {arrayAddress, count, true};
    }

    const std::optional<size_t> count = ArrayCount(begin, end, elementSize, maxCount);

    if (!count)
        return std::nullopt;

    return ArraySpan {begin, *count, false};
}

CollectionRole CollectionRoleOf(std::string_view conditionClass, std::optional<int32_t> level) {
    if (conditionClass == "ZValueBoolBasic")
        return CollectionRole::Bool;

    if (conditionClass == "ZDifficultyCondition" && level == 2)
        return CollectionRole::Professional;

    if (conditionClass == "ZDifficultyCondition" && level == 4)
        return CollectionRole::Hard;

    return CollectionRole::Other;
}

} // namespace fr
