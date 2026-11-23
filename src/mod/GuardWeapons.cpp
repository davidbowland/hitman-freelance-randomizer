// Guard weapons (see GuardWeapons.h), and the FreelancerRandomizer
// StartItemStreamIn detour that hands calls to it.

#include "GuardWeapons.h"

#include "ActorReads.h"
#include "EngineText.h"
#include "FreelancerRandomizer.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "Repository.h"
#include "Scene.h"
#include "SceneOf.h"
#include "SceneStage.h"

#include <Glacier/ZActor.h>
#include <Glacier/ZOutfit.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>

namespace {

constexpr uint32_t kMaxLinesPerScene = 2'000; // stream-in, skip and rebind lines
constexpr size_t kMaxKitKeys = 64;

double MsSince(std::chrono::steady_clock::time_point p_Start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_Start).count();
}

// Raw-pointer steps follow the guard pattern in Guard.h.

struct KeyRead {
    ZEntityType** entity;
    const ZItemRepositoryKeyEntity* key;
};

__declspec(noinline) void KeyRefOf(const TEntityRef<ZItemRepositoryKeyEntity>* p_Ref, KeyRead* p_Out) {
    *p_Out = {p_Ref->m_entityRef.m_pObj, p_Ref->m_pInterfaceRef};
}

