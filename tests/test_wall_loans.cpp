#include "doctest.h"

#include "WallLoans.h"

#include <string>
#include <vector>

namespace {

fr::Guid LoanId(uint32_t n) {
    fr::Guid id;
    id.data1 = 0x30000000u + n;
    id.data2 = static_cast<uint16_t>(n);
    id.data4[7] = static_cast<uint8_t>(n);
    return id;
}

fr::LoadoutState State(std::vector<fr::Guid> wall, std::vector<fr::Guid> inventory, int64_t updated = 100) {
    fr::LoadoutState state;
    state.lists["PersistentItems"] = std::move(wall);
    state.lists["InventoryItems"] = std::move(inventory);
    state.updatedUnix = updated;
    return state;
}

const fr::Guid kBoombox = LoanId(1);
const fr::Guid kShashka = LoanId(2);
const fr::Guid kOther = LoanId(3);

} // namespace

TEST_CASE("wall loans: only Freelancer tools are removed from their list once 47 holds them") {
    CHECK(fr::RemovesFromSource(fr::LoadoutSource::Transient));
    CHECK_FALSE(fr::RemovesFromSource(fr::LoadoutSource::Persistent));
    // A spawned safehouse pickup is in no list: nothing to remove (nor lend).
    CHECK_FALSE(fr::RemovesFromSource(fr::LoadoutSource::Spawned));
}

TEST_CASE("wall loans: an item is lent once") {
    fr::WallLoans loans;
    CHECK(loans.Add(kBoombox, "Beatdown Boombox"));
    CHECK_FALSE(loans.Add(kBoombox, "again"));
    REQUIRE(loans.loans.size() == 1);
    CHECK(loans.Find(kBoombox)->name == "Beatdown Boombox");
    CHECK(loans.Find(kShashka) == nullptr);
}

TEST_CASE("wall loans: where a lent item is") {
    CHECK(fr::JudgeLoan(State({kOther, kBoombox}, {}), kBoombox) == fr::LoanStatus::OnWall);
    CHECK(fr::JudgeLoan(State({kOther}, {kBoombox}), kBoombox) == fr::LoanStatus::InLoadout);
    // On the wall wins over the loadout (never doubled by the game, but the wall is what counts).
    CHECK(fr::JudgeLoan(State({kBoombox}, {kBoombox}), kBoombox) == fr::LoanStatus::OnWall);
    CHECK(fr::JudgeLoan(State({kOther}, {}), kBoombox) == fr::LoanStatus::Missing);
    // No wall list, or a wiped wall: never refilled.
    fr::LoadoutState noWall;
    noWall.lists["InventoryItems"] = {};
    CHECK(fr::JudgeLoan(noWall, kBoombox) == fr::LoanStatus::Unknown);
    CHECK(fr::JudgeLoan(State({}, {}), kBoombox) == fr::LoanStatus::Unknown);
}

TEST_CASE("wall loans: a boombox dropped, not carried out, never removed: back on the wall") {
    fr::WallLoans loans;
    loans.Add(kBoombox, "(Pomada) Boombox Distraction");
    loans.Add(kShashka, "(AR) Shashka A33");

    // The safehouse after the mission: the boombox stayed on the wall, the
    // Shashka was carried out (the game moved it into the loadout).
    const auto outcomes = fr::ReviewLoans(loans, State({kOther, kBoombox}, {kShashka}), std::nullopt);
    REQUIRE(outcomes.size() == 2);
    CHECK(outcomes[0].status == fr::LoanStatus::OnWall);
    CHECK(outcomes[0].closed);
    CHECK_FALSE(outcomes[0].repair);
    CHECK(outcomes[1].status == fr::LoanStatus::InLoadout);
    CHECK_FALSE(outcomes[1].closed);
    REQUIRE(loans.loans.size() == 1);
    CHECK(loans.loans[0].id == kShashka);

    // Later hung on the wall in the safehouse: the next departure closes it.
    const auto later = fr::ReviewLoans(loans, State({kOther, kShashka}, {}, 200), int64_t {200});
    REQUIRE(later.size() == 1);
    CHECK(later[0].closed);
    CHECK(loans.loans.empty());
}

