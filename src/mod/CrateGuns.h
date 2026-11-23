#pragma once

// Supply crates in Freelancer missions: crate items (crate_items; pure part in
// core/CrateItems.h) and, with crate_items off, crate guns (crate_guns; pure
// part in core/CrateGunMath.h).
// - At scene playing one walk of the loaded bricks finds the shared stash
//   items instance: its crate seed, branch choices and crate options; with
//   crate_items also its slot selectors, the mission's other option helpers
//   and (crate_level_items) the level's loose item spawners.
// - Crate items: each crate (told apart by its seed) gets a plan; when it
//   opens, the plan's items go into the options' keys, their spawners are
//   turned and moved so each item lies as the game lays it, and each offered
//   item is measured and moved to the middle of its slot.
// - Crate guns: before each open the branch weights are written for the
//   seeded draw, and the firearms' weights when their pool activates.
// Threads: the hooks run on any thread and take m_Mutex; engine writes made
// under it may re-enter the hooks on the same thread, which then return at
// once (HeldLock in the .cpp). Lines "crate items" and "crate guns" go to
// log_supplier_crates. Every engine read and write is guarded.

#include "CrateGunMath.h"
#include "CrateItems.h"
#include "Guid.h"
#include "PinWatchTable.h"
#include "Seed.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class ProRepository;
class ZDynamicObject;
class ZObjectRef;
struct RepoLoadoutEntry;

class CrateGuns {
public:
    // The CPD's owned items (never offered by crate_items; fr::CrateGunOwned's "listed"), or empty when no list was seen.
    using OwnedItems = std::function<std::optional<std::vector<fr::Guid>>()>;

    explicit CrateGuns(ProRepository& p_Repo) : m_Repo(p_Repo) {}

    // At config load (sanitized here). Nothing runs when all are off.
    void Configure(const fr::CrateItemsConfig& p_Items, const fr::CrateGunsConfig& p_Guns, OwnedItems p_Owned);

    bool On() const { return m_Items.on || m_Config.on; }

    // Any thread: ItemPickedUp lines are wanted (a crate holds an item the mod placed).
    bool PickupsWanted() const { return m_PickupWatch.load(std::memory_order_relaxed); }

    // Any thread: whether the hooks have anything to do (one relaxed load).
    bool Armed() const { return m_Armed.load(std::memory_order_relaxed); }

    // Game thread, from the SetLoadingStage detour, with the dials' scene seed.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                        const std::optional<fr::SeedResolution>& p_Seed) noexcept;

    // Game thread, at a scene clear: the last report and the summary, then the load is forgotten.
    void OnSceneClear() noexcept;

    // Game thread, every play-mode frame: open detection fallback, reports,
    // centering the offers.
    void Tick() noexcept;

    // Any thread, from the SignalOutputPin detour. Hot: a lock-free pointer
    // lookup unless the entity is a watched one.
    void OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept;

    // Any thread, from the ZEntityManager_ActivateEntity detour: offers
    // spawned under a crate option during an open's report window.
    void OnActivateEntity(const ZEntityRef* p_Entity) noexcept;

    // Any thread, from the OnEventSent detour while PickupsWanted(): pickups
    // from the crates (their options free again).
    void OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept;

private:
    using Clock = std::chrono::steady_clock;

    // Watched entities' values: option index << 2 | role. Role 0 is never
    // watched, so the 0 of a cleared slot is rejected.
    static constexpr uint32_t kRoleCleared = 0;
    static constexpr uint32_t kRoleSeed = 1;
    static constexpr uint32_t kRoleState = 2;
    static constexpr uint32_t kRoleMath = 3;

