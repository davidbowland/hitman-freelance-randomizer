#include "SceneAlias.h"

#include "SceneKinds.h" // MissionBrickPath, MildMissionBrickPath
#include "Scene.h"

namespace fr {

namespace {

// The alias's logical scene (any case), with its actual one set.
bool IsLogicalScene(const SceneAlias* p_Alias, std::string_view p_Scene) {
    return p_Alias && !p_Alias->actual.empty() && !p_Alias->logical.empty() &&
           ToLower(p_Scene) == ToLower(p_Alias->logical);
}

// The alias's own mission brick, for either name of its scene.
std::optional<std::string> OwnMissionBrick(const SceneAlias* p_Alias, std::string_view p_Scene) {
    if (!p_Alias || p_Alias->missionBrick.empty())
        return std::nullopt;

    if (IsAliasedScene(p_Alias, p_Scene) || IsLogicalScene(p_Alias, p_Scene))
        return p_Alias->missionBrick;

    return std::nullopt;
}

} // namespace

bool IsAliasedScene(const SceneAlias* p_Alias, std::string_view p_Scene) {
    return p_Alias && !p_Alias->actual.empty() && ToLower(p_Scene) == ToLower(p_Alias->actual);
}

std::string ResolveScene(const SceneAlias* p_Alias, std::string_view p_Scene) {
    return IsAliasedScene(p_Alias, p_Scene) ? p_Alias->logical : std::string(p_Scene);
}

std::optional<std::string> AliasMissionBrickPath(const SceneAlias* p_Alias, std::string_view p_Scene) {
    if (auto own = OwnMissionBrick(p_Alias, p_Scene))
        return own;

    return MissionBrickPath(ResolveScene(p_Alias, p_Scene));
}

std::optional<std::string> AliasMildMissionBrickPath(const SceneAlias* p_Alias, std::string_view p_Scene) {
    const std::string resolved = ResolveScene(p_Alias, p_Scene);

    // Only a scene that counts as a mild mission gets the alias's brick.
    if (!IsMildMissionScene(resolved))
        return std::nullopt;

    if (auto own = OwnMissionBrick(p_Alias, p_Scene))
        return own;

    return MildMissionBrickPath(resolved);
}

std::string MoverTableScene(const SceneAlias* p_Alias, std::string_view p_Scene, bool p_LoadedHasRows) {
    if (IsAliasedScene(p_Alias, p_Scene) && p_Alias->sharesCrowd && !p_LoadedHasRows && !p_Alias->logical.empty())
        return p_Alias->logical;

    return std::string(p_Scene);
}

std::string SceneLabel(const SceneAlias* p_Alias, std::string_view p_Scene) {
    if (!IsAliasedScene(p_Alias, p_Scene))
        return std::string(p_Scene);

    return std::string(p_Scene) + " (" + (p_Alias->name.empty() ? std::string() : p_Alias->name + ", ") +
           "counted as " + p_Alias->logical + ")";
}

} // namespace fr
