// Supply crates: crate items and crate guns (see
// CrateGuns.h). Every raw engine read and write follows the guard pattern in
// Guard.h.

#include "CrateGuns.h"

#include "ActorReads.h" // SafeOwning
#include "Collections.h"
#include "EngineText.h"
#include "EntityWalk.h"
#include "GameIds.h"
#include "Guard.h"
#include "LogTopics.h"
#include "LootMerces.h"
#include "Merces.h" // fr::JsonEventName
#include "ModLog.h"
#include "EntityReads.h" // ReadPosition
#include "Repository.h"
#include "Scene.h"
#include "SceneOf.h"
#include "SupplierRules.h" // fr::IsStockSelectorClass
#include "StockSteerMath.h"
#include "WeaponRarity.h"

#include <Functions.h>
#include <Globals.h>

#include <Glacier/Hash.h>
#include <Glacier/ZMath.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZPrimitives.h>
#include <Glacier/ZResource.h>
#include <Glacier/ZResourceID.h>
#include <Glacier/ZString.h>
#include <Glacier/ZTypeRegistry.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <set>
#include <string_view>

namespace {

constexpr int kMaxParentSteps = 12;          // logical parents climbed for an offer's option
constexpr double kReportSeconds = 3.0;       // an open's report window (offers spawn ~0.1 s after the open)
constexpr double kCenterDelaySeconds = 0.05; // crate_items: an offered item is measured this long after it spawned
constexpr double kCenterGiveUpSeconds = 0.9; // ... and left as it lies when still unread this long after
// ItemBoxCenter's *p_From for a middle measured this load (not a box: fr::kCrateCenterSlackMeters).
constexpr std::string_view kMeasuredFrom = "its middle measured where it spawned earlier this load";
constexpr size_t kMaxOptions = 256;          // crate options handled (95 seen)
constexpr size_t kMaxListed = 80;            // names in one candidates line
constexpr size_t kMaxLevelSpawners = 16'384; // crate_level_items: item spawners outside the option helpers kept
constexpr size_t kMaxLevelNames = 12;        // crate_level_items: kept items named in the log
constexpr const char* kTag = "crate guns";
constexpr const char* kItemsTag = "crate items";

// True on a thread while it holds CrateGuns' m_Mutex (HeldLock). The mod's
// engine writes under the lock (item keys, transforms, box sizes) run the
// engine's change handlers on the same thread, which may spawn an item or
// signal a pin into this module's hooks; those return at once instead of
// locking the non-recursive mutex again (a deadlock).
thread_local bool t_HoldingLock = false;

class HeldLock {
public:
    explicit HeldLock(std::mutex& p_Mutex) : m_Lock(p_Mutex) { t_HoldingLock = true; }
    ~HeldLock() { t_HoldingLock = false; }
    HeldLock(const HeldLock&) = delete;
    HeldLock& operator=(const HeldLock&) = delete;

private:
    std::lock_guard<std::mutex> m_Lock;
};

// The storage of a raw bool property, or 0.
uintptr_t RawBoolAddress(ZEntityType** p_Entity, std::string_view p_Name) {
    if (!p_Entity)
        return 0;

    const PropertyLocation s_Location = LocateProperty(p_Entity, p_Name);

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != 1 ||
        std::strcmp(s_Location.typeName, "bool") != 0)
        return 0;

    return s_Location.address;
}

std::optional<bool> ReadBoolAt(uintptr_t p_Address) {
    uint8_t s_Value = 0;

    if (!p_Address || ReadEngineBytes(p_Address, &s_Value, sizeof(s_Value)))
        return std::nullopt;

    return s_Value != 0;
}

std::optional<float> ReadFloatProperty(ZEntityType** p_Entity, std::string_view p_Name) {
    std::string s_Why;
    const uintptr_t s_Address = Raw4Address(p_Entity, p_Name, "float32", &s_Why);
    float s_Value = 0.0f;

    if (!s_Address || ReadEngineBytes(s_Address, &s_Value, sizeof(s_Value)))
        return std::nullopt;

    return s_Value;
}

bool WriteFloat(uintptr_t p_Address, float p_Value) {
    float s_Back = 0.0f;
    return p_Address && !WriteEngineBytes(p_Address, &p_Value, sizeof(p_Value)) &&
           !ReadEngineBytes(p_Address, &s_Back, sizeof(s_Back)) && s_Back == p_Value;
}

// --- item keys ------------------------------------------------------------------------------------------------

constexpr std::string_view kKeyProperty = "m_RepositoryId"; // ZItemRepositoryKeyEntity's item
constexpr size_t kMaxChoices = 512;                         // m_Choices elements read per selector
constexpr int kMaxOwnerSteps = 4;                           // owners climbed from a choice element to its option

// A key's item through the engine (its change handlers run, as for a value set in the editor).
struct KeySetArgs {
    ZEntityType** entity;
    fr::Guid id;
    bool ok;
};

__declspec(noinline) void KeySetOf(KeySetArgs* p_Args) {
    ZRepositoryID s_Id;
    std::memcpy(static_cast<void*>(&s_Id), &p_Args->id, sizeof(p_Args->id));
    p_Args->ok =
        ZEntityRef(p_Args->entity)
            .SetProperty(Hash::Crc32(kKeyProperty.data(), kKeyProperty.size()), ZObjectRef::From<ZRepositoryID>(s_Id));
}

__declspec(noinline) DWORD SafeKeySet(KeySetArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        KeySetOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// A key's m_RepositoryId, as the crate reads it (the SDK offset; the same read names the options).
std::optional<fr::Guid> KeyItem(ZEntityType** p_Key) {
    uint32_t s_Code = 0;
    return p_Key ? ReadRepositoryId(ZEntityRef(p_Key), &s_Code) : std::nullopt;
}

// Sets a key's item: SetProperty, else the 16 bytes straight into the
// property's storage. True when it reads back; *p_How says which way took.
bool WriteKeyItem(ZEntityType** p_Key, const fr::Guid& p_Item, std::string* p_How) {
    KeySetArgs s_Args {p_Key, p_Item, false};
    const DWORD s_SetCode = SafeKeySet(&s_Args);

    if (KeyItem(p_Key) == std::optional<fr::Guid>(p_Item)) {
        *p_How = "SetProperty";
        return true;
    }

    const PropertyLocation s_Location = LocateProperty(p_Key, kKeyProperty);

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != sizeof(fr::Guid)) {
        *p_How = fmt::format("SetProperty {}{}, no raw {} to write", s_Args.ok ? "returned true" : "failed",
                             s_SetCode ? " " + FaultText(s_SetCode) : std::string(), kKeyProperty);
        return false;
    }

    const bool s_Ok = !WriteEngineBytes(s_Location.address, &p_Item, sizeof(p_Item)) &&
                      KeyItem(p_Key) == std::optional<fr::Guid>(p_Item);
    *p_How = s_Ok ? "raw write" : "raw write did not read back";
    return s_Ok;
}

ZEntityType** OwnerOf(ZEntityType** p_Entity) {
    ZEntityType** s_Out = nullptr;

    if (!p_Entity || SafeOwning(p_Entity, &s_Out) || s_Out == p_Entity)
        return nullptr;

    return s_Out;
}

// A selector's m_Choices elements (each a 16-byte entity reference: the
// entity pointer first), or false when unreadable.
bool ReadChoiceElements(ZEntityType** p_Selector, std::vector<ZEntityType**>& p_Out) {
    p_Out.clear();
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

        if (ReadEngineBytes(s_Span->begin + 16 * k, &s_Pointer, sizeof(s_Pointer)) ||
            !fr::IsPlausibleEntityPointer(s_Pointer))
            s_Pointer = 0;

        p_Out.push_back(reinterpret_cast<ZEntityType**>(s_Pointer));
    }

    return true;
}

// --- item spawner transforms (an item's rest) -------------------------------------------------------------------

// A spatial entity's world transform, refreshed through the engine as
// GetObjectToWorldMatrix does (guarded), or empty when unread.
std::optional<fr::SpatialTransform> ReadSpatial(ZEntityType** p_Entity) {
    const PositionRead s_Read = ReadPosition(p_Entity, true);

    if (s_Read.faultCode || !s_Read.at || !s_Read.axes)
        return std::nullopt;

    return fr::SpatialTransform {*s_Read.axes, {s_Read.at->x, s_Read.at->y, s_Read.at->z}};
}

std::array<float, 12> RowsOf(const fr::SpatialTransform& p_Transform) {
    std::array<float, 12> s_Rows {};
    std::copy(p_Transform.axes.begin(), p_Transform.axes.end(), s_Rows.begin());
    std::copy(p_Transform.at.begin(), p_Transform.at.end(), s_Rows.begin() + 9);
    return s_Rows;
}

// Sets a spawner's world transform to p_Want: its m_mTransform property (the
// transform under m_eidParent, computed from the parent's world transform),
// else SetObjectToWorldMatrixFromEditor. True when the world transform reads
// back as p_Want; *p_How says which way took, or why neither did.
bool WriteSpawnerTransform(ZEntityType** p_Spawner, const fr::SpatialTransform& p_Want, std::string* p_How) {
    const auto s_ReadsBack = [&] {
        const std::optional<fr::SpatialTransform> s_Back = ReadSpatial(p_Spawner);
        return s_Back && fr::SameTransform(*s_Back, p_Want);
    };

    std::string s_Local;
    const RefRead s_Parent = ReadEntityRefProperty(p_Spawner, "m_eidParent");
    std::optional<fr::SpatialTransform> s_ParentWorld;

    if (!s_Parent.why.empty())
        s_Local = "m_eidParent unread: " + s_Parent.why;
    else if (!s_Parent.entity)
        s_ParentWorld = fr::SpatialTransform {}; // no parent: under the world
    else {
        s_ParentWorld = ReadSpatial(s_Parent.entity);

        if (!s_ParentWorld)
            s_Local = "m_eidParent's transform unread";
    }

    if (s_ParentWorld) {
        bool s_Ok = false;
        const uint32_t s_Code =
            WriteLocalTransform(p_Spawner, RowsOf(fr::LocalTransform(*s_ParentWorld, p_Want)), &s_Ok);

        if (!s_Code && s_ReadsBack()) {
            *p_How = "m_mTransform written";
            return true;
        }

        s_Local = s_Code ? "m_mTransform " + FaultText(s_Code)
                  : s_Ok ? std::string("m_mTransform set, did not read back")
                         : std::string("m_mTransform SetProperty failed");
    }

    bool s_Found = false;
    const uint32_t s_Code = WriteWorldTransform(p_Spawner, RowsOf(p_Want), &s_Found);

    if (!s_Code && s_Found && s_ReadsBack()) {
        *p_How = s_Local + "; SetObjectToWorldMatrixFromEditor written";
        return true;
    }

    *p_How = s_Local + "; SetObjectToWorldMatrixFromEditor " +
             (s_Code     ? FaultText(s_Code)
              : !s_Found ? std::string("not spatial")
                         : std::string("did not read back"));
    return false;
}

// --- selector boxes (crate_even_boxes) --------------------------------------------------------------------------

constexpr std::string_view kBoxSizeProperty = "m_vGlobalSize"; // ZBoxVolumeEntity's size (SVector3, along its axes)

// A raw SVector3 property (a box's m_vGlobalSize), or empty.
std::optional<std::array<double, 3>> ReadVector3Property(ZEntityType** p_Entity, std::string_view p_Name) {
    if (!p_Entity)
        return std::nullopt;

    const PropertyLocation s_Location = LocateProperty(p_Entity, p_Name);
    std::array<float, 3> s_Value {};

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != sizeof(s_Value) ||
        std::strcmp(s_Location.typeName, "SVector3") != 0 ||
        ReadEngineBytes(s_Location.address, s_Value.data(), sizeof(s_Value)))
        return std::nullopt;

    return std::array<double, 3> {s_Value[0], s_Value[1], s_Value[2]};
}

struct Vector3SetArgs {
    ZEntityType** entity;
    uint32_t propertyId;
    float x, y, z;
    bool ok;
};

__declspec(noinline) void Vector3SetOf(Vector3SetArgs* p_Args) {
    const SVector3 s_Value(p_Args->x, p_Args->y, p_Args->z);
    p_Args->ok = ZEntityRef(p_Args->entity).SetProperty(p_Args->propertyId, ZObjectRef::From<SVector3>(s_Value));
}

__declspec(noinline) DWORD SafeVector3Set(Vector3SetArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        Vector3SetOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// Sets an SVector3 property through the engine setter (its change handlers run, as for a value set in the editor).
// True when it reads back within a millimeter; *p_How says why not.
bool WriteVector3Property(ZEntityType** p_Entity, std::string_view p_Name, const std::array<double, 3>& p_Value,
                          std::string* p_How) {
    Vector3SetArgs s_Args {p_Entity,
                           Hash::Crc32(p_Name.data(), p_Name.size()),
                           static_cast<float>(p_Value[0]),
                           static_cast<float>(p_Value[1]),
                           static_cast<float>(p_Value[2]),
                           false};
    const DWORD s_Code = p_Entity ? SafeVector3Set(&s_Args) : 0xC0000005;
    const std::optional<std::array<double, 3>> s_Back = ReadVector3Property(p_Entity, p_Name);
    bool s_Same = s_Back.has_value();

    for (size_t k = 0; s_Back && k < 3; ++k)
        s_Same = s_Same && std::fabs((*s_Back)[k] - p_Value[k]) <= 0.001;

    if (s_Same)
        return true;

    *p_How = fmt::format("{} SetProperty {}{}", p_Name,
                         s_Code      ? FaultText(s_Code)
                         : s_Args.ok ? "true"
                                     : "false",
                         s_Back ? ", did not read back" : ", unreadable");
    return false;
}

// --- item resources (items the map has not loaded) -------------------------------------------------------------

// GetResourcePtr for the item's runtime resource id; the reference it takes is
// kept (the pointer's index is taken out before it is destroyed).
__declspec(noinline) void AcquireOf(uint64_t p_Rid, int32_t* p_Index) {
    ZResourcePtr s_Ptr;
    Globals::ResourceManager->GetResourcePtr(s_Ptr, ZRuntimeResourceID(p_Rid), 0);
    *p_Index = s_Ptr.m_nResourceIndex.val;
    s_Ptr.m_nResourceIndex.val = -1;
}

__declspec(noinline) DWORD SafeAcquire(uint64_t p_Rid, int32_t* p_Index) {
    DWORD s_Code = 0;

    __try {
        AcquireOf(p_Rid, p_Index);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// Drops the kept reference (a pointer holding the index, destroyed).
__declspec(noinline) void ReleaseOf(int32_t p_Index) {
    ZResourcePtr s_Ptr;
    s_Ptr.m_nResourceIndex.val = p_Index;
}

__declspec(noinline) DWORD SafeRelease(int32_t p_Index) {
    DWORD s_Code = 0;

    __try {
        ReleaseOf(p_Index);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct ResourceState {
    uint64_t rid;
    int32_t status;
    bool data;
    long refs;
};

__declspec(noinline) void ResourceStateOf(int32_t p_Index, ResourceState* p_Out) {
    const auto& s_Info = (*Globals::ResourceContainer)->m_resources[p_Index];
    p_Out->rid = s_Info.rid.GetID();
    p_Out->status = static_cast<int32_t>(s_Info.status);
    p_Out->data = s_Info.resourceData != nullptr;
    p_Out->refs = s_Info.refCount;
}

__declspec(noinline) DWORD SafeResourceState(int32_t p_Index, ResourceState* p_Out) {
    DWORD s_Code = 0;

    __try {
        ResourceStateOf(p_Index, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

const char* ResourceStatusName(int32_t p_Status) {
    switch (p_Status) {
        case RESOURCE_STATUS_UNKNOWN: return "unknown";
        case RESOURCE_STATUS_LOADING: return "loading";
        case RESOURCE_STATUS_INSTALLING: return "installing";
        case RESOURCE_STATUS_FAILED: return "FAILED";
        case RESOURCE_STATUS_VALID: return "valid (loaded)";
        default: return "?";
    }
}

std::string ResourceText(int32_t p_Index) {
    if (p_Index < 0)
        return "not known to the resource manager (no index)";

    ResourceState s_State {};

    if (const DWORD s_Code = SafeResourceState(p_Index, &s_State))
        return fmt::format("index {}, state unreadable {}", p_Index, FaultText(s_Code));

    return fmt::format("index {}, {}, data {}, {} references", p_Index, ResourceStatusName(s_State.status),
                       s_State.data ? "present" : "none", s_State.refs);
}

} // namespace

void CrateGuns::Configure(const fr::CrateItemsConfig& p_Items, const fr::CrateGunsConfig& p_Guns, OwnedItems p_Owned) {
    m_Items = fr::SanitizeCrateItemsConfig(p_Items);
    m_Config = fr::SanitizeCrateGunsConfig(p_Guns);
    m_Owned = std::move(p_Owned);

    if (m_Items.on)
        LogTo(fr::LogTopic::SupplierCrates, "{}: on: {} (lines under log_supplier_crates)", kItemsTag,
              fr::DescribeCrateItemsConfig(m_Items, m_Config));
    else if (m_Config.on)
        Log("{}: on: {}", kTag, fr::DescribeCrateGunsConfig(m_Config));
}

std::string CrateGuns::SeedText() const {
    return m_Seed ? fmt::format("seed {} ({})", m_Seed->seed, fr::SeedSourceName(m_Seed->source))
                  : std::string("no seed (0)");
}

int32_t CrateGuns::SeedValue() const {
    return m_Seed ? m_Seed->seed : 0;
}

void CrateGuns::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                               const std::optional<fr::SeedResolution>& p_Seed) noexcept {
    if (On() && !Reentered(Hook::LoadingStage))
        Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context, p_Seed); });
}

void CrateGuns::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                             const std::optional<fr::SeedResolution>& p_Seed) {
    if (!p_Context)
        return;

    if (p_Stage == ESceneLoadingStage::eLoading_AssetsLoaded) {
        // A new load (a restart may come without a ClearScene: the old entities may be gone, nothing is written).
        Contained(kTag, "scene clear", [&] { ClearScene(); });
        const std::string_view s_Type = p_Context->m_SceneInitParameters.m_Type.ToStringView();
        const std::string s_Scene =
            SceneOf(p_Context); // a full variant (Landslide, The Author) counts as its contract's scene

        if (!fr::IsFreelancerMission(s_Type, s_Scene))
            return;

        if (*Globals::TypeRegistry) {
            if (!m_Int32Type.load(std::memory_order_relaxed))
                m_Int32Type.store((*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("int32"))),
                                  std::memory_order_release);

            if (!m_Float32Type.load(std::memory_order_relaxed))
                m_Float32Type.store((*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("float32"))),
                                    std::memory_order_release);
        }

        const HeldLock s_Lock(m_Mutex);
        m_Scene = std::string(s_Scene);
        m_Pending = true;

        if (p_Seed)
            m_Seed = p_Seed;

        return;
    }

    {
        const HeldLock s_Lock(m_Mutex);

        if (!m_Pending)
            return;

        if (p_Seed && !m_Seed)
            m_Seed = p_Seed;
    }

    if (p_Stage == ESceneLoadingStage::eLoading_ScenePlaying) {
        // The CPD's owned items, before this lock (the loadout takes its own).
        const std::optional<std::vector<fr::Guid>> s_Owned =
            Wide() && m_Owned ? m_Owned() : std::optional<std::vector<fr::Guid>>();
        Setup(p_Context, s_Owned);
    }
}

