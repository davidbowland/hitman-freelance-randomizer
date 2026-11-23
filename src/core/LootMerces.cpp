#include "LootMerces.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace fr {

namespace {

constexpr std::array<LootTemplate, 19> kTemplates {{
    {0x004F5ED36051B2C6ull, 0x00F1DC5FBEE15B8Eull, "setpiece_evergreen_container_items_safe_unique", LootKind::Safe},
    {0x007568D09482220Eull, 0x0025CD93E3A002EFull, "setpiece_evergreen_moneymulesetup_unique", LootKind::MoneyMule},
    {0x00D1399ACD41FD9Full, 0x00430B7BE6F18F94ull, "setpiece_evergreen_stash_unique", LootKind::Stash},
    {kStashItemsTemp, kStashItemsTblu, "setpiece_evergreen_stash_items", LootKind::StashItems},
    {0x00B917F348CF904Dull, 0x00B08A36F5AF5C5Bull, "setpiece_evergreen_supplycrate", LootKind::SupplyCrate},
    {0x005283F93D3D2A9Aull, 0x00810700ED31C44Dull, "helper_evergreen_selectableitem_merces_verysmall",
     LootKind::SelectableMerces},
    {0x006DE4F7491BF42Bull, 0x0078E9DA5EA9861Bull, "helper_evergreen_selectableitem_merces_small",
     LootKind::SelectableMerces},
    {0x001BB3FBE100081Full, 0x00D2F167B51EE1B0ull, "helper_evergreen_selectableitem_merces_medium",
     LootKind::SelectableMerces},
    {0x00666A01677EEDD5ull, 0x008CD34443C48F3Cull, "helper_evergreen_selectableitem_merces_large",
     LootKind::SelectableMerces},
    {0x000EAD45E3BD9C58ull, 0x0056DE5314F1A0C3ull, "helper_evergreen_selectableitem_merces_max",
     LootKind::SelectableMerces},
    {0x0050079A9D9F31D4ull, 0x003647AE3DABA492ull, "helper_evergreen_selectableitem_merces_randomlarge",
     LootKind::SelectableMerces},
    {kMoneyItemSmallTemp, kMoneyItemSmallTblu, "prop_questitem_evergreen_moneyitem_small_amount_runtime",
     LootKind::MoneyItem},
    {kMoneyItemMediumTemp, kMoneyItemMediumTblu, "prop_questitem_evergreen_moneyitem_medium_amount_runtime",
     LootKind::MoneyItem},
    {kMoneyItemLargeTemp, kMoneyItemLargeTblu, "prop_questitem_evergreen_moneyitem_large_amount_runtime",
     LootKind::MoneyItem},
    {0x00AB82DDA53C7581ull, 0x007F98BF08B2FF1Bull, "setpiece_evergreen_moneystack_small_unique", LootKind::MoneyStack},
    {0x006609234B433E41ull, 0x006068CC65859C1Bull, "setpiece_evergreen_moneystack_large_unique", LootKind::MoneyStack},
    {0x006A485B2607DD73ull, 0x007DD9F93EF90D37ull, "helper_evergreen_global_money", LootKind::GlobalMoney},
    {0x002B99D548381C1Full, 0x005B94514F7D184Dull, "eventtoken_evergreen_moneyearn", LootKind::MoneyEarnToken},
    // Owner of one ZRandomSelectorChoice (m_Weight 1) per crate option; its
    // name and TEMP id are unknown.
    {0, kCrateOptionTblu, "selectable item helper (TBLU 00BCA591C00A579D)", LootKind::SelectableItem},
}};

// The position just after `"key":` (whitespace after the colon skipped) for
// the first occurrence of key as a key, or npos.
size_t ValueStart(std::string_view json, std::string_view key) {
    size_t from = 0;

    while (from < json.size()) {
        const size_t quote = json.find('"', from);

        if (quote == std::string_view::npos)
            return std::string_view::npos;

        const size_t end = quote + 1 + key.size();

        if (json.substr(quote + 1, key.size()) == key && end < json.size() && json[end] == '"') {
            size_t at = end + 1;

            while (at < json.size() && (json[at] == ' ' || json[at] == '\t'))
                ++at;

            if (at < json.size() && json[at] == ':') {
                ++at;

                while (at < json.size() && (json[at] == ' ' || json[at] == '\t'))
                    ++at;

                return at;
            }
        }

        // Skip this whole string so a value equal to the key is never taken as one.
        size_t close = quote + 1;

        while (close < json.size() && json[close] != '"')
            close += json[close] == '\\' ? 2 : 1;

        from = close + 1;
    }

    return std::string_view::npos;
}

} // namespace

