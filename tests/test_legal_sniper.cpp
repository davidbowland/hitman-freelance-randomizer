#include "doctest.h"

#include "LegalSniper.h"

#include <cstring>
#include <string>
#include <vector>

TEST_CASE("legal sniper: sniper keyword and items") {
    CHECK(fr::IsSniperKeyword("INVENTORY_TYPE_SNIPER"));
    CHECK(fr::IsSniperKeyword("inventory_type_sniper"));
    CHECK(fr::IsSniperKeyword("ITEM_SNIPER_RIFLE"));
    CHECK_FALSE(fr::IsSniperKeyword("MAGAZINE_SNIPER_STEALTH"));
    CHECK_FALSE(fr::IsSniperKeyword("INVENTORY_TYPE_ASSAULTRIFLE"));
    CHECK_FALSE(fr::IsSniperKeyword(""));

    CHECK(fr::IsSniperItem("sniperrifle", "eSniper_SakoTRG"));
    CHECK(fr::IsSniperItem("sniperrifle", ""));
    CHECK(fr::IsSniperItem("", "eSniper_SakoTRGSilenced"));
    CHECK_FALSE(fr::IsSniperItem("assaultrifle", "eRifle_HK416"));
    CHECK_FALSE(fr::IsSniperItem("pistol", "eGun_HardBaller_01"));
}

TEST_CASE("legal sniper: keyword texts") {
    using fr::EntryMatch;
    // Nothing read (the Dartmoor guards' "<no text>" keyword) is unknown, never "not a sniper".
    CHECK(fr::MatchKeywordTexts({}) == EntryMatch::Unknown);
    CHECK(fr::MatchKeywordTexts({"INVENTORY_TYPE_SNIPER"}) == EntryMatch::Sniper);
    CHECK(fr::MatchKeywordTexts({"Inventory_Type_Sniper"}) == EntryMatch::Sniper);
    CHECK(fr::MatchKeywordTexts({"INVENTORY_TYPE_PISTOL", "INVENTORY_TYPE_SNIPER"}) == EntryMatch::Sniper);
    CHECK(fr::MatchKeywordTexts({"INVENTORY_TYPE_ASSAULTRIFLE"}) == EntryMatch::Other);
    CHECK(fr::MatchKeywordTexts({"MAGAZINE_SNIPER_STEALTH"}) == EntryMatch::Other);
}

TEST_CASE("legal sniper: which kits are changed") {
    using fr::KitSniper;
    CHECK(fr::ClassifyKit({false, 0, 0}) == KitSniper::NoFirearms);
    // An exception without m_bWeaponsAllowed changes nothing: no firearm is legal there anyway.
    CHECK(fr::ClassifyKit({false, 1, 0}) == KitSniper::NoFirearms);
    CHECK(fr::ClassifyKit({false, 0, 0, 1}) == KitSniper::NoFirearms);
    CHECK(fr::ClassifyKit({false, 0, 1}) == KitSniper::NoFirearmsSniperOk);
    CHECK(fr::ClassifyKit({true, 0, 0}) == KitSniper::AlreadyLegal);
    CHECK(fr::ClassifyKit({true, 0, 2}) == KitSniper::AlreadyLegal);
    CHECK(fr::ClassifyKit({true, 1, 0}) == KitSniper::Legalize);
    CHECK(fr::ClassifyKit({true, 2, 1}) == KitSniper::Legalize);
    // The Dartmoor guards: firearms allowed, one exception keyword not read. Not "already legal".
    CHECK(fr::ClassifyKit({true, 0, 0, 1}) == KitSniper::Unknown);
    // A sniper exception read next to an unreadable one: the sniper one is still removed.
    CHECK(fr::ClassifyKit({true, 1, 0, 1}) == KitSniper::Legalize);
    CHECK_FALSE(fr::KitSniperText(KitSniper::Legalize).empty());
    CHECK(fr::KitSniperText(KitSniper::Unknown) != fr::KitSniperText(KitSniper::AlreadyLegal));
}