// One walk of the loaded bricks: the stash items instance (the first entity
// whose own blueprint is the stash items template), then everything reached
// from it: its branch entities and crate seed, its crate options (entities
// whose own blueprint is the option helper) and their members. The walk visits
// a parent before its children, so an entity's owner is its parent's.
//
// With crate_items (Wide) the walk goes on through every
// brick: the stash items' ZRandomSelectors are kept, and every other option
// helper with its item key (fr::kOptionKeyId) and owned / entitled flags.
bool CrateGuns::Walk(ZEntitySceneContext* p_Context, std::string* p_Why) {
    // -2: the stash items root's members; >= 0: an option's members.
    constexpr int32_t kStash = -2;
    std::unordered_map<ZEntityType**, int32_t> s_Owner;
    // Members of the other option helpers (index into m_SceneOptions).
    std::unordered_map<ZEntityType**, size_t> s_SceneOwner;
    size_t s_Faults = 0;
    size_t s_FoundInBrick = SIZE_MAX;
    size_t s_BrickNumber = 0;
    const bool s_Wide = Wide();
    const bool s_LevelScan = s_Wide && LevelScan();

    std::vector<size_t> s_Order(p_Context->m_aLoadedBricks.size());

    for (size_t i = 0; i < s_Order.size(); ++i)
        s_Order[i] = i;

    const WalkStats s_Stats = VisitEntitiesInOrder(
        p_Context->m_aLoadedBricks, s_Order,
        [&](const VisitedEntity& p_Visited) {
            uint64_t s_Rid = 0;

            if (p_Visited.factory && ReadFactoryRid(p_Visited.factory, &s_Rid)) {
                ++s_Faults;
                s_Rid = 0;
            }

            if (!m_StashItems && (s_Rid == fr::kStashItemsTblu || s_Rid == fr::kStashItemsTemp)) {
                m_StashItems = p_Visited.entity;
                m_StashItemsBrick = p_Visited.brickRid;
                s_FoundInBrick = s_BrickNumber;
                s_Owner.emplace(p_Visited.entity, kStash);
                return true;
            }

            const auto s_Parent = m_StashItems ? s_Owner.find(p_Visited.parent) : s_Owner.end();

            if (s_Parent == s_Owner.end()) {
                // Outside the stash items: the other option helpers.
                if (!s_Wide)
                    return false;

                if (s_Rid == fr::kCrateOptionTblu) {
                    s_SceneOwner.emplace(p_Visited.entity, m_SceneOptions.size());
                    SceneOption s_New;
                    s_New.root = p_Visited.entity;
                    s_New.brick = p_Visited.brickRid;
                    std::string s_Class;

                    // For the log only (where an item's other placements are).
                    if (ReadEntityClassName(p_Visited.entity, s_Class, &s_New.id))
                        ++s_Faults;

                    if (p_Visited.parent && ReadEntityClassName(p_Visited.parent, s_New.holderClass, &s_New.holderId))
                        ++s_Faults;

                    m_SceneOptions.push_back(std::move(s_New));
                    return false;
                }

                const auto s_SceneParent = s_SceneOwner.find(p_Visited.parent);

                if (s_SceneParent == s_SceneOwner.end()) {
                    // crate_level_items: an item spawner outside every option helper (the level's items).
                    if (s_LevelScan) {
                        EntityClassCheck s_Check {};

                        if (CheckEntityClass(p_Visited.entity, "ZItemSpawner", &s_Check))
                            ++s_Faults;
                        else if (s_Check.matches && m_LevelSpawners.size() >= kMaxLevelSpawners)
                            ++m_LevelSpawnersOver;
                        else if (s_Check.matches)
                            m_LevelSpawners.emplace_back(p_Visited.entity, p_Visited.brickRid);
                    }

                    return false;
                }

                const size_t s_SceneIndex = s_SceneParent->second;
                s_SceneOwner.emplace(p_Visited.entity, s_SceneIndex);
                SceneOption& s_Scene = m_SceneOptions[s_SceneIndex];
                std::string s_Class;
                uint64_t s_Id = 0;

                if (ReadEntityClassName(p_Visited.entity, s_Class, &s_Id)) {
                    ++s_Faults;
                    return false;
                }

                if (s_Id == fr::kOptionKeyId && !s_Scene.key)
                    s_Scene.key = p_Visited.entity;
                else if (std::find(std::begin(fr::kUnownedFlagIds), std::end(fr::kUnownedFlagIds), s_Id) !=
                             std::end(fr::kUnownedFlagIds) &&
                         std::find(s_Scene.unownedIds.begin(), s_Scene.unownedIds.end(), s_Id) ==
                             s_Scene.unownedIds.end()) {
                    s_Scene.unownedIds.push_back(s_Id);
                    s_Scene.unownedFlags.push_back(p_Visited.entity);
                }
                else if (s_Id == fr::kEntitledFlagId && !s_Scene.entitledFlag)
                    s_Scene.entitledFlag = p_Visited.entity;
                // Its item spawners: listed with its item's rest, never learned from (RestOf; an entity once).
                else if (s_Class == "ZItemSpawner" && s_Scene.spawners.size() < 4 &&
                         std::find(s_Scene.spawners.begin(), s_Scene.spawners.end(), p_Visited.entity) ==
                             s_Scene.spawners.end()) {
                    s_Scene.spawners.push_back(p_Visited.entity);
                    s_Scene.spawnerIds.push_back(s_Id);
                }

                return false;
            }

            if (s_Rid == fr::kCrateOptionTblu && m_Options.size() < kMaxOptions) {
                Option s_Option;
                s_Option.root = p_Visited.entity;
                s_Option.holder = p_Visited.parent;
                std::string s_Class;
                ReadEntityClassName(p_Visited.entity, s_Class, &s_Option.id);
                const auto s_Index = static_cast<int32_t>(m_Options.size());
                m_OptionByRoot.emplace(p_Visited.entity, m_Options.size());
                m_Options.push_back(std::move(s_Option));
                s_Owner.emplace(p_Visited.entity, s_Index);
                return true;
            }

            const int32_t s_OwnerIndex = s_Parent->second;
            s_Owner.emplace(p_Visited.entity, s_OwnerIndex);
            std::string s_Class;
            uint64_t s_Id = 0;

            if (ReadEntityClassName(p_Visited.entity, s_Class, &s_Id)) {
                ++s_Faults;
                return false;
            }

            if (s_OwnerIndex == kStash) {
                switch (s_Id) {
                    case fr::kStashItemsSeedId:
                        if (!m_SeedEntity)
                            m_SeedEntity = p_Visited.entity;
                        break;
                    case fr::kCrateJunkBranchId:
                        if (!m_JunkChoice)
                            m_JunkChoice = p_Visited.entity;
                        break;
                    case fr::kCrateWeaponBranchId:
                        if (!m_WeaponChoice)
                            m_WeaponChoice = p_Visited.entity;
                        break;
                    default: break;
                }

                // The crate's slots (the branch selector is among them; it maps no option).
                if (s_Wide && fr::IsStockSelectorClass(s_Class))
                    m_Selectors.push_back(p_Visited.entity);

                return false;
            }

            Option& s_Option = m_Options[static_cast<size_t>(s_OwnerIndex)];

            // The item keys (an entity once: some members are reached at two depths).
            if (fr::IsCrateOptionKeyId(s_Id) && std::find(s_Option.keyEntities.begin(), s_Option.keyEntities.end(),
                                                          p_Visited.entity) == s_Option.keyEntities.end()) {
                s_Option.keyEntities.push_back(p_Visited.entity);
                s_Option.keyIds.push_back(s_Id);
            }

            // The owned / entitled flags (each member id once: some are reached at two depths).
            if (std::find(std::begin(fr::kUnownedFlagIds), std::end(fr::kUnownedFlagIds), s_Id) !=
                    std::end(fr::kUnownedFlagIds) &&
                std::find(s_Option.unownedIds.begin(), s_Option.unownedIds.end(), s_Id) == s_Option.unownedIds.end()) {
                s_Option.unownedIds.push_back(s_Id);
                s_Option.unownedFlags.push_back(p_Visited.entity);
                return false;
            }

            if (s_Id == fr::kEntitledFlagId) {
                if (!s_Option.entitledFlag)
                    s_Option.entitledFlag = p_Visited.entity;
                return false;
            }

            // Its item spawners (turned for its written item at an open: FitOption; an entity once).
            if (s_Class == "ZItemSpawner" && s_Option.spawners.size() < 4 &&
                std::find(s_Option.spawners.begin(), s_Option.spawners.end(), p_Visited.entity) ==
                    s_Option.spawners.end()) {
                s_Option.spawners.push_back(p_Visited.entity);
                s_Option.spawnerIds.push_back(s_Id);
                s_Option.spawnerTurns.push_back({});
            }

            // Its menu entry: its m_boundary names the option's selector box (BoxOf).
            if (s_Id == fr::kOptionMenuEntryId && !s_Option.menuEntry)
                s_Option.menuEntry = p_Visited.entity;

            switch (s_Id) {
                case fr::kOptionChoiceId:
                    if (!s_Option.choice && s_Class == "ZRandomSelectorChoice")
                        s_Option.choice = p_Visited.entity;
                    break;
                case fr::kOptionWeightMathId:
                    if (!s_Option.math)
                        s_Option.math = p_Visited.entity;
                    break;
                case fr::kOptionCostId:
                    if (!s_Option.cost)
                        s_Option.cost = p_Visited.entity;
                    break;
                case fr::kOptionStateId:
                    if (!s_Option.state)
                        s_Option.state = p_Visited.entity;
                    break;
                case fr::kOptionKeyId:
                    if (!s_Option.key)
                        s_Option.key = p_Visited.entity;
                    break;
                case fr::kOptionConditionId:
                    if (!s_Option.condition)
                        s_Option.condition = p_Visited.entity;
                    break;
                default: break;
            }

            return false;
        },
        kWideWalkEntityCap,
        [&](size_t p_Done) {
            s_BrickNumber = p_Done;
            // Everything under the stash items is in the brick it was found in (a wide walk goes on).
            return !s_Wide && m_StashItems && s_FoundInBrick != SIZE_MAX && p_Done > s_FoundInBrick;
        });

    LogTo(fr::LogTopic::SupplierCrates, "{}: walk: {} entities, {} bricks walked, {} faults{}; stash items {}", kTag,
          s_Stats.visited, s_Stats.bricks.size(), s_Faults + s_Stats.faults, s_Stats.capped ? " (capped)" : "",
          m_StashItems ? fmt::format("found in brick {:016X}", m_StashItemsBrick) : std::string("NOT found"));

    if (s_Wide)
        LogTo(fr::LogTopic::SupplierCrates,
              "{}: walk: {} selectors in the stash items, {} other option helpers ({} with an item key, "
              "{} with owned flags){}",
              kItemsTag, m_Selectors.size(), m_SceneOptions.size(),
              std::count_if(m_SceneOptions.begin(), m_SceneOptions.end(),
                            [](const SceneOption& o) { return o.key != nullptr; }),
              std::count_if(m_SceneOptions.begin(), m_SceneOptions.end(),
                            [](const SceneOption& o) { return !o.unownedFlags.empty(); }),
              s_LevelScan ? fmt::format(", {} item spawners outside them{}", m_LevelSpawners.size(),
                                        m_LevelSpawnersOver ? fmt::format(" ({} more left out: at most {})",
                                                                          m_LevelSpawnersOver, kMaxLevelSpawners)
                                                            : std::string())
                          : std::string(", items around the level off (crate_level_items)"));

    if (!m_StashItems) {
        *p_Why = "no setpiece_evergreen_stash_items instance in the loaded bricks";
        return false;
    }

    return true;
}

// Each option's item, from its repository key, named from pro.repo; returns
// pro.repo's items for the candidates (empty when it is not ready).
std::vector<RepoLoadoutEntry> CrateGuns::NameOptions() {
    for (Option& s_Option : m_Options) {
        if (!s_Option.key)
            continue;

        uint32_t s_Code = 0;
        s_Option.item = ReadRepositoryId(ZEntityRef(s_Option.key), &s_Code);
    }

    for (SceneOption& s_Option : m_SceneOptions)
        s_Option.item = KeyItem(s_Option.key);

    std::string s_Why;
    std::vector<RepoLoadoutEntry> s_Items;

    if (!m_Repo.Ready(s_Why)) {
        LogTo(fr::LogTopic::SupplierCrates, "{}: pro.repo: {}; options are not named, no firearm is known", kTag,
              s_Why);
        return s_Items;
    }

    size_t s_Overflow = 0;
    size_t s_Scanned = 0;

    if (const uint32_t s_Code = m_Repo.ScanLoadoutItems(s_Items, &s_Overflow, &s_Scanned))
        LogTo(fr::LogTopic::SupplierCrates, "{}: pro.repo item scan faulted ({}) after {} entries", kTag,
              FaultText(s_Code), s_Scanned);

    std::map<fr::Guid, const RepoLoadoutEntry*> s_ById;

    for (const RepoLoadoutEntry& s_Item : s_Items)
        s_ById.emplace(s_Item.guid, &s_Item);

    for (Option& s_Option : m_Options) {
        if (!s_Option.item)
            continue;

        if (const auto s_Found = s_ById.find(*s_Option.item); s_Found != s_ById.end()) {
            const RepoLoadoutEntry& s_Entry = *s_Found->second;
            s_Option.name = s_Entry.title;
            s_Option.icon = s_Entry.icon;

            if (s_Entry.hasRarity)
                s_Option.rarity = fr::RarityFromValue(s_Entry.rarity).value_or(0);
        }

        s_Option.gun = fr::IsFirearmIcon(s_Option.icon);
        s_Option.merces = fr::IsMercesItem(s_Option.name, s_Option.icon);
    }

    return s_Items;
}

// "Crowbar (tool) 0123abcd-..." from pro.repo's names (the candidates and the crate's options).
std::string CrateGuns::ItemText(const fr::Guid& p_Item) const {
    if (const fr::CrateCandidate* s_Item = Candidate(p_Item); s_Item && !s_Item->name.empty())
        return fmt::format("{} ({}) {}", s_Item->name, s_Item->icon.empty() ? "?" : s_Item->icon,
                           fr::FormatGuid(p_Item));

    for (const Option& s_Option : m_Options) {
        if (s_Option.item == std::optional<fr::Guid>(p_Item) && !s_Option.name.empty())
            return fmt::format("{} ({}) {}", s_Option.name, s_Option.icon.empty() ? "?" : s_Option.icon,
                               fr::FormatGuid(p_Item));
    }

    return fmt::format("{} (not named)", fr::FormatGuid(p_Item));
}

const fr::CrateCandidate* CrateGuns::Candidate(const fr::Guid& p_Item) const {
    for (const fr::CrateCandidate& s_Item : m_Candidates) {
        if (s_Item.item == p_Item)
            return &s_Item;
    }

    return nullptr;
}

void CrateGuns::Setup(ZEntitySceneContext* p_Context, const std::optional<std::vector<fr::Guid>>& p_Owned) {
    const HeldLock s_Lock(m_Mutex);
    m_Pending = false;
    const auto s_Start = Clock::now();
    std::string s_Why;

    if (!Walk(p_Context, &s_Why)) {
        LogTo(fr::LogTopic::SupplierCrates, "{}: {}: {}; crates stay vanilla", kTag, ShortSceneName(m_Scene), s_Why);

        if (Wide())
            LogTo(fr::LogTopic::SupplierCrates, "{}: {}: not run: {}", kItemsTag, ShortSceneName(m_Scene), s_Why);

        Forget(); // nothing armed: no summary at the scene clear
        return;
    }

    const std::vector<RepoLoadoutEntry> s_Repo = NameOptions();
    std::string s_Problems;

    m_SeedAddress = Raw4Address(m_SeedEntity, "m_nValue", "int32", &s_Why);

    if (!m_SeedAddress)
        s_Problems += fmt::format("crate seed {:016x}: {}; ", fr::kStashItemsSeedId, s_Why);
    else
        ReadEngineBytes(m_SeedAddress, &m_LastSeed, sizeof(m_LastSeed));

    m_JunkAddress = Raw4Address(m_JunkChoice, "m_Weight", "float32", &s_Why);

    if (!m_JunkAddress)
        s_Problems += fmt::format("tool branch {:016x}: {}; ", fr::kCrateJunkBranchId, s_Why);
    else
        ReadEngineBytes(m_JunkAddress, &m_VanillaJunk, sizeof(float));

    m_WeaponAddress = Raw4Address(m_WeaponChoice, "m_Weight", "float32", &s_Why);

    if (!m_WeaponAddress)
        s_Problems += fmt::format("weapon branch {:016x}: {}; ", fr::kCrateWeaponBranchId, s_Why);
    else
        ReadEngineBytes(m_WeaponAddress, &m_VanillaWeapon, sizeof(float));

    size_t s_Weighted = 0;

    for (size_t i = 0; i < m_Options.size(); ++i) {
        Option& s_Option = m_Options[i];
        s_Option.weightAddress = Raw4Address(s_Option.choice, "m_Weight", "float32", &s_Why);
        s_Weighted += s_Option.weightAddress ? 1 : 0;

        if (s_Option.gun && s_Option.weightAddress)
            m_Guns.push_back(i);

        // The owned flags and the choice condition (firearms: owned or not; crate_items writes them).
        for (ZEntityType** s_Flag : s_Option.unownedFlags) {
            if (const uintptr_t s_Address = RawBoolAddress(s_Flag, "m_bValue"))
                s_Option.unownedAddresses.push_back(s_Address);
        }

        s_Option.entitledAddress = RawBoolAddress(s_Option.entitledFlag, "m_bValue");
        s_Option.conditionAddress = RawBoolAddress(s_Option.condition, "m_bValue");
    }

    // Branch steering needs both branch weights; crate guns' firearm weights need their choices.
    if (!m_JunkAddress || !m_WeaponAddress || (!Wide() && m_Guns.empty())) {
        LogTo(fr::LogTopic::SupplierCrates,
              "{}: {}: {} options ({} firearms, {} with a weight), {}{}; crates stay vanilla", kTag,
              ShortSceneName(m_Scene), m_Options.size(), m_Guns.size(), s_Weighted, s_Problems,
              m_Guns.empty() ? "no firearm option found" : "a branch choice is not writable");

        if (Wide())
            LogTo(fr::LogTopic::SupplierCrates,
                  "{}: {}: not run: the crate's branch choices were not found (see the crate guns "
                  "line)",
                  kItemsTag, ShortSceneName(m_Scene));

        Forget(); // nothing armed: no summary at the scene clear
        return;
    }

    for (size_t i = 0; i < m_Options.size(); ++i) {
        if (m_Options[i].state)
            Watch(m_Options[i].state, i, kRoleState);

        if (m_Options[i].math)
            Watch(m_Options[i].math, i, kRoleMath);
    }

    if (m_SeedEntity)
        Watch(m_SeedEntity, 0, kRoleSeed);

    std::array<uint32_t, 4> s_Rarities {};
    std::string s_GunList;

    for (const size_t i : m_Guns) {
        ++s_Rarities[fr::CrateRarityIndex(m_Options[i].rarity)];
        s_GunList += fmt::format("{}{} ({})", s_GunList.empty() ? "" : ", ", m_Options[i].name,
                                 fr::CrateRarityName(m_Options[i].rarity));
    }

    std::string s_Merces;

    for (const Option& s_Option : m_Options) {
        if (s_Option.merces)
            s_Merces += fmt::format("{}{:016x} {}", s_Merces.empty() ? "" : ", ", s_Option.id, s_Option.name);
    }

    m_Armed.store(true, std::memory_order_release);
    LogTo(fr::LogTopic::SupplierCrates,
          "{}: {}: armed ({}, {:.0f} ms): stash items {:016X} brick {:016X}; {} crate options ({} named), {} firearms "
          "({} common, {} rare, {} epic, {} legendary; options without a rarity count as common): {}; merces option: "
          "{}; branch weights vanilla tool {:g} / weapon {:g}; crate seed {}{}{}",
          kTag, ShortSceneName(m_Scene), SeedText(),
          std::chrono::duration<double, std::milli>(Clock::now() - s_Start).count(), fr::kStashItemsTblu,
          m_StashItemsBrick, m_Options.size(),
          std::count_if(m_Options.begin(), m_Options.end(), [](const Option& o) { return !o.name.empty(); }),
          m_Guns.size(), s_Rarities[0], s_Rarities[1], s_Rarities[2], s_Rarities[3], s_GunList,
          s_Merces.empty() ? std::string("none") : s_Merces, m_VanillaJunk, m_VanillaWeapon,
          m_SeedAddress ? fmt::format("{}", m_LastSeed) : std::string("not readable (opens found by option pins)"),
          s_Problems.empty() ? std::string() : fmt::format("; {}", s_Problems),
          m_Items.on ? "; crate_items plans the crates (no gun roll here)" : "");

    if (Wide()) {
        MapSlots();
        BuildCandidates(s_Repo, p_Owned);
    }

    // crate_items: crate #1's keys now (written again at its open, with the choice conditions, which the game
    // sets up later than scene playing).
    if (m_Items.on && m_SlotOptions.size() >= 3) {
        m_Next = PlanItems(0, "scene playing");
        ApplyPlan(&*m_Next, std::nullopt, "scene playing");
    }
    else if (m_Items.on) {
        LogTo(fr::LogTopic::SupplierCrates,
              "{}: {}: not run: {} of the 3 tool slots found; crates stay as the game fills them", kItemsTag,
              ShortSceneName(m_Scene), m_SlotOptions.size());
    }

    WriteBranch(0, "scene playing", m_Next ? &*m_Next : nullptr, std::nullopt);
}

