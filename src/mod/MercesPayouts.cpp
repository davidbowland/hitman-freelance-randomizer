// Merces payouts (see MercesPayouts.h). The engine writes are the SetProperty
// on the payout table's m_nValue and, in the safehouse, on the prestige
// wall's display copies, and the int32 payload of the display polls' signals.

#include "MercesPayouts.h"

#include "BlueprintRead.h"
#include "BrickMemory.h"
#include "ClientReqs.h" // PayoutObjectiveName
#include "ActorReads.h" // SafeOwning
#include "EngineText.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "EntityReads.h"
#include "Scene.h"
#include "SettingsTable.h" // fr::PercentText
#include "SceneOf.h"
#include "SceneStage.h"
#include "Variants.h"
#include "WrappedText.h"

#include <Functions.h>
#include <Globals.h>

#include <Glacier/Reflection.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZResourceID.h>
#include <Glacier/ZTypeRegistry.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <utility>

namespace {

constexpr double kLateReadSeconds = 5.0;        // of play after eLoading_ScenePlaying
constexpr size_t kMaxProperties = 256;          // listed per entity
constexpr size_t kMaxParents = 12;              // logical-parent chain logged per table instance
constexpr size_t kMaxEntryTreeEntities = 2'000; // entities walked per display entry's template instance
constexpr uint64_t kMaxDisplayPinLines = 400;   // display poll signals logged per scene (108 per safehouse load)
constexpr size_t kMaxDisplayCheckLines = 60;    // display entry check lines per moment

// Copies at most N - 1 characters of printable ASCII and terminates p_Out;
// any other character empties it (a garbage pointer, not a name).
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

std::string ClassText(const std::string& p_Class) {
    return p_Class.empty() ? std::string("<no class>") : p_Class;
}

// Raw-pointer steps follow the guard pattern in Guard.h.

struct PropertyEntry {
    const void* typeId; // STypeID*
    int64_t offset;
    uint32_t id;
    bool getter;
};

struct PropertyList {
    size_t total; // the type's properties
    size_t count; // listed in entries
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
    char name[96];
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

// The walk's per-entity check: the type's sub-entity id.
__declspec(noinline) void TypeEntityIdOf(const ZEntityType* p_Type, uint64_t* p_Out) {
    *p_Out = p_Type->m_nEntityID;
}

__declspec(noinline) DWORD SafeTypeEntityId(const ZEntityType* p_Type, uint64_t* p_Out) {
    DWORD s_Code = 0;

    __try {
        TypeEntityIdOf(p_Type, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// The entity's sub-entity id only (0 without a type): the pin hook's check,
// made for every int32 payload.
__declspec(noinline) void EntityIdOf(ZEntityType** p_Entity, uint64_t* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;
    *p_Out = s_Type ? s_Type->m_nEntityID : 0;
}

__declspec(noinline) DWORD SafeEntityId(ZEntityType** p_Entity, uint64_t* p_Out) {
    DWORD s_Code = 0;

    __try {
        EntityIdOf(p_Entity, p_Out);
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

// Writes the payload of an int32 pin signal in place (the receivers get it).
__declspec(noinline) void WritePayloadOf(const ZObjectRef* p_Data, int32_t p_Value) {
    void* s_Bytes = p_Data->GetData();

    if (s_Bytes)
        std::memcpy(s_Bytes, &p_Value, sizeof(p_Value));
}

__declspec(noinline) DWORD SafeWritePayload(const ZObjectRef* p_Data, int32_t p_Value) {
    DWORD s_Code = 0;

    __try {
        WritePayloadOf(p_Data, p_Value);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// The engine's property setter, without change handlers: the write stands
// for a different blueprint value before anything reads it.
struct SetValueArgs {
    ZEntityType** entity;
    int32_t value;
    bool ok;
};

__declspec(noinline) void SetValueOf(SetValueArgs* p_Args) {
    ZEntityRef s_Entity(p_Args->entity);
    p_Args->ok = s_Entity.SetProperty(fr::kValueIntValueId, ZObjectRef::From<int32_t>(p_Args->value), false);
}

__declspec(noinline) DWORD SafeSetValue(SetValueArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SetValueOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// m_nValue read through the entity's property table.
struct ValueRead {
    enum class Status {
        Ok,
        NotRaw, // missing, with a getter, or not an int32
        Fault,
    };

    Status status;
    int32_t value;
    uint32_t faultCode;
};

ValueRead ReadValue(ZEntityType** p_Entity) {
    const PropertyLocation s_Location = LocateProperty(p_Entity, "m_nValue");

    if (s_Location.status == PropertyLocation::Status::Fault)
        return {ValueRead::Status::Fault, 0, s_Location.faultCode};

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != sizeof(int32_t) ||
        std::strcmp(s_Location.typeName, "int32") != 0) {
        return {ValueRead::Status::NotRaw, 0, 0};
    }

    int32_t s_Value = 0;

    if (const uint32_t s_Code = ReadEngineBytes(s_Location.address, &s_Value, sizeof(s_Value)))
        return {ValueRead::Status::Fault, 0, s_Code};

    return {ValueRead::Status::Ok, s_Value, 0};
}

// A raw int32 property (no getter) found by id, or empty (missing, not
// int32, or a fault).
std::optional<int32_t> ReadInt32ById(ZEntityType** p_Entity, uint32_t p_Id) {
    const PropertyLocation s_Location = LocatePropertyById(p_Entity, p_Id);
    int32_t s_Value = 0;

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != sizeof(int32_t) ||
        std::strcmp(s_Location.typeName, "int32") != 0 ||
        ReadEngineBytes(s_Location.address, &s_Value, sizeof(s_Value)))
        return std::nullopt;

    return s_Value;
}

constexpr std::string_view kTag = "merces";

// " x1.234", plus " (draw 3)" when out-of-range draws were redrawn, or
// " (fallback, center +2 x 50)" when every draw was out of range.
std::string RollText(const fr::TruncatedDraw& p_Roll) {
    if (p_Roll.fallback)
        return fmt::format(" x{:.3f} (fallback, center {:+} x {})", p_Roll.value, p_Roll.jitterSteps, fr::kPayoutStep);

    return fmt::format(" x{:.3f}{}", p_Roll.value,
                       p_Roll.tries > 1 ? fmt::format(" (draw {})", p_Roll.tries) : std::string());
}

} // namespace

void MercesPayouts::Configure(fr::PayoutMode p_Mode, fr::PayoutScaleConfig p_Scale, fr::PrestigeMemory p_Memory,
                              SaveMemory p_Save, SeedFor p_SeedFor) {
    m_Mode = p_Mode;
    m_Scale = fr::SanitizePayoutScaleConfig(p_Scale);
    m_Memory = std::move(p_Memory);
    m_Save = std::move(p_Save);
    m_SeedFor = std::move(p_SeedFor);

    if (Writes()) {
        LogTo(fr::LogTopic::Money, "merces: payout table {}{}", fr::PayoutModeName(m_Mode),
              m_Mode == fr::PayoutMode::Scale
                  ? fmt::format(" (payout_sigma {}, payout_lean {}, payout_min {}, payout_max {}, "
                                "payout_jackpot_chance {}, payout_jackpot_bonus {}, payout_same_base {}; normal "
                                "amounts below {} scaled from {}; shown amounts rounded to {}, at least {})",
                                m_Scale.sigma, m_Scale.lean, m_Scale.min, m_Scale.max,
                                fr::PercentText(m_Scale.jackpotChance), m_Scale.jackpotBonus, m_Scale.sameBase,
                                fr::kPayoutNormalBaseFloor, fr::kPayoutNormalBaseFloor, fr::kPayoutStep,
                                fr::kPayoutMinShown)
                  : std::string());

        LogTo(fr::LogTopic::Money, "merces: prestige memory: bonus {}, {} prestige ids, {} normal ids",
              m_Memory.bonus ? fmt::format("{}", *m_Memory.bonus) : std::string("unknown"), m_Memory.prestige.size(),
              m_Memory.normal.size());
    }
}

// The int32 type from the engine's type registry (the STypeID a ZObjectRef
// holding an int32 points at). Looked up again at the next load until found.
void MercesPayouts::ResolveTypes() {
    if (m_TypesResolved)
        return;

    const STypeID* s_Int32 = nullptr;

    if (*Globals::TypeRegistry)
        s_Int32 = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("int32")));

    if (!s_Int32) {
        LogTo(fr::LogTopic::Money,
              "merces: int32 not in the type registry yet; pin payloads are not checked and nothing is written this "
              "load");
        return;
    }

    m_TypesResolved = true;
    m_Int32Type.store(s_Int32, std::memory_order_release);
}

void MercesPayouts::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    if (Writes())
        Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context); });
}

void MercesPayouts::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) {
    if (!p_Context)
        return;

    const bool s_Freelancer = fr::IsFreelancerScene(p_Context->m_SceneInitParameters.m_Type.ToStringView());

    if (p_Stage == ESceneLoadingStage::eLoading_AssetsLoaded) {
        // A restart may load again without a ClearScene in between.
        ForgetScene();
        ResolveTypes();
        m_Armed = true;
        m_PinsOn.store(false, std::memory_order_relaxed);

        if (s_Freelancer) {
            LogTo(fr::LogTopic::Money, "merces: {} (payout table at scene start{})",
                  SceneLabelOf(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView()),
                  m_Mode == fr::PayoutMode::Scale ? ", randomly scaled" : "");
        }
    }
    else if (p_Stage == ESceneLoadingStage::eLoading_SceneStarted) {
        if (!s_Freelancer || m_SceneHandled)
            return;

        m_SceneHandled = true;

        // eLoading_AssetsLoaded not seen for this load: arm here.
        if (!m_Armed) {
            ResolveTypes();
            m_Armed = true;
        }

        FindTable(p_Context->m_aLoadedBricks,
                  ReadBricks(&p_Context->m_SceneInitParameters.m_aAdditionalBrickResources));

        if (Writes())
            WriteTable(SceneOf(p_Context)); // a full variant: its contract's scene, seed and overlay

        // The prestige wall's display entries (safehouse): named after the
        // table write (which classifies the entries); their copies are only
        // set during eLoading_SceneActivated, right before each poll emits
        // its copy, so the pin hook rewrites the emitted amount (DisplayPin).
        FindDisplayEntries();
    }
    else if (p_Stage == ESceneLoadingStage::eLoading_ScenePlaying && s_Freelancer && !m_Children.empty()) {
        Observe("scene playing");
        CheckDisplayEntries("scene playing");

        m_LateArmed = true;
        m_LatePlay = 0.0;

        if (m_UsedSeed) {
            CheckLiveSeed("scene playing");
            m_SeedCheckPlay = 0.0;
            m_SeedChecksDone = 0;
        }
    }
}

