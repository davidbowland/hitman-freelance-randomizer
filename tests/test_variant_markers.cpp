#include "doctest.h"

#include "VariantMarkers.h"

#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int32_t kObjective = static_cast<int32_t>(fr::MapLayer::Objective);
constexpr int32_t kOpportunity = static_cast<int32_t>(fr::MapLayer::Opportunity);
constexpr int32_t kExit = static_cast<int32_t>(fr::MapLayer::Exit);

} // namespace

// Lust's safes (scenario_ambrosia): ld_maptracker_customobjective's root is an
// aspect entity, ZSpatialEntity + ZCustomObjectiveMapTrackerAspect.
TEST_CASE("variant markers: Lust's custom objective markers are switched off, whatever their layer") {
    const std::vector<std::string> safe {"ZSpatialEntity", "ZCustomObjectiveMapTrackerAspect", "ZUIMapTrackerAspect",
                                         "ZEntityImpl"};

    CHECK(fr::MarkerClassOf(safe) == "ZCustomObjectiveMapTrackerAspect");
    CHECK(fr::DecideMarker(safe, std::nullopt) == fr::MarkerAction::SwitchOff);
    CHECK(fr::DecideMarker(safe, kObjective) == fr::MarkerAction::SwitchOff);
    CHECK(fr::DecideMarker(safe, kExit) == fr::MarkerAction::SwitchOff);
}

TEST_CASE("variant markers: the other story classes are switched off") {
    for (const char* name : {"ZGeneralObjectiveMapTrackerAspect", "ZOpportunityMapTrackerAspect"}) {
        const std::vector<std::string> interfaces {"ZSpatialEntity", name, "ZUIMapTrackerAspect"};
        CHECK(fr::DecideMarker(interfaces, std::nullopt) == fr::MarkerAction::SwitchOff);
    }
}

TEST_CASE("variant markers: a generic tracker is switched off only on the objective or opportunity layer") {
    const std::vector<std::string> generic {"ZSpatialEntity", "ZGenericUIMapTrackerAspect", "ZUIMapTrackerAspect"};

    CHECK(fr::MarkerClassOf(generic) == "ZGenericUIMapTrackerAspect");
    CHECK(fr::DecideMarker(generic, kObjective) == fr::MarkerAction::SwitchOff);
    CHECK(fr::DecideMarker(generic, kOpportunity) == fr::MarkerAction::SwitchOff);
    CHECK(fr::DecideMarker(generic, kExit) == fr::MarkerAction::Leave);
    CHECK(fr::DecideMarker(generic, std::nullopt) == fr::MarkerAction::Leave);

    // A stash point derives from the generic tracker: its own class is named.
    const std::vector<std::string> stash {"ZSpatialEntity", "ZStashPointTrackerAspect", "ZGenericUIMapTrackerAspect",
                                          "ZUIMapTrackerAspect"};
    CHECK(fr::MarkerClassOf(stash) == "ZStashPointTrackerAspect");
    CHECK(fr::DecideMarker(stash, static_cast<int32_t>(fr::MapLayer::AgencyPickup)) == fr::MarkerAction::Leave);
}

TEST_CASE("variant markers: an NPC's or 47's tracker is never switched off") {
    const std::vector<std::string> npc {"ZActor", "ZNpcUIMapTrackerAspect", "ZUIMapTrackerAspect"};
    const std::vector<std::string> hero {"ZHitman5", "ZHeroUIMapTrackerAspect", "ZUIMapTrackerAspect"};

    CHECK(fr::DecideMarker(npc, kObjective) == fr::MarkerAction::Character);
    CHECK(fr::DecideMarker(hero, kObjective) == fr::MarkerAction::Character);
}

TEST_CASE("variant markers: an entity that is no map tracker is left") {
    const std::vector<std::string> plain {"ZSpatialEntity", "ZEntityImpl"};

    CHECK(fr::MarkerClassOf(plain).empty());
    CHECK(fr::DecideMarker(plain, kObjective) == fr::MarkerAction::Leave);
    CHECK(fr::DecideMarker({}, std::nullopt) == fr::MarkerAction::Leave);
    CHECK_FALSE(fr::IsMapTrackerClass("ZContractObjectiveHudHintEntity"));
    CHECK(fr::IsMapTrackerClass("ZUIMapTrackerAspect"));
}

TEST_CASE("variant markers: layer names") {
    CHECK(fr::MapLayerName(kObjective) == "objective");
    CHECK(fr::MapLayerName(kExit) == "exit");
    CHECK(fr::MapLayerName(99) == "unknown");
}

TEST_CASE("variant markers: the log line's tally") {
    CHECK(fr::MarkerTallyText({}) == "none");

    fr::MarkerTally tally;
    tally.switchedOff = 12;
    tally.offByClass["ZCustomObjectiveMapTrackerAspect"] = 12;
    CHECK(fr::MarkerTallyText(tally) == "12 switched off (ZCustomObjectiveMapTrackerAspect x12)");

    tally.failed = 1;
    tally.leftByKind["ZGenericUIMapTrackerAspect (exit layer)"] = 2;
    CHECK(fr::MarkerTallyText(tally) == "12 switched off (ZCustomObjectiveMapTrackerAspect x12), 1 could not be "
                                        "switched off (they may still show); left as they are: "
                                        "ZGenericUIMapTrackerAspect (exit layer) x2");
}