// Under m_Mutex, at scene playing: the stash items' selectors to their crate
// options (the tool pool's slots and the weapon pool's merces option) and the
// item keys of those options.
void CrateGuns::MapSlots() {
    const std::string s_Scene = ShortSceneName(m_Scene);

    // Each m_Choices element to its crate option: the option's holder, its choice or itself, up to four owners up.
    std::unordered_map<const void*, size_t> s_ByHolder;
    std::unordered_map<const void*, size_t> s_ByChoice;
    std::unordered_map<const void*, size_t> s_HolderCount;

    for (size_t i = 0; i < m_Options.size(); ++i) {
        ++s_HolderCount[m_Options[i].holder];

        if (m_Options[i].choice)
            s_ByChoice.emplace(m_Options[i].choice, i);
    }

    for (size_t i = 0; i < m_Options.size(); ++i) {
        if (m_Options[i].holder && s_HolderCount[m_Options[i].holder] == 1)
            s_ByHolder.emplace(m_Options[i].holder, i);
    }

    const auto s_OptionOf = [&](ZEntityType** p_Element) -> std::optional<size_t> {
        ZEntityType** s_At = p_Element;

        for (int s_Step = 0; s_At && s_Step < kMaxOwnerSteps; ++s_Step) {
            for (const auto* s_Map : {&s_ByHolder, &s_ByChoice, &m_OptionByRoot}) {
                if (const auto s_Found = s_Map->find(s_At); s_Found != s_Map->end())
                    return s_Found->second;
            }

            s_At = OwnerOf(s_At);
        }

        return std::nullopt;
    };

    std::vector<fr::CrateSlotSummary> s_Summaries;
    std::vector<std::vector<size_t>> s_SelectorOptions;
    std::string s_SelectorText;

    for (size_t k = 0; k < m_Selectors.size(); ++k) {
        std::vector<ZEntityType**> s_Elements;
        fr::CrateSlotSummary s_Summary;
        std::vector<size_t> s_Mapped;
        const bool s_Read = ReadChoiceElements(m_Selectors[k], s_Elements);
        s_Summary.choices = s_Elements.size();

        for (ZEntityType** s_Element : s_Elements) {
            const std::optional<size_t> s_Option = s_Element ? s_OptionOf(s_Element) : std::nullopt;

            if (!s_Option || std::find(s_Mapped.begin(), s_Mapped.end(), *s_Option) != s_Mapped.end())
                continue;

            s_Mapped.push_back(*s_Option);
            s_Summary.firearms += m_Options[*s_Option].gun ? 1 : 0;
            s_Summary.merces += m_Options[*s_Option].merces ? 1 : 0;
        }

        s_Summary.options = s_Mapped.size();
        s_SelectorText +=
            fmt::format("{}#{}: {}", s_SelectorText.empty() ? "" : "; ", k,
                        s_Read ? fmt::format("{} choices, {} crate options ({} firearms, {} merces)", s_Summary.choices,
                                             s_Summary.options, s_Summary.firearms, s_Summary.merces)
                               : std::string("m_Choices not readable"));
        s_Summaries.push_back(s_Summary);
        s_SelectorOptions.push_back(std::move(s_Mapped));
    }

    const std::vector<size_t> s_Slots = fr::CrateToolSlots(s_Summaries);
    std::string s_SlotText;

    for (size_t n = 0; n < s_Slots.size(); ++n) {
        m_SlotOptions.push_back(s_SelectorOptions[s_Slots[n]]);
        s_SlotText += fmt::format("{}slot {} = selector #{} ({} options)", s_SlotText.empty() ? "" : ", ", n + 1,
                                  s_Slots[n], s_Summaries[s_Slots[n]].options);
    }

    // The one-item crate: the merces option(s) of the weapon pool (the selector with firearms or merces).
    for (size_t k = 0; k < s_Summaries.size(); ++k) {
        if (s_Summaries[k].merces == 0 && s_Summaries[k].firearms == 0)
            continue;

        for (const size_t i : s_SelectorOptions[k]) {
            if (m_Options[i].merces &&
                std::find(m_SingleOptions.begin(), m_SingleOptions.end(), i) == m_SingleOptions.end())
                m_SingleOptions.push_back(i);
        }
    }

    // The keys of every slot option and one-item option, with the item each holds now.
    std::vector<size_t> s_Keyed;

    for (const std::vector<size_t>& s_Options : m_SlotOptions)
        s_Keyed.insert(s_Keyed.end(), s_Options.begin(), s_Options.end());

    s_Keyed.insert(s_Keyed.end(), m_SingleOptions.begin(), m_SingleOptions.end());
    size_t s_NoKeys = 0;

    for (const size_t i : s_Keyed) {
        const Option& s_Option = m_Options[i];
        s_NoKeys += s_Option.keyEntities.empty() ? 1 : 0;

        for (size_t k = 0; k < s_Option.keyEntities.size(); ++k) {
            const std::optional<fr::Guid> s_Now = KeyItem(s_Option.keyEntities[k]);

            if (!s_Now) {
                ++s_NoKeys;
                continue;
            }

            m_Keys.push_back({i, s_Option.keyIds[k], s_Option.keyEntities[k], *s_Now, *s_Now});
        }
    }

    std::string s_Single;

    for (const size_t i : m_SingleOptions)
        s_Single += fmt::format("{}{:016x} {}", s_Single.empty() ? "" : ", ", m_Options[i].id, m_Options[i].name);

    // The menu name's key and the draw's condition, per option written.
    size_t s_MenuKeys = 0;
    size_t s_Conditions = 0;
    size_t s_Flags = 0;

    for (const size_t i : s_Keyed) {
        const Option& s_Option = m_Options[i];
        s_MenuKeys +=
            std::find(s_Option.keyIds.begin(), s_Option.keyIds.end(), fr::kOptionMenuKeyId) != s_Option.keyIds.end()
                ? 1
                : 0;
        s_Conditions += s_Option.conditionAddress ? 1 : 0;
        s_Flags += s_Option.unownedAddresses.size() == std::size(fr::kUnownedFlagIds) ? 1 : 0;
    }

    LogTo(fr::LogTopic::SupplierCrates,
          "{}: {}: {} stash items selectors: {}; tool slots: {}; one-item crate (weapon pool): {}; {} item "
          "keys to write{}; of the {} options: {} with the menu name's key ({:016x}), {} with a writable choice "
          "condition "
          "({:016x}), {} with their 3 owned flags",
          kItemsTag, s_Scene, m_Selectors.size(), s_SelectorText.empty() ? std::string("none") : s_SelectorText,
          s_SlotText.empty() ? std::string("none") : s_SlotText,
          s_Single.empty() ? std::string("no merces option found (large items are left out)") : s_Single, m_Keys.size(),
          s_NoKeys ? fmt::format(" ({} options or keys without a readable item key: left alone)", s_NoKeys)
                   : std::string(),
          s_Keyed.size(), s_MenuKeys, fr::kOptionMenuKeyId, s_Conditions, fr::kOptionConditionId, s_Flags);
}

// Under m_Mutex: a gun option's owned / entitled flags, read now.
std::optional<bool> CrateGuns::FlagsEligible(const std::vector<ZEntityType**>& p_Unowned,
                                             ZEntityType** p_Entitled) const {
    fr::StockFlags s_Flags;

    for (ZEntityType** s_Flag : p_Unowned) {
        if (const std::optional<bool> s_Value = ReadBoolAt(RawBoolAddress(s_Flag, "m_bValue")))
            (*s_Value ? s_Flags.unownedTrue : s_Flags.unownedFalse) += 1;
    }

    s_Flags.entitled = ReadBoolAt(RawBoolAddress(p_Entitled, "m_bValue"));

    if (s_Flags.unownedTrue + s_Flags.unownedFalse == 0 || !s_Flags.entitled)
        return std::nullopt;

    return fr::StockEligible(s_Flags);
}

// Under m_Mutex, at scene playing: every item an option helper of the
// mission holds (loaded), and the firearms and Freelancer tools of pro.repo
// the map has not loaded.
void CrateGuns::BuildCandidates(const std::vector<RepoLoadoutEntry>& p_Repo,
                                const std::optional<std::vector<fr::Guid>>& p_Owned) {
    const std::string s_Scene = ShortSceneName(m_Scene);
    std::map<fr::Guid, const RepoLoadoutEntry*> s_ById;

    for (const RepoLoadoutEntry& s_Entry : p_Repo)
        s_ById.emplace(s_Entry.guid, &s_Entry);

    // The owned lists' ids and their names (an item with the same name as one you own is the same thing to you).
    fr::CrateOwnedItems s_Listed;

    if (p_Owned) {
        std::vector<std::pair<fr::Guid, std::string>> s_Named;
        s_Named.reserve(p_Repo.size());

        for (const RepoLoadoutEntry& s_Entry : p_Repo)
            s_Named.emplace_back(s_Entry.guid, std::string(s_Entry.title));

        s_Listed = fr::MakeCrateOwnedItems(*p_Owned, s_Named);
    }

    size_t s_OwnedByName = 0;

    // The crate's own tool-slot items (never large: the game puts them in three-item crates).
    std::set<fr::Guid> s_Vanilla;

    for (const KeySlot& s_Key : m_Keys) {
        if (std::find(m_SingleOptions.begin(), m_SingleOptions.end(), s_Key.option) == m_SingleOptions.end())
            s_Vanilla.insert(s_Key.original);
    }

    // Loaded items, with the owned flags' verdict where an option helper of a firearm has them, and where they are
    // held (a vanilla crate option, another option helper of the mission).
    std::map<fr::Guid, std::optional<bool>> s_Loaded;
    std::set<fr::Guid> s_InCrate;
    std::set<fr::Guid> s_OnMap;
    const auto s_IsGun = [&](const fr::Guid& p_Item) {
        const auto s_Found = s_ById.find(p_Item);
        return s_Found != s_ById.end() && fr::IsFirearmIcon(s_Found->second->icon);
    };
    const auto s_Add = [&](const fr::Guid& p_Item, const std::vector<ZEntityType**>& p_Unowned,
                           ZEntityType** p_Entitled) {
        if (p_Item.IsEmpty())
            return;

        std::optional<bool>& s_Flags = s_Loaded[p_Item];

        // A firearm's flags; two helpers that disagree count as not eligible.
        if (s_IsGun(p_Item) && !p_Unowned.empty()) {
            if (const std::optional<bool> s_Read = FlagsEligible(p_Unowned, p_Entitled))
                s_Flags = s_Flags ? (*s_Flags && *s_Read) : *s_Read;
        }
    };

    for (const Option& s_Option : m_Options) {
        if (s_Option.item) {
            s_Add(*s_Option.item, s_Option.unownedFlags, s_Option.entitledFlag);
            s_InCrate.insert(*s_Option.item);
        }
    }

    for (const SceneOption& s_Option : m_SceneOptions) {
        if (s_Option.item) {
            s_Add(*s_Option.item, s_Option.unownedFlags, s_Option.entitledFlag);
            s_OnMap.insert(*s_Option.item);
        }
    }

    // The level's loose items (crate_level_items; never firearms: no owned flags to read).
    std::set<fr::Guid> s_OnLevel;
    size_t s_LevelNew = 0;

    if (LevelScan()) {
        for (const fr::Guid& s_Item : ReadLevelItems(p_Repo)) {
            s_LevelNew += s_Loaded.contains(s_Item) ? 0 : 1;
            s_Add(s_Item, {}, nullptr);
            s_OnLevel.insert(s_Item);
        }
    }

    const auto s_Make = [&](const fr::Guid& p_Item, bool p_Loaded, std::optional<bool> p_Flags) {
        fr::CrateCandidate s_Item;
        s_Item.item = p_Item;
        s_Item.loaded = p_Loaded;
        s_Item.inCrate = s_InCrate.contains(p_Item);
        s_Item.onMap = s_OnMap.contains(p_Item);
        s_Item.onLevel = s_OnLevel.contains(p_Item);
        const auto s_Found = s_ById.find(p_Item);

        if (s_Found == s_ById.end())
            return s_Item; // unnamed: never offered

        const RepoLoadoutEntry& s_Entry = *s_Found->second;
        s_Item.name = s_Entry.title;
        s_Item.icon = s_Entry.icon;
        s_Item.rarity = s_Entry.hasRarity ? fr::RarityFromValue(s_Entry.rarity).value_or(0) : 0;
        s_Item.firearm = fr::IsFirearmIcon(s_Item.icon);
        s_Item.merces = fr::IsMercesItem(s_Item.name, s_Item.icon);
        s_Item.tool = fr::IsFreelancerTool(s_Entry.loseOnWounded == 1, s_Item.icon, s_Item.name);
        // Alone when it does not fit a slot: ItemSize large, longer than a slot by its box or model in the game
        // files, or with no size known a medium melee item of a long grip (the Hatchet).
        const fr::CrateItemFit s_Fit = fr::JudgeCrateItemFit(
            s_Entry.itemSize, s_Entry.grip, s_Item.icon, fr::CrateItemLongestSide(p_Item), s_Vanilla.contains(p_Item));
        s_Item.large = s_Fit.alone != fr::CrateAlone::No;

        if (s_Item.large && !s_Item.firearm)
            s_Item.aloneWhy = fr::DescribeCrateItemFit(s_Fit, s_Entry.grip);

        s_Item.runtime = fr::ParseRuntimeId(s_Entry.runtime).value_or(0);

        // Firearms: the owned flags and the CPD's lists (unknown counts as owned); any other item: in the CPD's lists.
        // Listed: by its id, or by its name (another id with the same name is listed).
        const fr::CrateOwnedBy s_By = fr::CrateItemListed(s_Listed, p_Item, s_Item.name);

        if (s_Item.firearm)
            s_Item.owned = fr::CrateGunOwned(p_Flags, p_Owned ? std::optional<bool>(s_By != fr::CrateOwnedBy::None)
                                                              : std::optional<bool>());
        else
            s_Item.owned = s_By != fr::CrateOwnedBy::None;

        if (s_By == fr::CrateOwnedBy::Name) {
            if (s_OwnedByName++ < kMaxListed)
                LogTo(fr::LogTopic::SupplierCrates,
                      "{}: {}: owned by name: {} \"{}\" (another item with this name is yours)", kItemsTag, s_Scene,
                      fr::FormatGuid(p_Item), s_Item.name);
        }

        return s_Item;
    };

    m_Candidates.clear();

    for (const auto& [s_Item, s_Flags] : s_Loaded)
        m_Candidates.push_back(s_Make(s_Item, true, s_Flags));

    // pro.repo's firearms and Freelancer tools the map has not loaded: named,
    // with a gear slot cost (a Freelancer item), needing no entitlement.
    size_t s_UnloadedGuns = 0;
    size_t s_UnloadedTools = 0;

    for (const RepoLoadoutEntry& s_Entry : p_Repo) {
        if (s_Loaded.contains(s_Entry.guid) || !s_Entry.title[0] || !s_Entry.hasCapacity || s_Entry.entitlement[0])
            continue;

        const bool s_Gun = fr::IsFirearmIcon(s_Entry.icon);
        const bool s_Tool = fr::IsFreelancerTool(s_Entry.loseOnWounded == 1, s_Entry.icon, s_Entry.title);

        if (!s_Gun && !s_Tool)
            continue;

        if (std::any_of(m_Candidates.begin(), m_Candidates.end(),
                        [&](const fr::CrateCandidate& c) { return c.item == s_Entry.guid; }))
            continue;

        m_Candidates.push_back(s_Make(s_Entry.guid, false, std::nullopt));
        s_UnloadedGuns += s_Gun ? 1 : 0;
        s_UnloadedTools += s_Tool ? 1 : 0;
    }

    // The log: the loaded guns you don't own, the Freelancer tools.
    std::string s_Guns;
    std::string s_Tools;
    size_t s_GunCount = 0;
    size_t s_ToolCount = 0;
    size_t s_FlagsRead = 0;

    for (const fr::CrateCandidate& s_Item : m_Candidates) {
        if (s_Item.firearm && s_Item.loaded) {
            s_FlagsRead += s_Loaded[s_Item.item] ? 1 : 0;

            if (!s_Item.owned && s_GunCount++ < kMaxListed)
                s_Guns += fmt::format("{}{} ({}{})", s_Guns.empty() ? "" : ", ", s_Item.name,
                                      fr::CrateRarityName(s_Item.rarity), s_Item.large ? ", large" : "");
        }

        if (s_Item.tool && s_ToolCount++ < kMaxListed)
            s_Tools +=
                fmt::format("{}{}{}", s_Tools.empty() ? "" : ", ", s_Item.name, s_Item.loaded ? "" : " (not loaded)");
    }

    const fr::CratePools s_Pools = fr::BuildCratePools(m_Candidates, m_Items, m_Config.on);
    LogTo(
        fr::LogTopic::SupplierCrates,
        "{}: {}: candidates: {} loaded items, what the mission's {} option helpers hold and {} ({} "
        "loaded firearms, {} with owned flags read), {} not loaded ({} guns, {} Freelancer tools); owned list from the "
        "CPD: {}; pools: {}",
        kItemsTag, s_Scene, s_Loaded.size(), m_Options.size() + m_SceneOptions.size(),
        LevelScan()
            ? fmt::format("the level's {} loose items ({} held by no option helper)", s_OnLevel.size(), s_LevelNew)
            : std::string("no items from around the level (crate_level_items off)"),
        std::count_if(m_Candidates.begin(), m_Candidates.end(),
                      [](const fr::CrateCandidate& c) { return c.firearm && c.loaded; }),
        s_FlagsRead, s_UnloadedGuns + s_UnloadedTools, s_UnloadedGuns, s_UnloadedTools,
        p_Owned ? fmt::format("{} items, {} names ({} candidates owned by name only)", s_Listed.ids.size(),
                              s_Listed.names.size(), s_OwnedByName)
                : std::string("none seen (guns without owned flags count as owned)"),
        fr::DescribeCratePools(s_Pools));
    LogTo(fr::LogTopic::SupplierCrates, "{}: {}: loaded guns you don't own ({}): {}; Freelancer tools ({}): {}",
          kItemsTag, s_Scene, s_GunCount, s_Guns.empty() ? std::string("none") : s_Guns, s_ToolCount,
          s_Tools.empty() ? std::string("none") : s_Tools);

    // The loaded items that come alone (they do not fit a three-item crate's slot), and why.
    std::string s_Alone;
    size_t s_AloneCount = 0;

    for (const fr::CrateCandidate& s_Item : m_Candidates) {
        if (s_Item.large && s_Item.loaded && !s_Item.firearm && !s_Item.name.empty() && s_AloneCount++ < kMaxListed)
            s_Alone += fmt::format("{}{} ({})", s_Alone.empty() ? "" : ", ", s_Item.name, s_Item.aloneWhy);
    }

    LogTo(fr::LogTopic::SupplierCrates,
          "{}: {}: loaded items that come alone, too big for a slot ({}, a slot holds {:.2f} m): {}", kItemsTag,
          s_Scene, s_AloneCount, fr::kCrateSlotLengthMeters, s_Alone.empty() ? std::string("none") : s_Alone);
}