    // One crate option inside the stash items.
    struct Option {
        ZEntityType** root = nullptr;
        ZEntityType** holder = nullptr; // the entity the walk reached it from (what a selector's m_Choices lists)
        uint64_t id = 0;
        ZEntityType** choice = nullptr; // ZRandomSelectorChoice (m_Weight)
        ZEntityType** math = nullptr;   // ZMathMultiplyDivide (m_fA / m_fB)
        ZEntityType** cost = nullptr;   // ZValueFloat_Basic (m_fValue)
        ZEntityType** state = nullptr;  // ZValueInt_Basic (m_nValue: 1 tool pool, 3 weapon pool)
        ZEntityType** key = nullptr;    // ZItemRepositoryKeyEntity
        // The item keys (fr::kCrateOptionKeyIds), each entity once: what crate_items writes.
        std::vector<ZEntityType**> keyEntities;
        std::vector<uint64_t> keyIds;
        uintptr_t weightAddress = 0;
        std::optional<fr::Guid> item; // its own item (as the walk found it)
        std::string name;
        std::string icon;
        // The owned / entitled flags (members fr::kUnownedFlagIds, fr::kEntitledFlagId).
        std::vector<uint64_t> unownedIds;
        std::vector<ZEntityType**> unownedFlags;
        ZEntityType** entitledFlag = nullptr;
        std::vector<uintptr_t> unownedAddresses; // their m_bValue (firearms, and the options crate_items writes)
        uintptr_t entitledAddress = 0;
        // The choice condition (fr::kOptionConditionId, m_bValue): crate_items sets it with the owned flags.
        ZEntityType** condition = nullptr;
        uintptr_t conditionAddress = 0;
        // Its ZItemSpawner members (the rest turn), each entity once, and the mod's change
        // to each (fr::kNoTurn: vanilla).
        std::vector<ZEntityType**> spawners;
        std::vector<uint64_t> spawnerIds;
        std::vector<fr::SpawnerTurn> spawnerTurns;
        // Its menu entry (fr::kOptionMenuEntryId) and the selector box its m_boundary names (the item template's
        // ZBoxVolumeEntity "Boundary"; BoxOf), read before the mod first writes it: its transform under its
        // m_eidParent and m_vGlobalSize (crate_even_boxes, FitBox).
        ZEntityType** menuEntry = nullptr;
        ZEntityType** box = nullptr;
        uint64_t boxId = 0;
        std::string boxWhy; // why no box was found (empty: not looked for yet, or found)
        bool boxLooked = false;
        std::optional<fr::SpatialTransform> boxLocal;
        std::optional<std::array<double, 3>> boxSize;
        bool boxWritten = false; // the mod changed it (size or spot)
        // crate_items: where the mod means its written item's middle to lie, in its first spawner's axes as placed
        // (FitOption): the item's box center, or with no box the slot's box middle (CenterTarget). Empty: not set.
        std::optional<std::array<double, 3>> centerTarget;
        bool centerFromBox = false; // the target is a box's center from the game files (fr::kCrateCenterBoxSlackMeters)
        std::string centerFrom;     // for the log: where the target comes from
        // crate_items: where its written item's rest came from (Learned also for its own item and the one-item
        // option: the game's placement). Only a guessed one (Table, Box, Default) is laid flat when the item is
        // measured standing upright (CenterOffers, fr::LayItemFlat).
        fr::CrateRestSource restSource = fr::CrateRestSource::Learned;
        // crate_items: the crate whose item it still holds (offered there, not picked up), and that item.
        std::optional<size_t> heldBy;
        fr::Guid heldItem;
        bool excluded = false; // the mod set its condition false (kept out of the draw)
        bool gun = false;
        bool merces = false;
        int32_t rarity = 0;
        // Per open:
        int32_t state0 = 0;             // the state its last activation signaled
        double target = -1.0;           // firearms: the weight written this open (-1: none)
        std::optional<double> observed; // non-firearms in the weapon pool: the weight the game gave it this open
    };

    struct Open {
        uint32_t index = 0;
        int32_t seedValue = 0;
        fr::CrateBranchDraw draw;
        std::string via;
        Clock::time_point at;
        std::array<uint32_t, 4> pools {}; // activations per fr::CratePool
        std::vector<size_t> offers;       // options an item spawned under
        std::vector<uint64_t> offerBlueprints;
        std::vector<ZEntityType**> offerEntities; // the spawned items
        std::vector<Clock::time_point> offerAt;   // when each spawned
        std::vector<uint8_t> offerCentered;       // crate_items: each measured and centered (CenterOffers) or given up
        uint32_t gunsWrites = 0;                  // firearm weight writes
        uint32_t payloadRewrites = 0;             // firearm weight-math outputs rewritten
        uint32_t otherZeroed = 0;                 // non-firearm weights rewritten to 0 (share 1)
        double otherUsed = -1.0;                  // the other weight the firearms were last balanced against
        bool hitNoGun = false;                    // the roll hit, but every firearm reads owned: left vanilla
        std::optional<size_t> crate;              // crate_items: the crate (m_Crates) it opened
        bool balanced = false;
        bool reported = false;
    };

