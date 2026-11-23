#include "LegalSniper.h"

#include "Scene.h"

#include <cctype>
#include <cstring>
#include <set>

namespace fr {

namespace {

constexpr uint64_t kInlineFlag = uint64_t {1} << 62;
constexpr size_t kInlineBytes = 2 * sizeof(uint64_t);

} // namespace

bool IsSniperKeyword(std::string_view keyword) {
    const std::string s_Keyword = ToLower(keyword);
    return s_Keyword == ToLower(kSniperKeyword) || s_Keyword == ToLower(kSniperRifleKeyword);
}

bool IsSniperItem(std::string_view inventoryCategoryIcon, std::string_view itemType) {
    return ToLower(inventoryCategoryIcon) == "sniperrifle" || ToLower(itemType).starts_with("esniper");
}

EntryMatch MatchKeywordTexts(const std::vector<std::string>& texts) {
    if (texts.empty())
        return EntryMatch::Unknown;

    for (const std::string& s_Text : texts)
        if (IsSniperKeyword(s_Text))
            return EntryMatch::Sniper;

    return EntryMatch::Other;
}

KitSniper ClassifyKit(const KitSniperFacts& facts) {
    if (!facts.weaponsAllowed)
        return facts.sniperAllowed > 0 ? KitSniper::NoFirearmsSniperOk : KitSniper::NoFirearms;

    if (facts.sniperExceptions > 0)
        return KitSniper::Legalize;

    return facts.unknownExceptions > 0 ? KitSniper::Unknown : KitSniper::AlreadyLegal;
}

std::string_view KitSniperText(KitSniper verdict) {
    switch (verdict) {
        case KitSniper::NoFirearms: return "no visible firearms allowed; left alone";
        case KitSniper::NoFirearmsSniperOk: return "no visible firearms allowed, but a sniper rifle is; left alone";
        case KitSniper::AlreadyLegal: return "firearms allowed, sniper rifles already legal";
        case KitSniper::Legalize: return "firearms allowed, sniper rifles excepted";
        case KitSniper::Unknown:
            return "firearms allowed, an exception UNREADABLE (may be the sniper rifles); left alone";
    }

    return "?";
}

ExceptionEdit PlanExceptionEdit(const std::vector<bool>& isSniper) {
    ExceptionEdit s_Edit;

    for (size_t i = 0; i < isSniper.size(); ++i)
        if (!isSniper[i])
            s_Edit.order.push_back(i);

    s_Edit.kept = s_Edit.order.size();

    for (size_t i = 0; i < isSniper.size(); ++i)
        if (isSniper[i])
            s_Edit.order.push_back(i);

    return s_Edit;
}

bool IsInlineArray(const RawArrayHeader& header, size_t elementSize) {
    return elementSize > 0 && elementSize <= kInlineBytes && (header.third & kInlineFlag) != 0;
}

std::optional<RawArrayHeader> ShrinkArrayHeader(const RawArrayHeader& header, size_t elementSize, size_t newCount) {
    if (elementSize == 0)
        return std::nullopt;

    RawArrayHeader s_Out = header;

    if (IsInlineArray(header, elementSize)) {
        const size_t s_Count = static_cast<size_t>(header.third & 0xFF);

        if (newCount > s_Count || s_Count * elementSize > kInlineBytes)
            return std::nullopt;

        s_Out.third = (header.third & ~uint64_t {0xFF}) | static_cast<uint64_t>(newCount);
        return s_Out;
    }

    if (header.end < header.begin || (header.end - header.begin) % elementSize != 0 ||
        (header.begin == 0 && header.end != 0))
        return std::nullopt;

    const size_t s_Count = static_cast<size_t>((header.end - header.begin) / elementSize);

    if (newCount > s_Count)
        return std::nullopt;

    s_Out.end = header.begin + static_cast<uint64_t>(newCount * elementSize);
    return s_Out;
}

bool LooksLikePointer(uint64_t value) {
    return value >= 0x10000 && value < (uint64_t {1} << 47);
}

std::vector<KeywordIdSlot> KeywordIdSlots(const std::vector<uint8_t>& window, const std::vector<ByteRange>& skip,
                                          const std::vector<int32_t>& registeredIds) {
    std::vector<KeywordIdSlot> s_Out;
    const std::set<int32_t> s_Registered(registeredIds.begin(), registeredIds.end());

    for (size_t s_Offset = 0; s_Offset + 4 <= window.size(); s_Offset += 4) {
        bool s_Skipped = false;

        for (const ByteRange& s_Range : skip)
            s_Skipped = s_Skipped || (s_Offset < s_Range.end && s_Offset + 4 > s_Range.begin);

        const size_t s_Qword = s_Offset & ~size_t {7};

        if (!s_Skipped && s_Qword + 8 <= window.size()) {
            uint64_t s_Value = 0;
            std::memcpy(&s_Value, window.data() + s_Qword, sizeof(s_Value));
            s_Skipped = LooksLikePointer(s_Value);
        }

        if (s_Skipped)
            continue;

        int32_t s_Id = 0;
        std::memcpy(&s_Id, window.data() + s_Offset, sizeof(s_Id));

        if (s_Registered.contains(s_Id))
            s_Out.push_back({s_Offset, s_Id});
    }

    return s_Out;
}

std::optional<int32_t> PickKeywordId(const std::vector<KeywordIdSlot>& slots) {
    if (slots.empty())
        return std::nullopt;

    for (const KeywordIdSlot& s_Slot : slots)
        if (s_Slot.id != slots.front().id)
            return std::nullopt;

    return slots.front().id;
}

bool IsKeywordLikeText(std::string_view text) {
    if (text.size() < 2 || text.size() > 96)
        return false;

    for (const char c : text)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;

    return true;
}

} // namespace fr
