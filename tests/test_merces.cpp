#include "doctest.h"

#include "Merces.h"
#include "support/PayoutTestUtil.h"

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::array<std::string_view, 3> kCpdParts {"Payout", "Money", "Merces"};

using fr::test::PinnedPlan;

} // namespace

TEST_CASE("merces_amount_index") {
    for (size_t i = 0; i < fr::kPayoutAmounts.size(); ++i)
        CHECK(fr::PayoutAmountIndex(fr::kPayoutAmounts[i]) == i);

    // The payout table's probable prestige bases.
    CHECK(fr::PayoutAmountIndex(2000));
    CHECK(fr::PayoutAmountIndex(2500));

    for (const int64_t miss : {0LL, 1LL, 199LL, 201LL, 301LL, 395LL, 1999LL, 6999LL, 7001LL, -500LL, -7000LL})
        CHECK_FALSE(fr::PayoutAmountIndex(miss));

    CHECK_FALSE(fr::PayoutAmountIndex(7000LL + (1LL << 32)));
}

TEST_CASE("merces_numeric_kinds") {
    CHECK(fr::NumericKindOf("int32") == fr::NumericKind::Int32);
    CHECK(fr::NumericKindOf("int64") == fr::NumericKind::Int64);
    CHECK(fr::NumericKindOf("float32") == fr::NumericKind::Float32);

    for (const std::string_view other : {"", "uint32", "int16", "float64", "Int32", "int32 ", "bool", "ZString"})
        CHECK(fr::NumericKindOf(other) == fr::NumericKind::Other);

    CHECK(fr::NumericKindSize(fr::NumericKind::Int32) == 4);
    CHECK(fr::NumericKindSize(fr::NumericKind::Int64) == 8);
    CHECK(fr::NumericKindSize(fr::NumericKind::Float32) == 4);
    CHECK(fr::NumericKindSize(fr::NumericKind::Other) == 0);
}

