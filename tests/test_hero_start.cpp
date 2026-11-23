#include "doctest.h"

#include "Guid.h"
#include "HeroStart.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace {

fr::Guid Id(const char* text) {
    return *fr::ParseGuid(text);
}

const char* const kRestaurant = "be6045fd-26ab-4a3b-a6ce-b1ccdf405d09"; // agencymarkup_snowcrane, deleted by mamushi
const char* const kMamushi = "aca2b971-d13e-4291-9a4d-c48605ad5566";    // scenario_mamushi's own, default
const char* const kMorgue = "bdc80464-31dd-440d-ad79-2767b923a0a4";
const char* const kPatientRoom = "a786fc74-b379-41f4-a4ac-ce970ee88b2c";

// Hokkaido mild under snow_festival, as the bricks have it: snowcrane's other
// spawns (one of them a default too), and HeroSpawn_Mamushi on the
// Restaurant's spot. Mamushi's position is the brick's; the other two are
// made up.
std::vector<fr::HeroSpawnFacts> SnowFestivalSpawns() {
    return {
        {Id(kMorgue), false, fr::StartPoint {-30.0f, 60.0f, -10.0f}, false},
        {Id(kPatientRoom), true, fr::StartPoint {20.0f, -80.0f, 5.0f}, false},
        {Id(kMamushi), true, fr::StartPoint {-117.0f, 34.79f, -0.47f}, true},
    };
}

} // namespace

TEST_CASE("hero start: snow_festival's deleted Restaurant start is taken over by HeroSpawn_Mamushi") {
    const auto spawns = SnowFestivalSpawns();
    const fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kRestaurant));

    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 2);
    CHECK(decision.why == fr::RepointWhy::SameSpot);
    CHECK(decision.matches == 0);
}

TEST_CASE("hero start: without positions, the variant's default spawn is re-pointed") {
    auto spawns = SnowFestivalSpawns();

    for (fr::HeroSpawnFacts& spawn : spawns)
        spawn.where.reset();

    const fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kRestaurant));
    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 2);
    CHECK(decision.why == fr::RepointWhy::VariantDefault);
}

TEST_CASE("hero start: a spawn answering the entrance is left alone") {
    const auto spawns = SnowFestivalSpawns();
    const fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kMorgue));

    CHECK(decision.action == fr::StartAction::Matched);
    CHECK(decision.spawn == 0);
    CHECK(decision.matches == 1);
}

TEST_CASE("hero start: no requested entrance, nothing to do") {
    const auto spawns = SnowFestivalSpawns();

    CHECK(fr::DecideHeroStart(spawns, std::nullopt).action == fr::StartAction::NoRequest);
    CHECK(fr::DecideHeroStart(spawns, fr::Guid {}).action == fr::StartAction::NoRequest);
}

TEST_CASE("hero start: an unknown entrance falls to the variant's default, then the scene's, then any spawn") {
    const fr::Guid other = Id("11111111-2222-3333-4444-555555555555");
    auto spawns = SnowFestivalSpawns();

    // Not a known deleted spawn: no same-spot rule, even though Mamushi stands there.
    fr::StartDecision decision = fr::DecideHeroStart(spawns, other);
    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 2);
    CHECK(decision.why == fr::RepointWhy::VariantDefault);

    spawns[2].isDefault = false;
    decision = fr::DecideHeroStart(spawns, other);
    CHECK(decision.spawn == 1);
    CHECK(decision.why == fr::RepointWhy::SceneDefault);

    spawns[1].isDefault = false;
    decision = fr::DecideHeroStart(spawns, other);
    CHECK(decision.spawn == 2); // the variant's own first
    CHECK(decision.why == fr::RepointWhy::AnySpawn);
}