// Under m_Mutex, at scene playing: each item spawner the walk kept outside the
// option helpers read (its item key's item, m_bSpawnOnStart, its brick),
// named from pro.repo, and the level's loose items collected and logged.
std::vector<fr::Guid> CrateGuns::ReadLevelItems(const std::vector<RepoLoadoutEntry>& p_Repo) {
    const std::string s_Scene = ShortSceneName(m_Scene);
    std::map<fr::Guid, const RepoLoadoutEntry*> s_ById;

    for (const RepoLoadoutEntry& s_Entry : p_Repo)
        s_ById.emplace(s_Entry.guid, &s_Entry);

    const uint64_t s_SetupBrick = fr::kEvergreenSetupBrick;
    std::vector<fr::LevelSpawner> s_Spawners;
    s_Spawners.reserve(m_LevelSpawners.size());
    size_t s_KeyUnread = 0;

    for (const auto& [s_Entity, s_Brick] : m_LevelSpawners) {
        fr::LevelSpawner s_Spawner;
        s_Spawner.freelancerBrick = s_Brick == s_SetupBrick;
        s_Spawner.spawnOnStart = ReadBoolAt(RawBoolAddress(s_Entity, "m_bSpawnOnStart"));
        const RefRead s_Key = ReadEntityRefProperty(s_Entity, "m_rMainItemKey");

        if (s_Key.entity)
            s_Spawner.item = KeyItem(s_Key.entity);
        else
            s_KeyUnread += s_Key.why.empty() ? 0 : 1; // null: a spawner with no key set

        if (s_Spawner.item) {
            if (const auto s_Found = s_ById.find(*s_Spawner.item); s_Found != s_ById.end()) {
                const RepoLoadoutEntry& s_Entry = *s_Found->second;
                s_Spawner.name = s_Entry.title;
                s_Spawner.commonName = s_Entry.commonName;
                s_Spawner.icon = s_Entry.icon;
                s_Spawner.itemType = s_Entry.itemType;
                s_Spawner.hasRuntime = fr::ParseRuntimeId(s_Entry.runtime).has_value();
            }
        }

        s_Spawners.push_back(std::move(s_Spawner));
    }

    const fr::LevelItems s_Items = fr::CollectLevelItems(s_Spawners);
    LogTo(fr::LogTopic::SupplierCrates, "{}: {}: items around the level: {}{}{}", kItemsTag, s_Scene,
          fr::DescribeLevelItems(s_Spawners, s_Items, kMaxLevelNames),
          s_KeyUnread ? fmt::format("; {} spawners' m_rMainItemKey unreadable", s_KeyUnread) : std::string(),
          p_Repo.empty() ? "; pro.repo not read: none can be named, none kept" : "");

    std::vector<fr::Guid> s_Kept;

    for (const size_t i : s_Items.kept)
        s_Kept.push_back(*s_Spawners[i].item);

    return s_Kept;
}

// Under m_Mutex: the keys of p_Options set to p_Item, or (p_Restore) to their own items.
std::optional<fr::Guid> CrateGuns::OptionItemNow(size_t p_Option) const {
    for (const KeySlot& s_Key : m_Keys) {
        if (s_Key.option == p_Option)
            return s_Key.current;
    }

    return p_Option < m_Options.size() ? m_Options[p_Option].item : std::nullopt;
}

// Under m_Mutex: the resource of an item the map has not loaded, requested and held.
void CrateGuns::HoldResource(const fr::CrateCandidate& p_Item, const char* p_Why) {
    if (p_Item.loaded ||
        std::any_of(m_Held.begin(), m_Held.end(), [&](const HeldResource& h) { return h.item == p_Item.item; }))
        return;

    HeldResource s_Held {p_Item.item, p_Item.name, p_Item.runtime, -1};

    if (!p_Item.runtime || !Globals::ResourceManager || !Globals::ResourceContainer) {
        LogTo(fr::LogTopic::SupplierCrates, "{}: resource of {} ({}): not requested: {}", kItemsTag, p_Item.name, p_Why,
              p_Item.runtime ? "no resource manager" : "pro.repo gives it no Runtime id");
        m_Held.push_back(s_Held);
        return;
    }

    const DWORD s_Code = SafeAcquire(p_Item.runtime, &s_Held.index);
    LogTo(fr::LogTopic::SupplierCrates, "{}: resource of {} ({}): GetResourcePtr(Runtime {:016X}){}: {}", kItemsTag,
          p_Item.name, p_Why, p_Item.runtime, s_Code ? " " + FaultText(s_Code) : std::string(),
          ResourceText(s_Held.index));
    m_Held.push_back(s_Held);
}

std::string CrateGuns::HeldText(const fr::Guid& p_Item) const {
    for (const HeldResource& s_Held : m_Held) {
        if (s_Held.item == p_Item)
            return s_Held.rid ? fmt::format("resource {:016X} {}", s_Held.rid, ResourceText(s_Held.index))
                              : std::string("resource not requested (no Runtime id)");
    }

    return "the map has it loaded";
}

// Each held reference dropped, only while its index still holds the item's
// resource (a container that reused the index holds another's).
void CrateGuns::ReleaseHeld() {
    for (const HeldResource& s_Held : m_Held) {
        if (s_Held.index < 0)
            continue;

        ResourceState s_State {};

        if (SafeResourceState(s_Held.index, &s_State) || s_State.rid != s_Held.rid) {
            LogTo(fr::LogTopic::SupplierCrates, "{}: resource of {}: not released (index {} holds {}, not {:016X})",
                  kItemsTag, s_Held.name, s_Held.index,
                  s_State.rid ? fmt::format("{:016X}", s_State.rid) : std::string("an unreadable resource"),
                  s_Held.rid);
            continue;
        }

        SafeRelease(s_Held.index);
    }

    m_Held.clear();
}

// --- crate_items: crates and their options ---------------------------------------------------------------------

const std::vector<size_t>* CrateGuns::GroupOptions(size_t p_Group) const {
    if (p_Group < m_SlotOptions.size() && p_Group < fr::kCrateOneItemGroup)
        return &m_SlotOptions[p_Group];

    return p_Group == fr::kCrateOneItemGroup ? &m_SingleOptions : nullptr;
}

std::optional<size_t> CrateGuns::GroupOf(size_t p_Option) const {
    for (size_t g = 0; g < fr::kCrateGroups; ++g) {
        const std::vector<size_t>* s_Options = GroupOptions(g);

        if (s_Options && std::find(s_Options->begin(), s_Options->end(), p_Option) != s_Options->end())
            return g;
    }

    return std::nullopt;
}

std::optional<size_t> CrateGuns::OneItemHeldElsewhere(std::optional<size_t> p_Crate) const {
    for (const size_t i : m_SingleOptions) {
        if (m_Options[i].heldBy && m_Options[i].heldBy != p_Crate)
            return m_Options[i].heldBy;
    }

    return std::nullopt;
}

bool CrateGuns::ToolItemOut() const {
    for (size_t g = 0; g < fr::kCrateOneItemGroup && g < m_SlotOptions.size(); ++g) {
        for (const size_t i : m_SlotOptions[g]) {
            if (m_Options[i].heldBy)
                return true;
        }
    }

    return false;
}

// Under m_Mutex: one option's keys to p_Item, or (empty) to their own items.
size_t CrateGuns::WriteKeysOf(size_t p_Option, const std::optional<fr::Guid>& p_Item, bool* p_Changed,
                              std::string* p_Failed) {
    size_t s_Ok = 0;

    for (KeySlot& s_Key : m_Keys) {
        if (s_Key.option != p_Option)
            continue;

        const fr::Guid s_Target = p_Item ? *p_Item : s_Key.original;
        std::string s_How;

        if (KeyItem(s_Key.entity) == std::optional<fr::Guid>(s_Target)) {
            s_Key.current = s_Target;
            ++s_Ok;
            continue;
        }

        *p_Changed = true;

        if (WriteKeyItem(s_Key.entity, s_Target, &s_How)) {
            s_Key.current = s_Target;
            ++s_Ok;
        }
        else if (p_Failed->size() < 600) {
            *p_Failed += fmt::format("{}{:016x} of option {:016x}: {}", p_Failed->empty() ? "" : "; ", s_Key.id,
                                     m_Options[s_Key.option].id, s_How);
        }
    }

    return s_Ok;
}

// Under m_Mutex: the draw takes an option whose choice condition holds; the
// condition follows the owned flags, which the game set for the option's own
// item (or for an item its keys named at a pickup) and never clears.
bool CrateGuns::SetEligible(Option& p_Option, bool p_On) {
    bool s_Ok = p_Option.conditionAddress != 0;
    const uint8_t s_Value = p_On ? 1 : 0;

    if (p_Option.conditionAddress)
        s_Ok =
            !WriteEngineBytes(p_Option.conditionAddress, &s_Value, 1) && ReadBoolAt(p_Option.conditionAddress) == p_On;

    // Kept out: one owned flag false is enough for a condition the game evaluates again.
    for (size_t k = 0; k < p_Option.unownedAddresses.size() && (p_On || k == 0); ++k)
        s_Ok = !WriteEngineBytes(p_Option.unownedAddresses[k], &s_Value, 1) && s_Ok;

    p_Option.excluded = !p_On;
    return s_Ok;
}

// Under m_Mutex: the plan for crate number p_Crate.
CrateGuns::Prepared CrateGuns::PlanItems(uint32_t p_Crate, const char* p_When) {
    fr::CratePools s_Pools = fr::BuildCratePools(m_Candidates, m_Items, m_Config.on, m_Planned);
    const std::optional<size_t> s_OneItemHeld = OneItemHeldElsewhere(std::nullopt);
    const bool s_ToolOut = ToolItemOut();

    // No one-item crate (a gun, a large item, merces) now while the one-item option still has an earlier crate's
    // item out (writing it would swap that item), while a tool slot has an item out (the weapon pool does not draw
    // the tool slots, so that item would lie beside the one item), or when the crate has no one-item option.
    if (s_OneItemHeld || s_ToolOut || m_SingleOptions.empty())
        fr::DropOneItemCrates(s_Pools);

    fr::CratePlan s_Plan = fr::PlanCrate(SeedValue(), m_Scene, p_Crate, m_Candidates, s_Pools, m_Items, m_Config);

    if (s_Plan.single && m_SingleOptions.empty()) {
        s_Plan.why += "; no one-item option found, so the crate stays as the game fills it";
        s_Plan.kind = fr::CratePlanKind::Vanilla;
        s_Plan.single.reset();
    }

    Prepared s_Prepared;
    s_Prepared.crate = p_Crate;
    s_Prepared.plan = s_Plan;

    for (size_t s = 0; s < 3; ++s) {
        if (s_Plan.slots[s]) {
            s_Prepared.slots[s] = m_Candidates[*s_Plan.slots[s]].item;
            s_Prepared.items.push_back(*s_Prepared.slots[s]);
        }
    }

    if (s_Plan.single) {
        s_Prepared.single = m_Candidates[*s_Plan.single].item;
        s_Prepared.items.push_back(*s_Prepared.single);
    }

    for (const fr::Guid& s_Item : s_Prepared.items) {
        const fr::CrateCandidate* s_Candidate = Candidate(s_Item);

        if (!s_Candidate)
            continue;

        if (!s_Candidate->loaded)
            HoldResource(*s_Candidate, fmt::format("crate #{}", p_Crate + 1).c_str());

        if (s_Candidate->firearm || s_Candidate->large)
            m_Planned.push_back(s_Item);
    }

    LogTo(fr::LogTopic::SupplierCrates, "{}: crate #{} planned at {}: {}; pools: {}{}", kItemsTag, p_Crate + 1, p_When,
          fr::DescribeCratePlan(s_Prepared.plan, m_Candidates), fr::DescribeCratePools(s_Pools),
          s_OneItemHeld
              ? fmt::format(" (no one-item crate: its option still has crate #{}'s item out)", *s_OneItemHeld + 1)
          : s_ToolOut               ? std::string(" (no one-item crate: a tool slot has an item out)")
          : m_SingleOptions.empty() ? std::string(" (no one-item crate: no merces option found)")
                                    : std::string());
    return s_Prepared;
}

// Under m_Mutex: a crate's plan into the options of its slots (see
// fr::CrateSlotActions) and its one-item option. p_Crate empty: crate #1's
// keys at scene playing (nothing held yet; the conditions are set at its open).
void CrateGuns::ApplyPlan(const Prepared* p_Plan, std::optional<size_t> p_Crate, const char* p_When) {
    const bool s_Flags = p_Crate.has_value();
    std::string s_Groups;
    std::string s_Failed;
    size_t s_KeysOk = 0;
    size_t s_KeysTotal = 0;
    size_t s_Changed = 0;
    size_t s_FlagsFailed = 0;
    // The crate floor, the slots' floors, middles and box middles, read once when an item's rest is wanted.
    std::optional<double> s_Floor;
    SlotFloors s_Floors;
    SlotMiddles s_Middles;
    SlotMiddles s_BoxMiddles;
    bool s_FloorRead = false;

    for (size_t g = 0; g < fr::kCrateGroups; ++g) {
        const std::vector<size_t>* s_Options = GroupOptions(g);

        if (!s_Options || s_Options->empty())
            continue;

        // What the group gets: crate_items its plan (an unplanned group: the options' own items).
        if (!ItemsOn())
            continue;

        std::optional<fr::Guid> s_Item;

        if (p_Plan)
            s_Item = g == fr::kCrateOneItemGroup ? p_Plan->single : p_Plan->slots[g];

        const bool s_Planned = s_Item.has_value();

        std::vector<std::optional<size_t>> s_Held;

        for (const size_t i : *s_Options)
            s_Held.push_back(p_Crate ? m_Options[i].heldBy : std::nullopt);

        const bool s_Picked = p_Crate && m_Crates[*p_Crate].picked[g];
        std::vector<fr::CrateOptionAction> s_Actions;

        // The one-item option is the weapon pool's only weighted choice: kept out only while this crate's item
        // still lies in it (opened again: drawn again, it would spawn a second one) or was taken; while another
        // crate has its item out it is left as it is (the branch keeps to the tool pool, and no one-item crate is
        // planned).
        if (g == fr::kCrateOneItemGroup) {
            for (size_t k = 0; k < s_Held.size(); ++k)
                s_Actions.push_back(s_Held[k] || s_Picked ? fr::CrateOptionAction::Exclude
                                                          : fr::CrateOptionAction::Offer);
        }
        else {
            s_Actions = fr::CrateSlotActions(s_Held, p_Crate.value_or(SIZE_MAX), s_Picked);

            // A reused option that holds the slot's item already would not change, and drawn again it would spawn
            // that item a second time: the item lying there is this crate's now, and nothing is drawn.
            for (size_t k = 0; k < s_Actions.size(); ++k) {
                Option& s_Option = m_Options[(*s_Options)[k]];

                if (s_Actions[k] != fr::CrateOptionAction::Reuse || !s_Planned ||
                    OptionItemNow((*s_Options)[k]) != s_Item)
                    continue;

                s_Actions[k] = fr::CrateOptionAction::Exclude;
                s_Option.heldBy = p_Crate;
                s_Held[k] = p_Crate;
            }
        }

        // crate_items at an open: every option that will spawn an item gets its spawners turned and moved so the
        // item lies as the game lays it (its rest axis and its spot in the slot, learned from its own vanilla
        // placement: RestOf), before its keys are written (an option with an item out re-spawns it at the key
        // write). An unplanned group's options get their own items back, so their spawners go back to vanilla. In a
        // planned slot an option that cannot be turned is kept out unless none is left (fr::KeepUnfitOut).
        fr::CrateFitPick s_FitPick;
        std::string s_UnfitText;
        std::optional<fr::CrateRest> s_GroupRest;
        size_t s_Writes = 0;
        size_t s_BoxWrites = 0; // crate_even_boxes: selector box sizes and spots written

        if (p_Crate) {
            std::vector<fr::CrateFit> s_Fits(s_Options->size(), fr::CrateFit::Unread);

            for (size_t k = 0; k < s_Options->size(); ++k) {
                Option& s_Option = m_Options[(*s_Options)[k]];
                const bool s_Spawns = s_Actions[k] != fr::CrateOptionAction::Exclude;
                const std::optional<fr::Guid> s_Written = s_Planned ? s_Item : s_Option.item;
                s_Option.centerTarget.reset(); // a written item's, set below (its own item lies as vanilla: none)

                if (!s_Spawns || !s_Written)
                    continue;

                if (!s_FloorRead) {
                    s_Floor = StashFloor();
                    s_Floors = StashSlotFloors(s_Floor);
                    s_Middles = StashSlotMiddles();
                    s_BoxMiddles = StashSlotBoxMiddles(s_Middles);
                    s_FloorRead = true;
                }

                // An option spawning its own item is the game's own placement, and the one-item option (the merces
                // option, for a large item or a gun) has no crate placement to learn from: both stay vanilla.
                if (g == fr::kCrateOneItemGroup || s_Written == s_Option.item) {
                    s_Option.restSource = fr::CrateRestSource::Learned; // the game's own placement
                    std::string s_Text;
                    s_Fits[k] =
                        FitOption((*s_Options)[k], nullptr, s_Floor, std::nullopt, std::nullopt, &s_Text, &s_Writes);

                    // Its box one size with the others' too (the one-item option's item comes alone: left as it is).
                    if (m_Items.evenBoxes && g != fr::kCrateOneItemGroup)
                        FitBox((*s_Options)[k], *s_Written, &s_BoxWrites);

                    continue;
                }

                const fr::CrateRest s_Rest = RestOf(*s_Written, s_Floor, s_Floors, s_Middles);
                s_Option.restSource = s_Rest.source;
                std::string s_Text;
                const bool s_Slot = g < s_Middles.size();
                s_Fits[k] = FitOption((*s_Options)[k], &s_Rest, s_Slot ? s_Floors[g] : s_Floor,
                                      s_Slot ? s_Middles[g] : std::nullopt, s_Slot ? s_BoxMiddles[g] : std::nullopt,
                                      &s_Text, &s_Writes);

                if (s_Slot)
                    CenterTarget((*s_Options)[k], *s_Written, s_BoxMiddles[g]);

                if (m_Items.evenBoxes)
                    FitBox((*s_Options)[k], *s_Written, &s_BoxWrites);

                if (s_Planned)
                    s_GroupRest = s_Rest;

                if (s_Fits[k] == fr::CrateFit::Unfit)
                    s_UnfitText +=
                        fmt::format("{}option {:016x}: {}", s_UnfitText.empty() ? "" : "; ", s_Option.id, s_Text);
            }

            if (s_Planned && g != fr::kCrateOneItemGroup)
                s_FitPick = fr::KeepUnfitOut(s_Actions, s_Fits);
        }

        std::array<size_t, 4> s_Count {};

        for (size_t k = 0; k < s_Options->size(); ++k) {
            const size_t i = (*s_Options)[k];
            Option& s_Option = m_Options[i];
            const fr::CrateOptionAction s_Action = s_Actions[k];
            const bool s_Skip = g == fr::kCrateOneItemGroup && s_Action == fr::CrateOptionAction::Exclude &&
                                s_Held[k] && s_Held[k] != p_Crate;
            const bool s_Out = s_Action == fr::CrateOptionAction::Reuse || s_Action == fr::CrateOptionAction::Rewrite;
            ++s_Count[static_cast<size_t>(s_Action)];

            if (s_Skip)
                continue;

            if (s_Action == fr::CrateOptionAction::Exclude) {
                if (s_Flags && !SetEligible(s_Option, false))
                    ++s_FlagsFailed;

                continue;
            }

            // Offer, Reuse, Rewrite: the keys get the group's item, so whatever the option has out (it came along
            // with the shared options) re-spawns as it and the menu names it.
            bool s_Wrote = false;
            s_KeysOk += WriteKeysOf(i, s_Item, &s_Wrote, &s_Failed);
            s_KeysTotal += static_cast<size_t>(
                std::count_if(m_Keys.begin(), m_Keys.end(), [&](const KeySlot& k2) { return k2.option == i; }));
            s_Changed += s_Wrote ? 1 : 0;

            if (s_Action == fr::CrateOptionAction::Rewrite) {
                if (s_Flags && !SetEligible(s_Option, false))
                    ++s_FlagsFailed;
            }
            // Reuse: the slot's one draw. Offer: its own item back without a change keeps the game's owned flags.
            else if (s_Flags && (s_Out || s_Planned || s_Wrote || s_Option.excluded) && !SetEligible(s_Option, true)) {
                ++s_FlagsFailed;
            }

            // Its item out is this crate's now (the offer, if the hook sees it, says the same).
            if (s_Flags && s_Out) {
                s_Option.heldBy = p_Crate;
                s_Option.heldItem = OptionItemNow(i).value_or(fr::Guid {});
            }
        }

        const std::string s_What = s_Planned ? ItemText(*s_Item) : std::string("their own items");
        const size_t s_Reused = s_Count[static_cast<size_t>(fr::CrateOptionAction::Reuse)];
        const size_t s_Rewritten = s_Count[static_cast<size_t>(fr::CrateOptionAction::Rewrite)];
        const size_t s_Excluded = s_Count[static_cast<size_t>(fr::CrateOptionAction::Exclude)] - s_FitPick.keptOut;
        std::string s_Platform;

        if (s_GroupRest)
            s_Platform += fmt::format("; the item rests {}", fr::DescribeCrateRest(*s_GroupRest));

        if (s_Writes)
            s_Platform += fmt::format("; {} spawner transform{} written", s_Writes, s_Writes == 1 ? "" : "s");

        if (s_BoxWrites)
            s_Platform += fmt::format("; {} selector box write{} (one size, on the item)", s_BoxWrites,
                                      s_BoxWrites == 1 ? "" : "s");

        if (s_FitPick.turned)
            s_Platform += fmt::format(", {} option{} turned or moved for the item", s_FitPick.turned,
                                      s_FitPick.turned == 1 ? "" : "s");

        if (s_FitPick.keptOut)
            s_Platform +=
                fmt::format(", {} kept out (its spawner could not be turned to the item's rest)", s_FitPick.keptOut);

        if (s_FitPick.fallback)
            s_Platform +=
                ", no offering option could be turned to the item's rest: all offer it as they are (the item may "
                "stand on end)";

        if (s_FitPick.unfitReused)
            s_Platform += ", the reused option could not be turned (left as it is: it has the item out)";

        if (s_FitPick.unread)
            s_Platform += fmt::format(", {} option{} with no readable spawner (offered as it is)", s_FitPick.unread,
                                      s_FitPick.unread == 1 ? "" : "s");

        if (!s_UnfitText.empty())
            s_Platform += fmt::format(" [could not turn: {}]", s_UnfitText);

        // This crate's own item for the group still lies in it (opened again): nothing is drawn.
        const bool s_OwnOut = p_Crate && std::find(s_Held.begin(), s_Held.end(), p_Crate) != s_Held.end();
        s_Groups += fmt::format(
            "{}{}: {} offer {}{}{}{}{}", s_Groups.empty() ? "" : " | ",
            g == fr::kCrateOneItemGroup ? std::string("one-item option") : fmt::format("slot {}", g + 1),
            s_Count[static_cast<size_t>(fr::CrateOptionAction::Offer)], s_What,
            s_Reused ? fmt::format(", {} reused (it had an item out, which came along to this crate; it gets {} and is "
                                   "the one drawn)",
                                   s_Reused, s_What)
                     : std::string(),
            s_Rewritten ? fmt::format(", {} more with an item out rewritten to it (kept out of the draw)", s_Rewritten)
                        : std::string(),
            s_Excluded
                ? fmt::format(", {} {}", s_Excluded,
                              s_Picked   ? "kept out (an item was taken from this slot)"
                              : s_OwnOut ? "kept out (this crate's item lies in it already: drawn again, its option "
                                           "would spawn a second one)"
                              : g == fr::kCrateOneItemGroup ? "left as they are (another crate has their item out)"
                              : s_Reused                    ? "kept out (the reused option is drawn)"
                                                            : "kept out")
                : std::string(),
            s_Platform);
    }

    if (s_Groups.empty())
        return;

    const std::string s_Whose = p_Crate ? fmt::format("crate #{}", *p_Crate + 1) : std::string("crate #1");
    LogTo(fr::LogTopic::SupplierCrates, "{}: {} written at {}: {}; keys {} of {} read back ({} options changed){}{}",
          kItemsTag, s_Whose, p_When, s_Groups, s_KeysOk, s_KeysTotal, s_Changed,
          s_Flags ? (s_FlagsFailed ? fmt::format("; {} choice conditions NOT written", s_FlagsFailed)
                                   : std::string("; choice conditions set"))
                  : std::string("; choice conditions at its open"),
          s_Failed.empty() ? std::string() : fmt::format(" (FAILED: {})", s_Failed));
}

