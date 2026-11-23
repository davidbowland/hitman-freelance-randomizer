#include "doctest.h"

#include "Guid.h"
#include "SafehouseCoin.h"

#include <array>
#include <set>
#include <string>

namespace {

constexpr std::array<bool, fr::kCoinSpots> kVanilla = fr::kCoinVanillaItemSpots;

} // namespace

TEST_CASE("safehouse coin: Out pins map to spots, unconnected ones to none") {
    CHECK(fr::CoinSpotOfOut(1) == std::optional<size_t>(0));
    CHECK(fr::CoinSpotOfOut(2) == std::optional<size_t>(1));
    CHECK(fr::CoinSpotOfOut(3) == std::optional<size_t>(2));
    CHECK_FALSE(fr::CoinSpotOfOut(4));
    CHECK_FALSE(fr::CoinSpotOfOut(0));
    CHECK_FALSE(fr::CoinSpotOfOut(-2));
}

TEST_CASE("safehouse coin: the mod's random spot is always a spot with an item, and reaches each of them") {
    std::set<size_t> seen;

    for (uint64_t e = 0; e < 300; ++e) {
        const auto spot = fr::PickCoinSpot(e, kVanilla);
        REQUIRE(spot);
        CHECK(kVanilla[*spot]);
        seen.insert(*spot);
    }

    CHECK(seen == std::set<size_t> {1, 2});
    CHECK_FALSE(fr::PickCoinSpot(7, {false, false, false}));
    CHECK(fr::PickCoinSpot(7, {true, false, false}) == std::optional<size_t>(0));
}

TEST_CASE("safehouse coin: the coin id parses and is not empty") {
    const auto id = fr::ParseGuid(fr::kCoinRepositoryId);
    REQUIRE(id);
    CHECK_FALSE(id->IsEmpty());
    CHECK(fr::FormatGuid(*id) == std::string(fr::kCoinRepositoryId));
}

TEST_CASE("safehouse coin: a coin spot picked by the game passes, and nothing more happens") {
    fr::CoinPlanner p(true, kVanilla);
    p.OnListener(0.2);

    for (int out : {2, 3}) {
        fr::CoinPlanner q(true, kVanilla);
        CHECK(q.OnRouteOut(out, 0.2) == fr::CoinPinVerdict::Pass);
        CHECK(q.Spot() == std::optional<size_t>(out - 1));
        CHECK_FALSE(q.ByMod());

        for (double t = 0.3; t < 120.0; t += 0.5)
            CHECK(q.Tick(t, 1).kind == fr::CoinActionKind::None);
    }
}

TEST_CASE("safehouse coin: the empty spot is blocked and the route picks again until it picks a coin spot") {
    fr::CoinPlanner p(true, kVanilla);
    p.OnListener(0.2);
    std::string why;
    CHECK(p.OnRouteOut(1, 0.2, &why) == fr::CoinPinVerdict::Block);
    CHECK(why.find("BLOCKED") != std::string::npos);
    CHECK_FALSE(p.Placed());

    // The next frame signals In again.
    fr::CoinAction a = p.Tick(0.25, 1);
    CHECK(a.kind == fr::CoinActionKind::SignalRouteIn);
    CHECK(p.Retries() == 1);

    // The route picks the empty spot again: blocked again, In again.
    CHECK(p.OnRouteOut(1, 0.25) == fr::CoinPinVerdict::Block);
    CHECK(p.Tick(0.3, 1).kind == fr::CoinActionKind::SignalRouteIn);

    // Then a coin spot: passed, done.
    CHECK(p.OnRouteOut(3, 0.3) == fr::CoinPinVerdict::Pass);
    CHECK(p.Spot() == std::optional<size_t>(2));
    CHECK(p.EmptyPicks() == 2);
    CHECK(p.Tick(0.35, 1).kind == fr::CoinActionKind::None);

    // Exactly one coin: any later pick is blocked, a coin spot too.
    CHECK(p.OnRouteOut(2, 50.0) == fr::CoinPinVerdict::Block);
    CHECK(p.OnRouteOut(1, 51.0) == fr::CoinPinVerdict::Block);
    CHECK(p.Blocked() == 4);
}

