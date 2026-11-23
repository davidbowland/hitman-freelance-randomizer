#pragma once

// Merces payouts (payouts; pure part in core/PayoutScale.h and core/Merces.h),
// in Freelancer scenes (missions and the safehouse):
// - The payout table: found once per load at eLoading_SceneStarted by one walk
//   of the loaded bricks (the bricks that held it earlier this session first).
//   In missions no entity has the table's id, so entities whose sub-entity id
//   observation learned (the persisted prestige memory) count as entries.
//   Every child's m_nValue is read again at scene playing and after 5 s of
//   play to learn which entries get the prestige bonus.
// - payouts: at scene start each entry is written so the amount shown is
//   scaled by its own seeded factor (fr::RollPayoutFactor, fr::RolledPayout,
//   fr::PayoutJackpot). Without a known campaign seed nothing is written.
//   Listed in the overlay's Payouts section.
// - Prestige wall (safehouse): each card's display poll signal, and its copy,
//   is rewritten to what the objective's table entry shows
//   (fr::PlanDisplayWrite).

#include "Debounce.h"
#include "Merces.h"
#include "PayoutScale.h"
#include "Seed.h"
#include "EntityWalk.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class ZDynamicObject;
class ZObjectRef;
struct BrickList; // Variants.h

class MercesPayouts {
public:
    // Called when observation changed the prestige memory.
    using SaveMemory = std::function<void(const fr::PrestigeMemory&)>;

    // The campaign seed for the scene being started: in missions the seed the
    // dials settled for the scene, elsewhere generated > persisted (the live
    // seed entity reads 0 at scene start). A Fallback resolution means unknown.
    using SeedFor = std::function<fr::SeedResolution()>;

    // At config load: p_Mode (fr::ChoosePayoutMode of payouts) with its scale settings, the persisted memory
    // and how to save it, and the seed source for the scale. Nothing runs
    // when the mode is off. The entry points below catch everything they throw
    // (logged), so a failure never skips the calling detour's own work.
    void Configure(fr::PayoutMode p_Mode, fr::PayoutScaleConfig p_Scale, fr::PrestigeMemory p_Memory, SaveMemory p_Save,
                   SeedFor p_SeedFor);

    // Game thread, from the OnGenerate detour: a new campaign seed was
    // generated; the seeds remembered per scene are forgotten so the next
    // scene start scales with the new seed (the current scene keeps its amounts).
    void OnCampaignGenerated(int32_t p_Seed) noexcept;

    // Render thread: the overlay's Payouts section (nothing before a scale
    // was planned in this scene).
    void DrawOverlay();
    // Render thread: DrawOverlay draws something.
    bool HasOverlay();

    // Game thread, from the SetLoadingStage detour (before the original):
    // arms at eLoading_AssetsLoaded of a Freelancer scene, finds
    // (and writes) the table at eLoading_SceneStarted and reads it again at
    // eLoading_ScenePlaying.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;

    // Game thread, at a scene clear: forgets the scene's entities.
    void OnSceneClear() noexcept;

    // Game thread, every play-mode frame: p_GameSeconds of play passed; runs
    // the read after 5 s of play when due.
    void Tick(double p_GameSeconds) noexcept;

    // Game thread, after OnLoadingStage(eLoading_SceneStarted): how long the
    // table search of that call took, or empty when none ran; then forgets it.
    std::optional<double> TakeSearchMs() { return std::exchange(m_SearchMs, std::nullopt); }

    // Game thread, after OnLoadingStage(eLoading_SceneStarted): the amount the
    // player sees for each client requirement in this scene, by its objective
    // number (fr::IsClientReqId): this mod's write (payouts), else the
    // table's own value (vanilla). Empty when the table was not read: payouts
    // is off, the table was not found, or the scene has not started yet. For the random
    // loadout's order (fr::LoadoutInput::clientReqPayouts).
    std::map<int32_t, int32_t> ClientReqPayouts() const;

    // Any thread: whether OnOutputPin wants signals (one relaxed load).
    bool PinsOn() const { return m_PinsOn.load(std::memory_order_relaxed); }

    // Any thread, from the SignalOutputPin detour. Never throws.
    void OnOutputPin(const ZEntityRef& p_Entity, const ZObjectRef& p_Data) noexcept;

private:
    // How an instance was found, and so which of its children are entries.
    enum class FoundBy {
        TableId,          // its sub-entity id is fr::kPayoutTableId: every child
        RememberedParent, // its id is in m_TableParents: children with learned ids only
        LearnedChildren,  // the parent of children with learned ids: those children only
    };

    // One instance of the table template.
    struct Instance {
        ZEntityType** entity;
        ZEntityType** holder; // the entity the walk reached it from (null for LearnedChildren)
        uint64_t brickRid;    // for LearnedChildren, the brick its first learned child was reached from
        FoundBy foundBy;
    };