std::map<int32_t, int32_t> MercesPayouts::ClientReqPayouts() const {
    std::map<int32_t, int32_t> s_Payouts;

    // Client requirements are normal entries: what is stored is what is shown
    // (no prestige bonus). The first instance's entry wins, as in the overlay.
    for (const Child& s_Child : m_Children) {
        if (!s_Child.hasValue || !s_Child.readable || s_Child.className != "ZValueInt" || !s_Child.objective ||
            !fr::IsClientReqId(*s_Child.objective))
            continue;

        s_Payouts.emplace(*s_Child.objective, s_Child.written.value_or(s_Child.start));
    }

    return s_Payouts;
}

void MercesPayouts::OnSceneClear() noexcept {
    if (Writes())
        Contained(kTag, "scene clear", [&] { SceneClear(); });
}

void MercesPayouts::SceneClear() {
    m_PinsOn.store(false, std::memory_order_relaxed);
    ForgetScene();
    m_Armed = false;
}

// Forgets the scene's entities and per-scene counters (pins may still be
// arriving on other threads; their counters are atomics).
void MercesPayouts::ForgetScene() {
    PublishOverlay({});
    m_Instances.clear();
    m_Children.clear();
    m_DisplayPolls.clear();
    m_DisplayEntriesFound = false;
    m_DisplayArmed.store(false, std::memory_order_release);
    {
        std::lock_guard s_DisplayLock(m_DisplayMutex);
        m_DisplayEntries.clear();
        m_DisplayIndex.clear();
        m_DisplayAmounts.clear();
        m_DisplayBases.clear();
        m_DisplayCounts.clear();
        m_DisplayPinLines = 0;
    }
    m_SceneHandled = false;
    m_LateArmed = false;
    m_UsedSeed.reset();
    m_LiveSeedDiffers = false;
    m_SeedCheckPlay = -1.0;
    m_SeedChecksDone = 0;
}

void MercesPayouts::Tick(double p_GameSeconds) noexcept {
    if (Writes())
        Contained(kTag, "frame update", [&] { FrameTick(p_GameSeconds); });
}

void MercesPayouts::FrameTick(double p_GameSeconds) {
    // The live seed again after 5 s and 60 s of play (when the dials' late-seed check reads it).
    if (m_SeedCheckPlay >= 0.0) {
        constexpr std::array<double, 2> s_At {5.0, 60.0};
        m_SeedCheckPlay += p_GameSeconds;

        if (m_SeedCheckPlay >= s_At[m_SeedChecksDone]) {
            CheckLiveSeed(m_SeedChecksDone == 0 ? "5 s of play" : "60 s of play");

            if (++m_SeedChecksDone == static_cast<int>(s_At.size()))
                m_SeedCheckPlay = -1.0;
        }
    }

    if (!m_LateArmed)
        return;

    m_LatePlay += p_GameSeconds;

    if (m_LatePlay < kLateReadSeconds)
        return;

    m_LateArmed = false;
    Observe("5 s of play");
    CheckDisplayEntries("5 s of play");
}

// The additional bricks named like evergreen, freelancer or mission bricks,
// by runtime resource id (logged with their paths).
std::set<uint64_t> MercesPayouts::NamedBricks(const BrickList& p_Additional) {
    std::set<uint64_t> s_Named;
    std::string s_Text;

    for (const std::string& s_Path : p_Additional.paths) {
        const uint64_t s_Rid = ZRuntimeResourceID::FromString(fr::BrickResourcePath(s_Path)).GetID();
        const bool s_Likely = fr::IsLikelyTableBrickPath(s_Path) && !s_Path.ends_with("...");

        if (s_Likely)
            s_Named.insert(s_Rid);

        s_Text += fmt::format("{}{} {:016X}{}", s_Text.empty() ? "" : ", ", s_Path, s_Rid, s_Likely ? " (named)" : "");
    }

    LogTo(fr::LogTopic::Money, "merces table: additional bricks: {}{}{}", s_Text.empty() ? std::string("none") : s_Text,
          p_Additional.paths.size() < p_Additional.count
              ? fmt::format(" ({} of {} read)", p_Additional.paths.size(), p_Additional.count)
              : std::string(),
          p_Additional.fault ? fmt::format(" (read stopped: {})", FaultText(p_Additional.fault)) : std::string());

    return s_Named;
}

// One walk of the loaded bricks, most likely first (fr::OrderTableBricks): an
// entity with the table's sub-entity id (or a parent id remembered from an
// earlier scene) is an instance; an entity reached from an instance is one of
// its children (breadth first, so the instance is always seen before its
// children). An entity whose sub-entity id observation learned is an entry
// wherever it sits; its parent becomes an instance. Stops after the brick
// that completes the search (fr::TableSearchDone) or at kWideWalkEntityCap
// entities.
void MercesPayouts::FindTable(const TArray<SBrickAllocationInfo>& p_Bricks, const BrickList& p_Additional) {
    std::unordered_map<const void*, size_t> s_InstanceIndex;
    size_t s_Faults = 0;
    size_t s_ById = 0; // instances with fr::kPayoutTableId
    fr::LearnedEntryIds s_Learned(m_Memory);
    const auto s_Start = std::chrono::steady_clock::now();

    std::vector<uint64_t> s_Loaded;
    s_Loaded.reserve(p_Bricks.size());

    for (size_t i = 0; i < p_Bricks.size(); ++i)
        s_Loaded.push_back(p_Bricks[i].m_RuntimeResourceID.GetID());

    const fr::TableBrickOrder s_Order = fr::OrderTableBricks(s_Loaded, m_TableBricks, NamedBricks(p_Additional));
    std::map<fr::TableBrickTier, size_t> s_TierCounts;

    for (const fr::TableBrickTier s_Tier : s_Order.tiers)
        ++s_TierCounts[s_Tier];

    LogTo(fr::LogTopic::Money,
          "merces table: searching {} loaded bricks: {} that held the table earlier this session, {} named like an "
          "evergreen or mission brick, {} others; matching table id {:016x}, {} remembered parent ids and {} learned "
          "entry ids; stop rule: after the brick that completes it, once an instance with the table id is found or {}% "
          "of the learned entry ids are, and no brick still to walk held one earlier this session (cap {} entities)",
          s_Loaded.size(), s_TierCounts[fr::TableBrickTier::Held], s_TierCounts[fr::TableBrickTier::Named],
          s_TierCounts[fr::TableBrickTier::Other], fr::kPayoutTableId, m_TableParents.size(), s_Learned.Size(),
          fr::kLearnedFoundPercent, kWideWalkEntityCap);

    // The order's rids, so the stop rule sees the bricks still to walk.
    std::vector<uint64_t> s_OrderRids;
    s_OrderRids.reserve(s_Order.order.size());

    for (const size_t s_Index : s_Order.order)
        s_OrderRids.push_back(s_Loaded[s_Index]);

    bool s_Stopped = false;
    const auto s_Done = [&](size_t p_Walked) {
        const std::span<const uint64_t> s_Unwalked = std::span<const uint64_t>(s_OrderRids).subspan(p_Walked);
        s_Stopped =
            !s_Unwalked.empty() && fr::TableSearchDone(fr::TableFound(s_ById, s_Learned), s_Unwalked, m_TableBricks);
        return s_Stopped;
    };

    const auto s_AddInstance = [&](ZEntityType** p_Entity, ZEntityType** p_Holder, uint64_t p_Brick, FoundBy p_By) {
        s_InstanceIndex.emplace(p_Entity, m_Instances.size());
        m_Instances.push_back({p_Entity, p_Holder, p_Brick, p_By});
        return m_Instances.size() - 1;
    };

    const WalkStats s_Stats = VisitEntitiesInOrder(
        p_Bricks, s_Order.order,
        [&](const VisitedEntity& p_Visited) {
            uint64_t s_Id = 0;

            if (SafeTypeEntityId(p_Visited.type, &s_Id)) {
                ++s_Faults;
                return false;
            }

            // The prestige wall (safehouse): never table entries.
            if (s_Id == fr::kPrestigeDisplayPollId) {
                m_DisplayPolls.push_back(p_Visited.entity);
                return false;
            }

            if (s_Id == fr::kPrestigeInfoEntityId ||
                std::find(fr::kPrestigeInfoProviderIds.begin(), fr::kPrestigeInfoProviderIds.end(), s_Id) !=
                    fr::kPrestigeInfoProviderIds.end())
                return false;

            if (s_Id == fr::kPayoutTableId) {
                s_AddInstance(p_Visited.entity, p_Visited.parent, p_Visited.brickRid, FoundBy::TableId);
                ++s_ById;
                return true;
            }

            if (m_TableParents.contains(s_Id)) {
                s_AddInstance(p_Visited.entity, p_Visited.parent, p_Visited.brickRid, FoundBy::RememberedParent);
                return true;
            }

            const bool s_IsLearned = s_Learned.Contains(s_Id);
            const auto s_Instance = s_InstanceIndex.find(p_Visited.parent);

            if (s_Instance != s_InstanceIndex.end()) {
                if (s_IsLearned || m_Instances[s_Instance->second].foundBy == FoundBy::TableId) {
                    m_Children.push_back({p_Visited.entity, p_Visited.type, s_Id, s_Instance->second});
                    s_Learned.Match(s_Id);
                }

                return false;
            }

            if (s_IsLearned && p_Visited.parent) {
                const size_t s_New =
                    s_AddInstance(p_Visited.parent, nullptr, p_Visited.brickRid, FoundBy::LearnedChildren);
                m_Children.push_back({p_Visited.entity, p_Visited.type, s_Id, s_New});
                s_Learned.Match(s_Id);
                return true;
            }

            return false;
        },
        kWideWalkEntityCap, s_Done, std::min(m_LastVisited, fr::kVisitedReserveMax));
    m_LastVisited = std::max(m_LastVisited, s_Stats.visited);
    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();
    m_SearchMs = s_Ms;
    DropUntrustedInstances(s_Learned.Size());

    // Each child's class and m_nValue before anything is written.
    size_t s_WithValue = 0;
    std::map<std::string, size_t> s_Others; // class: children without a raw int32 m_nValue

    for (Child& s_Child : m_Children) {
        uint64_t s_Id = 0;
        ReadEntityClassName(s_Child.entity, s_Child.className, &s_Id);
        const ValueRead s_Value = ReadValue(s_Child.entity);

        if (s_Value.status == ValueRead::Status::Ok) {
            s_Child.hasValue = true;
            s_Child.start = s_Value.value;
            s_Child.objective = ReadInt32ById(s_Child.entity, fr::kObjectiveNumberPropertyId);
            ++s_WithValue;
            continue;
        }

        if (s_Value.status == ValueRead::Status::Fault) {
            s_Child.readable = false;
            LogTo(fr::LogTopic::Money, "merces table: {:016x} ({}): reading m_nValue: {}", s_Child.id,
                  ClassText(s_Child.className), FaultText(s_Value.faultCode));
        }

        ++s_Others[ClassText(s_Child.className)];
    }

    std::vector<uint64_t> s_Bricks;

    for (const Instance& s_Instance : m_Instances) {
        if (std::find(s_Bricks.begin(), s_Bricks.end(), s_Instance.brickRid) == s_Bricks.end())
            s_Bricks.push_back(s_Instance.brickRid);
    }

    // Where the table sat, by walk position and tier: the next scenes walk these bricks first.
    for (const uint64_t s_Rid : s_Bricks) {
        size_t s_Position = 0;

        while (s_Position < s_OrderRids.size() && s_OrderRids[s_Position] != s_Rid)
            ++s_Position;

        const size_t s_Count =
            static_cast<size_t>(std::count_if(m_Instances.begin(), m_Instances.end(), [&](const Instance& p_Instance) {
                return p_Instance.brickRid == s_Rid;
            }));
        const bool s_New = m_TableBricks.insert(s_Rid).second;

        LogTo(fr::LogTopic::Money, "merces table: brick {:016X} held {} instance{} (walked #{} of {}, {}){}", s_Rid,
              s_Count, s_Count == 1 ? "" : "s", s_Position + 1, s_OrderRids.size(),
              s_Position < s_Order.tiers.size() ? fr::TableBrickTierName(s_Order.tiers[s_Position]) : "?",
              s_New ? "; remembered for later scenes" : "");
    }

    // Learned entries' parents: their ids are matched as instances in later scenes.
    for (const Instance& s_Instance : m_Instances) {
        if (s_Instance.foundBy != FoundBy::LearnedChildren)
            continue;

        std::string s_Class;
        uint64_t s_ParentId = 0;
        const uint32_t s_Code = ReadEntityClassName(s_Instance.entity, s_Class, &s_ParentId);
        const bool s_New = !s_Code && s_ParentId && m_TableParents.insert(s_ParentId).second;

        LogTo(fr::LogTopic::Money, "merces table: learned entries found under parent {} in brick {:016X}{}",
              s_Code ? FaultText(s_Code) : fmt::format("{:016x} ({})", s_ParentId, ClassText(s_Class)),
              s_Instance.brickRid, s_New ? "; parent id remembered for later scenes" : "");
    }

    LogTo(fr::LogTopic::Money,
          "merces table: {} instances ({} by id), {} children ({} with an int32 m_nValue) in {} bricks; {} of {} "
          "learned entry ids found; walked {} of {} bricks, {} entities, {} faults{}, {:.1f} ms",
          m_Instances.size(), s_ById, m_Children.size(), s_WithValue, s_Bricks.size(), s_Learned.Found(),
          s_Learned.Size(), s_Stats.bricks.size(), s_OrderRids.size(), s_Stats.visited, s_Stats.faults + s_Faults,
          s_Stats.capped ? fmt::format(", stopped at the {}-entity cap", kWideWalkEntityCap)
          : s_Stopped    ? std::string(", stopped by the stop rule")
                         : std::string(),
          s_Ms);

    for (const auto& [s_Class, s_Count] : s_Others)
        LogTo(fr::LogTopic::Money, "merces table: {} children of class {} without an int32 m_nValue (not written)",
              s_Count, s_Class);

    if (m_Instances.empty()) {
        LogTo(fr::LogTopic::Money,
              "merces table: payout table {:016x} not found in this scene, nor a parent of enough of the {} learned "
              "entry ids ({} found); nothing written",
              fr::kPayoutTableId, s_Learned.Size(), s_Learned.Found());
    }

    LogInstances();
}

