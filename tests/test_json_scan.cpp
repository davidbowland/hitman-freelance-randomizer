#include "doctest.h"

#include "JsonScan.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("contains_any_ignores_case") {
    constexpr std::array<std::string_view, 2> parts {"abc", ""};
    CHECK(fr::ContainsAnyNoCase("xxABcx", parts));
    CHECK_FALSE(fr::ContainsAnyNoCase("xyz", parts)); // the empty part never matches
}

TEST_CASE("json_members_with_key_at_any_depth") {
    constexpr std::array<std::string_view, 2> parts {"item", "reward"};
    const std::string_view json = R"({"Name":"CpdSet","Value":{"MyMoney":10,"InventoryItems":[{"Id":"a"},{"Id":"b"}],)"
                                  R"("Nested":{"LastReward":"x\"y","n":1},"itemcount": 3 },"Id":"z"})";
    const std::vector<fr::JsonMember> members = fr::JsonMembersWithKeyContaining(json, parts);
    REQUIRE(members.size() == 3);
    CHECK(members[0].key == "InventoryItems");
    CHECK(members[0].value == R"([{"Id":"a"},{"Id":"b"}])");
    CHECK(members[1].key == "LastReward");
    CHECK(members[1].value == R"("x\"y")");
    CHECK(members[2].key == "itemcount");
    CHECK(members[2].value == "3");

    // A matched object's own keys are not listed again.
    constexpr std::array<std::string_view, 1> outer {"reward"};
    const std::vector<fr::JsonMember> nested =
        fr::JsonMembersWithKeyContaining(R"({"Reward":{"RewardId":1},"b":2})", outer);
    REQUIRE(nested.size() == 1);
    CHECK(nested[0].value == R"({"RewardId":1})");

    // Strings that look like keys inside values are not keys.
    CHECK(fr::JsonMembersWithKeyContaining(R"({"a":"\"item\":1"})", parts).empty());

    // Malformed: what was found first is kept.
    const std::vector<fr::JsonMember> cut = fr::JsonMembersWithKeyContaining(R"({"item":1,"reward":[1,2)", parts);
    REQUIRE(cut.size() == 1);
    CHECK(cut[0].key == "item");
    CHECK(fr::JsonMembersWithKeyContaining("", parts).empty());
}

TEST_CASE("credentials_are_redacted") {
    CHECK(fr::HasSensitiveWord("Authorization"));
    CHECK(fr::HasSensitiveWord("ContractSessionId"));
    CHECK(fr::HasSensitiveWord("refresh_token"));
    CHECK(fr::HasSensitiveWord("x-ticket"));
    CHECK(fr::HasSensitiveWord("Password"));
    CHECK_FALSE(fr::HasSensitiveWord("RepositoryId"));

    const std::string redacted = fr::RedactJson(
        R"({"Name":"X","SessionId":"abc","Value":{"token":{"a":"secret"},"Item":"ok"},"auth":[{"Ticket":"t1"}]})");
    CHECK(
        redacted ==
        R"({"Name":"X","SessionId":"<redacted>","Value":{"token":"<redacted>","Item":"ok"},"auth":[{"Ticket":"<redacted>"}]})");
    CHECK(redacted.find("secret") == std::string::npos);
    CHECK(redacted.find("t1") == std::string::npos);

    // Nothing to redact: unchanged. A scalar is fine too.
    CHECK(fr::RedactJson(R"({"a":1})") == R"({"a":1})");
    CHECK(fr::RedactJson(R"("plain")") == R"("plain")");

    // Malformed: an unfinished sensitive member is never returned.
    const std::string cut = fr::RedactJson(R"({"a":1,"access_token":"abc)");
    CHECK(cut.find("abc") == std::string::npos);
    CHECK(cut.find("<cut: malformed>") != std::string::npos);
    CHECK(fr::RedactJson("not json token=abc").find("abc") == std::string::npos);
}