    // One child of an instance, as read at scene start.
    struct Child {
        ZEntityType** entity = nullptr;
        ZEntityType* type = nullptr;
        uint64_t id = 0;
        size_t instance = 0; // index in m_Instances
        std::string className {};
        bool hasValue = false;             // m_nValue is a raw int32 property, read at scene start
        bool readable = true;              // false after a read faulted
        int32_t start = 0;                 // m_nValue at scene start, before any write
        std::optional<int32_t> written {}; // this mod's value (written now, or found from an earlier load)
        bool wroteNow = false;             // written at this load's scene start, so start is the vanilla base
        fr::PayoutKind kind = fr::PayoutKind::Unknown;
        fr::TruncatedDraw roll {}; // the factor draw its write used (payouts)
        int32_t jackpot = 0;       // the jackpot its write was given (applied to normal entries only)
        int32_t scaleBase = 0;     // the base its factor was applied to (payout_same_base: its kind's shared base)
        std::optional<int32_t> objective {}; // fr::kObjectiveNumberPropertyId, read at scene start
    };

    // One display poll of the prestige wall (safehouse): its copy, and the
    // display entry it sits in (its owning entity), named by objective.
    struct DisplayEntry {
        ZEntityType** poll = nullptr;
        ZEntityType** copy = nullptr;  // the poll's m_rValueEntity
        ZEntityType** owner = nullptr; // the poll's owning entity: the display entry's root
        uint64_t ownerId = 0;
        std::string name; // the entry's name in the template holding it, or why not
        std::optional<int32_t> objective;
        std::string objectiveFrom;        // how the objective was named
        std::optional<int32_t> lastWrite; // this mod's last value for it this load
    };

    // What an objective's table entry holds: its vanilla base and this mod's write.
    struct DisplayAmounts {
        uint64_t entryId = 0;
        int32_t base = 0;
        std::optional<int32_t> written;
    };

    // A display entry id's objective, as named once this session.
    struct OwnerObjective {
        int32_t objective;
        std::string how;
        std::string name;
    };

    // This mod's last write for an entry id this session, and the vanilla
    // base it was computed from (fr::PlanPayoutWriteTo's lastBase).
    // The draw, jackpot and kind it used are shown for an entry kept from it.
    struct LastWrite {
        int32_t base;
        int32_t written;
        fr::TruncatedDraw roll;
        int32_t jackpot; // applied (0 unless normal)
        fr::PayoutKind kind;
        int32_t scaleBase; // the base the factor was applied to
    };

    // One overlay line: an entry's amount shown, vanilla and now.
    struct OverlayEntry {
        uint64_t id;
        std::optional<int32_t> objective; // fr::kObjectiveNumberPropertyId
        std::string name;                 // fr::PayoutObjectiveName: the objective's in-game name
        bool prestige;
        int64_t before;
        int64_t after;
        fr::TruncatedDraw roll; // the factor draw the amount came from
        int32_t jackpot;        // added to after (normal entries only), or 0
        int64_t from;           // the shown amount the factor was applied to when not the game's own (a shared base,
                                // or 1,000 for a cheaper normal entry), else 0
    };

