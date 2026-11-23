#include "doctest.h"

#include "FishingScript.h"
#include "Guid.h"

#include <array>
#include <optional>
#include <vector>

namespace {

using fr::FishCatch;

// A plausible layout: fish on two spawners, line, saber, two others.
const std::vector<FishCatch> kItems = {FishCatch::Fish,        FishCatch::Other, FishCatch::FishingLine,
                                       FishCatch::PirateSaber, FishCatch::Fish,  FishCatch::Other};

std::vector<std::optional<float>> Weights(float p_Each = 1.0f) {
    return std::vector<std::optional<float>>(kItems.size(), p_Each);
}

} // namespace

TEST_CASE("fishing script: the scripted catches are fish, line, saber, then vanilla") {
    CHECK(fr::ScriptedCatch(1) == FishCatch::Fish);
    CHECK(fr::ScriptedCatch(2) == FishCatch::FishingLine);
    CHECK(fr::ScriptedCatch(3) == FishCatch::PirateSaber);
    CHECK_FALSE(fr::ScriptedCatch(4));
    CHECK_FALSE(fr::ScriptedCatch(0));
}

TEST_CASE("fishing script: items are classified by repository id, saber key or title") {
    const auto s_Fish = fr::ParseGuid(fr::kFishRepositoryId);
    const auto s_Line = fr::ParseGuid(fr::kFishingLineRepositoryId);
    const auto s_Coin = fr::ParseGuid("dda002e9-02b1-4208-82a5-cf059f3c79cf");
    CHECK(fr::ClassifyFishItem(s_Fish, 0, "") == FishCatch::Fish);
    CHECK(fr::ClassifyFishItem(s_Line, 0, "") == FishCatch::FishingLine);
    CHECK(fr::ClassifyFishItem(s_Coin, fr::kPirateSaberKeyId, "") == FishCatch::PirateSaber);
    CHECK(fr::ClassifyFishItem(s_Coin, 0, "Jarl's PIRATE Saber") == FishCatch::PirateSaber);
    CHECK(fr::ClassifyFishItem(s_Coin, 0, "Sabre") == FishCatch::Other);
    CHECK(fr::ClassifyFishItem(std::nullopt, 0, "") == FishCatch::Other);
}

TEST_CASE("fishing script: the plan leaves only the wanted item weighted") {
    std::vector<std::optional<float>> s_Weights = {2.0f, 1.0f, 0.5f, 0.0f, 3.0f, 1.0f};

    const auto s_Fish = fr::PlanFishingSteer(kItems, s_Weights, FishCatch::Fish);
    REQUIRE(s_Fish.steer);
    CHECK(s_Fish.targets == 2);
    CHECK(s_Fish.weights == std::vector<float> {2.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f});

    // A wanted choice whose weight is 0 gets 1, so it can be drawn at all.
    const auto s_Saber = fr::PlanFishingSteer(kItems, s_Weights, FishCatch::PirateSaber);
    REQUIRE(s_Saber.steer);
    CHECK(s_Saber.weights == std::vector<float> {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f});
}

TEST_CASE("fishing script: no plan without the item or with an unreadable weight") {
    const std::vector<FishCatch> s_NoSaber(6, FishCatch::Fish);
    CHECK_FALSE(fr::PlanFishingSteer(s_NoSaber, Weights(), FishCatch::PirateSaber).steer);

    auto s_Weights = Weights();
    s_Weights[4].reset();
    CHECK_FALSE(fr::PlanFishingSteer(kItems, s_Weights, FishCatch::Fish).steer);

    CHECK_FALSE(fr::PlanFishingSteer(kItems, std::vector<std::optional<float>>(3, 1.0f), FishCatch::Fish).steer);
}

TEST_CASE("fishing script: three steered catches, then vanilla, counted from the visit's start") {
    fr::FishingRolls s_Rolls(true, 0, kItems);
    const auto s_W = Weights();

    const auto s_First = s_Rolls.Start(s_W, 1.0);
    REQUIRE(s_First.opened);
    CHECK(s_First.steer);
    CHECK(s_First.number == 1);
    CHECK(s_First.wanted == FishCatch::Fish);
    // A second Start for the same roll (the Pick input after the timer's Out) does nothing.
    CHECK_FALSE(s_Rolls.Start(s_W, 1.0).opened);
    s_Rolls.Picked(4);
    const auto s_End1 = s_Rolls.Done(1.0);
    CHECK(s_End1.restore);
    CHECK(s_End1.counted);
    CHECK(s_End1.catches == 1);
    CHECK(s_End1.verdict.find("as planned") != std::string::npos);

    REQUIRE(s_Rolls.Start(s_W, 5.0).wanted == FishCatch::FishingLine);
    s_Rolls.Picked(2);
    CHECK(s_Rolls.Done(5.0).catches == 2);

    REQUIRE(s_Rolls.Start(s_W, 9.0).wanted == FishCatch::PirateSaber);
    s_Rolls.Picked(3);
    CHECK(s_Rolls.Done(9.0).catches == 3);

    const auto s_Fourth = s_Rolls.Start(s_W, 12.0);
    CHECK(s_Fourth.opened);
    CHECK_FALSE(s_Fourth.steer);
    CHECK_FALSE(s_Fourth.wanted);
    s_Rolls.Picked(1);
    const auto s_End4 = s_Rolls.Done(12.0);
    CHECK_FALSE(s_End4.restore);
    CHECK(s_End4.counted);
    CHECK(s_End4.catches == 4);
    CHECK(s_Rolls.AsPlanned() == 3);
    CHECK(s_Rolls.Steered() == 3);
}