// Under m_Mutex, at an open (m_Open set): its crate, by the crate seed. A new
// crate gets its plan written now; a crate opened again gets it again
// (fr::CrateSlotActions): a slot whose item still lies in it, or was taken,
// draws nothing, so nothing spawns a second time (drawn again, its option
// would spawn its item again). Seen late (by the poll), nothing is written.
void CrateGuns::OpenCrate(int32_t p_Seed, const char* p_Via, OpenSeen p_Seen) {
    std::optional<size_t> s_Found;

    for (size_t c = 0; c < m_Crates.size(); ++c) {
        if (m_Crates[c].seed == p_Seed) {
            s_Found = c;
            break;
        }
    }

    if (s_Found) {
        Crate& s_Crate = m_Crates[*s_Found];
        s_Crate.lastOpen = m_Open->index;
        m_Open->crate = *s_Found;
        const char* s_How = p_Seen == OpenSeen::Poll ? "seen late: nothing written" : nullptr;

        LogTo(fr::LogTopic::SupplierCrates,
              "{}: crate open #{} (seen by {}): crate #{} again (first opened at crate open #{}{}){}{}", kItemsTag,
              m_Open->index + 1, p_Via, *s_Found + 1, s_Crate.firstOpen + 1,
              *s_Found == m_LastCrate ? "; the crate opened last" : "", s_How ? ": " : "", s_How ? s_How : "");

        if (!s_How)
            ApplyPlan(s_Crate.plan ? &*s_Crate.plan : nullptr, *s_Found,
                      fmt::format("crate open #{} (crate #{} again)", m_Open->index + 1, *s_Found + 1).c_str());

        m_LastCrate = s_Found;
        return;
    }

    Crate s_Crate;
    s_Crate.seed = p_Seed;
    s_Crate.firstOpen = m_Open->index;
    s_Crate.lastOpen = m_Open->index;
    const auto s_Number = static_cast<uint32_t>(m_Crates.size());

    if (ItemsOn()) {
        if (m_Next && m_Next->crate == s_Number)
            s_Crate.plan = std::move(m_Next);
        else
            s_Crate.plan = PlanItems(s_Number, fmt::format("crate open #{}", m_Open->index + 1).c_str());

        m_Next.reset();
    }

    m_Crates.push_back(std::move(s_Crate));
    const size_t s_Index = m_Crates.size() - 1;
    const Crate& s_New = m_Crates[s_Index];
    m_Open->crate = s_Index;
    m_LastCrate = s_Index;

    if (s_New.plan)
        LogTo(fr::LogTopic::SupplierCrates, "{}: crate open #{} (seen by {}): crate #{}: {}", kItemsTag,
              m_Open->index + 1, p_Via, s_Index + 1, fr::DescribeCratePlan(s_New.plan->plan, m_Candidates));

    if (p_Seen == OpenSeen::Poll && s_Index > 0) {
        ++m_LateOpens;
        LogTo(fr::LogTopic::SupplierCrates,
              "{}: crate open #{}: seen late (by the frame's seed poll, after its items spawned): "
              "its plan is not written; it shows what the options held",
              kItemsTag, m_Open->index + 1);
        return;
    }

    ApplyPlan(s_New.plan ? &*s_New.plan : nullptr, s_Index,
              fmt::format("crate open #{} ({})", m_Open->index + 1, p_Via).c_str());
}

// Under m_Mutex: an ItemPickedUp of an item a crate's option holds: that
// option is free again (its item left the crate), and the crate's slot taken.
void CrateGuns::CratePickup(uint32_t p_EventIndex, const std::string& p_Name, const fr::Guid& p_Item) {
    std::vector<fr::CrateHold> s_Holds;
    std::vector<size_t> s_Options;

    for (size_t i = 0; i < m_Options.size(); ++i) {
        const Option& s_Option = m_Options[i];

        if (s_Option.heldBy && s_Option.heldItem == p_Item && *s_Option.heldBy < m_Crates.size()) {
            s_Holds.push_back({*s_Option.heldBy, m_Crates[*s_Option.heldBy].lastOpen});
            s_Options.push_back(i);
        }
    }

    const std::optional<size_t> s_Source = fr::CratePickupSource(s_Holds);

    if (!s_Source)
        return;

    const size_t i = s_Options[*s_Source];
    const size_t s_Crate = *m_Options[i].heldBy;
    const std::optional<size_t> s_Group = GroupOf(i);
    m_Options[i].heldBy.reset();

    if (s_Group)
        m_Crates[s_Crate].picked[*s_Group] = true;

    const bool s_AnyHeld =
        std::any_of(m_Options.begin(), m_Options.end(), [](const Option& o) { return o.heldBy.has_value(); });
    m_PickupWatch.store(s_AnyHeld, std::memory_order_release);
    LogTo(fr::LogTopic::SupplierCrates, "{}: ItemPickedUp #{} \"{}\" {}: from crate #{} (crate open #{}), {}{}",
          kItemsTag, p_EventIndex, p_Name, ItemText(p_Item), s_Crate + 1, m_Crates[s_Crate].firstOpen + 1,
          !s_Group                             ? std::string("?")
          : *s_Group == fr::kCrateOneItemGroup ? std::string("its one item")
                                               : fmt::format("slot {}", *s_Group + 1),
          s_Holds.size() > 1 ? fmt::format(" ({} crates held it; the one opened last gave it)", s_Holds.size())
                             : std::string());
}

// Under m_Mutex: the firearms whose owned flags read not owned and whose
// entitled flag reads true (fr::StockEligible, the supplier's rule).
size_t CrateGuns::EligibleGuns(size_t* p_Read, std::string* p_Names) const {
    size_t s_Eligible = 0;
    *p_Read = 0;

    for (const size_t i : m_Guns) {
        const Option& s_Option = m_Options[i];
        fr::StockFlags s_Flags;

        for (const uintptr_t s_Address : s_Option.unownedAddresses) {
            if (const std::optional<bool> s_Value = ReadBoolAt(s_Address))
                (*s_Value ? s_Flags.unownedTrue : s_Flags.unownedFalse) += 1;
        }

        s_Flags.entitled = ReadBoolAt(s_Option.entitledAddress);

        if (s_Flags.unownedTrue + s_Flags.unownedFalse == 0 || !s_Flags.entitled)
            continue;

        ++*p_Read;

        if (!fr::StockEligible(s_Flags))
            continue;

        ++s_Eligible;

        if (p_Names)
            *p_Names += fmt::format("{}{}", p_Names->empty() ? "" : ", ", s_Option.name);
    }

    return s_Eligible;
}

// Under m_Mutex: the branch weights for crate open p_OpenIndex.
fr::CrateHitVerdict CrateGuns::WriteBranch(uint32_t p_OpenIndex, const char* p_When, const Prepared* p_Plan,
                                           std::optional<size_t> p_OneItemHeld) {
    // crate_items: the crate's plan decides.
    if (m_Items.on) {
        float s_JunkWeight = m_VanillaJunk;
        float s_WeaponWeight = m_VanillaWeapon;
        const fr::CrateBranchPick s_Pick =
            fr::CrateItemsBranch(p_Plan ? &p_Plan->plan : nullptr, p_OneItemHeld.has_value(), ToolItemOut());
        std::string s_What;

        if (s_Pick.steer == fr::CrateBranchSteer::Weapon) {
            s_JunkWeight = 0.0f;
            s_WeaponWeight = 1.0f;
        }
        else if (s_Pick.steer == fr::CrateBranchSteer::Tool) {
            s_JunkWeight = 1.0f;
            s_WeaponWeight = 0.0f;
        }

        switch (s_Pick.reason) {
            case fr::CrateBranchReason::NoPlan: s_What = "vanilla: no plan"; break;
            case fr::CrateBranchReason::OneItem: s_What = "weapon pool forced: a one-item crate"; break;
            case fr::CrateBranchReason::Merces:
                s_What = "weapon pool forced: a merces crate (its own merces option)";
                break;
            case fr::CrateBranchReason::ToolPlan: s_What = "tool pool forced: three items (no merces crate)"; break;
            case fr::CrateBranchReason::OneItemHeld:
                // The game's merces crate would offer the one-item option, which still has an earlier crate's item out.
                s_What = fmt::format("tool pool forced: the one-item option still has crate #{}'s item out",
                                     p_OneItemHeld ? *p_OneItemHeld + 1 : 0);
                break;
            case fr::CrateBranchReason::ToolSlotItemOut:
                // The weapon pool does not draw the tool slots: their items out would lie beside the merces.
                s_What = "tool pool forced: a tool slot has an item out (the game's merces crate would show it)";
                break;
            case fr::CrateBranchReason::Other: s_What = "vanilla"; break;
        }

        const bool s_Junk = WriteFloat(m_JunkAddress, s_JunkWeight);
        const bool s_Weapon = WriteFloat(m_WeaponAddress, s_WeaponWeight);
        m_BranchFor = p_OpenIndex;
        LogTo(fr::LogTopic::SupplierCrates,
              "{}: crate open #{}: branch weights tool {:g} / weapon {:g} ({}{}) written at {}{}", kItemsTag,
              p_OpenIndex + 1, s_JunkWeight, s_WeaponWeight, s_What,
              p_Plan ? fmt::format("; crate #{}'s plan", p_Plan->crate + 1) : std::string(), p_When,
              s_Junk && s_Weapon ? "" : " (WRITE FAILED: not read back)");
        return fr::CrateHitVerdict::Miss;
    }

    const fr::CrateBranchDraw s_Draw = fr::DrawCrateBranch(SeedValue(), m_Scene, p_OpenIndex, GunChance());
    size_t s_Read = 0;
    std::string s_Names;
    const size_t s_Eligible = s_Draw.weaponPool ? EligibleGuns(&s_Read, &s_Names) : 0;
    const fr::CrateHitVerdict s_Verdict = fr::JudgeCrateHit(s_Draw.weaponPool, m_Guns.size(), s_Read, s_Eligible);
    const fr::CrateBranchWeights s_Weights = fr::CrateBranchWeightsFor(s_Verdict == fr::CrateHitVerdict::Steer);
    const bool s_Junk = WriteFloat(m_JunkAddress, s_Weights.junk);
    const bool s_Weapon = WriteFloat(m_WeaponAddress, s_Weights.weapon);
    m_BranchFor = p_OpenIndex;
    LogTo(fr::LogTopic::SupplierCrates,
          "{}: crate open #{} roll {:.4f} vs gun chance {:g}: {}{} -> branch weights tool {:g} / weapon {:g} "
          "written at {}{}",
          kTag, p_OpenIndex + 1, s_Draw.u, GunChance(), fr::CrateHitVerdictText(s_Verdict),
          s_Draw.weaponPool ? fmt::format(" ({} of {} firearms not owned{}, {} with their flags read)", s_Eligible,
                                          m_Guns.size(), s_Names.empty() ? "" : ": " + s_Names, s_Read)
                            : std::string(),
          s_Weights.junk, s_Weights.weapon, p_When, s_Junk && s_Weapon ? "" : " (WRITE FAILED: not read back)");
    return s_Verdict;
}

// Under m_Mutex: a crate open starts (the crate seed changed, or a pool
// option activated with no open running). Crate items: its crate's plan goes
// into the options now (at the seed's pin the slots have not drawn yet).
void CrateGuns::BeginOpen(int32_t p_SeedValue, const char* p_Via, OpenSeen p_Seen) {
    if (m_Open && !m_Open->reported)
        Report("the next crate opened");

    Open s_Open;
    s_Open.index = m_Opens++;
    s_Open.seedValue = p_SeedValue;
    s_Open.draw = fr::DrawCrateBranch(SeedValue(), m_Scene, s_Open.index, GunChance());
    s_Open.via = p_Via;
    s_Open.at = Clock::now();
    m_LastSeed = p_SeedValue;

    for (Option& s_Option : m_Options) {
        s_Option.state0 = 0;
        s_Option.target = -1.0;
        s_Option.observed.reset();
    }

    m_Open = std::move(s_Open);
    m_Window.store(true, std::memory_order_release);
    LogTo(fr::LogTopic::SupplierCrates, "{}: crate open #{} (crate seed {}, seen by {})", kTag, m_Open->index + 1,
          p_SeedValue, p_Via);

    if (!Managed()) {
        // Crate guns: written again in any case: the branch pick follows the seed. A hit that finds every firearm
        // owned is written vanilla and handled as a miss.
        const fr::CrateHitVerdict s_Verdict = WriteBranch(m_Open->index, p_Via, nullptr, std::nullopt);
        m_Open->hitNoGun = s_Verdict == fr::CrateHitVerdict::NoGunToOffer;
        m_Open->draw.weaponPool = s_Verdict == fr::CrateHitVerdict::Steer;
        m_ForcedWeapon += m_Open->draw.weaponPool ? 1 : 0;
        m_HitsNoGun += m_Open->hitNoGun ? 1 : 0;
        return;
    }

    OpenCrate(p_SeedValue, p_Via, p_Seen);

    if (!m_Open->crate)
        return;

    const Crate& s_Crate = m_Crates[*m_Open->crate];
    WriteBranch(m_Open->index, p_Via, s_Crate.plan ? &*s_Crate.plan : nullptr, OneItemHeldElsewhere(*m_Open->crate));
}

double CrateGuns::CostOf(const Option& p_Option) const {
    if (p_Option.math) {
        if (const std::optional<float> s_B = ReadFloatProperty(p_Option.math, "m_fB"); s_B && *s_B > 1.0f)
            return *s_B;
    }

    if (p_Option.cost) {
        if (const std::optional<float> s_Cost = ReadFloatProperty(p_Option.cost, "m_fValue"); s_Cost && *s_Cost > 0.0f)
            return *s_Cost;
    }

    return 0.0; // fr::CrateGunWeights counts it as 100
}

// Under m_Mutex: the weapon pool's other weights: the ones the game gave
// non-firearms this open, else the merces option's vanilla weight (100 / cost).
double CrateGuns::OtherWeight(bool* p_Observed) const {
    double s_Observed = 0.0;
    double s_Expected = 0.0;
    bool s_Any = false;

    for (const Option& s_Option : m_Options) {
        if (s_Option.gun)
            continue;

        if (s_Option.observed) {
            s_Any = true;
            s_Observed += *s_Option.observed;
        }
        else if (s_Option.merces) {
            s_Expected += fr::CrateMercesWeight(CostOf(s_Option));
        }
    }

    *p_Observed = s_Any;
    // Observed weights win; the merces option's expected weight stands in until its own arrives.
    return s_Any ? s_Observed + s_Expected : s_Expected;
}

// Under m_Mutex, on a hit: every firearm option's m_Weight written for this open.
void CrateGuns::Balance(const char* p_Why) {
    if (!m_Open || !m_Open->draw.weaponPool)
        return;

    bool s_Observed = false;
    const double s_Other = m_Config.gunShare >= 1.0 ? 0.0 : OtherWeight(&s_Observed);
    std::vector<fr::CrateGun> s_Guns;

    for (const size_t i : m_Guns)
        s_Guns.push_back({m_Options[i].rarity, CostOf(m_Options[i])});

    const std::vector<double> s_Weights = fr::CrateGunWeights(s_Guns, s_Other, m_Config);
    std::string s_Lines;
    size_t s_Failed = 0;

    for (size_t g = 0; g < m_Guns.size(); ++g) {
        Option& s_Option = m_Options[m_Guns[g]];
        const auto s_Weight = static_cast<float>(s_Weights[g]);
        float s_Before = 0.0f;
        ReadEngineBytes(s_Option.weightAddress, &s_Before, sizeof(s_Before));
        const bool s_Ok = WriteFloat(s_Option.weightAddress, s_Weight);
        s_Option.target = s_Weight;
        ++m_Open->gunsWrites;
        s_Failed += s_Ok ? 0 : 1;

        if (!m_Open->balanced)
            s_Lines += fmt::format("{}{} ({}, cost {:g}): {:g} -> {:.4g}{}", s_Lines.empty() ? "" : "; ", s_Option.name,
                                   fr::CrateRarityName(s_Option.rarity), s_Guns[g].cost, s_Before, s_Weight,
                                   s_Ok ? "" : " FAILED");
    }

    const fr::CrateGunShares s_Shares = fr::CrateGunSharesOf(s_Guns, s_Weights, s_Other);
    LogTo(fr::LogTopic::SupplierCrates,
          "{}: crate open #{}: firearm weights written ({}): {} firearms against other weight {:.4g} ({}): firearms "
          "{:.0f}% of the pool; by rarity common {:.0f}%, rare {:.0f}%, epic {:.0f}%, legendary {:.0f}%{}{}",
          kTag, m_Open->index + 1, p_Why, m_Guns.size(), s_Other,
          m_Config.gunShare >= 1.0 ? "gun share 1: other weights zeroed"
          : s_Observed             ? "seen this open"
                                   : "the merces option's vanilla 100 / cost",
          100.0 * s_Shares.gunShare, 100.0 * s_Shares.rarityShare[0], 100.0 * s_Shares.rarityShare[1],
          100.0 * s_Shares.rarityShare[2], 100.0 * s_Shares.rarityShare[3],
          s_Failed ? fmt::format("; {} writes FAILED", s_Failed) : std::string(),
          s_Lines.empty() ? std::string() : fmt::format(": {}", s_Lines));
    m_Open->balanced = true;
    m_Open->otherUsed = s_Other;
}

