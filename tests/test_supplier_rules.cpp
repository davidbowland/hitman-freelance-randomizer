#include "doctest.h"

#include "SupplierRules.h"

#include <string>
#include <vector>

TEST_CASE("stock class filters") {
    CHECK(fr::IsStockFlagClass("ZEvergreenGearUnlockMasteryInfo"));
    CHECK(fr::IsStockFlagClass("ZValueBool_Repository"));
    CHECK(fr::IsStockFlagClass("ZValueInt_Comparator_Poll"));
    CHECK(fr::IsStockFlagClass("ZRandomSelectorChoice"));
    CHECK(fr::IsStockFlagClass("ZMathMultiplyDivide"));
    CHECK(fr::IsStockFlagClass("ZItemRepositoryKeyEntity"));
    CHECK(fr::IsStockFlagClass("ZValueInt_Basic"));
    CHECK_FALSE(fr::IsStockFlagClass("ZSpatialEntity"));
    CHECK_FALSE(fr::IsStockFlagClass("ZItemSpawner"));

    CHECK(fr::IsStockSelectorClass("ZRandomSelector"));
    CHECK(fr::IsStockSelectorClass("ZSignalSelector"));
    CHECK_FALSE(fr::IsStockSelectorClass("ZRandomSelectorChoice"));
    CHECK_FALSE(fr::IsStockSelectorClass("ZActor"));
}

TEST_CASE("ClassifyStockItem") {
    CHECK(fr::ClassifyStockItem("Mercer_Evergreen_Small_RewardCrate", "", "") == fr::StockItemKind::Merces);
    CHECK(fr::ClassifyStockItem("", "Merces", "questitem") == fr::StockItemKind::Merces);
    CHECK(fr::ClassifyStockItem("Sieger 300", "", "sniperrifle") == fr::StockItemKind::Weapon);
    CHECK(fr::ClassifyStockItem("", "HWK21", "Pistol") == fr::StockItemKind::Weapon);
    CHECK(fr::ClassifyStockItem("Iron", "Iron", "melee") == fr::StockItemKind::Other);
}

TEST_CASE("IsSupplierVisitEvent") {
    CHECK(fr::IsSupplierVisitEvent("SupplierVisited"));
    CHECK_FALSE(fr::IsSupplierVisitEvent("SupplierVisited2"));
    CHECK_FALSE(fr::IsSupplierVisitEvent("ItemPickedUp"));
}

TEST_CASE("LooseBlockOf follows the six-block layout") {
    CHECK(fr::LooseBlockOf(0, fr::kLooseOptionCount) == 'A');
    CHECK(fr::LooseBlockOf(64, fr::kLooseOptionCount) == 'A');
    CHECK(fr::LooseBlockOf(65, fr::kLooseOptionCount) == 'B');
    CHECK(fr::LooseBlockOf(88, fr::kLooseOptionCount) == 'B');
    CHECK(fr::LooseBlockOf(89, fr::kLooseOptionCount) == 'C');
    CHECK(fr::LooseBlockOf(146, fr::kLooseOptionCount) == 'C');
    CHECK(fr::LooseBlockOf(147, fr::kLooseOptionCount) == 'D');
    CHECK(fr::LooseBlockOf(158, fr::kLooseOptionCount) == 'D');
    CHECK(fr::LooseBlockOf(159, fr::kLooseOptionCount) == 'E');
    CHECK(fr::LooseBlockOf(216, fr::kLooseOptionCount) == 'E');
    CHECK(fr::LooseBlockOf(217, fr::kLooseOptionCount) == 'F');
    CHECK(fr::LooseBlockOf(278, fr::kLooseOptionCount) == 'F');

    // Past the group, or another layout.
    CHECK(fr::LooseBlockOf(279, fr::kLooseOptionCount) == '?');
    CHECK(fr::LooseBlockOf(10, 280) == '?');
    CHECK(fr::LooseBlockOf(0, 0) == '?');
}

TEST_CASE("TallyText counts keys, most first") {
    CHECK(fr::TallyText({}) == "none");

    const std::vector<std::string> s_Keys {"melee", "tool", "melee", "", "melee", "tool", "distraction"};
    CHECK(fr::TallyText(s_Keys) == "melee 3, tool 2, ? 1, distraction 1");
}
