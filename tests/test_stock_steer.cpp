#include "doctest.h"

#include "StockSteerMath.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

TEST_CASE("stock steer: rarity values as the game writes m_fA") {
    CHECK(fr::StockRarityValue(1, true) == 70.0);
    CHECK(fr::StockRarityValue(2, true) == 20.0);
    CHECK(fr::StockRarityValue(3, true) == 9.0);
    CHECK(fr::StockRarityValue(4, true) == 1.0);
    // No rarity: 100 (the Coin, the crate tier).
    CHECK(fr::StockRarityValue(0, false) == 100.0);
    CHECK(fr::StockRarityValue(2, false) == 100.0);
    CHECK(fr::StockRarityValue(9, true) == 100.0);
}

TEST_CASE("stock steer: eligibility from the owned and entitled flags") {
    // Game data, Krugermeier .22: all three unowned flags true, entitled.
    CHECK(fr::StockEligible({3, 0, true}));
    // Owned: the unowned flags read false.
    CHECK_FALSE(fr::StockEligible({0, 3, true}));
    // COMIC Concept 5 Redline: not owned, but not entitled.
    CHECK_FALSE(fr::StockEligible({3, 0, false}));
    // Disagreeing or unread flags: not eligible.
    CHECK_FALSE(fr::StockEligible({2, 1, true}));
    CHECK_FALSE(fr::StockEligible({0, 0, true}));
    CHECK_FALSE(fr::StockEligible({3, 0, std::nullopt}));
}

TEST_CASE("stock steer: supplier weights uniform among eligible guns, m_fB only lowered") {
    // Game data, a pistol slot: a common gun at cost 121, a legendary at 178,
    // a rare at 117, and an owned common at 85 (not eligible).
    const std::vector<fr::SupplierSteerOption> s_Options {
        {70.0, 121.0, true},
        {1.0, 178.0, true},
        {20.0, 117.0, true},
        {70.0, 85.0, false},
    };
    const fr::SupplierSteerPlan s_Plan = fr::PlanSupplierSteer(s_Options);

    REQUIRE(s_Plan.fB.size() == 4);
    REQUIRE(s_Plan.target.size() == 4);
    CHECK(s_Plan.eligible == 3);
    CHECK(s_Plan.weight == doctest::Approx(70.0 / 121.0));
    // The heaviest keeps its cost; the others go down.
    CHECK(s_Plan.fB[0] == doctest::Approx(121.0));
    CHECK(s_Plan.lowered == 2);

    for (size_t i = 0; i < 3; ++i) {
        CHECK(s_Plan.fB[i] <= s_Options[i].fB);
        CHECK(s_Options[i].fA / s_Plan.fB[i] == doctest::Approx(s_Plan.weight));
        CHECK(s_Plan.target[i] == doctest::Approx(s_Plan.weight));
    }

    // The owned gun is left alone.
    CHECK(s_Plan.fB[3] == 85.0);
    CHECK(s_Plan.target[3] == -1.0);
}

TEST_CASE("stock steer: supplier with fewer than two eligible guns changes nothing") {
    const std::vector<fr::SupplierSteerOption> s_One {{1.0, 178.0, true}, {70.0, 85.0, false}};
    const fr::SupplierSteerPlan s_Plan = fr::PlanSupplierSteer(s_One);
    CHECK(s_Plan.eligible == 1);
    CHECK(s_Plan.lowered == 0);
    CHECK(s_Plan.fB == std::vector<double> {178.0, 85.0});
    CHECK(s_Plan.target == std::vector<double> {-1.0, -1.0});

    const std::vector<fr::SupplierSteerOption> s_None {{70.0, 85.0, false}, {20.0, 99.0, false}};
    CHECK(fr::PlanSupplierSteer(s_None).eligible == 0);
    CHECK(fr::PlanSupplierSteer(s_None).weight == 0.0);
    CHECK(fr::PlanSupplierSteer({}).fB.empty());
}

