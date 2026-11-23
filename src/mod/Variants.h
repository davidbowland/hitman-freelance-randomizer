#pragma once

// Official map variants. In the LoadScene detour of a Freelancer mission whose
// map has variant options, picks one option or none per campaign seed and map
// (core/Variants.h) and appends the option's bricks to the scene's additional
// bricks, after checking that each one's resource is in a chunk. An option with
// a full version (core/LandslideFull.h) loads the mod's package instead
// (LandslidePackage.h): its own scene, or the option's bricks plus a fix brick,
// once its checks pass, and the normal map otherwise. Once the scene's entities
// exist, the variant's own story is kept out of Freelancer's mission: Diana's
// briefing and lines (core/DianaBriefing.h), its mission stories
// (core/MissionStories.h), map markers (core/VariantMarkers.h), timed damage
// (core/VariantDamage.h) and commentary (core/VariantComments.h) are muted, the
// NPCs it strands are switched off (VariantCleanup.h), the hero's start is
// fixed (HeroStart.h) and its exits run Freelancer's exit flow (VariantExits.h).
// A heavy option's target pool is checked at scene start and in play.

#include "../core/DianaBriefing.h"
#include "../core/Variants.h" // the pure logic (this header shares its name)
#include "BriefingGraphs.h"
#include "Config.h"
#include "HeroStart.h"
#include "LandslidePackage.h"
#include "Seed.h"
#include "VariantCleanup.h"
#include "VariantExits.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// A scene's additional bricks (SSceneInitParameters::m_aAdditionalBrickResources).
struct BrickList {
    std::vector<std::string> paths; // a truncated path ends in "..." and never matches
    size_t count = 0;               // as the array reports it
    uint32_t fault = 0;             // the first read's exception code, or 0
};

// The additional bricks' paths, read guarded (game thread).
BrickList ReadBricks(const TArray<ZString>* p_Bricks);

class ZActor;

// The map variant a load applied, for the features that depend on it (crate
// lamps, safe spots).
struct AppliedVariant {
    std::string scene;          // the contract's scene; empty: none applied
    std::string option;         // "holiday_hoarders"
    bool full = false;          // a full version: the whole scene is the variant's
    std::vector<uint64_t> rids; // an appended option's bricks' runtime resource ids
};

class Variants {
public:
    // Game thread, in the LoadScene detour after the mission's seed was
    // resolved (EarlyApply): for a Freelancer mission with official variants
    // and `variants` on, appends the pick's bricks to p_Parameters'
    // additional bricks. Publishes the pick for the overlay (cleared for
    // every other scene).
    void OnLoadScene(SSceneInitParameters& p_Parameters, std::string_view p_Scene, bool p_IsMission,
                     const std::optional<fr::SeedResolution>& p_Seed, const fr::Config& p_Config);

    // Game thread, at a restart without LoadScene: logs the brick list the
    // engine reuses (read only). The overlay keeps the load's pick.
    void OnRestart(const SSceneInitParameters& p_Parameters, std::string_view p_Scene, bool p_IsMission);

    // Game thread, first thing at every LoadScene: forgets the last pick (and its check).
    void ClearOverlay();

    // Game thread, from the SetLoadingStage detour: once the scene's entities
    // exist, mutes the variant's story (see above); at eLoading_SceneStarted of
    // a scene that loaded a heavy option, checks the target pool.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;

    // Game thread, at a scene clear: forgets the actors read (the pick's check
    // stays armed, so a restart checks again).
    void OnSceneClear() noexcept;

    // Game thread, every play-mode frame: the second check after
    // kVariantCheckPlaySeconds of play.
    void Tick(double p_GameSeconds) noexcept;

    // Render thread, inside the overlay window: the pick line, if any.
    void DrawOverlay();

    // Game thread: the scene this load swapped in (a full variant), or empty.
    std::string_view SwappedScene() const { return m_SwappedScene; }

    // Game thread: the variant this load applied (set with the pick, kept
    // over restarts, cleared at the next LoadScene).
    const AppliedVariant& Applied() const { return m_Applied; }

    // Init: the full variants' packages and checks.
    void SetLandslidePackage(LandslidePackage* p_Package) { m_LandslidePackage = p_Package; }

    // Init: pro.repo, for the start fix (HeroStart.h).
    void SetRepository(ProRepository* p_Repo) { m_HeroStart.SetRepository(p_Repo); }

    // The start fix's HeroSpawn_Location check (HeroStart.h): any thread.
    bool StartEventsWanted() const noexcept { return m_HeroStart.EventsWanted(); }
    void OnStartEvent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept {
        m_HeroStart.OnEventSent(p_EventIndex, p_Event);
    }

    // Hot path: one relaxed load while no briefing listener or Diana line is found.
    bool OutputPinsOn() const noexcept { return m_OutputPinsOn.load(std::memory_order_relaxed); }