// Instances found through learned ids (fr::LearnedInstanceTrusted) with too
// few learned children are dropped with their children before anything is
// read, written or remembered.
void MercesPayouts::DropUntrustedInstances(size_t p_LearnedSize) {
    std::vector<size_t> s_Counts(m_Instances.size(), 0);

    for (const Child& s_Child : m_Children)
        ++s_Counts[s_Child.instance];

    std::vector<size_t> s_NewIndex(m_Instances.size(), SIZE_MAX);
    std::vector<Instance> s_Kept;

    for (size_t i = 0; i < m_Instances.size(); ++i) {
        const Instance& s_Instance = m_Instances[i];

        if (s_Instance.foundBy != FoundBy::TableId && !fr::LearnedInstanceTrusted(s_Counts[i], p_LearnedSize)) {
            LogTo(fr::LogTopic::Money,
                  "merces table: {} in brick {:016X} (found by {}) has {} learned entries, fewer than max({}, {}% of "
                  "{}); dropped, not written",
                  EntityText(s_Instance.entity), s_Instance.brickRid,
                  s_Instance.foundBy == FoundBy::RememberedParent ? "remembered parent id" : "learned children",
                  s_Counts[i], fr::kMinBonusEntries, fr::kLearnedFoundPercent, p_LearnedSize);
            continue;
        }

        s_NewIndex[i] = s_Kept.size();
        s_Kept.push_back(s_Instance);
    }

    if (s_Kept.size() == m_Instances.size())
        return;

    std::vector<Child> s_Children;

    for (Child& s_Child : m_Children) {
        if (s_NewIndex[s_Child.instance] == SIZE_MAX)
            continue;

        s_Child.instance = s_NewIndex[s_Child.instance];
        s_Children.push_back(std::move(s_Child));
    }

    m_Instances = std::move(s_Kept);
    m_Children = std::move(s_Children);
}

// Per instance: where it is, and its logical-parent chain.
void MercesPayouts::LogInstances() {
    for (size_t i = 0; i < m_Instances.size(); ++i) {
        const Instance& s_Instance = m_Instances[i];
        std::string s_Chain;
        ZEntityType** s_At = s_Instance.entity;

        for (size_t s_Level = 0; s_Level < kMaxParents; ++s_Level) {
            ZEntityType** s_Parent = nullptr;

            if (const uint32_t s_Code = ReadLogicalParent(s_At, &s_Parent)) {
                s_Chain += " <- " + FaultText(s_Code);
                break;
            }

            if (!s_Parent || s_Parent == s_At)
                break;

            s_Chain += " <- " + EntityText(s_Parent);
            s_At = s_Parent;
        }

        size_t s_Children = 0;

        for (const Child& s_Child : m_Children)
            s_Children += s_Child.instance == i ? 1 : 0;

        const char* s_By = s_Instance.foundBy == FoundBy::TableId            ? "table id"
                           : s_Instance.foundBy == FoundBy::RememberedParent ? "remembered parent id"
                                                                             : "learned children";

        LogTo(fr::LogTopic::Money,
              "merces table #{}: entity {} (found by {}) with {} children, {} in brick {:016X}; logical parents:{}", i,
              EntityText(s_Instance.entity), s_By, s_Children,
              s_Instance.holder ? "reached from " + EntityText(s_Instance.holder)
                                : std::string("parent of its children"),
              s_Instance.brickRid, s_Chain.empty() ? std::string(" none") : s_Chain);
    }
}

const char* MercesPayouts::Tag() const {
    return "merces scale";
}

// The value to store for an entry: scaled and rounded to 50 (payouts; from its
// kind's shared base with payout_same_base).
std::optional<int32_t> MercesPayouts::Target(int32_t p_Base, const fr::TruncatedDraw& p_Roll, fr::PayoutKind p_Kind,
                                             int32_t p_Bonus, int32_t p_Jackpot) const {
    return fr::RolledPayout(fr::ScaleBaseFor(p_Base, p_Kind, m_Shared, m_Scale.sameBase), p_Roll, m_Scale, p_Kind,
                            p_Bonus, p_Jackpot);
}

// The scale's seed for this load: the one remembered for the scene (a
// restart, or a reload of the safehouse, keeps it), else the caller's
// resolution (remembered when known). Empty when no seed is known (logged).
std::optional<fr::SeedResolution> MercesPayouts::ScaleSeed(const std::string& p_Scene) {
    m_ScaleSeeds.ForgetOtherScenes(p_Scene);

    if (const auto s_Kept = m_ScaleSeeds.Find(p_Scene)) {
        LogTo(fr::LogTopic::Money, "merces scale: reusing seed {} ({}) from this scene's first scale this session",
              s_Kept->seed, fr::SeedSourceName(s_Kept->source));
        m_UsedSeed = s_Kept;
        return s_Kept;
    }

    const fr::SeedResolution s_Resolution = m_SeedFor ? m_SeedFor() : fr::SeedResolution {fr::SeedSource::Fallback, 0};

    if (s_Resolution.source == fr::SeedSource::Fallback) {
        LogTo(fr::LogTopic::Money,
              "merces scale: campaign seed not known for {} (no settled mission seed, no generated or persisted "
              "seed); payout table left unchanged",
              p_Scene);
        return std::nullopt;
    }

    LogTo(fr::LogTopic::Money, "merces scale: campaign seed {} (source {}) for {}; kept for this scene",
          s_Resolution.seed, fr::SeedSourceName(s_Resolution.source), p_Scene);
    m_ScaleSeeds.Remember(p_Scene, s_Resolution);
    m_UsedSeed = s_Resolution;
    return s_Resolution;
}

