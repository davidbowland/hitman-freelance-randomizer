#pragma once

// Loot amounts (setting loot_amounts, on by default; fr::RollLootAmount in
// core/LootAmounts.h). In Freelancer missions, the merces a safe, a supply
// crate's merces option or a money mule gives comes from a money item
// (prop_questitem_evergreen_moneyitem_{small,medium,large}_amount_runtime)
// spawned at runtime; its ZRandEntity_int32 rolls the amount in
// [m_nMin, m_nMax] (vanilla [1000, 1500] in the small item).
// - When such an item is activated (ZEntityManager_ActivateEntity detour,
//   before the engine's activation runs), it is classified by the setpieces
//   above it (its logical parents and their owning entities: safe, money
//   mule, crate setpieces; New York's bank vault by its spawner;
//   fr::ClassifyMoneyItem), gets its own amount (scene seed, kind,
//   the kind's spawn number in this load) and every ZRandEntity_int32 in it
//   gets m_nMin = m_nMax = that amount. Amounts follow activation order per
//   kind (the Nth item of a kind activated in the load gets draw N: the Nth
//   crate merces taken, the Nth mule's money), so a restart repeats them
//   only when things spawn and are opened in the same order. Activations before the scene seed is
//   known wait for it (scene start at the latest); an item whose tree has no
//   ZRandEntity_int32 yet is tried again on the next frames.
// - Right before a roll could happen at pickup: any input pin of a written
//   ZRandEntity_int32 (SignalInputPin detour) checks the values and writes
//   them again if they changed.
// Every engine read and write is guarded; the entry points catch everything.

#include "LootAmounts.h"
#include "PinWatchTable.h"
#include "Seed.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class ZObjectRef;

class MoneyItems {
public:
    // At config load, with each kind's bounds (fr::Config::lootBounds, by
    // MoneyKind; sanitized). Nothing runs when off.
    void Configure(bool p_On, const std::array<fr::LootAmountBounds, fr::kMoneyKinds>& p_Bounds);

    bool On() const { return m_On; }

    // Any thread: whether the hooks have anything to do (one relaxed load).
    bool Armed() const { return m_Armed.load(std::memory_order_relaxed); }

    // Game thread, from the SetLoadingStage detour, with the dials' scene seed.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                        const std::optional<fr::SeedResolution>& p_Seed) noexcept;

    // Game thread, at a scene clear: the summary line, then the load is forgotten.
    void OnSceneClear() noexcept;

    // Game thread, every play-mode frame: retries.
    void Tick() noexcept;

    // Any thread, from the ZEntityManager_ActivateEntity detour.
    void OnActivateEntity(const ZEntityRef* p_Entity) noexcept;

    // Any thread, from the SignalInputPin detour. Hot: a lock-free pointer
    // lookup unless the entity is a watched one.
    void OnInputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept;

private:
    // One ZRandEntity_int32 of an item.
    struct Rand {
        ZEntityType** entity;
        uint64_t id;
        uintptr_t minAddress;
        uintptr_t maxAddress;
        int32_t vanillaMin;
        int32_t vanillaMax;
        bool written;
    };

    struct Item {
        ZEntityType** entity;
        uint64_t id;
        uint64_t blueprint;
        fr::MoneyItemSize size;
        fr::MoneyKind kind;
        bool known;
        std::string owner; // the deciding setpiece, or why the kind is a guess
        std::string above; // the entities above it (logged with the first write)
        uint32_t spawnIndex;
        fr::LootAmountRoll roll;
        std::vector<Rand> rands;
        int tries = 0;        // walks made
        bool done = false;    // written, or given up
        bool retired = false; // its entity was freed and the address reused by another item: never touched again
        uint32_t reapplied = 0;
    };

    // Watched entities' values: item index << 2 | role. Role 0 (a dropped
    // entry, or a cleared slot) is rejected.
    static constexpr uint32_t kRoleDropped = 0;
    static constexpr uint32_t kRoleRand = 1;

    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                      const std::optional<fr::SeedResolution>& p_Seed);
    void Activate(ZEntityType** p_Entity, uint64_t p_Rid);
    // The kind from the setpieces above the entity (*p_Path: the deciding ones, or why it is a guess;
    // *p_Above: every entity above it, for the log).
    fr::MoneyKindGuess Classify(ZEntityType** p_Entity, fr::MoneyItemSize p_Size, std::string* p_Path,
                                std::string* p_Above);
    void Reactivate(size_t p_Item);
    void Retire(size_t p_Item, const std::string& p_Why);
    void Unwatch(const void* p_Entity);
    // Hit path only: the watched ZRandEntity_int32 is still one, at the same property addresses.
    bool StillRand(const Rand& p_Rand, std::string* p_Why) const;
    void TakeSeed(const fr::SeedResolution& p_Seed, const char* p_When);
    void Start(size_t p_Item); // classify (done at activation), roll, walk and write
    bool Walk(size_t p_Item);  // false when no ZRandEntity_int32 was found
    void Write(size_t p_Item, const char* p_When);
    void Watch(const void* p_Entity, size_t p_Item, uint32_t p_Role);
    void Pin(const ZEntityRef& p_Entity, uint32_t p_PinId);
    void Forget();
    std::string SeedText() const;

    bool m_On = false;
    std::atomic<bool> m_Armed {false};
    fr::PinWatchTable<1024> m_Watch; // the ZRandEntity_int32 written, for the pin hook

    // Under m_Mutex.
    mutable std::mutex m_Mutex;
    // Each kind's draw range (fr::LootAmountRangeOf with the configured bounds), by MoneyKind.
    std::array<fr::LootAmountRange, fr::kMoneyKinds> m_Ranges {
        fr::LootAmountRangeOf(fr::MoneyKind::Safe), fr::LootAmountRangeOf(fr::MoneyKind::Crate),
        fr::LootAmountRangeOf(fr::MoneyKind::Mule), fr::LootAmountRangeOf(fr::MoneyKind::Vault)};
    std::string m_Scene;
    std::optional<fr::SeedResolution> m_Seed;
    std::array<uint32_t, fr::kMoneyKinds> m_Spawns {}; // per fr::MoneyKind
    std::vector<Item> m_Items;
    std::unordered_map<const void*, size_t> m_ByEntity;
    std::vector<size_t> m_Waiting; // activated before the seed was known
    uint32_t m_Reactivations = 0;
    uint32_t m_WatchFull = 0;
};
