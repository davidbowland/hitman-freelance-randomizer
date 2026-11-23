#pragma once

// Stranded NPCs out of the scene (the variant cleanup). A map variant can
// strand scene NPCs: The Reflection's deletes leave two guards by the cable
// car exit standing on nothing. Each option lists them
// (fr::VariantOption::removed); they are never targets (the target pool swap takes them out of Freelancer's pool), and at
// eLoading_SceneAllocated (after the target pool swap) every listed NPC of the
// options whose bricks are all loaded gets m_bStartEnabled false, the way the
// game keeps an actor out of a scene. Freelancer's target pool (the mission
// brick's ZVsTargetPicker lists) is read first: one still in it is left in,
// and with the pool not found or not fully read nobody is switched off (a
// showdown has no picker: its targets are the mission brick's own). Read back
// at scene playing. Logged as "variant cleanup:" lines. Never touches the full
// Landslide's day crowd (core/VariantCleanup.h). Every raw engine step
// follows the guard pattern in Guard.h.

#include "../core/VariantCleanup.h" // the pure logic (this header shares its name)

#include <Glacier/ZScene.h>

#include <string>
#include <string_view>
#include <vector>

class ZActor;

class VariantCleanup {
public:
    // Game thread, in the LoadScene detour, when a variant option was applied
    // to p_Scene (and its restarts); logs what p_Option removes, if anything.
    void Arm(std::string_view p_Scene, const fr::VariantOption& p_Option);

    // Game thread, first thing at every LoadScene.
    void Forget() noexcept;

    // Game thread, from the SetLoadingStage detour (after the target pool's).
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;

    // Game thread, at a scene clear: a restart allocates the scene again
    // (switched off again; the arming stays).
    void OnSceneClear() noexcept;

private:
    // One NPC switched off: read back at scene playing.
    struct SwitchedOff {
        std::string label; // "reflection: \"Hidaka Uno\""
        ZEntityType** entity;
        const ZActor* actor;
    };

    void Clear(ZEntitySceneContext* p_Context);
    void Check(const char* p_Moment);

    std::string m_Scene; // empty: not armed
    bool m_Done = false; // this allocation's cleanup ran
    std::vector<SwitchedOff> m_SwitchedOff;
};
