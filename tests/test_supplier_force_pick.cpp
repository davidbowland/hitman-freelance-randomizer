#include "doctest.h"

#include "SupplierForcePick.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

// The slots JudgeForcePick would force, in order.
std::vector<size_t> SlotsToForce(const std::vector<fr::ForcePickSlot>& slots) {
    std::vector<size_t> out;

    for (size_t i = 0; i < slots.size(); ++i) {
        if (fr::JudgeForcePick(slots[i]) == fr::ForcePickVerdict::Force)
            out.push_back(i);
    }

    return out;
}

} // namespace

TEST_CASE("force pick: only a silent slot with eligible guns that never picked is forced") {
    // Game data, Mumbai: #8, #7, #4, #5 sent OnPickFailed twice (no eligible
    // gun), #6 (pistol, 6 eligible) sent nothing.
    const std::vector<fr::ForcePickSlot> s_Slots {
        {0, 2, false, 0, false}, // #4 assault rifle
        {0, 2, false, 0, false}, // #5 sniper
        {0, 0, false, 6, false}, // #6 pistol: silent
        {0, 2, false, 0, false}, // #7 SMG
        {0, 2, false, 0, false}, // #8 shotgun
    };
    CHECK(SlotsToForce(s_Slots) == std::vector<size_t> {2});
}

TEST_CASE("force pick: verdicts") {
    // Answered in this burst (either signal): never forced.
    CHECK(fr::JudgeForcePick({1, 0, false, 6, false}) == fr::ForcePickVerdict::Signaled);
    CHECK(fr::JudgeForcePick({0, 1, false, 6, false}) == fr::ForcePickVerdict::Signaled);
    // Picked in an earlier visit: never forced again.
    CHECK(fr::JudgeForcePick({0, 0, true, 6, false}) == fr::ForcePickVerdict::PickedBefore);
    // Nothing eligible: silent is as good as failed.
    CHECK(fr::JudgeForcePick({0, 0, false, 0, false}) == fr::ForcePickVerdict::NoEligible);
    // Once per burst.
    CHECK(fr::JudgeForcePick({0, 0, false, 6, true}) == fr::ForcePickVerdict::AlreadyForced);
    // One eligible gun is enough.
    CHECK(fr::JudgeForcePick({0, 0, false, 1, false}) == fr::ForcePickVerdict::Force);
    CHECK(SlotsToForce({}).empty());
}

TEST_CASE("force pick: never when the supplier filled its stands with other items") {
    // Game data, Ambrose Island: the pistol selector (2 eligible) stayed
    // silent while (COMIC) Knife (block E) and (COMIC) Sword (block C) were
    // offered; the forced Pick put the Hero 50AE on a stand that held one.
    const std::vector<fr::ForcePickSlot> s_Slots {
        {0, 3, false, 0, false, 2}, // #0 assault rifle
        {0, 3, false, 0, false, 2}, // #1 sniper
        {0, 0, false, 2, false, 2}, // #2 pistol: silent
        {0, 3, false, 0, false, 2}, // #3 SMG
        {1, 0, false, 1, false, 2}, // #4 shotgun
    };
    CHECK(SlotsToForce(s_Slots).empty());
    CHECK(fr::JudgeForcePick(s_Slots[2]) == fr::ForcePickVerdict::StandFilled);
    // The other verdicts come first: StandFilled is only for a slot that would be forced.
    CHECK(fr::JudgeForcePick({0, 3, false, 0, false, 2}) == fr::ForcePickVerdict::Signaled);
    CHECK(fr::JudgeForcePick({0, 0, false, 0, false, 2}) == fr::ForcePickVerdict::NoEligible);
    CHECK(fr::JudgeForcePick({0, 0, true, 2, false, 2}) == fr::ForcePickVerdict::PickedBefore);
    // Mumbai (no alternative stand offered): still forced.
    CHECK(fr::JudgeForcePick({0, 0, false, 6, false, 0}) == fr::ForcePickVerdict::Force);
    CHECK(std::string_view(fr::ForcePickVerdictText(fr::ForcePickVerdict::StandFilled)).find("left alone") !=
          std::string_view::npos);
}