const char* LootKindName(LootKind kind) {
    switch (kind) {
        case LootKind::Safe: return "safe";
        case LootKind::MoneyMule: return "money mule";
        case LootKind::Stash: return "stash";
        case LootKind::StashItems: return "stash items";
        case LootKind::SupplyCrate: return "supply crate";
        case LootKind::SelectableMerces: return "selectable merces";
        case LootKind::MoneyItem: return "money item";
        case LootKind::MoneyStack: return "money stack";
        case LootKind::GlobalMoney: return "global money";
        case LootKind::MoneyEarnToken: return "money-earn token";
        case LootKind::SelectableItem: return "selectable item";
    }

    return "?";
}

const LootTemplate* FindLootTemplate(uint64_t rid) {
    if (rid == 0)
        return nullptr;

    for (const LootTemplate& t : kTemplates) {
        if (t.temp == rid || t.tblu == rid)
            return &t;
    }

    return nullptr;
}

bool IsPlausibleEntityPointer(uint64_t value) {
    return value >= 0x10000 && value < 0x0000800000000000ull && value % 8 == 0;
}

PropKind PropKindOf(std::string_view typeName) {
    PropKind kind;
    const NumericKind numeric = NumericKindOf(typeName);

    if (numeric != NumericKind::Other) {
        kind.shape = PropShape::Numeric;
        kind.numeric = numeric;
        return kind;
    }

    if (typeName == "bool") {
        kind.shape = PropShape::Bool;
        return kind;
    }

    if (typeName == "ZRepositoryID" || typeName == "ZGuid") {
        kind.shape = PropShape::Guid;
        return kind;
    }

    if (typeName == "ZEntityRef") {
        kind.shape = PropShape::EntityRef;
        return kind;
    }

    if (typeName.starts_with("TEntityRef<") && typeName.ends_with(">")) {
        kind.shape = PropShape::TypedEntityRef;
        return kind;
    }

    if (typeName.starts_with("TArray<") && typeName.ends_with(">")) {
        const PropKind element = PropKindOf(typeName.substr(7, typeName.size() - 8));
        kind.shape = PropShape::Array;
        kind.element = element.shape == PropShape::Array ? PropShape::Other : element.shape;
        kind.elementNumeric = element.numeric;

        switch (kind.element) {
            case PropShape::Numeric: kind.elementSize = NumericKindSize(element.numeric); break;
            case PropShape::Bool: kind.elementSize = 1; break;
            case PropShape::Guid: kind.elementSize = 16; break;
            case PropShape::EntityRef: kind.elementSize = 8; break;
            case PropShape::TypedEntityRef: kind.elementSize = 16; break;
            default: kind.elementSize = 0; break;
        }

        return kind;
    }

    return kind;
}

std::optional<std::string_view> JsonStringField(std::string_view json, std::string_view key) {
    const size_t at = ValueStart(json, key);

    if (at == std::string_view::npos || at >= json.size() || json[at] != '"')
        return std::nullopt;

    size_t close = at + 1;

    while (close < json.size() && json[close] != '"')
        close += json[close] == '\\' ? 2 : 1;

    if (close >= json.size())
        return std::nullopt;

    return json.substr(at + 1, close - at - 1);
}

std::optional<double> JsonNumberField(std::string_view json, std::string_view key) {
    const size_t at = ValueStart(json, key);

    if (at == std::string_view::npos)
        return std::nullopt;

    size_t end = at;

    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-' ||
                                 json[end] == '+' || json[end] == '.' || json[end] == 'e' || json[end] == 'E')) {
        ++end;
    }

    if (end == at)
        return std::nullopt;

    const std::string text(json.substr(at, end - at));
    char* parsed = nullptr;
    const double value = std::strtod(text.c_str(), &parsed);

    if (parsed != text.c_str() + text.size() || !std::isfinite(value))
        return std::nullopt;

    return value;
}

} // namespace fr