TEST_CASE("stock steer: supplier skips unusable values") {
    const double s_NaN = std::numeric_limits<double>::quiet_NaN();
    const std::vector<fr::SupplierSteerOption> s_Options {
        {70.0, 0.0, true}, // no cost yet
        {s_NaN, 100.0, true},
        {20.0, 100.0, true},
        {9.0, 50.0, true},
    };
    const fr::SupplierSteerPlan s_Plan = fr::PlanSupplierSteer(s_Options);
    CHECK(s_Plan.eligible == 2);
    CHECK(s_Plan.weight == doctest::Approx(0.2));
    CHECK(s_Plan.fB[0] == 0.0);
    CHECK(s_Plan.target[0] == -1.0);
    CHECK(std::isnan(s_Plan.fB[1]) == false);
    CHECK(s_Plan.fB[1] == 100.0);
    CHECK(s_Plan.fB[3] == doctest::Approx(45.0));
    CHECK(s_Plan.target[2] == doctest::Approx(0.2));
}

TEST_CASE("stock steer: held weights") {
    CHECK(fr::SteeredWeightHeld(1000.0, 1000.0));
    CHECK(fr::SteeredWeightHeld(0.57851237f, 0.578512397));
    CHECK_FALSE(fr::SteeredWeightHeld(4.347826, 1000.0));
    CHECK_FALSE(fr::SteeredWeightHeld(std::numeric_limits<double>::quiet_NaN(), 1.0));

    const std::vector<fr::SteeredWeight> s_Weights {{"Krugermeier .22", 1000.0, 1000.0}, {"Bellini", 0.0, 1000.0}};
    CHECK(fr::SteeredWeightsText(s_Weights, 8) ==
          "1 of 2 held (Krugermeier .22 1000, Bellini 0 MISSED (steered 1000))");
    CHECK(fr::SteeredWeightsText(s_Weights, 1) == "1 of 2 held (Krugermeier .22 1000, +1 more)");
    CHECK(fr::SteeredWeightsText({}, 4) == "0 of 0 held");
}

TEST_CASE("stock steer: timing constants keep the steer ahead of the supplier's roll") {
    // Game data: the first supplier roll comes at 19 s of play.
    CHECK(fr::kStockSteerAt + fr::kStockSteerCheckEvery < 19.0);
    CHECK(fr::kStockSteerAt > 1.2); // owned flags settled
    CHECK(fr::kStockRestoreAfter > 0.13);
}

TEST_CASE("stock steer gear: the family is the options in no selector, less firearms and those without weight math") {
    CHECK(fr::IsGearSteerOption({false, false, true}));
    CHECK_FALSE(fr::IsGearSteerOption({true, false, true}));   // a gun slot's (or a crate's) option
    CHECK_FALSE(fr::IsGearSteerOption({false, true, true}));   // a loose pistol (blocks B, D)
    CHECK_FALSE(fr::IsGearSteerOption({false, false, false})); // no m_fA / m_fB found
}

TEST_CASE("stock steer gear: every eligible item of the family weighs the same, so each block is uniform") {
    // A family across blocks: explosives / poisons (A), melee (C, E), rarities 70 / 20 / 9 / 1 / none (100).
    const std::vector<fr::SupplierSteerOption> s_Family {
        {70.0, 50.0, true},   // block A, common, eligible
        {20.0, 300.0, true},  // block A, rare, eligible
        {100.0, 25.0, false}, // block A, owned: untouched
        {9.0, 900.0, true},   // block C, epic melee, eligible
        {1.0, 2000.0, true},  // block C, legendary melee, eligible
        {70.0, 40.0, false},  // block E, not entitled: untouched
        {100.0, 10.0, true},  // block E, no rarity, eligible: the heaviest (10)
    };
    const fr::SupplierSteerPlan s_Plan = fr::PlanSupplierSteer(s_Family);
    CHECK(s_Plan.eligible == 5);
    CHECK(s_Plan.weight == doctest::Approx(10.0));
    CHECK(s_Plan.lowered == 4);

    for (size_t i = 0; i < s_Family.size(); ++i) {
        CAPTURE(i);

        if (!s_Family[i].eligible) {
            CHECK(s_Plan.fB[i] == s_Family[i].fB);
            CHECK(s_Plan.target[i] == -1.0);
            continue;
        }

        CHECK(s_Plan.fB[i] <= s_Family[i].fB);
        CHECK(s_Family[i].fA / s_Plan.fB[i] == doctest::Approx(10.0));
        CHECK(s_Plan.target[i] == doctest::Approx(10.0));
    }

    // Whichever block a real selector draws (here block C's two), its eligible items share one weight.
    CHECK(s_Family[3].fA / s_Plan.fB[3] == doctest::Approx(s_Family[4].fA / s_Plan.fB[4]));
}