TEST_CASE("safehouse coin: after the re-picks run out the mod spawns a coin spot itself") {
    fr::CoinPlanner p(true, kVanilla);
    double t = 0.2;

    for (int i = 0; i < fr::kCoinMaxRetries; ++i) {
        CHECK(p.OnRouteOut(1, t) == fr::CoinPinVerdict::Block);
        t += 0.05;
        CHECK(p.Tick(t, 1).kind == fr::CoinActionKind::SignalRouteIn);
    }

    CHECK(p.OnRouteOut(1, t) == fr::CoinPinVerdict::Block);
    const fr::CoinAction a = p.Tick(t + 0.05, 99);
    REQUIRE(a.kind == fr::CoinActionKind::SpawnDirect);
    CHECK(kVanilla[a.spot]);
    p.OnSpawnDirect(a.spot, t + 0.05, true);
    CHECK(p.ByMod());
    CHECK(p.Spot() == std::optional<size_t>(a.spot));
    CHECK(p.OnRouteOut(2, t + 1) == fr::CoinPinVerdict::Block);
}

TEST_CASE("safehouse coin: a route that does not answer the mod's In gets a direct spawn") {
    fr::CoinPlanner p(true, kVanilla);
    CHECK(p.OnRouteOut(1, 0.2) == fr::CoinPinVerdict::Block);
    CHECK(p.Tick(0.25, 1).kind == fr::CoinActionKind::SignalRouteIn);
    CHECK(p.Tick(0.25 + fr::kCoinRouteGrace / 2, 1).kind == fr::CoinActionKind::None);
    const fr::CoinAction a = p.Tick(0.25 + fr::kCoinRouteGrace, 1);
    CHECK(a.kind == fr::CoinActionKind::SpawnDirect);
    CHECK(kVanilla[a.spot]);
}

TEST_CASE("safehouse coin: a silent route or listener starts the re-picks") {
    SUBCASE("listener fired, route silent") {
        fr::CoinPlanner p(true, kVanilla);
        p.OnListener(1.0);
        CHECK(p.Tick(1.0 + fr::kCoinRouteGrace / 2, 1).kind == fr::CoinActionKind::None);
        CHECK(p.Tick(1.0 + fr::kCoinRouteGrace, 1).kind == fr::CoinActionKind::SignalRouteIn);
    }

    SUBCASE("nothing fired: the deadline, not before") {
        fr::CoinPlanner p(true, kVanilla);
        CHECK(p.Tick(fr::kCoinRouteDeadline - 0.5, 1).kind == fr::CoinActionKind::None);
        CHECK(p.Tick(fr::kCoinRouteDeadline, 1).kind == fr::CoinActionKind::SignalRouteIn);
    }
}

TEST_CASE("safehouse coin: with the fix off nothing is blocked and nothing is done") {
    fr::CoinPlanner p(false, kVanilla);
    p.OnListener(0.2);
    std::string why;
    CHECK(p.OnRouteOut(1, 0.2, &why) == fr::CoinPinVerdict::Pass);
    CHECK(why.find("no coin this visit") != std::string::npos);
    CHECK_FALSE(p.Placed());
    CHECK(p.EmptyPicks() == 1);

    for (double t = 0.3; t < 120.0; t += 0.5)
        CHECK(p.Tick(t, 1).kind == fr::CoinActionKind::None);

    CHECK(p.OnRouteOut(2, 130.0) == fr::CoinPinVerdict::Pass);
    CHECK(p.OnRouteOut(3, 131.0) == fr::CoinPinVerdict::Pass);
    CHECK(p.Blocked() == 0);
}

TEST_CASE("safehouse coin: with no item on any spot the mod does nothing") {
    fr::CoinPlanner p(true, {false, false, false});
    CHECK(p.OnRouteOut(1, 0.2) == fr::CoinPinVerdict::Pass);
    CHECK(p.Tick(0.25, 1).kind == fr::CoinActionKind::None);
    CHECK(p.Tick(100.0, 1).kind == fr::CoinActionKind::None);
}

TEST_CASE("safehouse coin: ids are distinct") {
    std::set<uint64_t> ids {fr::kCoinListenerId, fr::kCoinRouteId};

    for (size_t i = 0; i < fr::kCoinSpots; ++i) {
        ids.insert(fr::kCoinSpawnerIds[i]);
        ids.insert(fr::kCoinNestIds[i]);
    }

    CHECK(ids.size() == 2 + 2 * fr::kCoinSpots);
    CHECK(fr::kCoinNestIds[2] == 0x6c5fe35c072a20beull); // birdsnest_a_02 in g_snug.txt
}