// "Scissors [melee, no rarity, state 3] (keys hold (COMIC) Duck Cane)": the
// option is named by its own item; the item its keys hold now (what it
// spawns) is added when the mod wrote another.
std::string CrateGuns::OptionText(size_t p_Option) const {
    if (p_Option >= m_Options.size())
        return "?";

    const Option& s_Option = m_Options[p_Option];
    const std::optional<fr::Guid> s_Now = OptionItemNow(p_Option);
    const bool s_Swapped = s_Now && s_Now != s_Option.item;

    // The option's state value, not a pool name: the states do not tell the pools apart (fr::CratePoolTaken).
    return fmt::format("{} [{}{}, {}, state {}]{}",
                       s_Option.name.empty() ? fmt::format("{:016x}", s_Option.id) : s_Option.name,
                       s_Option.gun ? "FIREARM, " : "", s_Option.icon.empty() ? "?" : s_Option.icon,
                       fr::CrateRarityName(s_Option.rarity), s_Option.state0,
                       s_Swapped ? fmt::format(" (keys hold {})", ItemText(*s_Now)) : std::string());
}

// Under m_Mutex: the open's result line; with crate guns, the next open's branch weights.
void CrateGuns::Report(const char* p_When) {
    if (!m_Open || m_Open->reported)
        return;

    Open& s_Open = *m_Open;
    s_Open.reported = true;
    m_Window.store(false, std::memory_order_release);

    const uint32_t s_Tool = s_Open.pools[static_cast<size_t>(fr::CratePool::Tool)];
    const uint32_t s_Weapon = s_Open.pools[static_cast<size_t>(fr::CratePool::Weapon)];

    std::string s_Offers;
    size_t s_Guns = 0;
    size_t s_WeaponOnly = 0; // firearms and the merces option: only the weapon pool has them

    for (size_t k = 0; k < s_Open.offers.size(); ++k) {
        const size_t i = s_Open.offers[k];
        const bool s_Gun = i < m_Options.size() && m_Options[i].gun;
        s_Guns += s_Gun ? 1 : 0;
        s_WeaponOnly += (s_Gun || (i < m_Options.size() && m_Options[i].merces)) ? 1 : 0;
        s_Offers += fmt::format("{}{} (item blueprint {:016X})", s_Offers.empty() ? "" : " | ", OptionText(i),
                                s_Open.offerBlueprints[k]);
    }

    const fr::CratePool s_Pool = fr::CratePoolTaken({s_Open.offers.size(), s_WeaponOnly, s_Tool, s_Weapon});
    const char* s_Taken = s_Pool == fr::CratePool::Weapon ? "weapon pool"
                          : s_Pool == fr::CratePool::Tool ? "tool pool"
                                                          : "unknown (no offer or option state seen)";
    m_TookWeapon += s_Pool == fr::CratePool::Weapon ? 1 : 0;
    m_TookTool += s_Pool == fr::CratePool::Tool ? 1 : 0;
    const bool s_Missed = s_Open.draw.weaponPool && s_Pool != fr::CratePool::Weapon;
    m_Missed += s_Missed ? 1 : 0;

    m_Offers += static_cast<uint32_t>(s_Open.offers.size());
    m_GunOffers += static_cast<uint32_t>(s_Guns);

    // Did the firearm weights hold?
    std::string s_Held;

    if (s_Open.balanced) {
        size_t s_Ok = 0;
        std::string s_First;

        for (const size_t i : m_Guns) {
            float s_Now = -1.0f;
            ReadEngineBytes(m_Options[i].weightAddress, &s_Now, sizeof(s_Now));

            if (std::fabs(s_Now - static_cast<float>(m_Options[i].target)) < 1e-4f)
                ++s_Ok;
            else if (s_First.empty())
                s_First = fmt::format(" (first changed: {} {:g}, written {:.4g})", m_Options[i].name, s_Now,
                                      m_Options[i].target);
        }

        s_Held =
            fmt::format("; firearm weights {} of {} still as written{}; {} writes, {} weight-math outputs "
                        "rewritten, {} other weights zeroed",
                        s_Ok, m_Guns.size(), s_First, s_Open.gunsWrites, s_Open.payloadRewrites, s_Open.otherZeroed);
    }

    std::string s_Other;

    for (const Option& s_Option : m_Options) {
        if (s_Option.observed)
            s_Other += fmt::format("{}{} {:.4g}", s_Other.empty() ? "" : ", ", s_Option.name, *s_Option.observed);
    }

    LogTo(fr::LogTopic::SupplierCrates,
          "{}: crate open #{} result ({}): {}; the game took the {} ({} options in state 1, {} in state 3, "
          "{} other){}; {} offers, {} firearm{}: {}{}{}",
          kTag, s_Open.index + 1, p_When,
          m_Items.on ? std::string("planned by crate_items")
                     : fmt::format("roll {} -> {}", s_Open.draw.weaponPool || s_Open.hitNoGun ? "HIT" : "miss",
                                   s_Open.draw.weaponPool ? "forced weapon pool"
                                   : s_Open.hitNoGun      ? "vanilla (you own every gun a crate can offer)"
                                                          : "vanilla"),
          s_Taken, s_Tool, s_Weapon, s_Open.pools[static_cast<size_t>(fr::CratePool::Other)],
          s_Missed ? " - STEER MISSED: the weapon pool was forced but not taken" : "", s_Open.offers.size(), s_Guns,
          s_Guns == 1 ? "" : "s", s_Offers.empty() ? std::string("none seen") : s_Offers, s_Held,
          s_Other.empty() ? std::string() : fmt::format("; weapon-pool non-firearm weights seen: {}", s_Other));

    if (m_Items.on)
        ReportItems(s_Open);

    if (!m_OfferLog.empty())
        m_OfferLog += "; ";

    m_OfferLog += fmt::format("#{} {} {} {}", s_Open.index + 1,
                              m_Items.on               ? "planned"
                              : s_Open.draw.weaponPool ? "hit"
                              : s_Open.hitNoGun        ? "hit (no gun to offer)"
                                                       : "miss",
                              s_Taken, s_Guns ? "gun" : "no gun");

    // Crate guns: the next open's roll now (crate_items writes each crate's branch at its open).
    if (!Managed() && m_JunkAddress && m_WeaponAddress)
        WriteBranch(s_Open.index + 1, fmt::format("crate open #{} reported", s_Open.index + 1).c_str(), nullptr,
                    std::nullopt);
}

// Under m_Mutex, at an open's report with crate_items: the offers against its crate's plan.
void CrateGuns::ReportItems(const Open& p_Open) {
    if (!p_Open.crate || *p_Open.crate >= m_Crates.size() || !m_Crates[*p_Open.crate].plan)
        return;

    const Crate& s_Crate = m_Crates[*p_Open.crate];
    const Prepared& s_Prepared = *s_Crate.plan;
    const bool s_First = p_Open.index == s_Crate.firstOpen;
    std::string s_Offers;
    size_t s_FromPlan = 0;

    for (size_t k = 0; k < p_Open.offers.size(); ++k) {
        const size_t i = p_Open.offers[k];
        const std::optional<size_t> s_Group = GroupOf(i);
        const std::string s_Where = !s_Group                             ? std::string("an option outside the slots")
                                    : *s_Group == fr::kCrateOneItemGroup ? std::string("the one-item option")
                                                                         : fmt::format("slot {}", *s_Group + 1);
        const std::optional<fr::Guid> s_Now = OptionItemNow(i);
        // A merces crate's plan is the one-item option with its own item.
        const bool s_Merces = s_Prepared.plan.kind == fr::CratePlanKind::Merces && s_Group == fr::kCrateOneItemGroup;
        const bool s_Planned = s_Merces || (s_Now && std::find(s_Prepared.items.begin(), s_Prepared.items.end(),
                                                               *s_Now) != s_Prepared.items.end());
        s_FromPlan += s_Planned ? 1 : 0;
        s_Offers += fmt::format("{}{}: {} (spawned blueprint {:016X}){}", s_Offers.empty() ? "" : " | ", s_Where,
                                s_Now ? ItemText(*s_Now) : std::string("?"), p_Open.offerBlueprints[k],
                                s_Planned ? "" : " - not the plan's");
    }

    const size_t s_Expected = s_Prepared.items.size() + (s_Prepared.plan.kind == fr::CratePlanKind::Merces ? 1 : 0);
    std::string s_Held;

    for (const fr::Guid& s_Item : s_Prepared.items) {
        const fr::CrateCandidate* s_Candidate = Candidate(s_Item);

        if (s_Candidate && !s_Candidate->loaded)
            s_Held += fmt::format("; {}: {}", s_Candidate->name, HeldText(s_Item));
    }

    LogTo(fr::LogTopic::SupplierCrates,
          "{}: crate open #{} result: crate #{}{} ({}); {} offers, {} of the plan's {} items: {}{}{}", kItemsTag,
          p_Open.index + 1, *p_Open.crate + 1, s_First ? "" : " opened again",
          fr::CratePlanKindName(s_Prepared.plan.kind), p_Open.offers.size(), s_FromPlan, s_Expected,
          s_Offers.empty() ? std::string("none seen") : s_Offers,
          s_First && s_Expected > 0 && s_FromPlan < s_Expected && p_Open.offers.size() < 3
              ? " - FEWER items than planned (a slot offered nothing, or the game took the other pool)"
              : "",
          s_Held);
}

// Under m_Mutex (the crate seed's pin; transforms refreshed through the engine,
// every read guarded by EntityReads): option i's spawner n as the game
// placed it, the mod's change to it undone.
std::optional<fr::SpatialTransform> CrateGuns::SpawnerVanilla(size_t i, size_t n) {
    if (i >= m_Options.size() || n >= m_Options[i].spawners.size())
        return std::nullopt;

    const std::optional<fr::SpatialTransform> s_Now = ReadSpatial(m_Options[i].spawners[n]);

    if (!s_Now)
        return std::nullopt;

    const Option& s_Option = m_Options[i];
    return fr::UndoSpawnerTurn(*s_Now,
                               n < s_Option.spawnerTurns.size() ? s_Option.spawnerTurns[n] : fr::SpawnerTurn {});
}

std::optional<double> CrateGuns::StashFloor() {
    std::vector<fr::SpatialTransform> s_Spawners;

    for (size_t i = 0; i < m_Options.size(); ++i) {
        for (size_t n = 0; n < m_Options[i].spawners.size(); ++n) {
            if (const std::optional<fr::SpatialTransform> s_Vanilla = SpawnerVanilla(i, n))
                s_Spawners.push_back(*s_Vanilla);
        }
    }

    return fr::CrateFloorHeight(s_Spawners);
}

// Under m_Mutex (as StashFloor): each tool slot's middle, from its options' vanilla spawners (the stash stands in
// this crate now, so these are world positions in it).
CrateGuns::SlotMiddles CrateGuns::StashSlotMiddles() {
    SlotMiddles s_Out;

    for (size_t g = 0; g < s_Out.size() && g < m_SlotOptions.size(); ++g) {
        std::vector<fr::SpatialTransform> s_Spawners;

        for (const size_t i : m_SlotOptions[g]) {
            for (size_t n = 0; n < m_Options[i].spawners.size(); ++n) {
                if (const std::optional<fr::SpatialTransform> s_Vanilla = SpawnerVanilla(i, n))
                    s_Spawners.push_back(*s_Vanilla);
            }
        }

        s_Out[g] = fr::CrateSlotMiddle(s_Spawners);
    }

    return s_Out;
}

// Under m_Mutex (as StashFloor): where each tool slot's items sit, from its options' vanilla boxes (read as the
// spawners are, so in the same place; one farther than fr::kCrateBoxMiddleMaxMeters from the slot's middle is left).
CrateGuns::SlotMiddles CrateGuns::StashSlotBoxMiddles(const SlotMiddles& p_Middles) {
    SlotMiddles s_Out;

    for (size_t g = 0; g < s_Out.size() && g < m_SlotOptions.size(); ++g) {
        std::vector<fr::SpatialTransform> s_Boxes;

        for (const size_t i : m_SlotOptions[g]) {
            if (const std::optional<fr::CrateBox> s_Box = BoxVanilla(i))
                s_Boxes.push_back(s_Box->world);
        }

        s_Out[g] = fr::CrateSlotBoxMiddle(s_Boxes, p_Middles[g]);
    }

    return s_Out;
}

// Under m_Mutex (as StashFloor): each tool slot's floor (its options' flat vanilla spawners). The transforms read at
// an open are where the stash stood last, and that crate may stand tilted along its slots, so each slot gets its own
// floor.
CrateGuns::SlotFloors CrateGuns::StashSlotFloors(std::optional<double> p_Stash) {
    SlotFloors s_Out;

    for (size_t g = 0; g < s_Out.size(); ++g) {
        std::vector<fr::SpatialTransform> s_Spawners;

        if (g < m_SlotOptions.size()) {
            for (const size_t i : m_SlotOptions[g]) {
                for (size_t n = 0; n < m_Options[i].spawners.size(); ++n) {
                    if (const std::optional<fr::SpatialTransform> s_Vanilla = SpawnerVanilla(i, n))
                        s_Spawners.push_back(*s_Vanilla);
                }
            }
        }

        const std::optional<double> s_Floor = fr::CrateFloorHeight(s_Spawners);
        s_Out[g] = s_Floor ? s_Floor : p_Stash;
    }

    return s_Out;
}

// Under m_Mutex: an item's rest is learned only from the crate's own tool-slot
// options whose own item it is (the game laying that item in this crate:
// their spawners' rotation, rest axis and yaw, and height above the crate
// floor). The mission's other option helpers whose own item it is (the
// supplier's stock, reward crates) and the crate's one-item option are only
// listed: the supplier's stand or hang their items for show (an item
// learned from them could rest on its edge). An item with no such option and a twin
// (fr::CrateTwinOf: a crate item with the same box) is learned from the
// twin's crate options, the pose turned to the item's axes; one with neither
// keeps its own box center from the game files (fr::CrateItemBoxCenter), so
// it is centered in the slot as a twin is. Learned once per load; the pose
// (a world rotation) and its option's spot in its slot (from p_Middles) are
// re-read at every call.
fr::CrateRest CrateGuns::RestOf(const fr::Guid& p_Item, std::optional<double> p_Floor, const SlotFloors& p_Floors,
                                const SlotMiddles& p_Middles) {
    LearnedRest* s_Known = nullptr;

    for (LearnedRest& s_Learned : m_Rests) {
        if (s_Learned.item == p_Item) {
            s_Known = &s_Learned;
            break;
        }
    }

    if (!s_Known) {
        std::vector<fr::CrateRestSample> s_Samples;
        std::vector<std::pair<size_t, size_t>> s_At; // per sample: option and spawner (SIZE_MAX: not an option)
        // The crate's options whose own item is p_Item, then (for a twin) those whose own item is its twin.
        std::vector<fr::CrateRestSample> s_TwinSamples;
        std::vector<std::pair<size_t, size_t>> s_TwinAt;
        const std::optional<fr::CrateTwin> s_Twin = fr::CrateTwinOf(p_Item);

        for (size_t i = 0; i < m_Options.size(); ++i) {
            const bool s_Own = m_Options[i].item == std::optional<fr::Guid>(p_Item);

            if (!s_Own && !(s_Twin && m_Options[i].item == std::optional<fr::Guid>(s_Twin->twin)))
                continue;

            const std::optional<size_t> s_Group = GroupOf(i);
            const bool s_Slot = s_Group && *s_Group < fr::kCrateOneItemGroup;

            for (size_t n = 0; n < m_Options[i].spawners.size(); ++n) {
                fr::CrateRestSample s_Sample;
                const std::optional<fr::SpatialTransform> s_Vanilla = SpawnerVanilla(i, n);
                s_Sample.attach = ReadBoolAt(RawBoolAddress(m_Options[i].spawners[n], "m_bUsePlacementAttach"));
                s_Sample.where = s_Slot ? fr::CratePlacement::ToolSlot : fr::CratePlacement::Other;
                s_Sample.source =
                    fmt::format("{} option {:016x}, spawner {:016x}",
                                s_Slot    ? fmt::format("crate slot {}", *s_Group + 1)
                                : s_Group ? std::string("crate one-item")
                                          : std::string("crate (no slot)"),
                                m_Options[i].id, n < m_Options[i].spawnerIds.size() ? m_Options[i].spawnerIds[n] : 0);

                if (s_Vanilla) {
                    s_Sample.axes = s_Vanilla->axes;
                    // From its own slot's floor (a crate read tilted gives each slot its own).
                    const std::optional<double> s_Floor =
                        s_Slot && *s_Group < p_Floors.size() ? p_Floors[*s_Group] : p_Floor;

                    if (s_Floor)
                        s_Sample.lift = s_Vanilla->at[2] - *s_Floor;
                }

                (s_Own ? s_Samples : s_TwinSamples).push_back(std::move(s_Sample));
                (s_Own ? s_At : s_TwinAt).emplace_back(i, n);
            }
        }

        for (const SceneOption& s_Scene : m_SceneOptions) {
            if (s_Scene.item != std::optional<fr::Guid>(p_Item))
                continue;

            for (size_t n = 0; n < s_Scene.spawners.size(); ++n) {
                fr::CrateRestSample s_Sample;
                s_Sample.attach = ReadBoolAt(RawBoolAddress(s_Scene.spawners[n], "m_bUsePlacementAttach"));
                s_Sample.where = fr::CratePlacement::Other;
                s_Sample.source = fmt::format("mission option helper {:016x} (under {:016x} ({}), brick {:016X}), "
                                              "spawner {:016x}",
                                              s_Scene.id, s_Scene.holderId,
                                              s_Scene.holderClass.empty() ? std::string("?") : s_Scene.holderClass,
                                              s_Scene.brick, n < s_Scene.spawnerIds.size() ? s_Scene.spawnerIds[n] : 0);

                if (const std::optional<fr::SpatialTransform> s_Now = ReadSpatial(s_Scene.spawners[n]))
                    s_Sample.axes = s_Now->axes;

                s_Samples.push_back(std::move(s_Sample));
                s_At.emplace_back(SIZE_MAX, SIZE_MAX);
            }
        }

        LearnedRest s_New;
        s_New.item = p_Item;
        s_New.rest =
            fr::CrateRestFor(p_Item, s_Samples, s_TwinSamples, s_Twin ? ItemText(s_Twin->twin) : std::string());
        const bool s_ByTwin = s_New.rest.source == fr::CrateRestSource::Twin;
        const std::vector<std::pair<size_t, size_t>>& s_PoseAt = s_ByTwin ? s_TwinAt : s_At;

        if (s_New.rest.poseSample < s_PoseAt.size()) {
            s_New.poseOption = s_PoseAt[s_New.rest.poseSample].first;
            s_New.poseSpawner = s_PoseAt[s_New.rest.poseSample].second;

            if (s_ByTwin)
                s_New.poseTurn = s_Twin->turn;
        }

        m_Rests.push_back(std::move(s_New));
        s_Known = &m_Rests.back();
    }

    fr::CrateRest s_Rest = s_Known->rest;
    s_Rest.pose.reset();
    s_Rest.offset.reset();

    // An item with neither (the table or +Z) keeps its own box center from the game files (fr::CrateRestFor).
    if (s_Rest.source == fr::CrateRestSource::Twin)
        s_Rest.center.reset();

    // The pose now, in this crate (its option's vanilla rotation; unread: its axis alone), and that option's spot:
    // its spawner from the middle of its slot; a twin's: no spot, its box center from its spawner, turned to the
    // item's axes (the item is centered in its slot by it).
    if (s_Known->poseOption != SIZE_MAX) {
        if (const std::optional<fr::SpatialTransform> s_Vanilla =
                SpawnerVanilla(s_Known->poseOption, s_Known->poseSpawner)) {
            s_Rest.pose = fr::TurnAxes(s_Vanilla->axes, s_Known->poseTurn); // a twin's: turned to the item's axes
            const std::optional<size_t> s_Group = GroupOf(s_Known->poseOption);

            if (s_Rest.source == fr::CrateRestSource::Twin) {
                if (const std::optional<fr::CrateBox> s_Box = BoxVanilla(s_Known->poseOption)) {
                    const fr::WorldPoint s_Center {s_Box->world.at[0], s_Box->world.at[1], s_Box->world.at[2]};
                    s_Rest.center = fr::TurnPoint(fr::PointInSpawner(*s_Vanilla, s_Center), s_Known->poseTurn);
                }
            }
            else if (s_Group && *s_Group < p_Middles.size() && p_Middles[*s_Group]) {
                const fr::WorldPoint& s_Middle = *p_Middles[*s_Group];
                s_Rest.offset = fr::WorldPoint {s_Vanilla->at[0] - s_Middle[0], s_Vanilla->at[1] - s_Middle[1],
                                                s_Vanilla->at[2] - s_Middle[2]};
            }
        }
    }

    // An item with neither its own option nor a twin seen off its widest face earlier this load: laid on the side
    // measured (its box center, for the other side up, dropped).
    if (const std::optional<fr::LocalAxis> s_Axis = MeasuredRest(p_Item))
        fr::UseMeasuredRest(s_Rest, *s_Axis);

    // An item with neither its own option nor a twin, measured where it spawned earlier this load: centered by that.
    if (const std::optional<std::array<double, 3>> s_Measured = MeasuredCenter(p_Item))
        fr::UseMeasuredCenter(s_Rest, *s_Measured);

    return s_Rest;
}

