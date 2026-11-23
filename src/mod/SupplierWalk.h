#pragma once

// The supplier walk: what supplier_steer, supplier_steer_gear and
// supplier_force_pick (StockSteer.h) steer. Freelancer missions only.
// - At eLoading_SceneStarted one walk of the loaded bricks collects the
//   option helpers (TBLU fr::kCrateOptionTblu) and the random selectors
//   (fr::IsStockSelectorClass).
// - Each option is read a few levels deep: its choice's m_Weight and
//   m_Condition, its weight math's m_fA / m_fB, its repository id (named from
//   pro.repo: title, category, rarity, entitlement) and the m_bValue of its
//   flag members (the owned and entitled flags).
// - Each selector's m_Choices are mapped to options, and its m_Seed read.
// - All of it goes to the steering (StockSteer::Prepare). From scene playing
//   on, the walk passes on its play time, SupplierVisited and every entity
//   activated under an option (its offer).
// Lines start with "supplier walk". Every engine read is guarded; the entry
// points catch whatever their body throws.

#include "Guid.h"
#include "LootMerces.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ProRepository;
class StockSteer;
class ZDynamicObject;

class SupplierWalk {
public:
    explicit SupplierWalk(ProRepository& p_Repo) : m_Repo(p_Repo) {}

    // At config load. Nothing runs when off.
    void Configure(bool p_On) { m_On = p_On; }

    // At config load: the steering fed from the walk (or none).
    void SetSteer(StockSteer* p_Steer) { m_Steer = p_Steer; }

    bool On() const { return m_On; }

    // Game thread. The entry points catch and log whatever their body throws.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;
    void OnSceneClear() noexcept;
    void Tick(double p_GameSeconds) noexcept;

    // Any thread.
    void OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept;

    // Any thread, from the ZEntityManager_ActivateEntity detour: an entity
    // activated under an option during play is that option's offer.
    bool OffersOn() const { return m_OffersOn.load(std::memory_order_relaxed); }

    void OnActivateEntity(const ZEntityRef* p_Entity) noexcept;

private:
    // One entity whose properties were read.
    struct Entry {
        ZEntityType** entity;
        uint64_t id;
    };

    // One raw property value as read at the walk.
    struct Slot {
        size_t entry;
        std::string name;
        uintptr_t address;
        fr::PropKind kind;
        uint8_t bytes[16];
    };

    struct ItemInfo {
        std::string title;
        std::string commonName;
        std::string icon;
        double rarity = 0.0;
        bool hasRarity = false;
        std::string entitlement; // Evergreen_UnlockEntitlement (DLC / location; empty: base game)
    };

    // An option helper found by the walk, and the entity holding it (its
    // wrapper: what a selector's m_Choices points at).
    struct RawOption {
        ZEntityType** entity;
        ZEntityBlueprintFactoryBase* factory;
        ZEntityType** holder;
    };

    struct Option {
        ZEntityType** entity = nullptr;
        ZEntityBlueprintFactoryBase* factory = nullptr;
        ZEntityType** holder = nullptr;
        uint64_t id = 0;
        fr::Guid repo {};
        ZEntityType** choice = nullptr; // its ZRandomSelectorChoice
        size_t weightSlot = SIZE_MAX;
        size_t conditionSlot = SIZE_MAX;  // the choice's m_Condition entity's m_bValue
        std::vector<size_t> boolSlots {}; // the bool members (owned / entitled flags among them)
        std::vector<size_t> mathSlots {}; // the weight math's m_fA and m_fB
        std::string name {};              // its item's title
        bool firearm = false;
    };

    struct Selector {
        ZEntityType** entity;
        std::string label;
        std::vector<int> options;  // per choice: the option, or -1
        uint64_t seedId = 0;       // its m_Seed entity's id (0: none read)
        uintptr_t seedAddress = 0; // that entity's m_nValue (int32), 0 when not found
    };

    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context);
    void SceneClear();
    void FrameTick(double p_GameSeconds);
    void EventSent(const ZDynamicObject& p_Event);

    void Arm(bool p_Mission, std::string_view p_Scene);
    void Forget();
    void LoadItems();
    void FindSupplier(const TArray<SBrickAllocationInfo>& p_Bricks);
    void SetUpOptions(const std::vector<RawOption>& p_Raw, const std::vector<ZEntityType**>& p_Selectors);
    void ReadOption(size_t p_Option);
    void MapSelectors(const std::vector<ZEntityType**>& p_Selectors);
    void BuildLookup();
    int LookupOption(ZEntityType** p_Element) const;
    bool ReadChoices(ZEntityType** p_Selector, std::vector<int>& p_Options) const;
    void HandOver();
    size_t AddEntry(ZEntityType** p_Entity, std::string* p_Class = nullptr);
    void ReadSlots(size_t p_Entry);
    const std::string& ClassNameOf(ZEntityType* p_Type);
    const fr::PropKind& KindOf(const void* p_TypeId);

    ProRepository& m_Repo;
    StockSteer* m_Steer = nullptr;
    bool m_On = false;

    // Game thread, per load.
    std::string m_Scene;
    bool m_Mission = false;
    bool m_Found = false;
    bool m_Playing = false;
    double m_Play = 0.0;
    size_t m_Faults = 0;
    std::vector<Entry> m_Entries;
    std::vector<Slot> m_Slots;
    std::vector<Option> m_Options;
    std::vector<Selector> m_Selectors;
    std::unordered_map<const void*, size_t> m_LookupByHolder; // option lookup for m_Choices elements
    std::unordered_map<const void*, size_t> m_LookupByChoice;
    std::unordered_map<const void*, size_t> m_LookupByRoot;

    // Game thread, session.
    std::unordered_map<const void*, fr::PropKind> m_Kinds;     // property STypeID* -> kind
    std::unordered_map<const void*, std::string> m_ClassNames; // class STypeID* -> name
    std::map<fr::Guid, ItemInfo> m_Items;                      // pro.repo items
    bool m_ItemsReady = false;

    // Any thread.
    std::atomic<bool> m_EventsOn {false};
    std::atomic<bool> m_OffersOn {false};
    std::atomic<bool> m_VisitPending {false};
    mutable std::shared_mutex m_OfferMutex;
    std::unordered_map<const void*, uint32_t> m_OptionRoots; // option entity -> option index (under m_OfferMutex)
};
