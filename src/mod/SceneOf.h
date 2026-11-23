#pragma once

// The current scene alias (core/SceneAlias.h): set by a full variant's swap
// on the game thread, kept over restarts, cleared at the next LoadScene.
// Features look the current scene up through SceneOf, so a swapped-in scene
// counts as its contract's scene. Any thread.

#include "../core/SceneAlias.h"

#include <optional>
#include <string>
#include <string_view>

class ZEntitySceneContext;

// Game thread: the swap's record.
void SetSceneAlias(fr::SceneAlias p_Alias);

// Game thread, first thing at every LoadScene.
void ClearSceneAlias();

// The alias in force, if any.
std::optional<fr::SceneAlias> CurrentSceneAlias();

// The scene the mod counts p_Scene as (fr::ResolveScene with the current alias).
std::string SceneOf(std::string_view p_Scene);

// The same for the scene the context loads; empty without a context.
std::string SceneOf(const ZEntitySceneContext* p_Context);

// p_Scene is the current alias's actual (swapped-in) scene.
bool IsSwappedScene(std::string_view p_Scene);

// The context loads the swapped-in scene.
bool IsSwappedScene(const ZEntitySceneContext* p_Context);

// fr::AliasMissionBrickPath / fr::AliasMildMissionBrickPath with the current alias.
std::optional<std::string> MissionBrickOf(std::string_view p_Scene);
std::optional<std::string> MildMissionBrickOf(std::string_view p_Scene);

// fr::SceneLabel with the current alias, for log lines.
std::string SceneLabelOf(std::string_view p_Scene);