// Reads the live seed entity; when it is known and differs from the seed this
// load's table was scaled with, says so once per load. The table is not
// written again mid-scene.
void MercesPayouts::CheckLiveSeed(const char* p_Moment) {
    if (!m_UsedSeed || m_LiveSeedDiffers)
        return;

    const ZEntitySceneContext* s_Context = CurrentSceneContext();

    if (!s_Context)
        return;

    const std::optional<int32_t> s_Live = ReadLiveSeed(s_Context->m_aLoadedBricks);

    if (!fr::IsKnownSeed(s_Live) || *s_Live == m_UsedSeed->seed)
        return;

    m_LiveSeedDiffers = true;
    LogTo(
        fr::LogTopic::Money,
        "merces scale: LIVE SEED DIFFERS at {}: the live campaign seed is {}, but this scene's payouts were scaled "
        "with seed {} ({}); this scene keeps its amounts. If the live seed is persisted (the dials' late-seed check), "
        "later scenes scale with {} and differ from what the safehouse board showed",
        p_Moment, *s_Live, m_UsedSeed->seed, fr::SeedSourceName(m_UsedSeed->source), *s_Live);
}

void MercesPayouts::OnCampaignGenerated(int32_t p_Seed) noexcept {
    if (m_Mode != fr::PayoutMode::Scale)
        return;

    Contained(kTag, "campaign generated", [&] {
        LogTo(fr::LogTopic::Money,
              "merces scale: campaign generated with seed {}; forgetting {} remembered scene seeds, so the next scene "
              "start scales with it{}",
              p_Seed, m_ScaleSeeds.Size(),
              m_UsedSeed ? fmt::format(" (the current scene keeps seed {})", m_UsedSeed->seed) : std::string());
        m_ScaleSeeds.Clear();
    });
}

void MercesPayouts::PublishOverlay(OverlaySnapshot p_Snapshot) {
    std::lock_guard s_Lock(m_OverlayMutex);
    m_Overlay = std::move(p_Snapshot);
}

void MercesPayouts::PublishVanilla(const std::string& p_Scene, std::string_view p_Why) {
    LogTo(fr::LogTopic::Money, "merces scale: overlay for {}: {}; vanilla amounts", p_Scene, p_Why);
    PublishOverlay({p_Scene, fmt::format("Payouts: the game's own amounts ({}).", p_Why), {}});
}