// Under m_Mutex, at the crate seed's pin (before the keys are written and the
// items spawn): each spawner of option i set to its vanilla transform with
// the change FitSpawner gives (none: back to vanilla), unless it already holds
// that change. Each write is read back (WriteSpawnerTransform). p_Rest null:
// vanilla (each spawner fits as the game placed it).
fr::CrateFit CrateGuns::FitOption(size_t i, const fr::CrateRest* p_Rest, std::optional<double> p_Floor,
                                  std::optional<fr::WorldPoint> p_Middle, std::optional<fr::WorldPoint> p_BoxMiddle,
                                  std::string* p_Text, size_t* p_Writes) {
    if (i >= m_Options.size())
        return fr::CrateFit::Unread;

    Option& s_Option = m_Options[i];
    s_Option.spawnerTurns.resize(s_Option.spawners.size());
    std::vector<fr::CrateFit> s_Fits;
    std::string s_Text;

    for (size_t n = 0; n < s_Option.spawners.size(); ++n) {
        ZEntityType** s_Spawner = s_Option.spawners[n];
        const std::optional<fr::SpatialTransform> s_Vanilla = SpawnerVanilla(i, n);
        std::string s_One;

        if (!s_Vanilla) {
            s_Fits.push_back(fr::CrateFit::Unread);
            s_Text += fmt::format("{}unread (left as it is)", s_Text.empty() ? "" : " | ");
            continue;
        }

        fr::SpawnerFit s_Fit;

        if (p_Rest) {
            s_Fit = fr::FitSpawner(*s_Vanilla, p_Floor, *p_Rest, p_Middle, p_BoxMiddle);
        }
        else {
            s_Fit.up = fr::UpLocalAxis(s_Vanilla->axes);
            s_Fit.fits = true;
            s_Fit.posed = true; // its own pose
        }

        const std::string s_Up =
            s_Fit.up ? "vanilla local " + fr::LocalAxisText(*s_Fit.up) + " up" : std::string("vanilla: no axis up");
        const bool s_Turnable = s_Fit.up || s_Fit.posed;
        fr::CrateFit s_Result = !s_Turnable  ? fr::CrateFit::Unfit
                                : s_Fit.fits ? fr::CrateFit::Fits
                                             : fr::CrateFit::Turned;
        const std::string s_Axis = p_Rest ? fr::LocalAxisText(p_Rest->axis) : std::string("its own");
        const bool s_Twin = p_Rest && p_Rest->source == fr::CrateRestSource::Twin;
        const std::string s_To = s_Fit.posed ? fmt::format("the pose of the crate's own option for {} ({} up, its yaw)",
                                                           s_Twin ? "its twin, turned to its axes" : "it", s_Axis)
                                             : fmt::format("{} up", s_Axis);
        // "turned to <pose>, height +0.03 m, moved 0.14 m to the middle of the slot".
        const double s_Moved = std::hypot(s_Fit.change.slide[0], s_Fit.change.slide[1], s_Fit.change.slide[2]);
        std::string s_Change = s_Fit.change.turn != fr::kNoTurn ? "turned to " + s_To
                               : s_Fit.posed ? std::string("has the pose of the crate's own option for it")
                                             : s_Axis + " up already";

        if (s_Fit.change.lift != 0.0)
            s_Change += fmt::format(", height {:+.2f} m", s_Fit.change.lift);

        if (s_Moved > 0.0)
            s_Change += fmt::format(", moved {:.2f} m to {}", s_Moved,
                                    s_Fit.ownSpot    ? "the item's own spot in the slot"
                                    : s_Fit.centered ? "center its item in the slot"
                                                     : "the middle of the slot");

        if (s_Fit.change == s_Option.spawnerTurns[n]) {
            s_One = !s_Turnable ? s_Up + ", cannot be turned (left as it is)"
                    : s_Fit.fits
                        ? s_Up + (s_Fit.posed ? ", has that pose already (left vanilla)" : ", fits (left vanilla)")
                        : s_Up + ", already " + s_Change;
        }
        else {
            const fr::SpatialTransform s_Want = fr::ApplySpawnerTurn(*s_Vanilla, s_Fit.change);
            const std::optional<fr::SpatialTransform> s_Before = ReadSpatial(s_Spawner);
            std::string s_How;
            ++*p_Writes;

            if (WriteSpawnerTransform(s_Spawner, s_Want, &s_How)) {
                s_Option.spawnerTurns[n] = s_Fit.change;
                s_One = s_Fit.change == fr::SpawnerTurn {}
                            ? fmt::format("{}, {}: put back to vanilla ({})", s_Up,
                                          s_Fit.fits ? "fits" : "cannot be turned", s_How)
                            : fmt::format("{}: {} ({})", s_Up, s_Change, s_How);
            }
            else {
                // Not taken: put back as it was (the same way), so a spawner is never left half written.
                std::string s_Back = "unchanged";
                const std::optional<fr::SpatialTransform> s_After = ReadSpatial(s_Spawner);

                if (s_Before && s_After && !fr::SameTransform(*s_After, *s_Before)) {
                    std::string s_BackHow;
                    s_Back = WriteSpawnerTransform(s_Spawner, *s_Before, &s_BackHow)
                                 ? std::string("put back as it was")
                                 : "could NOT be put back: " + s_BackHow;
                }

                s_Result = fr::CrateFit::Unfit;
                s_One = fmt::format("{}: the change ({}) did not take ({}; {})", s_Up, s_Change, s_How, s_Back);
            }
        }

        s_Fits.push_back(s_Result);
        s_Text += (s_Text.empty() ? "" : " | ") + s_One;
    }

    *p_Text = s_Text.empty() ? std::string("no item spawner found (offered as it is)") : s_Text;
    return fr::OptionFit(s_Fits);
}

// Under m_Mutex: the box the option's menu entry draws over its item (the
// game files: its m_boundary, set by the item template to its one
// ZBoxVolumeEntity, "Boundary"), looked up once a load.
ZEntityType** CrateGuns::BoxOf(size_t i) {
    if (i >= m_Options.size())
        return nullptr;

    Option& s_Option = m_Options[i];

    if (s_Option.boxLooked)
        return s_Option.box;

    s_Option.boxLooked = true;

    if (!s_Option.menuEntry) {
        s_Option.boxWhy = fmt::format("no menu entry ({:016x}) found in the option", fr::kOptionMenuEntryId);
        return nullptr;
    }

    const RefRead s_Boundary = ReadEntityRefProperty(s_Option.menuEntry, "m_boundary");
    std::string s_Class;

    if (!s_Boundary.entity) {
        s_Option.boxWhy = s_Boundary.why.empty() ? std::string("its menu entry's m_boundary is null")
                                                 : "its menu entry's m_boundary unread: " + s_Boundary.why;
        return nullptr;
    }

    if (const uint32_t s_Code = ReadEntityClassName(s_Boundary.entity, s_Class, &s_Option.boxId)) {
        s_Option.boxWhy = "its menu entry's m_boundary " + FaultText(s_Code);
        return nullptr;
    }

    if (s_Class != "ZBoxVolumeEntity") {
        s_Option.boxWhy = fmt::format("its menu entry's m_boundary is {:016x} ({}), not a ZBoxVolumeEntity",
                                      s_Option.boxId, s_Class.empty() ? std::string("?") : s_Class);
        return nullptr;
    }

    s_Option.box = s_Boundary.entity;
    return s_Option.box;
}

// Under m_Mutex (as SpawnerVanilla). The box's transform under its parent
// (the option helper's "Pose when resting", which the mod never moves) and
// its size are read once a load, before the mod first writes them; its world
// transform follows the parent (the shared stash stands in each crate).
std::optional<fr::CrateBox> CrateGuns::BoxVanilla(size_t i) {
    if (!BoxOf(i))
        return std::nullopt;

    Option& s_Option = m_Options[i];
    const RefRead s_Parent = ReadEntityRefProperty(s_Option.box, "m_eidParent");
    std::optional<fr::SpatialTransform> s_ParentWorld;

    if (s_Parent.why.empty())
        s_ParentWorld = s_Parent.entity ? ReadSpatial(s_Parent.entity) : fr::SpatialTransform {};

    if ((!s_Option.boxLocal || !s_Option.boxSize) && !s_Option.boxWritten) {
        const std::optional<fr::SpatialTransform> s_World = ReadSpatial(s_Option.box);
        s_Option.boxSize = ReadVector3Property(s_Option.box, kBoxSizeProperty);

        if (s_World && s_ParentWorld)
            s_Option.boxLocal = fr::LocalTransform(*s_ParentWorld, *s_World);
    }

    if (!s_Option.boxLocal || !s_Option.boxSize || !s_ParentWorld)
        return std::nullopt;

    return fr::CrateBox {fr::WorldTransform(*s_ParentWorld, *s_Option.boxLocal), *s_Option.boxSize};
}

// Under m_Mutex: the medians of the three tool slots' vanilla boxes, once a load (logged once).
std::optional<fr::CrateBoxSize> CrateGuns::CommonBoxSize() {
    if (m_BoxSizeRead)
        return m_BoxSize;

    std::vector<fr::CrateBox> s_Boxes;
    size_t s_Total = 0;

    for (size_t g = 0; g < fr::kCrateOneItemGroup && g < m_SlotOptions.size(); ++g) {
        for (const size_t i : m_SlotOptions[g]) {
            ++s_Total;

            if (const std::optional<fr::CrateBox> s_Box = BoxVanilla(i))
                s_Boxes.push_back(*s_Box);
        }
    }

    m_BoxSize = fr::CommonCrateBoxSize(s_Boxes);
    m_BoxSizeRead = m_BoxSize.has_value(); // none read: tried again at the next open
    LogTo(fr::LogTopic::SupplierCrates, "{}: selector boxes: {} of the tool slots' {} options' boxes read; {}",
          kItemsTag, s_Boxes.size(), s_Total,
          m_BoxSize ? "every box a crate may draw gets their medians, " + fr::DescribeCrateBoxSize(*m_BoxSize)
                    : std::string("no common size (boxes left as they are)"));
    return m_BoxSize;
}

// Under m_Mutex: the item's box center from its spawner, in the spawner's axes: its own option's box (p_Own), else
// the box of the option its rest was learned from (a twin's turned to the item's axes), else its middle measured where
// it spawned earlier this load (CenterOffers), else its own box from the game files (fr::CrateItemBoxCenter: an item
// with neither, centered in the slot by it); *p_From says which.
std::optional<std::array<double, 3>> CrateGuns::ItemBoxCenter(const fr::Guid& p_Item, std::optional<size_t> p_Own,
                                                              std::string* p_From) {
    size_t s_Option = SIZE_MAX;
    size_t s_Spawner = 0;
    fr::AxisTurn s_Turn = fr::kNoTurn;

    if (p_Own) {
        s_Option = *p_Own;
        *p_From = "its own box's center";
    }
    else {
        for (const LearnedRest& s_Learned : m_Rests) {
            if (s_Learned.item != p_Item)
                continue;

            s_Option = s_Learned.poseOption;
            s_Spawner = s_Learned.poseSpawner;
            s_Turn = s_Learned.poseTurn;
            *p_From = s_Learned.rest.source == fr::CrateRestSource::Twin ? "its twin's box center, turned to its axes"
                                                                         : "the box center of its own option";
            break;
        }
    }

    if (s_Option == SIZE_MAX) {
        if (const std::optional<std::array<double, 3>> s_Measured = MeasuredCenter(p_Item)) {
            *p_From = kMeasuredFrom;
            return s_Measured;
        }

        // Laid on another side than its box shows (measured standing upright): its box center is not where it lies.
        if (MeasuredRest(p_Item))
            return std::nullopt;

        const std::optional<std::array<double, 3>> s_Template = fr::CrateItemBoxCenter(p_Item);

        if (s_Template)
            *p_From = "its box center in the game files (its option template)";

        return s_Template;
    }

    const std::optional<fr::CrateBox> s_Box = BoxVanilla(s_Option);
    const std::optional<fr::SpatialTransform> s_Spawner0 = SpawnerVanilla(s_Option, s_Spawner);

    if (!s_Box || !s_Spawner0)
        return std::nullopt;

    const fr::WorldPoint s_Center {s_Box->world.at[0], s_Box->world.at[1], s_Box->world.at[2]};
    return fr::TurnPoint(fr::PointInSpawner(*s_Spawner0, s_Center), s_Turn);
}

// Under m_Mutex, after FitOption placed option i's spawner (its first spawner: an option has one): its box gets the
// common size, turned to the box's axes, and goes where its item lies (fr::CrateBoxCenter); its rotation is kept.
// Each write is read back; nothing is written when it holds that already.
void CrateGuns::FitBox(size_t i, const fr::Guid& p_Item, size_t* p_Writes) {
    if (i >= m_Options.size() || !BoxOf(i))
        return;

    Option& s_Option = m_Options[i];

    const std::optional<fr::CrateBoxSize> s_Size = CommonBoxSize();
    const std::optional<fr::CrateBox> s_Box = BoxVanilla(i);
    const std::optional<fr::SpatialTransform> s_SpawnerVanilla =
        s_Option.spawners.empty() ? std::nullopt : SpawnerVanilla(i, 0);

    if (!s_Size || !s_Box || !s_SpawnerVanilla)
        return;

    const fr::SpatialTransform s_SpawnerNow = fr::ApplySpawnerTurn(
        *s_SpawnerVanilla, s_Option.spawnerTurns.empty() ? fr::SpawnerTurn {} : s_Option.spawnerTurns[0]);
    std::string s_From;
    const std::optional<size_t> s_Own =
        s_Option.item == std::optional<fr::Guid>(p_Item) ? std::optional<size_t>(i) : std::nullopt;
    const std::optional<std::array<double, 3>> s_ItemCenter = ItemBoxCenter(p_Item, s_Own, &s_From);
    const fr::WorldPoint s_Vanilla {s_Box->world.at[0], s_Box->world.at[1], s_Box->world.at[2]};
    const fr::WorldPoint s_At = fr::CrateBoxCenter(*s_SpawnerVanilla, s_SpawnerNow, s_Vanilla, s_ItemCenter);
    const std::array<double, 3> s_Extent = fr::CrateBoxExtent(s_Box->world.axes, *s_Size);
    const fr::SpatialTransform s_Want {
        s_Box->world.axes, {static_cast<float>(s_At[0]), static_cast<float>(s_At[1]), static_cast<float>(s_At[2])}};

    const std::optional<std::array<double, 3>> s_SizeNow = ReadVector3Property(s_Option.box, kBoxSizeProperty);
    bool s_SizeHeld = s_SizeNow.has_value();

    for (size_t k = 0; s_SizeNow && k < 3; ++k)
        s_SizeHeld = s_SizeHeld && std::fabs((*s_SizeNow)[k] - s_Extent[k]) <= 0.001;

    if (!s_SizeHeld) {
        std::string s_How;
        ++*p_Writes;
        s_Option.boxWritten = true;
        WriteVector3Property(s_Option.box, kBoxSizeProperty, s_Extent, &s_How);
    }

    const std::optional<fr::SpatialTransform> s_WorldNow = ReadSpatial(s_Option.box);

    if (!s_WorldNow || !fr::SameTransform(*s_WorldNow, s_Want)) {
        std::string s_How;
        ++*p_Writes;
        s_Option.boxWritten = true;

        WriteSpawnerTransform(s_Option.box, s_Want, &s_How);
    }
}

// Under m_Mutex, after FitOption placed option i's spawners: where its item's middle should lie, in its first
// spawner's axes as placed (the spawner moves with the crate, the target with it). The item's box center as FitBox
// takes it (its own option's, the option its rest was learned from, its middle measured this load, its option
// template's), else the slot's box middle, where the centered items' boxes go (fr::FitSpawner), else the spawner's
// origin.
void CrateGuns::CenterTarget(size_t i, const fr::Guid& p_Item, std::optional<fr::WorldPoint> p_BoxMiddle) {
    if (i >= m_Options.size())
        return;

    Option& s_Option = m_Options[i];
    s_Option.centerTarget.reset();
    s_Option.centerFromBox = false;
    s_Option.centerFrom.clear();
    const std::optional<fr::SpatialTransform> s_Vanilla =
        s_Option.spawners.empty() ? std::nullopt : SpawnerVanilla(i, 0);

    if (!s_Vanilla)
        return;

    const fr::SpatialTransform s_Placed =
        fr::ApplySpawnerTurn(*s_Vanilla, s_Option.spawnerTurns.empty() ? fr::SpawnerTurn {} : s_Option.spawnerTurns[0]);
    std::string s_From;
    const std::optional<size_t> s_Own =
        s_Option.item == std::optional<fr::Guid>(p_Item) ? std::optional<size_t>(i) : std::nullopt;

    if (const std::optional<std::array<double, 3>> s_Center = ItemBoxCenter(p_Item, s_Own, &s_From)) {
        s_Option.centerTarget = s_Center;
        s_Option.centerFromBox = s_From != kMeasuredFrom;
        s_Option.centerFrom = s_From;
    }
    else if (p_BoxMiddle) {
        s_Option.centerTarget = fr::PointInSpawner(s_Placed, *p_BoxMiddle);
        s_Option.centerFrom = "no box: the slot's box middle";
    }
    else {
        s_Option.centerTarget = std::array<double, 3> {};
        s_Option.centerFrom = "no box: its spawner's origin";
    }
}

std::optional<std::array<double, 3>> CrateGuns::MeasuredCenter(const fr::Guid& p_Item) const {
    for (const auto& [s_Item, s_Center] : m_Measured) {
        if (s_Item == p_Item)
            return s_Center;
    }

    return std::nullopt;
}

std::optional<fr::LocalAxis> CrateGuns::MeasuredRest(const fr::Guid& p_Item) const {
    for (const auto& [s_Item, s_Axis] : m_MeasuredRests) {
        if (s_Item == p_Item)
            return s_Axis;
    }

    return std::nullopt;
}

