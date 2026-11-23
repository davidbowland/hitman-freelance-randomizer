#pragma once

// Guarded reads of actors shared by the enforcer shuffle, the map variants
// and others: the actor interface of an entity, an actor's
// name, bits and kit type, its owning entity, and the
// actor manager's activated and spawned actor lists. Raw-pointer steps follow
// the guard pattern in Guard.h; each Safe* returns 0 or the fault code.

#include "EntityWalk.h"
#include "Guard.h"
#include "Guid.h"

#include <Glacier/ZEntity.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class ZActor;
class ZActorManager;
struct STypeID;

// The ZActorManager lists hold at most this many actors.
constexpr size_t kMaxManagerActors = 500;

// The entity's p_Type interface (null when it has none).
DWORD SafeInterface(ZEntityType** p_Entity, const STypeID* p_Type, void** p_Out);

// The bits and values of one actor, read guarded.
struct ActorCore {
    bool activated;
    bool enabled;
    bool alive;
    bool startEnabled;
    bool crowd;
    bool named;
    bool activeEnforcer;
    bool potentialEnforcer;
    bool dynamicEnforcer;
    bool contractTarget;
    int32_t runtimeId;
    fr::Guid outfit;
};

DWORD SafeActorCore(const ZActor* p_Actor, ActorCore* p_Out);

// m_sActorName, at most 79 characters.
struct NameOut {
    char text[80];
};

DWORD SafeActorName(const ZActor* p_Actor, NameOut* p_Out);

// The outfit kit's actor type (eAT_Civilian 0, eAT_Guard 1, ...).
struct KitTypeOut {
    bool hasKit;
    int32_t actorType;
};

DWORD SafeKitType(const ZActor* p_Actor, KitTypeOut* p_Out);

// The entity's owning entity (GetOwningEntity), null for none.
DWORD SafeOwning(ZEntityType** p_Entity, ZEntityType*** p_Out);

// An actor's name (quoted) and core bits, read guarded.
struct ActorFacts {
    std::string name;
    std::string plainName; // unquoted, empty when unreadable
    bool hasCore = false;
    ActorCore core {};
    uint32_t fault = 0;
};

ActorFacts ReadActor(const ZActor* p_Actor);

// The outfit kit's actor type name, "no kit", or the fault.
std::string ActorTypeText(const ZActor* p_Actor);

// An outfit kit of actor type guard.
bool IsGuard(const ZActor* p_Actor);

// One actor of a manager list: its entity and actor interface.
using ActorRef = std::pair<ZEntityType**, const ZActor*>;

// ZActorManager::m_activatedActors (at most kMaxManagerActors); *p_Total is
// the list's own count. Returns 0, or the fault code (nothing read).
uint32_t ReadActivatedActors(const ZActorManager* p_Manager, std::vector<ActorRef>& p_Out, size_t* p_Total);

// ZActorManager::m_SpawnedActors (at most kMaxManagerActors). Returns 0, or the fault code.
uint32_t ReadSpawnedActors(const ZActorManager* p_Manager, std::vector<ActorRef>& p_Out, size_t* p_Total);

// An entity's m_aActors (TArray<TEntityRef<ZActor>>, e.g. a ZVsTargetPicker's
// target pool), read raw: at most p_Max elements. why is empty when read.
struct ActorRefList {
    std::vector<ActorRef> items;
    std::string why;
};

ActorRefList ReadActorRefs(ZEntityType** p_Owner, size_t p_Max = 4'096);

// The repository id on the actor entity or its owner (RepositoryId, then
// m_RepositoryId; 16 bytes stored raw), or nothing.
std::optional<fr::Guid> ReadActorRepo(ZEntityType** p_Entity);

// Freelancer's target picker: its m_aActors is the mission's target pool.
inline constexpr const char* kTargetPickerClass = "ZVsTargetPicker";

// Entities a walk of one mission brick visits at most (a mild one has ~40,000).
inline constexpr size_t kMissionWalkCap = 500'000;

// The target pickers of the loaded bricks at p_Order (the mission brick), by
// one walk of at most kMissionWalkCap entities. p_Seen, when set, gets every
// entity the walk visits (the mission brick's own actors).
struct TargetPickers {
    std::vector<ZEntityType**> pickers;
    WalkStats walk {};
};

TargetPickers FindTargetPickers(const TArray<SBrickAllocationInfo>& p_Bricks, std::span<const size_t> p_Order,
                                std::unordered_set<ZEntityType**>* p_Seen = nullptr);
