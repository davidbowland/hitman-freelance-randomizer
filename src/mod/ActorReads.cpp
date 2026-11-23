// Guarded actor reads (see ActorReads.h); every raw engine read follows the
// guard pattern in Guard.h.

#include "ActorReads.h"

#include "../core/GuardWeapons.h" // fr::ActorTypeName, fr::kActorTypeGuard
#include "Collections.h"          // fr::ArrayElements
#include "EngineText.h"

#include <Glacier/ZActor.h>
#include <Glacier/ZOutfit.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

__declspec(noinline) void InterfaceOf(ZEntityType** p_Entity, const STypeID* p_Type, void** p_Out) {
    *p_Out = ZEntityRef(p_Entity).QueryInterface(p_Type);
}

__declspec(noinline) void ActorCoreOf(const ZActor* p_Actor, ActorCore* p_Out) {
    p_Out->activated = p_Actor->m_bActorActivated;
    p_Out->enabled = p_Actor->m_bEnabled;
    p_Out->alive = p_Actor->m_bAlive;
    p_Out->startEnabled = p_Actor->m_bStartEnabled;
    p_Out->crowd = p_Actor->m_bCrowdCharacter;
    p_Out->named = p_Actor->m_bIsNamedNPC;
    p_Out->activeEnforcer = p_Actor->m_bIsActiveEnforcer;
    p_Out->potentialEnforcer = p_Actor->m_bIsPotentialEnforcer;
    p_Out->dynamicEnforcer = p_Actor->m_bIsDynamicEnforcer;
    p_Out->contractTarget = p_Actor->m_bContractTarget;
    p_Out->runtimeId = p_Actor->m_nActorRuntimeId;
    std::memcpy(&p_Out->outfit, &p_Actor->m_OutfitRepositoryID, sizeof(p_Out->outfit));
}

__declspec(noinline) void ActorNameOf(const ZActor* p_Actor, NameOut* p_Out) {
    const std::string_view s_Name = p_Actor->m_sActorName.ToStringView();
    const size_t s_Count = std::min(s_Name.size(), sizeof(p_Out->text) - 1);
    std::memcpy(p_Out->text, s_Name.data(), s_Count);
    p_Out->text[s_Count] = '\0';
}

__declspec(noinline) void KitTypeOf(const ZActor* p_Actor, KitTypeOut* p_Out) {
    const ZGlobalOutfitKit* s_Kit = p_Actor->m_rOutfit.m_pInterfaceRef;
    p_Out->hasKit = s_Kit != nullptr;
    p_Out->actorType = s_Kit ? static_cast<int32_t>(s_Kit->m_eActorType) : 0;
}

__declspec(noinline) void OwningOf(ZEntityType** p_Entity, ZEntityType*** p_Out) {
    *p_Out = ZEntityRef(p_Entity).GetOwningEntity().m_pObj;
}

struct ActorsOut {
    size_t total;
    size_t count;
    ActorRef actors[kMaxManagerActors];
};

__declspec(noinline) void ActivatedActorsOf(const ZActorManager* p_Manager, ActorsOut* p_Out) {
    const auto& s_Actors = p_Manager->m_activatedActors;
    p_Out->total = s_Actors.size();
    p_Out->count = std::min(p_Out->total, kMaxManagerActors);

    for (size_t i = 0; i < p_Out->count; ++i)
        p_Out->actors[i] = {s_Actors[i].m_entityRef.m_pObj, s_Actors[i].m_pInterfaceRef};
}

