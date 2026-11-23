#pragma once

// Sensor dials (core/SensorDials.h): in a Freelancer mission with dials and
// sensor_dials on, the professional difficulty's shared sensor gets the
// mission's rolled values while the scene loads. The sensor is found the way
// Freelancer reaches it: the difficulty collection whose enable condition is
// ZDifficultyCondition eDL_NORMAL (the professional "Default"), its
// m_rSharedSensor (a ZSharedSensorDef, 6b6889480b488495),
// in the globaldata brick. Plain floats go through the engine setter
// (WriteFloatProperty); the focus-cone ranges are raw float writes into the
// m_VisibilitySetting array's elements, located through the element type's
// reflection (m_eSensitivity, m_fFocusConeRange offsets and the enum's names),
// and only when every element reads as a known sensitivity with a sane range.
//
// Written at eLoading_SceneAllocated (the earliest the entities exist), else
// at eLoading_SceneStarted. Every write is read back at once, again at scene
// start, scene playing, and 5 s and 60 s into play ("sensor dials check").
// The originals are written back when the scene is cleared (the sensor is
// shared by every professional mode, not just Freelancer), only where the
// value still holds what was written.
//
// Not yet confirmed in game: whether the AI reads these values after the
// scene has loaded (the read-backs show only that they hold).

#include "Dials.h"
#include "../core/SensorDials.h" // the pure logic (this header shares its name)

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

class SensorDials {
public:
    // What this load gets, from the main mod's state at each stage.
    struct Plan {
        bool write = false; // a Freelancer mission with dials and sensor_dials on
        int32_t seed = 0;
        std::string scene;
        fr::RollConfig roll;
        std::set<std::string> off;
    };

    // One dial written (or tried) this load.
    struct Row {
        const fr::SensorDial* dial;
        double found; // the sensor's value before the write
        double base;  // vanilla
        double multiplier;
        float written;
        bool ok; // written and read back
        int attempts;
        bool fellBack;
        std::string status; // "written", "write failed", then the latest check
        bool held = true;   // the latest check read back what was written (true before any check)
    };

    // Game thread, from the SetLoadingStage detour (after the main mod's own
    // handling). Returns true when the rows changed (for the overlay).
    bool OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context, const Plan& p_Plan) noexcept;

    // Game thread, every play-mode frame: the 5 s and 60 s checks. True when the rows changed.
    bool Tick(double p_Seconds) noexcept;

    // Game thread, at a scene clear (and at mod unload): writes back the
    // originals that still hold what was written, then forgets the load.
    void Restore(const char* p_When) noexcept;

    // Game thread, at LoadScene: a load with values still outstanding (no
    // clear came) forgets them unwritten (their entity may be gone).
    void OnLoadScene() noexcept;

    const std::vector<Row>& Rows() const { return m_Rows; }
    const std::string& Summary() const { return m_Summary; }

private:
    // One value written: a sensor float (by name) or a visibility element's cone range (by address).
    struct Written {
        size_t row;
        ZEntityType** entity; // plain float
        uintptr_t address;    // cone range (0 for a plain float)
        float original;
        float written;
    };

    bool LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context, const Plan& p_Plan);
    bool PlayChecks(double p_Seconds);
    bool Apply(ZEntitySceneContext* p_Context, const Plan& p_Plan, const char* p_Stage);
    void Check(const char* p_When);
    bool ReadCurrent(const Written& p_Written, float* p_Out) const;
    void Reset() noexcept; // forgets the load

    bool m_Done = false; // decided for this load (written, or nothing to write)
    bool m_Playing = false;
    double m_Play = 0.0;
    bool m_Did5 = false;
    bool m_Did60 = false;
    ZEntityType** m_Sensor = nullptr;
    std::vector<Row> m_Rows;
    std::vector<Written> m_Written;
    std::string m_Summary;
};
