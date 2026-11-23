#pragma once

// Client requirements shuffle (client_reqs, on by default). A Freelancer campaign gives each mild location up to three
// optional client requirements, saved in the CPD as Mild<Loc>_ClientReq01..03
// (CpdSet). In the safehouse brick (snug/mission_vanilla.brick, the layout
// Freelancer Variations' HB addon copies), each location's
// ZEvergreenTerritoryStatusEntity "TerritoryStatus_<Loc>" lists in
// m_idsBonusRequirements its "<Loc> Req 0N" ValueInts, which the board reads;
// each mirrors the CPD helper's exposed "ClientReq0N" int through a
// "ValueInt_Changed" child (m_rValueEntity -> the CPD int; OnValue ->
// "ValueInt_Set" -> the mirror). The requirements themselves are
// ZEvergreenBonusRequirementDefinitionEntity entities (m_id,
// m_destinationBlacklist -> ZEvergreenDestination, m_crimeSectorBlacklist ->
// ZEvergreenCrimeSectorDefinition).
//
// ZEvergreenCampaignManager generates a campaign (OnGenerate) when the
// safehouse offers the next campaigns, but the game writes the CPD picks only
// when the player chooses one, which can be minutes later, or in a later
// session (the player quits at the selection screen). It then writes every
// location of the campaign in one burst (~10 ms, the
// ZEvergreenTerritoryDefinitionReceiverEntity signals). So
// after a generation this walks the safehouse brick once per safehouse visit
// (from 1 s after the generation or the visit's first frame update), reads
// every definition and location, caches the slots' entities (the CPD int, the
// board's mirror and the ValueInt_Changed between them; each mirror's slot is
// told by its entity id, fr::ClientReqMirrorSlot, since
// m_idsBonusRequirements is not in slot order) and then only re-reads those
// values: every second, and at the next frame update after any of the cached
// entities signals an output pin (SignalOutputPin hook). Once the picks have
// held for fr::kClientReqSettleMs it plans new picks
// (fr::PlanClientReqTypeCampaign, from the campaign seed and the chosen
// campaign type: in campaign mode one pool for the campaign, logged, that the
// locations pick from). With client_reqs on, it writes each changed slot into
// the CPD int (SetValue input pin, else the m_nValue property with change
// handlers), checks that the board's mirror followed (signals its SetValue if
// not), and then checks the next CpdSet carries the written values. For the
// rest of the visit it keeps reading the slots (fr::ClientReqWriteGuard): if
// the game writes them again, the plan is applied again once they settle.
//
// A generation not yet picked survives scene changes and is persisted
// ([campaign] client_reqs_pending, with the campaign seed), so a later visit
// or session still catches the pick. A watch carried over that way acts only
// on picks that appear while it watches (fr::ClientReqPickWatch); picks
// already there at its first read are left alone. The picks are drawn from
// the seed, never from the values in the CPD, so they cannot compound.
// Anything unreadable leaves the game's picks.
//
// Repair: once per campaign (until [campaign] client_reqs_checked holds the
// campaign seed), a safehouse visit with no pick awaited walks the brick and
// rewrites, from the same plan, every location holding an id the rules never
// allow (a prestige objective id >= 100, an excluded id).
//
// The campaign pool depends on the campaign type (the book's name,
// fr::ClientReqCampaignKey), so each book gets its own. Right after the walk
// it previews the pools for the current seed (logged, and in the overlay:
// DrawOverlay): the books on offer in their campaign order when the walk read
// every book's crime sector (ReadBooks), else all eight crime sectors by
// index, so the chosen book's pool is always among them. After the pick it
// reads the chosen type from the CPD_CrimeSectorName string the game sets, or
// CPDCrimeSectorIndex (fr::ResolveClientReqCampaignType), and shows what each
// location got.

#include "ClientReqs.h"
#include "Debounce.h"
#include "PinWatchTable.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

class ZDynamicObject;

class ClientReqShuffle {
public:
    // At config load. p_On writes the picks; off, they are only logged.
    void Configure(bool p_On, std::set<int32_t> p_Exclude, fr::ClientReqMode p_Mode, int32_t p_PoolSize);

    // The CpdSet check needs the sent-events hook.
    bool On() const { return m_On; }

    // At init: stores a generation whose picks are still awaited (0: none)
    // in [campaign] client_reqs_pending.
    void SetPersist(std::function<void(int32_t)> p_Persist);

    // At init: told the campaign seed when the player's campaign pick is seen
    // (the picks settled, or were already there at a carried-over watch's
    // first read). Runs whether or not client_reqs is on.
    void SetOnPicked(std::function<void(int32_t)> p_OnPicked) { m_OnPicked = std::move(p_OnPicked); }

