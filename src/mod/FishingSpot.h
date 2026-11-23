#pragma once

// Fishing script (fishing_script, on by default). The graph, the plan and the
// pure part are in core/FishingScript.h. Lines: "fishing:" (what was found,
// each catch, the summary) under log_safehouse.
//
// In the Freelancer safehouse, at eLoading_SceneAllocated (or
// eLoading_SceneStarted when the entities are not there yet), the fishing
// chain's entities are looked up by sub-entity id in the safehouse brick: the
// fishing spot, "TimerSimple 2", the RandomSelector, its six choices and six
// item spawners, "CaughtSomething" and "HeroItemAction". Each spawner's item
// (m_rMainItemKey -> m_RepositoryId, title from pro.repo) is read and
// classified (fish, fishing line, pirate saber, other), and each choice's
// m_Weight located.
// - Script: right before the selector's Pick (the timer's Out on its way, or
//   the Pick input, whichever the hooks see first) fr::FishingRolls decides;
//   for a scripted catch every choice but the scripted item's gets m_Weight 0
//   (read back), and at the selector's OnPickDone / OnPickFailed the game's
//   weights are written back. OnPickFailed of a steered roll (the item cannot
//   be drawn) makes the mod signal Pick again next frame, unsteered. The
//   script restarts from the first catch on every safehouse visit.
// Every line starts with "fishing". Every engine read and write is guarded;
// the entry points catch everything.

#include "FishingScript.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class ProRepository;

class FishingSpot {
public:
    explicit FishingSpot(ProRepository& p_Repo) : m_Repo(p_Repo) {}

    // At config load.
    void Configure(bool p_On);

    // Whether the pin hooks are needed at all this session.
    bool Wanted() const { return m_On; }

    // Game thread, from the SetLoadingStage detour.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;

    // Game thread, at a scene clear: restores open weights, logs the summary.
    void OnSceneClear() noexcept;

    // Game thread, every play-mode frame.
    void Tick(double p_Dt) noexcept;

    // Hot path: one relaxed load when no fishing chain is armed.
    bool PinsOn() const noexcept { return m_PinsOn.load(std::memory_order_relaxed); }

    // Any thread, before the engine passes the signal on.
    void OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept;
    void OnInputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept;

private:
    // The watched entities, by role.
    enum Role : size_t {
        kSpot,
        kTimer,
        kSelector,
        kCaught,
        kHands,
        kChoice0,
        kSpawner0 = kChoice0 + fr::kFishingChoices,
        kRoles = kSpawner0 + fr::kFishingChoices,
    };

    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context);
    bool Find(ZEntitySceneContext* p_Context, const char* p_Stage); // false: try again at the next stage
    void ReadItems();
    void Reset();
    std::optional<size_t> RoleOf(ZEntityType** p_Entity) const;
    void BeginRoll(const char* p_Source); // m_Mutex held
    void EndRoll(bool p_Done);            // m_Mutex held
    void Restore(const char* p_Why);      // m_Mutex held
    std::vector<std::optional<float>> ReadWeights() const;
    std::string WeightsText(const std::vector<std::optional<float>>& p_Weights) const;

    ProRepository& m_Repo;
    bool m_On = true;
    uint32_t m_Catches = 0; // this visit's catches (game thread and pins, under m_Mutex)

    // This load (game thread; the pin hooks read the atomics).
    bool m_Done = false;
    std::string m_Scene;
    std::atomic<bool> m_PinsOn {false};
    std::array<std::atomic<ZEntityType**>, kRoles> m_Entities {};
    std::array<uintptr_t, fr::kFishingChoices> m_WeightAddress {};
    std::array<std::string, fr::kFishingChoices> m_ItemText {};
    std::vector<fr::FishCatch> m_Items = std::vector<fr::FishCatch>(fr::kFishingChoices, fr::FishCatch::Other);
    bool m_ItemsRead = false;

    // Shared with the pin hooks.
    std::mutex m_Mutex;
    std::optional<fr::FishingRolls> m_Rolls;
    bool m_Playing = false;
    double m_Now = 0.0;                            // seconds of play
    std::vector<std::optional<float>> m_Originals; // the game's weights of the open steered roll
    bool m_Written = false;                        // m_Originals must be written back
    bool m_RetryNow = false;                       // Tick signals Pick (vanilla re-pick)
    std::string m_Summary;                         // one entry per catch this visit
};
