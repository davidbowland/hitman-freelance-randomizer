#include "doctest.h"

#include "SafeSpots.h"

#include <limits>
#include <string>
#include <vector>

TEST_CASE("safe spots: a spot is picked when any of its values says so") {
    // An untouched spot: route 1 out, both values 0.
    CHECK(fr::ActivityOf({1, 0, 0}) == fr::SafeActivity::Idle);
    // The picked spot at 5 s of play.
    CHECK(fr::ActivityOf({5, 3, 3}) == fr::SafeActivity::Active);
    // Opened: the state value drops back, the route stays at 5.
    CHECK(fr::ActivityOf({5, 0, 3}) == fr::SafeActivity::Active);
    // Scene playing, before the route changed in some logs.
    CHECK(fr::ActivityOf({1, 1, 1}) == fr::SafeActivity::Active);
    // Partly read: what was read decides.
    CHECK(fr::ActivityOf({std::nullopt, 0, std::nullopt}) == fr::SafeActivity::Idle);
    CHECK(fr::ActivityOf({5, std::nullopt, std::nullopt}) == fr::SafeActivity::Active);
    CHECK(fr::ActivityOf({}) == fr::SafeActivity::Unknown);

    CHECK(fr::SignalsText({5, std::nullopt, 3}) == "route outs 5, state ?, count 3");
}

TEST_CASE("safe spots: one line per spot") {
    fr::SafeSpot spot;
    spot.id = 0x61afbf6e1e2ee784ull;
    spot.brickRid = 0x00598B6364BC39F5ull;
    spot.at = fr::SpotPoint {1.0f, -2.5f, 3.25f};
    spot.signals = {5, 3, 3};

    CHECK(fr::SafeSpotText(2, 8, spot) == "spot 3/8 entity 61afbf6e1e2ee784 in brick 00598B6364BC39F5 at (1.00, "
                                          "-2.50, 3.25): picked (route outs 5, state 3, count 3)");

    // An uncomputed transform is no position.
    spot.at = fr::SpotPoint {0.0f, 0.0f, 0.0f};
    spot.signals = {1, 0, 0};
    CHECK(fr::SafeSpotText(0, 1, spot) == "spot 1/1 entity 61afbf6e1e2ee784 in brick 00598B6364BC39F5 position "
                                          "unread (at the origin): not picked (route outs 1, state 0, count 0)");

    spot.at.reset();
    spot.noPosition = "world transform not computed yet";
    CHECK(fr::SafeSpotText(0, 1, spot) == "spot 1/1 entity 61afbf6e1e2ee784 in brick 00598B6364BC39F5 position "
                                          "unread (world transform not computed yet): not picked (route outs 1, "
                                          "state 0, count 0)");

    spot.at.reset();
    spot.noPosition = "not spatial";
    spot.signals = {};
    CHECK(fr::SafeSpotText(0, 1, spot) == "spot 1/1 entity 61afbf6e1e2ee784 in brick 00598B6364BC39F5 position "
                                          "unread (not spatial): unknown (values unread) (route outs ?, state ?, "
                                          "count ?)");
}

TEST_CASE("safe spots: the summary names the picked spot") {
    std::vector<fr::SafeSpot> spots(3);
    spots[0].id = 1;
    spots[1].id = 2;
    spots[2].id = 3;

    for (fr::SafeSpot& spot : spots)
        spot.signals = {1, 0, 0};

    CHECK(fr::SafeSummaryText({}) == "no safe spots");
    CHECK(fr::SafeSummaryText(spots) == "3 spots, none picked yet");

    spots[1].signals = {5, 3, 3};
    CHECK(fr::SafeSummaryText(spots) == "3 spots, picked: spot 2 (0000000000000002)");

    spots[2].signals = {5, 3, 3};
    spots[0].signals = {};
    CHECK(fr::SafeSummaryText(spots) ==
          "3 spots, 2 look picked: spot 2 (0000000000000002), spot 3 (0000000000000003); 1 unreadable");

    CHECK(fr::SafeSummaryText(std::span(spots).first(1)) == "1 spot, none picked yet; 1 unreadable");
}