    // An option helper outside the stash items (the supplier's and the rest).
    struct SceneOption {
        ZEntityType** root = nullptr;
        ZEntityType** key = nullptr; // fr::kOptionKeyId
        std::optional<fr::Guid> item;
        std::vector<uint64_t> unownedIds;
        std::vector<ZEntityType**> unownedFlags;
        ZEntityType** entitledFlag = nullptr;
        // Its ZItemSpawner members: its item's placement there, logged with an item's rest (never learned from:
        // fr::CratePlacement::Other), and their entity ids.
        std::vector<ZEntityType**> spawners;
        std::vector<uint64_t> spawnerIds;
        // For the log: its entity id, the entity the walk reached it from (id, class) and its brick.
        uint64_t id = 0;
        uint64_t holderId = 0;
        std::string holderClass;
        uint64_t brick = 0;
    };

    // An item's rest learned this load (RestOf), and the crate option spawner its pose comes from (re-read at each
    // open: a world rotation holds only in the crate it was read in).
    struct LearnedRest {
        fr::Guid item;
        fr::CrateRest rest;
        size_t poseOption = SIZE_MAX;
        size_t poseSpawner = SIZE_MAX;
        // A twin's option (fr::CrateTwinOf): its rotation turned to the item's axes (fr::kNoTurn: the item's own).
        fr::AxisTurn poseTurn = fr::kNoTurn;
    };

    // One item key of a slot option or a one-item option: what the walk found
    // in it and what it holds now (the mod's writes).
    struct KeySlot {
        size_t option = 0;
        uint64_t id = 0;
        ZEntityType** entity = nullptr;
        fr::Guid original;
        fr::Guid current;
    };

    // crate_items: one crate's plan.
    struct Prepared {
        uint32_t crate = 0; // the crate's number in the load (the plan's seed)
        fr::CratePlan plan;
        std::array<std::optional<fr::Guid>, 3> slots;
        std::optional<fr::Guid> single;
        std::vector<fr::Guid> items; // every item of the plan
    };

    // A crate opened this load, told apart by its crate seed (copied into the
    // shared seed entity when it opens; the same crate opened again brings
    // the same seed).
    struct Crate {
        int32_t seed = 0;
        uint32_t firstOpen = 0; // open index of its first open
        uint32_t lastOpen = 0;
        std::optional<Prepared> plan;                 // crate_items' (empty: crate_items off)
        std::array<bool, fr::kCrateGroups> picked {}; // an item was picked up from this slot of it
    };

    // How an open was seen: at the crate seed's pin (before the slots draw),
    // at a crate option activating (after the draw, before the spawns), or by
    // the frame's seed poll (late: the spawns may be done).
    enum class OpenSeen : uint8_t { Pin, Activation, Poll };

