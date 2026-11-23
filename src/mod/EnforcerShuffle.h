#pragma once

// Enforcer shuffle (enforcers, on by default). An
// enforcer is an actor whose ZActor::m_aEnforcedOutfits (0x480,
// TArray<TEntityRef<ZOutfitProfessionEntity>>) lists professions: it sees
// through the outfits of those professions. Measured in game: the
// arrays are filled from the bricks by eLoading_SceneAllocated, unchanged through
// play, and the active/potential enforcer bits set during play only on actors
// with a non-empty array (they are derived). So in a Freelancer mild mission or showdown,
// at eLoading_SceneAllocated (eLoading_SceneStarted when the bricks had no
// entities yet, or no actor had a list yet, as on a restart), this moves whole
// lists between actors of the same actor type (same brick and outfit first, see
// fr::PlanEnforcerShuffle) by swapping the arrays' 24-byte headers (begin, end, allocation
// end or inline flags) of an enforcer and a non-enforcer: no allocation, no
// element is changed, and the number of enforcers stays exactly the same.
// Role actors, crowd and mission brick actors are never touched; possible
// targets, start-disabled actors and sentries take part unless their setting
// (enforcers_include_targets / _late / _sentries) is off. Start-disabled
// actors are handled at load like the rest (their lists are readable then,
// and a swapped list survives activation), only ever with another
// start-disabled actor as partner. Selection: fr::PlanEnforcerShuffle
// (core/Enforcers.h), seeded by the dials' scene seed; with no known seed
// nothing changes. In a showdown the mission brick is mission_hot_<game>.brick,
// which holds the suspects, lookouts and assassins, so they are never touched.

#include "Enforcers.h"
#include "Seed.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class ZActor;

class EnforcerShuffle {
public:
    // At config load. Nothing runs when off.
    // p_ShareMin, p_ShareMax: the least and most share of enforcers moved (enforcers_share_min, _max);
    // p_AnyBrick: lists may move to another brick's actors (enforcers_any_brick);
    // p_Include: which otherwise rejected actors take part (enforcers_include_*).
    void Configure(bool p_On, double p_ShareMin, double p_ShareMax, bool p_AnyBrick,
                   const fr::EnforcerInclude& p_Include);

    // Game thread, from the SetLoadingStage detour, after the target pool
    // shuffle (so it sees the pool as written). p_TargetPoolWaiting: the
    // target pool shuffle will try again at a later stage; then this waits
    // too (at eLoading_SceneAllocated only), so the possible targets it reads
    // are final. Catches and logs whatever its body throws.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                        const std::optional<fr::SeedResolution>& p_Seed, bool p_TargetPoolWaiting) noexcept;

    // Game thread, at a scene clear: forgets the load (not what was written).
    void OnSceneClear() noexcept;

    // An m_aEnforcedOutfits header, raw.
    struct Header {
        uint64_t q[3] {};

        bool operator==(const Header&) const = default;
    };

private:
    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                      const std::optional<fr::SeedResolution>& p_Seed, bool p_TargetPoolWaiting);
    // False when the bricks have no entities yet (tried again at the next stage).
    bool Run(ZEntitySceneContext* p_Context, const fr::SeedResolution& p_Seed, const char* p_Stage);

    bool m_On = false;
    fr::ShareRange m_Shares {fr::kDefaultEnforcerShareMin, fr::kDefaultEnforcerShareMax};
    bool m_AnyBrick = fr::kDefaultEnforcerAnyBrick;
    fr::EnforcerInclude m_Include;
    bool m_Done = false; // ran (wrote or decided not to) this load

    // Across loads: the non-empty headers this mod wrote, by actor entity
    // (`entity` received the list `from` had). A scene whose actors still hold
    // them was not rebuilt and is left alone.
    struct Written {
        ZEntityType** entity;
        Header header;
        ZEntityType** from;
        size_t count; // the list's count (`from` held it, then 0)
    };
    std::vector<Written> m_LastWritten;
};