TEST_CASE("hero start: a spawn whose entrance id was not read is never re-pointed") {
    std::vector<fr::HeroSpawnFacts> spawns {
        {std::nullopt, true, fr::StartPoint {-117.0f, 34.79f, -0.47f}, true},
        {Id(kMorgue), false, std::nullopt, false},
    };

    fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kRestaurant));
    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 1);
    CHECK(decision.why == fr::RepointWhy::AnySpawn);

    spawns.pop_back();
    CHECK(fr::DecideHeroStart(spawns, Id(kRestaurant)).action == fr::StartAction::NoSpawn);
    CHECK(fr::DecideHeroStart({}, Id(kRestaurant)).action == fr::StartAction::NoSpawn);
}

TEST_CASE("hero start: the same-spot rule takes the nearest spawn within kSameSpotMeters only") {
    std::vector<fr::HeroSpawnFacts> spawns {
        {Id(kMorgue), true, fr::StartPoint {-117.0f, 34.79f + 5.0f, -0.47f}, true},        // 5 m off: too far
        {Id(kPatientRoom), false, fr::StartPoint {-117.0f, 34.79f + 2.0f, -0.47f}, false}, // 2 m
        {Id(kMamushi), false, fr::StartPoint {-117.0f, 34.79f + 1.0f, -0.47f}, false},     // 1 m: nearest
    };

    fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kRestaurant));
    CHECK(decision.spawn == 2);
    CHECK(decision.why == fr::RepointWhy::SameSpot);

    spawns.pop_back();
    spawns.pop_back();
    decision = fr::DecideHeroStart(spawns, Id(kRestaurant));
    CHECK(decision.spawn == 0);
    CHECK(decision.why == fr::RepointWhy::VariantDefault);
}

TEST_CASE("hero start: the known deleted spawns parse and the texts are plain") {
    for (const fr::DeletedSpawn& spawn : fr::KnownDeletedSpawns())
        CHECK(fr::ParseGuid(spawn.entrance).has_value());

    CHECK(fr::RepointWhyText(fr::RepointWhy::SameSpot) == "it stands where the deleted spawn stood");
    CHECK(fr::RepointWhyText(fr::RepointWhy::VariantDefault) == "the map variant's default spawn");
}

TEST_CASE("hero start: nearest spawn to 47 skips spawns without a position") {
    const std::vector<std::optional<fr::StartPoint>> points {std::nullopt, fr::StartPoint {0.0f, 0.0f, 0.0f},
                                                             fr::StartPoint {-117.0f, 34.79f, -0.47f}};

    // The broken start: 47 left at location.brick's Agent47_Default, in the sky.
    const auto sky = fr::NearestSpawn(points, fr::StartPoint {-107.6f, -38.6f, 174.2f});
    REQUIRE(sky);
    CHECK(sky->index == 2);
    CHECK(sky->distance > fr::kHeroFarMeters);

    const auto ok = fr::NearestSpawn(points, fr::StartPoint {-116.5f, 35.0f, -0.47f});
    REQUIRE(ok);
    CHECK(ok->index == 2);
    CHECK(ok->distance < 1.0);

    CHECK_FALSE(fr::NearestSpawn(std::vector<std::optional<fr::StartPoint>> {std::nullopt}, {}).has_value());
}

TEST_CASE("hero start: reflection's lone HeroSpawn_Bellini takes the requested entrance") {
    // scenario_bellini deletes all of agencymarkup_snowcrane's spawns
    // (HeroSpawns_Snowcrane) and keeps its own, default, in the Mirror World
    // (Wolverine_DreamFloor_Mirror at z -179.25), where 47 is meant to start.
    const char* const kBellini = "6ad6d556-60dd-4d0b-8d8c-9c64f6265910";
    const std::vector<fr::HeroSpawnFacts> spawns {
        {Id(kBellini), true, fr::StartPoint {0.35f, 4.78f, -179.10f}, true},
    };

    const fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kRestaurant));
    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 0);
    CHECK(decision.why == fr::RepointWhy::VariantDefault); // not on the Restaurant's spot

    CHECK(fr::DecideHeroStart(spawns, Id(kBellini)).action == fr::StartAction::Matched);
}

TEST_CASE("hero start: the entrance is ZHeroSpawn's m_sRepositoryId, not the template's m_sId alias") {
    CHECK(fr::kHeroSpawnEntranceProperty == "m_sRepositoryId");
}