// payouts: every ZValueInt child's m_nValue that holds a known payout amount,
// so that the amount shown is scaled by the entry's factor; logged per child. The plan (fr::PlanPayoutWriteTo)
// always starts from the vanilla base: a value this mod wrote at an earlier
// load (a kept table) is never scaled again, and a rebuilt table's vanilla
// value is recognized by the base the last write was computed from.
void MercesPayouts::WriteTable(const std::string& p_Scene) {
    const char* s_Tag = Tag();
    const bool s_Scale = m_Mode == fr::PayoutMode::Scale;

    if (m_Children.empty()) {
        if (s_Scale)
            PublishVanilla(p_Scene, "no payouts found here");

        return;
    }

    if (!m_Int32Type.load(std::memory_order_relaxed) || !IsTypeRegistered("int32")) {
        LogTo(fr::LogTopic::Money, "{}: int32 not in the type registry; nothing written", s_Tag);

        if (s_Scale)
            PublishVanilla(p_Scene, "the game was not ready");

        return;
    }

    // The scale's factors come from the campaign seed; without a known seed
    // nothing is guessed and the table keeps its vanilla amounts.
    int32_t s_Seed = 0;
    std::string s_SeedText;

    if (s_Scale) {
        const std::optional<fr::SeedResolution> s_Resolution = ScaleSeed(p_Scene);

        if (!s_Resolution) {
            PublishVanilla(p_Scene, "the campaign seed was not found");
            return;
        }

        s_Seed = s_Resolution->seed;
        s_SeedText = fmt::format("seed {}, {}", s_Seed, fr::SeedSourceName(s_Resolution->source));
    }

    const int32_t s_Bonus = m_Memory.bonus.value_or(0);
    // Writes of earlier loads only: two instances in one scene each get their own write.
    const std::unordered_map<uint64_t, LastWrite> s_Earlier = m_LastWritten;
    std::vector<OverlayEntry> s_Overlay;
    std::unordered_set<uint64_t> s_OverlayIds;
    size_t s_NormalByValue = 0;
    size_t s_NormalObserved = 0;
    size_t s_Prestige = 0;
    size_t s_Already = 0;
    size_t s_Unchanged = 0;
    size_t s_NotAmount = 0;
    size_t s_Unknown = 0;
    size_t s_NoTarget = 0;
    size_t s_Failed = 0;
    size_t s_Other = 0;
    size_t s_Jackpots = 0;     // overlay entries (distinct ids) with a jackpot
    size_t s_ShownChanged = 0; // overlay entries whose shown amount differs from vanilla
    size_t s_ShownEarlier = 0; // of those, kept from an earlier load's write (already scaled)

    // payout_same_base: one base per kind, the mean of this table's vanilla
    // bases (each entry's vanilla base, also when the table still holds an
    // earlier load's write), so the game's own amounts no longer decide which
    // objective pays most.
    m_Shared = {};

    if (s_Scale) {
        std::vector<fr::PayoutBaseSample> s_Samples;

        for (const Child& s_Child : m_Children) {
            if (!s_Child.hasValue || !s_Child.readable || s_Child.className != "ZValueInt")
                continue;

            const fr::PayoutKind s_Kind = fr::ClassifyPayout(m_Memory, s_Child.id, s_Child.start).kind;
            const auto s_Last = s_Earlier.find(s_Child.id);
            const bool s_HasLast = s_Last != s_Earlier.end();
            s_Samples.push_back(
                {s_Child.id,
                 fr::VanillaBaseOf(s_Child.start, s_Kind, s_Bonus,
                                   s_HasLast ? std::optional<int32_t>(s_Last->second.written) : std::nullopt,
                                   s_HasLast ? std::optional<int32_t>(s_Last->second.base) : std::nullopt),
                 s_Kind});
        }

        m_Shared = fr::ComputeSharedPayoutBases(s_Samples);
        const auto s_BaseText = [](std::optional<int32_t> p_Base, size_t p_Count) {
            return p_Base ? fmt::format("{} (mean of {} entries)", *p_Base, p_Count) : std::string("none (no entries)");
        };
        LogTo(fr::LogTopic::Money,
              "merces scale: payout_same_base {}: shared bases computed from this table: prestige {}{}, common {}{}",
              m_Scale.sameBase ? "on" : "off (each objective scales from its own amount)",
              s_BaseText(m_Shared.prestige, m_Shared.prestigeEntries),
              m_Shared.prestige ? fmt::format(" (shows {} with bonus {})", *m_Shared.prestige + s_Bonus, s_Bonus)
                                : std::string(),
              s_BaseText(m_Shared.normal, m_Shared.normalEntries),
              m_Shared.normal && *m_Shared.normal < fr::kPayoutNormalBaseFloor
                  ? fmt::format(" (scaled from {})", fr::kPayoutNormalBaseFloor)
                  : std::string());
    }

    // One overlay line per entry id (the first instance's).
    // p_Roll, p_Jackpot and p_Kind are what the shown value was computed with
    // (an earlier load's for a kept value); p_ScaleBase is the base its factor
    // was applied to.
    const auto s_Show = [&](uint64_t p_Id, std::optional<int32_t> p_Objective, int32_t p_Base, int32_t p_Value,
                            const fr::TruncatedDraw& p_Roll, int32_t p_Jackpot, fr::PayoutKind p_Kind, bool p_Earlier,
                            int32_t p_ScaleBase) {
        if (m_Mode != fr::PayoutMode::Scale || !s_OverlayIds.insert(p_Id).second)
            return;

        if (p_Value != p_Base) {
            ++s_ShownChanged;
            s_ShownEarlier += p_Earlier ? 1 : 0;
        }

        const int32_t s_Jackpot = fr::JackpotFor(p_Kind, p_Jackpot);
        s_Jackpots += s_Jackpot ? 1 : 0;
        // The shown amount the factor was applied to, when it is not the game's own.
        const int64_t s_From = fr::ScaledFrom(p_ScaleBase, p_Kind, s_Bonus);
        s_Overlay.push_back({p_Id, p_Objective, fr::PayoutObjectiveName(p_Objective),
                             p_Kind == fr::PayoutKind::Prestige, fr::ShownPayout(p_Base, p_Kind, s_Bonus),
                             fr::ShownPayout(p_Value, p_Kind, s_Bonus), p_Roll, s_Jackpot,
                             s_From != fr::ShownPayout(p_Base, p_Kind, s_Bonus) ? s_From : 0});
    };

    for (Child& s_Child : m_Children) {
        if (!s_Child.hasValue || !s_Child.readable || s_Child.className != "ZValueInt") {
            ++s_Other;
            continue;
        }

        const fr::PayoutClass s_Class = fr::ClassifyPayout(m_Memory, s_Child.id, s_Child.start);
        s_Child.kind = s_Class.kind;
        const fr::TruncatedDraw s_Roll =
            m_Mode == fr::PayoutMode::Scale ? fr::RollPayoutFactor(s_Seed, s_Child.id, m_Scale) : fr::TruncatedDraw {};
        s_Child.roll = s_Roll;
        // Separate hash stream from the factor's; RolledPayout applies it to normal entries only.
        const int32_t s_Jackpot = m_Mode == fr::PayoutMode::Scale ? fr::PayoutJackpot(s_Seed, s_Child.id, m_Scale) : 0;
        s_Child.jackpot = s_Jackpot;
        const int32_t s_ScaleBase = m_Mode == fr::PayoutMode::Scale
                                        ? fr::ScaleBaseFor(s_Child.start, s_Class.kind, m_Shared, m_Scale.sameBase)
                                        : s_Child.start;
        s_Child.scaleBase = s_ScaleBase;
        const auto s_Last = s_Earlier.find(s_Child.id);
        const bool s_HasLast = s_Last != s_Earlier.end();
        const fr::PayoutPlan s_Plan =
            fr::PlanPayoutWriteTo(s_Child.start, s_Class.kind, s_Bonus,
                                  s_HasLast ? std::optional<int32_t>(s_Last->second.written) : std::nullopt,
                                  s_HasLast ? std::optional<int32_t>(s_Last->second.base) : std::nullopt,
                                  [&](int32_t p_Base, fr::PayoutKind p_Kind, int32_t p_B) {
                                      return Target(p_Base, s_Roll, p_Kind, p_B, s_Jackpot);
                                  });
        const std::string s_Label = fmt::format("{:016x} {}{}", s_Child.id, fr::PayoutKindName(s_Class.kind),
                                                s_Class.kind == fr::PayoutKind::Unknown ? ""
                                                : s_Class.observed                      ? " (observed)"
                                                                                        : " (by value)");
        const int32_t s_Applied = fr::JackpotFor(s_Class.kind, s_Jackpot);
        const std::string s_FactorText =
            m_Mode != fr::PayoutMode::Scale
                ? std::string()
                : fmt::format("{}{}{}", RollText(s_Roll),
                              s_Class.kind != fr::PayoutKind::Unknown &&
                                      fr::ScaledFrom(s_ScaleBase, s_Class.kind, s_Bonus) !=
                                          fr::ShownPayout(s_Child.start, s_Class.kind, s_Bonus)
                                  ? fmt::format(" (scaled from {}{})",
                                                fr::ScaledFrom(s_ScaleBase, s_Class.kind, s_Bonus),
                                                s_ScaleBase != s_Child.start ? " shared" : "")
                                  : std::string(),
                              s_Applied ? fmt::format(" JACKPOT +{}", s_Applied) : std::string());

        switch (s_Plan.action) {
            case fr::PayoutAction::NotAnAmount:
                ++s_NotAmount;
                LogTo(fr::LogTopic::Money, "{}: {} {}: not a payout amount; not written", s_Tag, s_Label,
                      s_Child.start);
                continue;
            case fr::PayoutAction::Unknown:
                ++s_Unknown;
                LogTo(fr::LogTopic::Money, "{}: {} {}: skipped (unknown: not {} until observed)", s_Tag, s_Label,
                      s_Child.start, "scaled");
                continue;
            case fr::PayoutAction::NoTarget:
                ++s_NoTarget;
                LogTo(fr::LogTopic::Money, "{}: {} {}{}: no valid target (bonus {}); not written", s_Tag, s_Label,
                      s_Child.start, s_FactorText, s_Bonus);
                continue;
            case fr::PayoutAction::AlreadyWritten: {
                const LastWrite& s_Kept = s_Last->second;
                ++s_Already;
                s_Child.written = s_Kept.written;
                // The kept value's own draw, jackpot and kind, not this load's.
                s_Child.roll = s_Kept.roll;
                s_Child.jackpot = s_Kept.jackpot;
                s_Child.scaleBase = s_Kept.scaleBase;
                s_Show(s_Child.id, s_Child.objective, s_Kept.base, s_Kept.written, s_Kept.roll, s_Kept.jackpot,
                       s_Kept.kind, true, s_Kept.scaleBase);
                LogTo(fr::LogTopic::Money,
                      "{}: {} {}: already written (this mod wrote {} from base {} at an earlier load as {}{}{}; the "
                      "table was not rebuilt); not written again",
                      s_Tag, s_Label, s_Child.start, s_Kept.written, s_Kept.base, fr::PayoutKindName(s_Kept.kind),
                      m_Mode == fr::PayoutMode::Scale ? RollText(s_Kept.roll) : std::string(),
                      s_Kept.jackpot ? fmt::format(" JACKPOT +{}", s_Kept.jackpot) : std::string());
                continue;
            }
            case fr::PayoutAction::Unchanged:
                ++s_Unchanged;
                s_Show(s_Child.id, s_Child.objective, s_Child.start, s_Child.start, s_Roll, s_Jackpot, s_Class.kind,
                       false, s_ScaleBase);
                LogTo(fr::LogTopic::Money, "{}: {} {}{}: unchanged", s_Tag, s_Label, s_Child.start, s_FactorText);
                continue;
            case fr::PayoutAction::Write: break;
        }

        SetValueArgs s_Args {s_Child.entity, s_Plan.target, false};

        if (const DWORD s_Code = SafeSetValue(&s_Args)) {
            ++s_Failed;
            s_Child.readable = false;
            Log("{}: {} {} -> {}: {}", s_Tag, s_Label, s_Child.start, s_Plan.target, FaultText(s_Code));
            continue;
        }

        if (!s_Args.ok) {
            ++s_Failed;
            Log("{}: {} {} -> {}: write failed", s_Tag, s_Label, s_Child.start, s_Plan.target);
            continue;
        }

        s_Child.written = s_Plan.target;
        s_Child.wroteNow = true;
        m_LastWritten[s_Child.id] = {s_Child.start, s_Plan.target, s_Roll, s_Applied, s_Class.kind, s_ScaleBase};
        s_Show(s_Child.id, s_Child.objective, s_Child.start, s_Plan.target, s_Roll, s_Jackpot, s_Class.kind, false,
               s_ScaleBase);
        ++(s_Class.kind == fr::PayoutKind::Prestige ? s_Prestige
           : s_Class.observed                       ? s_NormalObserved
                                                    : s_NormalByValue);

        const ValueRead s_Back = ReadValue(s_Child.entity);
        const std::string s_BackText = s_Back.status == ValueRead::Status::Ok      ? fmt::format("{}", s_Back.value)
                                       : s_Back.status == ValueRead::Status::Fault ? FaultText(s_Back.faultCode)
                                                                                   : std::string("<not raw>");

        LogTo(fr::LogTopic::Money, "{}: {} {} -> {}{} written, read back {}{}", s_Tag, s_Label, s_Child.start,
              s_Plan.target, s_FactorText, s_BackText,
              s_Class.kind == fr::PayoutKind::Prestige
                  ? fmt::format(" (shows {} -> {} with bonus {})", s_Child.start + s_Bonus, s_Plan.target + s_Bonus,
                                s_Bonus)
                  : std::string());
    }

    const size_t s_Written = s_NormalByValue + s_NormalObserved + s_Prestige;

    LogTo(fr::LogTopic::Money,
          "{} scene start: {} written ({} normal by value, {} normal observed, {} prestige observed with "
          "bonus {}), {} already written, {} unchanged, {} skipped as unknown, {} no valid target, {} not a payout "
          "amount, {} failed, {} children not ZValueInt with an int32 m_nValue{}",
          s_Tag, s_Written, s_NormalByValue, s_NormalObserved, s_Prestige,
          m_Memory.bonus ? fmt::format("{}", *m_Memory.bonus) : std::string("unknown"), s_Already, s_Unchanged,
          s_Unknown, s_NoTarget, s_NotAmount, s_Failed, s_Other,
          s_Scale ? fmt::format("; {} jackpots (+{}) among the shown entries", s_Jackpots, m_Scale.jackpotBonus)
                  : std::string());

    if (s_Scale) {
        if (s_Overlay.empty()) {
            PublishVanilla(p_Scene, s_Unknown ? fmt::format("no payout could be changed; {} prestige payouts are "
                                                            "changed from a later mission on",
                                                            s_Unknown)
                                              : std::string("no payout could be changed"));
            return;
        }

        // Counted before the rows are moved out.
        const size_t s_Rows = s_Overlay.size();
        const std::string s_Of = s_Rows > s_ShownChanged ? fmt::format("{} of {}", s_ShownChanged, s_Rows)
                                                         : fmt::format("{}", s_ShownChanged);
        const std::string s_Count =
            s_ShownChanged == 0 ? fmt::format("none of {} payouts changed", s_Rows)
            : s_ShownEarlier == s_ShownChanged
                ? fmt::format("{} payouts changed (kept from earlier in the campaign)", s_Of)
            : s_ShownEarlier
                ? fmt::format("{} payouts changed ({} kept from earlier in the campaign)", s_Of, s_ShownEarlier)
                : fmt::format("{} payouts changed", s_Of);
        std::string s_Status = fmt::format(
            "Random payouts: {}{}{}", s_Count, s_Jackpots ? fmt::format(", {} jackpots", s_Jackpots) : std::string(),
            s_Unknown ? fmt::format(", {} prestige payouts not changed until a later mission", s_Unknown)
                      : std::string());
        LogTo(fr::LogTopic::Money, "merces scale: overlay for {} ({}): {}", ShortSceneName(p_Scene), s_SeedText,
              s_Status);
        // Client requirements A to Z, then prestige objectives, then the rest.
        std::stable_sort(s_Overlay.begin(), s_Overlay.end(), [](const OverlayEntry& a, const OverlayEntry& b) {
            return fr::PayoutRowBefore(a.objective, b.objective);
        });
        OverlaySnapshot s_Snapshot;
        s_Snapshot.scene = p_Scene;
        s_Snapshot.status = std::move(s_Status);
        s_Snapshot.entries = std::move(s_Overlay);
        PublishOverlay(std::move(s_Snapshot));
    }
}

// The overlay's Payouts section: collapsed by default, one line per entry
// (amount shown vanilla -> now, factor, jackpot). The header and the rows come
// from one snapshot, published together for one scene.
bool MercesPayouts::HasOverlay() {
    std::lock_guard s_Lock(m_OverlayMutex);
    return !m_Overlay.status.empty();
}

