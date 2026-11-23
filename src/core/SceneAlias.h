#pragma once

// The scene alias of a swapped load (pure logic; src/mod/SceneOf.h holds the
// current one). The full Landslide (core/LandslideFull.h) loads
// _scene_mamba.entity in place of the contract's scene, mild Sapienza's
// scene_costaltown_mild_bingo.entity; the full Author loads scene_ebola.entity and the full Icon
// _scene_copperhead.entity (so does the full Bad Boy, with one more brick: its mover rows are tagged
// with its key, fr::MoverTableVariants); the full Marrakesh night
// loads Marrakesh's _scene_python.entity in place of scene_spider_mild_mancala.entity (so does the
// full Entertainer, with one more brick), and the full
// Patient Zero Hokkaido's _scene_flu.entity in place of scene_snowcrane_mild_shogi.entity (it keeps the
// normal hotel's people, so mild Hokkaido's mover rows apply to it until it has its own). Every feature
// that looks a scene up (seeded draws, per-scene tables, the mild-mission checks, the client
// requirements' location) counts the swapped scene as the contract's, so the
// full Landslide gets what mild Sapienza gets; the mission brick that stands
// in for mission_mild_bingo is the package's own. Tables bound to the
// entities of a brick the swap does not load (mission01's day crowd: the
// mover table) are keyed by the scene as loaded instead, so they do not
// apply to the full Landslide (or the full Icon) until it has its own rows. The full Author keeps
// mild Sapienza's day crowd bricks (sharesCrowd), so mild Sapienza's rows
// apply to it until it has rows of its own (MoverTableScene).

#include <optional>
#include <string>
#include <string_view>

namespace fr {

struct SceneAlias {
    std::string actual;       // the scene the engine loads (_scene_mamba.entity)
    std::string logical;      // the scene the mod counts it as (the contract's)
    std::string missionBrick; // the brick standing in for the logical scene's mission brick; empty: none
    std::string name;         // for the log ("the full Landslide"); may be empty
    std::string fullKey;      // the full variant's key ("badboy"; fr::FullVariantOfLoad): two may share a scene
    bool sharesCrowd = false; // the actual scene keeps the logical scene's crowd bricks (the full Author)
};

// p_Scene is the alias's actual scene (any case). False without an alias.
bool IsAliasedScene(const SceneAlias* p_Alias, std::string_view p_Scene);

// The scene the mod counts p_Scene as: the alias's logical scene when p_Scene
// is its actual one, else p_Scene.
std::string ResolveScene(const SceneAlias* p_Alias, std::string_view p_Scene);

// The mission brick of p_Scene (MissionBrickPath, SceneKinds.h): the alias's
// missionBrick for its actual or logical scene (when set), else that of the
// resolved scene.
std::optional<std::string> AliasMissionBrickPath(const SceneAlias* p_Alias, std::string_view p_Scene);

// The same for a mild mission only (MildMissionBrickPath).
std::optional<std::string> AliasMildMissionBrickPath(const SceneAlias* p_Alias, std::string_view p_Scene);

// The scene whose mover table rows (MoverTable.h) apply to p_Scene as loaded:
// the logical scene for the aliased scene of an alias that shares its crowd
// while p_LoadedHasRows is false (no census of the swapped scene yet), else
// p_Scene itself.
std::string MoverTableScene(const SceneAlias* p_Alias, std::string_view p_Scene, bool p_LoadedHasRows);

// For log lines: p_Scene, and for the aliased scene what it counts as
// ("..._scene_mamba.entity (the full Landslide, counted as ..._mild_bingo.entity)").
std::string SceneLabel(const SceneAlias* p_Alias, std::string_view p_Scene);

} // namespace fr