TEST_CASE("force pick: the alternative stands are blocks B to E") {
    CHECK_FALSE(fr::IsAlternativeStandBlock('A')); // gear, every visit
    CHECK(fr::IsAlternativeStandBlock('B'));       // tools / pistols
    CHECK(fr::IsAlternativeStandBlock('C'));       // melee
    CHECK(fr::IsAlternativeStandBlock('D'));       // tools / pistols
    CHECK(fr::IsAlternativeStandBlock('E'));       // melee
    CHECK_FALSE(fr::IsAlternativeStandBlock('F')); // gear (a copy of A)
    CHECK(fr::IsAlternativeStandBlock('?'));       // an unknown layout: never force there
    CHECK_FALSE(fr::IsAlternativeStandBlock('\0'));
}

TEST_CASE("force pick: the burst is judged late enough for the stands' offers") {
    // Ambrose Island: the alternative stands' offers came 0.23-0.26 s after
    // the burst's signals; the old 0.3 s judged 0.05 s after the last one.
    CHECK(fr::kForcePickQuietSeconds >= 0.8);
    fr::GunPickBurst s_Burst;
    s_Burst.OnSignal(65.84);
    CHECK_FALSE(s_Burst.Due(66.15));
    CHECK(s_Burst.Due(65.84 + fr::kForcePickQuietSeconds));
}

TEST_CASE("force pick: burst of pick signals") {
    fr::GunPickBurst s_Burst(0.3);
    CHECK_FALSE(s_Burst.Open());
    CHECK_FALSE(s_Burst.Due(100.0));

    // The five slots within one frame of play time (486.11 s), then quiet.
    CHECK(s_Burst.OnSignal(486.11));
    CHECK_FALSE(s_Burst.OnSignal(486.11));
    CHECK_FALSE(s_Burst.OnSignal(486.13));
    CHECK(s_Burst.Open());
    CHECK(s_Burst.Visits() == 1);
    CHECK(s_Burst.First() == doctest::Approx(486.11));
    CHECK(s_Burst.Last() == doctest::Approx(486.13));
    CHECK_FALSE(s_Burst.Due(486.40));
    CHECK(s_Burst.Due(486.43));

    // A late signal pushes the end out; an out-of-order one does not pull it back.
    CHECK_FALSE(s_Burst.OnSignal(486.42));
    CHECK_FALSE(s_Burst.OnSignal(486.20));
    CHECK(s_Burst.Last() == doctest::Approx(486.42));
    CHECK_FALSE(s_Burst.Due(486.60));
    CHECK(s_Burst.Due(486.72));

    s_Burst.Close();
    CHECK_FALSE(s_Burst.Open());
    CHECK_FALSE(s_Burst.Due(500.0));

    // A second visit opens a new burst.
    CHECK(s_Burst.OnSignal(600.0));
    CHECK(s_Burst.Visits() == 2);
    CHECK(s_Burst.First() == doctest::Approx(600.0));
    CHECK(s_Burst.Last() == doctest::Approx(600.0));
}

TEST_CASE("force pick: outcome of the forced Pick") {
    CHECK(fr::ForcePickOutcomeOf(1, 0) == fr::ForcePickOutcome::Picked);
    CHECK(fr::ForcePickOutcomeOf(1, 2) == fr::ForcePickOutcome::Picked);
    CHECK(fr::ForcePickOutcomeOf(0, 2) == fr::ForcePickOutcome::Failed);
    CHECK(fr::ForcePickOutcomeOf(0, 0) == fr::ForcePickOutcome::Swallowed);
    CHECK(std::string_view(fr::ForcePickOutcomeText(fr::ForcePickOutcome::Swallowed)).starts_with("SWALLOWED"));
}