TEST_CASE("wall loans: a safehouse load never repairs; a mission load pushes a missing item back") {
    fr::WallLoans loans;
    loans.Add(kBoombox, "Boombox");

    const auto safehouse = fr::ReviewLoans(loans, State({kOther}, {}), std::nullopt);
    REQUIRE(safehouse.size() == 1);
    CHECK(safehouse[0].status == fr::LoanStatus::Missing);
    CHECK_FALSE(safehouse[0].repair);
    CHECK_FALSE(safehouse[0].closed);
    CHECK(loans.loans[0].repairs == 0);

    const auto mission = fr::ReviewLoans(loans, State({kOther}, {}, 500), int64_t {500});
    REQUIRE(mission.size() == 1);
    CHECK(mission[0].repair);
    CHECK_FALSE(mission[0].closed);
    CHECK(loans.loans[0].repairs == 1);
    CHECK(loans.loans[0].lastRepairDeparture == 500);
    CHECK(mission[0].loan.repairs == 1);

    // A restart (same departure) pushes again without counting.
    const auto restart = fr::ReviewLoans(loans, State({kOther}, {}, 500), int64_t {500});
    CHECK(restart[0].repair);
    CHECK(loans.loans[0].repairs == 1);

    // The push reached the save: the next load closes the loan.
    const auto saved = fr::ReviewLoans(loans, State({kOther, kBoombox}, {}, 600), int64_t {600});
    CHECK(saved[0].closed);
    CHECK_FALSE(saved[0].repair);
    CHECK(loans.loans.empty());
}

TEST_CASE("wall loans: an item in the loadout at departure is not pushed (no double)") {
    fr::WallLoans loans;
    loans.Add(kShashka, "Shashka");
    const auto outcomes = fr::ReviewLoans(loans, State({kOther}, {kShashka}, 700), int64_t {700});
    CHECK(outcomes[0].status == fr::LoanStatus::InLoadout);
    CHECK_FALSE(outcomes[0].repair);
    CHECK(loans.loans.size() == 1);

    // Lost later as a packed item: missing, then pushed back.
    const auto lost = fr::ReviewLoans(loans, State({kOther}, {}, 800), int64_t {800});
    CHECK(lost[0].repair);
}

TEST_CASE("wall loans: given up after kMaxLoanRepairs departures") {
    fr::WallLoans loans;
    loans.Add(kBoombox, "Boombox");

    for (int32_t i = 1; i <= fr::kMaxLoanRepairs; ++i) {
        const auto outcomes = fr::ReviewLoans(loans, State({kOther}, {}, 1000 + i), int64_t {1000 + i});
        REQUIRE(outcomes.size() == 1);
        CHECK(outcomes[0].repair);
        CHECK(loans.loans[0].repairs == i);
    }

    const auto last = fr::ReviewLoans(loans, State({kOther}, {}, 2000), int64_t {2000});
    REQUIRE(last.size() == 1);
    CHECK(last[0].gaveUp);
    CHECK(last[0].closed);
    CHECK_FALSE(last[0].repair);
    CHECK(loans.loans.empty());
    CHECK(fr::LoanOutcomeText(last[0]).find("giving up") != std::string::npos);
}

TEST_CASE("wall loans: an unknown wall keeps the loan and repairs nothing") {
    fr::WallLoans loans;
    loans.Add(kBoombox, "Boombox");
    const auto outcomes = fr::ReviewLoans(loans, State({}, {}, 5), int64_t {5});
    CHECK(outcomes[0].status == fr::LoanStatus::Unknown);
    CHECK_FALSE(outcomes[0].repair);
    CHECK_FALSE(outcomes[0].closed);
    CHECK(loans.loans[0].repairs == 0);
}

TEST_CASE("wall loans: the state file lines round-trip") {
    fr::WallLoans loans;
    loans.Add(kBoombox, "(Pomada) Boombox Distraction, the loud one");
    loans.Add(kShashka, "");
    loans.loans[0].repairs = 2;
    loans.loans[0].lastRepairDeparture = 1794103222;

    const std::string text = "freelancerrandomizer loadout state 1\nlive.updated=5\n" + fr::FormatWallLoans(loans) +
                             "loan=not-a-guid,1,2,x\nloan=" + fr::FormatGuid(kOther) + ",oops,2,x\n";
    const fr::WallLoans parsed = fr::ParseWallLoans(text);
    REQUIRE(parsed.loans.size() == 2);
    CHECK(parsed.loans[0].id == kBoombox);
    CHECK(parsed.loans[0].name == "(Pomada) Boombox Distraction, the loud one");
    CHECK(parsed.loans[0].repairs == 2);
    CHECK(parsed.loans[0].lastRepairDeparture == 1794103222);
    CHECK(parsed.loans[1].id == kShashka);
    CHECK(parsed.loans[1].name.empty());
    CHECK(fr::ParseWallLoans("").loans.empty());
}