    // At init: the persisted campaign seed (the campaign under way, or the
    // last generated one), the persisted [campaign] client_reqs_checked text,
    // and how to store it (a campaign whose locations hold only allowed ids;
    // 0 clears it).
    void SetCampaign(std::optional<int32_t> p_Seed, std::string p_CheckedText,
                     std::function<void(int32_t)> p_PersistChecked);

    // At init: a generation of an earlier session whose picks were never seen
    // (fr::ResumableClientReqSeed); watched at the next safehouse visit.
    void Resume(int32_t p_Seed) noexcept;
    bool Waiting() const { return m_Pending.has_value(); }

    // Game thread, from the OnGenerate detour: a campaign was generated with
    // this seed (empty or 0: unknown, nothing is planned).
    void OnCampaignGenerated(std::optional<int32_t> p_Seed) noexcept;

    // Game thread, at each loading stage: the watch starts 1 s after
    // eLoading_ScenePlaying (the generation fires before it).
    void OnLoadingStage(ESceneLoadingStage p_Stage) noexcept;

    // Game thread, every frame update.
    void Tick() noexcept;

    // Game thread, at a scene clear: the cached entities are dropped; a
    // generation not yet picked is watched again at the next safehouse visit.
    void OnSceneClear() noexcept;

    // SignalOutputPin hook (hot): one relaxed load when nothing is watched;
    // otherwise a lock-free pointer lookup that marks the values for a re-read.
    bool PinsOn() const noexcept { return m_PinsOn.load(std::memory_order_relaxed); }
    void OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept;

    // From the sent-events hook: checks the first CpdSets after a write.
    void OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept;

    // Render thread, inside the overlay window: the "Next campaign
    // objectives" section (each campaign type's list before the pick, each
    // location's objectives after it). Draws nothing before a generation.
    void DrawOverlay();
    // Render thread: DrawOverlay draws something.
    bool HasOverlay();

private:
    // One slot's entities.
    struct Slot {
        ZEntityType** mirror = nullptr;  // "<Loc> Req 0N", which the board reads
        ZEntityType** cpd = nullptr;     // the CPD int it mirrors
        ZEntityType** changed = nullptr; // the child that follows the CPD int (ValueInt_Changed)
        uint64_t cpdId = 0;              // the CPD int's entity id (one per slot for every location)
        std::string cpdWhy;              // why there is no CPD int
        std::optional<int32_t> mirrorValue;
        std::optional<int32_t> cpdValue;
    };

    struct Location {
        const fr::ClientReqLocation* known;
        ZEntityType** entity;
        int32_t destination;
        std::array<Slot, 3> slots;
        std::string notWritable; // non-empty: never written (why)
    };

    // One safehouse visit's walk: what the plan needs, and the entities whose
    // values are re-read.
    struct Watch {
        ZEntitySceneContext* context = nullptr;
        std::vector<fr::ClientReqDef> defs;
        std::vector<std::string> unreadable;           // definitions or blacklists not read
        ZEntityType** sectorNameHelper = nullptr;      // helper_evergreen_cpd_string_CPD_CrimeSectorName
        std::vector<ZEntityType**> sectorNameChildren; // its sub-entities
        std::vector<std::string> sectorNamesAtWalk;    // its strings at the walk (before the pick)
        ZEntityType** sectorIndexInt = nullptr;        // ValueInt_Set_CPDCrimeSectorIndex's target
        std::vector<Location> locations;
        // The books on offer as read at the walk (ReadBooks), and the
        // campaign types the last preview listed.
        std::vector<fr::ClientReqBook> books;
        std::set<std::string> previewed;
        fr::ClientReqPickWatch picks;
        std::chrono::steady_clock::time_point nextRead;
        std::chrono::steady_clock::time_point watchedSince;
        size_t entities = 0; // pin-watched entities
        // After the pick's writes: the campaign, its type and what the slots must hold.
        int32_t guardSeed = 0;
        fr::ClientReqTypeChoice type;
        std::optional<fr::ClientReqWriteGuard> guard;
        bool loggedGuardChange = false;
    };

    struct Pending {
        int32_t seed;
        bool thisVisit; // generated during the current safehouse visit
    };

    // Why Apply runs: the pick, a rewrite after the game changed the written
    // slots, or a repair of a campaign's invalid ids.
    enum class ApplyKind { Pick, Rewrite, Repair };
    struct ApplyOutcome {
        std::vector<int32_t> flat; // every slot after the writes
        bool complete = false;     // every campaign location holds only allowed ids
    };