// Under m_Mutex, game thread, each frame of an open (crate_items; transforms refreshed through the engine, every read
// and write guarded). An item's runtime template may lay its model by a placement helper off its origin, so it lies
// off its spawner (fr::CheckItemCenter). Each offered item of a tool slot is measured once, kCenterDelaySeconds after it spawned:
// its model's bounds (ReadGeomBounds) by its world transform, against its option's center target from its spawner
// now. One off by more than the slack along the floor is moved by the difference (WriteSpawnerTransform: its
// m_mTransform under its parent, read back), and its selector box with it when crate_even_boxes placed that. Its
// measured middle is kept for its next placement this load when it had no box or its box missed. One still unread
// after kCenterGiveUpSeconds is left as it lies; every one is logged.
void CrateGuns::CenterOffers() {
    if (!m_Open || !m_Open->crate)
        return;

    const Clock::time_point s_Now = Clock::now();
    const size_t s_Count = std::min(
        {m_Open->offers.size(), m_Open->offerEntities.size(), m_Open->offerAt.size(), m_Open->offerCentered.size()});

    for (size_t k = 0; k < s_Count; ++k) {
        if (m_Open->offerCentered[k])
            continue;

        const size_t i = m_Open->offers[k];
        const std::optional<size_t> s_Group = i < m_Options.size() ? GroupOf(i) : std::nullopt;

        // A tool slot's item the mod placed (the one-item option's lies as the game lays it).
        if (!s_Group || *s_Group >= fr::kCrateOneItemGroup || !m_Options[i].centerTarget ||
            m_Options[i].spawners.empty()) {
            m_Open->offerCentered[k] = 1;
            continue;
        }

        const double s_Since = std::chrono::duration<double>(s_Now - m_Open->offerAt[k]).count();

        if (s_Since < kCenterDelaySeconds)
            continue;

        Option& s_Option = m_Options[i];
        ZEntityType** s_Entity = m_Open->offerEntities[k];
        const std::optional<fr::Guid> s_Written = OptionItemNow(i);
        const std::string s_Name = s_Written ? ItemText(*s_Written) : std::string("?");
        const GeomBoundsRead s_Bounds = ReadGeomBounds(s_Entity);
        const std::optional<fr::SpatialTransform> s_ItemAt = ReadSpatial(s_Entity);
        const std::optional<fr::SpatialTransform> s_SpawnerAt = ReadSpatial(s_Option.spawners[0]);
        const std::string s_Unread =
            s_Bounds.faultCode ? "its bounds " + FaultText(s_Bounds.faultCode)
            : !s_Bounds.geom   ? std::string("not a ZGeomEntity")
            : !s_Bounds.loaded ? std::string("its model not loaded")
            : !s_Bounds.bounds
                ? fmt::format("its model's resource is not a primitive (type {:08x})", s_Bounds.resourceType)
            : !s_ItemAt    ? std::string("its transform unread")
            : !s_SpawnerAt ? std::string("its spawner's transform unread")
                           : std::string();

        if (!s_Unread.empty()) {
            if (s_Since < kCenterGiveUpSeconds)
                continue; // the next frame

            m_Open->offerCentered[k] = 1;
            LogTo(fr::LogTopic::SupplierCrates,
                  "{}: crate open #{} centering: slot {}, option {:016x}: {} not measured ({}); "
                  "left as it lies",
                  kItemsTag, m_Open->index + 1, *s_Group + 1, s_Option.id, s_Name, s_Unread);
            continue;
        }

        m_Open->offerCentered[k] = 1;
        const std::array<float, 6>& s_Box = *s_Bounds.bounds;
        const fr::ItemBounds s_Model {{s_Box[0], s_Box[1], s_Box[2]}, {s_Box[3], s_Box[4], s_Box[5]}};
        const fr::WorldPoint s_Target = fr::PointFromSpawner(*s_SpawnerAt, *s_Option.centerTarget);
        fr::SpatialTransform s_ItemNow = *s_ItemAt;
        std::string s_Done;

        // A guessed rest (no option of its own in the crate, no twin: the table, its box or +Z) that leaves the item
        // off its widest face (an item standing on its end): laid on its widest face where it lies, so it is seen
        // whole from above, and its
        // spawners turned for it at its next placement this load. The game's own placements are left.
        const bool s_Guessed = s_Option.restSource == fr::CrateRestSource::Table ||
                               s_Option.restSource == fr::CrateRestSource::Box ||
                               s_Option.restSource == fr::CrateRestSource::Default;
        const std::optional<fr::LocalAxis> s_FlatAxis =
            s_Guessed && s_Written ? fr::MeasuredRestAxis(*s_ItemAt, s_Model, *s_SpawnerAt) : std::nullopt;

        if (s_FlatAxis) {
            if (const std::optional<fr::SpatialTransform> s_Flat = fr::LayItemFlat(*s_ItemAt, s_Model)) {
                std::string s_How;

                if (WriteSpawnerTransform(s_Entity, *s_Flat, &s_How)) {
                    s_ItemNow = *s_Flat;
                    s_Done =
                        fmt::format("; it stood upright: laid on its widest face ({}), its spawner{} to lie {} "
                                    "up at its next placement this load",
                                    s_How, s_Option.spawners.size() == 1 ? "" : "s", fr::LocalAxisText(*s_FlatAxis));
                }
                else {
                    s_Done =
                        fmt::format("; it stands upright: NOT laid flat ({}); its spawner{} to lie {} up at its "
                                    "next placement this load",
                                    s_How, s_Option.spawners.size() == 1 ? "" : "s", fr::LocalAxisText(*s_FlatAxis));
                }

                std::erase_if(m_MeasuredRests, [&](const auto& m) { return m.first == *s_Written; });
                m_MeasuredRests.emplace_back(*s_Written, *s_FlatAxis);
                std::erase_if(m_Measured, [&](const auto& m) { return m.first == *s_Written; });
            }
        }

        const fr::CrateCenterCheck s_Check =
            fr::CheckItemCenter(s_ItemNow, s_Model, *s_SpawnerAt, s_Target,
                                s_Option.centerFromBox ? fr::kCrateCenterBoxSlackMeters : fr::kCrateCenterSlackMeters);
        fr::WorldPoint s_Middle = s_Check.center; // where its middle lies now
        bool s_Moved = false;

        if (s_Check.verdict == fr::CrateCentering::Moved) {
            fr::SpatialTransform s_Want = s_ItemNow;
            s_Want.at[0] += static_cast<float>(s_Check.shift[0]);
            s_Want.at[1] += static_cast<float>(s_Check.shift[1]);
            std::string s_How;
            s_Moved = WriteSpawnerTransform(s_Entity, s_Want, &s_How);
            s_Done = (s_Moved ? fmt::format(" ({})", s_How) : fmt::format(" - NOT taken ({})", s_How)) + s_Done;

            if (s_Moved) {
                s_Middle[0] += s_Check.shift[0];
                s_Middle[1] += s_Check.shift[1];
            }
        }

        // Its selector box (crate_even_boxes put it where the item was meant to lie, FitBox; with no box, at its
        // spawner's origin) goes on its middle along the floor, when it was moved or had no box.
        const bool s_Trusted =
            s_Check.verdict == fr::CrateCentering::Moved || s_Check.verdict == fr::CrateCentering::Centered;

        if (s_Trusted && (s_Moved || !s_Option.centerFromBox) && m_Items.evenBoxes && s_Option.box &&
            s_Option.boxWritten) {
            if (const std::optional<fr::SpatialTransform> s_BoxAt = ReadSpatial(s_Option.box)) {
                fr::SpatialTransform s_BoxWant = *s_BoxAt;
                s_BoxWant.at[0] = static_cast<float>(s_Middle[0]);
                s_BoxWant.at[1] = static_cast<float>(s_Middle[1]);
                std::string s_BoxHow;

                if (!fr::SameTransform(*s_BoxAt, s_BoxWant))
                    s_Done += WriteSpawnerTransform(s_Option.box, s_BoxWant, &s_BoxHow)
                                  ? std::string("; its selector box onto it")
                                  : "; its selector box NOT moved (" + s_BoxHow + ")";
            }
            else {
                s_Done += "; its selector box unread (left)";
            }
        }

        // Kept for its next placement when it had no box, or its box missed (not when laid flat: its spawner turns
        // at the next placement, and its middle is measured again then).
        if (s_Written && s_Trusted && !s_FlatAxis &&
            (!s_Option.centerFromBox || s_Check.verdict == fr::CrateCentering::Moved)) {
            std::erase_if(m_Measured, [&](const auto& m) { return m.first == *s_Written; });
            m_Measured.emplace_back(*s_Written, s_Check.centerLocal);
            s_Done += "; its middle kept for its next placement";
        }

        LogTo(fr::LogTopic::SupplierCrates,
              "{}: crate open #{} centering: slot {}, option {:016x}: {}: {} (target: {}){}", kItemsTag,
              m_Open->index + 1, *s_Group + 1, s_Option.id, s_Name, fr::DescribeCenterCheck(s_Check),
              s_Option.centerFrom, s_Done);
    }
}

void CrateGuns::Tick() noexcept {
    if (!On() || !m_Armed.load(std::memory_order_relaxed) || Reentered(Hook::Frame))
        return;

    Contained(kTag, "frame", [&] {
        const HeldLock s_Lock(m_Mutex);

        if (m_Open) {
            const double s_Since = std::chrono::duration<double>(Clock::now() - m_Open->at).count();

            if (!m_Open->reported && s_Since >= kReportSeconds)
                Report("report window over");

            if (ItemsOn())
                CenterOffers();
        }

        // The open fallback: the crate seed changed without its pin being seen.
        int32_t s_Seed = 0;

        if (m_SeedAddress && !ReadEngineBytes(m_SeedAddress, &s_Seed, sizeof(s_Seed)) && s_Seed != m_LastSeed)
            BeginOpen(s_Seed, "crate seed poll", OpenSeen::Poll);
    });
}

void CrateGuns::OnSceneClear() noexcept {
    if (On() && !Reentered(Hook::SceneClear))
        Contained(kTag, "scene clear", [&] { ClearScene(); });
}

// From the ClearScene detour (before the game clears the scene) and at a new
// load's assets loaded (a restart may come without a ClearScene).
void CrateGuns::ClearScene() {
    {
        m_Armed.store(false, std::memory_order_relaxed);
        m_Window.store(false, std::memory_order_relaxed);
        const HeldLock s_Lock(m_Mutex);

        if (m_Open && !m_Open->reported)
            Report("scene clear");

        // Crate_items' keys stay as they are: the scene's entities go with it.

        if (m_StashItems) {
            LogTo(fr::LogTopic::SupplierCrates,
                  "{} summary {} ({}): {} crate opens, {} rolls hit (weapon pool forced), {} hits left vanilla (you "
                  "own every gun a crate can offer), the game took the weapon pool {} times and the tool pool {} "
                  "times, {} "
                  "steers missed; {} offers seen, {} firearms{}; hooks skipped while the mod wrote (re-entered): {}",
                  kTag, ShortSceneName(m_Scene), SeedText(), m_Opens, m_ForcedWeapon, m_HitsNoGun, m_TookWeapon,
                  m_TookTool, m_Missed, m_Offers, m_GunOffers,
                  m_OfferLog.empty() ? std::string() : fmt::format(": {}", m_OfferLog), ReentriesText());

            if (Managed())
                LogTo(fr::LogTopic::SupplierCrates,
                      "{} summary {}: {} crate opens of {} crates, {} new crates seen too late to write their "
                      "plan; {} options still hold an item in a crate; {} guns and large items planned; {} resources "
                      "held for "
                      "items the map had not loaded",
                      kItemsTag, ShortSceneName(m_Scene), m_Opens, m_Crates.size(), m_LateOpens,
                      std::count_if(m_Options.begin(), m_Options.end(),
                                    [](const Option& o) { return o.heldBy.has_value(); }),
                      m_Planned.size(), m_Held.size());
        }

        Forget();
    }
}

bool CrateGuns::Reentered(Hook p_Hook) {
    if (!t_HoldingLock)
        return false;

    m_Reentries[static_cast<size_t>(p_Hook)].fetch_add(1, std::memory_order_relaxed);
    return true;
}

// "none", or "output pin 3, activate entity 1" for the summary.
std::string CrateGuns::ReentriesText() const {
    static constexpr std::array<const char*, kHooks> kNames = {"loading stage", "frame",      "scene clear",
                                                               "event",         "output pin", "activate entity"};
    std::string s_Text;

    for (size_t i = 0; i < kHooks; ++i) {
        if (const uint32_t s_Count = m_Reentries[i].load(std::memory_order_relaxed))
            s_Text += fmt::format("{}{} {}", s_Text.empty() ? "" : ", ", kNames[i], s_Count);
    }

    return s_Text.empty() ? std::string("none") : s_Text;
}

void CrateGuns::Forget() {
    for (std::atomic<uint32_t>& s_Count : m_Reentries)
        s_Count.store(0, std::memory_order_relaxed);

    m_Watch.Clear();
    m_Pending = false;
    m_Scene.clear();
    m_Seed.reset();
    m_StashItems = nullptr;
    m_StashItemsBrick = 0;
    m_SeedEntity = nullptr;
    m_SeedAddress = 0;
    m_JunkChoice = nullptr;
    m_WeaponChoice = nullptr;
    m_JunkAddress = 0;
    m_WeaponAddress = 0;
    m_VanillaJunk = fr::kVanillaJunkBranchWeight;
    m_VanillaWeapon = fr::kVanillaWeaponBranchWeight;
    m_Options.clear();
    m_OptionByRoot.clear();
    m_Guns.clear();
    m_LastSeed = 0;
    m_Opens = 0;
    m_Open.reset();
    m_BranchFor.reset();
    m_ForcedWeapon = 0;
    m_HitsNoGun = 0;
    m_TookWeapon = 0;
    m_TookTool = 0;
    m_Missed = 0;
    m_GunOffers = 0;
    m_Offers = 0;
    m_LateOpens = 0;
    m_OfferLog.clear();
    m_Selectors.clear();
    m_SceneOptions.clear();
    m_LevelSpawners.clear();
    m_LevelSpawnersOver = 0;
    m_SlotOptions.clear();
    m_SingleOptions.clear();
    m_Keys.clear();
    m_Candidates.clear();
    m_Planned.clear();
    m_Rests.clear();
    m_Measured.clear();
    m_MeasuredRests.clear();
    m_BoxSize.reset();
    m_BoxSizeRead = false;
    m_Next.reset();
    m_Crates.clear();
    m_LastCrate.reset();
    ReleaseHeld();
    m_PickupWatch.store(false, std::memory_order_release);
}

void CrateGuns::OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept {
    if (!PickupsWanted() || Reentered(Hook::Event))
        return;

    Contained(kTag, "event", [&] {
        std::string s_Json;

        if (ReadDynamicObjectJson(p_Event, &s_Json))
            return;

        if (fr::JsonEventName(s_Json) != "ItemPickedUp")
            return;

        const std::optional<std::string_view> s_IdText = fr::JsonStringField(s_Json, "RepositoryId");
        const std::optional<fr::Guid> s_Id = s_IdText ? fr::ParseGuid(*s_IdText) : std::nullopt;
        const std::string s_Name(fr::JsonStringField(s_Json, "ItemName").value_or(""));
        const HeldLock s_Lock(m_Mutex);

        // A pickup of an item a crate holds: its option is free again.
        if (s_Id)
            CratePickup(p_EventIndex, s_Name, *s_Id);
    });
}

void CrateGuns::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept {
    if (!p_Entity.m_pObj || m_Watch.Empty() || Reentered(Hook::OutputPin))
        return;

    Contained(kTag, "output pin", [&] { Pin(p_Entity, p_PinId, p_Data); });
}

// An output of a watched entity: the crate seed (an open), an option's state
// (its pool activating) or an option's weight math (its weight on the way to
// its choice).
void CrateGuns::Pin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) {
    const std::optional<uint32_t> s_Value = m_Watch.Lookup(p_Entity.m_pObj);

    if (!s_Value)
        return;

    const uint32_t s_Role = *s_Value & 3;
    const size_t s_Index = *s_Value >> 2;

    if (s_Role == kRoleCleared)
        return;

    PinPayload s_Payload;

    if (ReadPinPayload(p_Data, &s_Payload) || !s_Payload.data)
        return;

    const void* s_Int32 = m_Int32Type.load(std::memory_order_acquire);
    const void* s_Float32 = m_Float32Type.load(std::memory_order_acquire);
    const HeldLock s_Lock(m_Mutex);

    if (!m_Armed.load(std::memory_order_relaxed))
        return;

    if (s_Role == kRoleSeed) {
        int32_t s_Seed = 0;

        if (s_Payload.type != s_Int32 || ReadEngineBytes(reinterpret_cast<uintptr_t>(s_Payload.data), &s_Seed, 4))
            return;

        if (s_Seed != m_LastSeed)
            BeginOpen(s_Seed, fmt::format("crate seed output pin {:08x}", p_PinId).c_str(), OpenSeen::Pin);

        return;
    }

    if (s_Index >= m_Options.size())
        return;

    Option& s_Option = m_Options[s_Index];

    if (s_Role == kRoleState) {
        int32_t s_State = 0;

        if (s_Payload.type != s_Int32 || ReadEngineBytes(reinterpret_cast<uintptr_t>(s_Payload.data), &s_State, 4) ||
            s_State == 0)
            return;

        // An activation with no open running (the seed was not seen yet this frame).
        if (!m_Open || m_Open->reported) {
            int32_t s_Seed = m_LastSeed;

            if (m_SeedAddress)
                ReadEngineBytes(m_SeedAddress, &s_Seed, sizeof(s_Seed));

            BeginOpen(s_Seed, "a crate option activating", OpenSeen::Activation);
        }

        if (s_Option.state0 == 0)
            ++m_Open->pools[static_cast<size_t>(fr::CratePoolOfState(s_State))];

        s_Option.state0 = s_State;

        if (s_Option.gun && m_Open->draw.weaponPool)
            Balance(m_Open->balanced ? "a firearm activating" : "the first firearm activating");

        return;
    }

    // Weight math output: float32 on its way to the option's choice.
    if (s_Role != kRoleMath || s_Payload.type != s_Float32 || !m_Open || m_Open->reported || !m_Open->draw.weaponPool)
        return;

    const auto s_Address = reinterpret_cast<uintptr_t>(s_Payload.data);

    if (s_Option.gun) {
        if (!m_Open->balanced)
            Balance("a firearm's weight math output");

        const auto s_Target = static_cast<float>(s_Option.target);

        if (s_Option.target >= 0.0 && !WriteEngineBytes(s_Address, &s_Target, sizeof(s_Target)))
            ++m_Open->payloadRewrites;

        return;
    }

    if (fr::CratePoolOfState(s_Option.state0) != fr::CratePool::Weapon)
        return;

    float s_Weight = 0.0f;

    if (ReadEngineBytes(s_Address, &s_Weight, sizeof(s_Weight)))
        return;

    if (m_Config.gunShare >= 1.0) {
        const float s_Zero = 0.0f;

        if (s_Weight != 0.0f && !WriteEngineBytes(s_Address, &s_Zero, sizeof(s_Zero))) {
            ++m_Open->otherZeroed;
            WriteFloat(s_Option.weightAddress, 0.0f);
        }

        s_Option.observed = 0.0;
        return;
    }

    s_Option.observed = std::max(0.0, static_cast<double>(s_Weight));
    bool s_Observed = false;

    if (m_Open->balanced && std::fabs(OtherWeight(&s_Observed) - m_Open->otherUsed) > 1e-4)
        Balance(fmt::format("rebalanced: {} got {:g} in the weapon pool", s_Option.name, s_Weight).c_str());
}

void CrateGuns::OnActivateEntity(const ZEntityRef* p_Entity) noexcept {
    if (!p_Entity || !p_Entity->m_pObj || !m_Window.load(std::memory_order_relaxed) || Reentered(Hook::Activate))
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

        if (s_Chain.empty())
            return;

        uint64_t s_Rid = 0;
        ReadBlueprintRid(p_Entity->m_pObj, &s_Rid);
        const HeldLock s_Lock(m_Mutex);

        if (!m_Open || m_Open->reported)
            return;

        for (ZEntityType** s_Parent : s_Chain) {
            const auto s_Found = m_OptionByRoot.find(s_Parent);

            if (s_Found == m_OptionByRoot.end())
                continue;

            if (std::find(m_Open->offers.begin(), m_Open->offers.end(), s_Found->second) != m_Open->offers.end())
                return;

            m_Open->offers.push_back(s_Found->second);
            m_Open->offerBlueprints.push_back(s_Rid);
            m_Open->offerEntities.push_back(p_Entity->m_pObj);
            m_Open->offerAt.push_back(Clock::now());
            m_Open->offerCentered.push_back(0);
            LogTo(fr::LogTopic::SupplierCrates, "{}: crate open #{} offer: {} (spawned {:016X})", kTag,
                  m_Open->index + 1, OptionText(s_Found->second), s_Rid);

            // Its item now sits in this crate: the option is not written again until that item is picked up.
            if (Managed() && m_Open->crate && GroupOf(s_Found->second)) {
                Option& s_Option = m_Options[s_Found->second];
                s_Option.heldBy = m_Open->crate;
                s_Option.heldItem = OptionItemNow(s_Found->second).value_or(fr::Guid {});
                m_PickupWatch.store(true, std::memory_order_release);
            }

            return;
        }
    });
}

// Under m_Mutex.
void CrateGuns::Watch(const void* p_Entity, size_t p_Option, uint32_t p_Role) {
    if (!m_Watch.Watch(p_Entity, static_cast<uint32_t>(p_Option << 2) | p_Role))
        LogTo(fr::LogTopic::SupplierCrates, "{}: watch table full; an entity's pins are not seen", kTag);
}
