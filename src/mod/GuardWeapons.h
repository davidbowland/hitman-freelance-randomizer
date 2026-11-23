#pragma once

// Guard weapons. Logs every ZActorInventoryHandler::StartItemStreamIn call of
// a Freelancer scene and, in Freelancer missions with guard_weapons,
// gives each eligible guard a weapon picked from its own outfit kit's keys by
// calling the original with that key (never writing a repository id).

#include "../core/GuardWeapons.h" // the pure logic (this header shares its name)
#include "Seed.h"

#include <Glacier/TArray.h>
#include <Glacier/ZEntity.h>
#include <Glacier/ZRepository.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ProRepository;
class ZActorInventoryHandler;

class GuardWeapons {
public:
    explicit GuardWeapons(ProRepository& p_Repo);

    // Game thread, at a scene's load (LoadScene, or a restart's
    // eLoading_AssetsLoaded): resets the per-scene state and publishes the new
    // scene. p_Seed is the mission's seed resolved before the scene loads.
    void OnSceneLoad(std::string_view p_Scene, std::string_view p_Type, const std::optional<fr::SeedResolution>& p_Seed,
                     bool p_On);

    // Game thread: at eLoading_SceneStarted, publishes a scene that was
    // loaded without LoadScene.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context);

    // Game thread, at a scene clear: logs the scene's counts and forgets it.
    void OnSceneClear();

    // Any thread, from the StartItemStreamIn detour: logs the call and returns
    // the key to call the original with instead of p_WeaponKey (one of the
    // actor's own outfit kit keys), or empty to leave the call unchanged.
    // Reads nothing but guarded engine memory and writes nothing to the
    // engine.
    std::optional<TEntityRef<ZItemRepositoryKeyEntity>>
    OnStreamIn(ZActorInventoryHandler* p_Handler, const TArray<TEntityRef<ZItemRepositoryKeyEntity>>& p_InventoryKeys,
               const TEntityRef<ZItemRepositoryKeyEntity>& p_WeaponKey,
               const TEntityRef<ZItemRepositoryKeyEntity>& p_GrenadeKey);

private:
    // One scene, immutable once published.
    struct SceneState {
        std::string scene;
        bool freelancer = false;
        bool mission = false;
        bool on = false; // guard_weapons: the swap runs (else stream-ins are only logged)
        std::optional<fr::SeedResolution> seed;
    };

    // An entity's first decision of the scene: rebind to key (whose
    // repository id read as id), or leave the call alone (no key).
    struct Decision {
        std::optional<TEntityRef<ZItemRepositoryKeyEntity>> key;
        fr::Guid id;
    };

    void Publish(std::unique_ptr<SceneState> p_State);
    void ResetScene();
    void EnsureItemCache();
    void CheckSceneContext(ZEntitySceneContext* p_Context);
    void LogCounts(const char* p_When);
    bool TakeLine();
    std::string ItemText(const fr::Guid& p_Id) const;
    void Decide(const std::optional<uint64_t>& p_EntityId, Decision p_Decision);
    std::optional<TEntityRef<ZItemRepositoryKeyEntity>> Repeat(const Decision& p_Decision, const std::string& p_Name,
                                                               uint64_t p_EntityId);

    ProRepository& m_Repo; // game thread only

    // Published to the detour (any thread). Every published state is kept
    // until the mod unloads, so a reader holding an older pointer never sees
    // it freed (a few hundred bytes per scene).
    std::atomic<const SceneState*> m_State {nullptr};
    std::atomic<const fr::ItemCache*> m_Items {nullptr};
    std::vector<std::unique_ptr<const SceneState>> m_States; // game thread only
    std::unique_ptr<const fr::ItemCache> m_ItemCache;        // game thread only

    // Per scene.
    std::atomic<int> m_Stage {-1};
    std::atomic<uint32_t> m_GameThread {0};
    std::atomic<uint32_t> m_Calls {0};
    std::atomic<uint32_t> m_Lines {0};
    std::atomic<uint32_t> m_Rebinds {0};
    std::atomic<uint32_t> m_Kept {0};    // eligible guards whose pick is their current weapon
    std::atomic<uint32_t> m_Repeats {0}; // later calls of an entity already decided
    std::array<std::atomic<uint32_t>, fr::kWeaponSkipCount> m_Skips {};
    std::array<std::atomic<uint32_t>, fr::kWeaponBucketCount * fr::kWeaponBucketCount> m_Transitions {}; // rebinds

    // Missions with guard_weapons on: each entity id's first
    // decision this scene, repeated on its later stream-ins (a guard streamed
    // in again mid-mission, when its role flags are set, keeps its first
    // weapon).
    std::mutex m_DecisionsMutex;
    std::unordered_map<uint64_t, Decision> m_Decisions;
};
