#pragma once

// Sick spot steer (sick_spot_kills, on by default; core/SickSpots.h has the
// pure logic and what the game files say): emetic-poisoned NPCs prefer the
// vomit spots where 47 can kill them.
//
// At eLoading_ScenePlaying of a Freelancer mission, one walk over the loaded
// bricks (VisitAllEntities) finds every sick spot (an instance of either sick
// spot template, by its blueprint), the entities inside each spot, and every
// ZContextKillGuide. Positions are then read with the engine refreshing stale
// world transforms (ReadPosition, game thread). Each spot is classified
// (fr::ClassifySickSpot: in a toilet core, near a kill guide, plain), and on
// each normal-template kill spot the ZDramaSituationEntity's m_nPriority is
// raised by sick_spot_kill_boost through the engine's property setter
// (change handlers run) and read back. Bins (the high template) are left at
// the game's priority.
//
// Logged (Log: AI difficulty rolls) as "sick spots:" lines: one summary per
// load and one line per spot (name, template, blueprint, position, class,
// nearest kill guide, priority before -> after as read back). In play, the
// output pins of each spot's own entities that tell an NPC took it (the
// role's OnIActorChanged, the act's OnStart / OnReached / FXActivatePuke and
// the like) are logged with the spot's class and, when the pin carries the
// actor, the actor's name and distance to that spot, to the nearest kill spot
// and to the nearest plain one: whether the priority beats the distance.
// Every engine step goes through the guarded helpers of EntityWalk.h,
// EntityReads.h and ActorReads.h, or the guard pattern in Guard.h.

#include "../core/SickSpots.h" // the pure logic

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ZObjectRef;
struct STypeID;

class SickSpotSteer {
public:
    // At config load: the switch and the boost (already clamped).
    void Configure(bool p_On, int32_t p_Boost) {
        m_On = p_On;
        m_Boost = p_Boost;
    }

    // Game thread, from the SetLoadingStage detour. Catches and logs whatever its body throws.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;

    // Game thread, at a scene clear: the spots are forgotten (a restart finds them again).
    void OnSceneClear() noexcept;

    // Any thread: whether the output pin hook wants signals (one relaxed load).
    bool PinsOn() const noexcept { return m_PinsOn.load(std::memory_order_relaxed); }

    // Any thread, from the SignalOutputPin detour: a watched spot entity's pin is logged.
    void OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept;

private:
    struct Spot {
        ZEntityType** entity = nullptr;
        ZEntityBlueprintFactoryBase* factory = nullptr; // the template's blueprint factory
        uint64_t id = 0;
        uint64_t brickRid = 0;
        uint64_t blueprintRid = 0;
        fr::SickTemplate kind = fr::SickTemplate::None;
        bool inToiletCore = false;
        std::string name;                         // authored name, or the template's
        ZEntityType** act = nullptr;              // the act sub-entity (where the NPC throws up)
        ZEntityType** situation = nullptr;        // DramaSituation_SickSpot
        std::vector<ZEntityType**> dramaEntities; // ZDramaSituationEntity in the spot's tree
        std::optional<fr::SpotPoint> at;
        std::string atFrom; // which entity's position, or why none
        fr::SickVerdict verdict;
        std::string priority; // before -> after, for the spot line
    };

    // What the pin hook knows of a watched entity.
    struct Watch {
        size_t spot = 0;
        const char* part = "";
    };

    void Find(ZEntitySceneContext* p_Context);
    void Classify();
    void Raise(Spot& p_Spot, fr::SickTally& p_Tally);
    void Report(const fr::SickTally& p_Tally, double p_WalkMs);
    std::string SpotLabel(size_t p_Index) const;

    bool m_On = true;
    int32_t m_Boost = fr::kSickBoostDefault;

    // This load (written on the game thread before m_PinsOn is set, under m_Mutex).
    std::string m_Scene;
    std::vector<Spot> m_Spots;
    std::vector<fr::KillGuide> m_Guides;
    std::unordered_map<ZEntityType**, Watch> m_Watch;
    std::chrono::steady_clock::time_point m_PlayingAt {};
    const STypeID* m_ActorType = nullptr;

    // Game thread: the priorities raised in the current scene's loads, by
    // entity (a restart that keeps the entities is not raised again).
    std::string m_RaisedScene;
    std::unordered_map<ZEntityType**, fr::SickRaise> m_Raised;

    mutable std::shared_mutex m_Mutex;
    std::atomic<bool> m_PinsOn {false};
    std::atomic<size_t> m_PinLines {0};
};