    // An output pin of any entity: true to block it (a pin that starts Diana's
    // briefing or one of her other lines).
    bool OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept;

    // Hot path: one relaxed load while no Diana line of a variant is found.
    bool LinePinsOn() const noexcept { return m_LinePinsOn.load(std::memory_order_relaxed); }

    // An input pin of any entity: true to block it (the Trigger of one of
    // Diana's lines in the variant's bricks).
    bool OnInputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept;

    // The variant exit fix (VariantExits.h): hot path, one relaxed load while
    // no exit pin is armed; true from OnExitPin blocks the pin (a variant
    // exit's own contract end, replaced by Freelancer's).
    bool ExitPinsOn() const noexcept { return m_Exits.PinsOn(); }
    bool OnExitPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept {
        return m_Exits.OnOutputPin(p_Entity, p_PinId, p_Data);
    }

    // The variant exit fix's game events (exit_gate, EvergreenExitTriggered, the level's end): any thread.
    bool ExitEventsWanted() const noexcept { return m_Exits.EventsWanted(); }
    void OnExitEvent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept {
        m_Exits.OnEventSent(p_EventIndex, p_Event);
    }

private:
    // OnLoadScene's work; catches and logs whatever it throws.
    void LoadScene(SSceneInitParameters& p_Parameters, std::string_view p_Scene, bool p_IsMission,
                   const std::optional<fr::SeedResolution>& p_Seed, const fr::Config& p_Config);

    // A full variant that does not append (core/LandslideFull.h): runs the
    // checks and, when they pass, writes its crash sentinel and loads its own
    // scene with its package's bricks instead of the contract's (a
    // difficulty's brick of the contract is left out, logged). True when
    // swapped (logged and published). False leaves
    // p_Parameters as they were, after logging why; *p_Overlay then gets the
    // overlay text for the normal map, which plays instead (there is no
    // simple version).
    // When swapped, sets the scene alias (SceneOf.h): p_Scene, the contract's
    // scene, is what the rest of the mod counts the variant's scene as.
    bool TryFullVariant(const fr::FullVariant& p_Variant, SSceneInitParameters& p_Parameters, std::string_view p_Scene,
                        const BrickList& p_Before, bool p_Forced, std::string* p_Overlay);

    // A full variant's checks before its load: the gate, the contract (not
    // for an appending one: it keeps the contract's bricks), its chunk mounted
    // and the package's resources. False (logged) with *p_Overlay set to the
    // normal map's overlay when one fails; *p_Dropped gets the contract's
    // bricks the swap leaves out.
    bool CheckFullVariant(const fr::FullVariant& p_Variant, const BrickList& p_Before, bool p_Forced,
                          std::vector<std::string>* p_Dropped, std::string* p_Overlay);

    std::string m_SwappedScene;
    LandslidePackage* m_LandslidePackage = nullptr;
    const fr::FullVariant* m_FullPending = nullptr; // game thread: a full variant's load has not reached play yet
    const fr::FullVariant* m_FullLoaded = nullptr;  // game thread: the full variant this load swapped in or appended

    // One target-pool element the check reads.
    struct CheckEntry {
        ZEntityType** entity = nullptr;
        const ZActor* actor = nullptr;
        uint64_t id = 0;
        bool dedicated = false;
    };

    bool CheckResource(const fr::VariantBrick& p_Brick, std::string_view p_Scene, std::string& p_Why);
    void Publish(std::string p_Line);
    void LoadCheck(ZEntitySceneContext* p_Context);
    void PlayCheck();
    // Logs the summary and each problem; p_Moment is "scene start" or "10 s of play".
    void LogCheck(const char* p_Moment, bool p_Playing);

    // Game thread: the heavy option this load applied (empty: no check).
    std::string m_CheckScene;
    std::string m_CheckOption;
    std::string m_CheckBrick; // the mission brick to check when not the scene's own (a full variant's)
    bool m_LoadChecked = false;
    bool m_Watching = false; // the play check is still to come
    double m_Played = 0.0;
    std::vector<CheckEntry> m_Pool;

