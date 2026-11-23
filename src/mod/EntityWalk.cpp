#include "EntityWalk.h"

#include "EngineText.h"
#include "GameIds.h"
#include "Guard.h"
#include "LogTopics.h"

#include <Glacier/Enums.h>
#include <Glacier/EntityFactory.h>
#include <Glacier/Reflection.h>
#include <Glacier/ZRepository.h>

#include <Globals.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <functional>
#include <queue>
#include <set>
#include <unordered_set>
#include <utility>

static_assert(ZHMTypeName<EDifficultyParameter> == "EDifficultyParameter",
              "m_eParameter is read as EDifficultyParameter; its engine type name must match");

namespace {

constexpr size_t kMaxEntitiesPerScene = kWalkEntityCap;
constexpr const char* kDifficultyEntityClass = "ZDifficultyParameterEntity";

// Raw-pointer steps follow the guard pattern in Guard.h.

__declspec(noinline) void SubEntitiesCountOf(ZEntityBlueprintFactoryBase* p_Factory, int64_t* p_Out) {
    *p_Out = p_Factory->GetSubEntitiesCount();
}

__declspec(noinline) DWORD SafeSubEntitiesCount(ZEntityBlueprintFactoryBase* p_Factory, int64_t* p_Out) {
    DWORD s_Code = 0;

    __try {
        SubEntitiesCountOf(p_Factory, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void SubEntityOf(ZEntityBlueprintFactoryBase* p_Factory, ZEntityType** p_Root, int p_Index,
                                      ZEntityType*** p_Out) {
    *p_Out = p_Factory->GetSubEntity(p_Root, p_Index);
}

__declspec(noinline) DWORD SafeGetSubEntity(ZEntityBlueprintFactoryBase* p_Factory, ZEntityType** p_Root, int p_Index,
                                            ZEntityType*** p_Out) {
    DWORD s_Code = 0;

    __try {
        SubEntityOf(p_Factory, p_Root, p_Index, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void SubEntityBlueprintOf(ZEntityBlueprintFactoryBase* p_Factory, int p_Index,
                                               ZEntityBlueprintFactoryBase** p_Out) {
    *p_Out = p_Factory->GetSubEntityBlueprint(p_Index);
}

__declspec(noinline) DWORD SafeGetSubEntityBlueprint(ZEntityBlueprintFactoryBase* p_Factory, int p_Index,
                                                     ZEntityBlueprintFactoryBase** p_Out) {
    DWORD s_Code = 0;

    __try {
        SubEntityBlueprintOf(p_Factory, p_Index, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void SubEntityIndexOf(ZEntityBlueprintFactoryBase* p_Factory, uint64_t p_Id, int* p_Out) {
    *p_Out = p_Factory->GetSubEntityIndex(p_Id);
}

__declspec(noinline) DWORD SafeGetSubEntityIndex(ZEntityBlueprintFactoryBase* p_Factory, uint64_t p_Id, int* p_Out) {
    DWORD s_Code = 0;

    __try {
        SubEntityIndexOf(p_Factory, p_Id, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void TypeOf(ZEntityType** p_Entity, ZEntityType** p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    *p_Out = s_Entity ? s_Entity->GetType() : nullptr;
}

__declspec(noinline) DWORD SafeGetType(ZEntityType** p_Entity, ZEntityType** p_Out) {
    DWORD s_Code = 0;

    __try {
        TypeOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void BlueprintFactoryOf(ZEntityType** p_Entity, ZEntityBlueprintFactoryBase** p_Out) {
    *p_Out = ZEntityRef(p_Entity).GetBlueprintFactory();
}

__declspec(noinline) DWORD SafeGetBlueprintFactory(ZEntityType** p_Entity, ZEntityBlueprintFactoryBase** p_Out) {
    DWORD s_Code = 0;

    __try {
        BlueprintFactoryOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void BlueprintRidOf(ZEntityBlueprintFactoryBase* p_Factory, uint64_t* p_Out) {
    *p_Out = p_Factory->m_ridResource.GetID();
}

__declspec(noinline) DWORD SafeBlueprintRid(ZEntityBlueprintFactoryBase* p_Factory, uint64_t* p_Out) {
    DWORD s_Code = 0;

    __try {
        BlueprintRidOf(p_Factory, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void HasInterfaceOf(const ZEntityType* p_Type, const STypeID* p_TypeId, bool* p_Out) {
    *p_Out = false;
    const TArray<SInterfaceData>* s_Interfaces = p_Type->m_pInterfaceData;

    if (!s_Interfaces)
        return;

    for (size_t i = 0; i < s_Interfaces->size(); ++i) {
        if ((*s_Interfaces)[i].m_Type == p_TypeId) {
            *p_Out = true;
            return;
        }
    }
}

__declspec(noinline) DWORD SafeHasInterface(const ZEntityType* p_Type, const STypeID* p_TypeId, bool* p_Out) {
    DWORD s_Code = 0;

    __try {
        HasInterfaceOf(p_Type, p_TypeId, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// Compares the concrete class name, read as the Editor does:
// (*type->m_pInterfaceData)[0].m_Type->GetTypeInfo()->pszTypeName.
__declspec(noinline) void ClassNameEqualsOf(const ZEntityType* p_Type, const char* p_Name, bool* p_Out) {
    *p_Out = false;
    const TArray<SInterfaceData>* s_Interfaces = p_Type->m_pInterfaceData;

    if (!s_Interfaces || s_Interfaces->size() == 0)
        return;

    const STypeID* s_TypeId = (*s_Interfaces)[0].m_Type;
    const IType* s_TypeInfo = s_TypeId ? s_TypeId->GetTypeInfo() : nullptr;
    const char* s_TypeName = s_TypeInfo ? s_TypeInfo->pszTypeName : nullptr;
    *p_Out = s_TypeName && std::strcmp(s_TypeName, p_Name) == 0;
}

__declspec(noinline) DWORD SafeClassNameEquals(const ZEntityType* p_Type, const char* p_Name, bool* p_Out) {
    DWORD s_Code = 0;

    __try {
        ClassNameEqualsOf(p_Type, p_Name, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// One property's storage as GetProperty locates it: the entity type's
// SPropertyData for the ID, at the entity pointer plus m_nPropertyOffset.
struct RawPropertyOut {
    bool found;
    bool hasGetter; // read through a getter function, so the storage is not the value
    uint16_t typeSize;
    uint32_t bits; // the storage, when typeSize is 4 and there is no getter
};

__declspec(noinline) void RawProperty32Of(ZEntityType** p_Entity, uint32_t p_PropertyId, RawPropertyOut* p_Out) {
    *p_Out = {false, false, 0, 0};
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;
    const SPropertyData* s_Property = s_Type ? s_Type->FindProperty(p_PropertyId) : nullptr;
    const SPropertyInfo* s_Info = s_Property ? s_Property->m_pPropertyInfo : nullptr;

    if (!s_Info)
        return;

    const IType* s_TypeInfo = s_Info->m_Type ? s_Info->m_Type->GetTypeInfo() : nullptr;
    p_Out->found = true;
    p_Out->hasGetter = (s_Info->m_Flags & EPropertyInfoFlags::E_HAS_GETTER_SETTER) != 0;
    p_Out->typeSize = s_TypeInfo ? s_TypeInfo->m_nTypeSize : 0;

    if (!p_Out->hasGetter && p_Out->typeSize == sizeof(p_Out->bits)) {
        const auto s_Address = reinterpret_cast<uintptr_t>(p_Entity) + s_Property->m_nPropertyOffset;
        std::memcpy(&p_Out->bits, reinterpret_cast<const void*>(s_Address), sizeof(p_Out->bits));
    }
}

__declspec(noinline) DWORD SafeRawProperty32(ZEntityType** p_Entity, uint32_t p_PropertyId, RawPropertyOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        RawProperty32Of(p_Entity, p_PropertyId, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// m_RepositoryId at its SDK offset (ZRepositoryKeyEntity::m_RepositoryId, 0x18).
__declspec(noinline) void RepositoryIdOf(ZEntityType** p_Entity, fr::Guid* p_Out) {
    const auto* s_Entity = reinterpret_cast<const ZRepositoryKeyEntity*>(ZEntityRef(p_Entity).GetEntity());
    std::memcpy(p_Out, &s_Entity->m_RepositoryId, sizeof(*p_Out));
}

__declspec(noinline) DWORD SafeRepositoryId(ZEntityType** p_Entity, fr::Guid* p_Out) {
    DWORD s_Code = 0;

    __try {
        RepositoryIdOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void EntityClassOf(ZEntityType** p_Entity, const char* p_ClassName, EntityClassCheck* p_Out) {
    *p_Out = {false, false, 0};
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;

    if (!s_Type)
        return;

    p_Out->hasType = true;
    p_Out->id = s_Type->m_nEntityID;
    ClassNameEqualsOf(s_Type, p_ClassName, &p_Out->matches);
}

__declspec(noinline) DWORD SafeEntityClass(ZEntityType** p_Entity, const char* p_ClassName, EntityClassCheck* p_Out) {
    DWORD s_Code = 0;

    __try {
        EntityClassOf(p_Entity, p_ClassName, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

void LogFault(const char* p_Step, DWORD p_Code, uint64_t p_BrickRid) {
    Log("entity walk fault in {} (exception 0x{:08X}) in brick {:016X}; skipping the rest of this brick", p_Step,
        static_cast<uint32_t>(p_Code), p_BrickRid);
}

// m_eParameter read raw as int32, for when the engine's type registry lacks
// EDifficultyParameter (so ReadProperty cannot read it).
std::optional<int32_t> ReadParamRaw(const ZEntityRef& p_Entity, uint64_t p_BrickRid) {
    static bool s_LoggedFallback = false;
    static bool s_LoggedNotRaw = false;

    if (!s_LoggedFallback) {
        s_LoggedFallback = true;
        Log("m_eParameter: {} not in the type registry; reading it raw as int32", ZHMTypeName<EDifficultyParameter>);
    }

    const RawRead32 s_Raw = ReadPropertyRaw32(p_Entity, "m_eParameter");

    if (s_Raw.status == RawRead32::Status::Ok)
        return std::bit_cast<int32_t>(s_Raw.bits);

    if (s_Raw.status == RawRead32::Status::NotRaw && !s_LoggedNotRaw) {
        s_LoggedNotRaw = true;
        Log("m_eParameter has engine type size {} or a getter; not read raw", s_Raw.typeSize);
    }
    else if (s_Raw.status == RawRead32::Status::Fault) {
        Log("m_eParameter: fault reading it raw (exception 0x{:08X}) in brick {:016X}", s_Raw.faultCode, p_BrickRid);
    }

    return std::nullopt;
}

FoundParam ReadFoundParam(ZEntityType** p_Entity, const char* p_MatchedBy, uint64_t p_BrickRid) {
    const ZEntityRef s_Ref(p_Entity);
    FoundParam s_Found {s_Ref,        std::nullopt, std::nullopt, std::nullopt,
                        std::nullopt, std::nullopt, p_MatchedBy,  p_BrickRid};

    if (!IsTypeRegistered(ZHMTypeName<EDifficultyParameter>))
        s_Found.param = ReadParamRaw(s_Ref, p_BrickRid);
    else if (const auto s_Param = ReadProperty<EDifficultyParameter>(s_Ref, "m_eParameter"))
        s_Found.param = static_cast<int32_t>(*s_Param);

    s_Found.value = ReadProperty<float>(s_Ref, "m_fValue");
    s_Found.count = ReadProperty<int32_t>(s_Ref, "m_nCount");
    s_Found.toggle = ReadProperty<bool>(s_Ref, "m_bToggle");

    uint32_t s_Fault = 0;
    s_Found.repoId = ReadRepositoryId(s_Ref, &s_Fault);

    if (s_Fault)
        Log("m_RepositoryId: fault reading it (exception 0x{:08X}) in brick {:016X}", s_Fault, p_BrickRid);

    return s_Found;
}

// What one walk collects: difficulty-parameter entities (into p_Params),
// entities of one concrete class (into p_ClassOut), or whatever a visitor
// counts as matched.
struct WalkTarget {
    const STypeID* difficultyTypeId;
    std::vector<FoundParam>* params;
    const char* className;
    std::vector<ClassEntity>* classOut;
    const EntityVisitor* visitor = nullptr;
};

// Copies at most p_Capacity - 1 characters and terminates p_Out.
void CopyName(const char* p_Text, char* p_Out, size_t p_Capacity) {
    size_t i = 0;

    for (; p_Text && p_Text[i] && i + 1 < p_Capacity; ++i)
        p_Out[i] = p_Text[i];

    p_Out[i] = '\0';
}

__declspec(noinline) void LocatePropertyOf(ZEntityType** p_Entity, uint32_t p_PropertyId, PropertyLocation* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;
    const SPropertyData* s_Property = s_Type ? s_Type->FindProperty(p_PropertyId) : nullptr;
    const SPropertyInfo* s_Info = s_Property ? s_Property->m_pPropertyInfo : nullptr;

    if (!s_Info)
        return;

    if ((s_Info->m_Flags & EPropertyInfoFlags::E_HAS_GETTER_SETTER) != 0) {
        p_Out->status = PropertyLocation::Status::HasGetter;
        return;
    }

    const IType* s_TypeInfo = s_Info->m_Type ? s_Info->m_Type->GetTypeInfo() : nullptr;
    p_Out->status = PropertyLocation::Status::Ok;
    p_Out->address = reinterpret_cast<uintptr_t>(p_Entity) + s_Property->m_nPropertyOffset;

    if (!s_TypeInfo)
        return;

    p_Out->typeSize = s_TypeInfo->m_nTypeSize;
    CopyName(s_TypeInfo->pszTypeName, p_Out->typeName, sizeof(p_Out->typeName));

    if (s_TypeInfo->IsArray()) {
        p_Out->isArray = true;
        const STypeID* s_ElementId = reinterpret_cast<const IContainerType*>(s_TypeInfo)->elementType;
        const IType* s_Element = s_ElementId ? s_ElementId->GetTypeInfo() : nullptr;

        if (s_Element) {
            p_Out->elementSize = s_Element->m_nTypeSize;
            CopyName(s_Element->pszTypeName, p_Out->elementTypeName, sizeof(p_Out->elementTypeName));
        }
    }
}

__declspec(noinline) DWORD SafeLocateProperty(ZEntityType** p_Entity, uint32_t p_PropertyId, PropertyLocation* p_Out) {
    DWORD s_Code = 0;

    __try {
        LocatePropertyOf(p_Entity, p_PropertyId, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void BytesOf(uintptr_t p_Address, void* p_Out, size_t p_Size) {
    std::memcpy(p_Out, reinterpret_cast<const void*>(p_Address), p_Size);
}

__declspec(noinline) DWORD SafeBytes(uintptr_t p_Address, void* p_Out, size_t p_Size) {
    DWORD s_Code = 0;

    __try {
        BytesOf(p_Address, p_Out, p_Size);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct ClassNameOut {
    bool hasType;
    uint64_t id;
    char name[96];
};

__declspec(noinline) void ClassNameOf(ZEntityType** p_Entity, ClassNameOut* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;

    if (!s_Type)
        return;

    p_Out->hasType = true;
    p_Out->id = s_Type->m_nEntityID;
    const TArray<SInterfaceData>* s_Interfaces = s_Type->m_pInterfaceData;

    if (!s_Interfaces || s_Interfaces->size() == 0)
        return;

    const STypeID* s_TypeId = (*s_Interfaces)[0].m_Type;
    const IType* s_TypeInfo = s_TypeId ? s_TypeId->GetTypeInfo() : nullptr;
    CopyName(s_TypeInfo ? s_TypeInfo->pszTypeName : nullptr, p_Out->name, sizeof(p_Out->name));
}

__declspec(noinline) DWORD SafeClassName(ZEntityType** p_Entity, ClassNameOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        ClassNameOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// Walks one brick. Returns false if the walk's entity cap (p_Cap) was reached.
bool WalkTree(ZEntityType** p_Root, const WalkTarget& p_Target, std::unordered_set<const void*>& p_Visited,
              size_t& p_SceneVisited, size_t p_Cap, BrickWalkStats& p_Stats) {
    ZEntityType** const s_Root = p_Root;
    ZEntityBlueprintFactoryBase* s_RootFactory = nullptr;

    if (!s_Root)
        return true;

    if (const DWORD s_Code = SafeGetBlueprintFactory(s_Root, &s_RootFactory)) {
        p_Stats.faultStep = "GetBlueprintFactory";
        LogFault(p_Stats.faultStep, s_Code, p_Stats.brickRid);
        return true;
    }

    if (!s_RootFactory)
        return true;

    std::queue<std::pair<ZEntityBlueprintFactoryBase*, ZEntityType**>> s_Queue;
    s_Queue.emplace(s_RootFactory, s_Root);

    while (!s_Queue.empty()) {
        const auto [s_Factory, s_Parent] = s_Queue.front();
        s_Queue.pop();

        int64_t s_Count = 0;

        if (const DWORD s_Code = SafeSubEntitiesCount(s_Factory, &s_Count)) {
            p_Stats.faultStep = "GetSubEntitiesCount";
            LogFault(p_Stats.faultStep, s_Code, p_Stats.brickRid);
            return true;
        }

        if (s_Count < 0 || static_cast<uint64_t>(s_Count) > kMaxEntitiesPerScene) {
            Log("entity walk: factory reports {} sub-entities (outside 0 to {}) in brick {:016X}; skipping this "
                "factory",
                s_Count, kMaxEntitiesPerScene, p_Stats.brickRid);
            continue;
        }

        for (int i = 0; i < s_Count; ++i) {
            ZEntityType** s_Sub = nullptr;
            ZEntityBlueprintFactoryBase* s_SubFactory = nullptr;
            ZEntityType* s_Type = nullptr;

            if (const DWORD s_Code = SafeGetSubEntity(s_Factory, s_Parent, i, &s_Sub)) {
                p_Stats.faultStep = "GetSubEntity";
                LogFault(p_Stats.faultStep, s_Code, p_Stats.brickRid);
                return true;
            }

            if (const DWORD s_Code = SafeGetSubEntityBlueprint(s_Factory, i, &s_SubFactory)) {
                p_Stats.faultStep = "GetSubEntityBlueprint";
                LogFault(p_Stats.faultStep, s_Code, p_Stats.brickRid);
                return true;
            }

            if (const DWORD s_Code = SafeGetType(s_Sub, &s_Type)) {
                p_Stats.faultStep = "GetType";
                LogFault(p_Stats.faultStep, s_Code, p_Stats.brickRid);
                return true;
            }

            if (!s_Type)
                continue;

            // One hash lookup per entity (the set is discarded after the walk,
            // so an entity inserted at the cap does no harm).
            if (!p_Visited.insert(ZEntityRef(s_Sub).GetEntity()).second)
                continue;

            if (p_SceneVisited >= p_Cap)
                return false;

            ++p_SceneVisited;
            ++p_Stats.visited;

            if (p_Target.params) {
                bool s_ByInterface = false;
                bool s_ByClass = false;

                if (p_Target.difficultyTypeId) {
                    if (const DWORD s_Code = SafeHasInterface(s_Type, p_Target.difficultyTypeId, &s_ByInterface)) {
                        p_Stats.faultStep = "interface data";
                        LogFault(p_Stats.faultStep, s_Code, p_Stats.brickRid);
                        return true;
                    }
                }

                if (const DWORD s_Code = SafeClassNameEquals(s_Type, kDifficultyEntityClass, &s_ByClass)) {
                    p_Stats.faultStep = "type name";
                    LogFault(p_Stats.faultStep, s_Code, p_Stats.brickRid);
                    return true;
                }

                if (s_ByInterface || s_ByClass) {
                    const char* s_MatchedBy = s_ByInterface && s_ByClass ? "interface and class name"
                                              : s_ByInterface            ? "interface"
                                                                         : "class name";
                    p_Target.params->push_back(ReadFoundParam(s_Sub, s_MatchedBy, p_Stats.brickRid));
                    ++p_Stats.matched;
                }
            }

            if (p_Target.classOut) {
                bool s_Matches = false;

                if (const DWORD s_Code = SafeClassNameEquals(s_Type, p_Target.className, &s_Matches)) {
                    p_Stats.faultStep = "type name";
                    LogFault(p_Stats.faultStep, s_Code, p_Stats.brickRid);
                    return true;
                }

                if (s_Matches) {
                    p_Target.classOut->push_back({s_Sub, p_Stats.brickRid});
                    ++p_Stats.matched;
                }
            }

            if (p_Target.visitor &&
                (*p_Target.visitor)({s_Sub, s_Type, s_Factory, i, s_Parent, p_Stats.brickRid, s_SubFactory})) {
                ++p_Stats.matched;
            }

            if (s_SubFactory) {
                int64_t s_SubCount = 0;

                if (const DWORD s_Code = SafeSubEntitiesCount(s_SubFactory, &s_SubCount)) {
                    p_Stats.faultStep = "GetSubEntitiesCount";
                    LogFault(p_Stats.faultStep, s_Code, p_Stats.brickRid);
                    return true;
                }

                if (s_SubCount > 0)
                    s_Queue.emplace(s_SubFactory, s_Sub);
            }
        }
    }

    return true;
}

bool WalkBrick(const SBrickAllocationInfo& p_Brick, const WalkTarget& p_Target,
               std::unordered_set<const void*>& p_Visited, size_t& p_SceneVisited, size_t p_Cap,
               BrickWalkStats& p_Stats) {
    return WalkTree(p_Brick.m_EntityRef.m_pObj, p_Target, p_Visited, p_SceneVisited, p_Cap, p_Stats);
}

} // namespace

WalkStats VisitEntityTree(ZEntityType** p_Root, uint64_t p_Label, const EntityVisitor& p_Visitor, size_t p_Cap) {
    WalkStats s_Stats {0, 0, false, 0, {}};
    std::unordered_set<const void*> s_Visited;
    size_t s_Visits = 0;
    BrickWalkStats s_TreeStats {p_Label, 0, 0, nullptr};
    const WalkTarget s_Target {nullptr, nullptr, nullptr, nullptr, &p_Visitor};
    s_Stats.capped = !WalkTree(p_Root, s_Target, s_Visited, s_Visits, p_Cap, s_TreeStats);
    s_Stats.visited = s_TreeStats.visited;
    s_Stats.matched = s_TreeStats.matched;
    s_Stats.faults = s_TreeStats.faultStep ? 1 : 0;
    s_Stats.bricks.push_back(s_TreeStats);
    return s_Stats;
}

SubEntityLookup LookUpSubEntity(ZEntityType** p_Parent, ZEntityBlueprintFactoryBase* p_ParentFactory, uint64_t p_Id) {
    SubEntityLookup s_Out;

    if (!p_Parent)
        return s_Out;

    ZEntityBlueprintFactoryBase* s_Factory = p_ParentFactory;

    if (!s_Factory) {
        if (const DWORD s_Code = SafeGetBlueprintFactory(p_Parent, &s_Factory)) {
            s_Out.faultStep = "GetBlueprintFactory";
            s_Out.faultCode = static_cast<uint32_t>(s_Code);
            return s_Out;
        }

        if (!s_Factory)
            return s_Out;
    }

    s_Out.parentFactory = s_Factory;

    if (const DWORD s_Code = SafeGetSubEntityIndex(s_Factory, p_Id, &s_Out.index)) {
        s_Out.faultStep = "GetSubEntityIndex";
        s_Out.faultCode = static_cast<uint32_t>(s_Code);
        s_Out.index = -1;
        return s_Out;
    }

    if (s_Out.index < 0) {
        s_Out.index = -1;
        return s_Out;
    }

    if (const DWORD s_Code = SafeGetSubEntity(s_Factory, p_Parent, s_Out.index, &s_Out.entity)) {
        s_Out.faultStep = "GetSubEntity";
        s_Out.faultCode = static_cast<uint32_t>(s_Code);
        s_Out.entity = nullptr;
        return s_Out;
    }

    if (const DWORD s_Code = SafeGetSubEntityBlueprint(s_Factory, s_Out.index, &s_Out.factory)) {
        s_Out.faultStep = "GetSubEntityBlueprint";
        s_Out.faultCode = static_cast<uint32_t>(s_Code);
        s_Out.factory = nullptr;
    }

    return s_Out;
}

uint32_t ReadSubEntityCount(ZEntityBlueprintFactoryBase* p_Factory, int64_t* p_Count) {
    *p_Count = 0;
    return p_Factory ? static_cast<uint32_t>(SafeSubEntitiesCount(p_Factory, p_Count)) : 0;
}

uint32_t ReadSubEntityAt(ZEntityBlueprintFactoryBase* p_Factory, ZEntityType** p_Parent, int p_Index,
                         ZEntityType*** p_Entity, ZEntityBlueprintFactoryBase** p_SubFactory) {
    *p_Entity = nullptr;
    *p_SubFactory = nullptr;

    if (!p_Factory || !p_Parent)
        return 0;

    if (const DWORD s_Code = SafeGetSubEntity(p_Factory, p_Parent, p_Index, p_Entity))
        return static_cast<uint32_t>(s_Code);

    return static_cast<uint32_t>(SafeGetSubEntityBlueprint(p_Factory, p_Index, p_SubFactory));
}

uint32_t ReadOwnBlueprintFactory(ZEntityType** p_Entity, ZEntityBlueprintFactoryBase** p_Factory) {
    *p_Factory = nullptr;
    return p_Entity ? static_cast<uint32_t>(SafeGetBlueprintFactory(p_Entity, p_Factory)) : 0;
}

uint32_t ReadBlueprintRid(ZEntityType** p_Entity, uint64_t* p_Rid) {
    *p_Rid = 0;

    if (!p_Entity)
        return 0;

    ZEntityBlueprintFactoryBase* s_Factory = nullptr;

    if (const DWORD s_Code = SafeGetBlueprintFactory(p_Entity, &s_Factory))
        return static_cast<uint32_t>(s_Code);

    if (!s_Factory)
        return 0;

    return static_cast<uint32_t>(SafeBlueprintRid(s_Factory, p_Rid));
}

uint32_t ReadFactoryRid(ZEntityBlueprintFactoryBase* p_Factory, uint64_t* p_Rid) {
    *p_Rid = 0;
    return p_Factory ? static_cast<uint32_t>(SafeBlueprintRid(p_Factory, p_Rid)) : 0;
}

bool IsTypeRegistered(std::string_view p_TypeName) {
    static std::set<std::string, std::less<>> s_Logged;

    if (*Globals::TypeRegistry && (*Globals::TypeRegistry)->GetTypeID(ZString(p_TypeName)))
        return true;

    if (s_Logged.emplace(p_TypeName).second) {
        if (*Globals::TypeRegistry)
            Log("type {} not in registry; properties of this type are not read", p_TypeName);
        else
            Log("type registry not available; properties of type {} are not read", p_TypeName);
    }

    return false;
}

RawRead32 ReadPropertyRaw32(const ZEntityRef& p_Entity, std::string_view p_Name) {
    RawPropertyOut s_Out {};

    if (const DWORD s_Code = SafeRawProperty32(p_Entity.m_pObj, Hash::Crc32(p_Name.data(), p_Name.size()), &s_Out))
        return {RawRead32::Status::Fault, 0, 0, static_cast<uint32_t>(s_Code)};

    if (!s_Out.found)
        return {RawRead32::Status::Missing, 0, 0, 0};

    if (s_Out.hasGetter || s_Out.typeSize != sizeof(s_Out.bits))
        return {RawRead32::Status::NotRaw, 0, s_Out.typeSize, 0};

    return {RawRead32::Status::Ok, s_Out.bits, s_Out.typeSize, 0};
}

std::optional<fr::Guid> ReadRepositoryId(const ZEntityRef& p_Entity, uint32_t* p_FaultCode) {
    *p_FaultCode = 0;

    if (!p_Entity.m_pObj)
        return std::nullopt;

    fr::Guid s_Guid;

    if (const DWORD s_Code = SafeRepositoryId(p_Entity.m_pObj, &s_Guid)) {
        *p_FaultCode = static_cast<uint32_t>(s_Code);
        return std::nullopt;
    }

    return s_Guid;
}

uint32_t CheckEntityClass(ZEntityType** p_Entity, const char* p_ClassName, EntityClassCheck* p_Out) {
    *p_Out = {false, false, 0};

    if (!p_Entity)
        return 0;

    return static_cast<uint32_t>(SafeEntityClass(p_Entity, p_ClassName, p_Out));
}

void LogPropertyFault(std::string_view p_Name, uint32_t p_Code) {
    static std::set<std::string> s_Logged;

    if (s_Logged.insert(std::string(p_Name)).second)
        Log("property {}: reading it faulted ({}); value not read", p_Name, FaultText(p_Code));
}

void LogPropertyTypeMismatch(std::string_view p_Name, std::string_view p_Expected, const char* p_Actual) {
    static std::set<std::string> s_Logged;
    const std::string s_Actual = p_Actual ? p_Actual : "<unknown>";

    if (s_Logged.insert(std::string(p_Name) + ":" + s_Actual).second)
        Log("property {} has engine type {} (expected {}); value not read", p_Name, s_Actual, p_Expected);
}

WalkStats FindDifficultyParams(const TArray<SBrickAllocationInfo>& p_Bricks, std::vector<FoundParam>& p_Out,
                               const std::vector<uint64_t>* p_OnlyBricks, size_t p_VisitedReserve, size_t p_Cap) {
    WalkStats s_Stats {0, 0, false, 0, {}};

    const STypeID* s_DifficultyTypeId = nullptr;

    if (*Globals::TypeRegistry)
        s_DifficultyTypeId = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view(kDifficultyEntityClass)));

    if (!s_DifficultyTypeId)
        Log("type {} not in the type registry; matching by class name only", kDifficultyEntityClass);

    std::unordered_set<const void*> s_Visited;
    s_Visited.reserve(p_VisitedReserve);
    size_t s_SceneVisited = 0;

    for (size_t i = 0; i < p_Bricks.size(); ++i) {
        const auto& s_Brick = p_Bricks[i];
        BrickWalkStats s_BrickStats {s_Brick.m_RuntimeResourceID.GetID(), 0, 0, nullptr};

        if (p_OnlyBricks &&
            std::find(p_OnlyBricks->begin(), p_OnlyBricks->end(), s_BrickStats.brickRid) == p_OnlyBricks->end()) {
            continue;
        }

        const WalkTarget s_Target {s_DifficultyTypeId, &p_Out, nullptr, nullptr};
        const bool s_UnderCap = WalkBrick(s_Brick, s_Target, s_Visited, s_SceneVisited, p_Cap, s_BrickStats);

        s_Stats.visited += s_BrickStats.visited;
        s_Stats.matched += s_BrickStats.matched;

        if (s_BrickStats.faultStep)
            ++s_Stats.faults;

        s_Stats.bricks.push_back(s_BrickStats);

        if (!s_UnderCap) {
            s_Stats.capped = true;
            break;
        }
    }

    return s_Stats;
}

WalkStats FindEntitiesOfClass(const TArray<SBrickAllocationInfo>& p_Bricks, const std::vector<uint64_t>& p_OnlyBricks,
                              const char* p_ClassName, std::vector<ClassEntity>& p_Out) {
    WalkStats s_Stats {0, 0, false, 0, {}};
    std::unordered_set<const void*> s_Visited;
    size_t s_SceneVisited = 0;

    for (size_t i = 0; i < p_Bricks.size(); ++i) {
        const auto& s_Brick = p_Bricks[i];
        BrickWalkStats s_BrickStats {s_Brick.m_RuntimeResourceID.GetID(), 0, 0, nullptr};

        if (std::find(p_OnlyBricks.begin(), p_OnlyBricks.end(), s_BrickStats.brickRid) == p_OnlyBricks.end())
            continue;

        const WalkTarget s_Target {nullptr, nullptr, p_ClassName, &p_Out};
        const bool s_UnderCap = WalkBrick(s_Brick, s_Target, s_Visited, s_SceneVisited, kWalkEntityCap, s_BrickStats);

        s_Stats.visited += s_BrickStats.visited;
        s_Stats.matched += s_BrickStats.matched;
        s_Stats.faults += s_BrickStats.faultStep ? 1 : 0;
        s_Stats.bricks.push_back(s_BrickStats);

        if (!s_UnderCap) {
            s_Stats.capped = true;
            break;
        }
    }

    return s_Stats;
}

WalkStats VisitAllEntities(const TArray<SBrickAllocationInfo>& p_Bricks, const EntityVisitor& p_Visitor,
                           size_t p_VisitedReserve) {
    std::vector<size_t> s_Order(p_Bricks.size());

    for (size_t i = 0; i < s_Order.size(); ++i)
        s_Order[i] = i;

    return VisitEntitiesInOrder(p_Bricks, s_Order, p_Visitor, kWideWalkEntityCap, {}, p_VisitedReserve);
}

WalkStats VisitEntitiesInOrder(const TArray<SBrickAllocationInfo>& p_Bricks, std::span<const size_t> p_Order,
                               const EntityVisitor& p_Visitor, size_t p_Cap, const BrickDoneCheck& p_Done,
                               size_t p_VisitedReserve) {
    WalkStats s_Stats {0, 0, false, 0, {}};
    std::unordered_set<const void*> s_Visited;
    s_Visited.reserve(p_VisitedReserve);
    size_t s_SceneVisited = 0;

    for (size_t n = 0; n < p_Order.size(); ++n) {
        if (p_Order[n] >= p_Bricks.size())
            continue;

        const auto& s_Brick = p_Bricks[p_Order[n]];
        BrickWalkStats s_BrickStats {s_Brick.m_RuntimeResourceID.GetID(), 0, 0, nullptr};
        const WalkTarget s_Target {nullptr, nullptr, nullptr, nullptr, &p_Visitor};
        const bool s_UnderCap = WalkBrick(s_Brick, s_Target, s_Visited, s_SceneVisited, p_Cap, s_BrickStats);

        s_Stats.visited += s_BrickStats.visited;
        s_Stats.matched += s_BrickStats.matched;
        s_Stats.faults += s_BrickStats.faultStep ? 1 : 0;
        s_Stats.bricks.push_back(s_BrickStats);

        if (!s_UnderCap) {
            s_Stats.capped = true;
            break;
        }

        if (p_Done && p_Done(n + 1))
            break;
    }

    return s_Stats;
}

PropertyLocation LocateProperty(ZEntityType** p_Entity, std::string_view p_Name) {
    return LocatePropertyById(p_Entity, Hash::Crc32(p_Name.data(), p_Name.size()));
}

PropertyLocation LocatePropertyById(ZEntityType** p_Entity, uint32_t p_Id) {
    PropertyLocation s_Out {};
    s_Out.status = PropertyLocation::Status::Missing;

    if (!p_Entity)
        return s_Out;

    if (const DWORD s_Code = SafeLocateProperty(p_Entity, p_Id, &s_Out)) {
        s_Out = {};
        s_Out.status = PropertyLocation::Status::Fault;
        s_Out.faultCode = static_cast<uint32_t>(s_Code);
    }

    return s_Out;
}

uint32_t ReadEngineBytes(uintptr_t p_Address, void* p_Out, size_t p_Size) {
    if (p_Address == 0)
        return 0xC0000005; // as the read would fault

    return static_cast<uint32_t>(SafeBytes(p_Address, p_Out, p_Size));
}

uint32_t ReadEntityClassName(ZEntityType** p_Entity, std::string& p_Name, uint64_t* p_Id) {
    p_Name.clear();
    *p_Id = 0;

    if (!p_Entity)
        return 0;

    ClassNameOut s_Out {};

    if (const DWORD s_Code = SafeClassName(p_Entity, &s_Out))
        return static_cast<uint32_t>(s_Code);

    p_Name = s_Out.name;
    *p_Id = s_Out.id;
    return 0;
}

std::string EntityText(ZEntityType** p_Entity) {
    if (!p_Entity)
        return "<none>";

    std::string s_Class;
    uint64_t s_Id = 0;

    if (const uint32_t s_Code = ReadEntityClassName(p_Entity, s_Class, &s_Id))
        return FaultText(s_Code);

    return EntityIdText(s_Id, s_Class);
}

uintptr_t Raw4Address(ZEntityType** p_Entity, std::string_view p_Name, const char* p_Type, std::string* p_Why) {
    if (!p_Entity) {
        *p_Why = fmt::format("{}: no entity", p_Name);
        return 0;
    }

    const PropertyLocation s_Location = LocateProperty(p_Entity, p_Name);

    if (s_Location.status == PropertyLocation::Status::Fault) {
        *p_Why = fmt::format("{} {}", p_Name, FaultText(s_Location.faultCode));
        return 0;
    }

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != 4 ||
        std::strcmp(s_Location.typeName, p_Type) != 0) {
        *p_Why = fmt::format("{} not a raw {} ({})", p_Name, p_Type,
                             s_Location.status == PropertyLocation::Status::Missing ? "missing" : s_Location.typeName);
        return 0;
    }

    return s_Location.address;
}

std::optional<int32_t> ReadLiveSeed(const TArray<SBrickAllocationInfo>& p_Bricks) {
    for (size_t i = 0; i < p_Bricks.size(); ++i) {
        const auto& s_Brick = p_Bricks[i];

        if (s_Brick.m_RuntimeResourceID.GetID() != fr::kEvergreenSetupBrick)
            continue;

        ZEntityType** const s_Root = s_Brick.m_EntityRef.m_pObj;
        ZEntityBlueprintFactoryBase* s_Factory = nullptr;

        if (const DWORD s_Code = s_Root ? SafeGetBlueprintFactory(s_Root, &s_Factory) : 0) {
            LogTo(fr::LogTopic::Missions, "live seed: fault reading the setup brick's factory (exception 0x{:08X})",
                  static_cast<uint32_t>(s_Code));
            return std::nullopt;
        }

        if (!s_Factory) {
            LogTo(fr::LogTopic::Missions, "live seed: evergreen_setup brick has no blueprint factory");
            return std::nullopt;
        }

        int s_Index = -1;

        if (const DWORD s_Code = SafeGetSubEntityIndex(s_Factory, fr::kCampaignSeedEntityId, &s_Index)) {
            LogTo(fr::LogTopic::Missions, "live seed: fault looking up the seed entity (exception 0x{:08X})",
                  static_cast<uint32_t>(s_Code));
            return std::nullopt;
        }

        if (s_Index == -1) {
            LogTo(fr::LogTopic::Missions, "live seed: seed entity {:016x} not in the evergreen_setup brick",
                  fr::kCampaignSeedEntityId);
            return std::nullopt;
        }

        ZEntityType** s_SeedEntity = nullptr;

        if (const DWORD s_Code = SafeGetSubEntity(s_Factory, s_Root, s_Index, &s_SeedEntity)) {
            LogTo(fr::LogTopic::Missions, "live seed: fault reading the seed entity (exception 0x{:08X})",
                  static_cast<uint32_t>(s_Code));
            return std::nullopt;
        }

        const RawRead32 s_Seed = ReadPropertyRaw32(ZEntityRef(s_SeedEntity), "m_nValue");

        switch (s_Seed.status) {
            case RawRead32::Status::Ok: return std::bit_cast<int32_t>(s_Seed.bits);
            case RawRead32::Status::Missing:
                LogTo(fr::LogTopic::Missions, "live seed: seed entity has no m_nValue");
                break;
            case RawRead32::Status::NotRaw:
                LogTo(fr::LogTopic::Missions,
                      "live seed: seed entity's m_nValue has a getter or engine type size {}; not read raw",
                      s_Seed.typeSize);
                break;
            case RawRead32::Status::Fault:
                LogTo(fr::LogTopic::Missions, "live seed: fault reading m_nValue (exception 0x{:08X})",
                      s_Seed.faultCode);
                break;
        }

        return std::nullopt;
    }

    LogTo(fr::LogTopic::Missions, "live seed: evergreen_setup brick not loaded");
    return std::nullopt;
}

std::string FindSetupBrickEntities(const TArray<SBrickAllocationInfo>& p_Bricks, std::span<const uint64_t> p_Ids,
                                   std::vector<ZEntityType**>& p_Out) {
    p_Out.assign(p_Ids.size(), nullptr);

    for (size_t i = 0; i < p_Bricks.size(); ++i) {
        const auto& s_Brick = p_Bricks[i];

        if (s_Brick.m_RuntimeResourceID.GetID() != fr::kEvergreenSetupBrick)
            continue;

        ZEntityType** const s_Root = s_Brick.m_EntityRef.m_pObj;
        ZEntityBlueprintFactoryBase* s_Factory = nullptr;

        if (!s_Root)
            return "the evergreen_setup brick has no root entity";

        if (const DWORD s_Code = SafeGetBlueprintFactory(s_Root, &s_Factory))
            return fmt::format("fault reading the evergreen_setup brick's factory (exception 0x{:08X})",
                               static_cast<uint32_t>(s_Code));

        if (!s_Factory)
            return "the evergreen_setup brick has no blueprint factory";

        for (size_t k = 0; k < p_Ids.size(); ++k) {
            int s_Index = -1;

            if (SafeGetSubEntityIndex(s_Factory, p_Ids[k], &s_Index) || s_Index == -1)
                continue;

            ZEntityType** s_Entity = nullptr;

            if (!SafeGetSubEntity(s_Factory, s_Root, s_Index, &s_Entity))
                p_Out[k] = s_Entity;
        }

        return {};
    }

    return "the evergreen_setup brick is not loaded";
}
