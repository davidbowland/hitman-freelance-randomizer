#pragma once

// The evergreen setpiece templates that hold safe, money-mule and
// supply-crate merces, the crate option members the supply crate features
// read, and small JSON field readers for event JSON.

#include "GameIds.h"
#include "Merces.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fr {

// --- Templates ---

enum class LootKind {
    Safe,             // setpiece_evergreen_container_items_safe_unique
    MoneyMule,        // setpiece_evergreen_moneymulesetup_unique
    Stash,            // setpiece_evergreen_stash_unique
    StashItems,       // setpiece_evergreen_stash_items
    SupplyCrate,      // setpiece_evergreen_supplycrate
    SelectableMerces, // helper_evergreen_selectableitem_merces_*
    MoneyItem,        // prop_questitem_evergreen_moneyitem_*_amount_runtime
    MoneyStack,       // setpiece_evergreen_moneystack_*_unique
    GlobalMoney,      // helper_evergreen_global_money
    MoneyEarnToken,   // eventtoken_evergreen_moneyearn
    SelectableItem,   // the per-option helper the crate draws from (TBLU only)
};

const char* LootKindName(LootKind kind);

// One template, by its factory (TEMP) and blueprint (TBLU) runtime resource ids
// (temp 0 when only the blueprint is known).
struct LootTemplate {
    uint64_t temp;
    uint64_t tblu;
    const char* name;
    LootKind kind;
};

// The template whose TEMP or TBLU runtime resource id is rid, or nullptr.
const LootTemplate* FindLootTemplate(uint64_t rid);

// A value that could be a user-mode heap pointer to an entity (8-aligned,
// above the first 64 KiB, below the 47-bit user-space limit).
bool IsPlausibleEntityPointer(uint64_t value);

// --- Crate options ---

// The crate option (selectable item helper, TBLU 00BCA591C00A579D) members.
inline constexpr uint64_t kOptionChoiceId = 0x7201b6977aaa1657ull;     // ZRandomSelectorChoice, m_Weight
inline constexpr uint64_t kOptionWeightMathId = 0xfc12a8df429f30c0ull; // ZMathMultiplyDivide, m_fA / m_fB
inline constexpr uint64_t kOptionCostId = 0x0720fcf1a0a8ff4aull;       // ZValueFloat_Basic, m_fValue (feeds m_fB)
inline constexpr uint64_t kOptionStateId = 0x5953441ba0a933acull;  // ZValueInt_Basic (0 -> 3 when its pool activates)
inline constexpr uint64_t kOptionState2Id = 0xd6a16992fb94b46bull; // ZValueInt_Basic
inline constexpr uint64_t kOptionKeyId = 0x3303c63a6583a672ull;    // ZItemRepositoryKeyEntity
inline constexpr uint64_t kOptionKey2Id = 0xacce7665084a7786ull;   // ZItemRepositoryKeyEntity

// How a property's engine type is read for the crate and item dumps.
enum class PropShape {
    Other,
    Numeric,        // int32, int64, float32
    Bool,           // bool (1 byte)
    Guid,           // ZRepositoryID, ZGuid (16 bytes)
    EntityRef,      // ZEntityRef (8 bytes: the entity pointer)
    TypedEntityRef, // TEntityRef<T> (16 bytes: ZEntityRef, then the interface pointer)
    Array,          // TArray<T> (begin, end, capacity end pointers)
};

struct PropKind {
    PropShape shape = PropShape::Other;
    NumericKind numeric = NumericKind::Other; // Numeric
    PropShape element = PropShape::Other;     // Array: the element's shape (never Array)
    NumericKind elementNumeric = NumericKind::Other;
    size_t elementSize = 0; // Array: bytes per element, 0 when unknown
};

PropKind PropKindOf(std::string_view typeName);

// --- JSON fields (single-line event JSON as ZDynamicObject_ToString writes it) ---

// The first string value of key (exact key match) without its quotes, or empty.
std::optional<std::string_view> JsonStringField(std::string_view json, std::string_view key);

// The first number value of key (exact key match), or empty when missing or
// not a number.
std::optional<double> JsonNumberField(std::string_view json, std::string_view key);

} // namespace fr