    // Diana's briefing in the option's bricks (core/DianaBriefing.h), found at
    // scene allocation (else scene start) from the blueprints copied while the
    // scene loaded (BriefingGraphs.h), and muted while this load applied the
    // option; kept across restarts (found again at each scene allocation).
    static constexpr size_t kMaxBriefings = 8;
    struct BriefingPin {
        std::atomic<ZEntityType**> entity {nullptr};
        std::atomic<uint32_t> pin {0}; // the output pin's id (CRC-32 of its name)
    };
    std::string m_BriefingScene;                         // game thread: armed when not empty
    const fr::VariantOption* m_BriefingOption = nullptr; // game thread; null for a full variant's swapped scene
    std::string m_BriefingName;                          // game thread: the option, or "<key> (full version)"
    // A full variant's swapped scene (TryFullVariant): its mission stories are switched off in every loaded
    // brick but the package's Freelancer layer (m_StoryKeepRids); Diana's briefing and lines only in the vanilla
    // bricks its swap appends after the package's (m_DianaRids, fr::FullDianaBricks: the full Bad Boy's
    // mission_caipirinha), as an appended variant's are. The scene's other bricks keep their Diana lines
    // (Freelancer's own load among them).
    bool m_StoriesOnly = false;            // game thread
    std::vector<uint64_t> m_StoryKeepRids; // game thread
    std::vector<uint64_t> m_DianaRids;     // game thread
    bool m_BriefingsFound = false;         // game thread: this allocation's search ran
    std::atomic<bool> m_BriefingPinsOn {false};
    std::array<BriefingPin, kMaxBriefings> m_BriefingPins {};
    std::atomic<bool> m_BriefingBlockLogged {false}; // the first block of this allocation was logged

    // Diana's other lines in the option's bricks (every DianaSpeak the
    // briefing search walks): they belong to the variant's own story (its
    // target escaping, eliminated, identified; its mission stories), never to
    // Freelancer's, so each one's Trigger input is blocked. Found with the
    // briefing; the labels are written only while no pin is armed.
    static constexpr size_t kMaxLines = 64;
    struct LinePin {
        std::atomic<ZEntityType**> entity {nullptr};
        std::atomic<bool> logged {false}; // its first block this allocation was logged
    };
    std::atomic<bool> m_LinePinsOn {false};
    std::array<LinePin, kMaxLines> m_LinePins {};
    std::array<std::string, kMaxLines> m_LineLabels; // "scenario_pomada \"Diana announces Pen escaping\" (...)"
    size_t m_LineCount = 0;                          // game thread: pins filled
    std::set<ZEntityType**> m_LineEntities;          // game thread: their entities

    // Game thread: arms one line's block; false when it is armed already or there is no room.
    bool AddLine(ZEntityType** p_Entity, const std::string& p_Label);

    // The same lines muted on the output side (connection-driven inputs never
    // reach OnInputPin): the output pin that feeds each one's Trigger is
    // blocked, and the signal is sent on along that pin's other links that
    // start more than Diana's speech (fr::FindLineMutes), so that logic runs.
    using Resend = briefing_graphs::Resend;
    struct LineOut {
        std::atomic<ZEntityType**> entity {nullptr};
        std::atomic<uint32_t> pin {0}; // the output pin's id (CRC-32 of its name)
        std::atomic<bool> logged {false};
        std::array<Resend, fr::kLineMaxResend> resend {}; // written before the pin is armed
        size_t resendCount = 0;
        bool line = false;    // it feeds a Diana line
        bool story = false;   // it feeds a mission story (core/MissionStories.h)
        bool comment = false; // it starts the variant's commentary (a hint, a sin's line) or feeds Sloth's vitality
                              // (core/VariantComments.h)
    };
    enum class LineOutKind { Line, Story, Comment };
    std::array<LineOut, kMaxLines> m_LineOuts {};
    std::array<std::string, kMaxLines> m_LineOutLabels;
    size_t m_LineOutCount = 0; // game thread
    std::atomic<bool> m_LineOutsOn {false};

    // Game thread, before m_LineOutsOn is set: arms one feeding pin's block (with the links it sends the signal
    // on along); false when there is no room. A pin armed already for another kind (a pin into both a Diana line
    // and a mission story) keeps only the links both send on, and both labels.
    bool AddLineOut(ZEntityType** p_Entity, const std::string& p_Pin, const std::vector<Resend>& p_Resend,
                    const std::string& p_Label, LineOutKind p_Kind = LineOutKind::Line);

    std::atomic<bool> m_OutputPinsOn {false}; // briefing pins or line outputs armed

    void ArmBriefings(std::string_view p_Scene, const fr::VariantOption& p_Option);
    // A full variant's swapped scene: its mission stories only (TryFullVariant).
    void ArmStories(std::string_view p_Scene, const fr::FullVariant& p_Variant);
    // p_LastChance: scene start (at scene allocation, a search with none of the option's bricks loaded waits).
    void FindBriefings(ZEntitySceneContext* p_Context, bool p_LastChance);
    void ForgetBriefingEntities();

    VariantCleanup m_Cleanup; // game thread only
    HeroStart m_HeroStart;    // game thread, except its event check
    VariantExits m_Exits;     // game thread, except its pin and event checks

    // The start fix for an appended option (its bricks' runtime resource ids),
    // and m_Applied.
    void ArmHeroStart(std::string_view p_Scene, const fr::VariantOption& p_Option);

    AppliedVariant m_Applied; // game thread only

    std::set<uint64_t> m_NotFound; // game thread only: brick RIDs the SDK listed in no chunk this session

    std::mutex m_OverlayMutex;
    std::string m_Overlay; // "Bangkok hot: the_source", or empty
};