TEST_CASE("fishing script: rolls that start after earlier catches go on from there") {
    fr::FishingRolls s_Rolls(true, 2, kItems);
    const auto s_Start = s_Rolls.Start(Weights(), 0.5);
    CHECK(s_Start.number == 3);
    CHECK(s_Start.wanted == FishCatch::PirateSaber);
}

TEST_CASE("fishing script: an undrawable saber falls back to one vanilla re-pick, and the catch counts") {
    fr::FishingRolls s_Rolls(true, 2, kItems);
    REQUIRE(s_Rolls.Start(Weights(), 3.0).steer);
    const auto s_Failed = s_Rolls.Failed(3.0);
    CHECK(s_Failed.restore);
    CHECK(s_Failed.retry);
    CHECK_FALSE(s_Failed.counted);
    CHECK(s_Failed.catches == 2);

    REQUIRE(s_Rolls.TakeRetry());
    CHECK_FALSE(s_Rolls.TakeRetry());
    const auto s_Retry = s_Rolls.Start(Weights(), 3.1);
    CHECK(s_Retry.opened);
    CHECK_FALSE(s_Retry.steer);
    CHECK(s_Retry.number == 3);
    s_Rolls.Picked(0);
    const auto s_Done = s_Rolls.Done(3.1);
    CHECK_FALSE(s_Done.restore);
    CHECK(s_Done.counted);
    CHECK(s_Done.catches == 3);
    CHECK(s_Done.verdict.find("vanilla re-pick") != std::string::npos);

    // Past the script now.
    CHECK_FALSE(s_Rolls.Start(Weights(), 8.0).steer);
}

TEST_CASE("fishing script: a failed vanilla re-pick is not retried again") {
    fr::FishingRolls s_Rolls(true, 0, kItems);
    REQUIRE(s_Rolls.Start(Weights(), 1.0).steer);
    REQUIRE(s_Rolls.Failed(1.0).retry);
    REQUIRE(s_Rolls.TakeRetry());
    REQUIRE(s_Rolls.Start(Weights(), 1.1).opened);
    const auto s_End = s_Rolls.Failed(1.1);
    CHECK_FALSE(s_End.retry);
    CHECK_FALSE(s_End.restore);
    CHECK_FALSE(s_Rolls.TakeRetry());
    CHECK(s_Rolls.Catches() == 0);
}

TEST_CASE("fishing script: a different catch than planned is reported as a miss") {
    fr::FishingRolls s_Rolls(true, 0, kItems);
    REQUIRE(s_Rolls.Start(Weights(), 1.0).steer);
    s_Rolls.Picked(2);
    const auto s_End = s_Rolls.Done(1.0);
    CHECK(s_End.verdict.find("NOT the planned fish") != std::string::npos);
    CHECK(s_Rolls.Missed() == 1);
    CHECK(s_End.catches == 1);
}

TEST_CASE("fishing script: off, it only records and never counts") {
    fr::FishingRolls s_Rolls(false, 0, kItems);
    const auto s_Start = s_Rolls.Start(Weights(), 1.0);
    CHECK(s_Start.opened);
    CHECK_FALSE(s_Start.steer);
    CHECK_FALSE(s_Start.wanted);
    s_Rolls.Picked(3);
    const auto s_End = s_Rolls.Done(1.0);
    CHECK_FALSE(s_End.counted);
    CHECK(s_End.catches == 0);
    CHECK(s_End.verdict == "vanilla: caught pirate saber");
}

TEST_CASE("fishing script: an unanswered roll is closed and restored after the timeout") {
    fr::FishingRolls s_Rolls(true, 0, kItems);
    REQUIRE(s_Rolls.Start(Weights(), 10.0).steer);
    CHECK_FALSE(s_Rolls.Tick(11.0).closed);
    const auto s_End = s_Rolls.Tick(10.0 + fr::kFishingRollTimeout);
    CHECK(s_End.closed);
    CHECK(s_End.restore);
    CHECK_FALSE(s_End.counted);
    CHECK_FALSE(s_Rolls.Open());
    // The same catch number is planned again.
    CHECK(s_Rolls.Start(Weights(), 20.0).number == 1);
}

TEST_CASE("fishing script: an unplannable roll stays vanilla but still counts as the scripted catch") {
    const std::vector<FishCatch> s_NoFish(6, FishCatch::Other);
    fr::FishingRolls s_Rolls(true, 0, s_NoFish);
    const auto s_Start = s_Rolls.Start(Weights(), 1.0);
    CHECK_FALSE(s_Start.steer);
    CHECK(s_Start.wanted == FishCatch::Fish);
    s_Rolls.Picked(0);
    const auto s_End = s_Rolls.Done(1.0);
    CHECK(s_End.counted);
    CHECK(s_End.verdict.find("not steered") != std::string::npos);
}
