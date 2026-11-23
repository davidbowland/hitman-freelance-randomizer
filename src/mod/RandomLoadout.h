#pragma once

// Random loadout (random_loadout; the plan in core/Loadout.h), the lockpick
// grant (lockpick_grant; core/LockpickFree.h) and the item grants
// (core/ItemGrants.h), with the always-on state they need.
//
// State: every CpdSet and gear slot event of a Freelancer scene is merged into
// fr::LoadoutState (PersistentItems: the gear wall; TransientItems: the
// Freelancer tools; InventoryItems: what 47 packs; OptinPayoutObjective), kept
// in mods/freelancerrandomizer-loadout.txt so a game restart between the
// safehouse and the mission keeps it. A mission load freezes it as the
// departure snapshot (a restart keeps the snapshot).
//
// Plan and delivery: the plan (fr::PlanLoadout) is made at eLoading_SceneStarted,
// once the payout table is written, so the client requirements go highest
// payout first. Once the scene plays, the game's "Inventory items" list has
// given the packed items and random_loadout_delay has passed, each planned
// item 47 does not carry is created in his inventory with
// ZCharacterSubcontrollerInventory::CreateItem, with the create type the game
// used for a packed item. ItemPickedUp confirms it (the world inventory scan
// is a second source; it reads no items in the current game). A Freelancer
// tool is then removed from the tools list; a gear wall item is lent
// (fr::WallLoans): from EvergreenExitTriggered until the mission's CpdSet it is
// taken out of "Inventory items" and put back on "Persistent items"
// (fr::PlanWallReturn), so it never leaves the wall.
// - A large holsterable item (fr::IsPocketDeliveryItem) is spawned with
//   ZWorldInventory::RequestNewItem and handed to "HeroItemAction".SetItem, the
//   game's own pocket call; if the game will not pocket it, it is destroyed
//   and created with CreateItem (it may then arrive in 47's hands).
// - The briefcase item is spawned the same way and handed to "Purchased item
//   actions".SetItem, the game's path for a large item bought in a mission:
//   it arrives in an ICA Briefcase in 47's hands. A briefcase in his hands
//   ends a blend or an act and can leave him stuck, so while his state bits
//   (ZHitman5+0xE28) say he is in one, it is held (fr::JudgeBlendHold).
// - A pending RequestNewItem calls back into this object (a member delegate);
//   unloading the mod in the seconds before it arrives is not supported.
//
// Grants: once the scene plays and the loadout has created its items
// (fr::DecideGrantStart), 47's items are counted by the item ledger
// (fr::HeroItemLedger, from the packed items, the mod's CreateItems and the
// game's item events); a lockpick (or each asked grant item) he lacks is
// created with the same CreateItem. With slow_lockpick, and for the item
// grants, the given ids are kept out of "Inventory items": the SignalInputPin
// hook blocks their pushes and removes, and from the exit until the mission's
// CpdSet the mod sends a RemoveRepositoryId of each twice a second. Each
// mission CpdSet is checked for them (nails are not guarded).

#include "PeImage.h"
#include "ItemGrants.h"
#include "Loadout.h"
#include "LockpickFree.h"
#include "Repository.h"
#include "Seed.h"
#include "WallLoans.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZPrimitives.h>
#include <Glacier/ZString.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class ZDynamicObject;
class ZWorldInventory;
class IItemBase;
struct STypeID;

class RandomLoadout {
public:
    explicit RandomLoadout(ProRepository& p_Repo) : m_Repo(p_Repo) {}

    // At config load.
    void Configure(bool p_On, double p_DelaySeconds, bool p_Briefcase, bool p_LockpickFirst, bool p_ObjectiveItems);
    bool On() const { return m_On; }

    // Init: reads the state file.
    void LoadState() noexcept;

    // The amount the player sees for each client requirement in this scene
    // (objective number -> amount; MercesPayouts::ClientReqPayouts), asked on
    // the game thread when the plan is made, at eLoading_SceneStarted (after
    // the payout table was written). Empty: unknown.
    using ClientReqPayoutsFor = std::function<std::map<int32_t, int32_t>()>;
    // At init. Without it every payout is unknown.
    void SetClientReqPayouts(ClientReqPayoutsFor p_Source) { m_PayoutsFor = std::move(p_Source); }

    // Game thread: a scene loads (LoadScene, or a restart's eLoading_AssetsLoaded).
    void OnSceneLoad(const std::string& p_Scene, const std::string& p_Type,
                     const std::optional<fr::SeedResolution>& p_Seed, const char* p_When) noexcept;