void MercesPayouts::DrawOverlay() {
    OverlaySnapshot s_Snapshot;
    {
        std::lock_guard s_Lock(m_OverlayMutex);

        if (m_Overlay.status.empty())
            return;

        s_Snapshot = m_Overlay;
    }

    if (s_Snapshot.entries.empty()) {
        WrappedTextAvail(s_Snapshot.status);
        return;
    }

    // The status above a short header: a header's label stays on one line and would be cut off.
    WrappedTextAvail(s_Snapshot.status);
    ImGui::SetNextItemOpen(m_HeaderOpen);
    const bool s_HeaderOpen = ImGui::CollapsingHeader("Payout of each objective###payouts");

    if (s_HeaderOpen != m_HeaderOpen && m_HeaderDebounce.Accept(ImGui::GetTime()))
        m_HeaderOpen = s_HeaderOpen;

    if (!m_HeaderOpen)
        return;

    // No ScrollY: the window scrolls (drag to scroll), and a table scrolling on its own would swallow the drag.
    constexpr ImGuiTableFlags s_Flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##Payouts", 4, s_Flags))
        return;

    // The objective's name takes the room the numbers leave and wraps in it, so the table
    // never runs past the window's edge.
    ImGui::TableSetupColumn("Objective", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Game -> now");
    ImGui::TableSetupColumn("Multiplier");
    ImGui::TableSetupColumn("Jackpot");
    ImGui::TableHeadersRow();

    for (const OverlayEntry& s_Entry : s_Snapshot.entries) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        WrappedTextAvail(s_Entry.name);
        ImGui::TableNextColumn();
        ImGui::Text("%lld -> %lld", static_cast<long long>(s_Entry.before), static_cast<long long>(s_Entry.after));
        ImGui::TableNextColumn();
        if (s_Entry.from)
            ImGui::Text("x%.2f of %lld", s_Entry.roll.value, static_cast<long long>(s_Entry.from));
        else
            ImGui::Text("x%.2f", s_Entry.roll.value);
        ImGui::TableNextColumn();

        if (s_Entry.jackpot)
            ImGui::Text("Jackpot +%d", s_Entry.jackpot);
    }

    ImGui::EndTable();
}

