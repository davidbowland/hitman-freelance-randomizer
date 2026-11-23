#pragma once

// Night crate lamps (core/CrateLamps.h has the pure logic and the crate's
// lights): on a night load (a night map variant, fr::JudgeNightLoad), every
// Freelancer supply crate's lamp is dimmed so opening a crate no longer
// blinds. At eLoading_ScenePlaying of a Freelancer mission, one pass over
// each loaded brick's own sub-entities finds the crates (instances of
// setpiece_evergreen_stash_unique; in the game files Sapienza's sixteen are direct
// sub-entities of override_bingo), and each crate's center and environment
// lights, and its lamp core's DiffusePower when that is a property at run
// time, are lowered to their night caps through the engine's property setter
// (change handlers run) and read back. Then, every play-mode frame, each
// crate's four side lights are read raw: whatever the lamp core sends them at
// an open above the cap is set back to the cap at once, and about a second
// after a crate opens one line says what its lights show. Day loads change
// nothing (one line says so). Logged as "crate lamps:" lines (Log: map
// variants). Every engine step goes through the guarded helpers of
// EntityWalk.h and EntityReads.h.

#include "../core/CrateLamps.h" // the pure logic (this header shares its name)
#include "Variants.h"           // AppliedVariant

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class CrateLamps {
public:
    // Game thread, from the SetLoadingStage detour (after the variants), with
    // the variant this load applied. Catches and logs whatever its body throws.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                        const AppliedVariant& p_Variant) noexcept;

    // Game thread, every play-mode frame: the side lights held at the cap.
    void Tick(double p_GameSeconds) noexcept;

    // Game thread, at a scene clear: the crates are forgotten (a restart finds them again).
    void OnSceneClear() noexcept;

private:
    // A side light watched in play: its m_fDiffusePower's raw storage.
    struct Side {
        ZEntityType** entity = nullptr;
        uintptr_t address = 0; // 0: not watched (no raw float storage)
        float loaded = 0.0f;   // its value at scene playing
    };

    struct Crate {
        ZEntityType** entity = nullptr;
        ZEntityBlueprintFactoryBase* factory = nullptr; // the template's blueprint factory
        uint64_t id = 0;
        uint64_t brickRid = 0;
        // fr::CrateLampEntities' order: lamp core, center, environment, the four side lights.
        std::array<ZEntityType**, fr::kCrateLampEntityCount> parts {};
        std::array<Side, fr::kCrateSideLightCount> sides {};
        size_t held = 0;                // side light writes in play
        std::optional<double> openedAt; // play time the side lights first changed
        bool openLogged = false;
    };

    void Find(ZEntitySceneContext* p_Context);
    void Dim(const fr::NightVerdict& p_Night);
    void Hold();
    void LogOpen(size_t p_Index);

    std::string m_Scene; // the log's label of the scene
    std::vector<Crate> m_Crates;
    bool m_Watching = false; // a night load whose side lights are read in play
    double m_Play = 0.0;     // s of play since scene playing
};