TEST_CASE("wall loans: player-facing lines are plain") {
    fr::WallLoans loans;
    loans.Add(kBoombox, "Beatdown Boombox");
    auto outcomes = fr::ReviewLoans(loans, State({kBoombox}, {}), std::nullopt);
    CHECK(fr::LoanOutcomeText(outcomes[0]).starts_with("loadout: \"Beatdown Boombox\" ("));
    CHECK(fr::LoanOutcomeText(outcomes[0]).find("back on your gear wall") != std::string::npos);

    loans.Add(kBoombox, "Beatdown Boombox");
    outcomes = fr::ReviewLoans(loans, State({kOther}, {}, 9), int64_t {9});
    CHECK(fr::LoanOutcomeText(outcomes[0]).find("returned to your gear wall") != std::string::npos);
}

TEST_CASE("wall return: a lent item 47 may carry out is returned at the exit") {
    fr::WallReturnInput input;
    input.delivered = true;
    input.wall = 1;
    input.heroCopies = 1;
    CHECK(fr::PlanWallReturn(input) == fr::WallReturnPlan::Return);

    // Dropped, thrown or in a briefcase (no item event): still returned (a round never doubles it).
    input.heroCopies = 0;
    CHECK(fr::PlanWallReturn(input) == fr::WallReturnPlan::Return);
    CHECK(fr::WallReturnPlanText(fr::WallReturnPlan::Return).find("returned to your gear wall") != std::string::npos);
}

TEST_CASE("wall return: the skips that keep every copy safe") {
    fr::WallReturnInput input;
    input.wall = 1;
    input.heroCopies = 1;
    CHECK(fr::PlanWallReturn(input) == fr::WallReturnPlan::NotDelivered);

    input.delivered = true;
    input.packed = 1;
    CHECK(fr::PlanWallReturn(input) == fr::WallReturnPlan::Packed);

    input.packed = 0;
    input.wall = 0;
    CHECK(fr::PlanWallReturn(input) == fr::WallReturnPlan::NotOnWall);

    // Two on the wall, one lent: a round would leave one, so nothing is sent.
    input.wall = 2;
    CHECK(fr::PlanWallReturn(input) == fr::WallReturnPlan::WallCopies);

    // 47 picked up another copy: the remove could take his new one.
    input.wall = 1;
    input.heroCopies = 2;
    CHECK(fr::PlanWallReturn(input) == fr::WallReturnPlan::ExtraCopy);
    CHECK(fr::WallReturnPlanText(fr::WallReturnPlan::ExtraCopy).starts_with("not returned"));
}

TEST_CASE("wall return: the mission's save judged against the departure") {
    // Game data, without the return: in the loadout, off the wall.
    CHECK(fr::JudgeWallReturn(1, 0, size_t {0}, size_t {1}) == fr::WallReturnVerdict::InLoadout);
    // Returned.
    CHECK(fr::JudgeWallReturn(1, 0, size_t {1}, size_t {0}) == fr::WallReturnVerdict::Back);
    // Off both lists.
    CHECK(fr::JudgeWallReturn(1, 0, size_t {0}, size_t {0}) == fr::WallReturnVerdict::Missing);
    // Doubled: two on the wall, or on the wall and in the loadout.
    CHECK(fr::JudgeWallReturn(1, 0, size_t {2}, size_t {0}) == fr::WallReturnVerdict::Doubled);
    CHECK(fr::JudgeWallReturn(1, 0, size_t {1}, size_t {1}) == fr::WallReturnVerdict::Doubled);
    // A packed copy stays packed: only a new one counts.
    CHECK(fr::JudgeWallReturn(1, 1, size_t {1}, size_t {1}) == fr::WallReturnVerdict::Back);
    // A list missing from the save.
    CHECK(fr::JudgeWallReturn(1, 0, std::nullopt, size_t {0}) == fr::WallReturnVerdict::Partial);
    CHECK(fr::JudgeWallReturn(1, 0, size_t {1}, std::nullopt) == fr::WallReturnVerdict::Partial);
    CHECK(std::string(fr::WallReturnVerdictText(fr::WallReturnVerdict::Back)).starts_with("OK"));
}

TEST_CASE("wall return: copies counted in a list") {
    const std::vector<fr::Guid> list {kBoombox, kOther, kBoombox};
    CHECK(fr::CountOf(&list, kBoombox) == 2);
    CHECK(fr::CountOf(&list, kShashka) == 0);
    CHECK(fr::CountOf(nullptr, kBoombox) == 0);
}
