#include "doctest.h"

#include "Collections.h"

#include <optional>
#include <string>
#include <string_view>

TEST_CASE("entity_ref_types") {
    CHECK(fr::EntityRefSize("ZEntityRef") == 8u);
    CHECK(fr::EntityRefSize("TEntityRef<ZDifficultyParameterEntity>") == 16u);
    CHECK(fr::EntityRefSize("TEntityRef<IDifficultyCondition>") == 16u);
    CHECK_FALSE(fr::EntityRefSize("ZString").has_value());
    CHECK_FALSE(fr::EntityRefSize("").has_value());
    CHECK_FALSE(fr::EntityRefSize("TArray<ZEntityRef>").has_value());
}

TEST_CASE("entity_ref_array_element") {
    CHECK(fr::EntityRefArrayElementSize("TArray<ZEntityRef>") == 8u);
    CHECK(fr::EntityRefArrayElementSize("TArray<TEntityRef<ZDifficultyParameterEntity>>") == 16u);
    CHECK(fr::EntityRefArrayElementSize("TArray<TEntityRef<ZDifficultyParameterEntity> >") == 16u);
    CHECK_FALSE(fr::EntityRefArrayElementSize("TArray<float32>").has_value());
    CHECK_FALSE(fr::EntityRefArrayElementSize("ZEntityRef").has_value());
    CHECK_FALSE(fr::EntityRefArrayElementSize("TArray<ZEntityRef").has_value());
}

TEST_CASE("array_count") {
    // (end - begin) / element size, only for a sane, whole, bounded span.
    CHECK(fr::ArrayCount(0x1000, 0x1000 + 16 * 5, 16, 4096) == 5u);
    CHECK(fr::ArrayCount(0x1000, 0x1000, 8, 4096) == 0u);
    CHECK(fr::ArrayCount(0, 0, 8, 4096) == 0u);
    CHECK_FALSE(fr::ArrayCount(0x2000, 0x1000, 8, 4096).has_value());            // end before begin
    CHECK_FALSE(fr::ArrayCount(0x1000, 0x1000 + 12, 8, 4096).has_value());       // not whole elements
    CHECK_FALSE(fr::ArrayCount(0x1000, 0x1000 + 8 * 5000, 8, 4096).has_value()); // over the cap
    CHECK_FALSE(fr::ArrayCount(0, 0x80, 8, 4096).has_value());                   // null begin with elements
    CHECK_FALSE(fr::ArrayCount(0x1000, 0x1010, 0, 4096).has_value());
}

TEST_CASE("array_elements") {
    constexpr uint64_t kInline = uint64_t {1} << 62;

    // Heap storage: elements at begin, count from the span.
    {
        const auto s = fr::ArrayElements(0x9000, 0x1000, 0x1000 + 16 * 3, 0x1000 + 16 * 4, 16, 4096);
        REQUIRE(s.has_value());
        CHECK(s->begin == 0x1000u);
        CHECK(s->count == 3u);
        CHECK_FALSE(s->isInline);
    }

    // Inline storage (bit 62 of the third qword): the count is its low byte and
    // the elements live in the array's own first 16 bytes.
    {
        const auto s = fr::ArrayElements(0x9000, 0xDEAD, 0xBEEF, kInline | (2u << 8) | 1u, 8, 4096);
        REQUIRE(s.has_value());
        CHECK(s->begin == 0x9000u);
        CHECK(s->count == 1u);
        CHECK(s->isInline);
    }
    {
        const auto s = fr::ArrayElements(0x9000, 0, 0, kInline | 2u, 8, 4096);
        REQUIRE(s.has_value());
        CHECK(s->count == 2u);
    }
    {
        const auto s = fr::ArrayElements(0x9000, 0, 0, kInline | 1u, 16, 4096);
        REQUIRE(s.has_value());
        CHECK(s->count == 1u);
        CHECK(s->begin == 0x9000u);
    }
    {
        const auto s = fr::ArrayElements(0x9000, 0, 0, kInline, 16, 4096);
        REQUIRE(s.has_value());
        CHECK(s->count == 0u);
    }

    // More inline elements than fit in 16 bytes, or over the cap: implausible.
    CHECK_FALSE(fr::ArrayElements(0x9000, 0, 0, kInline | 3u, 8, 4096).has_value());
    CHECK_FALSE(fr::ArrayElements(0x9000, 0, 0, kInline | 2u, 16, 4096).has_value());
    CHECK_FALSE(fr::ArrayElements(0x9000, 0, 0, kInline | 2u, 8, 1).has_value());
    CHECK_FALSE(fr::ArrayElements(0, 0, 0, kInline | 1u, 8, 4096).has_value());

    // Elements too large to fit inline ignore the flag (as the SDK's TArray does).
    {
        const auto s = fr::ArrayElements(0x9000, 0x1000, 0x1000 + 24 * 2, kInline | 1u, 24, 4096);
        REQUIRE(s.has_value());
        CHECK(s->count == 2u);
        CHECK(s->begin == 0x1000u);
        CHECK_FALSE(s->isInline);
    }

    // Heap span rules still apply.
    CHECK_FALSE(fr::ArrayElements(0x9000, 0x2000, 0x1000, 0, 8, 4096).has_value());
}