__declspec(noinline) DWORD SafeActivatedActors(const ZActorManager* p_Manager, ActorsOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        ActivatedActorsOf(p_Manager, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void SpawnedActorsOf(const ZActorManager* p_Manager, ActorsOut* p_Out) {
    const auto& s_Actors = p_Manager->m_SpawnedActors;
    p_Out->total = s_Actors.size();
    p_Out->count = std::min(p_Out->total, kMaxManagerActors);

    for (size_t i = 0; i < p_Out->count; ++i)
        p_Out->actors[i] = {s_Actors[i].m_entityRef.m_pObj, s_Actors[i].m_pInterfaceRef};
}

__declspec(noinline) DWORD SafeSpawnedActors(const ZActorManager* p_Manager, ActorsOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        SpawnedActorsOf(p_Manager, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// One manager list through p_Read: the copy is heap-allocated (8 KB).
template <class F>
uint32_t ReadList(const ZActorManager* p_Manager, F p_Read, std::vector<ActorRef>& p_Out, size_t* p_Total) {
    p_Out.clear();
    *p_Total = 0;

    if (!p_Manager)
        return EXCEPTION_ACCESS_VIOLATION;

    const auto s_List = std::make_unique<ActorsOut>();

    if (const DWORD s_Code = p_Read(p_Manager, s_List.get()))
        return s_Code;

    *p_Total = s_List->total;
    p_Out.assign(s_List->actors, s_List->actors + s_List->count);
    return 0;
}

} // namespace

DWORD SafeInterface(ZEntityType** p_Entity, const STypeID* p_Type, void** p_Out) {
    DWORD s_Code = 0;

    __try {
        InterfaceOf(p_Entity, p_Type, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

DWORD SafeActorCore(const ZActor* p_Actor, ActorCore* p_Out) {
    DWORD s_Code = 0;

    __try {
        ActorCoreOf(p_Actor, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

DWORD SafeActorName(const ZActor* p_Actor, NameOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        ActorNameOf(p_Actor, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

DWORD SafeKitType(const ZActor* p_Actor, KitTypeOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        KitTypeOf(p_Actor, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

DWORD SafeOwning(ZEntityType** p_Entity, ZEntityType*** p_Out) {
    DWORD s_Code = 0;

    __try {
        OwningOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

ActorFacts ReadActor(const ZActor* p_Actor) {
    ActorFacts s_Out;

    if (!p_Actor) {
        s_Out.name = "<no actor>";
        return s_Out;
    }

    NameOut s_Name {};

    if (const DWORD s_Code = SafeActorName(p_Actor, &s_Name)) {
        s_Out.name = FaultText(s_Code);
    }
    else {
        s_Out.plainName = s_Name.text;
        s_Out.name = fmt::format("\"{}\"", s_Name.text);
    }

    if (const DWORD s_Code = SafeActorCore(p_Actor, &s_Out.core))
        s_Out.fault = s_Code;
    else
        s_Out.hasCore = true;

    return s_Out;
}

std::string ActorTypeText(const ZActor* p_Actor) {
    KitTypeOut s_Kit {};

    if (const DWORD s_Code = SafeKitType(p_Actor, &s_Kit))
        return "kit " + FaultText(s_Code);

    return s_Kit.hasKit ? fr::ActorTypeName(s_Kit.actorType) : std::string("no kit");
}

bool IsGuard(const ZActor* p_Actor) {
    KitTypeOut s_Kit {};
    return !SafeKitType(p_Actor, &s_Kit) && s_Kit.hasKit && s_Kit.actorType == fr::kActorTypeGuard;
}

uint32_t ReadActivatedActors(const ZActorManager* p_Manager, std::vector<ActorRef>& p_Out, size_t* p_Total) {
    return ReadList(p_Manager, SafeActivatedActors, p_Out, p_Total);
}

uint32_t ReadSpawnedActors(const ZActorManager* p_Manager, std::vector<ActorRef>& p_Out, size_t* p_Total) {
    return ReadList(p_Manager, SafeSpawnedActors, p_Out, p_Total);
}

ActorRefList ReadActorRefs(ZEntityType** p_Owner, size_t p_Max) {
    constexpr size_t kElementSize = 16; // TEntityRef<ZActor>: the entity, then the interface
    ActorRefList s_Out;
    const PropertyLocation s_Location = LocateProperty(p_Owner, "m_aActors");

    switch (s_Location.status) {
        case PropertyLocation::Status::Ok: break;
        case PropertyLocation::Status::Missing: s_Out.why = "no m_aActors"; return s_Out;
        case PropertyLocation::Status::HasGetter: s_Out.why = "m_aActors has a getter"; return s_Out;
        case PropertyLocation::Status::Fault: s_Out.why = "m_aActors " + FaultText(s_Location.faultCode); return s_Out;
    }

    if (!s_Location.isArray || s_Location.elementSize != kElementSize ||
        std::string_view(s_Location.elementTypeName) != "TEntityRef<ZActor>") {
        s_Out.why = "m_aActors is not TArray<TEntityRef<ZActor>>";
        return s_Out;
    }

    struct {
        uintptr_t begin;
        uintptr_t end;
        uint64_t flags;
    } s_Array {};

    if (const uint32_t s_Code = ReadEngineBytes(s_Location.address, &s_Array, sizeof(s_Array))) {
        s_Out.why = "array " + FaultText(s_Code);
        return s_Out;
    }

    const auto s_Span =
        fr::ArrayElements(s_Location.address, s_Array.begin, s_Array.end, s_Array.flags, kElementSize, p_Max);

    if (!s_Span) {
        s_Out.why = "implausible array";
        return s_Out;
    }

    for (size_t i = 0; i < s_Span->count; ++i) {
        struct {
            ZEntityType** entity;
            const ZActor* actor;
        } s_Element {};
        static_assert(sizeof(s_Element) == kElementSize);

        if (const uint32_t s_Code = ReadEngineBytes(s_Span->begin + i * kElementSize, &s_Element, kElementSize)) {
            s_Out.why = fmt::format("element {} {}", i, FaultText(s_Code));
            s_Out.items.clear();
            return s_Out;
        }

        s_Out.items.emplace_back(s_Element.entity, s_Element.actor);
    }

    return s_Out;
}

std::optional<fr::Guid> ReadActorRepo(ZEntityType** p_Entity) {
    ZEntityType** s_Owner = nullptr;
    const bool s_HasOwner = !SafeOwning(p_Entity, &s_Owner) && s_Owner && s_Owner != p_Entity;

    for (int s_Which = 0; s_Which < (s_HasOwner ? 2 : 1); ++s_Which) {
        for (const char* s_Name : {"RepositoryId", "m_RepositoryId"}) {
            const PropertyLocation s_Location = LocateProperty(s_Which == 0 ? p_Entity : s_Owner, s_Name);

            if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != sizeof(fr::Guid))
                continue;

            fr::Guid s_Guid;

            if (!ReadEngineBytes(s_Location.address, &s_Guid, sizeof(s_Guid)))
                return s_Guid;
        }
    }

    return std::nullopt;
}

TargetPickers FindTargetPickers(const TArray<SBrickAllocationInfo>& p_Bricks, std::span<const size_t> p_Order,
                                std::unordered_set<ZEntityType**>* p_Seen) {
    TargetPickers s_Out;
    std::string s_Class;

    if (p_Order.empty())
        return s_Out;

    s_Out.walk = VisitEntitiesInOrder(p_Bricks, p_Order,
                                      [&](const VisitedEntity& p_Visit) {
                                          if (p_Seen)
                                              p_Seen->insert(p_Visit.entity);

                                          uint64_t s_Id = 0;

                                          if (ReadEntityClassName(p_Visit.entity, s_Class, &s_Id) ||
                                              s_Class != kTargetPickerClass)
                                              return false;

                                          s_Out.pickers.push_back(p_Visit.entity);
                                          return true;
                                      },
                                      kMissionWalkCap, {});

    return s_Out;
}
