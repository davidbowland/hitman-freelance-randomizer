#pragma once

// Safe spot log (log_safe_spots; on by default), log only: changes
// nothing in the game. In a Freelancer mission, the game picks one of the
// map's safe spots (instances of the safe setpiece) and a map variant's set
// dressing can bury it, so this logs, as "safe spots:" lines:
// - at eLoading_SceneStarted, every safe spot: its entity, brick, position
//   and whether it looks picked (core/SafeSpots.h says by which values);
// - at eLoading_ScenePlaying, the summary again;
// - after fr::kSafeCheckSeconds of play, every spot again with the summary
//   and, when the load applied an appended map variant (Variants::Applied),
//   the variant's placed entities within fr::kSafeNearRadius of each spot
//   (one line per spot); again after fr::kSafeLateCheckSeconds when nothing
//   looked picked at the first check.
// The safes are direct sub-entities of their bricks (all 1912 safe instances
// measured in game were), so one pass over each loaded brick's own
// sub-entities finds them; the variant's entities are its bricks' own
// sub-entities too (a template instance by its root entity's position).
// Neither pass descends into templates, so each takes a few milliseconds
// (logged). Every engine read is guarded.
// Positions: static set dressing keeps a stale (dirty), all-zero cached world
// transform well into play, so the in-play reports (fr::kSafeCheckSeconds and
// later) read world positions as ZSpatialEntity::GetObjectToWorldMatrix does:
// the engine refreshes a stale cache first (ReadPosition in EntityReads.h; it
// writes that cache and nothing else). The load-time reports read the cache
// only, and a stale one is "position unread". (0, 0, 0) is never a position.

#include "../core/SafeSpots.h"
#include "Variants.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <cstdint>
#include <string>
#include <vector>

class SafeSpotLog {
public:
    // At config load. Nothing runs when off.
    void Configure(bool p_On) { m_On = p_On; }

    // Game thread, from the SetLoadingStage detour, with the variant this
    // load applied. Catches and logs whatever its body throws.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                        const AppliedVariant& p_Variant) noexcept;

    // Game thread, every play-mode frame.
    void Tick(double p_GameSeconds) noexcept;

    // Game thread, at a scene clear: the load is forgotten.
    void OnSceneClear() noexcept;

private:
    // One spot found by the walk, with the entities its values live in.
    struct Spot {
        ZEntityType** entity = nullptr;
        ZEntityBlueprintFactoryBase* factory = nullptr; // the template's blueprint factory
        ZEntityType** route = nullptr;                  // fr::kSafeRouteId
        ZEntityType** state = nullptr;                  // fr::kSafeStateId
        ZEntityType** count = nullptr;                  // fr::kSafeCountId
        fr::SafeSpot read;
    };

    void Find(ZEntitySceneContext* p_Context);
    void FindMembers(Spot& p_Spot);
    // p_Refresh (in play only): stale world transforms are refreshed by the engine first.
    void Read(bool p_Refresh);
    void Report(const char* p_Moment, bool p_Spots);
    void Near(const char* p_Moment);

    bool m_On = true;

    // This load (game thread).
    bool m_Armed = false; // a Freelancer mission whose walk ran
    std::string m_Scene;
    std::vector<Spot> m_Spots;
    AppliedVariant m_Variant; // the variant this load applied, if any
    bool m_Playing = false;
    double m_Play = 0.0;        // s of play since scene playing
    bool m_Checked = false;     // the fr::kSafeCheckSeconds report ran
    bool m_LateChecked = false; // the late report ran, or is not needed (a spot looked picked)
};

// The placed entities of an appended map variant's bricks (the safe spot
// log's comparison set), read in
// play: stale world transforms are refreshed by the engine first, so game
// thread only. Empty for no variant or a full version. *p_Summary: what was
// read, for the log ("3 of 3 bricks loaded, ... placed ..., in 4.2 ms").
std::vector<fr::PlacedPiece> CollectVariantPieces(const AppliedVariant& p_Variant, std::string* p_Summary);
