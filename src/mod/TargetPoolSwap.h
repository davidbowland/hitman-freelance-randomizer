#pragma once

// Target pool shuffle (target_pool, on by default). In a
// Freelancer mild mission, at eLoading_SceneAllocated (the first stage whose
// bricks have entities; measured in game, the pool is there and
// unchanged at eLoading_SceneStarted, long before the first target event),
// replaces seeded shares of the target pool with other always-present scene
// actors in two tiers: civilians that the scene's mover table
// (core/MoverTable.h) lists as movers, onto non-guard slots (by default a
// share drawn per mission load from the scene seed, anywhere from none to
// all), then (target_pool_any_guard) any guard, onto a second, independently
// drawn share of the slots left; a scene without a table gets guards only.
// The write target is the ZVsTargetPicker in the mission brick (the
// C++ entity that picks, m_aActors at 0x2A8), cross-checked against the
// Helper_Evergreen_Spawn_Targets template's m_aActors (written too when it is
// a separate copy). Elements are overwritten in place: no allocation. The
// pool's stationary entries (fr::StationaryRule) are replaced beyond the
// shares while eligible actors last; the rest are dropped by moving each
// TArray's end down (kept entries from the tail fill their slots), only when
// m_aVolumes is empty and every header holds the list as read, never below
// fr::kMinTargetPoolLength. Anything unreadable, unexpected or faulting leaves
// the pool untouched. The selection is fr::PlanTargetSwaps (core/TargetPool.h).
// The full Landslide, the full Author and the full Icon count as mild
// Sapienza (SceneOf.h), with the package's mission brick; the full Landslide
// and the full Icon with their own scene's mover table (keyed by the scene
// as loaded), the full Author with mild Sapienza's (its day crowd)
// until its own is recorded. The full Marrakesh night counts as
// mild Marrakesh, with its own scene's mover table (the night level's
// crowds), and Kong Tuo-Kwang and Matthieu Mendola are never targets. The
// main story mission's targets at each location (fr::StoryTargets: the
// Washington twins at Sgail, ...) are never targets, whatever is loaded.

#include "TargetPool.h"
#include "Seed.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class TargetPoolSwap {
public:
    // At config load. Off (or with no known seed), nothing is swapped in, but
    // the people who can never be targets in a mild mission (the main story
    // mission's targets there, a loaded map variant's own targets, the NPCs
    // it strands or shuts in: fr::TargetPoolExclusions) are still taken out
    // of its pool.
    // Each mission load draws both tiers' shares between p_ShareMin and
    // p_ShareMax (target_pool_swap_min, _max). p_AnyGuard adds the guard tier
    // (target_pool_any_guard).
    void Configure(bool p_On, double p_ShareMin, double p_ShareMax, bool p_AnyGuard);

    // Game thread, from the SetLoadingStage detour (before the original). p_Seed
    // is the mission's seed resolved before the scene loaded. Catches and logs
    // whatever its body throws.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                        const std::optional<fr::SeedResolution>& p_Seed) noexcept;

    // Game thread, at a scene clear: forgets the load (not what was written:
    // a rebuilt pool holding it again would be left alone).
    void OnSceneClear() noexcept;

    // On (or taking people out of the pool), and not yet done with this load
    // (it tries again at a later loading stage). Read after OnLoadingStage in
    // the same stage.
    bool WaitingForLaterStage() const noexcept { return (m_On || m_ExclusionsOnly) && !m_Done; }

private:
    // One TArray<TEntityRef<ZActor>> m_aActors, read raw.
    struct RawElement {
        ZEntityType** entity = nullptr;
        void* actor = nullptr; // the TEntityRef's interface pointer (ZActor*)

        bool operator==(const RawElement&) const = default;
    };

    struct RawPool {
        ZEntityType** owner = nullptr;
        uint64_t ownerId = 0;
        std::string ownerClass;
        uintptr_t property = 0; // the TArray's own address
        uintptr_t elements = 0; // its first element
        std::vector<RawElement> items;
        std::string why; // empty when read
    };

    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                      const std::optional<fr::SeedResolution>& p_Seed);
    // False when the bricks have no entities yet (tried again at the next stage).
    // p_Swaps false: no swaps, only the people who can never be targets here are taken out.
    bool Run(ZEntitySceneContext* p_Context, const fr::SeedResolution& p_Seed, const char* p_Stage, bool p_Swaps);
    RawPool ReadPool(ZEntityType** p_Owner) const;
    // Hot missions and showdowns: logs every picker's pool (no swap). False when the bricks have no entities yet.
    bool LogOtherPools(ZEntitySceneContext* p_Context, const char* p_Stage);

    bool m_On = false;
    fr::ShareRange m_Shares {fr::kDefaultTargetPoolSwapMin, fr::kDefaultTargetPoolSwapMax};
    bool m_AnyGuard = fr::kDefaultTargetPoolAnyGuard;

    // Per load.
    bool m_Done = false;           // ran (wrote or decided not to) this load
    bool m_ExclusionsOnly = false; // this load runs without swaps (taking people out only)

    // The last list this mod wrote (entity pointers in pool order), across
    // loads: a pool found holding exactly it was not rebuilt and is left alone.
    std::vector<ZEntityType**> m_LastWritten;
};
