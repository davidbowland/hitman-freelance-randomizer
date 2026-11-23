#pragma once

// The start fix: a map variant can delete the hero spawn the contract's
// entrance asks for, which leaves 47 at the scene's placeholder in the sky.
// Armed whenever a map variant loads; at eLoading_SceneAllocated, when no
// loaded spawn answers the entrance, another spawn on the map is re-pointed to
// it (rules in core/HeroStart.h). 47 is never moved; afterwards his distance
// to the nearest spawn is logged ("start check:" lines).

#include "../core/HeroStart.h" // the pure logic (this header shares its name)

#include <Glacier/ZScene.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ProRepository;
class ZDynamicObject;

class HeroStart {
public:
    // Init: pro.repo, to check the requested entrance is a real one.
    void SetRepository(ProRepository* p_Repo) { m_Repo = p_Repo; }

    // Game thread, in the LoadScene detour, when a map variant was applied:
    // p_Scene is the scene that loads (a full variant's own), p_Option the
    // variant's name for the log, p_VariantBricks the runtime resource ids of
    // the bricks the variant appended (empty for a full variant: the whole
    // scene is the variant's).
    void Arm(std::string_view p_Scene, std::string_view p_Option, std::vector<uint64_t> p_VariantBricks);

    // Game thread, first thing at every LoadScene.
    void Forget() noexcept;

    // Game thread, from the SetLoadingStage detour.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;

    // Game thread, at a scene clear: a restart allocates the scene again
    // (fixed and checked again; the arming stays).
    void OnSceneClear() noexcept;

    // Game thread, every play-mode frame: the position check.
    void Tick(double p_GameSeconds) noexcept;

    // Hot path: one relaxed load while no HeroSpawn_Location event is awaited.
    bool EventsWanted() const noexcept { return m_EventsWanted.load(std::memory_order_relaxed); }

    // Any sent event: logs the game's HeroSpawn_Location.
    void OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept;

private:
    // One ZHeroSpawn found at scene allocation: the one inside a herospawn
    // template instance (owner, the instance's root; the brick names the
    // instance, e.g. HeroSpawn_Restaurant).
    struct Spawn {
        ZEntityType** entity = nullptr;
        uint64_t id = 0;               // sub-entity id (the template's, the same in every instance)
        ZEntityType** owner = nullptr; // the template instance's root
        ZEntityBlueprintFactoryBase* ownerFactory = nullptr; // the factory entity is a sub-entity of
        int index = -1;                                      // entity's index there
        uint64_t ownerId = 0;                                // the instance's sub-entity id in its brick, 0 when unread
        std::string name;                                    // the instance's authored name, or "HeroSpawn"
        std::string pointFrom;                               // where the position was read, for the log
        fr::HeroSpawnFacts facts;
    };

    void ReadPoint(Spawn& p_Spawn, bool p_Refresh = false);

    void Fix(ZEntitySceneContext* p_Context);
    void CheckPosition();
    std::string SpawnLabel(size_t p_Index) const;
    std::string SealedWhy(size_t p_Index) const;

    ProRepository* m_Repo = nullptr;

    // Game thread.
    std::string m_Scene; // empty: not armed
    std::string m_Option;
    std::vector<uint64_t> m_VariantBricks;
    bool m_Done = false;     // this allocation's fix ran
    bool m_Watching = false; // the position check is still to come
    double m_Played = 0.0;
    std::vector<Spawn> m_Spawns;

    // The HeroSpawn_Location event (any thread): what to compare it with.
    std::atomic<bool> m_EventsWanted {false};
    std::mutex m_EventMutex;
    std::optional<fr::Guid> m_Requested;                     // under m_EventMutex
    std::vector<std::pair<fr::Guid, std::string>> m_Answers; // spawn entrance ids and labels, under m_EventMutex
};