TEST_CASE("hero start: a spawn's position is its own, else its first template mate's that was read") {
    const std::vector<std::optional<fr::StartPoint>> mates {std::nullopt, fr::StartPoint {1.0f, 2.0f, 3.0f},
                                                            fr::StartPoint {4.0f, 5.0f, 6.0f}};

    fr::SpawnPoint point = fr::PickSpawnPoint(fr::StartPoint {7.0f, 8.0f, 9.0f}, mates);
    CHECK(point.source == fr::SpawnPointSource::Own);
    REQUIRE(point.where);
    CHECK(point.where->x == 7.0f);

    point = fr::PickSpawnPoint(std::nullopt, mates);
    CHECK(point.source == fr::SpawnPointSource::TemplateMate);
    CHECK(point.mate == 1);
    REQUIRE(point.where);
    CHECK(point.where->z == 3.0f);

    point = fr::PickSpawnPoint(std::nullopt, {});
    CHECK(point.source == fr::SpawnPointSource::None);
    CHECK_FALSE(point.where);
}

namespace {

const char* const kDartmoorRequested = "0aec817c-738e-4381-bbd4-a13b5d1c6730";
const char* const kDartmoorGarden = "7e17b82c-56a6-4c6c-9666-b4ae2a8c0ac3";
const char* const kSlothStaging = "62b8f500-38df-4fca-9fb8-a485b95e3e3f";
constexpr uint64_t kSlothStagingId = 0x22f118f0776459a5ull;

// dartmoor's showdown scene under the sloth variant: the
// garden brick's spawn (its position made up, on the ground) and the
// variant's default spawn, a staging spot 500 m up.
std::vector<fr::HeroSpawnFacts> SlothSpawns() {
    return {
        {Id(kDartmoorGarden), false, fr::StartPoint {40.0f, -60.0f, 2.0f}, false, 0x44bc69efb5aa958dull},
        {Id(kSlothStaging), true, fr::StartPoint {-509.04f, -0.10f, 499.99f}, true, kSlothStagingId},
    };
}

} // namespace

TEST_CASE("hero start: dartmoor sloth's staging spawn 500 m up is passed over for the spawn on the map") {
    auto spawns = SlothSpawns();
    fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kDartmoorRequested));

    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 0);
    CHECK(decision.why == fr::RepointWhy::AnySpawn);
    CHECK(decision.offMap == 1);

    // Off the map by its height alone (not on the known list).
    decision = fr::DecideHeroStart(spawns, Id(kDartmoorRequested), fr::KnownDeletedSpawns(), {});
    CHECK(decision.spawn == 0);
    CHECK(decision.offMap == 1);

    // No positions read in time: the known list still rules it out; the
    // garden spawn (position unknown) can still take the entrance.
    for (fr::HeroSpawnFacts& spawn : spawns)
        spawn.where.reset();

    decision = fr::DecideHeroStart(spawns, Id(kDartmoorRequested));
    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 0);
    CHECK(decision.offMap == 1);

    // Neither a position nor a known id: nothing tells, the old order holds.
    decision = fr::DecideHeroStart(spawns, Id(kDartmoorRequested), fr::KnownDeletedSpawns(), {});
    CHECK(decision.spawn == 1);
    CHECK(decision.why == fr::RepointWhy::VariantDefault);
    CHECK(decision.offMap == 0);
}

TEST_CASE("hero start: a default spawn off the map gives way to another default on it") {
    const std::vector<fr::HeroSpawnFacts> spawns {
        {Id(kMorgue), true, fr::StartPoint {10.0f, 10.0f, 300.0f}, true},      // the variant's default, 300 m up
        {Id(kPatientRoom), true, fr::StartPoint {20.0f, -80.0f, 5.0f}, false}, // another brick's default
    };

    const fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kRestaurant));
    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 1);
    CHECK(decision.why == fr::RepointWhy::SceneDefault);
    CHECK(decision.offMap == 1);
}

