#pragma once

#include "Guard.h"
#include "Guid.h"
#include "ModLog.h"

#include <Glacier/CompileReflection.h>
#include <Glacier/Hash.h>
#include <Glacier/TArray.h>
#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>
#include <Glacier/ZTypeRegistry.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// One ZDifficultyParameterEntity found in the loaded bricks. A field is empty
// when the property is missing or has an unexpected type.
struct FoundParam {
    ZEntityRef entity;
    std::optional<int32_t> param; // EDifficultyParameter
    std::optional<float> value;
    std::optional<int32_t> count;
    std::optional<bool> toggle;
    std::optional<fr::Guid> repoId; // m_RepositoryId (ZRepositoryKeyEntity), read raw
    const char* matchedBy;          // "interface", "class name" or "interface and class name"
    uint64_t brickRid;
};

struct BrickWalkStats {
    uint64_t brickRid;
    size_t visited;
    size_t matched;
    const char* faultStep; // the guarded step that faulted, or nullptr
};

struct WalkStats {
    size_t visited;
    size_t matched;
    bool capped;
    size_t faults;
    std::vector<BrickWalkStats> bricks;
};

// Entities one walk visits at most.
constexpr size_t kWalkEntityCap = 2'000'000;

// The cap of a walk over a whole mission (the largest missions exceed
// kWalkEntityCap).
constexpr size_t kWideWalkEntityCap = 6'000'000;

// Breadth-first walk of the loaded bricks' entity trees (the Editor's walk)
// collecting difficulty-parameter entities. With p_OnlyBricks, only loaded
// bricks whose runtime resource ID is listed are walked; without it, every
// loaded brick. Raw-pointer steps are guarded; a fault aborts that brick's walk
// only. Stops after p_Cap entities. p_VisitedReserve slots are reserved in the
// visited set up front (fr::BrickMemory::VisitedEstimate), so a large walk
// does not rehash it over and over.
WalkStats FindDifficultyParams(const TArray<SBrickAllocationInfo>& p_Bricks, std::vector<FoundParam>& p_Out,
                               const std::vector<uint64_t>* p_OnlyBricks = nullptr, size_t p_VisitedReserve = 0,
                               size_t p_Cap = kWalkEntityCap);

// One entity of a requested class found by FindEntitiesOfClass.
struct ClassEntity {
    ZEntityType** entity;
    uint64_t brickRid;
};

// The same guarded walk, over the listed loaded bricks only, collecting every
// entity whose concrete class name is p_ClassName.
WalkStats FindEntitiesOfClass(const TArray<SBrickAllocationInfo>& p_Bricks, const std::vector<uint64_t>& p_OnlyBricks,
                              const char* p_ClassName, std::vector<ClassEntity>& p_Out);

// One entity the walk reaches: the entity, its type, the blueprint factory it
// is sub-entity p_Index of (with that factory's entity, its parent), the
// loaded brick it was reached from, and its own blueprint factory (null for
// none; for a template instance, the template's blueprint).
struct VisitedEntity {
    ZEntityType** entity;
    ZEntityType* type;
    ZEntityBlueprintFactoryBase* parentFactory;
    int index;
    ZEntityType** parent;
    uint64_t brickRid;
    ZEntityBlueprintFactoryBase* factory;
};

// Called once per distinct entity; returns true to count it as matched.
using EntityVisitor = std::function<bool(const VisitedEntity&)>;

// Called after each brick a walk finished (not after one the cap cut off)
// with the number of bricks walked so far; returns true to stop the walk.
using BrickDoneCheck = std::function<bool(size_t)>;

// The same guarded walk over the loaded bricks at the indices p_Order lists,
// in that order (an index past the list is skipped), calling p_Visitor for
// each entity (the merces payout table search). Stops after p_Cap entities,
// or when p_Done (if set) says so. The visitor's own engine reads must be
// guarded.
WalkStats VisitEntitiesInOrder(const TArray<SBrickAllocationInfo>& p_Bricks, std::span<const size_t> p_Order,
                               const EntityVisitor& p_Visitor, size_t p_Cap, const BrickDoneCheck& p_Done,
                               size_t p_VisitedReserve = 0);

// VisitEntitiesInOrder over every loaded brick in load order, stopping after
// kWideWalkEntityCap entities.
WalkStats VisitAllEntities(const TArray<SBrickAllocationInfo>& p_Bricks, const EntityVisitor& p_Visitor,
                           size_t p_VisitedReserve = 0);

// The same guarded walk over the entity tree under p_Root (its blueprint
// factory's sub-entities, recursively; p_Root itself is not visited), e.g. a
// runtime-spawned template instance. p_Label stands in for the brick id.
WalkStats VisitEntityTree(ZEntityType** p_Root, uint64_t p_Label, const EntityVisitor& p_Visitor, size_t p_Cap);

