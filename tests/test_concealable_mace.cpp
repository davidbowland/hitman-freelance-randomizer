#include "doctest.h"

#include "ConcealableMace.h"

#include <map>
#include <string>

TEST_CASE("concealable mace: mace names") {
    CHECK(fr::IsMaceName("Mace", ""));
    CHECK(fr::IsMaceName("", "Melee_Mace"));
    CHECK(fr::IsMaceName("Mace Gold", ""));
    CHECK(fr::IsMaceName("(Melee) MACE", ""));
    CHECK(fr::IsMaceName("Ceremonial Mace 2", "x"));
    CHECK_FALSE(fr::IsMaceName("Grimace", ""));
    CHECK_FALSE(fr::IsMaceName("Macedonian Dagger", "Melee_Macedonian"));
    CHECK_FALSE(fr::IsMaceName("Katana", "Melee_Katana"));
    CHECK_FALSE(fr::IsMaceName("", ""));
}

TEST_CASE("concealable mace: melee icon and large sizes") {
    CHECK(fr::IsMeleeIcon("melee"));
    CHECK(fr::IsMeleeIcon("Melee"));
    CHECK_FALSE(fr::IsMeleeIcon("tool"));
    CHECK(fr::IsLargeItemSize("ITEMSIZE_LARGE"));
    CHECK(fr::IsLargeItemSize("itemsize_large"));
    CHECK_FALSE(fr::IsLargeItemSize("ITEMSIZE_MEDIUM"));
    CHECK_FALSE(fr::IsLargeItemSize(""));
}

TEST_CASE("concealable mace: the concealed ItemSize is the most common non-large one") {
    CHECK(fr::PickConcealedItemSize({}) == "ITEMSIZE_SMALL");
    CHECK(fr::PickConcealedItemSize({{"ITEMSIZE_LARGE", 9}}) == "ITEMSIZE_SMALL");
    CHECK(fr::PickConcealedItemSize({{"ITEMSIZE_SMALL", 3}, {"ITEMSIZE_MEDIUM", 5}, {"ITEMSIZE_LARGE", 9}}) ==
          "ITEMSIZE_MEDIUM");
    CHECK(fr::PickConcealedItemSize({{"ITEMSIZE_SMALL", 20}, {"ITEMSIZE_MEDIUM", 2}}) == "ITEMSIZE_SMALL");
    // Ties go to the smaller size.
    CHECK(fr::PickConcealedItemSize({{"ITEMSIZE_MEDIUM", 4}, {"ITEMSIZE_SMALL", 4}}) == "ITEMSIZE_SMALL");
    CHECK(fr::PickConcealedItemSize({{"ITEMSIZE_ODD", 4}, {"ITEMSIZE_MEDIUM", 4}}) == "ITEMSIZE_MEDIUM");
    CHECK(fr::PickConcealedItemSize({{"", 7}, {"ITEMSIZE_MEDIUM", 1}}) == "ITEMSIZE_MEDIUM");
}

TEST_CASE("concealable mace: plan writes only differing keys that exist") {
    fr::ConcealInput s_Large {true, "ITEMSIZE_LARGE", true, "eCanNotBeHolstered"};
    fr::ConcealChange s_Change = fr::PlanConceal(s_Large, "ITEMSIZE_SMALL");
    CHECK(s_Change.itemSize);
    CHECK(s_Change.holster);

    fr::ConcealInput s_Done {true, "ITEMSIZE_SMALL", true, "eHolsteringAllowed"};
    s_Change = fr::PlanConceal(s_Done, "ITEMSIZE_SMALL");
    CHECK_FALSE(s_Change.itemSize);
    CHECK_FALSE(s_Change.holster);

    fr::ConcealInput s_Temporary {true, "ITEMSIZE_MEDIUM", true, "eHolsterTemporarilyOnly"};
    s_Change = fr::PlanConceal(s_Temporary, "ITEMSIZE_SMALL");
    CHECK(s_Change.itemSize);
    CHECK(s_Change.holster);

    fr::ConcealInput s_Missing {false, "", false, ""};
    s_Change = fr::PlanConceal(s_Missing, "ITEMSIZE_SMALL");
    CHECK_FALSE(s_Change.itemSize);
    CHECK_FALSE(s_Change.holster);
}

TEST_CASE("concealable mace: restore only what still holds the written text") {
    CHECK(fr::ShouldRestoreText("ITEMSIZE_SMALL", "ITEMSIZE_SMALL", "ITEMSIZE_LARGE"));
    CHECK_FALSE(fr::ShouldRestoreText("ITEMSIZE_MEDIUM", "ITEMSIZE_SMALL", "ITEMSIZE_LARGE"));
    CHECK_FALSE(fr::ShouldRestoreText("ITEMSIZE_LARGE", "ITEMSIZE_SMALL", "ITEMSIZE_LARGE"));
    CHECK_FALSE(fr::ShouldRestoreText("ITEMSIZE_SMALL", "ITEMSIZE_SMALL", "ITEMSIZE_SMALL"));
}