TEST_CASE("hero start: with only spawns off the map, nothing is re-pointed") {
    auto spawns = SlothSpawns();
    spawns.erase(spawns.begin());

    fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kDartmoorRequested));
    CHECK(decision.action == fr::StartAction::OffMapOnly);
    CHECK(decision.offMap == 1);

    // A spawn whose entrance was not read is not counted: no spawn at all to re-point.
    spawns[0].entrance.reset();
    decision = fr::DecideHeroStart(spawns, Id(kDartmoorRequested));
    CHECK(decision.action == fr::StartAction::NoSpawn);
    CHECK(decision.offMap == 0);
}

TEST_CASE("hero start: a spawn answering the entrance is left alone even off the map") {
    const auto spawns = SlothSpawns();
    const fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kSlothStaging));

    CHECK(decision.action == fr::StartAction::Matched);
    CHECK(decision.spawn == 1);
    CHECK(decision.offMap == 0);
}

TEST_CASE("hero start: reflection's Mirror World start at z -179 is on the map") {
    const std::vector<fr::HeroSpawnFacts> spawns {
        {Id("6ad6d556-60dd-4d0b-8d8c-9c64f6265910"), true, fr::StartPoint {0.35f, 4.78f, -179.10f}, true},
    };

    CHECK(fr::SpawnPlaceOf(spawns, 0) == fr::SpawnPlace::OnMap);

    const fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kRestaurant));
    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 0);
    CHECK(decision.offMap == 0);
}

TEST_CASE("hero start: a spawn's place: bounds, unread positions, and height against the other spawns") {
    const auto place = [](const std::vector<fr::HeroSpawnFacts>& spawns, size_t index) {
        return fr::SpawnPlaceOf(spawns, index, {});
    };
    const auto at = [](float x, float y, float z) {
        return fr::HeroSpawnFacts {Id(kMorgue), false, fr::StartPoint {x, y, z}, false};
    };

    CHECK(place({at(0.0f, 0.0f, 249.0f)}, 0) == fr::SpawnPlace::OnMap);
    CHECK(place({at(0.0f, 0.0f, -249.0f)}, 0) == fr::SpawnPlace::OnMap);
    CHECK(place({at(0.0f, 0.0f, 251.0f)}, 0) == fr::SpawnPlace::OffMap);
    CHECK(place({at(0.0f, 0.0f, -251.0f)}, 0) == fr::SpawnPlace::OffMap);
    CHECK(place({at(3001.0f, 0.0f, 1.0f)}, 0) == fr::SpawnPlace::OffMap);
    CHECK(place({at(0.0f, -3001.0f, 1.0f)}, 0) == fr::SpawnPlace::OffMap);

    // (0, 0, 0) is a stale cache, not a position; nor is a NaN.
    CHECK(place({at(0.0f, 0.0f, 0.0f)}, 0) == fr::SpawnPlace::Unknown);
    CHECK(place({at(0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f)}, 0) == fr::SpawnPlace::Unknown);
    CHECK(place({fr::HeroSpawnFacts {Id(kMorgue), false, std::nullopt, false}}, 0) == fr::SpawnPlace::Unknown);
    CHECK(place({}, 0) == fr::SpawnPlace::Unknown);

    // 180 m up: within the bounds, but far above two spawns on the ground.
    CHECK(place({at(0.0f, 0.0f, 180.0f), at(10.0f, 10.0f, 0.5f), at(-20.0f, 5.0f, 4.0f)}, 0) == fr::SpawnPlace::OffMap);
    CHECK(place({at(0.0f, 0.0f, 180.0f), at(10.0f, 10.0f, 0.5f), at(-20.0f, 5.0f, 4.0f)}, 1) == fr::SpawnPlace::OnMap);
    // One other spawn is not enough to judge by.
    CHECK(place({at(0.0f, 0.0f, 180.0f), at(10.0f, 10.0f, 0.5f)}, 0) == fr::SpawnPlace::OnMap);
    // Spawns off the map, or unread, are not peers.
    CHECK(place({at(0.0f, 0.0f, 180.0f), at(10.0f, 10.0f, 0.5f), at(1.0f, 1.0f, 600.0f)}, 0) == fr::SpawnPlace::OnMap);
    CHECK(place({at(0.0f, 0.0f, 180.0f), at(10.0f, 10.0f, 0.5f), at(0.0f, 0.0f, 0.0f)}, 0) == fr::SpawnPlace::OnMap);

    // A known off-map spawn, by id, with or without a position.
    const std::vector<fr::HeroSpawnFacts> known {{Id(kSlothStaging), true, std::nullopt, true, kSlothStagingId}};
    CHECK(fr::SpawnPlaceOf(known, 0) == fr::SpawnPlace::OffMap);
    CHECK(fr::SpawnPlaceOf(known, 0, {}) == fr::SpawnPlace::Unknown);

    CHECK(fr::SpawnPlaceText(fr::SpawnPlace::OffMap) == "off the map");
}