    // Walks the safehouse brick into m_Watch; false when there is nothing to watch.
    bool Walk(ZEntitySceneContext* p_Context);
    // The campaign entities the walk found (entity, class): reads the books on
    // offer into m_Watch->books (fr::ReadClientReqBook on each book text
    // entity's properties).
    void ReadBooks(const std::vector<std::pair<ZEntityType**, std::string>>& p_Entities);
    // Re-reads every cached slot; false when a value that read before no longer does.
    bool ReadSlots(std::vector<int32_t>& p_Flat, std::string& p_Why);
    ApplyOutcome Apply(int32_t p_Seed, ApplyKind p_Kind);
    bool WriteSlot(const std::string& p_Key, Slot& p_Slot, int32_t p_Value);
    void TickPick(const std::vector<int32_t>& p_Flat, bool p_ByPin, uint32_t p_Pin);
    void TickGuard(const std::vector<int32_t>& p_Flat);
    void TickCheck(const std::vector<int32_t>& p_Flat);
    bool CheckDue() const;
    void MarkChecked(int32_t p_Seed);
    void PublishPins();
    void ClearPins() noexcept;
    void DropWatch() noexcept;
    void Finish(const char* p_Why, bool p_KeepWatch = false);
    void SetPending(int32_t p_Seed, bool p_ThisVisit);
    // The options every plan starts from (the config).
    fr::ClientReqOptions BaseOptions() const;
    // Previews each campaign type's pool for this seed (logged, overlay).
    void Preview(int32_t p_Seed);
    // The strings of the CPD_CrimeSectorName helper and its sub-entities.
    std::vector<std::string> ReadSectorNameStrings() const;

    // What the overlay shows, published by the game thread.
    struct OverlayType {
        std::string name; // the label
        std::vector<int32_t> pool;
    };
    struct OverlayLocation {
        std::string destination;
        std::vector<int32_t> picks;
        std::vector<std::string> notes; // empty: written (or unchanged)
    };
    struct Overlay {
        enum class Stage { None, Reading, Preview, Applied, Left } stage = Stage::None;
        int32_t seed = 0;
        std::vector<std::string> notes;
        std::vector<OverlayType> types; // Preview
        std::string type;               // Applied: the campaign's type
        std::vector<int32_t> pool;      // Applied
        std::vector<OverlayLocation> locations;
    };
    void PublishOverlay(Overlay p_Overlay);
    void SetOverlayStage(Overlay::Stage p_Stage, int32_t p_Seed, std::vector<std::string> p_Notes);
    // A collapsible heading inside the section, open by default, toggled
    // with the tap debounce; true when open.
    bool Fold(const std::string& p_Label, const std::string& p_Id, bool p_DefaultOpen);

    bool m_On = false;
    std::set<int32_t> m_Exclude;
    fr::ClientReqMode m_Mode = fr::kDefaultClientReqMode;
    int32_t m_PoolSize = fr::kDefaultClientReqPoolSize;

    std::function<void(int32_t)> m_Persist;
    std::function<void(int32_t)> m_PersistChecked;
    std::function<void(int32_t)> m_OnPicked;
    std::optional<int32_t> m_CampaignSeed;
    std::string m_CheckedText;
    bool m_CheckedThisVisit = false; // a check ran (or cannot run) this safehouse visit
    std::optional<Pending> m_Pending;
    std::optional<Watch> m_Watch;
    std::set<int32_t> m_DoneSeeds; // generations handled this session
    std::chrono::steady_clock::time_point m_NextWalk {};
    bool m_ScenePlaying = false; // eLoading_ScenePlaying seen since the last scene clear
    int m_WalksThisVisit = 0;    // walks (and failed reads) since the last scene clear
    bool m_LoggedNotSafehouse = false;
    bool m_LoggedPicksAppeared = false;

    // The pin-watched entities (value 1), written by the game thread with
    // m_PinsOn false, read by the pin hook.
    std::atomic<bool> m_PinsOn {false};
    std::atomic<bool> m_Dirty {false};
    std::atomic<uint32_t> m_DirtyPin {0};
    fr::PinWatchTable<1024> m_PinEntities;

    std::mutex m_OverlayMutex;
    Overlay m_Overlay;
    bool m_HeaderOpen = true;
    std::map<std::string, bool> m_FoldOpen; // render thread: Fold states by id
    fr::Debounce m_HeaderDebounce {fr::kTapDebounceSeconds};

    // Written values awaiting the CpdSet check, by CPD key: (game's pick, written).
    std::mutex m_VerifyMutex;
    std::map<std::string, std::pair<int32_t, int32_t>> m_Verify;
    int m_VerifyCpdSets = 0; // CpdSets seen without any of the keys
};