__declspec(noinline) DWORD SafeKeyRef(const TEntityRef<ZItemRepositoryKeyEntity>* p_Ref, KeyRead* p_Out) {
    DWORD s_Code = 0;

    __try {
        KeyRefOf(p_Ref, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void KeyIdOf(const ZItemRepositoryKeyEntity* p_Key, fr::Guid* p_Out) {
    std::memcpy(p_Out, &p_Key->m_RepositoryId, sizeof(*p_Out));
}

__declspec(noinline) DWORD SafeKeyId(const ZItemRepositoryKeyEntity* p_Key, fr::Guid* p_Out) {
    DWORD s_Code = 0;

    __try {
        KeyIdOf(p_Key, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void KeyCountOf(const TArray<TEntityRef<ZItemRepositoryKeyEntity>>* p_Keys, size_t* p_Out) {
    *p_Out = p_Keys->size();
}

__declspec(noinline) DWORD SafeKeyCount(const TArray<TEntityRef<ZItemRepositoryKeyEntity>>* p_Keys, size_t* p_Out) {
    DWORD s_Code = 0;

    __try {
        KeyCountOf(p_Keys, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct HandlerActor {
    ZEntityType** entity;
    const ZActor* actor;
};

__declspec(noinline) void HandlerActorOf(const ZActorInventoryHandler* p_Handler, HandlerActor* p_Out) {
    *p_Out = {p_Handler->m_rActor.m_entityRef.m_pObj, p_Handler->m_rActor.m_pInterfaceRef};
}

__declspec(noinline) DWORD SafeHandlerActor(const ZActorInventoryHandler* p_Handler, HandlerActor* p_Out) {
    DWORD s_Code = 0;

    __try {
        HandlerActorOf(p_Handler, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct EntityIdOut {
    bool hasType;
    uint64_t id;
};

__declspec(noinline) void EntityIdOf(ZEntityType** p_Entity, EntityIdOut* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;
    *p_Out = {s_Type != nullptr, s_Type ? s_Type->m_nEntityID : 0};
}

__declspec(noinline) DWORD SafeEntityId(ZEntityType** p_Entity, EntityIdOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        EntityIdOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct KitOut {
    const ZGlobalOutfitKit* kit;
    int32_t actorType;
    int32_t rank;
    int32_t outfitType;
    bool weaponsAllowed;
};

__declspec(noinline) void KitOf(const ZActor* p_Actor, KitOut* p_Out) {
    const ZGlobalOutfitKit* s_Kit = p_Actor->m_rOutfit.m_pInterfaceRef;
    *p_Out = {s_Kit, 0, 0, 0, false};

    if (!s_Kit)
        return;

    p_Out->actorType = static_cast<int32_t>(s_Kit->m_eActorType);
    p_Out->rank = static_cast<int32_t>(s_Kit->m_eActorRank);
    p_Out->outfitType = static_cast<int32_t>(s_Kit->m_eOutfitType);
    p_Out->weaponsAllowed = s_Kit->m_bWeaponsAllowed;
}

__declspec(noinline) DWORD SafeKit(const ZActor* p_Actor, KitOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        KitOf(p_Actor, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void RolesOf(const ZActor* p_Actor, fr::RoleFlags* p_Out) {
    p_Out->contractTarget = p_Actor->m_bContractTarget;
    p_Out->contractTargetLive = p_Actor->m_bContractTargetLive;
    p_Out->contractTargetHidden = p_Actor->m_bContractTargetHidden;
    p_Out->activeEnforcer = p_Actor->m_bIsActiveEnforcer;
    p_Out->potentialEnforcer = p_Actor->m_bIsPotentialEnforcer;
    p_Out->dynamicEnforcer = p_Actor->m_bIsDynamicEnforcer;
}

__declspec(noinline) DWORD SafeRoles(const ZActor* p_Actor, fr::RoleFlags* p_Out) {
    DWORD s_Code = 0;

    __try {
        RolesOf(p_Actor, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct KitKeysOut {
    size_t total;
    size_t count;
    KeyRead keys[kMaxKitKeys];
};

__declspec(noinline) void KitKeysOf(const ZGlobalOutfitKit* p_Kit, KitKeysOut* p_Out) {
    const auto& s_Keys = p_Kit->m_AvailableWeaponKeys;
    p_Out->total = s_Keys.size();
    p_Out->count = std::min(p_Out->total, kMaxKitKeys);

    for (size_t i = 0; i < p_Out->count; ++i)
        p_Out->keys[i] = {s_Keys[i].m_entityRef.m_pObj, s_Keys[i].m_pInterfaceRef};
}

__declspec(noinline) DWORD SafeKitKeys(const ZGlobalOutfitKit* p_Kit, KitKeysOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        KitKeysOf(p_Kit, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// One key's entity and repository id, read guarded.
struct KeyFacts {
    KeyRead ref {};
    std::optional<fr::Guid> id;
    uint32_t fault = 0;
};

KeyFacts ReadKey(const TEntityRef<ZItemRepositoryKeyEntity>& p_Ref) {
    KeyFacts s_Out;

    if ((s_Out.fault = SafeKeyRef(&p_Ref, &s_Out.ref)))
        return s_Out;

    fr::Guid s_Id;

    if (s_Out.ref.key && !(s_Out.fault = SafeKeyId(s_Out.ref.key, &s_Id)))
        s_Out.id = s_Id;

    return s_Out;
}

// What a stream-in reads of its actor, every step guarded.
struct StreamActor {
    std::optional<uint64_t> entityId;
    std::string name;
    std::string kitText;
    std::string rolesText;
    KitOut kit {};
    bool hasKit = false;
    bool rolesKnown = false;
    fr::RoleFlags roles;
};

StreamActor ReadStreamActor(const HandlerActor& p_Ref) {
    StreamActor s_Out;
    EntityIdOut s_Id {};

    if (p_Ref.entity && !SafeEntityId(p_Ref.entity, &s_Id) && s_Id.hasType)
        s_Out.entityId = s_Id.id;

    if (!p_Ref.actor) {
        s_Out.name = "<no actor>";
        s_Out.kitText = "<no actor>";
        s_Out.rolesText = "<no actor>";
        return s_Out;
    }

    NameOut s_Name {};

    if (const DWORD s_Code = SafeActorName(p_Ref.actor, &s_Name))
        s_Out.name = FaultText(s_Code);
    else
        s_Out.name = fmt::format("\"{}\"", s_Name.text);

    if (const DWORD s_Code = SafeKit(p_Ref.actor, &s_Out.kit)) {
        s_Out.kitText = "kit " + FaultText(s_Code);
    }
    else if (!s_Out.kit.kit) {
        s_Out.kitText = "<no kit>";
    }
    else {
        s_Out.hasKit = true;
        s_Out.kitText = fmt::format("kit {} {} {} weapons allowed {}", fr::ActorTypeName(s_Out.kit.actorType),
                                    fr::ActorRankName(s_Out.kit.rank), fr::OutfitTypeName(s_Out.kit.outfitType),
                                    s_Out.kit.weaponsAllowed);
    }

    if (const DWORD s_Code = SafeRoles(p_Ref.actor, &s_Out.roles)) {
        s_Out.rolesText = FaultText(s_Code);
    }
    else {
        s_Out.rolesKnown = true;
        s_Out.rolesText = fr::DescribeRoles(s_Out.roles);
    }

    return s_Out;
}

// A guard kit's weapon keys (at most kMaxKitKeys) and their repository ids,
// read guarded; ids[i] is empty when keys[i] is null or its id unreadable.
struct KitWeapons {
    uint32_t fault = 0;
    size_t total = 0;
    std::vector<KeyRead> keys;
    std::vector<fr::Guid> ids;
};

KitWeapons ReadKitWeapons(const ZGlobalOutfitKit* p_Kit) {
    KitWeapons s_Out;
    KitKeysOut s_Keys {};

    if ((s_Out.fault = SafeKitKeys(p_Kit, &s_Keys)))
        return s_Out;

    s_Out.total = s_Keys.total;

    for (size_t i = 0; i < s_Keys.count; ++i) {
        fr::Guid s_Id;

        if (!s_Keys.keys[i].key || !s_Keys.keys[i].entity || SafeKeyId(s_Keys.keys[i].key, &s_Id))
            s_Id = fr::Guid {};

        s_Out.keys.push_back(s_Keys.keys[i]);
        s_Out.ids.push_back(s_Id);
    }

    return s_Out;
}

fr::BucketCounts CountBuckets(const std::vector<fr::PoolWeapon>& p_Weapons) {
    fr::BucketCounts s_Out {};

    for (const auto& s_Weapon : p_Weapons)
        ++s_Out[static_cast<size_t>(s_Weapon.bucket)];

    return s_Out;
}

std::string EntityIdText(const std::optional<uint64_t>& p_Id) {
    return p_Id ? fmt::format("{:016x}", *p_Id) : std::string("<unknown>");
}

} // namespace

GuardWeapons::GuardWeapons(ProRepository& p_Repo) : m_Repo(p_Repo) {}

void GuardWeapons::OnSceneLoad(std::string_view p_Scene, std::string_view p_Type,
                               const std::optional<fr::SeedResolution>& p_Seed, bool p_On) {
    m_GameThread.store(GetCurrentThreadId(), std::memory_order_relaxed);

    auto s_State = std::make_unique<SceneState>();
    s_State->scene = std::string(p_Scene);
    s_State->freelancer = fr::IsFreelancerScene(p_Type);
    s_State->mission = fr::IsFreelancerMission(p_Type, p_Scene);
    s_State->on = p_On;
    s_State->seed = s_State->mission ? p_Seed : std::nullopt;

    if (s_State->mission) {
        LogTo(fr::LogTopic::Targets, "weapons: mission {}: guard_weapons {}, seed {}", s_State->scene, p_On,
              s_State->seed ? fmt::format("{} ({})", s_State->seed->seed, fr::SeedSourceName(s_State->seed->source))
                            : std::string("not resolved"));
    }

    const bool s_Freelancer = s_State->freelancer;
    Publish(std::move(s_State));

    if (s_Freelancer)
        EnsureItemCache();
}

void GuardWeapons::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) {
    m_Stage.store(static_cast<int>(p_Stage), std::memory_order_relaxed);

    if (p_Stage == ESceneLoadingStage::eLoading_SceneStarted)
        CheckSceneContext(p_Context);
}

void GuardWeapons::OnSceneClear() {
    if (const SceneState* s_State = m_State.load(std::memory_order_acquire); s_State && s_State->freelancer)
        LogCounts("scene clear");

    Publish(nullptr);
}

// A new scene (or none): per-scene counts and decisions start over.
void GuardWeapons::Publish(std::unique_ptr<SceneState> p_State) {
    ResetScene();
    const SceneState* s_State = p_State.get();

    if (p_State)
        m_States.push_back(std::move(p_State));

    m_State.store(s_State, std::memory_order_release);
}

void GuardWeapons::ResetScene() {
    m_Calls.store(0, std::memory_order_relaxed);
    m_Lines.store(0, std::memory_order_relaxed);
    m_Rebinds.store(0, std::memory_order_relaxed);
    m_Kept.store(0, std::memory_order_relaxed);
    m_Repeats.store(0, std::memory_order_relaxed);

    for (auto& s_Count : m_Skips)
        s_Count.store(0, std::memory_order_relaxed);

    for (auto& s_Count : m_Transitions)
        s_Count.store(0, std::memory_order_relaxed);

    std::lock_guard s_DecisionsLock(m_DecisionsMutex);
    m_Decisions.clear();
}

// Repository id -> Title, InventoryCategoryIcon, ItemType, built from one
// pro.repo pass the first time a Freelancer scene loads with pro.repo ready.
void GuardWeapons::EnsureItemCache() {
    if (m_ItemCache)
        return;

    std::string s_Why;

    if (!m_Repo.Ready(s_Why)) {
        LogTo(fr::LogTopic::Targets, "weapons: item cache not built: {}; retried at the next Freelancer scene", s_Why);
        return;
    }

    const auto s_Start = std::chrono::steady_clock::now();
    std::vector<RepoItemEntry> s_Entries;
    size_t s_Overflow = 0;
    size_t s_Scanned = 0;

    if (const uint32_t s_Code = m_Repo.ScanItems(s_Entries, &s_Overflow, &s_Scanned)) {
        LogTo(fr::LogTopic::Targets,
              "weapons: item cache not built: the pro.repo scan faulted (exception 0x{:08X}); retried at the next "
              "Freelancer scene",
              s_Code);
        return;
    }

    std::vector<fr::ItemInfo> s_Items;
    s_Items.reserve(s_Entries.size());

    for (const auto& s_Entry : s_Entries)
        s_Items.push_back({s_Entry.guid, s_Entry.title, s_Entry.icon, s_Entry.itemType});

    m_ItemCache = std::make_unique<fr::ItemCache>(std::move(s_Items));
    m_Items.store(m_ItemCache.get(), std::memory_order_release);

    LogTo(fr::LogTopic::Targets, "weapons: item cache: {} items from {} pro.repo entries{}, {:.1f} ms",
          m_ItemCache->Size(), s_Scanned,
          s_Overflow ? fmt::format(" ({} more over the {} limit)", s_Overflow, ProRepository::kMaxItemEntries)
                     : std::string(),
          MsSince(s_Start));
}

// A scene start whose context names another scene than the last load (a load
// that bypassed LoadScene): that scene is published, without a seed.
void GuardWeapons::CheckSceneContext(ZEntitySceneContext* p_Context) {
    const SceneState* s_Current = m_State.load(std::memory_order_acquire);

    if (!p_Context)
        return;

    const std::string s_Scene =
        SceneOf(p_Context); // a full variant (Landslide, The Author) counts as its contract's scene

    if (s_Scene.empty() || (s_Current && s_Current->scene == s_Scene))
        return;

    const std::string_view s_Type = p_Context->m_SceneInitParameters.m_Type.ToStringView();
    LogTo(fr::LogTopic::Targets,
          "weapons: scene start of {} (type \"{}\") without a matching load; no seed for weapon picks", s_Scene,
          s_Type);

    auto s_State = std::make_unique<SceneState>();
    s_State->scene = std::string(s_Scene);
    s_State->freelancer = fr::IsFreelancerScene(s_Type);
    s_State->mission = fr::IsFreelancerMission(s_Type, s_Scene);
    s_State->on = s_Current && s_Current->on;
    Publish(std::move(s_State));
}

void GuardWeapons::LogCounts(const char* p_When) {
    std::string s_Skips;

    for (size_t i = 0; i < fr::kWeaponSkipCount; ++i) {
        s_Skips +=
            fmt::format("{}{} {}", s_Skips.empty() ? "" : ", ", fr::WeaponSkipName(static_cast<fr::WeaponSkip>(i)),
                        m_Skips[i].load(std::memory_order_relaxed));
    }

    fr::TransitionCounts s_Transitions {};

    for (size_t i = 0; i < s_Transitions.size(); ++i)
        s_Transitions[i] = m_Transitions[i].load(std::memory_order_relaxed);

    LogTo(fr::LogTopic::Targets,
          "weapons counts at {}: {} stream-in calls, {} rebinds, {} kept, {} repeats; rebinds by bucket: {}; skips: {}",
          p_When, m_Calls.load(std::memory_order_relaxed), m_Rebinds.load(std::memory_order_relaxed),
          m_Kept.load(std::memory_order_relaxed), m_Repeats.load(std::memory_order_relaxed),
          fr::DescribeTransitions(s_Transitions), s_Skips);
}

// True while this scene's stream-in, skip and rebind lines are under the cap;
// the first line over it says so.
bool GuardWeapons::TakeLine() {
    const uint32_t s_Line = m_Lines.fetch_add(1, std::memory_order_relaxed) + 1;

    if (s_Line == kMaxLinesPerScene + 1)
        LogTo(fr::LogTopic::Targets,
              "weapons: {} lines logged this scene; further stream-in lines are not logged (still counted)",
              kMaxLinesPerScene);

    return s_Line <= kMaxLinesPerScene;
}

// "<title>" [<InventoryCategoryIcon>], from the item cache.
std::string GuardWeapons::ItemText(const fr::Guid& p_Id) const {
    const fr::ItemCache* s_Items = m_Items.load(std::memory_order_acquire);

    if (!s_Items)
        return "<item cache not built>";

    const fr::ItemInfo* s_Item = s_Items->Find(p_Id);
    return s_Item ? fmt::format("\"{}\" [{}]", s_Item->title, s_Item->icon) : std::string("<not in item cache>");
}

// Records an entity's first decision of the scene (later ones are ignored).
void GuardWeapons::Decide(const std::optional<uint64_t>& p_EntityId, Decision p_Decision) {
    if (!p_EntityId)
        return;

    std::lock_guard s_Lock(m_DecisionsMutex);
    m_Decisions.emplace(*p_EntityId, std::move(p_Decision));
}

// A later stream-in of an entity already decided this scene: the same key
// (if its repository id still reads as it did), or the call left alone.
std::optional<TEntityRef<ZItemRepositoryKeyEntity>>
GuardWeapons::Repeat(const Decision& p_Decision, const std::string& p_Name, uint64_t p_EntityId) {
    m_Repeats.fetch_add(1, std::memory_order_relaxed);

    if (!p_Decision.key) {
        if (TakeLine())
            LogTo(fr::LogTopic::Targets, "weapon repeat: {} {:016x}: left alone, as at its first stream-in", p_Name,
                  p_EntityId);

        return std::nullopt;
    }

    const TEntityRef<ZItemRepositoryKeyEntity>& s_Key = *p_Decision.key;
    fr::Guid s_KeyId;

    if (!s_Key.m_pInterfaceRef || !s_Key.m_entityRef.m_pObj || SafeKeyId(s_Key.m_pInterfaceRef, &s_KeyId) ||
        s_KeyId != p_Decision.id) {
        m_Skips[static_cast<size_t>(fr::WeaponSkip::KeyChanged)].fetch_add(1, std::memory_order_relaxed);

        if (TakeLine())
            LogTo(fr::LogTopic::Targets, "weapon repeat: {} {:016x}: key changed; left alone", p_Name, p_EntityId);

        return std::nullopt;
    }

    if (TakeLine())
        LogTo(fr::LogTopic::Targets, "weapon repeat: {} {:016x}: {} again", p_Name, p_EntityId,
              ItemText(p_Decision.id));

    return s_Key;
}

std::optional<TEntityRef<ZItemRepositoryKeyEntity>> GuardWeapons::OnStreamIn(
    ZActorInventoryHandler* p_Handler, const TArray<TEntityRef<ZItemRepositoryKeyEntity>>& p_InventoryKeys,
    const TEntityRef<ZItemRepositoryKeyEntity>& p_WeaponKey, const TEntityRef<ZItemRepositoryKeyEntity>& p_GrenadeKey) {
    const SceneState* s_State = m_State.load(std::memory_order_acquire);

    if (!s_State || !s_State->freelancer)
        return std::nullopt;

    const uint32_t s_Call = m_Calls.fetch_add(1, std::memory_order_relaxed) + 1;
    const uint32_t s_Thread = GetCurrentThreadId();

    HandlerActor s_ActorRef {};
    const DWORD s_HandlerCode = p_Handler ? SafeHandlerActor(p_Handler, &s_ActorRef) : 0;
    const StreamActor s_Actor = ReadStreamActor(s_ActorRef);
    const KeyFacts s_Weapon = ReadKey(p_WeaponKey);
    const KeyFacts s_Grenade = ReadKey(p_GrenadeKey);
    size_t s_InventoryCount = 0;
    const DWORD s_InventoryCode = SafeKeyCount(&p_InventoryKeys, &s_InventoryCount);

    const auto s_KeyText = [this](const KeyFacts& p_Key, bool p_WithItem) {
        if (p_Key.fault)
            return FaultText(p_Key.fault);
        if (!p_Key.ref.key)
            return std::string("none");
        if (!p_Key.id)
            return std::string("<unreadable id>");
        return p_WithItem ? fr::FormatGuid(*p_Key.id) + " " + ItemText(*p_Key.id) : fr::FormatGuid(*p_Key.id);
    };

    if (TakeLine()) {
        const int s_Stage = m_Stage.load(std::memory_order_relaxed);
        const std::string s_StageText =
            s_Stage < 0 ? std::string("no loading stage yet")
                        : fmt::format("{} ({})", StageName(static_cast<ESceneLoadingStage>(s_Stage)), s_Stage);

        LogTo(fr::LogTopic::Targets,
              "weapons stream-in #{} at {}, thread {} ({}): actor {} {}: {}; roles {}; weapon {}; grenade {}; "
              "inventory keys {}",
              s_Call, s_StageText, s_Thread,
              s_Thread == m_GameThread.load(std::memory_order_relaxed) ? "game thread" : "not the game thread",
              s_HandlerCode ? "handler " + FaultText(s_HandlerCode) : s_Actor.name, EntityIdText(s_Actor.entityId),
              s_Actor.kitText, s_Actor.rolesText, s_KeyText(s_Weapon, true), s_KeyText(s_Grenade, false),
              s_InventoryCode ? FaultText(s_InventoryCode) : fmt::format("{}", s_InventoryCount));
    }

    if (!s_State->mission)
        return std::nullopt;

    if (!s_State->on || !s_Weapon.ref.key)
        return std::nullopt;

    // An entity already decided this scene repeats that decision, whatever its
    // role flags say now (they are not yet set at load-time stream-ins).
    if (s_Actor.entityId) {
        std::optional<Decision> s_Earlier;
        {
            std::lock_guard s_Lock(m_DecisionsMutex);

            if (const auto s_It = m_Decisions.find(*s_Actor.entityId); s_It != m_Decisions.end())
                s_Earlier = s_It->second;
        }

        if (s_Earlier)
            return Repeat(*s_Earlier, s_Actor.name, *s_Actor.entityId);
    }

    const fr::Guid s_Current = s_Weapon.id.value_or(fr::Guid {});
    const fr::ItemCache* s_Items = m_Items.load(std::memory_order_acquire);
    const fr::ItemInfo* s_Item = s_Items && !s_Current.IsEmpty() ? s_Items->Find(s_Current) : nullptr;
    const fr::WeaponBucket s_CurrentBucket = fr::BucketOf(s_Item ? s_Item->icon : std::string());

    fr::StreamInFacts s_Facts;
    s_Facts.seedResolved = s_State->seed.has_value();
    s_Facts.hasKit = s_Actor.hasKit;
    s_Facts.actorType = s_Actor.kit.actorType;
    s_Facts.weaponsAllowed = s_Actor.kit.weaponsAllowed;
    s_Facts.rolesKnown = s_Actor.rolesKnown;
    s_Facts.roles = s_Actor.roles;
    s_Facts.hasEntityId = s_Actor.entityId.has_value();
    s_Facts.currentIsCandidate = s_Item && fr::IsCandidateBucket(s_CurrentBucket); // unreadable or empty id: no item

    std::optional<fr::WeaponSkip> s_Skip = fr::WeaponEligibility(s_Facts);
    std::string s_SkipDetail;
    std::optional<size_t> s_Pick; // into s_KitPool
    KitWeapons s_Kit;
    fr::WeaponPool s_KitPool;
    size_t s_Candidates = 0;

    // Candidates are this guard's own kit weapons (pistol, SMG, assault rifle,
    // shotgun) in repository id order, so the pick depends on nothing but the
    // seed, scene, entity id and the kit (not on which actors streamed in
    // before, nor on the current weapon).
    if (!s_Skip) {
        s_Kit = ReadKitWeapons(s_Actor.kit.kit);

        if (s_Kit.fault) {
            s_Skip = fr::WeaponSkip::NoCandidate;
            s_SkipDetail = " (reading the kit's weapon keys: " + FaultText(s_Kit.fault) + ")";
        }
        else {
            s_KitPool = fr::BuildPool(s_Kit.ids, s_Items);
            s_Candidates = fr::WeaponCandidates(s_KitPool.weapons).size();
            s_Pick = fr::PickWeapon(s_KitPool.weapons,
                                    fr::WeaponPickHash(s_State->seed->seed, s_State->scene, *s_Actor.entityId));

            if (!s_Pick) {
                s_Skip = fr::WeaponSkip::NoCandidate;
                s_SkipDetail = fmt::format(" (kit {} weapons: {})", s_KitPool.weapons.size(),
                                           fr::DescribeBucketCounts(CountBuckets(s_KitPool.weapons)));
            }
        }
    }

    // The pick is the weapon the guard already has: nothing to rebind.
    const bool s_Kept = s_Pick && s_KitPool.weapons[*s_Pick].id == s_Current;

    // The picked key must still read, guarded, as the repository id it had
    // when the kit was read.
    if (s_Pick && !s_Kept) {
        const KeyRead& s_Key = s_Kit.keys[s_KitPool.sources[*s_Pick]];
        fr::Guid s_KeyId;

        if (!s_Key.key || !s_Key.entity || SafeKeyId(s_Key.key, &s_KeyId) || s_KeyId != s_KitPool.weapons[*s_Pick].id) {
            s_Skip = fr::WeaponSkip::KeyChanged;
            s_Pick.reset();
        }
    }

    if (s_Skip) {
        m_Skips[static_cast<size_t>(*s_Skip)].fetch_add(1, std::memory_order_relaxed);
        Decide(s_Actor.entityId, {});

        if (TakeLine()) {
            LogTo(fr::LogTopic::Targets, "weapons skip: {} {}: {}{}", s_Actor.name, EntityIdText(s_Actor.entityId),
                  fr::WeaponSkipName(*s_Skip), s_SkipDetail);
        }

        return std::nullopt;
    }

    if (s_Kept) {
        m_Kept.fetch_add(1, std::memory_order_relaxed);
        Decide(s_Actor.entityId, {});

        if (TakeLine()) {
            LogTo(fr::LogTopic::Targets, "weapon kept: {} {} (entity {}, {} {}; kit {} weapons, {} candidates)",
                  s_Actor.name, ItemText(s_Current), EntityIdText(s_Actor.entityId), fr::FormatGuid(s_Current),
                  fr::BucketName(s_CurrentBucket), s_KitPool.weapons.size(), s_Candidates);
        }

        return std::nullopt;
    }

    const fr::PoolWeapon& s_New = s_KitPool.weapons[*s_Pick];
    m_Rebinds.fetch_add(1, std::memory_order_relaxed);
    m_Transitions[fr::TransitionIndex(s_CurrentBucket, s_New.bucket)].fetch_add(1, std::memory_order_relaxed);

    if (TakeLine()) {
        LogTo(fr::LogTopic::Targets, "weapon: {} {} -> {} (entity {}, {} {} -> {} {}; kit {} weapons, {} candidates)",
              s_Actor.name, ItemText(s_Current), ItemText(s_New.id), EntityIdText(s_Actor.entityId),
              fr::FormatGuid(s_Current), fr::BucketName(s_CurrentBucket), fr::FormatGuid(s_New.id),
              fr::BucketName(s_New.bucket), s_KitPool.weapons.size(), s_Candidates);
    }

    const KeyRead& s_Ref = s_Kit.keys[s_KitPool.sources[*s_Pick]];
    TEntityRef<ZItemRepositoryKeyEntity> s_Key;
    s_Key.m_entityRef = ZEntityRef(s_Ref.entity);
    s_Key.m_pInterfaceRef = const_cast<ZItemRepositoryKeyEntity*>(s_Ref.key);
    Decide(s_Actor.entityId, {s_Key, s_New.id});
    return s_Key;
}

// Hands each call to GuardWeapons. With a pick, the rebind is the last step:
// the original is called with a local copy of the chosen key (never assigning
// through rWeaponKey, never touching the other arguments or any repository
// id) and the call returns; it runs outside the try so a failure in it can
// never lead to the original running twice. Anything that fails before it
// leaves the call unchanged (Continue).
DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, void, ZActorInventoryHandler_StartItemStreamIn, ZActorInventoryHandler* th,
                     TArray<TEntityRef<ZItemRepositoryKeyEntity>>& rInventoryKeys,
                     TEntityRef<ZItemRepositoryKeyEntity>& rWeaponKey,
                     TEntityRef<ZItemRepositoryKeyEntity>& rGrenadeKey) {
    std::optional<TEntityRef<ZItemRepositoryKeyEntity>> s_Pick;

    if (!Contained("guard weapons", "StartItemStreamIn",
                   [&] { s_Pick = m_GuardWeapons.OnStreamIn(th, rInventoryKeys, rWeaponKey, rGrenadeKey); }))
        s_Pick.reset();

    if (!s_Pick)
        return HookResult<void>(HookAction::Continue());

    // CallOriginal calls the game function directly and Return() stops
    // Hook::Call's detour loop, so detours later than ours in this hook's list
    // (other mods, e.g. the SDK Randomizer) are skipped for a rebound call.
    // Calls left unchanged (Continue) still reach them.
    TEntityRef<ZItemRepositoryKeyEntity> s_WeaponKey = *s_Pick;
    p_Hook->CallOriginal(th, rInventoryKeys, s_WeaponKey, rGrenadeKey);
    return HookResult<void>(HookAction::Return());
}