    // Game thread, from the SetLoadingStage detour.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;

    // Game thread, at a scene clear.
    void OnSceneClear() noexcept;

    // OptinPayoutObjective as the CpdSets merged so far left it (the live
    // state), or empty when none carried it. For the prestige shuffle's log.
    std::optional<int32_t> LiveOptinObjective();

    // The items the CPD says the player owns as the CpdSets merged so far
    // left them (the live state): PersistentItems (the gear wall),
    // TransientItems (the Freelancer tools), InventoryItems (packed) and
    // AwardedItems, or empty when none was seen.
    // For crate_items: no item listed here is offered by a crate.
    std::optional<std::vector<fr::Guid>> OwnedItemIds();

    // From the OnEventSent detour.
    void OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept;

    // From the SignalOutputPin detour (hot: one relaxed load when nothing is watched).
    void OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept;

    // From the ZCharacterSubcontrollerInventory_CreateItem detour (logged in missions).
    void OnCreateItem(const void* p_Inventory, const ZRepositoryID& p_Id, const ZString& p_InstanceId,
                      int32_t p_Type) noexcept;

    // Frame update (game thread): delivery, confirmation, gear wall removals.
    void Tick(double p_GameSeconds) noexcept;

    // lockpick_grant, at config load. slow_lockpick always gives the classic
    // lockpick (fr::kSlowLockpickId, Tool_Lockpick).
    void ConfigureLockpickGrant(fr::LockpickGrantMode p_Mode, int32_t p_Nails);

    // lockpick_grant (DoorLockShuffle): finds the world inventory now (about
    // half a second; call behind the loading screen). Game thread.
    void PrepareWorldInventory() noexcept;

    // lockpick_grant: 47's lockpick-like items by a world inventory scan, his
    // own and the given ones (told apart by entity); empty, with *p_Why, when
    // the scan cannot tell. Game thread.
    std::optional<fr::LockpickCarry> HeroLockpicks(std::string* p_Why) noexcept;

    // The item grants, at config load: which items are asked (by
    // fr::GrantItemIndex). Independent of random_loadout.
    void ConfigureItemGrants(const std::array<bool, fr::kGrantItemCount>& p_On);

    // SignalInputPin detour (hot: one relaxed load while nothing is given).
    bool GrantPinsOn() const { return m_GrantListOn.load(std::memory_order_relaxed); }
    // True: block this signal (a push or remove of the given id on "Inventory items").
    bool OnInputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept;

    // ZWorldInventory::RequestNewItem's callback for the briefcase item and the
    // pocket items (a ZMemberDelegate; public for the delegate's member pointer).
    void OnBriefcaseItemCreated(uint32_t p_Ticket, TEntityRef<IItemBase> p_Item);

private:
    enum class Phase : uint8_t { Idle, WaitList, Delay, Deliver, Confirm, Done };

    struct Planned {
        fr::Guid id;
        std::string name;
        fr::LoadoutSource source = fr::LoadoutSource::Persistent;
        bool briefcase = false;
        bool pocket = false; // a large holsterable item: the game's pocket call (fr::IsPocketDeliveryItem)
        std::string why;
        std::string skipNote;            // why it was not requested ("47 already carries it", ...)
        bool requested = false;          // CreateItem called without a fault
        std::string requestNote;         // CreateItem's result
        std::set<ZEntityType**> before;  // item entities with this id before the request
        std::set<ZEntityType**> spawned; // new item entities with this id seen since
        std::string ownerState;          // last logged owner state of the new entities
        bool confirmed = false;          // 47 holds it (inventory scan or ItemPickedUp)
        std::string confirmedBy;
        bool removeQueued = false;
        bool removed = false; // RemoveRepositoryId sent to its source list
    };

    // One world inventory item (ZHM5Item) as the scan copies it.
    struct WorldItem {
        ZEntityType** entity = nullptr;
        ZEntityType** owner = nullptr;
        fr::Guid id;
        bool hasId = false;
    };

    struct WorldScan {
        bool ok = false;
        std::string why; // when not ok
        ZEntityType** hero = nullptr;
        size_t entries = 0; // m_aItems size
        std::vector<WorldItem> items;
    };

    // 47's inventory subcontroller: Found (m_Hero and m_HeroInventory set),
    // NotYet (47 or his subcontrollers are not there yet) or Failed; p_Text
    // says which subcontroller or why.
    enum class HeroFind : uint8_t { Found, NotYet, Failed };

