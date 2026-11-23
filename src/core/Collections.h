#pragma once

// Pure helpers for the difficulty collections: reading entity-reference
// properties raw, and a collection's role (the entity dials and guard senses
// look for the professional one).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fr {

// Bytes of an entity-reference property by its engine type name: 8 for
// ZEntityRef, 16 for TEntityRef<...> (ZEntityRef plus the interface pointer);
// empty for any other type.
std::optional<size_t> EntityRefSize(std::string_view typeName);

// Element size of a TArray of entity references (TArray<ZEntityRef> or
// TArray<TEntityRef<...>>), or empty for any other type.
std::optional<size_t> EntityRefArrayElementSize(std::string_view typeName);

// Elements in a TArray span [begin, end) of elementSize-byte elements: empty
// unless end >= begin, the span is whole elements, a null begin has no
// elements, and the count is at most maxCount.
std::optional<size_t> ArrayCount(uintptr_t begin, uintptr_t end, size_t elementSize, size_t maxCount);

// Where a raw TArray's elements are and how many there are.
struct ArraySpan {
    uintptr_t begin = 0;
    size_t count = 0;
    bool isInline = false;
};

// The elements of a raw TArray at arrayAddress whose three qwords are begin,
// end and allocationEndOrFlags. As in the SDK's TArray, elements of at most
// 16 bytes may be stored inline: when bit 62 of the third qword is set, the
// count is that qword's low byte and the elements are at arrayAddress itself
// (at most 16 bytes of them). Otherwise as ArrayCount. Empty when implausible.
std::optional<ArraySpan> ArrayElements(uintptr_t arrayAddress, uintptr_t begin, uintptr_t end,
                                       uint64_t allocationEndOrFlags, size_t elementSize, size_t maxCount);

// A difficulty collection's role, from its enable condition's class and, for a
// ZDifficultyCondition, its difficulty level.
enum class CollectionRole {
    Hard,         // ZDifficultyCondition at eDL_HARD (4)
    Bool,         // ZValueBoolBasic
    Professional, // ZDifficultyCondition at eDL_NORMAL (2): the one Freelancer plays on
    Other,
};

CollectionRole CollectionRoleOf(std::string_view conditionClass, std::optional<int32_t> level);

} // namespace fr