TEST_CASE("merces_event_name") {
    CHECK(fr::JsonEventName(R"({"Name":"Activate_SA","Value":{"SafehouseDummy":"","Payout":"7000"}})") ==
          "Activate_SA");
    CHECK(fr::JsonEventName(R"({"Value":{"Payout":"250"}, "Name" : "SetPayout"})") == "SetPayout");
    CHECK(fr::JsonEventName(R"({"Value":{}})").empty());
    CHECK(fr::JsonEventName(R"({"Name":3})").empty());
    CHECK(fr::JsonEventName(R"({"Name":"unterminated)").empty());
    CHECK(fr::JsonEventName("").empty());
}

TEST_CASE("merces_cpd_scalars") {
    constexpr std::string_view cpd =
        R"({"Name":"CpdSet","Value":{"MyMoney":4112.000000,"MyPayout":6750.000000,"Sapo_BasePayout":250.000000,)"
        R"("CPD_TotalMercesEarned":37844622,"PayoutTable":{"MyPayout":1},"Sapo_Name":"Payout","Other":1}})";
    const auto found = fr::JsonScalarsWithKeyContaining(cpd, kCpdParts);

    REQUIRE(found.size() == 5);
    CHECK(found[0].key == "MyMoney");
    CHECK(found[0].value == "4112.000000");
    CHECK(found[1].key == "MyPayout");
    CHECK(found[1].value == "6750.000000");
    CHECK(found[2].key == "Sapo_BasePayout");
    CHECK(found[2].value == "250.000000");
    CHECK(found[3].key == "CPD_TotalMercesEarned");
    CHECK(found[3].value == "37844622");
    // Nested scalars are found; the object-valued "PayoutTable" and the string
    // value "Payout" are not keys with scalars.
    CHECK(found[4].key == "MyPayout");
    CHECK(found[4].value == "1");
}

TEST_CASE("merces_cpd_scalars_once_per_key") {
    // A key containing two parts, and a part twice in one key.
    constexpr std::string_view json = R"({"MercesPayout":5,"PayoutPayout":"x","Plain":2})";
    const auto found = fr::JsonScalarsWithKeyContaining(json, kCpdParts);

    REQUIRE(found.size() == 2);
    CHECK(found[0].key == "MercesPayout");
    CHECK(found[0].value == "5");
    CHECK(found[1].key == "PayoutPayout");
    CHECK(found[1].value == "\"x\"");

    CHECK(fr::JsonScalarsWithKeyContaining("", kCpdParts).empty());
    CHECK(fr::JsonScalarsWithKeyContaining(json, {}).empty());
}

TEST_CASE("merces_one_line") {
    CHECK(fr::OneLine("a\nb\tc\r", 100) == "a b c ");
    CHECK(fr::OneLine("abcdef", 3) == "abc... (6 chars)");
    CHECK(fr::OneLine("abc", 3) == "abc");
    CHECK(fr::OneLine("", 3).empty());
    CHECK(fr::OneLine("a\x01"
                      "b\x7f",
                      100) == "a b ");
}

TEST_CASE("merces_classify_payout") {
    fr::PrestigeMemory memory;

    // By value.
    CHECK(fr::ClassifyPayout(memory, 1, 0).kind == fr::PayoutKind::Unknown);
    CHECK(fr::ClassifyPayout(memory, 1, 1).kind == fr::PayoutKind::Normal);
    CHECK(fr::ClassifyPayout(memory, 1, 1500).kind == fr::PayoutKind::Normal);
    CHECK(fr::ClassifyPayout(memory, 1, 1501).kind == fr::PayoutKind::Unknown);
    CHECK(fr::ClassifyPayout(memory, 1, 5000).kind == fr::PayoutKind::Unknown);
    CHECK_FALSE(fr::ClassifyPayout(memory, 1, 1000).observed);

    // Observed prestige needs a known bonus.
    memory.Observe(2, true);
    CHECK(fr::ClassifyPayout(memory, 2, 1000).kind == fr::PayoutKind::Unknown);
    CHECK(fr::ClassifyPayout(memory, 2, 1000).observed);
    memory.bonus = 2000;
    CHECK(fr::ClassifyPayout(memory, 2, 1000).kind == fr::PayoutKind::Prestige);
    CHECK(fr::ClassifyPayout(memory, 2, 5000).kind == fr::PayoutKind::Prestige);

    // Observed normal wins over the value.
    memory.Observe(3, false);
    CHECK(fr::ClassifyPayout(memory, 3, 2000).kind == fr::PayoutKind::Normal);
    CHECK(fr::ClassifyPayout(memory, 3, 2000).observed);
}

TEST_CASE("merces_prestige_memory_observe") {
    fr::PrestigeMemory memory;
    CHECK(memory.Observe(7, true));
    CHECK_FALSE(memory.Observe(7, true));
    CHECK(memory.prestige.contains(7));

    // A later observation moves the id.
    CHECK(memory.Observe(7, false));
    CHECK_FALSE(memory.prestige.contains(7));
    CHECK(memory.normal.contains(7));
    CHECK_FALSE(memory.Observe(7, false));
}

TEST_CASE("merces_plan_write") {
    // Fresh bases.
    auto plan = PinnedPlan(1000, fr::PayoutKind::Normal, 2000, 2.0, std::nullopt);
    CHECK(plan.action == fr::PayoutAction::Write);
    CHECK(plan.target == 2000);

    plan = PinnedPlan(5000, fr::PayoutKind::Prestige, 2000, 2.0, std::nullopt);
    CHECK(plan.action == fr::PayoutAction::Write);
    CHECK(plan.target == 12000);

    // A rebuilt table holds the vanilla base again: written again.
    plan = PinnedPlan(1000, fr::PayoutKind::Normal, 2000, 2.0, 2000);
    CHECK(plan.action == fr::PayoutAction::Write);
    CHECK(plan.target == 2000);

    // A kept table holds this mod's value, or that plus the bonus.
    CHECK(PinnedPlan(2000, fr::PayoutKind::Normal, 2000, 2.0, 2000).action == fr::PayoutAction::AlreadyWritten);
    CHECK(PinnedPlan(12000, fr::PayoutKind::Prestige, 2000, 2.0, 12000).action == fr::PayoutAction::AlreadyWritten);
    CHECK(PinnedPlan(14000, fr::PayoutKind::Prestige, 2000, 2.0, 12000).action == fr::PayoutAction::AlreadyWritten);
    // The bonus case is for prestige entries only.
    plan = PinnedPlan(4000, fr::PayoutKind::Normal, 2000, 2.0, 2000);
    CHECK(plan.action == fr::PayoutAction::Write);
    CHECK(plan.target == 8000);
    // Even an entry now unknown is not taken as a new base.
    CHECK(PinnedPlan(2000, fr::PayoutKind::Unknown, 2000, 2.0, 2000).action == fr::PayoutAction::AlreadyWritten);

    // Unchanged, Unknown, NoTarget.
    plan = PinnedPlan(1000, fr::PayoutKind::Normal, 2000, 1.0, std::nullopt);
    CHECK(plan.action == fr::PayoutAction::Unchanged);
    CHECK(plan.target == 1000);
    CHECK(PinnedPlan(5000, fr::PayoutKind::Unknown, 2000, 2.0, std::nullopt).action == fr::PayoutAction::Unknown);
    CHECK(PinnedPlan(5000, fr::PayoutKind::Prestige, 2000, 1000.0, std::nullopt).action ==
          fr::PayoutAction::NoTarget); // above kPayoutLimit

    // Only known payout amounts are written, whatever the kind.
    for (const int32_t other : {0, -1000, 1, 999, 1234, 2001, 12000}) {
        CHECK(PinnedPlan(other, fr::PayoutKind::Normal, 2000, 2.0, std::nullopt).action ==
              fr::PayoutAction::NotAnAmount);
        CHECK(PinnedPlan(other, fr::PayoutKind::Unknown, 2000, 2.0, std::nullopt).action ==
              fr::PayoutAction::NotAnAmount);
    }

    CHECK(PinnedPlan(2500, fr::PayoutKind::Prestige, 2000, 2.0, std::nullopt).target == 7000);
}

TEST_CASE("merces_plan_never_doubles_twice") {
    // Applying the plan to its own result (a table kept across a load, with
    // and without the bonus added in between) never writes again.
    for (const auto kind : {fr::PayoutKind::Normal, fr::PayoutKind::Prestige}) {
        for (const int32_t base : {500, 1000, 1500, 2000, 3000, 5000, 6000}) {
            const auto first = PinnedPlan(base, kind, 2000, 2.0, std::nullopt);
            REQUIRE(first.action == fr::PayoutAction::Write);

            CHECK(PinnedPlan(first.target, kind, 2000, 2.0, first.target).action == fr::PayoutAction::AlreadyWritten);

            if (kind == fr::PayoutKind::Prestige) {
                CHECK(PinnedPlan(first.target + 2000, kind, 2000, 2.0, first.target).action ==
                      fr::PayoutAction::AlreadyWritten);
            }

            // A rebuilt table: the same write again, not a doubled one.
            const auto again = PinnedPlan(base, kind, 2000, 2.0, first.target);
            CHECK(again.action == fr::PayoutAction::Write);
            CHECK(again.target == first.target);
        }
    }
}

TEST_CASE("merces_common_bonus") {
    bool disagree = true;
    CHECK_FALSE(fr::CommonBonus({}, &disagree));
    CHECK_FALSE(disagree);

    const std::array<int32_t, 5> unchanged {0, 0, 0, -5, 0};
    CHECK_FALSE(fr::CommonBonus(unchanged, &disagree));
    CHECK_FALSE(disagree);

    const std::array<int32_t, 6> bonus {0, 2000, 0, 2000, 2000, -1};
    CHECK(fr::CommonBonus(bonus, &disagree) == 2000);
    CHECK_FALSE(disagree);

    const std::array<int32_t, 3> mixed {2000, 0, 1500};
    CHECK_FALSE(fr::CommonBonus(mixed, &disagree));
    CHECK(disagree);
}

namespace {

// n entries with ids first.. changing by delta.
void AddChanges(std::vector<fr::EntryChange>& changes, uint64_t first, size_t n, int32_t delta) {
    for (size_t i = 0; i < n; ++i)
        changes.push_back({first + i, delta});
}

} // namespace

TEST_CASE("merces_learn_first_scene") {
    // Game data, a safehouse: 34 prestige entries +2000, 63 normal unchanged.
    fr::PrestigeMemory memory;
    std::vector<fr::EntryChange> changes;
    AddChanges(changes, 100, 34, 2000);
    AddChanges(changes, 200, 63, 0);

    const auto result = fr::LearnFromScene(memory, changes);
    CHECK(result.trusted);
    CHECK(result.changed);
    CHECK(result.bonus == 2000);
    CHECK(result.withBonus == 34);
    CHECK(memory.bonus == 2000);
    CHECK(memory.prestige.size() == 34);
    CHECK(memory.normal.size() == 63);

    // The same scene again changes nothing.
    CHECK_FALSE(fr::LearnFromScene(memory, changes).changed);
}

TEST_CASE("merces_learn_needs_enough_entries") {
    // Fewer than kMinBonusEntries with nothing known: not trusted.
    fr::PrestigeMemory memory;
    std::vector<fr::EntryChange> changes;
    AddChanges(changes, 100, fr::kMinBonusEntries - 1, 2000);
    AddChanges(changes, 200, 50, 0);

    auto result = fr::LearnFromScene(memory, changes);
    CHECK(result.bonus == 2000);
    CHECK_FALSE(result.trusted);
    CHECK_FALSE(result.changed);
    CHECK_FALSE(memory.bonus);
    CHECK(memory.prestige.empty());
    CHECK(memory.normal.empty());

    // Exactly kMinBonusEntries: trusted.
    changes.push_back({150, 2000});
    result = fr::LearnFromScene(memory, changes);
    CHECK(result.trusted);
    CHECK(memory.prestige.size() == fr::kMinBonusEntries);
}

TEST_CASE("merces_learn_most_known_prestige") {
    // Few entries changed, but most known prestige entries did: trusted.
    fr::PrestigeMemory memory;
    memory.bonus = 2000;

    for (uint64_t id = 100; id < 104; ++id)
        memory.Observe(id, true);

    std::vector<fr::EntryChange> changes {{100, 2000}, {101, 2000}, {102, 2000}, {103, 0}, {200, 0}};
    auto result = fr::LearnFromScene(memory, changes);
    CHECK(result.knownRead == 4);
    CHECK(result.knownWithBonus == 3);
    CHECK(result.trusted);
    // Same bonus: the unchanged known prestige entry becomes normal.
    CHECK(memory.normal.contains(103));
    CHECK(memory.normal.contains(200));

    // Half is not most.
    fr::PrestigeMemory half;
    half.bonus = 2000;

    for (uint64_t id = 100; id < 104; ++id)
        half.Observe(id, true);

    changes = {{100, 2000}, {101, 2000}, {102, 0}, {103, 0}};
    result = fr::LearnFromScene(half, changes);
    CHECK_FALSE(result.trusted);
    CHECK(half.prestige.size() == 4);
}

TEST_CASE("merces_learn_keeps_prestige_when_bonus_differs") {
    fr::PrestigeMemory memory;
    memory.bonus = 2000;

    for (uint64_t id = 100; id < 120; ++id)
        memory.Observe(id, true);

    // A scene with bonus 1500 on 15 of the 20 known entries; the other 5 unchanged.
    std::vector<fr::EntryChange> changes;
    AddChanges(changes, 100, 15, 1500);
    AddChanges(changes, 115, 5, 0);
    AddChanges(changes, 300, 10, 0);

    const auto result = fr::LearnFromScene(memory, changes);
    CHECK(result.trusted);
    CHECK(result.keptPrestige == 5);
    CHECK(memory.bonus == 1500);
    CHECK(memory.prestige.size() == 20);
    CHECK(memory.normal.size() == 10);
}

TEST_CASE("merces_learn_bonus_zero") {
    fr::PrestigeMemory memory;
    memory.bonus = 2000;
    memory.Observe(100, true);
    memory.Observe(101, true);
    memory.Observe(200, false);

    // Every known prestige entry unchanged, nothing rose: bonus 0.
    std::vector<fr::EntryChange> changes {{100, 0}, {101, 0}, {200, 0}};
    auto result = fr::LearnFromScene(memory, changes);
    CHECK(result.bonusZero);
    CHECK(result.changed);
    CHECK(memory.bonus == 0);
    CHECK(memory.prestige.size() == 2);
    // A prestige entry with bonus 0 is written like a normal one.
    CHECK(fr::ClassifyPayout(memory, 100, 5000).kind == fr::PayoutKind::Prestige);
    CHECK(fr::test::PinnedPayout(5000, 2.0, fr::PayoutKind::Prestige, *memory.bonus) == 10000);

    // Already 0: nothing more.
    CHECK_FALSE(fr::LearnFromScene(memory, changes).changed);

    // No known prestige entry read, or one of them changed otherwise: not set to 0.
    fr::PrestigeMemory other;
    other.bonus = 2000;
    other.Observe(100, true);
    changes = {{200, 0}};
    CHECK_FALSE(fr::LearnFromScene(other, changes).bonusZero);
    changes = {{100, -5}, {200, 0}};
    CHECK_FALSE(fr::LearnFromScene(other, changes).bonusZero);
    CHECK(other.bonus == 2000);

    // Unknown bonus: nothing to set.
    fr::PrestigeMemory fresh;
    changes = {{100, 0}};
    CHECK_FALSE(fr::LearnFromScene(fresh, changes).changed);
    CHECK_FALSE(fresh.bonus);
}

TEST_CASE("merces_learn_disagreement") {
    fr::PrestigeMemory memory;
    std::vector<fr::EntryChange> changes;
    AddChanges(changes, 100, 20, 2000);
    changes.push_back({300, 1500});
    AddChanges(changes, 200, 20, 0);

    const auto result = fr::LearnFromScene(memory, changes);
    CHECK(result.disagree);
    CHECK_FALSE(result.trusted);
    CHECK_FALSE(result.changed);
    CHECK(memory.prestige.empty());
}

TEST_CASE("merces_id_list") {
    const std::set<uint64_t> ids {fr::kPayoutTableId, 0xd9788bdba0da37ccull, 0x1ull};
    const std::string text = fr::FormatIdList(ids);
    CHECK(text == "0000000000000001,1c8a3cd106f999fe,d9788bdba0da37cc");

    std::vector<std::string> bad;
    CHECK(fr::ParseIdList(text, bad) == ids);
    CHECK(bad.empty());

    CHECK(fr::FormatIdList({}).empty());
    CHECK(fr::ParseIdList("", bad).empty());

    // Whitespace, empty entries, upper case, short ids; bad entries reported.
    const auto parsed = fr::ParseIdList(" D9788BDBA0DA37CC ,, 1f ,xyz,-1,0x10,11111111111111111,1 2", bad);
    CHECK(parsed == std::set<uint64_t> {0xd9788bdba0da37ccull, 0x1full});
    CHECK(bad == std::vector<std::string> {"xyz", "-1", "0x10", "11111111111111111", "1 2"});
}

TEST_CASE("merces_prestige_display_ids") {
    // 36 helpers, objectives 101-136 each once, ids distinct.
    std::set<int32_t> objectives;
    std::set<uint64_t> ids;

    for (const fr::PrestigeHelper& helper : fr::kPrestigeHelpers) {
        objectives.insert(helper.objective);
        ids.insert(helper.id);
        CHECK(fr::PrestigeHelperObjective(helper.id) == helper.objective);
    }

    CHECK(objectives.size() == 36);
    CHECK(*objectives.begin() == 101);
    CHECK(*objectives.rbegin() == 136);
    CHECK(ids.size() == 36);
    CHECK(fr::PrestigeHelperObjective(0x283ab5e07d1b5593ull) == 134); // Help_Prestige_BlowOpenSafe
    CHECK(fr::PrestigeHelperObjective(fr::kPayoutTableId) == std::nullopt);
    CHECK(std::string(fr::PrestigeObjectiveName(134)) == "Open Safe - Explosive");
    CHECK(std::string(fr::PrestigeObjectiveName(109)) == "Headshot Kill - Legendary");
    CHECK(std::string(fr::PrestigeObjectiveName(105)) == "Sub Machine Gun Kill - Epic");
    CHECK(std::string(fr::PrestigeObjectiveName(133)) == "Neutralize Lookout");
    CHECK(fr::PrestigeObjectiveName(100) == nullptr);
}

TEST_CASE("merces_plan_display_write") {
    using A = fr::DisplayAction;

    // Game data, Blow Open A Safe: base 2500, bonus 2000, table written 2100 (shows 4100).
    // The copy before the bonus: the table's stored value.
    auto plan = fr::PlanDisplayWrite(2500, 2500, 2100, 2000, std::nullopt);
    CHECK(plan.action == A::Write);
    CHECK(plan.target == 2100);
    CHECK_FALSE(plan.withBonus);

    // The copy with the bonus (the poll's values): the shown amount.
    plan = fr::PlanDisplayWrite(4500, 2500, 2100, 2000, std::nullopt);
    CHECK(plan.action == A::Write);
    CHECK(plan.target == 4100);
    CHECK(plan.withBonus);

    // After this mod's write, alone or with the bonus added since: in step.
    CHECK(fr::PlanDisplayWrite(2100, 2500, 2100, 2000, 2100).action == A::InStep);
    CHECK(fr::PlanDisplayWrite(4100, 2500, 2100, 2000, 2100).action == A::InStep);
    CHECK(fr::PlanDisplayWrite(4100, 2500, 2100, 2000, 4100).action == A::InStep);
    CHECK(fr::PlanDisplayWrite(4100, 2500, 2100, 2000, std::nullopt).action == A::InStep);

    // A write equal to base - bonus: now 2500 is this mod's write plus the bonus, not the vanilla base.
    CHECK(fr::PlanDisplayWrite(2500, 4500, 500, 2000, 500).action == A::InStep);

    // The table entry was not changed: nothing to do.
    CHECK(fr::PlanDisplayWrite(4500, 2500, 2500, 2000, std::nullopt).action == A::Unchanged);

    // Anything else is left alone.
    CHECK(fr::PlanDisplayWrite(0, 2500, 2100, 2000, std::nullopt).action == A::Unrecognized);
    CHECK(fr::PlanDisplayWrite(3500, 2500, 2100, 2000, std::nullopt).action == A::Unrecognized);

    // Without a bonus only the base is recognized.
    CHECK(fr::PlanDisplayWrite(4500, 2500, 2100, 0, std::nullopt).action == A::Unrecognized);
    plan = fr::PlanDisplayWrite(2500, 2500, 2100, 0, std::nullopt);
    CHECK(plan.action == A::Write);
    CHECK(plan.target == 2100);
}

TEST_CASE("merces_value_property_id") {
    // CRC32 of "m_nValue", as the engine hashes property names.
    CHECK(fr::kValueIntValueId == 0xd33b1d5au);
}

TEST_CASE("merces_table_brick_order") {
    const std::vector<uint64_t> loaded {0x10, 0x20, 0x30, 0x40, 0x50};

    SUBCASE("nothing known keeps load order") {
        const fr::TableBrickOrder order = fr::OrderTableBricks(loaded, {}, {});
        CHECK(order.order == std::vector<size_t> {0, 1, 2, 3, 4});
        CHECK(order.tiers == std::vector<fr::TableBrickTier>(5, fr::TableBrickTier::Other));
    }

    SUBCASE("held, then named, then the rest") {
        const fr::TableBrickOrder order = fr::OrderTableBricks(loaded, {0x40, 0x99}, {0x20, 0x50, 0x40});
        CHECK(order.order == std::vector<size_t> {3, 1, 4, 0, 2});
        CHECK(order.tiers == std::vector<fr::TableBrickTier> {fr::TableBrickTier::Held, fr::TableBrickTier::Named,
                                                              fr::TableBrickTier::Named, fr::TableBrickTier::Other,
                                                              fr::TableBrickTier::Other});
    }

    SUBCASE("a brick loaded twice is walked at both indices") {
        const std::vector<uint64_t> twice {0x10, 0x20, 0x10};
        CHECK(fr::OrderTableBricks(twice, {0x10}, {}).order == std::vector<size_t> {0, 2, 1});
    }

    SUBCASE("empty") {
        CHECK(fr::OrderTableBricks({}, {0x10}, {0x20}).order.empty());
    }

    CHECK(std::string_view(fr::TableBrickTierName(fr::TableBrickTier::Held)) == "held the table earlier");
}

TEST_CASE("merces_table_search_done") {
    const std::vector<uint64_t> unwalked {0x30, 0x40};

    CHECK_FALSE(fr::TableSearchDone(false, unwalked, {}));
    CHECK_FALSE(fr::TableSearchDone(false, {}, {}));
    CHECK(fr::TableSearchDone(true, unwalked, {}));
    CHECK(fr::TableSearchDone(true, unwalked, {0x10, 0x20}));
    CHECK_FALSE(fr::TableSearchDone(true, unwalked, {0x10, 0x40}));
    // The one brick left held the table before: walk it too.
    CHECK_FALSE(fr::TableSearchDone(true, std::vector<uint64_t> {0x10}, {0x10}));
    CHECK_FALSE(fr::TableSearchDone(true, unwalked, {0x30}));
    CHECK(fr::TableSearchDone(true, {}, {0x30}));
}

TEST_CASE("merces_learned_entry_ids") {
    fr::PrestigeMemory memory;
    memory.bonus = 2000;
    memory.prestige = {0x1, 0x2};
    memory.normal = {0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa};

    SUBCASE("prestige and normal ids both match; others do not") {
        fr::LearnedEntryIds learned(memory);
        CHECK(learned.Size() == 10);
        CHECK(learned.Contains(0x1));
        CHECK(learned.Contains(0xa));
        CHECK_FALSE(learned.Contains(0x1c8a3cd106f999feull));
        CHECK(learned.Match(0x2));
        CHECK(learned.Match(0x3));
        CHECK_FALSE(learned.Match(0xb));
        CHECK(learned.Found() == 2);
    }

    SUBCASE("an id carried by several entities is found once") {
        fr::LearnedEntryIds learned(memory);
        CHECK(learned.Match(0x1));
        CHECK(learned.Match(0x1));
        CHECK(learned.Found() == 1);
    }

    SUBCASE("mostly found at 90 percent") {
        fr::LearnedEntryIds learned(memory);

        for (uint64_t id = 0x1; id <= 0x8; ++id)
            learned.Match(id);

        CHECK(learned.Found() == 8);
        CHECK_FALSE(learned.MostlyFound());
        CHECK_FALSE(fr::TableFound(0, learned));
        CHECK(fr::TableFound(1, learned));
        learned.Match(0x9);
        CHECK(learned.MostlyFound());
        CHECK(fr::TableFound(0, learned));
    }

    SUBCASE("an id in both sets counts once") {
        fr::PrestigeMemory both;
        both.prestige = {0x1};
        both.normal = {0x1, 0x2};
        fr::LearnedEntryIds learned(both);
        CHECK(learned.Size() == 2);
        learned.Match(0x1);
        CHECK_FALSE(learned.MostlyFound());
        learned.Match(0x2);
        CHECK(learned.MostlyFound());
    }

    SUBCASE("nothing learned never counts as found") {
        fr::LearnedEntryIds learned(fr::PrestigeMemory {});
        CHECK(learned.Size() == 0);
        CHECK_FALSE(learned.Match(0x1));
        CHECK_FALSE(learned.MostlyFound());
        CHECK_FALSE(fr::TableFound(0, learned));
    }
}

TEST_CASE("merces_learned_108_ids") {
    // A learned ini: 34 prestige and 74 normal ids.
    fr::PrestigeMemory memory;
    memory.bonus = 2000;

    for (uint64_t id = 1; id <= 34; ++id)
        memory.prestige.insert(id);

    for (uint64_t id = 35; id <= 108; ++id)
        memory.normal.insert(id);

    fr::LearnedEntryIds learned(memory);
    REQUIRE(learned.Size() == 108);

    for (uint64_t id = 1; id <= 97; ++id)
        learned.Match(id);

    CHECK_FALSE(learned.MostlyFound());
    CHECK_FALSE(fr::TableFound(0, learned));
    CHECK_FALSE(fr::LearnedInstanceTrusted(97, 108));
    learned.Match(98);
    CHECK(learned.MostlyFound());
    CHECK(fr::TableFound(0, learned));
    CHECK(fr::LearnedInstanceTrusted(98, 108));
}

TEST_CASE("merces_learned_instance_trusted") {
    // One stray match is never a write target.
    CHECK_FALSE(fr::LearnedInstanceTrusted(1, 108));
    CHECK_FALSE(fr::LearnedInstanceTrusted(0, 0));
    // At least kMinBonusEntries, however small the learned set.
    CHECK_FALSE(fr::LearnedInstanceTrusted(fr::kMinBonusEntries - 1, fr::kMinBonusEntries - 1));
    CHECK(fr::LearnedInstanceTrusted(fr::kMinBonusEntries, fr::kMinBonusEntries));
    CHECK(fr::LearnedInstanceTrusted(fr::kMinBonusEntries, 11));
    CHECK_FALSE(fr::LearnedInstanceTrusted(fr::kMinBonusEntries, 12));
}

TEST_CASE("merces_table_found_ignores_remembered_parents") {
    // A remembered parent's instance is not passed as a table-id instance:
    // only its learned children count, through the learned ids.
    fr::PrestigeMemory memory;

    for (uint64_t id = 1; id <= 20; ++id)
        memory.normal.insert(id);

    fr::LearnedEntryIds learned(memory);
    CHECK_FALSE(fr::TableFound(0, learned));

    for (uint64_t id = 1; id <= 17; ++id)
        learned.Match(id);

    CHECK_FALSE(fr::TableFound(0, learned));
    learned.Match(18);
    CHECK(fr::TableFound(0, learned));
}

TEST_CASE("merces_likely_table_brick_path") {
    CHECK(fr::IsLikelyTableBrickPath("assembly:/_pro/scenes/missions/snug/mission_vanilla.brick"));
    CHECK(fr::IsLikelyTableBrickPath("assembly:/_pro/scenes/bricks/Evergreen_Setup.brick"));
    CHECK(fr::IsLikelyTableBrickPath("assembly:/_pro/scenes/missions/paris/freelancer_x.brick"));
    CHECK(fr::IsLikelyTableBrickPath("[assembly:/_pro/scenes/missions/bangkok/mission_zika.brick].pc_entitytype"));
    CHECK(fr::IsLikelyTableBrickPath("mission_vanilla.brick"));
    // Only the file name counts: every mission brick sits under /missions/.
    CHECK_FALSE(fr::IsLikelyTableBrickPath("assembly:/_pro/scenes/missions/bangkok/tod_zika_night.brick"));
    CHECK_FALSE(fr::IsLikelyTableBrickPath(""));
}

TEST_CASE("merces_objective_from_name") {
    CHECK(fr::ObjectiveFromName("Help_Prestige_SilentAssassin101") == 101);
    CHECK(fr::ObjectiveFromName("Help_Prestige_Perfect_Run136") == 136);
    CHECK(fr::ObjectiveFromName("Entry 125 Pacify") == 125);
    CHECK(fr::ObjectiveFromName("125_PacifyBaseballBat_125") == 125); // the same number twice
    // No number, out of range, not exactly three digits, or two different numbers.
    CHECK(fr::ObjectiveFromName("Help_Prestige_BlowOpenSafe") == std::nullopt);
    CHECK(fr::ObjectiveFromName("Entry100") == std::nullopt);
    CHECK(fr::ObjectiveFromName("Entry137") == std::nullopt);
    CHECK(fr::ObjectiveFromName("Entry1011") == std::nullopt);
    CHECK(fr::ObjectiveFromName("Entry_10") == std::nullopt);
    CHECK(fr::ObjectiveFromName("Entry101_102") == std::nullopt);
    CHECK(fr::ObjectiveFromName("ValueInt_Basic_03 101") == 101);
    CHECK(fr::ObjectiveFromName("") == std::nullopt);
}

TEST_CASE("merces_agreed_objective") {
    CHECK(fr::AgreedObjective(std::array<int32_t, 0> {}) == std::nullopt);
    CHECK(fr::AgreedObjective(std::array<int32_t, 3> {101, 101, 101}) == 101);
    CHECK(fr::AgreedObjective(std::array<int32_t, 3> {0, 5000, 118}) == 118); // other numbers ignored
    CHECK(fr::AgreedObjective(std::array<int32_t, 2> {101, 102}) == std::nullopt);
    CHECK(fr::AgreedObjective(std::array<int32_t, 2> {100, 137}) == std::nullopt);
}

TEST_CASE("merces_objective_by_unique_amount") {
    // Game data: vanilla bases of a safehouse (a part of them).
    const std::array<fr::PrestigeBase, 7> bases {{
        {101, 5000},
        {116, 5500},
        {115, 6000},
        {102, 2500},
        {103, 2500},
        {113, 20},
        {101, 5000},
    }};

    CHECK(fr::ObjectiveByUniqueAmount(7000, 2000, bases) == 101); // listed twice: counts once
    CHECK(fr::ObjectiveByUniqueAmount(7500, 2000, bases) == 116);
    CHECK(fr::ObjectiveByUniqueAmount(8000, 2000, bases) == 115);
    CHECK(fr::ObjectiveByUniqueAmount(2020, 2000, bases) == 113);
    CHECK(fr::ObjectiveByUniqueAmount(4500, 2000, bases) == std::nullopt); // 102 and 103
    CHECK(fr::ObjectiveByUniqueAmount(2030, 2000, bases) == std::nullopt); // no such base
    CHECK(fr::ObjectiveByUniqueAmount(5000, 0, bases) == 101);
}