TEST_CASE("legal sniper: keyword ids in an entity's bytes") {
    CHECK(fr::LooksLikePointer(0x27665F60));
    CHECK(fr::LooksLikePointer(0x1ADCDACF8));
    CHECK(fr::LooksLikePointer(0x7FF6'1234'5678));
    CHECK_FALSE(fr::LooksLikePointer(0));
    CHECK_FALSE(fr::LooksLikePointer(1234));
    CHECK_FALSE(fr::LooksLikePointer(0xFFFF'FFFF'FFFF'FFFF));

    const auto s_Put32 = [](std::vector<uint8_t>& p_Bytes, size_t p_At, uint32_t p_Value) {
        std::memcpy(p_Bytes.data() + p_At, &p_Value, sizeof(p_Value));
    };
    const auto s_Put64 = [](std::vector<uint8_t>& p_Bytes, size_t p_At, uint64_t p_Value) {
        std::memcpy(p_Bytes.data() + p_At, &p_Value, sizeof(p_Value));
    };

    // ZEntityImpl header, then m_rHolder (+0x18), m_aHolders (+0x20..+0x38), then members of its own.
    std::vector<uint8_t> s_Window(0x50, 0);
    s_Put64(s_Window, 0x00, 0x7FF6'0000'1000);      // vtable
    s_Put64(s_Window, 0x08, 0x2766'0000);           // m_pEntityType
    s_Put32(s_Window, 0x10, 37);                    // m_nEntityPtrIndex: a registered id, but in the header
    s_Put32(s_Window, 0x18, 37);                    // inside a property: skipped
    s_Put32(s_Window, 0x38, 512);                   // the keyword id
    s_Put64(s_Window, 0x40, 0x0000'0025'0000'1000); // a pointer whose high half is 37: skipped
    const std::vector<fr::ByteRange> s_Skip {{0, 0x18}, {0x18, 0x20}, {0x20, 0x38}};
    const std::vector<int32_t> s_Registered {37, 512, 900};

    const auto s_Slots = fr::KeywordIdSlots(s_Window, s_Skip, s_Registered);
    REQUIRE(s_Slots.size() == 1);
    CHECK(s_Slots[0] == fr::KeywordIdSlot {0x38, 512});
    CHECK(fr::PickKeywordId(s_Slots) == 512);

    // Without the skips, the header and the property count too.
    CHECK(fr::KeywordIdSlots(s_Window, {}, s_Registered).size() == 3);

    // An unregistered value is no keyword id.
    CHECK(fr::KeywordIdSlots(s_Window, s_Skip, {900}).empty());

    // None, or two different ids: no pick (unknown); the same id twice is that id.
    CHECK_FALSE(fr::PickKeywordId({}));
    CHECK_FALSE(fr::PickKeywordId({{0x38, 512}, {0x3C, 900}}));
    CHECK(fr::PickKeywordId({{0x38, 512}, {0x40, 512}}) == 512);
}

TEST_CASE("legal sniper: keyword-like text and hex") {
    CHECK(fr::IsKeywordLikeText("INVENTORY_TYPE_SNIPER"));
    CHECK(fr::IsKeywordLikeText("ab"));
    CHECK_FALSE(fr::IsKeywordLikeText("a"));
    CHECK_FALSE(fr::IsKeywordLikeText("INVENTORY TYPE"));
    CHECK_FALSE(fr::IsKeywordLikeText(std::string(97, 'A')));
    CHECK_FALSE(fr::IsKeywordLikeText(std::string("AB\0C", 4)));
}

TEST_CASE("legal sniper: exception list order") {
    const fr::ExceptionEdit s_None = fr::PlanExceptionEdit({false, false});
    CHECK_FALSE(s_None.Changes());
    CHECK(s_None.kept == 2);
    CHECK(s_None.order == std::vector<size_t> {0, 1});

    const fr::ExceptionEdit s_Mixed = fr::PlanExceptionEdit({true, false, true, false});
    CHECK(s_Mixed.Changes());
    CHECK(s_Mixed.kept == 2);
    CHECK(s_Mixed.order == std::vector<size_t> {1, 3, 0, 2});

    const fr::ExceptionEdit s_All = fr::PlanExceptionEdit({true});
    CHECK(s_All.kept == 0);
    CHECK(s_All.order == std::vector<size_t> {0});

    CHECK_FALSE(fr::PlanExceptionEdit({}).Changes());
}

TEST_CASE("legal sniper: shrinking an array header") {
    const fr::RawArrayHeader s_Heap {0x1000, 0x1030, 0x1040}; // 3 of 4 slots of 16 bytes
    CHECK_FALSE(fr::IsInlineArray(s_Heap, 16));

    const auto s_One = fr::ShrinkArrayHeader(s_Heap, 16, 1);
    REQUIRE(s_One);
    CHECK(s_One->begin == 0x1000);
    CHECK(s_One->end == 0x1010);
    CHECK(s_One->third == 0x1040);

    const auto s_Zero = fr::ShrinkArrayHeader(s_Heap, 16, 0);
    REQUIRE(s_Zero);
    CHECK(s_Zero->end == 0x1000);

    CHECK(fr::ShrinkArrayHeader(s_Heap, 16, 3) == s_Heap);
    CHECK_FALSE(fr::ShrinkArrayHeader(s_Heap, 16, 4));
    CHECK_FALSE(fr::ShrinkArrayHeader({0x1000, 0x1008, 0}, 16, 0)); // partial element
    CHECK_FALSE(fr::ShrinkArrayHeader({0x1000, 0x0FF0, 0}, 16, 0)); // end before begin
    CHECK_FALSE(fr::ShrinkArrayHeader(s_Heap, 0, 0));

    // Inline: one 16-byte element in begin/end, count in the flags' low byte.
    const fr::RawArrayHeader s_Inline {0xAAAA, 0xBBBB, (uint64_t {1} << 62) | 0x0101};
    CHECK(fr::IsInlineArray(s_Inline, 16));
    CHECK_FALSE(fr::IsInlineArray(s_Inline, 24));
    const auto s_Empty = fr::ShrinkArrayHeader(s_Inline, 16, 0);
    REQUIRE(s_Empty);
    CHECK(s_Empty->begin == 0xAAAA);
    CHECK(s_Empty->end == 0xBBBB);
    CHECK(s_Empty->third == ((uint64_t {1} << 62) | 0x0100));
    CHECK_FALSE(fr::ShrinkArrayHeader(s_Inline, 16, 2));

    // An empty heap array stays empty.
    CHECK(fr::ShrinkArrayHeader({0, 0, 0}, 16, 0) == fr::RawArrayHeader {0, 0, 0});
}
