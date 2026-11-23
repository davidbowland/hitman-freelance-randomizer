#pragma once

// The variant exit fix (core/VariantExits.h): a map variant's own exits run
// Freelancer's exit flow, and Freelancer's exits whose sequence the variant
// broke still end the level. Armed whenever a map variant loads (an appended
// option; a full version's scene is only listed: its package handles exits).
// At eLoading_SceneAllocated (again at scene start when nothing was found)
// every loaded brick is walked for Freelancer exits (each exit_evergreen_*
// instance's Helper_Evergreen_MissionExitHijacker) and for contract exits of
// the variant's bricks (a ZContractsExitEntity outside a Freelancer exit).
// Each exit's pins come from its template's blueprint links (copied while the
// scene loaded, BriefingGraphs.h) or, for the templates decoded from the game
// files, from its sub-entity ids. One "variant exits:" line lists every exit
// with its template, repository id, position, brick and what was done; with
// no Freelancer exit, a loud line says the variant's exits were left on.
// Then (OnOutputPin): a routed variant exit's start sends the Freelancer
// exit's hijacker exitActivated, and the pins that feed its contract end are
// blocked (the signal sent on along their other links) and replaced by the
// hijacker's sequenceDone; a confirmed Freelancer exit gets its sequenceDone
// kExitGapSeconds later when the variant deletes its sequence, or after
// kExitEndWaitSeconds of play when the level has not ended by then. The game
// events exit_gate (which exit), EvergreenExitTriggered (Freelancer's flow
// ran) and ContractEnd / ExitInventory (the level ends: nothing more is sent)
// are logged. A Freelancer exit whose getaway the loaded bricks delete
// (fr::FindExitSwitchOff: Sapienza's road car under The Guru's brick) is
// switched off at eLoading_ScenePlaying, when the exits' logic runs: its
// template's SetFalse pins (fr::KnownExitTemplate::offs) are signaled, as the
// game does once an exit was used, and each value is read back (SetProperty
// when the pin did not take); one "variant exits:" line says so. Every raw
// engine step follows the guard pattern in Guard.h.

#include "../core/VariantExits.h" // the pure logic (this header shares its name)

#include "../core/DianaBriefing.h" // kLineMaxResend
#include "BriefingGraphs.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class ZDynamicObject;

class VariantExits {
public:
    // Game thread, in the LoadScene detour, when a map variant was applied:
    // p_Scene is the scene that loads, p_Option the variant's name (for the
    // log and fr::KnownBrokenExit), p_VariantBricks the runtime resource ids
    // of the bricks it appended; p_Route false (a full version): exits are
    // listed only.
    void Arm(std::string_view p_Scene, std::string_view p_Option, std::vector<uint64_t> p_VariantBricks, bool p_Route);

    // Game thread, first thing at every LoadScene.
    void Forget() noexcept;

    // Game thread, from the SetLoadingStage detour.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;

    // Game thread, at a scene clear: a restart allocates the scene again
    // (found and armed again; the arming stays).
    void OnSceneClear() noexcept;

    // Game thread, every play-mode frame: a due sequenceDone.
    void Tick(double p_GameSeconds) noexcept;

    // Hot path: one relaxed load while no exit pin is armed.
    bool PinsOn() const noexcept { return m_PinsOn.load(std::memory_order_relaxed); }

    // An output pin of any entity: true to block it (a variant exit's
    // contract end, replaced by Freelancer's).
    bool OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept;

    // Hot path: one relaxed load while no exit is armed.
    bool EventsWanted() const noexcept { return m_EventsWanted.load(std::memory_order_relaxed); }