    // 47's state for the blend hold (ReadHeroState): his EHeroGameState bits
    // (ZHitman5+0xE28; empty: not readable) and his current movement's
    // EBaseMovementType (ZHitman5+0xAA0, then +0x28; empty: none or not
    // readable, for the log only); why: what was not readable.
    struct HeroStateRead {
        std::optional<uint64_t> mask;
        std::optional<int32_t> movement;
        std::string why;
    };

    // The lockpick grant, this scene.
    struct LockpickGrant {
        enum class Step : uint8_t { Idle, Wait, Give, Confirm, Done };
        Step step = Step::Idle;
        double timer = 0;
        double scanTimer = 0;
        int32_t created = 0;            // CreateItem calls that did not fault
        std::set<ZEntityType**> before; // item entities with the given id before the CreateItem
        std::set<ZEntityType**> given;  // the new ones 47 owns: the given items
        size_t pushesBlocked = 0;
        size_t removesBlocked = 0;
        size_t unread = 0; // pins of "Inventory items" whose payload could not be read
        std::string outcome;
    };

    // One item grant, this scene.
    struct ItemGrantJob {
        fr::Guid id;              // fr::GrantItemId
        int32_t created = 0;      // CreateItem calls that did not fault
        bool confirmDone = false; // its confirmation was logged
        size_t pushesBlocked = 0;
        size_t removesBlocked = 0;
        std::string outcome;
    };

    // The item grants, this scene: one timing for all of them.
    struct ItemGrants {
        enum class Step : uint8_t { Idle, Wait, Give, Confirm, Done };
        Step step = Step::Idle;
        double timer = 0;
        double scanTimer = 0;
        std::array<ItemGrantJob, fr::kGrantItemCount> items {};
    };

    // The exit (EvergreenExitTriggered): "Inventory items" gets a
    // RemoveRepositoryId of each given id (the slow lockpick's, the item
    // grants') every kExitRemoveEverySeconds until the mission's CpdSet.
    struct GrantExit {
        bool on = false;
        bool done = false;
        double timer = 0;
        double sendTimer = 0;
        uint32_t ticks = 0;
        size_t sends = 0; // rounds (one RemoveRepositoryId per id each)
        std::vector<fr::Guid> ids;
    };

    // The return at the exit (see fr::PlanWallReturn), this scene.
    struct WallReturn {
        struct Item {
            fr::Guid id;
            std::string name;
            size_t wall = 0;   // copies on the wall at departure
            size_t packed = 0; // copies packed at departure
        };
        bool on = false;      // rounds running
        bool checked = false; // the mission's CpdSet was judged
        double timer = 0;
        double sendTimer = 0;
        uint32_t ticks = 0;
        size_t sends = 0; // rounds
        std::vector<Item> items;
    };

    // evergreen_setup entities on the game's briefcase path (and the packed
    // items' path, for the log and the SetItem payload type).
    enum Flow : size_t {
        kFlowPurchased,         // "Purchased item actions": SetItem, Drop; Item -> place into the briefcase
        kFlowBriefcase,         // "Briefcase" (ItemSpawner of the ICA Briefcase): ItemSpawned
        kFlowContainerActions,  // "Briefcase container actions": OnContainerItemSet
        kFlowBriefcaseActions,  // "Briefcase actions": PickupIntoHands; Drop
        kFlowExistingContainer, // "Existing briefcase container actions": ContainerItem / NoContainerItem
        kFlowExistingActions,   // "Existing briefcase actions": drops a briefcase 47 already has
        kFlowHelper,            // "EvergreenGearPersistenceHelper": Pickup (the packed items)
        kFlowLoadedActions,     // "Loaded item actions" (the packed items)
        kFlowHeroActions,       // "HeroItemAction": SetItem -> PickupIntoPocket (the pocket items); OnFailed
        kFlowCount
    };

    struct BriefcaseJob {
        enum class Step : uint8_t { Idle, WaitItem, Watch, Undo, Done };
        Step step = Step::Idle;
        size_t plan = 0; // index into m_Plan
        uint32_t ticket = 0;
        bool abandoned = false;            // a late item from the world inventory is destroyed
        ZEntityType** item = nullptr;      // the spawned item entity
        ZEntityType** briefcase = nullptr; // the ICA Briefcase the game spawned for it
        ZEntityType** lastOwner = nullptr;
        bool ownerRead = false;
        bool ownerIsStorage = false; // lastOwner is a ZItemStorageEntity (the briefcase's storage)
        fr::BriefcaseProgress progress;
        fr::BriefcaseVerdict verdict = fr::BriefcaseVerdict::Pending;
        double undoTimer = 0;
        bool itemDropSent = false;
        bool briefcaseDropSent = false;
        double itemDropAt = 0;
        double briefcaseDropAt = 0;
        bool itemHandled = false;
        bool briefcaseHandled = false;
        std::string lastProgress; // last logged BriefcaseProgressText
    };

