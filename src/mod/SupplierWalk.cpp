// The supplier walk (see SupplierWalk.h). Read only: nothing here writes to
// the engine. Raw-pointer steps follow the guard pattern in Guard.h.

#include "SupplierWalk.h"

#include "ActorReads.h"  // SafeOwning
#include "Collections.h" // fr::ArrayElements
#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "Merces.h" // fr::JsonEventName
#include "ModLog.h"
#include "Repository.h"
#include "Scene.h"
#include "SceneOf.h"
#include "StockSteer.h"
#include "StockSteerMath.h"
#include "SupplierRules.h"

// EntityFactory.h needs ZEntity.h (and through it ZObject.h) first.
#include <Glacier/ZEntity.h>

#include <Glacier/EntityFactory.h>
#include <Glacier/Reflection.h>
#include <Glacier/ZObject.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <exception>
#include <set>
#include <string_view>

namespace {

constexpr int kMaxParentSteps = 12;         // logical parents climbed for an offer's option
constexpr size_t kMaxProperties = 256;      // read per entity
constexpr int64_t kMaxSubCount = 1'000'000; // sub-entities of one factory: more is garbage
constexpr size_t kOptionDepth = 3;          // levels of an option's subtree searched for flag members
constexpr size_t kMaxOptionMembers = 32;    // flag members kept per option
constexpr size_t kMaxSelectorsRaw = 50'000; // selectors collected by the walk
constexpr size_t kMaxSelectors = 64;        // selectors with options kept
constexpr size_t kMaxChoices = 4'096;

template <size_t N>
void CopyPrintable(const char* p_Text, size_t p_Size, char (&p_Out)[N]) {
    size_t i = 0;

    for (; p_Text && i < p_Size && p_Text[i] && i + 1 < N; ++i) {
        if (p_Text[i] < 0x20 || p_Text[i] > 0x7E) {
            p_Out[0] = '\0';
            return;
        }

        p_Out[i] = p_Text[i];
    }

    p_Out[i] = '\0';
}

// An event as JSON (empty on a fault).
std::string EventJson(const ZDynamicObject& p_Event) {
    std::string s_Json;
    ReadDynamicObjectJson(p_Event, &s_Json);
    return s_Json;
}

constexpr std::string_view kTag = "supplier walk";

// --- Guarded workers ---

struct PropertyEntry {
    const void* typeId; // STypeID*
    int64_t offset;
    uint32_t id;
    bool getter;
};

struct PropertyList {
    size_t total;
    size_t count;
    PropertyEntry entries[kMaxProperties];
};

__declspec(noinline) void PropertyListOf(const ZEntityType* p_Type, PropertyList* p_Out) {
    p_Out->total = 0;
    p_Out->count = 0;
    const TArray<SPropertyData>* s_Data = p_Type->m_pPropertyData;

    if (!s_Data)
        return;

    p_Out->total = s_Data->size();

    for (size_t i = 0; i < p_Out->total && p_Out->count < kMaxProperties; ++i) {
        const SPropertyData& s_Property = (*s_Data)[i];
        const SPropertyInfo* s_Info = s_Property.m_pPropertyInfo;

        if (!s_Info)
            continue;

        p_Out->entries[p_Out->count++] = {s_Info->m_Type, s_Property.m_nPropertyOffset, s_Property.m_nPropertyID,
                                          (s_Info->m_Flags & EPropertyInfoFlags::E_HAS_GETTER_SETTER) != 0};
    }
}

__declspec(noinline) DWORD SafePropertyList(const ZEntityType* p_Type, PropertyList* p_Out) {
    DWORD s_Code = 0;

    __try {
        PropertyListOf(p_Type, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct TypeNameOut {
    char name[128];
};

__declspec(noinline) void TypeNameOf(const STypeID* p_TypeId, TypeNameOut* p_Out) {
    p_Out->name[0] = '\0';
    const IType* s_TypeInfo = p_TypeId->GetTypeInfo();
    const char* s_Name = s_TypeInfo ? s_TypeInfo->pszTypeName : nullptr;
    CopyPrintable(s_Name, sizeof(p_Out->name), p_Out->name);
}

__declspec(noinline) DWORD SafeTypeName(const STypeID* p_TypeId, TypeNameOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        TypeNameOf(p_TypeId, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void PropertyNameOf(const ZEntityType* p_Type, uint32_t p_Id, TypeNameOut* p_Out) {
    p_Out->name[0] = '\0';
    const SPropertyData* s_Property = p_Type->FindProperty(p_Id);

    if (!s_Property || !s_Property->m_pPropertyInfo)
        return;

    const SNamedPropertyInfo* s_Named = s_Property->GetPropertyInfo();

    if (s_Named->m_nPropertyID == p_Id)
        CopyPrintable(s_Named->m_pszPropertyName, sizeof(p_Out->name), p_Out->name);
}

__declspec(noinline) DWORD SafePropertyName(const ZEntityType* p_Type, uint32_t p_Id, TypeNameOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        PropertyNameOf(p_Type, p_Id, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void EntityTypeOf(ZEntityType** p_Entity, const ZEntityType** p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    *p_Out = s_Entity ? s_Entity->GetType() : nullptr;
}

__declspec(noinline) DWORD SafeEntityType(ZEntityType** p_Entity, const ZEntityType** p_Out) {
    DWORD s_Code = 0;

    __try {
        EntityTypeOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void ClassTypeOf(const ZEntityType* p_Type, const void** p_Out) {
    *p_Out = nullptr;
    const TArray<SInterfaceData>* s_Interfaces = p_Type->m_pInterfaceData;

    if (s_Interfaces && s_Interfaces->size() > 0)
        *p_Out = (*s_Interfaces)[0].m_Type;
}

__declspec(noinline) DWORD SafeClassType(const ZEntityType* p_Type, const void** p_Out) {
    DWORD s_Code = 0;

    __try {
        ClassTypeOf(p_Type, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

uint64_t FactoryRid(ZEntityBlueprintFactoryBase* p_Factory) {
    uint64_t s_Rid = 0;

    if (!p_Factory || ReadFactoryRid(p_Factory, &s_Rid))
        return 0;

    return s_Rid;
}

ZEntityType** OwningEntity(ZEntityType** p_Entity) {
    ZEntityType** s_Out = nullptr;

    if (!p_Entity || SafeOwning(p_Entity, &s_Out) || s_Out == p_Entity)
        return nullptr;

    return s_Out;
}

size_t SlotSize(const fr::PropKind& p_Kind) {
    switch (p_Kind.shape) {
        case fr::PropShape::Numeric: return fr::NumericKindSize(p_Kind.numeric);
        case fr::PropShape::Bool: return 1;
        case fr::PropShape::Guid: return 16;
        case fr::PropShape::EntityRef:
        case fr::PropShape::TypedEntityRef: return 8; // the entity pointer
        case fr::PropShape::Array: return 16;         // begin, end
        case fr::PropShape::Other: return 0;
    }

    return 0;
}

// The entity a slot holding an entity reference points at, or null.
ZEntityType** SlotEntity(const fr::PropKind& p_Kind, const uint8_t* p_Bytes) {
    if (p_Kind.shape != fr::PropShape::EntityRef && p_Kind.shape != fr::PropShape::TypedEntityRef)
        return nullptr;

    uint64_t s_Pointer = 0;
    std::memcpy(&s_Pointer, p_Bytes, sizeof(s_Pointer));
    return fr::IsPlausibleEntityPointer(s_Pointer) ? reinterpret_cast<ZEntityType**>(s_Pointer) : nullptr;
}

} // namespace

// --- Setup ---

// pro.repo items (name, category, rarity, entitlement) for the repository
// ids; read once per session (tried again each scene until it works).
void SupplierWalk::LoadItems() {
    if (m_ItemsReady)
        return;

    std::string s_Why;

    if (!m_Repo.Ready(s_Why)) {
        LogTo(fr::LogTopic::SupplierCrates, "supplier walk: pro.repo not ready ({}); items are not named this scene",
              s_Why);
        return;
    }

    std::vector<RepoLoadoutEntry> s_Entries;
    size_t s_Overflow = 0;
    size_t s_Scanned = 0;

    if (const uint32_t s_Code = m_Repo.ScanLoadoutItems(s_Entries, &s_Overflow, &s_Scanned)) {
        LogTo(fr::LogTopic::SupplierCrates,
              "supplier walk: pro.repo item scan faulted ({}); items are not named this scene", FaultText(s_Code));
        return;
    }

    for (const RepoLoadoutEntry& s_Entry : s_Entries) {
        ItemInfo& s_Item = m_Items[s_Entry.guid];
        s_Item.title = s_Entry.title;
        s_Item.commonName = s_Entry.commonName;
        s_Item.icon = s_Entry.icon;
        s_Item.rarity = s_Entry.rarity;
        s_Item.hasRarity = s_Entry.hasRarity;
        std::vector<RepoPairText> s_Pairs;

        if (s_Entry.entry && !m_Repo.DumpPairs(s_Entry.entry, s_Pairs)) {
            for (const RepoPairText& s_Pair : s_Pairs) {
                if (std::string_view(s_Pair.key) == "Evergreen_UnlockEntitlement")
                    s_Item.entitlement = s_Pair.value;
            }
        }
    }

    m_ItemsReady = true;
}

const std::string& SupplierWalk::ClassNameOf(ZEntityType* p_Type) {
    static const std::string s_None;
    const void* s_TypeId = nullptr;

    if (!p_Type || SafeClassType(p_Type, &s_TypeId) || !s_TypeId)
        return s_None;

    const auto s_Cached = m_ClassNames.find(s_TypeId);

    if (s_Cached != m_ClassNames.end())
        return s_Cached->second;

    TypeNameOut s_Name {};

    if (SafeTypeName(static_cast<const STypeID*>(s_TypeId), &s_Name))
        s_Name.name[0] = '\0';

    return m_ClassNames.emplace(s_TypeId, std::string(s_Name.name)).first->second;
}

const fr::PropKind& SupplierWalk::KindOf(const void* p_TypeId) {
    static const fr::PropKind s_Other;

    if (!p_TypeId)
        return s_Other;

    auto s_Cached = m_Kinds.find(p_TypeId);

    if (s_Cached == m_Kinds.end()) {
        TypeNameOut s_Name {};

        if (SafeTypeName(static_cast<const STypeID*>(p_TypeId), &s_Name))
            s_Name.name[0] = '\0';

        s_Cached = m_Kinds.emplace(p_TypeId, fr::PropKindOf(s_Name.name)).first;
    }

    return s_Cached->second;
}

// --- Loading ---

void SupplierWalk::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    if (On())
        Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context); });
}

void SupplierWalk::Arm(bool p_Mission, std::string_view p_Scene) {
    // A restart may load again without a ClearScene in between.
    SceneClear();
    m_Scene = std::string(p_Scene);
    m_Mission = p_Mission;
    m_EventsOn.store(p_Mission, std::memory_order_relaxed);
}

void SupplierWalk::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) {
    if (!p_Context)
        return;

    const std::string_view s_Type = p_Context->m_SceneInitParameters.m_Type.ToStringView();
    const std::string s_Scene =
        SceneOf(p_Context); // a full variant (Landslide, The Author) counts as its contract's scene

    if (p_Stage == ESceneLoadingStage::eLoading_AssetsLoaded ||
        (p_Stage == ESceneLoadingStage::eLoading_SceneStarted && m_Scene != s_Scene))
        Arm(fr::IsFreelancerMission(s_Type, s_Scene), s_Scene);

    if (!m_Mission)
        return;

    if (p_Stage == ESceneLoadingStage::eLoading_SceneStarted && !m_Found) {
        m_Found = true;
        LoadItems();
        FindSupplier(p_Context->m_aLoadedBricks);
    }
    else if (p_Stage == ESceneLoadingStage::eLoading_ScenePlaying && m_Found && !m_Playing) {
        m_Playing = true;
        m_Play = 0.0;

        // Offers: activations under an option from now on (load-time ones are its own members).
        if (!m_Options.empty())
            m_OffersOn.store(true, std::memory_order_release);
    }
}

// --- Entities and their values ---

size_t SupplierWalk::AddEntry(ZEntityType** p_Entity, std::string* p_Class) {
    std::string s_Class;
    uint64_t s_Id = 0;

    if (ReadEntityClassName(p_Entity, s_Class, &s_Id))
        ++m_Faults;

    if (p_Class)
        *p_Class = std::move(s_Class);

    m_Entries.push_back({p_Entity, s_Id});
    return m_Entries.size() - 1;
}

// A slot for each readable property of a known shape (getters skipped).
void SupplierWalk::ReadSlots(size_t p_Entry) {
    // Static: too large for the stack (game thread only).
    static PropertyList s_List;
    ZEntityType** const s_Entity = m_Entries[p_Entry].entity;
    const ZEntityType* s_Type = nullptr;

    if (SafeEntityType(s_Entity, &s_Type) || !s_Type || SafePropertyList(s_Type, &s_List)) {
        ++m_Faults;
        return;
    }

    for (size_t i = 0; i < s_List.count; ++i) {
        const PropertyEntry& s_Entry = s_List.entries[i];

        if (s_Entry.getter)
            continue;

        const fr::PropKind s_Kind = KindOf(s_Entry.typeId);
        const size_t s_Size = SlotSize(s_Kind);

        if (!s_Size)
            continue;

        TypeNameOut s_Name {};
        SafePropertyName(s_Type, s_Entry.id, &s_Name);
        const uintptr_t s_Address = reinterpret_cast<uintptr_t>(s_Entity) + s_Entry.offset;
        Slot s_Slot {p_Entry, s_Name.name[0] ? s_Name.name : fmt::format("{:08x}", s_Entry.id), s_Address, s_Kind, {}};

        if (!ReadEngineBytes(s_Address, s_Slot.bytes, s_Size))
            m_Slots.push_back(std::move(s_Slot));
    }
}

// --- Supplier ---

void SupplierWalk::FindSupplier(const TArray<SBrickAllocationInfo>& p_Bricks) {
    std::vector<RawOption> s_RawOptions;
    std::vector<ZEntityType**> s_RawSelectors;
    const auto s_Start = std::chrono::steady_clock::now();
    const WalkStats s_Stats = VisitAllEntities(p_Bricks, [&](const VisitedEntity& p_Visited) {
        if (!p_Visited.type)
            return false;

        if (p_Visited.factory && FactoryRid(p_Visited.factory) == fr::kCrateOptionTblu) {
            s_RawOptions.push_back({p_Visited.entity, p_Visited.factory, p_Visited.parent});
            return true;
        }

        if (fr::IsStockSelectorClass(ClassNameOf(p_Visited.type)) && s_RawSelectors.size() < kMaxSelectorsRaw) {
            s_RawSelectors.push_back(p_Visited.entity);
            return false;
        }

        return false;
    });
    m_Faults += s_Stats.faults;

    SetUpOptions(s_RawOptions, s_RawSelectors);
    HandOver();
    LogTo(fr::LogTopic::SupplierCrates,
          "supplier walk: {}: {} bricks, {} entities{}, {:.1f} ms: {} options, {} selectors, "
          "{} of them drawing options; {} pro.repo items; {} faults",
          ShortSceneName(m_Scene), s_Stats.bricks.size(), s_Stats.visited,
          s_Stats.capped ? " (stopped at the entity cap)" : "",
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count(),
          m_Options.size(), s_RawSelectors.size(), m_Selectors.size(), m_Items.size(), m_Faults);
}

// The option helpers the walk found: each read, the selectors drawing them
// mapped, and the option roots published for the offer lookup.
void SupplierWalk::SetUpOptions(const std::vector<RawOption>& p_Raw, const std::vector<ZEntityType**>& p_Selectors) {
    for (const RawOption& s_Raw : p_Raw)
        m_Options.push_back({s_Raw.entity, s_Raw.factory, s_Raw.holder});

    for (size_t i = 0; i < m_Options.size(); ++i)
        ReadOption(i);

    BuildLookup();
    MapSelectors(p_Selectors);

    std::unique_lock s_Lock(m_OfferMutex);
    m_OptionRoots.clear();

    for (size_t i = 0; i < m_Options.size(); ++i)
        m_OptionRoots.emplace(m_Options[i].entity, static_cast<uint32_t>(i));
}

// One option: its known members and flag members (searched kOptionDepth
// levels deep) and the choice's condition, with their values.
void SupplierWalk::ReadOption(size_t p_Option) {
    Option& s_Option = m_Options[p_Option];
    s_Option.id = m_Entries[AddEntry(s_Option.entity)].id;

    struct Step {
        ZEntityBlueprintFactoryBase* factory;
        ZEntityType** parent;
        size_t depth;
    };

    std::deque<Step> s_Queue {{s_Option.factory, s_Option.entity, 1}};
    size_t s_Kept = 0;
    ZEntityType** s_Condition = nullptr; // the choice's m_Condition

    while (!s_Queue.empty()) {
        const Step s_Step = s_Queue.front();
        s_Queue.pop_front();
        int64_t s_Subs = 0;

        if (!s_Step.factory || ReadSubEntityCount(s_Step.factory, &s_Subs) || s_Subs < 0 || s_Subs > kMaxSubCount)
            continue;

        for (int i = 0; i < s_Subs; ++i) {
            ZEntityType** s_Sub = nullptr;
            ZEntityBlueprintFactoryBase* s_SubFactory = nullptr;

            if (ReadSubEntityAt(s_Step.factory, s_Step.parent, i, &s_Sub, &s_SubFactory) || !s_Sub)
                continue;

            if (s_SubFactory && s_Step.depth < kOptionDepth)
                s_Queue.push_back({s_SubFactory, s_Sub, s_Step.depth + 1});

            std::string s_Class;
            uint64_t s_Id = 0;

            if (ReadEntityClassName(s_Sub, s_Class, &s_Id))
                continue;

            const bool s_Known = s_Id == fr::kOptionChoiceId || s_Id == fr::kOptionWeightMathId ||
                                 s_Id == fr::kOptionCostId || s_Id == fr::kOptionStateId ||
                                 s_Id == fr::kOptionState2Id || s_Id == fr::kOptionKeyId || s_Id == fr::kOptionKey2Id;

            if (!s_Known && (!fr::IsStockFlagClass(s_Class) || s_Kept >= kMaxOptionMembers))
                continue;

            const size_t s_Entry = AddEntry(s_Sub);
            const size_t s_FromSlot = m_Slots.size();
            ReadSlots(s_Entry);
            ++s_Kept;

            for (size_t k = s_FromSlot; k < m_Slots.size(); ++k) {
                const Slot& s_Slot = m_Slots[k];

                if (s_Id == fr::kOptionChoiceId && s_Slot.name == "m_Weight") {
                    s_Option.weightSlot = k;
                    s_Option.choice = s_Sub;
                    continue;
                }

                if (s_Id == fr::kOptionChoiceId && s_Slot.name == "m_Condition" &&
                    (s_Slot.kind.shape == fr::PropShape::EntityRef ||
                     s_Slot.kind.shape == fr::PropShape::TypedEntityRef)) {
                    if (ZEntityType** s_Entity = SlotEntity(s_Slot.kind, s_Slot.bytes))
                        s_Condition = s_Entity;

                    continue;
                }

                if (s_Id == fr::kOptionWeightMathId) {
                    if (s_Slot.name == "m_fA" || s_Slot.name == "m_fB")
                        s_Option.mathSlots.push_back(k);

                    continue;
                }

                if (s_Id == fr::kOptionCostId)
                    continue;

                if (s_Slot.kind.shape == fr::PropShape::Guid) {
                    if ((s_Id == fr::kOptionKeyId || s_Option.repo.IsEmpty()) && s_Class == "ZItemRepositoryKeyEntity")
                        std::memcpy(&s_Option.repo, s_Slot.bytes, sizeof(s_Option.repo));

                    continue;
                }

                if (s_Slot.kind.shape == fr::PropShape::Bool)
                    s_Option.boolSlots.push_back(k);
            }
        }
    }

    // The choice's condition: whether the selector may pick it.
    if (s_Condition) {
        const size_t s_Entry = AddEntry(s_Condition);
        const size_t s_FromSlot = m_Slots.size();
        ReadSlots(s_Entry);

        for (size_t k = s_FromSlot; k < m_Slots.size(); ++k) {
            if (m_Slots[k].name == "m_bValue" && m_Slots[k].kind.shape == fr::PropShape::Bool) {
                s_Option.conditionSlot = k;
                s_Option.boolSlots.push_back(k);
            }
        }
    }

    const auto s_Info = m_Items.find(s_Option.repo);

    if (m_ItemsReady && s_Info != m_Items.end()) {
        const ItemInfo& s_Item = s_Info->second;
        s_Option.name = s_Item.title.empty() ? s_Item.commonName : s_Item.title;
        s_Option.firearm =
            fr::ClassifyStockItem(s_Item.commonName, s_Item.title, s_Item.icon) == fr::StockItemKind::Weapon;
    }

    if (s_Option.name.empty())
        s_Option.name = s_Option.repo.IsEmpty() ? fmt::format("{:016x}", s_Option.id) : fr::FormatGuid(s_Option.repo);
}

// What a selector's m_Choices element may be: the entity holding an option
// (its wrapper), the option's ZRandomSelectorChoice, the option itself, or an
// entity up to four owners below one of those.
void SupplierWalk::BuildLookup() {
    std::unordered_map<const void*, size_t> s_HolderCount;
    m_LookupByHolder.clear();
    m_LookupByChoice.clear();
    m_LookupByRoot.clear();

    for (size_t i = 0; i < m_Options.size(); ++i) {
        m_LookupByRoot.emplace(m_Options[i].entity, i);
        ++s_HolderCount[m_Options[i].holder];

        if (m_Options[i].choice)
            m_LookupByChoice.emplace(m_Options[i].choice, i);
    }

    // A holder of several options names none of them.
    for (size_t i = 0; i < m_Options.size(); ++i) {
        if (m_Options[i].holder && s_HolderCount[m_Options[i].holder] == 1)
            m_LookupByHolder.emplace(m_Options[i].holder, i);
    }
}

// The option a m_Choices element stands for (BuildLookup), or -1.
int SupplierWalk::LookupOption(ZEntityType** p_Element) const {
    ZEntityType** s_At = p_Element;

    for (size_t s_Step = 0; s_At && s_Step < 4; ++s_Step) {
        for (const auto* s_Map : {&m_LookupByHolder, &m_LookupByChoice, &m_LookupByRoot}) {
            const auto s_Found = s_Map->find(s_At);

            if (s_Found != s_Map->end())
                return static_cast<int>(s_Found->second);
        }

        s_At = OwningEntity(s_At);
    }

    return -1;
}

// A selector's m_Choices: per element, its option (or -1). False when the
// array is unreadable.
bool SupplierWalk::ReadChoices(ZEntityType** p_Selector, std::vector<int>& p_Options) const {
    p_Options.clear();
    const PropertyLocation s_Location = LocateProperty(p_Selector, "m_Choices");

    if (s_Location.status != PropertyLocation::Status::Ok || !s_Location.isArray || s_Location.elementSize != 16)
        return false;

    struct {
        uintptr_t begin;
        uintptr_t end;
        uint64_t flags;
    } s_Array {};

    if (ReadEngineBytes(s_Location.address, &s_Array, sizeof(s_Array)))
        return false;

    const auto s_Span =
        fr::ArrayElements(s_Location.address, s_Array.begin, s_Array.end, s_Array.flags, 16, kMaxChoices);

    if (!s_Span)
        return false;

    for (size_t k = 0; k < s_Span->count; ++k) {
        uint64_t s_Pointer = 0;
        int s_Option = -1;

        if (!ReadEngineBytes(s_Span->begin + 16 * k, &s_Pointer, sizeof(s_Pointer)) &&
            fr::IsPlausibleEntityPointer(s_Pointer))
            s_Option = LookupOption(reinterpret_cast<ZEntityType**>(s_Pointer));

        p_Options.push_back(s_Option);
    }

    return true;
}

// The selectors whose m_Choices hold options, with their seed.
void SupplierWalk::MapSelectors(const std::vector<ZEntityType**>& p_Selectors) {
    for (ZEntityType** s_Selector : p_Selectors) {
        if (m_Selectors.size() >= kMaxSelectors)
            break;

        Selector s_Found {s_Selector, {}, {}};

        if (!ReadChoices(s_Selector, s_Found.options) ||
            std::none_of(s_Found.options.begin(), s_Found.options.end(), [](int p_Option) { return p_Option >= 0; }))
            continue;

        std::string s_Class;
        const size_t s_Entry = AddEntry(s_Selector, &s_Class);
        s_Found.label = fmt::format("selector #{} {:016x} ({})", m_Selectors.size(), m_Entries[s_Entry].id,
                                    s_Class.empty() ? "?" : s_Class);
        const size_t s_FromSlot = m_Slots.size();
        ReadSlots(s_Entry);

        // Its seed: the m_Seed entity's id and int32 value.
        for (size_t i = s_FromSlot; i < m_Slots.size() && s_Found.seedId == 0; ++i) {
            ZEntityType** s_Seed =
                m_Slots[i].name == "m_Seed" ? SlotEntity(m_Slots[i].kind, m_Slots[i].bytes) : nullptr;
            std::string s_SeedClass;

            if (!s_Seed || ReadEntityClassName(s_Seed, s_SeedClass, &s_Found.seedId))
                continue;

            const PropertyLocation s_Value = LocateProperty(s_Seed, "m_nValue");

            if (s_Value.status == PropertyLocation::Status::Ok && s_Value.typeSize == 4)
                s_Found.seedAddress = s_Value.address;
        }

        m_Selectors.push_back(std::move(s_Found));
    }
}

// The options and selectors as the steering needs them (StockSteer.h).
void SupplierWalk::HandOver() {
    if (!m_Steer || !m_Steer->On())
        return;

    std::vector<StockSteer::OptionInfo> s_Options;
    s_Options.reserve(m_Options.size());

    for (const Option& s_Option : m_Options) {
        StockSteer::OptionInfo s_Info;
        s_Info.name = s_Option.name;
        s_Info.firearm = s_Option.firearm;
        const auto s_Item = m_Items.find(s_Option.repo);

        if (m_ItemsReady && s_Item != m_Items.end()) {
            s_Info.category = s_Item->second.icon;
            s_Info.entitlement = s_Item->second.entitlement;
            s_Info.hasRarity = s_Item->second.hasRarity;
            s_Info.rarity = static_cast<int32_t>(s_Item->second.rarity);
        }

        if (s_Option.weightSlot != SIZE_MAX)
            s_Info.weightAddress = m_Slots[s_Option.weightSlot].address;

        if (s_Option.conditionSlot != SIZE_MAX)
            s_Info.conditionAddress = m_Slots[s_Option.conditionSlot].address;

        for (const size_t k : s_Option.mathSlots) {
            const Entry& s_Member = m_Entries[m_Slots[k].entry];

            if (m_Slots[k].name == "m_fA") {
                s_Info.faAddress = m_Slots[k].address;
                s_Info.math = s_Member.entity;
            }
            else if (m_Slots[k].name == "m_fB") {
                s_Info.fbAddress = m_Slots[k].address;
            }
        }

        // The owned / entitled flags (each member once: some are reached at two depths).
        std::set<uint64_t> s_Seen;

        for (const size_t k : s_Option.boolSlots) {
            const Slot& s_Slot = m_Slots[k];
            const uint64_t s_Id = m_Entries[s_Slot.entry].id;

            if (s_Slot.name != "m_bValue" || !s_Seen.insert(s_Id).second)
                continue;

            if (s_Id == fr::kEntitledFlagId)
                s_Info.entitledFlag = s_Slot.address;
            else if (std::find(std::begin(fr::kUnownedFlagIds), std::end(fr::kUnownedFlagIds), s_Id) !=
                     std::end(fr::kUnownedFlagIds))
                s_Info.unownedFlags.push_back(s_Slot.address);
        }

        s_Options.push_back(std::move(s_Info));
    }

    std::vector<StockSteer::SelectorInfo> s_Selectors;

    for (const Selector& s_Selector : m_Selectors) {
        StockSteer::SelectorInfo s_Info;
        s_Info.entity = s_Selector.entity;
        s_Info.seedId = s_Selector.seedId;
        s_Info.seedAddress = s_Selector.seedAddress;
        s_Info.label = s_Selector.label;

        for (const int s_Option : s_Selector.options) {
            if (s_Option >= 0)
                s_Info.options.push_back(static_cast<size_t>(s_Option));
        }

        s_Selectors.push_back(std::move(s_Info));
    }

    m_Steer->Prepare(m_Scene, std::move(s_Options), std::move(s_Selectors));
}

// --- Per frame ---

void SupplierWalk::Tick(double p_GameSeconds) noexcept {
    if (On())
        Contained(kTag, "frame update", [&] { FrameTick(p_GameSeconds); });
}

void SupplierWalk::FrameTick(double p_GameSeconds) {
    if (!m_Found || !m_Playing || !m_Steer)
        return;

    m_Play += p_GameSeconds;
    m_Steer->Tick(m_Play);

    if (m_VisitPending.exchange(false, std::memory_order_relaxed))
        m_Steer->OnSupplierVisited();
}

// An entity activated under an option (its logical parents) during play: the
// option's offer, handed to the steering.
void SupplierWalk::OnActivateEntity(const ZEntityRef* p_Entity) noexcept {
    if (!p_Entity || !p_Entity->m_pObj || !m_Steer || !m_OffersOn.load(std::memory_order_acquire))
        return;

    Contained(kTag, "activate entity", [&] {
        std::vector<ZEntityType**> s_Chain;
        ZEntityType** s_At = p_Entity->m_pObj;

        for (int i = 0; i < kMaxParentSteps; ++i) {
            ZEntityType** s_Parent = nullptr;

            if (ReadLogicalParent(s_At, &s_Parent) || !s_Parent ||
                std::find(s_Chain.begin(), s_Chain.end(), s_Parent) != s_Chain.end())
                break;

            s_Chain.push_back(s_Parent);
            s_At = s_Parent;
        }

        uint32_t s_Option = UINT32_MAX;
        {
            std::shared_lock s_Lock(m_OfferMutex);

            for (ZEntityType** s_Parent : s_Chain) {
                const auto s_Found = m_OptionRoots.find(s_Parent);

                if (s_Found != m_OptionRoots.end()) {
                    s_Option = s_Found->second;
                    break;
                }
            }
        }

        // Every activation (the steering keeps the first per option itself).
        if (s_Option != UINT32_MAX)
            m_Steer->OnOffer(s_Option);
    });
}

// --- Events (any thread) ---

void SupplierWalk::OnEventSent(uint32_t, const ZDynamicObject& p_Event) noexcept {
    if (On() && m_EventsOn.load(std::memory_order_relaxed))
        Contained(kTag, "event", [&] { EventSent(p_Event); });
}

void SupplierWalk::EventSent(const ZDynamicObject& p_Event) {
    // The next frame tells the steering.
    if (fr::IsSupplierVisitEvent(fr::JsonEventName(EventJson(p_Event))))
        m_VisitPending.store(true, std::memory_order_relaxed);
}

// --- Clear ---

// Runs even when the settings are off now: they may have been turned off mid-mission.
void SupplierWalk::OnSceneClear() noexcept {
    Contained(kTag, "scene clear", [&] { SceneClear(); });
}

void SupplierWalk::SceneClear() {
    m_EventsOn.store(false, std::memory_order_relaxed);

    if (m_Steer)
        m_Steer->Clear();

    Forget();
}

void SupplierWalk::Forget() {
    m_OffersOn.store(false, std::memory_order_release);
    {
        std::unique_lock s_Lock(m_OfferMutex);
        m_OptionRoots.clear();
    }

    m_VisitPending.store(false, std::memory_order_relaxed);
    m_Scene.clear();
    m_Mission = false;
    m_Found = false;
    m_Playing = false;
    m_Play = 0.0;
    m_Faults = 0;
    m_Entries.clear();
    m_Slots.clear();
    m_Options.clear();
    m_Selectors.clear();
    m_LookupByHolder.clear();
    m_LookupByChoice.clear();
    m_LookupByRoot.clear();
}