TEST_CASE("safe spots: a variant's entities near a spot, nearest first, capped") {
    const fr::SpotPoint safe {10, 10, 0};
    const std::vector<fr::PlacedPiece> pieces {
        {0xa, "ZGeomEntity", {12, 10, 0}},                    // 2 m
        {0xb, "ZGeomEntity", {10, 10, 0.5f}},                 // 0.5 m
        {0xc, "ZCompositeEntity template 00AB", {10, 13, 0}}, // 3 m: too far
        {0xd, "ZGeomEntity", {10, 10, -2.5f}},                // 2.5 m: at the radius, listed
    };

    CHECK(fr::SafeNearText(safe, "holiday_hoarders", pieces) ==
          "3 from holiday_hoarders within 2.50 m, nearest first: 0.50 m ZGeomEntity 000000000000000b at (10.00, "
          "10.00, 0.50); 2.00 m ZGeomEntity 000000000000000a at (12.00, 10.00, 0.00); 2.50 m ZGeomEntity "
          "000000000000000d at (10.00, 10.00, -2.50)");

    CHECK(fr::SafeNearText(safe, "holiday_hoarders", pieces, 2.5, 1) ==
          "3 from holiday_hoarders within 2.50 m, nearest first: 0.50 m ZGeomEntity 000000000000000b at (10.00, "
          "10.00, 0.50), +2 more");

    // Nothing close: the nearest one is still named.
    CHECK(fr::SafeNearText({0, 0, 0.01f}, "holiday_hoarders", pieces) ==
          "nothing from holiday_hoarders within 2.50 m (nearest 14.15 m ZGeomEntity 000000000000000b at (10.00, "
          "10.00, 0.50))");

    CHECK(fr::SafeNearText(safe, "holiday_hoarders", {}) ==
          "nothing from holiday_hoarders within 2.50 m (no placed entities read)");
}

TEST_CASE("safe spots: (0, 0, 0) and non-finite positions are unread") {
    CHECK_FALSE(fr::IsReadPoint({0.0f, 0.0f, 0.0f}));
    CHECK_FALSE(fr::IsReadPoint({-0.0f, 0.0f, -0.0f}));
    CHECK_FALSE(fr::IsReadPoint({std::numeric_limits<float>::quiet_NaN(), 1.0f, 2.0f}));
    CHECK_FALSE(fr::IsReadPoint({1.0f, std::numeric_limits<float>::infinity(), 2.0f}));
    CHECK(fr::IsReadPoint({0.0f, 0.0f, 0.01f}));
    CHECK(fr::IsReadPoint({-52.233f, 8.826f, -0.483f})); // a door once its transform was computed
}

TEST_CASE("safe spots: unread positions are left out of the near list") {
    // Game data: every position (0, 0, 0) put all 1135 entities "within 2.50 m" of every safe.
    const std::vector<fr::PlacedPiece> pieces {
        {0xa, "ZLightEntity", {0, 0, 0}},  // unread
        {0xb, "ZGeomEntity", {10, 10, 1}}, // 1 m
        {0xc, "ZGeomEntity", {0, 0, 0}},   // unread
        {0xd, "ZGeomEntity", {40, 10, 0}}, // 30 m
    };

    CHECK(fr::SafeNearText({10, 10, 0}, "holiday_hoarders", pieces) ==
          "1 from holiday_hoarders within 2.50 m, nearest first: 1.00 m ZGeomEntity 000000000000000b at (10.00, "
          "10.00, 1.00); 2 with position unread, not compared");

    // Next to the origin, unread entities still are not "near".
    CHECK(fr::SafeNearText({0.5f, 0, 0}, "holiday_hoarders", pieces) ==
          "nothing from holiday_hoarders within 2.50 m (nearest 13.83 m ZGeomEntity 000000000000000b at (10.00, "
          "10.00, 1.00)); 2 with position unread, not compared");

    const std::vector<fr::PlacedPiece> unread {{0xa, "ZLightEntity", {0, 0, 0}}};
    CHECK(fr::SafeNearText({10, 10, 0}, "holiday_hoarders", unread) ==
          "nothing from holiday_hoarders within 2.50 m (no placed entities read); 1 with position unread, not "
          "compared");

    // The safe's own position unread: nothing is compared.
    CHECK(fr::SafeNearText({0, 0, 0}, "holiday_hoarders", pieces) == "position unread; not compared");
}