    // What the overlay's Payouts section shows for one scene: its status
    // line and rows are replaced together, never one without the other.
    struct OverlaySnapshot {
        std::string scene;  // the scene resource the status and rows describe
        std::string status; // "", or the scale's one-line result for this scene
        std::vector<OverlayEntry> entries;
    };

    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context);
    void SceneClear();
    void FrameTick(double p_GameSeconds);
    void OutputPin(const ZEntityRef& p_Entity, const ZObjectRef& p_Data);
    void ForgetScene();
    void FindTable(const TArray<SBrickAllocationInfo>& p_Bricks, const BrickList& p_Additional);
    static std::set<uint64_t> NamedBricks(const BrickList& p_Additional);
    void LogInstances();
    void DropUntrustedInstances(size_t p_LearnedSize);
    void WriteTable(const std::string& p_Scene);
    std::optional<fr::SeedResolution> ScaleSeed(const std::string& p_Scene);
    void CheckLiveSeed(const char* p_Moment);
    std::optional<int32_t> Target(int32_t p_Base, const fr::TruncatedDraw& p_Roll, fr::PayoutKind p_Kind,
                                  int32_t p_Bonus, int32_t p_Jackpot) const;
    const char* Tag() const;
    bool Writes() const { return m_Mode != fr::PayoutMode::Off; }
    void PublishOverlay(OverlaySnapshot p_Snapshot);
    // A scale status with no rows (the amounts are vanilla), logged too.
    void PublishVanilla(const std::string& p_Scene, std::string_view p_Why);
    // One child's m_nValue read by Observe.
    struct Reading {
        Child* child;
        int32_t now;
        int32_t delta; // now minus this mod's write (or the scene-start value)
    };

    void Observe(const char* p_Moment);
    void CheckScaled(const char* p_Moment, std::span<const Reading> p_Readings);
    void FindDisplayEntries();
    std::optional<int32_t> ObjectiveOfOwner(ZEntityType** p_Owner, uint64_t p_OwnerId, std::string* p_How,
                                            std::string* p_Name);
    void CollectObjectiveNumbers(ZEntityType** p_Entity, std::vector<int32_t>& p_Out);
    std::optional<DisplayAmounts> DisplayAmountsOf(DisplayEntry& p_Entry, int32_t p_Shown, std::string* p_Why);
    std::optional<int32_t> DisplayPin(ZEntityType** p_Poll, const ZObjectRef& p_Data, int32_t p_Value);
    void CheckDisplayEntries(const char* p_Moment);
    // The table child's vanilla stored value and this mod's write (an
    // earlier load's for a kept table), or empty when it was not written.
    std::optional<std::pair<int32_t, int32_t>> BaseAndWritten(const Child& p_Child) const;
    void ResolveTypes();

    fr::PayoutMode m_Mode = fr::PayoutMode::Off;
    fr::PayoutScaleConfig m_Scale;
    fr::SharedPayoutBases m_Shared; // payout_same_base: this load's shared bases (game thread)
    SaveMemory m_Save;
    SeedFor m_SeedFor;

    // Game thread.
    fr::PrestigeMemory m_Memory;
    std::unordered_map<uint64_t, LastWrite> m_LastWritten; // per entry id: this mod's last write this session
    bool m_TypesResolved = false;
    size_t m_LastVisited = 0;          // the largest walk so far: the next walk's visited-set reserve
    std::set<uint64_t> m_TableBricks;  // runtime resource ids of bricks that held a table instance this session
    std::set<uint64_t> m_TableParents; // sub-entity ids of learned entries' parents found this session
    std::optional<double> m_SearchMs;  // the last table search's ms, until TakeSearchMs
    bool m_Armed = false;              // eLoading_AssetsLoaded (or the scene start) of this load was seen
    bool m_SceneHandled = false;       // the table was looked for in this load
    bool m_LateArmed = false;
    double m_LatePlay = 0.0;
    // The scale's seed: one per scene resource (a restart reuses it; forgotten
    // when another scene's table is written or a campaign is generated), the
    // one this load used, and the play time of the live-seed checks.
    fr::SceneSeedMemory m_ScaleSeeds;
    std::optional<fr::SeedResolution> m_UsedSeed;
    bool m_LiveSeedDiffers = false; // logged for this load
    double m_SeedCheckPlay = -1.0;  // play seconds since scene playing; < 0: not checking
    int m_SeedChecksDone = 0;
    std::vector<Instance> m_Instances;
    std::vector<Child> m_Children;
    // The prestige wall's entities the table walk passed (safehouse).
    std::vector<ZEntityType**> m_DisplayPolls;
    bool m_DisplayEntriesFound = false;
    std::unordered_map<uint64_t, OwnerObjective> m_OwnerObjectives; // display entry id -> objective (session)

    // The display entries, built by the game thread at scene start, used by
    // the pin hook (DisplayPin) and the checks; under m_DisplayMutex
    // (recursive: an engine write made while holding it could signal a pin
    // on the same thread).
    std::recursive_mutex m_DisplayMutex;
    std::vector<DisplayEntry> m_DisplayEntries;
    std::unordered_map<ZEntityType**, size_t> m_DisplayIndex; // poll -> entry
    std::map<int32_t, DisplayAmounts> m_DisplayAmounts;       // objective -> its table entry
    std::vector<fr::PrestigeBase> m_DisplayBases;             // the prestige objectives' vanilla bases
    int32_t m_DisplayBonus = 0;
    uint64_t m_DisplayPinLines = 0;
    std::map<std::string, size_t> m_DisplayCounts; // display poll signals by outcome

    // Any thread.
    std::atomic<bool> m_PinsOn {false};
    std::atomic<bool> m_DisplayArmed {false};       // DisplayPin rewrites the display polls' signals
    std::atomic<const void*> m_Int32Type {nullptr}; // STypeID* of int32

    // Render thread reads, game thread writes.
    std::mutex m_OverlayMutex;
    OverlaySnapshot m_Overlay;

    // Render thread only: the Payouts header's open state, forced each frame so
    // the Deck's second click of one tap cannot close it again.
    bool m_HeaderOpen = false;
    fr::Debounce m_HeaderDebounce {fr::kTapDebounceSeconds};
};
