#include "doctest.h"

#include "LootMerces.h"

#include <set>
#include <cstdint>
#include <string_view>
#include <vector>

TEST_CASE("loot_templates_by_temp_or_tblu") {
    const fr::LootTemplate* safeByTemp = fr::FindLootTemplate(0x004F5ED36051B2C6ull);
    const fr::LootTemplate* safeByTblu = fr::FindLootTemplate(0x00F1DC5FBEE15B8Eull);
    REQUIRE(safeByTemp);
    CHECK(safeByTemp == safeByTblu);
    CHECK(safeByTemp->kind == fr::LootKind::Safe);

    CHECK(fr::FindLootTemplate(0x0025CD93E3A002EFull)->kind == fr::LootKind::MoneyMule);
    CHECK(fr::FindLootTemplate(0x00430B7BE6F18F94ull)->kind == fr::LootKind::Stash);
    CHECK(fr::FindLootTemplate(0x00B08A36F5AF5C5Bull)->kind == fr::LootKind::SupplyCrate);
    CHECK(fr::FindLootTemplate(0x003647AE3DABA492ull)->kind == fr::LootKind::SelectableMerces);
    CHECK(fr::FindLootTemplate(0x007DD9F93EF90D37ull)->kind == fr::LootKind::GlobalMoney);

    CHECK_FALSE(fr::FindLootTemplate(0));
    CHECK_FALSE(fr::FindLootTemplate(0x0000000000000001ull));
    CHECK_FALSE(fr::FindLootTemplate(0x00A9B93A825F5BDCull)); // the safehouse brick
}

TEST_CASE("loot_json_fields") {
    constexpr std::string_view pickup =
        R"({"Timestamp":382.129089,"Name":"ItemPickedUp","ContractId":"c147","Value":{"InstanceId":"",)"
        R"("ItemType":"Unrecognized Item type","ItemName":"Merces","RepositoryId":"da746498-bfa5-4904-af66-b9d3195e3e74",)"
        R"("OnlineTraits":["NONE"],"Category":null}})";

    CHECK(fr::JsonStringField(pickup, "Name") == std::string_view("ItemPickedUp"));
    CHECK(fr::JsonStringField(pickup, "ItemName") == std::string_view("Merces"));
    CHECK(fr::JsonStringField(pickup, "RepositoryId") == std::string_view("da746498-bfa5-4904-af66-b9d3195e3e74"));
    CHECK(fr::JsonStringField(pickup, "InstanceId") == std::string_view(""));
    CHECK_FALSE(fr::JsonStringField(pickup, "Category"));  // null
    CHECK_FALSE(fr::JsonStringField(pickup, "Timestamp")); // a number
    CHECK_FALSE(fr::JsonStringField(pickup, "Missing"));
    CHECK_FALSE(fr::JsonStringField(pickup, "Item")); // no partial key match
    REQUIRE(fr::JsonNumberField(pickup, "Timestamp"));
    CHECK(*fr::JsonNumberField(pickup, "Timestamp") == doctest::Approx(382.129089));
    CHECK_FALSE(fr::JsonNumberField(pickup, "ItemName"));

    // A key at the very end of the text (a cut blob) has no value.
    CHECK_FALSE(fr::JsonStringField(R"({"RepositoryId":)", "RepositoryId"));
    CHECK_FALSE(fr::JsonNumberField(R"({"Timestamp":)", "Timestamp"));

    // A string value equal to a key name is never taken for the key.
    constexpr std::string_view tricky = R"({"A":"Total","Total":5})";
    CHECK(fr::JsonNumberField(tricky, "Total") == 5.0);

    constexpr std::string_view data =
        R"({"Timestamp":0.000000,"Name":"Evergreen_Merces_Data","Value":{"TotalMercesEarned":37866157}})";
    CHECK(fr::JsonNumberField(data, "TotalMercesEarned") == 37866157.0);
    CHECK(fr::JsonNumberField(data, "Timestamp") == 0.0);
    CHECK(fr::JsonNumberField(R"({"N":-1250})", "N") == -1250.0);
    CHECK(fr::JsonNumberField(R"({"N" : 7})", "N") == 7.0);
    CHECK_FALSE(fr::JsonNumberField(R"({"N":})", "N"));
    CHECK_FALSE(fr::JsonStringField(R"({"S":"unterminated)", "S"));
    CHECK(fr::JsonStringField(R"({"S":"a\"b","T":"x"})", "T") == std::string_view("x"));
}

TEST_CASE("loot_entity_pointers") {
    CHECK(fr::IsPlausibleEntityPointer(0x000001F2'3456'7890ull));
    CHECK_FALSE(fr::IsPlausibleEntityPointer(0));
    CHECK_FALSE(fr::IsPlausibleEntityPointer(0x8000));
    CHECK_FALSE(fr::IsPlausibleEntityPointer(0x000001F2'3456'7891ull));
    CHECK_FALSE(fr::IsPlausibleEntityPointer(0xFFFF8000'0000'0000ull));
    CHECK_FALSE(fr::IsPlausibleEntityPointer(0x3F80'0000'0000'0000ull));
}

TEST_CASE("loot_prop_kinds") {
    CHECK(fr::PropKindOf("int32").shape == fr::PropShape::Numeric);
    CHECK(fr::PropKindOf("float32").numeric == fr::NumericKind::Float32);
    CHECK(fr::PropKindOf("bool").shape == fr::PropShape::Bool);
    CHECK(fr::PropKindOf("ZRepositoryID").shape == fr::PropShape::Guid);
    CHECK(fr::PropKindOf("ZGuid").shape == fr::PropShape::Guid);
    CHECK(fr::PropKindOf("ZEntityRef").shape == fr::PropShape::EntityRef);
    CHECK(fr::PropKindOf("TEntityRef<ZItemRepositoryKeyEntity>").shape == fr::PropShape::TypedEntityRef);
    CHECK(fr::PropKindOf("ZString").shape == fr::PropShape::Other);
    CHECK(fr::PropKindOf("TArray").shape == fr::PropShape::Other);

    const fr::PropKind repos = fr::PropKindOf("TArray<ZRepositoryID>");
    CHECK(repos.shape == fr::PropShape::Array);
    CHECK(repos.element == fr::PropShape::Guid);
    CHECK(repos.elementSize == 16);

    const fr::PropKind refs = fr::PropKindOf("TArray<TEntityRef<ZRandomSelectorChoice>>");
    CHECK(refs.element == fr::PropShape::TypedEntityRef);
    CHECK(refs.elementSize == 16);
    CHECK(fr::PropKindOf("TArray<ZEntityRef>").elementSize == 8);

    const fr::PropKind weights = fr::PropKindOf("TArray<float32>");
    CHECK(weights.element == fr::PropShape::Numeric);
    CHECK(weights.elementNumeric == fr::NumericKind::Float32);
    CHECK(weights.elementSize == 4);

    const fr::PropKind nested = fr::PropKindOf("TArray<TArray<int32>>");
    CHECK(nested.shape == fr::PropShape::Array);
    CHECK(nested.element == fr::PropShape::Other);
    CHECK(nested.elementSize == 0);
    CHECK(fr::PropKindOf("TArray<ZString>").elementSize == 0);
}