TEST_CASE("stock steer gear: offers before the steer do not move the restore") {
    fr::GearRestoreClock s_Clock;
    CHECK_FALSE(s_Clock.Offer(0.5));
    CHECK_FALSE(s_Clock.RestoreAt());

    CHECK_FALSE(s_Clock.Steered(2.0));
    CHECK_FALSE(s_Clock.Due(2.5));

    SUBCASE("an offer after the steer counts") {
        CHECK(s_Clock.Offer(3.0));
        CHECK(*s_Clock.RestoreAt() == doctest::Approx(4.0));
    }

    SUBCASE("a visit after the steer counts") {
        CHECK(s_Clock.Visit(3.0));
        CHECK(*s_Clock.RestoreAt() == doctest::Approx(5.0));
    }

    SUBCASE("reset forgets the steer") {
        s_Clock.Reset();
        CHECK_FALSE(s_Clock.Offer(0.5));
        CHECK_FALSE(s_Clock.RestoreAt());
    }
}

TEST_CASE("stock steer gear: a SupplierVisited before the steer makes the restore due at the steer") {
    fr::GearRestoreClock s_Clock;
    CHECK_FALSE(s_Clock.Visit(1.0));
    CHECK_FALSE(s_Clock.RestoreAt());

    SUBCASE("due at once") {
        CHECK(s_Clock.Steered(2.0));
        CHECK(*s_Clock.RestoreAt() == doctest::Approx(2.0));
        CHECK(s_Clock.Due(2.0));
        CHECK_FALSE(s_Clock.Offer(2.5)); // later offers do not move it
    }

    SUBCASE("reset forgets the visit") {
        s_Clock.Reset();
        CHECK_FALSE(s_Clock.Steered(2.0));
        CHECK_FALSE(s_Clock.RestoreAt());
    }
}

TEST_CASE("stock steer gear: restored 1 s after the first loose offer or 2 s after SupplierVisited, the earlier") {
    fr::GearRestoreClock s_Clock;
    s_Clock.Steered(0.0);
    CHECK_FALSE(s_Clock.RestoreAt());
    CHECK_FALSE(s_Clock.Due(1000.0));

    SUBCASE("SupplierVisited first, offers in the same frames: the offer's moment is earlier") {
        CHECK(s_Clock.Visit(100.0));
        CHECK(*s_Clock.RestoreAt() == doctest::Approx(102.0));
        CHECK(s_Clock.Offer(100.05));
        CHECK(*s_Clock.RestoreAt() == doctest::Approx(101.05));
        CHECK_FALSE(s_Clock.Offer(100.2)); // later offers do not move it
        CHECK_FALSE(s_Clock.Due(101.0));
        CHECK(s_Clock.Due(101.06));
    }

    SUBCASE("no offer seen: 2 s after SupplierVisited") {
        CHECK(s_Clock.Visit(50.0));
        CHECK_FALSE(s_Clock.Due(51.9));
        CHECK(s_Clock.Due(52.0));
    }

    SUBCASE("an offer long before a later visit keeps the earlier moment") {
        CHECK(s_Clock.Offer(10.0));
        CHECK_FALSE(s_Clock.Visit(20.0));
        CHECK(*s_Clock.RestoreAt() == doctest::Approx(11.0));
    }

    SUBCASE("reset; a NaN moment is ignored") {
        s_Clock.Offer(10.0);
        s_Clock.Reset();
        CHECK_FALSE(s_Clock.RestoreAt());
        s_Clock.Steered(0.0);
        CHECK_FALSE(s_Clock.Visit(std::numeric_limits<double>::quiet_NaN()));
        CHECK_FALSE(s_Clock.RestoreAt());
    }
}