// Reads every child's m_nValue again and compares it with the value at scene
// start (or this mod's write), then learns from the ZValueInt children's
// changes (fr::LearnFromScene; saved when the memory changed). With payouts
// on, every entry written this load gets a verdict.
void MercesPayouts::Observe(const char* p_Moment) {
    std::vector<Reading> s_Readings;
    std::vector<fr::EntryChange> s_Changes; // ZValueInt children only: the entries that are written
    size_t s_Faults = 0;

    for (Child& s_Child : m_Children) {
        if (!s_Child.hasValue || !s_Child.readable)
            continue;

        const ValueRead s_Value = ReadValue(s_Child.entity);

        if (s_Value.status != ValueRead::Status::Ok) {
            ++s_Faults;
            s_Child.readable = false;
            LogTo(fr::LogTopic::Money, "merces observe {}: {:016x}: {}", p_Moment, s_Child.id,
                  s_Value.status == ValueRead::Status::Fault ? FaultText(s_Value.faultCode) : "m_nValue not raw now");
            continue;
        }

        const int64_t s_Delta = static_cast<int64_t>(s_Value.value) - s_Child.written.value_or(s_Child.start);
        const int32_t s_Clamped = static_cast<int32_t>(
            std::clamp<int64_t>(s_Delta, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        s_Readings.push_back({&s_Child, s_Value.value, s_Clamped});

        if (s_Child.className == "ZValueInt")
            s_Changes.push_back({s_Child.id, s_Clamped});
    }

    // Learning (the bonus, a bonus of 0, ids moving between the sets) only
    // from scenes with the template instance: entries found by learned ids
    // (missions) are judged on a copy that is never kept.
    const bool s_Learns = std::any_of(m_Instances.begin(), m_Instances.end(), [](const Instance& p_Instance) {
        return p_Instance.foundBy == FoundBy::TableId;
    });
    const std::optional<int32_t> s_Before = m_Memory.bonus;
    fr::PrestigeMemory s_Scratch = m_Memory;
    const fr::LearnResult s_Learn = fr::LearnFromScene(s_Learns ? m_Memory : s_Scratch, s_Changes);
    size_t s_Unchanged = 0;
    size_t s_OtherChange = 0;

    for (const Reading& s_Reading : s_Readings) {
        const Child& s_Child = *s_Reading.child;

        if (s_Reading.delta == 0) {
            ++s_Unchanged;
            continue;
        }

        if (s_Learn.bonus && s_Reading.delta == *s_Learn.bonus)
            continue;

        ++s_OtherChange;
        LogTo(fr::LogTopic::Money,
              "merces observe {}: {:016x} ({}): start {}{} now {}: changed by {}, not the common change", p_Moment,
              s_Child.id, ClassText(s_Child.className), s_Child.start,
              s_Child.written ? fmt::format(" written {}", *s_Child.written) : std::string(), s_Reading.now,
              s_Reading.delta);
    }

    const auto s_BonusText = [](const std::optional<int32_t>& p_Bonus) {
        return p_Bonus ? fmt::format("{}", *p_Bonus) : std::string("unknown");
    };

    std::string s_Verdict;

    if (s_Learn.disagree)
        s_Verdict = "changes disagree: nothing learned";
    else if (s_Learn.bonusZero)
        s_Verdict = fmt::format("no entry rose and every known prestige entry is unchanged: bonus {} -> 0",
                                s_BonusText(s_Before));
    else if (!s_Learn.bonus)
        s_Verdict = "no entry rose: nothing learned";
    else if (!s_Learn.trusted)
        s_Verdict = fmt::format("too few entries ({} < {}, {} of {} known prestige): nothing learned",
                                s_Learn.withBonus, fr::kMinBonusEntries, s_Learn.knownWithBonus, s_Learn.knownRead);
    else
        s_Verdict = fmt::format(
            "bonus {} learned (was {}){}", *s_Learn.bonus, s_BonusText(s_Before),
            s_Learn.keptPrestige
                ? fmt::format(", {} known prestige entries unchanged but kept (bonus differs)", s_Learn.keptPrestige)
                : std::string());

    LogTo(fr::LogTopic::Money,
          "merces observe {}: {} read, {} unchanged, {} changed by {} ({} of {} known prestige), {} other changes, {} "
          "faults; {}; memory: bonus {}, {} prestige ids, {} normal ids{}",
          p_Moment, s_Readings.size(), s_Unchanged, s_Learn.withBonus,
          s_Learn.bonus ? fmt::format("{}", *s_Learn.bonus) : std::string("(none)"), s_Learn.knownWithBonus,
          s_Learn.knownRead, s_OtherChange, s_Faults,
          s_Learns ? s_Verdict : s_Verdict + " (not kept: no table-id instance in this scene)",
          s_BonusText(m_Memory.bonus), m_Memory.prestige.size(), m_Memory.normal.size(),
          s_Learns && s_Learn.changed ? " (updated, saved)" : "");

    if (s_Learns && s_Learn.changed && m_Save)
        m_Save(m_Memory);

    if (m_Mode == fr::PayoutMode::Scale)
        CheckScaled(p_Moment, s_Readings);
}

// payouts' verdicts: the amount shown now is the vanilla amount (the
// scene-start base plus whatever the game added since) scaled by the entry's
// factor draw and rounded to 50, as fr::RolledPayout computes it with the change
// the game actually made as the bonus.
void MercesPayouts::CheckScaled(const char* p_Moment, std::span<const Reading> p_Readings) {
    size_t s_Ok = 0;
    size_t s_Mismatch = 0;

    for (const Reading& s_Reading : p_Readings) {
        const Child& s_Child = *s_Reading.child;

        // A value found from an earlier load has no known vanilla base.
        if (!s_Child.wroteNow)
            continue;

        const int32_t s_Added = s_Child.kind == fr::PayoutKind::Prestige ? s_Reading.delta : 0;
        const int64_t s_Vanilla = static_cast<int64_t>(s_Child.start) + s_Reading.delta;
        const std::optional<int32_t> s_Stored =
            s_Added >= 0
                ? fr::RolledPayout(s_Child.scaleBase, s_Child.roll, m_Scale, s_Child.kind, s_Added, s_Child.jackpot)
                : std::nullopt;
        const int32_t s_Jackpot = fr::JackpotFor(s_Child.kind, s_Child.jackpot);
        const std::string s_How = fmt::format(
            "{}{}{} rounded to {} (at least {}){}",
            fr::ScaledFrom(s_Child.scaleBase, s_Child.kind, s_Added) != s_Vanilla ? fmt::format("{} as ", s_Vanilla)
                                                                                  : std::string(),
            fr::ScaledFrom(s_Child.scaleBase, s_Child.kind, s_Added), RollText(s_Child.roll), fr::kPayoutStep,
            fr::kPayoutMinShown, s_Jackpot ? fmt::format(" + jackpot {}", s_Jackpot) : std::string());
        const std::optional<int64_t> s_Wanted =
            s_Stored ? std::optional<int64_t>(static_cast<int64_t>(*s_Stored) + s_Added) : std::nullopt;
        const bool s_Match = s_Wanted && s_Reading.now == *s_Wanted;
        ++(s_Match ? s_Ok : s_Mismatch);

        LogTo(fr::LogTopic::Money, "merces scale check {}: {:016x} {} base {} written {} now {}: {}", p_Moment,
              s_Child.id, fr::PayoutKindName(s_Child.kind), s_Child.start, *s_Child.written, s_Reading.now,
              s_Match    ? fmt::format("shows {} = {} ok", s_Reading.now, s_How)
              : s_Wanted ? fmt::format("MISMATCH (wanted {} = {})", s_How, *s_Wanted)
                         : fmt::format("MISMATCH (no valid amount for {})", s_How));
    }

    LogTo(fr::LogTopic::Money, "merces scale check {}: {} ok, {} mismatches", p_Moment, s_Ok, s_Mismatch);
}

std::optional<std::pair<int32_t, int32_t>> MercesPayouts::BaseAndWritten(const Child& p_Child) const {
    if (!p_Child.written)
        return std::nullopt;

    if (p_Child.wroteNow)
        return std::pair {p_Child.start, *p_Child.written};

    // A kept table: the earlier load's base.
    const auto s_Last = m_LastWritten.find(p_Child.id);

    if (s_Last == m_LastWritten.end())
        return std::nullopt;

    return std::pair {s_Last->second.base, *p_Child.written};
}

// The int32 values of an entity's raw int32 properties, plus the objective
// number an entity reference points at (a Help_Prestige_* helper, or an
// entity with the payout table's objective-number property, or a ZValueInt
// holding 101 to 136). Game thread.
void MercesPayouts::CollectObjectiveNumbers(ZEntityType** p_Entity, std::vector<int32_t>& p_Out) {
    static PropertyList s_List; // 6 KB: off the stack (game thread only)
    const ZEntityType* s_Type = nullptr;

    if (SafeEntityType(p_Entity, &s_Type) || !s_Type || SafePropertyList(s_Type, &s_List))
        return;

    for (size_t i = 0; i < s_List.count; ++i) {
        const PropertyEntry& s_Entry = s_List.entries[i];
        TypeNameOut s_TypeName {};

        if (s_Entry.getter || !s_Entry.typeId ||
            SafeTypeName(static_cast<const STypeID*>(s_Entry.typeId), &s_TypeName)) {
            continue;
        }

        const std::string_view s_Name(s_TypeName.name);
        const uintptr_t s_Address = reinterpret_cast<uintptr_t>(p_Entity) + s_Entry.offset;

        if (s_Name == "int32") {
            int32_t s_Value = 0;

            if (!ReadEngineBytes(s_Address, &s_Value, sizeof(s_Value)))
                p_Out.push_back(s_Value);

            continue;
        }

        if (s_Name != "ZEntityRef" && !s_Name.starts_with("TEntityRef"))
            continue;

        // ZEntityRef, and TEntityRef's first member, is the entity pointer.
        ZEntityType** s_Target = nullptr;

        if (ReadEngineBytes(s_Address, &s_Target, sizeof(s_Target)) || !s_Target)
            continue;

        uint64_t s_TargetId = 0;
        const ZEntityType* s_TargetType = nullptr;

        if (SafeEntityId(s_Target, &s_TargetId) || SafeEntityType(s_Target, &s_TargetType))
            continue;

        if (const std::optional<int32_t> s_Helper = fr::PrestigeHelperObjective(s_TargetId)) {
            p_Out.push_back(*s_Helper);
            continue;
        }

        if (const std::optional<int32_t> s_Number = ReadInt32ById(s_Target, fr::kObjectiveNumberPropertyId)) {
            p_Out.push_back(*s_Number);
            continue;
        }

        const ValueRead s_Value = ReadValue(s_Target);

        if (s_Value.status == ValueRead::Status::Ok)
            p_Out.push_back(s_Value.value);
    }
}

// The objective of a display entry (the poll's owning entity, p_Owner with
// sub-entity id p_OwnerId), in this order: the name its picker template gives
// it, an int32 constant on its pin connections there, then the objective
// numbers in its own template instance's properties (fr::AgreedObjective).
// Remembered per owner id for the session (the three pickers share the ids).
std::optional<int32_t> MercesPayouts::ObjectiveOfOwner(ZEntityType** p_Owner, uint64_t p_OwnerId, std::string* p_How,
                                                       std::string* p_Name) {
    if (const auto s_Known = m_OwnerObjectives.find(p_OwnerId); s_Known != m_OwnerObjectives.end()) {
        *p_How = s_Known->second.how;
        *p_Name = s_Known->second.name;
        return s_Known->second.objective;
    }

    std::string s_Why;
    const std::optional<OwnedSubEntity> s_Owned = ReadOwnedSubEntity(p_Owner, p_OwnerId, &s_Why);
    std::optional<int32_t> s_Objective;

    if (s_Owned) {
        *p_Name = s_Owned->name;

        if ((s_Objective = fr::ObjectiveFromName(s_Owned->name))) {
            *p_How = "its name in the picker template";
        }
        else if ((s_Objective = fr::AgreedObjective(s_Owned->connectionConstants))) {
            *p_How = "an int32 constant on its pin connections in the picker template";
        }
    }
    else {
        *p_Name = "<" + s_Why + ">";
    }

    if (!s_Objective) {
        std::vector<int32_t> s_Numbers;
        CollectObjectiveNumbers(p_Owner, s_Numbers);
        VisitEntityTree(
            p_Owner, 0,
            [&](const VisitedEntity& p_Visited) {
                CollectObjectiveNumbers(p_Visited.entity, s_Numbers);
                return false;
            },
            kMaxEntryTreeEntities);

        if ((s_Objective = fr::AgreedObjective(s_Numbers)))
            *p_How = "an objective number in its template instance's properties";
    }

    if (s_Objective)
        m_OwnerObjectives[p_OwnerId] = {*s_Objective, *p_How, *p_Name};

    return s_Objective;
}

void MercesPayouts::FindDisplayEntries() {
    if (m_DisplayEntriesFound)
        return;

    m_DisplayEntriesFound = true;
    const bool s_Safehouse = std::any_of(m_Instances.begin(), m_Instances.end(), [](const Instance& p_Instance) {
        return p_Instance.foundBy == FoundBy::TableId;
    });

    if (!s_Safehouse || m_DisplayPolls.empty())
        return;

    const int32_t s_Bonus = m_Memory.bonus.value_or(0);
    std::vector<DisplayEntry> s_Entries;
    std::map<int32_t, DisplayAmounts> s_Amounts;
    std::vector<fr::PrestigeBase> s_Bases;

    // The objectives' table entries: a prestige entry first (the one the bonus goes to).
    for (const bool s_PrestigeOnly : {true, false}) {
        for (const Child& s_Child : m_Children) {
            if (!s_Child.objective || !s_Child.hasValue || s_Amounts.contains(*s_Child.objective) ||
                (s_PrestigeOnly && s_Child.kind != fr::PayoutKind::Prestige)) {
                continue;
            }

            const std::optional<std::pair<int32_t, int32_t>> s_Written = BaseAndWritten(s_Child);
            const int32_t s_Base = s_Written ? s_Written->first : s_Child.start;

            if (*s_Child.objective >= fr::kPrestigeObjectiveFirst && *s_Child.objective <= fr::kPrestigeObjectiveLast)
                s_Bases.push_back({*s_Child.objective, s_Base});

            s_Amounts[*s_Child.objective] = {s_Child.id, s_Base,
                                             s_Written ? std::optional(s_Written->second) : std::nullopt};
        }
    }

    size_t s_NoRef = 0;
    size_t s_Named = 0;
    std::map<std::string, size_t> s_Hows;

    for (ZEntityType** s_Poll : m_DisplayPolls) {
        DisplayEntry s_Entry {};
        s_Entry.poll = s_Poll;
        const RefRead s_Ref = ReadEntityRefProperty(s_Poll, "m_rValueEntity");
        s_Entry.copy = s_Ref.entity;

        if (!s_Ref.entity && ++s_NoRef <= 3)
            LogTo(fr::LogTopic::Money, "merces prestige wall: display poll {} instance {}: m_rValueEntity: {}",
                  EntityText(s_Poll), static_cast<const void*>(s_Poll),
                  s_Ref.why.empty() ? std::string("null") : s_Ref.why);

        if (SafeOwning(s_Poll, &s_Entry.owner) || s_Entry.owner == s_Poll)
            s_Entry.owner = nullptr;

        if (s_Entry.owner && !SafeEntityId(s_Entry.owner, &s_Entry.ownerId)) {
            s_Entry.objective = ObjectiveOfOwner(s_Entry.owner, s_Entry.ownerId, &s_Entry.objectiveFrom, &s_Entry.name);
        }

        if (s_Entry.objective) {
            ++s_Named;
            ++s_Hows[s_Entry.objectiveFrom];
        }

        s_Entries.push_back(std::move(s_Entry));
    }

    std::string s_HowText;

    for (const auto& [s_How, s_Count] : s_Hows)
        s_HowText += fmt::format("{}{} by {}", s_HowText.empty() ? "" : ", ", s_Count, s_How);

    LogTo(
        fr::LogTopic::Money,
        "merces prestige wall: {} display polls ({:016x}) in the walked bricks, {} without a readable m_rValueEntity; "
        "{} with their objective named ({}); the rest are named at the poll by their amount when only one prestige "
        "objective has it; {} prestige table entries, bonus {}",
        s_Entries.size(), fr::kPrestigeDisplayPollId, s_NoRef, s_Named, s_HowText.empty() ? "none" : s_HowText,
        s_Bases.size(), s_Bonus);

    // One line per distinct entry (the three pickers share the entries' ids).
    std::set<uint64_t> s_Logged;

    for (const DisplayEntry& s_Entry : s_Entries) {
        if (!s_Logged.insert(s_Entry.ownerId).second)
            continue;

        const char* s_Name = s_Entry.objective ? fr::PrestigeObjectiveName(*s_Entry.objective) : nullptr;
        const auto s_Table = s_Entry.objective ? s_Amounts.find(*s_Entry.objective) : s_Amounts.end();

        LogTo(fr::LogTopic::Money, "merces prestige wall: entry {:016x} \"{}\": objective {}; table entry {}",
              s_Entry.ownerId, s_Entry.name,
              s_Entry.objective
                  ? fmt::format("{} {} ({})", *s_Entry.objective, s_Name ? s_Name : "?", s_Entry.objectiveFrom)
                  : std::string("not named yet"),
              s_Table != s_Amounts.end()
                  ? fmt::format("{:016x} base {}{}", s_Table->second.entryId, s_Table->second.base,
                                s_Table->second.written ? fmt::format(" written {}", *s_Table->second.written)
                                                        : std::string(" not written"))
                  : std::string("none"));
    }

    std::lock_guard s_Lock(m_DisplayMutex);
    m_DisplayEntries = std::move(s_Entries);
    m_DisplayAmounts = std::move(s_Amounts);
    m_DisplayBases = std::move(s_Bases);
    m_DisplayBonus = s_Bonus;
    m_DisplayIndex.clear();

    for (size_t i = 0; i < m_DisplayEntries.size(); ++i)
        m_DisplayIndex.emplace(m_DisplayEntries[i].poll, i);

    if (Writes()) {
        m_DisplayArmed.store(Writes(), std::memory_order_release);
        m_PinsOn.store(true, std::memory_order_relaxed);
    }
}

// What a display entry should show: its objective's table entry's base and
// this mod's write, or why not. Holds m_DisplayMutex.
std::optional<MercesPayouts::DisplayAmounts> MercesPayouts::DisplayAmountsOf(DisplayEntry& p_Entry, int32_t p_Shown,
                                                                             std::string* p_Why) {
    if (!p_Entry.objective) {
        p_Entry.objective = fr::ObjectiveByUniqueAmount(p_Shown, m_DisplayBonus, m_DisplayBases);

        if (!p_Entry.objective) {
            *p_Why = fmt::format("objective unknown, and {} is not the vanilla amount plus bonus {} of exactly one "
                                 "prestige objective",
                                 p_Shown, m_DisplayBonus);
            return std::nullopt;
        }

        p_Entry.objectiveFrom = "its amount (the only prestige objective with it)";
    }

    const auto s_Found = m_DisplayAmounts.find(*p_Entry.objective);

    if (s_Found == m_DisplayAmounts.end()) {
        *p_Why = fmt::format("objective {}: no table entry", *p_Entry.objective);
        return std::nullopt;
    }

    if (!s_Found->second.written) {
        *p_Why = fmt::format("objective {}: its table entry {:016x} was not written", *p_Entry.objective,
                             s_Found->second.entryId);
        return std::nullopt;
    }

    return s_Found->second;
}

// A display poll emits its copy's amount (the objective's vanilla amount,
// bonus included): the signal is rewritten to what the objective's table
// entry shows (fr::PlanDisplayWrite: written + bonus), and the copy too, so
// whatever reads either sees the scaled amount. Returns the value sent. Any
// thread (the game thread in practice).
std::optional<int32_t> MercesPayouts::DisplayPin(ZEntityType** p_Poll, const ZObjectRef& p_Data, int32_t p_Value) {
    std::lock_guard s_Lock(m_DisplayMutex);
    const auto s_Index = m_DisplayIndex.find(p_Poll);
    const uint64_t s_Line = ++m_DisplayPinLines;
    const bool s_Log = s_Line <= kMaxDisplayPinLines;

    if (s_Index == m_DisplayIndex.end()) {
        if (s_Log)
            LogTo(fr::LogTopic::Money,
                  "merces prestige wall pin: display poll instance {} emits {}: not one the table walk passed; left",
                  static_cast<const void*>(p_Poll), p_Value);

        return std::nullopt;
    }

    DisplayEntry& s_Entry = m_DisplayEntries[s_Index->second];
    std::string s_Why;
    const std::optional<DisplayAmounts> s_Amounts = DisplayAmountsOf(s_Entry, p_Value, &s_Why);
    const std::string s_Label = fmt::format("entry {:016x} (objective {}{})", s_Entry.ownerId,
                                            s_Entry.objective ? fmt::format("{}", *s_Entry.objective) : "?",
                                            s_Entry.objective ? ", " + s_Entry.objectiveFrom : std::string());

    if (!s_Amounts) {
        ++m_DisplayCounts["left: " + s_Why.substr(0, s_Why.find(':'))];

        if (s_Log)
            LogTo(fr::LogTopic::Money, "merces prestige wall pin: {} emits {}: {}; left", s_Label, p_Value, s_Why);

        return std::nullopt;
    }

    const fr::DisplayPlan s_Plan =
        fr::PlanDisplayWrite(p_Value, s_Amounts->base, *s_Amounts->written, m_DisplayBonus, s_Entry.lastWrite);
    ++m_DisplayCounts[fr::DisplayActionName(s_Plan.action)];

    if (s_Plan.action != fr::DisplayAction::Write) {
        if (s_Log)
            LogTo(fr::LogTopic::Money,
                  "merces prestige wall pin: {} emits {}; table base {} written {} (bonus {}): {}; left", s_Label,
                  p_Value, s_Amounts->base, *s_Amounts->written, m_DisplayBonus, fr::DisplayActionName(s_Plan.action));

        return std::nullopt;
    }

    if (const DWORD s_Code = SafeWritePayload(&p_Data, s_Plan.target)) {
        ++m_DisplayCounts["failed"];

        if (s_Log)
            Log("merces prestige wall pin: {} emits {}: rewriting the signal: {}", s_Label, p_Value, FaultText(s_Code));

        return std::nullopt;
    }

    std::string s_CopyText = "no copy";

    if (s_Entry.copy) {
        SetValueArgs s_Args {s_Entry.copy, s_Plan.target, false};
        const DWORD s_Code = SafeSetValue(&s_Args);
        s_CopyText = s_Code ? "copy: " + FaultText(s_Code) : s_Args.ok ? "copy written" : "copy write failed";

        if (!s_Code && s_Args.ok)
            s_Entry.lastWrite = s_Plan.target;
    }

    if (s_Log)
        LogTo(fr::LogTopic::Money,
              "merces prestige wall pin: {} emits {} -> sent as {} (table base {} written {} + bonus {}); {}", s_Label,
              p_Value, s_Plan.target, s_Amounts->base, *s_Amounts->written, m_DisplayBonus, s_CopyText);

    return s_Plan.target;
}

// At scene playing and after 5 s: each entry's copy against what its
// objective's table entry shows (written + bonus). An entry still unnamed is
// looked at again (its instance's values are set by now); a copy holding the
// vanilla amount is written (late: a card may already show what the poll
// sent), so a later poll or read gets the scaled amount. Game thread.
void MercesPayouts::CheckDisplayEntries(const char* p_Moment) {
    std::lock_guard s_Lock(m_DisplayMutex);

    if (m_DisplayEntries.empty())
        return;

    std::string s_Counts;

    for (const auto& [s_What, s_Count] : m_DisplayCounts)
        s_Counts += fmt::format("{}{} {}", s_Counts.empty() ? "" : ", ", s_Count, s_What);

    size_t s_Ok = 0;
    size_t s_Late = 0;
    size_t s_Other = 0;
    size_t s_Unnamed = 0;
    size_t s_Lines = 0;

    for (DisplayEntry& s_Entry : m_DisplayEntries) {
        if (!s_Entry.copy)
            continue;

        if (!s_Entry.objective && s_Entry.owner) {
            s_Entry.objective = ObjectiveOfOwner(s_Entry.owner, s_Entry.ownerId, &s_Entry.objectiveFrom, &s_Entry.name);
        }

        const ValueRead s_Now = ReadValue(s_Entry.copy);

        if (s_Now.status != ValueRead::Status::Ok)
            continue;

        std::string s_Why;
        const std::optional<DisplayAmounts> s_Amounts = DisplayAmountsOf(s_Entry, s_Now.value, &s_Why);

        if (!s_Amounts) {
            ++s_Unnamed;
            continue;
        }

        const int32_t s_Shows = *s_Amounts->written + m_DisplayBonus;

        if (s_Now.value == s_Shows) {
            ++s_Ok;
            continue;
        }

        const fr::DisplayPlan s_Plan =
            fr::PlanDisplayWrite(s_Now.value, s_Amounts->base, *s_Amounts->written, m_DisplayBonus, s_Entry.lastWrite);

        if (s_Plan.action == fr::DisplayAction::Write && Writes()) {
            SetValueArgs s_Args {s_Entry.copy, s_Plan.target, false};

            if (!SafeSetValue(&s_Args) && s_Args.ok) {
                s_Entry.lastWrite = s_Plan.target;
                ++s_Late;

                if (++s_Lines <= kMaxDisplayCheckLines)
                    LogTo(fr::LogTopic::Money,
                          "merces prestige wall check {}: entry {:016x} (objective {}): copy {} -> {} (late write: "
                          "the card may still show {})",
                          p_Moment, s_Entry.ownerId, *s_Entry.objective, s_Now.value, s_Plan.target, s_Now.value);

                continue;
            }
        }

        ++s_Other;

        if (++s_Lines <= kMaxDisplayCheckLines)
            LogTo(
                fr::LogTopic::Money,
                "merces prestige wall check {}: entry {:016x} (objective {}) copy holds {}, its table entry shows {}: "
                "MISMATCH ({})",
                p_Moment, s_Entry.ownerId, *s_Entry.objective, s_Now.value, s_Shows,
                fr::DisplayActionName(s_Plan.action));
    }

    LogTo(fr::LogTopic::Money,
          "merces prestige wall check {}: {} entries: {} copies show what their table entry shows, {} written late, "
          "{} mismatches, {} not named or without a written table entry; display poll signals so far: {}",
          p_Moment, m_DisplayEntries.size(), s_Ok, s_Late, s_Other, s_Unnamed, s_Counts.empty() ? "none" : s_Counts);
}

void MercesPayouts::OnOutputPin(const ZEntityRef& p_Entity, const ZObjectRef& p_Data) noexcept {
    Contained(kTag, "output pin", [&] { OutputPin(p_Entity, p_Data); });
}

// The prestige wall's display polls (payouts, safehouse):
// their int32 signal is rewritten (DisplayPin).
void MercesPayouts::OutputPin(const ZEntityRef& p_Entity, const ZObjectRef& p_Data) {
    const void* s_Int32 = m_Int32Type.load(std::memory_order_acquire);

    if (!s_Int32 || !p_Entity.m_pObj)
        return;

    PinPayload s_Payload;
    int32_t s_Value = 0;

    if (ReadPinPayload(p_Data, &s_Payload) || s_Payload.type != s_Int32 || !s_Payload.data ||
        ReadEngineBytes(reinterpret_cast<uintptr_t>(s_Payload.data), &s_Value, sizeof(s_Value)))
        return;

    uint64_t s_Id = 0;

    if (SafeEntityId(p_Entity.m_pObj, &s_Id))
        return;

    if (s_Id == fr::kPrestigeDisplayPollId && m_DisplayArmed.load(std::memory_order_acquire))
        DisplayPin(p_Entity.m_pObj, p_Data, s_Value);
}