// One sub-entity looked up by its sub-entity id, as the walk reaches it: in
// p_ParentFactory (the factory p_Parent's sub-entities come from) or, when
// that is null, in p_Parent's own blueprint factory (a brick or template
// instance root). A nested template instance (e.g. a card picker in the
// safehouse brick) has no blueprint factory of its own (GetBlueprintFactory
// is null for it); its sub-entities are reached through `factory` below,
// with the entity itself as the root. Read guarded.
struct SubEntityLookup {
    ZEntityType** entity = nullptr;                 // null when not found
    ZEntityBlueprintFactoryBase* factory = nullptr; // the sub-entity's own factory (GetSubEntityBlueprint), may be null
    ZEntityBlueprintFactoryBase* parentFactory = nullptr; // the factory it was looked up in
    int index = -1;                                       // its index there, -1 when not found
    const char* faultStep = nullptr;                      // the guarded step that faulted
    uint32_t faultCode = 0;
};

SubEntityLookup LookUpSubEntity(ZEntityType** p_Parent, ZEntityBlueprintFactoryBase* p_ParentFactory, uint64_t p_Id);

// The number of sub-entities of a factory, and sub-entity p_Index of it under
// p_Parent with its own factory (diagnostics), read guarded. Return 0, or the
// fault code.
uint32_t ReadSubEntityCount(ZEntityBlueprintFactoryBase* p_Factory, int64_t* p_Count);
uint32_t ReadSubEntityAt(ZEntityBlueprintFactoryBase* p_Factory, ZEntityType** p_Parent, int p_Index,
                         ZEntityType*** p_Entity, ZEntityBlueprintFactoryBase** p_SubFactory);

// The entity's own blueprint factory (GetBlueprintFactory), read guarded.
// Returns 0, or the fault code.
uint32_t ReadOwnBlueprintFactory(ZEntityType** p_Entity, ZEntityBlueprintFactoryBase** p_Factory);

// The runtime resource id of the entity's own blueprint factory (for a
// template instance root, the template's TBLU), read guarded: 0 in *p_Rid
// when it has none. Returns 0, or the fault code.
uint32_t ReadBlueprintRid(ZEntityType** p_Entity, uint64_t* p_Rid);

// The runtime resource id of a blueprint factory (m_ridResource), read
// guarded. Returns 0, or the fault code.
uint32_t ReadFactoryRid(ZEntityBlueprintFactoryBase* p_Factory, uint64_t* p_Rid);

// Where one property is stored, as GetProperty locates it (the entity type's
// SPropertyData for the property, at the entity pointer plus its offset), read
// guarded. Engine type names are copied out.
struct PropertyLocation {
    enum class Status {
        Ok,
        Missing,   // the entity has no such property
        HasGetter, // read through a getter, so the storage is not the value
        Fault,     // locating it faulted
    };

    Status status;
    uintptr_t address;    // when Ok
    uint16_t typeSize;    // 0 when the type is unknown
    bool isArray;         // a container type
    uint16_t elementSize; // arrays: the element type's size, 0 when unknown
    char typeName[96];
    char elementTypeName[96]; // arrays
    uint32_t faultCode;       // when Fault
};

PropertyLocation LocateProperty(ZEntityType** p_Entity, std::string_view p_Name);
// The same by property id (the name's CRC-32), for properties whose names are unknown.
PropertyLocation LocatePropertyById(ZEntityType** p_Entity, uint32_t p_Id);

// Copies p_Size bytes of engine memory at p_Address, guarded. Returns 0, or
// the structured-exception code of a fault (also for a null address).
uint32_t ReadEngineBytes(uintptr_t p_Address, void* p_Out, size_t p_Size);

// The entity's concrete class name (empty without type or interface data) and
// sub-entity id, read guarded. Returns 0, or the fault code.
uint32_t ReadEntityClassName(ZEntityType** p_Entity, std::string& p_Name, uint64_t* p_Id);

// "<id> (<class>)" of an entity (EntityIdText), "<none>" for null, or the
// fault of the read.
std::string EntityText(ZEntityType** p_Entity);

// The storage address of a raw 4-byte property whose engine type is p_Type
// ("float32", "int32"), or 0 (*p_Why says why).
uintptr_t Raw4Address(ZEntityType** p_Entity, std::string_view p_Name, const char* p_Type, std::string* p_Why);

// The campaign seed from the evergreen_setup brick's seed entity, or empty
// (reason logged) if the brick, the entity, or the property is not readable.
std::optional<int32_t> ReadLiveSeed(const TArray<SBrickAllocationInfo>& p_Bricks);

// The sub-entities of the evergreen_setup brick with these ids (p_Out[i] for
// p_Ids[i], null when not found), read guarded. Returns empty, or why
// nothing could be looked up (brick not loaded, fault).
std::string FindSetupBrickEntities(const TArray<SBrickAllocationInfo>& p_Bricks, std::span<const uint64_t> p_Ids,
                                   std::vector<ZEntityType**>& p_Out);