    // One pocket item (fr::IsPocketDeliveryItem), this scene.
    struct PocketJob {
        enum class Step : uint8_t { WaitItem, Ready, Watch, Fallback, Done };
        Step step = Step::WaitItem;
        size_t plan = 0; // index into m_Plan
        uint32_t ticket = 0;
        bool abandoned = false;       // a late item from the world inventory is destroyed
        double waited = 0;            // game time since the request
        ZEntityType** item = nullptr; // the spawned item entity
        ZEntityType** lastOwner = nullptr;
        bool ownerRead = false;
        fr::PocketProgress progress;
        fr::PocketVerdict verdict = fr::PocketVerdict::Pending;
        std::string fallbackWhy;  // Step::Fallback: why
        std::string lastProgress; // last logged PocketProgressText (without the clock)
    };

    void SceneLoad(const std::string& p_Scene, const std::string& p_Type,
                   const std::optional<fr::SeedResolution>& p_Seed, const char* p_When);
    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context);
    void EventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event);
    void FindEntities(ZEntitySceneContext* p_Context, const char* p_Stage);
    void DeliveryTick(double p_Seconds);
    bool Deliver();
    void ConfirmScan(bool p_Final);
    WorldScan ScanWorld();
    void CheckImage();
    void ResolveWorldInventory();
    std::string NameOf(const fr::Guid& p_Id) const;
    void OnItemPickedUp(uint32_t p_EventIndex, const std::string& p_Json);
    void SaveCheck(uint32_t p_EventIndex, const std::string& p_Json);
    void Removals();
    // Gear wall loans: reviewed at each Freelancer scene load (p_Departure: a
    // mission's departure snapshot; nullptr: the safehouse, against the live
    // state); the mission's pushes back onto "Persistent items".
    void ReviewWallLoans(const char* p_When, const fr::LoadoutState& p_State, const fr::LoadoutState* p_Departure);
    void LoanRepairTick(double p_Seconds);
    // The return at the exit (fr::PlanWallReturn): from EvergreenExitTriggered
    // until the mission's CpdSet, the rounds that keep a carried-out lent
    // item out of the loadout and on the wall; checked at that CpdSet.
    void WallReturnStart(uint32_t p_EventIndex);
    void WallReturnTick(double p_Seconds);
    void WallReturnCheck(uint32_t p_EventIndex, const std::string& p_Json);
    bool EnsureItemRefType();
    // CreateItem of a planned item in 47's inventory ("loadout spawn requested:"); true: requested.
    bool CreatePlanned(Planned& p_Planned, const fr::LoadoutItemTypeChoice& p_Type);
    bool StartPocket(Planned& p_Planned, size_t p_Index);
    void PocketItemCreated(PocketJob& p_Job, uint32_t p_Ticket, ZEntityType** p_Entity, bool p_HasInterface,
                           const std::string& p_What);
    void PocketTick(double p_Seconds);
    void PocketWatch(PocketJob& p_Job, size_t p_Index, double p_Seconds);
    void PocketFallback(PocketJob& p_Job);
    void OnHoldingIllegalWeapon(uint32_t p_EventIndex, const std::string& p_Json);
    bool StartBriefcase(Planned& p_Planned, size_t p_Index);
    // The blend hold (fr::JudgeBlendHold): the exe check of 47's state bits
    // (once per process), 47's state now (bits and movement) and, every frame
    // while the briefcase item is held, whether to give it.
    void CheckHeroState();
    HeroStateRead ReadHeroState();
    void HoldTick(double p_Seconds);
    // Gear wall items requested so far are recorded as lent (fr::WallLoans).
    void LendRequested();
    void BriefcaseTick(double p_Seconds);
    void BriefcaseUndo(double p_Seconds);
    void BriefcaseFinish(const char* p_Why);
    void OnFlowPin(size_t p_Flow, uint32_t p_PinId, const ZObjectRef& p_Data);
    void LogSnapshot(const std::vector<RepoLoadoutEntry>& p_Entries);
    void Plan(const std::vector<RepoLoadoutEntry>& p_Entries, const std::optional<fr::SeedResolution>& p_Seed,
              const std::map<int32_t, int32_t>& p_Payouts, const char* p_When);
    void SaveState(const char* p_Why);
    std::filesystem::path StatePath() const;
    HeroFind FindHeroInventory(std::string& p_Text);
    bool GrantScan(std::vector<RepoLoadoutEntry>& p_Entries, std::string& p_Why);
    std::string OwnedText(const fr::Guid& p_Id) const;
    void GrantCensus(const char* p_When, const std::vector<RepoLoadoutEntry>* p_Entries, const std::string& p_Why);
    // fr::DecideGrantStart for a waiting grant ("loadout lockpick grant" or "loadout item grant"); logs a timeout.
    bool GrantStartReady(double p_Timer, const char* p_What) const;
    void GrantTick(double p_Seconds);
    void GrantGive();
    void GrantConfirm(bool p_Final);
    void GrantSaveCheck(uint32_t p_EventIndex, const std::string& p_Json);
    void GrantExitStart(uint32_t p_EventIndex, std::string_view p_EventName);
    void GrantExitTick(double p_Seconds);
    bool AnyItemGrant() const;
    void ResetItemGrants();
    bool IsGrantKind(fr::GrantItem p_Item, const fr::Guid& p_Id) const;
    fr::LockpickCarry KindCarry(fr::GrantItem p_Item) const;
    void ItemGrantCensus(const char* p_When, const std::vector<RepoLoadoutEntry>* p_Entries, const std::string& p_Why);
    void ItemGrantTick(double p_Seconds);
    void ItemGrantGive();
    void ItemGrantConfirm(bool p_Final);
    void ItemGrantSaveCheck(uint32_t p_EventIndex, const std::string& p_Json);
    void LedgerEvent(fr::HeroItemEvent p_Event, uint32_t p_EventIndex, const std::string& p_Json);
    fr::LockpickCarry LedgerCarry() const;
    bool IsLockpickLike(const fr::Guid& p_Id) const;

    ProRepository& m_Repo;
    bool m_On = false;
    double m_DelaySeconds = 1.0;
    bool m_Briefcase = true;      // !random_loadout_holsterable_only
    bool m_LockpickFirst = true;  // random_loadout_lockpick_first
    bool m_ObjectiveItems = true; // random_loadout_objective_items

    // Guards everything below (events, pins, the CreateItem detour and the game thread).
    std::recursive_mutex m_Mutex;

    // Gear wall items lent by the loadout until a CPD shows them back on the
    // wall (kept in the state file), and this mission's pushes back onto it.
    fr::WallLoans m_Loans;
    std::vector<fr::WallLoan> m_LoanRepairs;
    bool m_LoanRepairDone = false;
    WallReturn m_Return;
    bool m_ScenePlaying = false;
    double m_PlayingSeconds = 0;

    // The merged CPD (live) and the snapshot frozen at the last departure.
    fr::LoadoutState m_Live;
    std::optional<fr::LoadoutState> m_Departure;
    bool m_SafehouseCpdSinceFreeze = false;
    ClientReqPayoutsFor m_PayoutsFor;

    // The scene load's pro.repo scan and seed, kept until the plan is made at
    // eLoading_SceneStarted (the client requirements' payouts are known only
    // once the payout table is written then); empty once planned.
    std::optional<std::vector<RepoLoadoutEntry>> m_PendingEntries;
    std::optional<fr::SeedResolution> m_PendingSeed;

    // This scene.
    std::string m_Scene;
    std::atomic<bool> m_Freelancer {false}; // also read by the event hook without m_Mutex
    bool m_Mission = false;
    bool m_Safehouse = false;
    std::vector<Planned> m_Plan;
    std::map<fr::Guid, std::string> m_Names; // packed and owned items' names
    std::set<fr::Guid> m_Packed;             // the departure's InventoryItems
    bool m_Planned = false;
    bool m_EntitiesFound = false;
    ZEntityType** m_PersistentList = nullptr;
    ZEntityType** m_TransientList = nullptr;
    uint32_t m_Emitted = 0; // ids "Inventory items" gave this scene
    uint32_t m_PinLines = 0;
    bool m_IterationDone = false;
    Phase m_Phase = Phase::Idle;
    double m_Timer = 0;
    double m_ScanTimer = 0;
    void* m_HeroInventory = nullptr;
    ZEntityType** m_Hero = nullptr;
    std::optional<int32_t> m_PackedCreateType;
    uint32_t m_CreateLines = 0;
    bool m_InOwnCreate = false;
    bool m_ScanFaultLogged = false;
    BriefcaseJob m_Job;
    // The briefcase item held back while 47 is in a blend spot or an act (a
    // briefcase in his hands ends it and can leave him stuck; see
    // fr::JudgeBlendHold), this scene.
    struct BlendHold {
        std::optional<size_t> plan;       // the held item (m_Plan index); empty: nothing held
        double seconds = 0;               // game time held
        double clear = 0;                 // game time in a row read with no hold state on
        double unread = 0;                // game time in a row without a reading
        double sinceLog = 0;              // game time since the last "still held" line
        std::optional<uint64_t> lastMask; // the state bits last logged (a line at each change)
        std::optional<int32_t> lastMove;  // the movement last logged
        uint32_t changeLines = 0;         // "47's state changed" lines this hold
    };
    BlendHold m_Hold;
    std::vector<PocketJob> m_Pockets;
    std::optional<size_t> m_PocketActive; // the pocket job whose SetItem was sent (its pins and events)
    double m_HandsWatch = 0;              // game time left to report delivered items in 47's hands
    std::optional<fr::Guid> m_HandsItem;  // the delivered item last reported in 47's hands
    std::array<ZEntityType**, kFlowCount> m_FlowEntities {};
    const STypeID* m_ItemRefType = nullptr; // the payload type the game's SetItem gets (helper's Pickup)
    std::string m_ItemRefTypeName;

    // The live ZWorldInventory (ResolveWorldInventory, per scene): the SDK's
    // Globals::WorldInventory only when its RTTI (or, without RTTI, its item
    // list) vouches for it; else one found by its class. nullptr: none may be
    // used (no scan, no RequestNewItem, no DestroyItem).
    ZWorldInventory* m_World = nullptr;
    bool m_WorldResolved = false;
    std::string m_WorldWhy;

    // The game's exe (CheckImage, once per process).
    bool m_ImageChecked = false;
    std::optional<fr::ImageInfo> m_Image;
    std::vector<uint64_t> m_WorldVtables; // ZWorldInventory's (by RTTI)
    bool m_RequestNewItemOk = false;      // the SDK's RequestNewItem address is its pattern's only match
    std::string m_RequestNewItemWhy;
    bool m_HeroStateChecked = false; // CheckHeroState ran
    bool m_HeroStateOk = false;      // both code sites matched once: ZHitman5+0xE28 / +0xAA0 are trusted
    std::string m_HeroStateWhy;      // why not

    // lockpick_grant.
    fr::LockpickGrantMode m_GrantMode = fr::LockpickGrantMode::Off;
    int32_t m_GrantNails = fr::kDefaultLockpickNails;
    fr::Guid m_GrantId {};            // the mode's item this scene
    fr::Guid m_GrantChosen {};        // the mode's item (fr::LockpickGrantItemId)
    std::set<fr::Guid> m_LockpickIds; // lockpick-like pro.repo items (GrantCensus), with the given id
    std::map<fr::Guid, std::string> m_LockpickNames;
    LockpickGrant m_Grant;
    // 47's items by the game's item events (the world inventory scan sees none; see LockpickFree.h).
    fr::HeroItemLedger m_Ledger;
    uint32_t m_LedgerLines = 0;
    std::atomic<bool> m_OwnListSignal {false}; // this mod's own RemoveRepositoryId on "Inventory items": not blocked
    std::atomic<bool> m_GrantListOn {false};   // given items exist: the "Inventory items" pins are watched
    std::set<fr::Guid> m_GuardedIds;           // given ids "Inventory items" takes no push or remove of
    GrantExit m_Exit;

    // The item grants.
    std::array<bool, fr::kGrantItemCount> m_ItemGrantOn {};
    std::map<fr::Guid, fr::GrantItem> m_GrantKinds; // pro.repo items of the asked kinds (ItemGrantCensus)
    std::map<fr::Guid, std::string> m_GrantKindNames;
    ItemGrants m_Items;

    // Read by the pin hook.
    std::atomic<ZEntityType**> m_InventoryList {nullptr};
    std::atomic<const void*> m_RepoIdType {nullptr};
    std::atomic<bool> m_FlowOn {false};
    std::array<std::atomic<ZEntityType**>, kFlowCount> m_FlowWatch {};
};
