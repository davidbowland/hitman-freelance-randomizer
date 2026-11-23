#pragma once

// Freelancer scene kinds (mild missions, showdowns) and their mission brick,
// the known target-pool helper ids, behavior type names and role names.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fr {

// --- Scenes ---

// A Freelancer mild mission scene: its file name is scene_*_mild_<game>.entity (any case).
bool IsMildMissionScene(std::string_view scene);

// The mild mission's brick, in the scene's folder: .../scene_mongoose_mild_ludo.entity ->
// .../mission_mild_ludo.brick (lower case). Empty for other scenes.
std::optional<std::string> MildMissionBrickPath(std::string_view scene);

// A Freelancer showdown scene: scene_*_hot_<game>.entity (any case).
bool IsShowdownScene(std::string_view scene);

// The mission brick of a mild mission (MildMissionBrickPath) or a showdown
// (.../scene_ancestral_hot_bridge.entity -> .../mission_hot_bridge.brick, where
// the showdown's suspects, lookouts and assassins live). Empty for other scenes.
std::optional<std::string> MissionBrickPath(std::string_view scene);

// The Helper_Evergreen_Spawn_Targets sub-entity id in a mild mission brick
// (by the brick's runtime resource id), from the entity each
// Minnow.MoreFreelancerTargets v1.1.0 patch edits; empty for other bricks.
// It knows 24: the 19 mild missions, the full Landslide's, the full Author's
// and the full Icon's copies of bingo's, the full Marrakesh night's copy of
// mancala's, and the full Patient Zero's copy of shogi's.
std::optional<uint64_t> KnownTargetPoolHelper(uint64_t brickRid);

// --- Behaviors (ECompiledBehaviorType) ---

// Behavior types are 0 to kBehaviorTypeEnd - 1 (BT_BEHAVIORS_END is 141).
constexpr int32_t kBehaviorTypeEnd = 141;

// "BT_Patrol"; "BT_#<n>" outside 0 to kBehaviorTypeEnd - 1.
std::string BehaviorName(int32_t type);

// --- Role actors ---

// "lookout", "assassin", "mule", "supplier" or "suspect" when the actor name
// contains that word (any case), checked in that order; else empty.
std::optional<std::string_view> RoleFromActorName(std::string_view name);

} // namespace fr