// Result of ReadPropertyRaw32.
struct RawRead32 {
    enum class Status {
        Ok,
        Missing, // the entity has no such property
        NotRaw,  // the property has a getter or its engine type is not 4 bytes
        Fault,   // reading faulted
    };

    Status status;
    uint32_t bits;      // the property's 4 bytes when Ok
    uint16_t typeSize;  // the property's engine type size when Ok or NotRaw
    uint32_t faultCode; // the structured-exception code when Fault
};

// Reads a 4-byte property's storage directly, guarded against faults: the
// entity type's SPropertyData for the property (as GetProperty finds it), at
// the entity pointer plus its offset. Calls no engine getter or allocator.
RawRead32 ReadPropertyRaw32(const ZEntityRef& p_Entity, std::string_view p_Name);

// The entity's m_RepositoryId (ZRepositoryKeyEntity layout, as in the SDK's
// ZRepository.h), read guarded; empty (fault code in *p_FaultCode) on a fault.
std::optional<fr::Guid> ReadRepositoryId(const ZEntityRef& p_Entity, uint32_t* p_FaultCode);

// What the pin hook needs to know about an entity, read guarded.
struct EntityClassCheck {
    bool hasType; // the entity has a type
    bool matches; // its concrete class name equals the requested one
    uint64_t id;  // its sub-entity id (m_nEntityID), when hasType
};

// Returns 0, or the structured-exception code on a fault. Allocates nothing.
uint32_t CheckEntityClass(ZEntityType** p_Entity, const char* p_ClassName, EntityClassCheck* p_Out);

// True when the engine's type registry knows the type; a missing type is
// logged once per type name per session.
bool IsTypeRegistered(std::string_view p_TypeName);

// Logs a property whose engine type differs from the requested one, once per
// (property, type) pair per session.
void LogPropertyTypeMismatch(std::string_view p_Name, std::string_view p_Expected, const char* p_Actual);

// Logs a guarded property read that faulted, once per property per session.
void LogPropertyFault(std::string_view p_Name, uint32_t p_Code);

namespace detail {

// One property read through the engine (ReadProperty's steps).
template <class T>
struct PropertyRead {
    bool found = false; // the entity's type has the property
    bool read = false;  // its value is a T
    T value {};
    const char* actual = nullptr; // the engine type read instead, when not a T
};

// Raw-pointer steps follow the guard pattern in Guard.h. p_Get false: only
// whether the type has the property.
template <class T>
__declspec(noinline) void PropertyOf(ZEntityType** p_Entity, uint32_t p_Id, bool p_Get, PropertyRead<T>* p_Out) {
    const ZEntityRef s_Entity(p_Entity);
    const auto* s_Type = s_Entity->GetType();

    if (!s_Type || !s_Type->FindProperty(p_Id))
        return;

    p_Out->found = true;

    if (!p_Get)
        return;

    auto s_Variant = s_Entity.GetProperty<T>(p_Id);

    if (const T* s_Value = s_Variant.template As<T>()) {
        p_Out->read = true;
        p_Out->value = *s_Value;
        return;
    }

    const auto* s_TypeId = s_Variant.GetTypeID();
    const auto* s_TypeInfo = s_TypeId ? s_TypeId->GetTypeInfo() : nullptr;
    p_Out->actual = s_TypeInfo ? s_TypeInfo->pszTypeName : nullptr;
}

template <class T>
__declspec(noinline) DWORD SafeProperty(ZEntityType** p_Entity, uint32_t p_Id, bool p_Get, PropertyRead<T>* p_Out) {
    DWORD s_Code = 0;

    __try {
        PropertyOf(p_Entity, p_Id, p_Get, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace detail

// Reads a property as T, guarded. Empty when the entity has no such property,
// when the engine does not know T (logged), when the property's engine type is
// not T (logged) or when the read faults (logged). Never calls ZVariant::Get(),
// which dereferences null on a type mismatch.
template <class T>
std::optional<T> ReadProperty(const ZEntityRef& p_Entity, std::string_view p_Name) {
    if (!p_Entity)
        return std::nullopt;

    const uint32_t s_PropertyId = Hash::Crc32(p_Name.data(), p_Name.size());
    detail::PropertyRead<T> s_Read;

    if (const DWORD s_Code = detail::SafeProperty(p_Entity.m_pObj, s_PropertyId, false, &s_Read)) {
        LogPropertyFault(p_Name, s_Code);
        return std::nullopt;
    }

    // GetProperty<T> default-constructs a ZVariant<T>, which needs T in the
    // engine's type registry.
    if (!s_Read.found || !IsTypeRegistered(ZHMTypeName<T>))
        return std::nullopt;

    if (const DWORD s_Code = detail::SafeProperty(p_Entity.m_pObj, s_PropertyId, true, &s_Read)) {
        LogPropertyFault(p_Name, s_Code);
        return std::nullopt;
    }

    if (s_Read.read)
        return s_Read.value;

    LogPropertyTypeMismatch(p_Name, ZHMTypeName<T>, s_Read.actual);
    return std::nullopt;
}