TEST_CASE("hero start: the known sealed spawns, by option and spawn id") {
    const fr::SealedSpawn* balcony = fr::FindSealedSpawn("garden_show", 0x44bc69efb5aa958dull);
    REQUIRE(balcony != nullptr);
    CHECK(balcony->name == "Start Evergreen_AlexaBalcony");
    CHECK_FALSE(balcony->why.empty());
    // the same spawn is the start Sloth re-points to: open there
    CHECK(fr::FindSealedSpawn("sloth", 0x44bc69efb5aa958dull) == nullptr);
    CHECK(fr::FindSealedSpawn("illusions_of_grandeur", 0x86566aaa4894f5bbull) != nullptr); // HeroSpawn_Barge
    CHECK(fr::FindSealedSpawn("illusions_of_grandeur", 0xc5724fc0bd71ca79ull) != nullptr); // Bollywood_Dancer
    // starts a variant puts off limits with its people beside them
    CHECK(fr::FindSealedSpawn("a_bitter_pill", 0x8c10e5329a36e3f9ull) != nullptr);          // Evergreen_Spawn_House
    CHECK(fr::FindSealedSpawn("the_undying", 0x778eadc343bab4d9ull) != nullptr);            // Start_Kronstad_Engineer
    CHECK(fr::FindSealedSpawn("the_undying_returns", 0x778eadc343bab4d9ull) != nullptr);    // Start_Kronstad_Engineer
    CHECK(fr::FindSealedSpawn("embrace_of_the_serpent", 0xee87db326b6dce06ull) != nullptr); // HeroSpawn_Jungle
    CHECK(fr::FindSealedSpawn("illusions_of_grandeur", 0x033d8b8196931cdfull) != nullptr);  // HeroSpawn_Kashmirian
    // off limits with no one beside it: stays (The Banker's basement, shoot on sight but empty)
    CHECK(fr::FindSealedSpawn("the_banker", 0xbfa4eb62b20579f4ull) == nullptr); // HeroSpawn_Basement_Kitchen
    CHECK(fr::FindSealedSpawn("the_banker", 0x011253c599a25b8aull) == nullptr); // HeroSpawn_Basement_Lockeroom
    // the same house spawn with no variant, or another Whittleton Creek variant: open
    CHECK(fr::FindSealedSpawn("", 0x8c10e5329a36e3f9ull) == nullptr);
    CHECK(fr::FindSealedSpawn("the_bad_boy", 0x8c10e5329a36e3f9ull) == nullptr);
    CHECK(fr::FindSealedSpawn("", 0x86566aaa4894f5bbull) == nullptr);
    CHECK(fr::FindSealedSpawn("illusions_of_grandeur", 0) == nullptr);
}

