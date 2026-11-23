#pragma once

// Safehouse coin (safehouse_coin, on by default). The graph, the
// measurements and the plan are in core/SafehouseCoin.h. Lines under
// log_safehouse: "safehouse coin:" (what was found, why nothing was watched
// or changed, the placement, the summary) and the route's Out pins with the
// block / allow decision.
//
// In the Freelancer safehouse, at eLoading_SceneAllocated (or
// eLoading_SceneStarted when the entities are not there yet), one walk of the
// safehouse brick finds the listener, the random route, the three spawners
// and nests by sub-entity id, and reads each spawner's item (its ZRepositoryID
// property a1e67258; empty = no item). The chain's output pins are watched as
// they fire (listener EventOccurred, route Out<n>, spawner itemSpawned), and
// fr::CoinPlanner decides: a pick of a spot without an item is blocked and
// the route's In is signaled again on the next frame (the game picks again),
// after kCoinMaxRetries or a silent route the mod signals debug_SpawnItem on a
// random coin spot, and once a coin spot is picked every later route output is
// blocked. Every line starts with "safehouse coin".

#include "SafehouseCoin.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

class CoinSpawn {
public:
    void Configure(bool p_On);

    // Game thread, from the SetLoadingStage detour. Catches what it throws.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;

    // Game thread, at a scene clear: logs the visit's summary and forgets it.
    void OnSceneClear() noexcept;

    // Game thread, every play-mode frame.
    void Tick(double p_Dt) noexcept;

    // Hot path: one relaxed load when no safehouse chain is armed.
    bool PinsOn() const noexcept { return m_PinsOn.load(std::memory_order_relaxed); }

    // An output pin of any entity; true to block it (fr::CoinPlanner).
    bool OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept;

private:
    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context);
    bool Find(ZEntitySceneContext* p_Context, const char* p_Stage); // false: try again at the next stage
    void ReadItems();
    void LogRoute();
    void Reset();

    bool m_On = false;

    // This load (game thread; the pin hooks read the atomics).
    bool m_Done = false;
    bool m_IsSafehouse = false;
    std::string m_Scene;
    std::atomic<bool> m_PinsOn {false};
    std::atomic<ZEntityType**> m_Listener {nullptr};
    std::atomic<ZEntityType**> m_Route {nullptr};
    std::array<std::atomic<ZEntityType**>, fr::kCoinSpots> m_Spawners {};
    std::array<bool, fr::kCoinSpots> m_HasItem = fr::kCoinVanillaItemSpots;
    std::string m_ItemsNote; // how m_HasItem was decided

    // Shared with the pin hooks.
    std::mutex m_Mutex;
    std::optional<fr::CoinPlanner> m_Planner;
    bool m_Playing = false;
    double m_Now = 0.0;                                          // seconds of play
    std::array<std::optional<double>, fr::kCoinSpots> m_Spawned; // itemSpawned seen per spawner
    int m_RouteOuts = 0;                                         // route outputs seen
    bool m_LoggedPlaced = false;
    bool m_ReportedCheck = false;
};
