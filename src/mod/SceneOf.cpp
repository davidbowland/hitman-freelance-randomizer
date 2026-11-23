#include "SceneOf.h"

#include <Glacier/ZScene.h>

#include <mutex>

namespace {

std::mutex g_AliasMutex;
std::optional<fr::SceneAlias> g_Alias;

std::string_view ContextScene(const ZEntitySceneContext* p_Context) {
    return p_Context ? p_Context->m_SceneInitParameters.m_SceneResource.ToStringView() : std::string_view();
}

} // namespace

void SetSceneAlias(fr::SceneAlias p_Alias) {
    std::lock_guard s_Lock(g_AliasMutex);
    g_Alias = std::move(p_Alias);
}

void ClearSceneAlias() {
    std::lock_guard s_Lock(g_AliasMutex);
    g_Alias.reset();
}

std::optional<fr::SceneAlias> CurrentSceneAlias() {
    std::lock_guard s_Lock(g_AliasMutex);
    return g_Alias;
}

std::string SceneOf(std::string_view p_Scene) {
    std::lock_guard s_Lock(g_AliasMutex);
    return fr::ResolveScene(g_Alias ? &*g_Alias : nullptr, p_Scene);
}

std::string SceneOf(const ZEntitySceneContext* p_Context) {
    return p_Context ? SceneOf(ContextScene(p_Context)) : std::string();
}

bool IsSwappedScene(std::string_view p_Scene) {
    std::lock_guard s_Lock(g_AliasMutex);
    return fr::IsAliasedScene(g_Alias ? &*g_Alias : nullptr, p_Scene);
}

bool IsSwappedScene(const ZEntitySceneContext* p_Context) {
    return p_Context && IsSwappedScene(ContextScene(p_Context));
}

std::optional<std::string> MissionBrickOf(std::string_view p_Scene) {
    std::lock_guard s_Lock(g_AliasMutex);
    return fr::AliasMissionBrickPath(g_Alias ? &*g_Alias : nullptr, p_Scene);
}

std::optional<std::string> MildMissionBrickOf(std::string_view p_Scene) {
    std::lock_guard s_Lock(g_AliasMutex);
    return fr::AliasMildMissionBrickPath(g_Alias ? &*g_Alias : nullptr, p_Scene);
}

std::string SceneLabelOf(std::string_view p_Scene) {
    std::lock_guard s_Lock(g_AliasMutex);
    return fr::SceneLabel(g_Alias ? &*g_Alias : nullptr, p_Scene);
}
