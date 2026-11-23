#pragma once

// Map markers of a map variant's own story (generic, found at runtime).
//
// A variant brick made for its own contract brings the map markers of that
// contract: Lust's scenario_ambrosia (Berlin) has twelve
// instances of ui/maptrackercontrols.template?/ld_maptracker_customobjective
// ("LD_MapTracker_CustomObjective", "_01" to "_12"), each parented to one of
// its safes ("Setpiece_Container_Items_Safe_Ambrosia", "_01" to "_11") or to
// its item shrine, with the discovery icon as view
// (m_pViewFactory: ui_tracker_icon_discovery, hud.maptrackers.DiscoveryMapTracker):
// the blue question marks on the map. They start visible (the shrine's after
// three love letters, MakeVisible) and a safe's OnPickup hides its own
// (MakeInvisible). The template's root is an aspect entity (ZSpatialEntity +
// ZCustomObjectiveMapTrackerAspect), so the entity's interfaces name its map
// tracker class; ZUIMapTrackerAspect (the base of every map tracker) holds
// the map layer it is drawn on and where it shows (m_bShowOnMainMap, ...).
//
// Freelancer's contract has none of these objectives, so every map tracker of
// the objective or opportunity kind inside the variant's own bricks is
// switched off once the scene's entities exist: each m_bShowOn* property set
// to false, so it never shows on any map whatever its pins later do (its
// MakeVisible and MakeInvisible keep working, unseen). Nothing else about it
// changes: the safes, items and NPCs it marks work as before. Freelancer's own
// markers live in Freelancer's bricks, which are never searched; a variant's
// other trackers (NPCs, the hero, exits, agency pickups, area names, dropped
// items) are left as they are and counted in the log.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fr {

// The engine's UIMapLayer::EUIMapLayerID (ZUIMapTrackerAspect::m_eLayer).
enum class MapLayer : int32_t {
    Unspecified = 0,
    Staircase = 1,
    AreaUndiscovered = 2,
    Text = 3,
    DroppedItemsAndDisguises = 4,
    Npc = 5,
    NorthIndicator = 6,
    SecurityCamera = 7,
    AgencyPickup = 8,
    Opportunity = 9,
    Exit = 10,
    Objective = 11,
    Target = 12,
    Opponent = 13,
    Hero = 14,
};

std::string_view MapLayerName(int32_t layer);

// The map tracker classes of a contract's objectives and mission stories.
constexpr std::string_view kStoryMarkerClasses[] = {
    "ZCustomObjectiveMapTrackerAspect",
    "ZGeneralObjectiveMapTrackerAspect",
    "ZOpportunityMapTrackerAspect",
};

// The base every map tracker aspect derives from (where m_eLayer is read).
constexpr std::string_view kMapTrackerBaseClass = "ZUIMapTrackerAspect";

// The trackers that follow a character (an NPC, 47): never switched off.
constexpr std::string_view kCharacterMarkerClasses[] = {
    "ZNpcUIMapTrackerAspect",
    "ZHeroUIMapTrackerAspect",
};

// The properties switched off (ZUIMapTrackerAspect): where it shows.
constexpr std::string_view kMarkerShowProperties[] = {
    "m_bShowOnMainMap", "m_bShowOnMiniMap", "m_bShowOnMenuMap", "m_bShowOnLegend", "m_bShowOnEdge",
};

// Whether an engine class name is a map tracker class ("...TrackerAspect").
bool IsMapTrackerClass(std::string_view className);

// The map tracker class an entity's interface names show: the first one that
// is not a base (ZUIMapTrackerAspect, ZGenericUIMapTrackerAspect), else the
// first base; empty when it is no map tracker.
std::string_view MarkerClassOf(std::span<const std::string> interfaces);

enum class MarkerAction {
    SwitchOff, // an objective or opportunity marker of the variant's story
    Leave,     // another kind of map tracker (exit, pickup, area name, ...)
    Character, // it follows an NPC or 47
};

// interfaces: the entity's interface names; layer: its m_eLayer, when read.
// Switched off: a story marker class, or another map tracker drawn on the
// objective or opportunity layer; never a character's.
MarkerAction DecideMarker(std::span<const std::string> interfaces, std::optional<int32_t> layer);

// The one log line's counts.
struct MarkerTally {
    size_t switchedOff = 0;
    size_t failed = 0;                        // a property not set, or not read back as false
    std::map<std::string, size_t> offByClass; // "ZCustomObjectiveMapTrackerAspect" -> 12 (switched off)
    std::map<std::string, size_t> leftByKind; // "ZGenericUIMapTrackerAspect (exit layer)" -> 1
};

// "12 switched off (ZCustomObjectiveMapTrackerAspect x12); left as they are:
// ZGenericUIMapTrackerAspect (exit layer) x1"; "none" when nothing was found.
std::string MarkerTallyText(const MarkerTally& tally);

} // namespace fr
