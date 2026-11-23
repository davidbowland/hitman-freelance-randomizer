#include "doctest.h"

#include "PinWatchTable.h"

#include <cstdint>
#include <set>
#include <vector>

namespace {

const void* Ptr(uintptr_t p_Address) {
    return reinterpret_cast<const void*>(p_Address);
}

} // namespace

TEST_CASE("pin watch table: the first slot is the hash's top bits") {
    // The slot is the hash's top bits: a shift of 54 for 1024 slots, 53 for 2048.
    for (const uintptr_t s_Key : {uintptr_t {0x10}, uintptr_t {0x7ff6'1234'5670}, uintptr_t {0x1'0000'0000'0040}}) {
        CHECK(fr::PointerSlot(s_Key, 1024) == static_cast<size_t>((s_Key >> 4) * 0x9E3779B97F4A7C15ull >> 54));
        CHECK(fr::PointerSlot(s_Key, 2048) == static_cast<size_t>((s_Key >> 4) * 0x9E3779B97F4A7C15ull >> 53));
        CHECK(fr::PointerSlot(s_Key, 1024) < 1024);
    }

    // Neighboring 16-byte-aligned pointers spread over the slots.
    std::set<size_t> s_Slots;

    for (uintptr_t i = 0; i < 64; ++i)
        s_Slots.insert(fr::PointerSlot(0x2000'0000 + i * 16, 1024));

    CHECK(s_Slots.size() > 60);
}

TEST_CASE("pin watch table: watch, look up, replace and clear") {
    fr::PinWatchTable<16> s_Table;
    CHECK(s_Table.Empty());
    CHECK_FALSE(s_Table.Lookup(Ptr(0x100)));

    CHECK(s_Table.Watch(Ptr(0x100), 7));
    CHECK(s_Table.Watch(Ptr(0x200), 9));
    CHECK_FALSE(s_Table.Empty());
    CHECK(s_Table.Count() == 2);
    CHECK(s_Table.Lookup(Ptr(0x100)) == 7u);
    CHECK(s_Table.Lookup(Ptr(0x200)) == 9u);
    CHECK_FALSE(s_Table.Lookup(Ptr(0x300)));

    // Watching again only sets the value.
    CHECK(s_Table.Watch(Ptr(0x100), 8));
    CHECK(s_Table.Count() == 2);
    CHECK(s_Table.Lookup(Ptr(0x100)) == 8u);

    // A replaced key keeps its slot.
    s_Table.Replace(Ptr(0x100), 0);
    CHECK(s_Table.Lookup(Ptr(0x100)) == 0u);
    s_Table.Replace(Ptr(0x300), 5);
    CHECK_FALSE(s_Table.Lookup(Ptr(0x300)));

    s_Table.Clear();
    CHECK(s_Table.Empty());
    CHECK_FALSE(s_Table.Lookup(Ptr(0x200)));
}

TEST_CASE("pin watch table: fills to half, then refuses new keys") {
    fr::PinWatchTable<16> s_Table;
    std::vector<uintptr_t> s_Keys;

    for (uintptr_t i = 1; s_Keys.size() < 8; ++i) {
        REQUIRE(s_Table.Watch(Ptr(i * 16), static_cast<uint32_t>(i)));
        s_Keys.push_back(i * 16);
    }

    CHECK(s_Table.Count() == 8);
    CHECK_FALSE(s_Table.Watch(Ptr(0x9990), 1));
    // A key already in still takes a new value.
    CHECK(s_Table.Watch(Ptr(s_Keys[3]), 42));

    for (size_t i = 0; i < s_Keys.size(); ++i)
        CHECK(s_Table.Lookup(Ptr(s_Keys[i])) == (i == 3 ? 42u : static_cast<uint32_t>(s_Keys[i] / 16)));
}