namespace {

const char* const kBarge = "0e1f2a3b-0000-4000-8000-000000000001";  // made up
const char* const kDancer = "0e1f2a3b-0000-4000-8000-000000000002"; // made up
const char* const kMarket = "0e1f2a3b-0000-4000-8000-000000000003"; // made up
const char* const kChawl = "0e1f2a3b-0000-4000-8000-000000000004";  // made up: the variant's own, default

// Mumbai mild under Illusions of Grandeur: the barge and the dancer sealed,
// another Freelancer start open, and the variant's default spawn.
std::vector<fr::HeroSpawnFacts> IllusionsSpawns() {
    std::vector<fr::HeroSpawnFacts> spawns {
        {Id(kBarge), false, fr::StartPoint {17.6f, -34.1f, -0.8f}, false, 0x86566aaa4894f5bbull},
        {Id(kDancer), false, fr::StartPoint {-17.4f, 8.3f, 9.9f}, false, 0xc5724fc0bd71ca79ull},
        {Id(kMarket), false, fr::StartPoint {40.0f, 10.0f, 0.0f}, false, 0x1111ull},
        {Id(kChawl), true, fr::StartPoint {-85.42f, 43.32f, -0.96f}, true, 0x6fddc61c64dfdaa4ull},
    };

    for (fr::HeroSpawnFacts& spawn : spawns)
        spawn.sealed = fr::FindSealedSpawn("illusions_of_grandeur", spawn.id) != nullptr;

    return spawns;
}

} // namespace

TEST_CASE("hero start: a sealed requested start is never used; another spawn takes its entrance") {
    const auto spawns = IllusionsSpawns();
    REQUIRE(spawns[0].sealed);
    REQUIRE(spawns[1].sealed);
    CHECK_FALSE(spawns[2].sealed);

    fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kBarge));
    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 3); // the variant's default, as for a deleted start
    CHECK(decision.why == fr::RepointWhy::VariantDefault);
    CHECK(decision.matches == 0);
    CHECK(decision.sealedRequest);
    CHECK(decision.sealedSpawn == 0);

    decision = fr::DecideHeroStart(spawns, Id(kDancer));
    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.sealedSpawn == 1);
    CHECK(decision.spawn != 0);
    CHECK(decision.spawn != 1);

    // An open start is used as it is.
    decision = fr::DecideHeroStart(spawns, Id(kMarket));
    CHECK(decision.action == fr::StartAction::Matched);
    CHECK(decision.spawn == 2);
    CHECK_FALSE(decision.sealedRequest);
}

TEST_CASE("hero start: a sealed spawn is never the one re-pointed") {
    auto spawns = IllusionsSpawns();
    spawns.erase(spawns.begin() + 2, spawns.end()); // only the two sealed spawns left

    const fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kChawl));
    CHECK(decision.action == fr::StartAction::NoSpawn);
}

TEST_CASE("hero start: a sealed requested start with nothing to take over is left as it is") {
    auto spawns = IllusionsSpawns();
    spawns.erase(spawns.begin() + 1, spawns.end()); // the barge alone

    const fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kBarge));
    CHECK(decision.action == fr::StartAction::SealedOnly);
    CHECK(decision.sealedRequest);
    CHECK(decision.sealedSpawn == 0);
}

TEST_CASE("hero start: the garden show's balcony start goes to the variant's start") {
    const char* const kBalcony = "0e1f2a3b-0000-4000-8000-000000000005"; // made up
    const char* const kSnake = "0e1f2a3b-0000-4000-8000-000000000006";   // made up
    std::vector<fr::HeroSpawnFacts> spawns {
        {Id(kBalcony), false, fr::StartPoint {0.0f, 61.1f, 25.0f}, false, 0x44bc69efb5aa958dull},
        {Id(kSnake), true, fr::StartPoint {0.86f, -58.99f, 1.04f}, true, 0x333920730c44aab7ull},
    };

    for (fr::HeroSpawnFacts& spawn : spawns)
        spawn.sealed = fr::FindSealedSpawn("garden_show", spawn.id) != nullptr;

    fr::StartDecision decision = fr::DecideHeroStart(spawns, Id(kBalcony));
    CHECK(decision.action == fr::StartAction::Repoint);
    CHECK(decision.spawn == 1);
    CHECK(decision.sealedRequest);

    // Under Sloth the balcony is open (not sealed): it answers.
    spawns[0].sealed = fr::FindSealedSpawn("sloth", spawns[0].id) != nullptr;
    decision = fr::DecideHeroStart(spawns, Id(kBalcony));
    CHECK(decision.action == fr::StartAction::Matched);
    CHECK(decision.spawn == 0);
}