    // A resource requested for an item the map has not loaded (held: its
    // index with one reference of the mod's; released at the scene clear).
    struct HeldResource {
        fr::Guid item;
        std::string name;
        uint64_t rid = 0;
        int32_t index = -1;
    };

    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                      const std::optional<fr::SeedResolution>& p_Seed);
    void Setup(ZEntitySceneContext* p_Context, const std::optional<std::vector<fr::Guid>>& p_Owned);
    bool Walk(ZEntitySceneContext* p_Context, std::string* p_Why);
    bool Wide() const { return m_Items.on; } // the walk maps slots and other option helpers
    // The walk keeps the item spawners outside the option helpers (crate_level_items).
    bool LevelScan() const { return m_Items.on && m_Items.levelItems; }
    // Names the options and the scene items; returns pro.repo's items (empty when not ready).
    std::vector<RepoLoadoutEntry> NameOptions();
    double GunChance() const { return m_Config.on && !m_Items.on ? m_Config.poolChance : 0.0; }

    // Under m_Mutex (all below unless noted).
    // The stash items' selectors to their options: m_SlotOptions, m_SingleOptions, m_Keys.
    void MapSlots();
    // m_Candidates from the option helpers' items, the level's loose items and pro.repo.
    void BuildCandidates(const std::vector<RepoLoadoutEntry>& p_Repo,
                         const std::optional<std::vector<fr::Guid>>& p_Owned);
    // The level's loose items (fr::CollectLevelItems over m_LevelSpawners), logged; their GUIDs, sorted.
    std::vector<fr::Guid> ReadLevelItems(const std::vector<RepoLoadoutEntry>& p_Repo);
    // A gun option's owned flags: eligible (not owned, entitled), or empty when not read.
    std::optional<bool> FlagsEligible(const std::vector<ZEntityType**>& p_Unowned, ZEntityType** p_Entitled) const;
    // One option's keys set to p_Item (empty: its own); how many read back right, *p_Changed: any write.
    size_t WriteKeysOf(size_t p_Option, const std::optional<fr::Guid>& p_Item, bool* p_Changed, std::string* p_Failed);
    // An option's choice condition and owned flags: eligible (all true) or kept out (condition and one flag false).
    bool SetEligible(Option& p_Option, bool p_On);
    // The options of group p_Group (fr::kCrateGroups: the three tool slots, the one-item option).
    const std::vector<size_t>* GroupOptions(size_t p_Group) const;
    std::optional<size_t> GroupOf(size_t p_Option) const;
    // A crate other than p_Crate whose item the one-item option still holds.
    std::optional<size_t> OneItemHeldElsewhere(std::optional<size_t> p_Crate) const;
    // An option of a tool slot has an item out (offered in a crate, not picked up).
    bool ToolItemOut() const;
    // The item an option's keys hold now (the mod's last write), or its own.
    std::optional<fr::Guid> OptionItemNow(size_t p_Option) const;
    // crate_items: the plan for crate number p_Crate (its items' resources requested).
    Prepared PlanItems(uint32_t p_Crate, const char* p_When);
    // A crate's plan into the options (p_Crate empty: crate #1's keys at scene playing, no flags).
    void ApplyPlan(const Prepared* p_Plan, std::optional<size_t> p_Crate, const char* p_When);
    // An open's crate: found by its seed or new (planned), and its plan applied.
    void OpenCrate(int32_t p_Seed, const char* p_Via, OpenSeen p_Seen);
    // An ItemPickedUp of a crate's item: its option is free again, its slot taken.
    void CratePickup(uint32_t p_EventIndex, const std::string& p_Name, const fr::Guid& p_Item);
    // A resource request for an item the map has not loaded (logged).
    void HoldResource(const fr::CrateCandidate& p_Item, const char* p_Why);
    std::string HeldText(const fr::Guid& p_Item) const;
    void ReleaseHeld();
    void ReportItems(const Open& p_Open);
    // The middles of the three tool slots now (fr::CrateSlotMiddle of their options' vanilla spawners).
    using SlotMiddles = std::array<std::optional<fr::WorldPoint>, 3>;
    SlotMiddles StashSlotMiddles();
    // Where the three tool slots' items sit (fr::CrateSlotBoxMiddle of their options' vanilla boxes, checked against
    // p_Middles).
    SlotMiddles StashSlotBoxMiddles(const SlotMiddles& p_Middles);
    // Each tool slot's floor now (fr::CrateFloorHeight of its options' vanilla spawners; none: p_Stash).
    using SlotFloors = std::array<std::optional<double>, 3>;
    SlotFloors StashSlotFloors(std::optional<double> p_Stash);
    // Under m_Mutex (transforms refreshed, every read guarded): an item's rest, learned once per load from the
    // crate's tool-slot options whose own item it is (p_Floors: each slot's floor now, for their heights, p_Floor
    // the crate's for an option outside the slots), its pose re-read now, with its spot in its slot (p_Middles), or a
    // twin's box center.
    fr::CrateRest RestOf(const fr::Guid& p_Item, std::optional<double> p_Floor, const SlotFloors& p_Floors,
                         const SlotMiddles& p_Middles);
    // The crate floor now (fr::CrateFloorHeight of the stash options' vanilla spawners), or empty.
    std::optional<double> StashFloor();
    // Option i's spawner n's vanilla transform (the mod's change undone), or empty when unread.
    std::optional<fr::SpatialTransform> SpawnerVanilla(size_t i, size_t n);
    // Option i's spawners turned and moved for p_Rest (back to vanilla for a fitting one; p_Rest null: all back to
    // vanilla; p_Floor: its slot's floor; p_Middle and p_BoxMiddle: the middle of its slot and where its items sit);
    // *p_Text says what each got, *p_Writes counts the transforms written.
    fr::CrateFit FitOption(size_t i, const fr::CrateRest* p_Rest, std::optional<double> p_Floor,
                           std::optional<fr::WorldPoint> p_Middle, std::optional<fr::WorldPoint> p_BoxMiddle,
                           std::string* p_Text, size_t* p_Writes);
    // Option i's selector box: its menu entry's m_boundary, looked up once a load (null: none; Option::boxWhy).
    ZEntityType** BoxOf(size_t i);
    // crate_even_boxes: option i's selector box as the game placed it (read once a load, before the mod writes it;
    // its world transform from its parent's now), or empty when unread.
    std::optional<fr::CrateBox> BoxVanilla(size_t i);
    // The common box size of this load's crate (fr::CommonCrateBoxSize of the tool slots' vanilla boxes), read once.
    std::optional<fr::CrateBoxSize> CommonBoxSize();
    // Where p_Item's box center lies from its spawner (its spawner's axes): from its own crate option (p_Own: that
    // option), else from the option its rest was learned from (RestOf, a twin's turned), else its middle measured
    // this load (MeasuredCenter), else its option template's (fr::CrateItemBoxCenter), else empty; *p_From says
    // which.
    std::optional<std::array<double, 3>> ItemBoxCenter(const fr::Guid& p_Item, std::optional<size_t> p_Own,
                                                       std::string* p_From);
    // crate_even_boxes: option i's box set to the common size and centered on its item (p_Item: the item it will
    // spawn) after FitOption placed its spawner; *p_Writes counts the writes.
    void FitBox(size_t i, const fr::Guid& p_Item, size_t* p_Writes);
    // crate_items, after FitOption placed option i's spawner for p_Item: where its middle should lie
    // (Option::centerTarget): ItemBoxCenter, else p_BoxMiddle (the slot's box middle), else the spawner's origin.
    void CenterTarget(size_t i, const fr::Guid& p_Item, std::optional<fr::WorldPoint> p_BoxMiddle);
    // An item's middle measured this load (in its spawner's axes), or empty.
    std::optional<std::array<double, 3>> MeasuredCenter(const fr::Guid& p_Item) const;
    // The spawner axis an item measured standing upright this load should rest on (fr::MeasuredRestAxis), or
    // empty.
    std::optional<fr::LocalAxis> MeasuredRest(const fr::Guid& p_Item) const;
    // Game thread, each frame of an open: each offered item of a tool slot, once it has spawned, measured where it
    // lies (its model's bounds, fr::CheckItemCenter against its option's center target); one off its target is moved
    // there (its selector box with it), and its measured middle kept for its next placement (m_Measured).
    void CenterOffers();
    std::string ItemText(const fr::Guid& p_Item) const;
    const fr::CrateCandidate* Candidate(const fr::Guid& p_Item) const;
    void ClearScene();
    void Forget();
    // The hooks that return at once when the engine re-enters them during the
    // mod's own write (HeldLock in the .cpp), counted per load for the summary.
    enum class Hook : uint8_t { LoadingStage, Frame, SceneClear, Event, OutputPin, Activate };
    static constexpr size_t kHooks = 6;
    bool Reentered(Hook p_Hook); // true (counted): this thread holds m_Mutex
    std::string ReentriesText() const;
    std::string SeedText() const;
    int32_t SeedValue() const;

    void BeginOpen(int32_t p_SeedValue, const char* p_Via, OpenSeen p_Seen);
    bool ItemsOn() const { return m_Items.on && m_SlotOptions.size() >= 3; } // crate_items plans the crates
    bool Managed() const { return ItemsOn(); }
    // Writes the branch weights for the open: crate_items' plan (p_Plan; the
    // tool pool forced while the one-item option holds another crate's item,
    // p_OneItemHeld), or crate guns' roll (a hit with every firearm owned is
    // written vanilla). Returns crate guns' verdict (Miss otherwise).
    fr::CrateHitVerdict WriteBranch(uint32_t p_OpenIndex, const char* p_When, const Prepared* p_Plan,
                                    std::optional<size_t> p_OneItemHeld);
    // The firearms not owned and entitled, read now; *p_Read: how many had their flags read.
    size_t EligibleGuns(size_t* p_Read, std::string* p_Names) const;
    void Balance(const char* p_Why);
    double OtherWeight(bool* p_Observed) const;
    double CostOf(const Option& p_Option) const;
    void Report(const char* p_When);
    std::string OptionText(size_t p_Option) const;
    void Pin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data);
    void Watch(const void* p_Entity, size_t p_Option, uint32_t p_Role);

    ProRepository& m_Repo;
    fr::CrateItemsConfig m_Items {false};
    fr::CrateGunsConfig m_Config {false};
    OwnedItems m_Owned;
    std::atomic<bool> m_PickupWatch {false}; // crate_items: an option holds an item in a crate
    std::atomic<bool> m_Armed {false};
    std::atomic<bool> m_Window {false}; // an open's report window is running
    std::atomic<const void*> m_Int32Type {nullptr};
    std::atomic<const void*> m_Float32Type {nullptr};
    fr::PinWatchTable<1024> m_Watch;
    std::array<std::atomic<uint32_t>, kHooks> m_Reentries {};

    // Under m_Mutex.
    mutable std::mutex m_Mutex;
    bool m_Pending = false; // a Freelancer mission is loading
    std::string m_Scene;
    std::optional<fr::SeedResolution> m_Seed;
    ZEntityType** m_StashItems = nullptr;
    uint64_t m_StashItemsBrick = 0;
    ZEntityType** m_SeedEntity = nullptr;
    uintptr_t m_SeedAddress = 0;
    ZEntityType** m_JunkChoice = nullptr;
    ZEntityType** m_WeaponChoice = nullptr;
    uintptr_t m_JunkAddress = 0;
    uintptr_t m_WeaponAddress = 0;
    float m_VanillaJunk = fr::kVanillaJunkBranchWeight;
    float m_VanillaWeapon = fr::kVanillaWeaponBranchWeight;
    std::vector<Option> m_Options;
    std::unordered_map<const void*, size_t> m_OptionByRoot;
    std::vector<size_t> m_Guns; // option indices of the firearms
    // crate_items: the stash items' selectors (walk
    // order), the other option helpers, the slots, the keys, the candidates.
    std::vector<ZEntityType**> m_Selectors;
    std::vector<SceneOption> m_SceneOptions;
    // crate_level_items: the ZItemSpawners outside the stash items and the option helpers (the walk's), with the
    // brick each was reached from; and how many the cap left out.
    std::vector<std::pair<ZEntityType**, uint64_t>> m_LevelSpawners;
    size_t m_LevelSpawnersOver = 0;
    std::vector<std::vector<size_t>> m_SlotOptions; // the tool pool's slots (fr::CrateToolSlots), their options
    std::vector<size_t> m_SingleOptions;            // the weapon pool's merces option(s): a one-item crate
    std::vector<KeySlot> m_Keys;
    std::vector<fr::CrateCandidate> m_Candidates;
    std::vector<fr::Guid> m_Planned;  // guns and large items planned this load (each once)
    std::vector<LearnedRest> m_Rests; // items' rests learned this load (RestOf)
    // crate_items: items' middles measured where they spawned this load, in their spawner's axes (CenterOffers).
    std::vector<std::pair<fr::Guid, std::array<double, 3>>> m_Measured;
    // crate_items: items measured standing on their end this load, and the spawner axis to lay them on (CenterOffers).
    std::vector<std::pair<fr::Guid, fr::LocalAxis>> m_MeasuredRests;
    std::optional<fr::CrateBoxSize> m_BoxSize; // crate_even_boxes: the common box size (CommonBoxSize)
    bool m_BoxSizeRead = false;
    std::optional<Prepared> m_Next; // crate #1's plan, written at scene playing
    std::vector<Crate> m_Crates;
    std::optional<size_t> m_LastCrate; // the crate opened last
    std::vector<HeldResource> m_Held;
    int32_t m_LastSeed = 0;
    uint32_t m_Opens = 0;
    std::optional<Open> m_Open;
    std::optional<uint32_t> m_BranchFor; // the open index the branch weights are written for
    // Summary.
    uint32_t m_ForcedWeapon = 0;
    uint32_t m_HitsNoGun = 0; // hits left vanilla: every firearm of the pool owned
    uint32_t m_TookWeapon = 0;
    uint32_t m_TookTool = 0;
    uint32_t m_Missed = 0;
    uint32_t m_GunOffers = 0;
    uint32_t m_Offers = 0;
    uint32_t m_LateOpens = 0; // crate_items: new crates seen only after their spawns (their plan not written)
    std::string m_OfferLog;
};