    // Any sent event: exit_gate, EvergreenExitTriggered, the level's end.
    void OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept;

private:
    // One exit found at scene allocation.
    struct Exit {
        ZEntityType** root = nullptr; // the template instance (or, for a contract exit placed in a brick, the brick)
        ZEntityBlueprintFactoryBase* factory = nullptr; // the blueprint factory its parts are sub-entities of
        ZEntityType** anchor = nullptr; // the hijacker (Freelancer) or the ZContractsExitEntity (variant)
        int anchorIndex = -1;
        uint64_t anchorId = 0;
        uint64_t rootId = 0;      // the instance's entity id in its brick (0 when unread or the brick itself)
        uint64_t templateRid = 0; // the template's blueprint resource id (0: in a brick)
        uint64_t brickRid = 0;
        bool inVariantBrick = false;
        std::string name;     // the anchor's (or instance's) authored name, when read
        std::string repo;     // the exit's repository id (its ZRepositoryItemEntity's m_sId), or empty
        std::string where;    // "(x, y, z)" or why not
        std::string pinsFrom; // "blueprint links", "known template ids", or why not
        std::string broken;   // Freelancer: why its sequence is gone under this variant (fr::KnownBrokenExit)
        std::string
            switchOff; // Freelancer: why it is switched off under the loaded bricks (fr::FindExitSwitchOff), or empty
        std::vector<std::pair<ZEntityType**, fr::KnownExitPin>> offs; // its switches found (switchOff set)
        std::string offsFrom;                                         // why none were found (switchOff set, offs empty)
        size_t watched = 0; // Freelancer: confirm pins armed; variant: start pins armed
        size_t blocked = 0; // variant: end pins armed
        fr::VariantExitFacts facts;
        fr::VariantExitAction action = fr::VariantExitAction::KeepUnreadable;
    };

    enum class PinKind : uint8_t { FreelancerConfirm, VariantStart, VariantEnd };

    using Resend = briefing_graphs::Resend;

    static constexpr size_t kMaxPins = 64;

    struct WatchPin {
        std::atomic<ZEntityType**> entity {nullptr};
        std::atomic<uint32_t> pin {0};
        PinKind kind = PinKind::FreelancerConfirm; // written before the pin is armed
        size_t exit = 0;                           // index into m_Freelancer or m_Variant
        std::array<Resend, fr::kLineMaxResend> resend {};
        size_t resendCount = 0;
        std::string label; // "Bellini Exit 47eb3b2d1859c514: ValueBool_Poll_IsExitOnManualAllowed PollTrue"
        std::atomic<bool> logged {false};
    };

    void Find(ZEntitySceneContext* p_Context, bool p_LastChance);
    // The exit's repository id, position and name, for the log.
    void Describe(Exit& p_Exit);
    // Arms a pin of exit p_ExitIndex; false when there is no room or no entity.
    bool AddPin(PinKind p_Kind, size_t p_ExitIndex, ZEntityType** p_Entity, const std::string& p_Pin,
                const std::string& p_Label, const std::vector<Resend>& p_Resend);
    std::string Label(const Exit& p_Exit, bool p_Freelancer) const;
    // The exits switched off under the loaded bricks (at scene playing).
    void SwitchOff();
    // Sends the steps to the hijacker of m_Freelancer[p_Via] (outside the lock).
    void Send(const fr::ExitSteps& p_Steps, size_t p_Via, const std::string& p_Why);
    void ForgetPins() noexcept;

    // Game thread.
    std::string m_Scene; // empty: not armed
    std::string m_Option;
    std::vector<uint64_t> m_VariantBricks;
    bool m_Route = false;
    bool m_Done = false;    // this allocation's search ran
    bool m_OffDone = false; // this allocation's switch-off ran

    std::vector<Exit> m_Freelancer;
    std::vector<Exit> m_Variant;
    fr::VariantExitPlan m_Plan;

    std::atomic<bool> m_PinsOn {false};
    std::array<WatchPin, kMaxPins> m_Pins {};
    size_t m_PinCount = 0; // game thread, before m_PinsOn

    // The exit in progress (any thread: pins, events, frames).
    std::mutex m_Mutex;
    fr::ExitClock m_Clock;        // under m_Mutex
    double m_Played = 0.0;        // under m_Mutex: seconds of play since the search
    bool m_EvergreenExit = false; // under m_Mutex: EvergreenExitTriggered seen
    bool m_EndLogged = false;     // under m_Mutex
    std::atomic<bool> m_EventsWanted {false};
};
