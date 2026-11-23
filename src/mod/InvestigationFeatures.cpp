// footsteps_investigate and backup_guard (see InvestigationFeatures.h). Every raw engine read,
// write and call follows the guard pattern in Guard.h.

#include "InvestigationFeatures.h"

#include "ActorReads.h"
#include "BlueprintRead.h"
#include "EngineText.h"
#include "EntityWalk.h"
#include "ObjectValue.h"
#include "Guard.h"
#include "JsonScan.h" // fr::RedactJson
#include "Merces.h"   // fr::JsonEventName, fr::OneLine
#include "ModLog.h"
#include "EntityReads.h"
#include "../core/SceneKinds.h" // fr::BehaviorName
#include "Scene.h"
#include "SceneOf.h"

#include <Functions.h>
#include <Globals.h>

#include <Glacier/EntityFactory.h>
#include <Glacier/Hash.h>
#include <Glacier/SExternalReferences.h>
#include <Glacier/ZActor.h>
#include <Glacier/ZBehavior.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZResource.h>
#include <Glacier/ZResourceID.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <random>

namespace {

constexpr uint64_t kHelperRid = 0x00A824E80BF70DFEull;    // setpiecehelper_investigatecurious.entitytemplate
constexpr uint64_t kAttractorRid = 0x00E8BB33DEDD9125ull; // aisoundattractor.entitytemplate
constexpr const char* kHelperNames[] = {"investigate-curious helper", "sound attractor"};
constexpr size_t kMaxQueue = 256;
constexpr size_t kMaxHelperWatches = 64;   // the helper and its sub-entities
constexpr double kIndexRebuildEvery = 5.0; // s of play between repository-id index rebuilds on a miss
constexpr uint32_t kMaxScanLines = 600;    // NPC scan lines per scene (within kInvestigationMaxLines)
constexpr uint32_t kEntityActivated = 4;   // ZEntityImpl::EEntityFlags::ENTITYFLAG_ACTIVATED

std::string Join(const std::vector<std::string>& p_Items) {
    std::string s_Out;

    for (const std::string& s_Item : p_Items)
        s_Out += (s_Out.empty() ? "" : ", ") + s_Item;

    return s_Out;
}

std::string PosText(const std::optional<std::array<float, 3>>& p_At) {
    return p_At ? fmt::format("({:.1f}, {:.1f}, {:.1f})", (*p_At)[0], (*p_At)[1], (*p_At)[2])
                : std::string("(position unread)");
}

std::string DistText(const std::optional<std::array<float, 3>>& p_A, const std::optional<std::array<float, 3>>& p_B) {
    return p_A && p_B ? fmt::format("{:.1f} m", fr::Distance(*p_A, *p_B)) : std::string("?");
}

constexpr std::string_view kTag = "investigations";

// --- Guarded engine steps -------------------------------------------------------------------------------------

// GetResourcePtr for a template; the reference is kept (the index taken out
// of the pointer before it is destroyed), as CrateGuns holds item resources.
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
    int32_t status;
    void* data;
    long refs;
};

__declspec(noinline) void ResourceStateOf(int32_t p_Index, ResourceState* p_Out) {
    const auto& s_Info = (*Globals::ResourceContainer)->m_resources[p_Index];
    p_Out->status = static_cast<int32_t>(s_Info.status);
    p_Out->data = s_Info.resourceData;
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

const char* StatusWord(int32_t p_Status) {
    switch (p_Status) {
        case RESOURCE_STATUS_UNKNOWN: return "unknown";
        case RESOURCE_STATUS_LOADING: return "loading";
        case RESOURCE_STATUS_INSTALLING: return "installing";
        case RESOURCE_STATUS_FAILED: return "failed";
        case RESOURCE_STATUS_VALID: return "valid";
        default: return "?";
    }
}

std::string ResourceText(int32_t p_Index) {
    if (p_Index < 0)
        return "not requested";

    ResourceState s_State {};

    if (const DWORD s_Code = SafeResourceState(p_Index, &s_State))
        return fmt::format("index {}, state unreadable {}", p_Index, FaultText(s_Code));

    return fmt::format("index {}, {}, data {}, {} references", p_Index, StatusWord(s_State.status),
                       s_State.data ? "present" : "none", s_State.refs);
}

struct FlagsOut {
    bool entity;
    uint32_t flags;
};

__declspec(noinline) void FlagsOf(ZEntityType** p_Entity, FlagsOut* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    p_Out->entity = s_Entity != nullptr;
    p_Out->flags = s_Entity ? s_Entity->m_nEntityFlags : 0;
}

__declspec(noinline) DWORD SafeFlags(ZEntityType** p_Entity, FlagsOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        FlagsOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void ActivateOf(ZEntityType** p_Entity) {
    if (ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity())
        s_Entity->Activate(0);
}

__declspec(noinline) DWORD SafeActivate(ZEntityType** p_Entity) {
    DWORD s_Code = 0;

    __try {
        ActivateOf(p_Entity);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void DeleteOf(ZEntityType** p_Entity) {
    SExternalReferences s_ExternalRefs;
    Functions::ZEntityManager_DeleteEntity->Call(Globals::EntityManager, ZEntityRef(p_Entity), s_ExternalRefs);
}

__declspec(noinline) DWORD SafeDelete(ZEntityType** p_Entity) {
    DWORD s_Code = 0;

    __try {
        DeleteOf(p_Entity);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct BehaviorOut {
    bool hasBehavior;
    int32_t type;
};

__declspec(noinline) void BehaviorRead(const ZBehaviorService* p_Service, int32_t p_RuntimeId, BehaviorOut* p_Out) {
    const SBehaviorBase* s_Behavior = p_Service->m_aBehaviorStates[static_cast<size_t>(p_RuntimeId)].m_pCurrentBehavior;
    p_Out->hasBehavior = s_Behavior != nullptr;
    p_Out->type = s_Behavior ? static_cast<int32_t>(s_Behavior->eBehaviorType) : 0;
}

__declspec(noinline) DWORD SafeBehaviorRead(const ZBehaviorService* p_Service, int32_t p_RuntimeId,
                                            BehaviorOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        BehaviorRead(p_Service, p_RuntimeId, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

uint32_t PinId(std::string_view p_Name) {
    return Hash::Crc32(p_Name.data(), p_Name.size());
}

const std::map<uint32_t, std::string_view>& PinNames() {
    static const std::map<uint32_t, std::string_view> s_Names = [] {
        std::map<uint32_t, std::string_view> s_Out;

        for (const std::string_view s_Name : fr::InvestigationPinNames())
            s_Out.emplace(PinId(s_Name), s_Name);

        return s_Out;
    }();

    return s_Names;
}

std::string PayloadText(const ZObjectRef& p_Data) {
    ObjectValue s_Value {};

    if (const DWORD s_Code = SafeReadObjectValue(&p_Data, &s_Value))
        return FaultText(s_Code);

    return FormatObjectValue(s_Value);
}

} // namespace

template <class... A>
void InvestigationFeatures::Line(bool p_Helper, fmt::format_string<A...> p_Format, A&&... p_Args) {
    const bool s_On = p_Helper && (m_HelperIsBackup.load(std::memory_order_relaxed) ? m_BackupOn : m_StepOn);
    LogIf(s_On && TakeLine(), p_Format, std::forward<A>(p_Args)...);
}

void InvestigationFeatures::Configure(bool p_FootstepInvestigate, int p_FootstepChance, bool p_BackupGuard,
                                      fr::BackupDelays p_BackupDelays, bool p_BackupNotice) {
    m_StepOn = p_FootstepInvestigate;
    m_StepChance = fr::ClampFootstepInvestigateChance(p_FootstepChance);
    m_BackupOn = p_BackupGuard;
    m_BackupDelays = fr::ClampBackupDelays(p_BackupDelays.min, p_BackupDelays.max);
    m_NoticeOn = p_BackupNotice;
}

std::string InvestigationFeatures::SeedText() const {
    return m_Seed ? fmt::format("seed {} ({})", m_Seed->seed, fr::SeedSourceName(m_Seed->source))
                  : std::string("seed 0 (not resolved)");
}

bool InvestigationFeatures::TakeLine() {
    const uint32_t s_Line = m_Lines.fetch_add(1, std::memory_order_relaxed) + 1;

    if (s_Line == fr::kInvestigationMaxLines + 1)
        LogIf(m_StepOn || m_BackupOn, "investigations: {} lines logged this scene; further ones are not logged",
              fr::kInvestigationMaxLines);

    return s_Line <= fr::kInvestigationMaxLines;
}

bool InvestigationFeatures::HelperWindow() const {
    return HelperOut() && m_Now - m_Helper.spawnedAt <= fr::kFootstepWatchSeconds;
}

void InvestigationFeatures::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                           const std::optional<fr::SeedResolution>& p_Seed) noexcept {
    if (Wanted() || m_Mission)
        Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context, p_Seed); });
}

void InvestigationFeatures::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                         const std::optional<fr::SeedResolution>& p_Seed) {
    if (!p_Context)
        return;

    if (p_Stage == ESceneLoadingStage::eLoading_AssetsLoaded) {
        Forget();
        m_Seed.reset();
    }

    // The latest seed the load has (the mod resolves it before the scene loads, or at scene start).
    if (p_Seed)
        m_Seed = p_Seed;

    if (p_Stage == ESceneLoadingStage::eLoading_AssetsLoaded)
        return;

    if (p_Stage != ESceneLoadingStage::eLoading_ScenePlaying || m_Playing || !Wanted())
        return;

    const std::string s_Logical = SceneOf(p_Context);
    const std::string_view s_Type = p_Context->m_SceneInitParameters.m_Type.ToStringView();

    if (!fr::IsFreelancerMission(s_Type, s_Logical))
        return;

    Forget();
    m_Mission = true;
    m_Playing = true;
    m_Scene = SceneLabelOf(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView());
    m_RollScene = s_Logical;
    m_Lines.store(0, std::memory_order_relaxed);
    m_PlaySeconds.store(0.0, std::memory_order_relaxed);

    if (m_StepOn || m_BackupOn)
        Acquire();

    if (m_StepOn)
        LogIf(
            true,
            "footstep investigate: {}: armed: a heard footstep (Investigate_Curious, {}) brings the {} "
            "with chance {}% (one roll per hearing that passes the cooldowns, drawn at the time), at most every {:.0f} "
            "s "
            "and every {:.0f} s per NPC, one at a time, {} per scene; a hit spawns it at once where 47 was heard with "
            "a curious sound ({}); unanswered in {:.0f} s, his reaction is waited out (at most {:.0f} s after the "
            "hearing), then it goes again with a thrown item's landing ({}); {} {:016X}: {}; {} {:016X}: {}",
            m_Scene, fr::InvestigationTypeName(fr::kInvestigationFootsteps), kHelperNames[0], m_StepChance,
            fr::kFootstepGlobalCooldown, fr::kFootstepActorCooldown, fr::kFootstepMaxPerScene,
            fr::SoundEventName(fr::kFootstepCuriousEvent), fr::kFootstepCuriousWait, fr::kFootstepWaitMax,
            fr::SoundEventName(fr::kFootstepNoiseEvent), kHelperNames[0], kHelperRid, ResourceText(m_Resources[0]),
            kHelperNames[1], kAttractorRid, ResourceText(m_Resources[1]));

    LogIf(m_BackupOn,
          "backup: {}: armed: a guard who joins for a noise ({} or {}) and is taken down after a radio "
          "call of {:.1f} s or more, before he finished, brings a backup {}-{} s after his call ended (rolled per "
          "backup; {}, scene {}), at once when that has passed; at most {} per scene; its sound {}; notice {}; {} "
          "{:016X}: {}",
          m_Scene, fr::InvestigationTypeName(fr::kInvestigationHearItemToss),
          fr::InvestigationTypeName(fr::kInvestigationHearRadio), fr::kBackupCallMinSeconds, m_BackupDelays.min,
          m_BackupDelays.max, SeedText(), m_RollScene, fr::kBackupMaxPerScene,
          fr::SoundEventName(fr::kBackupSoundEvent), m_NoticeOn ? "on (when someone answers)" : "off", kHelperNames[0],
          kHelperRid, ResourceText(m_Resources[0]));

    m_SceneOn.store(true, std::memory_order_release);
}

void InvestigationFeatures::Acquire() {
    if (!Globals::ResourceManager || !Globals::ResourceContainer)
        return;

    const uint64_t s_Rids[2] = {kHelperRid, kAttractorRid};

    for (size_t i = 0; i < 2; ++i) {
        if (m_Resources[i] >= 0)
            continue;

        int32_t s_Index = -1;

        if (const DWORD s_Code = SafeAcquire(s_Rids[i], &s_Index))
            LogIf(true, "footstep investigate: requesting the {} {:016X} faulted {}", kHelperNames[i], s_Rids[i],
                  FaultText(s_Code));
        else
            m_Resources[i] = s_Index;
    }
}

void InvestigationFeatures::Release() {
    for (int32_t& s_Index : m_Resources) {
        if (s_Index >= 0)
            SafeRelease(s_Index);

        s_Index = -1;
    }
}

void InvestigationFeatures::OnSceneClear() noexcept {
    Contained(kTag, "scene clear", [&] {
        CancelFootstep(fr::FootstepOutcome::Abandoned, "the scene is cleared");

        if (m_Mission) {
            if (m_StepOn) {
                std::string s_Verdicts;

                for (const auto& [s_Verdict, s_Count] : m_Verdicts)
                    s_Verdicts += fmt::format("{}{} {}", s_Verdicts.empty() ? "" : ", ", s_Verdict, s_Count);

                std::string s_Outcomes;

                for (const auto& [s_What, s_Count] : m_FootstepOutcomes)
                    s_Outcomes += fmt::format("{}{} {}", s_Outcomes.empty() ? "" : ", ", s_What, s_Count);

                // The AI events dropped (queue full) are hearings the feature never saw.
                LogIf(true,
                      "footstep investigate: summary of {}: {} helpers spawned; heard footsteps: {}; {} cases: {}; "
                      "{} AI events dropped",
                      m_Scene, m_HelperCount, s_Verdicts.empty() ? std::string("none") : s_Verdicts, m_FootstepCount,
                      s_Outcomes.empty() ? std::string("none") : s_Outcomes, m_QueueDropped);
            }

            if (m_BackupOn) {
                std::string s_Counts;

                for (const auto& [s_What, s_Count] : m_BackupCounts)
                    s_Counts += fmt::format("{}{} {}", s_Counts.empty() ? "" : ", ", s_What, s_Count);

                // The AI events dropped (queue full) are joins and takedowns the backups never saw.
                LogIf(true,
                      "backup: summary of {}: {} backups scheduled, {} still pending at the scene's end; {}; {} AI "
                      "events dropped",
                      m_Scene, m_Backup.Scheduled(), m_Backup.Pending() + m_BackupWaiting.size(),
                      s_Counts.empty() ? std::string("no noise investigations") : s_Counts, m_QueueDropped);
            }
        }

        Forget();
        m_Seed.reset();
    });
}

// The scene is gone (or a new load begins): the helper went with the scene, so it is only forgotten.
void InvestigationFeatures::Forget() {
    m_SceneOn.store(false, std::memory_order_release);
    m_PinsOn.store(false, std::memory_order_release);

    {
        std::unique_lock s_Lock(m_WatchMutex);
        m_Watches.clear();
    }
    {
        std::lock_guard s_Lock(m_QueueMutex);
        m_Queue.clear();
        m_QueueDropped = 0;
    }
    Release();
    m_Mission = false;
    m_Playing = false;
    m_Now = 0.0;
    m_Scene.clear();
    m_RollScene.clear();
    m_SpawnFaulted = {};
    m_Gate.Reset();
    m_Footstep.reset();
    m_FootstepCount = 0;
    m_FootstepOutcomes.clear();
    m_Book.Reset();
    m_Behaviors.Reset();
    m_ScanBehaviors.Reset();
    m_Followed.clear();
    m_Helper = {};
    m_HelperCount = 0;
    m_HelperIsBackup.store(false, std::memory_order_relaxed);
    m_Backup.Reset();
    m_BackupWaiting.clear();
    m_BackupWaitLogged = false;
    m_BackupCounts.clear();
    m_Downed.clear();
    m_HelperNumber.store(0, std::memory_order_relaxed);
    m_HelperOutputs.store(0, std::memory_order_relaxed);
    m_HelperInvestigatedAt.store(-1.0, std::memory_order_relaxed);
    m_NextScanAt = 0.0;
    m_Index.clear();
    m_IndexBuiltAt = -1e9;
    m_Verdicts.clear();
    m_ScanLines = 0;
}

void InvestigationFeatures::OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept {
    if (!m_SceneOn.load(std::memory_order_acquire))
        return;

    Contained(kTag, "event", [&] {
        ZString s_Json;
        // ZDynamicObject_ToString takes a non-const pointer and only reads it.
        Functions::ZDynamicObject_ToString->Call(const_cast<ZDynamicObject*>(&p_Event), s_Json);
        const std::string_view s_Text = s_Json.ToStringView();
        const std::string_view s_Name = fr::JsonEventName(s_Text);
        std::optional<fr::SituationEvent> s_Situation = fr::ParseSituationEvent(s_Text);
        const bool s_Takedown = fr::IsTakedownEventName(s_Name);

        if (!s_Situation && !s_Takedown && !fr::IsInvestigationEventName(s_Name))
            return;

        QueuedEvent s_Queued;
        s_Queued.index = p_EventIndex;
        s_Queued.name = std::string(s_Name);
        s_Queued.json = fr::OneLine(fr::RedactJson(s_Text), fr::kInvestigationMaxChars);
        s_Queued.situation = std::move(s_Situation);

        if (s_Takedown) {
            for (const fr::JsonMember& s_Member :
                 fr::JsonMembersWithKeyContaining(s_Text, std::array<std::string_view, 1> {"RepositoryId"})) {
                std::string_view s_Value = s_Member.value;

                if (s_Value.size() >= 2 && s_Value.front() == '"')
                    s_Value = s_Value.substr(1, s_Value.size() - 2);

                if ((s_Queued.repo = fr::ParseGuid(s_Value)))
                    break;
            }
        }

        std::lock_guard s_Lock(m_QueueMutex);

        if (m_Queue.size() < kMaxQueue)
            m_Queue.push_back(std::move(s_Queued));
        else
            ++m_QueueDropped;
    });
}

void InvestigationFeatures::AddWatch(ZEntityType** p_Entity, std::string p_Label, bool p_Helper) {
    if (!p_Entity)
        return;

    std::unique_lock s_Lock(m_WatchMutex);
    m_Watches[p_Entity] = Watch {std::move(p_Label), p_Helper};
    m_PinsOn.store(true, std::memory_order_release);
}

void InvestigationFeatures::RemoveWatches(bool p_Helper) {
    std::unique_lock s_Lock(m_WatchMutex);
    std::erase_if(m_Watches, [&](const auto& p_Entry) { return p_Entry.second.helper == p_Helper; });
    m_PinsOn.store(!m_Watches.empty(), std::memory_order_release);
}

std::string InvestigationFeatures::PinName(uint32_t p_Pin) const {
    const auto& s_Names = PinNames();
    const auto s_It = s_Names.find(p_Pin);
    return s_It != s_Names.end() ? std::string(s_It->second) : fmt::format("pin {:08x}", p_Pin);
}

void InvestigationFeatures::OnPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data,
                                  bool p_Input) noexcept {
    if (!p_Entity.m_pObj)
        return;

    {
        std::shared_lock s_Lock(m_WatchMutex);

        if (!m_Watches.contains(p_Entity.m_pObj))
            return;
    }

    Contained(kTag, "pin", [&] {
        Watch s_Watch;
        {
            std::unique_lock s_Lock(m_WatchMutex);
            const auto s_It = m_Watches.find(p_Entity.m_pObj);

            if (s_It == m_Watches.end())
                return;

            ++s_It->second.pins;
            s_Watch = s_It->second;
        }

        const double s_Now = m_PlaySeconds.load(std::memory_order_relaxed);
        const std::string s_Pin = PinName(p_PinId);

        if (s_Watch.helper) {
            const double s_Spawned = m_HelperSpawnedAt.load(std::memory_order_relaxed);

            if (!p_Input)
                m_HelperOutputs.fetch_add(1, std::memory_order_relaxed);

            double s_None = -1.0;

            if (!p_Input && s_Pin == "OnInvestigated")
                m_HelperInvestigatedAt.compare_exchange_strong(s_None, s_Now);

            Line(true, "{} pin: helper #{} {} {} {} ({}) at {:.1f} s of play ({:.1f} s after the spawn)",
                 m_HelperIsBackup.load(std::memory_order_relaxed) ? "backup:" : "footstep investigate",
                 m_HelperNumber.load(std::memory_order_relaxed), s_Watch.label, p_Input ? "input" : "output", s_Pin,
                 PayloadText(p_Data), s_Now, s_Now - s_Spawned);
        }
    });
}

void InvestigationFeatures::Tick(double p_Dt) noexcept {
    if (!m_Playing)
        return;

    Contained(kTag, "tick", [&] { FrameTick(p_Dt); });
}

void InvestigationFeatures::FrameTick(double p_Dt) {
    m_Now += p_Dt;
    m_PlaySeconds.store(m_Now, std::memory_order_relaxed);

    ProcessEvents();
    HelperTick();
    SampleFollowed();
    FootstepTick();
    BackupTick();

    if (HelperWindow() && m_Now >= m_NextScanAt) {
        m_NextScanAt = m_Now + fr::kSituationScanEvery;
        ScanNpcs();
    }
}

void InvestigationFeatures::ProcessEvents() {
    std::deque<QueuedEvent> s_Events;
    {
        std::lock_guard s_Lock(m_QueueMutex);
        s_Events.swap(m_Queue);
    }

    for (const QueuedEvent& s_Event : s_Events)
        Contained(kTag, "event", [&] { ProcessEvent(s_Event); });
}

void InvestigationFeatures::ProcessEvent(const QueuedEvent& p_Event) {
    if (p_Event.situation) {
        OnSituationEvent(p_Event);
        return;
    }

    if (fr::IsTakedownEventName(p_Event.name)) {
        OnTakedown(p_Event);
        return;
    }

    // The weapon and distraction mechanisms' events, while a helper is out.
    if (HelperWindow())
        Line(true, "investigations event #{} at {:.1f} s of play: {}: {}", p_Event.index, m_Now, p_Event.name,
             p_Event.json);
}

void InvestigationFeatures::OnSituationEvent(const QueuedEvent& p_Event) {
    const fr::SituationEvent& e = *p_Event.situation;
    const int s_Type = e.investigationType.value_or(-1);

    std::optional<Npc> s_Npc = e.repo ? FindNpc(*e.repo) : std::nullopt;
    const std::optional<std::array<float, 3>> s_Pos = s_Npc ? PositionOf(s_Npc->entity) : std::nullopt;
    const std::optional<std::array<float, 3>> s_Hero = HeroPosition();
    const std::optional<int32_t> s_Behavior = s_Npc ? BehaviorOf(s_Npc->actor) : std::nullopt;
    const bool s_Window = HelperWindow();

    if (s_Window && e.eventType == "AISE_ActorJoined")
        ++m_Helper.joins;

    Line(s_Window,
         "investigations event #{} at {:.1f} s of play: {} {} {}{}{}: {} {}{} at {}, {} from 47{}; "
         "behavior {}{}",
         p_Event.index, m_Now, e.name, e.situation, e.eventType,
         e.investigationType ? fmt::format(" {} ({})", fr::InvestigationTypeName(s_Type),
                                           fr::NoiseKindName(fr::InvestigationKind(s_Type)))
                             : std::string(),
         e.joinReason.empty() ? std::string() : " " + e.joinReason,
         s_Npc ? NpcText(*s_Npc) : std::string("NPC not found"),
         e.repo ? fr::FormatGuid(*e.repo) : std::string("<no repository id>"),
         e.actorId ? fmt::format(" ActorId {:.0f}", *e.actorId) : std::string(), PosText(s_Pos),
         DistText(s_Pos, s_Hero),
         HelperOut() ? fmt::format(", {} from footstep helper #{}", DistText(s_Pos, m_Helper.at), m_Helper.number)
                     : std::string(),
         s_Behavior ? fr::BehaviorName(*s_Behavior) : std::string("unread"),
         m_Followed.empty() ? std::string() : fmt::format("; {}", p_Event.json));

    if (!e.repo)
        return;

    // backup_guard: a join with no investigation type in a backup helper's window is still a joiner to log (a sound
    // may draw a situation without one).
    if (e.eventType == "AISE_ActorJoined" && !e.investigationType && m_BackupOn && s_Window && m_Helper.backup)
        BackupJoin(e, s_Npc ? &*s_Npc : nullptr, s_Pos, s_Window);

    // footsteps_investigate: who joins a footstep helper's investigation in its window, with the type.
    if (e.eventType == "AISE_ActorJoined" && s_Window && !m_Helper.backup)
        LogIf(m_StepOn,
              "footstep investigate: helper #{} joiner at {:.1f} s of play ({:.1f} s after the spawn): {} "
              "[{}] {} {}, {} from the helper{}",
              m_Helper.number, m_Now, m_Now - m_Helper.spawnedAt, s_Npc ? NpcText(*s_Npc) : fr::FormatGuid(*e.repo),
              s_Npc ? ActorTypeText(s_Npc->actor) : std::string("kind unread"), e.situation,
              e.investigationType ? fr::InvestigationTypeName(s_Type) : std::string("(no investigation type)"),
              DistText(s_Pos, m_Helper.at),
              m_Footstep && *e.repo == m_Footstep->repo ? fmt::format(" (the hearer of case #{})", m_Footstep->number)
                                                        : std::string());

    // Follow him (a join).
    if (e.eventType == "AISE_ActorJoined" && e.investigationType) {
        m_Book.Join(*e.repo, s_Type, m_Now);

        if (s_Npc && (m_Followed.contains(*e.repo) || m_Followed.size() < fr::kMaxFollowedInvestigators)) {
            Followed& f = m_Followed[*e.repo];
            const bool s_Fresh = f.npc.actor == nullptr || m_Now - f.joinedAt > fr::kInvestigatorWatchSeconds;
            f.npc = *s_Npc;
            f.type = s_Type;
            f.joinedAt = m_Now;
            f.nextAt = m_Now;
            f.joinedPos = s_Pos;
            f.fromHelper = s_Window;

            if (s_Fresh && s_Behavior)
                m_Behaviors.Observe(reinterpret_cast<uintptr_t>(s_Npc->actor), *s_Behavior);
        }

        if (m_BackupOn)
            BackupJoin(e, s_Npc ? &*s_Npc : nullptr, s_Pos, s_Window);

        // footsteps_investigate: the hearer of an open case answered the curious helper, or joined something else.
        if (FootstepOpen() && *e.repo == m_Footstep->repo && s_Type != fr::kInvestigationFootsteps) {
            if (CuriousOut() && fr::AnswersCuriousNoise(s_Type))
                ApplyFootstep(m_Footstep->state.Answer(),
                              fmt::format("he joined {} {:.1f} s after the curious helper's spawn, {} from it",
                                          fr::InvestigationTypeName(s_Type), m_Now - m_Helper.spawnedAt,
                                          DistText(s_Pos, m_Helper.at)));
            else
                CancelFootstep(fr::FootstepOutcome::OtherInvestigation,
                               fmt::format("he joined {}{}", fr::InvestigationTypeName(s_Type),
                                           CuriousOut() ? " (not an answer to the curious sound)" : ""));
        }
    }

    // footsteps_investigate: one chance roll per hearing that passes the gate (never per footstep).
    if (m_StepOn && e.investigationType == fr::kInvestigationFootsteps) {
        // Busy: a helper is out, or a case is still waiting for its hearer.
        const fr::FootstepVerdict s_Verdict = m_Gate.Check(e, m_Now, HelperOut() || FootstepOpen());

        if (s_Verdict != fr::FootstepVerdict::Go) {
            ++m_Verdicts[fr::FootstepVerdictName(s_Verdict)];

            if (s_Verdict != fr::FootstepVerdict::NotJoin)
                LogIf(true, "footstep investigate: heard footsteps at {:.1f} s of play ({}): no helper: {}", m_Now,
                      s_Npc ? NpcText(*s_Npc) : std::string("NPC not found"), fr::FootstepVerdictName(s_Verdict));
            return;
        }

        const uint32_t s_Hearing = m_Gate.TakeHearing();
        // Drawn at the time, not from the seed: a replay of the same hearings rolls anew.
        static thread_local std::mt19937_64 s_Rng {std::random_device {}()};
        const fr::FootstepRoll s_Roll =
            fr::DecideFootstepInvestigate(std::uniform_real_distribution<double>(0.0, 1.0)(s_Rng), m_StepChance);
        const std::string s_Who = s_Npc ? NpcText(*s_Npc) : std::string("NPC not found");
        ++m_Verdicts[s_Roll.go ? fr::FootstepVerdictName(s_Verdict) : "chance roll missed"];

        if (!s_Roll.go) {
            // His cooldown starts: his next footsteps within it are not rolled again.
            m_Gate.MarkMiss(*e.repo, m_Now);
            LogIf(true,
                  "footstep investigate: heard footsteps at {:.1f} s of play ({}): hearing #{} rolled {:.3f}, "
                  "not under {}%: no helper (he only turns to look)",
                  m_Now, s_Who, s_Hearing + 1, s_Roll.u, m_StepChance);
            return;
        }

        LogIf(true,
              "footstep investigate: heard footsteps at {:.1f} s of play ({}): hearing #{} rolled {:.3f} < {}% "
              ": investigating",
              m_Now, s_Who, s_Hearing + 1, s_Roll.u, m_StepChance);
        StartFootstep(e, s_Npc ? &*s_Npc : nullptr);
    }
}

void InvestigationFeatures::OnTakedown(const QueuedEvent& p_Event) {
    if (!p_Event.repo)
        return;

    // A pacified NPC stays alive: a backup must not count him as a guard who could come, nor as a civilian in the way.
    m_Downed.insert(*p_Event.repo);

    if (FootstepOpen() && *p_Event.repo == m_Footstep->repo)
        CancelFootstep(fr::FootstepOutcome::TakenDown, p_Event.name);
    const auto s_Followed = m_Followed.find(*p_Event.repo);

    // backup_guard first: it needs his case whatever the investigation book says.
    if (m_BackupOn && m_Backup.Armed(*p_Event.repo)) {
        std::string s_Who = fr::FormatGuid(*p_Event.repo);
        std::optional<int32_t> s_Last;
        std::optional<std::array<float, 3>> s_Pos;

        if (s_Followed != m_Followed.end()) {
            s_Who = NpcText(s_Followed->second.npc);
            s_Last = m_Behaviors.Last(reinterpret_cast<uintptr_t>(s_Followed->second.npc.actor));
            s_Pos = PositionOf(s_Followed->second.npc.entity);
        }

        BackupTakedown(*p_Event.repo, fmt::format("{} {}", p_Event.name, s_Who), s_Last, s_Pos);
    }

    const std::optional<fr::TakedownFacts> s_Facts = m_Book.Takedown(*p_Event.repo, m_Now);

    if (!s_Facts) {
        // Taken down outside an investigation: one short line while a helper is out.
        if (HelperWindow())
            Line(true, "investigations takedown at {:.1f} s of play: {} {} (not in a followed investigation)", m_Now,
                 p_Event.name, fr::FormatGuid(*p_Event.repo));
        return;
    }

    std::string s_Npc = fr::FormatGuid(*p_Event.repo);
    std::optional<int32_t> s_Last;
    std::optional<std::array<float, 3>> s_Pos;
    std::optional<std::array<float, 3>> s_JoinedAt;
    bool s_FromHelper = false;

    if (s_Followed != m_Followed.end()) {
        s_Npc = NpcText(s_Followed->second.npc);
        s_Last = m_Behaviors.Last(reinterpret_cast<uintptr_t>(s_Followed->second.npc.actor));
        s_Pos = PositionOf(s_Followed->second.npc.entity);
        s_JoinedAt = s_Followed->second.joinedPos;
        s_FromHelper = s_Followed->second.fromHelper;
    }

    Line(s_FromHelper,
         "investigations takedown: {} {} at {:.1f} s of play, {:.1f} s after he joined an {} investigation "
         "({}, {} joins); last behavior {}; at {} ({} from where he joined); {}",
         p_Event.name, s_Npc, m_Now, s_Facts->since, fr::InvestigationTypeName(s_Facts->type),
         fr::NoiseKindName(s_Facts->kind), s_Facts->joins, s_Last ? fr::BehaviorName(*s_Last) : std::string("unread"),
         PosText(s_Pos), DistText(s_Pos, s_JoinedAt),
         m_BackupOn ? std::string("backup_guard decides on a backup (\"backup:\" lines)")
                    : std::string("backup_guard is off"));

    if (s_Followed != m_Followed.end())
        m_Followed.erase(s_Followed);
}

// --- footsteps_investigate -----------------------------------------------------------------------------------

bool InvestigationFeatures::TrySpawn(HelperKind p_Kind, int32_t p_Index, const std::array<float, 3>& p_At,
                                     std::string* p_Why, bool p_Backup, float p_Radius, int32_t p_SoundEvent) {
    const size_t s_K = static_cast<size_t>(p_Kind);

    if (m_SpawnFaulted[s_K]) {
        *p_Why = "an earlier spawn faulted this scene";
        return false;
    }

    ResourceState s_State {};

    if (p_Index < 0 || SafeResourceState(p_Index, &s_State) || s_State.status != RESOURCE_STATUS_VALID ||
        !s_State.data) {
        *p_Why = fmt::format("template {}", ResourceText(p_Index));
        return false;
    }

    ZEntitySceneContext* s_Context = CurrentSceneContext();

    if (!s_Context || !s_Context->m_pScene) {
        *p_Why = "no scene";
        return false;
    }

    ZEntityRef s_Root;

    if (const uint32_t s_Code =
            SpawnEntity(static_cast<ZTemplateEntityFactory*>(s_State.data), s_Context->m_pScene.m_entityRef, &s_Root)) {
        m_SpawnFaulted[s_K] = true;
        *p_Why = fmt::format("NewEntity faulted {}", FaultText(s_Code));
        return false;
    }

    if (!s_Root.m_pObj) {
        *p_Why = "NewEntity returned no entity";
        return false;
    }

    m_Helper = {};
    m_Helper.root = s_Root.m_pObj;
    m_Helper.kind = p_Kind;
    m_Helper.number = ++m_HelperCount;
    m_Helper.spawnedAt = m_Now;
    m_Helper.at = p_At;
    m_Helper.backup = p_Backup;
    m_Helper.reach = p_Backup ? p_Radius : 0.0f;
    m_Helper.sound = p_SoundEvent;
    m_HelperIsBackup.store(p_Backup, std::memory_order_relaxed);
    m_HelperNumber.store(m_Helper.number, std::memory_order_relaxed);
    m_HelperSpawnedAt.store(m_Now, std::memory_order_relaxed);
    m_HelperOutputs.store(0, std::memory_order_relaxed);
    m_HelperInvestigatedAt.store(-1.0, std::memory_order_relaxed);
    const char* s_Name = kHelperNames[s_K];
    const char* s_Tag = HelperTag();

    // Activation: an entity the engine did not activate is activated once.
    FlagsOut s_Flags {};
    std::string s_Activation;

    if (const DWORD s_Code = SafeFlags(s_Root.m_pObj, &s_Flags)) {
        s_Activation = "flags unreadable " + FaultText(s_Code);
    }
    else if (s_Flags.flags & kEntityActivated) {
        s_Activation = fmt::format("activated by the engine (flags 0x{:x})", s_Flags.flags);
    }
    else {
        const DWORD s_Code = SafeActivate(s_Root.m_pObj);
        FlagsOut s_After {};
        SafeFlags(s_Root.m_pObj, &s_After);
        s_Activation = fmt::format("not activated (flags 0x{:x}); Activate(0) {}, flags now 0x{:x}", s_Flags.flags,
                                   s_Code ? FaultText(s_Code) : std::string("called"), s_After.flags);
    }

    // At 47, upright.
    const std::array<float, 12> s_Rows {1, 0, 0, 0, 1, 0, 0, 0, 1, p_At[0], p_At[1], p_At[2]};
    bool s_Spatial = false;
    const uint32_t s_MoveCode = WriteWorldTransform(s_Root.m_pObj, s_Rows, &s_Spatial);
    const PositionRead s_Back = ReadPosition(s_Root.m_pObj, true);
    const std::optional<std::array<float, 3>> s_BackAt =
        s_Back.at ? std::optional<std::array<float, 3>>({s_Back.at->x, s_Back.at->y, s_Back.at->z}) : std::nullopt;

    LogIf(true, "{} helper #{}: {} spawned {} under the scene; {}; moved to {}: {}, reads back {} ({} off)", s_Tag,
          m_Helper.number, s_Name, EntityText(s_Root.m_pObj), s_Activation, PosText(p_At),
          s_MoveCode  ? FaultText(s_MoveCode)
          : s_Spatial ? std::string("written")
                      : std::string("not spatial"),
          PosText(s_BackAt), DistText(s_BackAt, p_At));

    // Its graph: the input forwardings say which pins it answers to.
    std::string s_GraphWhy;
    std::vector<std::string> s_Inputs;
    std::vector<std::string> s_Outputs;
    const STemplateEntityBlueprint* s_Blueprint = TemplateBlueprintOf(s_Root.m_pObj, &s_GraphWhy);

    if (s_Blueprint) {
        if (const auto s_Links = ReadBlueprintLinks(s_Blueprint, &s_GraphWhy)) {
            for (const BlueprintLink& s_Link : *s_Links) {
                if (s_Link.kind == BlueprintLink::Kind::InputForwarding)
                    s_Inputs.emplace_back(s_Link.fromPin);
                else if (s_Link.kind == BlueprintLink::Kind::OutputForwarding)
                    s_Outputs.emplace_back(s_Link.toPin);
            }
        }
    }

    LogIf(true, "{} helper #{}: input forwardings [{}], output forwardings [{}]{}", s_Tag, m_Helper.number,
          Join(s_Inputs), Join(s_Outputs), s_Blueprint ? std::string() : " (graph unread: " + s_GraphWhy + ")");

    // Watch it and its sub-entities.
    AddWatch(s_Root.m_pObj, fmt::format("{} root", s_Name), true);
    size_t s_Watched = 1;
    std::vector<std::pair<ZEntityType**, std::string>> s_Subs;
    VisitEntityTree(
        s_Root.m_pObj, 0,
        [&](const VisitedEntity& p_Visit) {
            if (s_Watched >= kMaxHelperWatches)
                return false;

            std::string s_Class;
            uint64_t s_Id = 0;
            ReadEntityClassName(p_Visit.entity, s_Class, &s_Id);
            AddWatch(p_Visit.entity, fmt::format("{} sub {} {}", s_Name, p_Visit.index, s_Class), true);
            s_Subs.emplace_back(p_Visit.entity, s_Class);
            ++s_Watched;
            return true;
        },
        kMaxHelperWatches);

    // Its ranges and sound events first, before its perception is on (a backup's or a footstep's alike).
    if (p_Radius > 0.0f)
        WriteHelperRanges(s_Root.m_pObj, s_Subs, p_Radius, p_SoundEvent);

    // Its pins: what its graph forwards, else the known pin / the guesses.
    std::vector<std::string> s_Fire = fr::PinsToFire(s_Inputs);

    if (s_Fire.empty()) {
        if (p_Kind == HelperKind::Helper) {
            s_Fire.emplace_back(fr::kHelperEnablePin);
        }
        else {
            for (const std::string_view s_Guess : fr::AttractorPinGuesses())
                s_Fire.emplace_back(s_Guess);
        }
    }

    for (const std::string& s_Pin : s_Fire) {
        const DWORD s_Code = SendInputPin(s_Root.m_pObj, PinId(s_Pin));
        m_Helper.fired.push_back(s_Code ? fmt::format("{} {}", s_Pin, FaultText(s_Code)) : s_Pin);
    }

    LogIf(true, "{} helper #{}: {} entities watched for pins; signaled {}", s_Tag, m_Helper.number, s_Watched,
          Join(m_Helper.fired));
    return true;
}

namespace {

bool Contains(std::string_view p_Text, std::string_view p_Part) {
    return p_Text.find(p_Part) != std::string_view::npos;
}

// A float property's value, read raw (or through its getter), as text.
std::string FloatText(ZEntityType** p_Entity, std::string_view p_Name) {
    const RawRead32 s_Raw = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);

    switch (s_Raw.status) {
        case RawRead32::Status::Ok: {
            float s_Value = 0.0f;
            std::memcpy(&s_Value, &s_Raw.bits, sizeof(s_Value));
            return fmt::format("{:.1f}", s_Value);
        }
        case RawRead32::Status::Missing: return "no such property";
        case RawRead32::Status::NotRaw: return "unread (a getter, or not 4 bytes)";
        case RawRead32::Status::Fault: return FaultText(s_Raw.faultCode);
    }

    return "?";
}

std::string IntText(ZEntityType** p_Entity, std::string_view p_Name) {
    const RawRead32 s_Raw = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);
    return s_Raw.status == RawRead32::Status::Ok ? fmt::format("{}", static_cast<int32_t>(s_Raw.bits))
                                                 : std::string("unread");
}

// An EAIEventType property read raw, as text; *p_Missing when the entity has no such property.
std::string EventTypeText(ZEntityType** p_Entity, std::string_view p_Name, bool* p_Missing) {
    const RawRead32 s_Raw = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);
    *p_Missing = s_Raw.status == RawRead32::Status::Missing;

    switch (s_Raw.status) {
        case RawRead32::Status::Ok: return fmt::format("{}", static_cast<int32_t>(s_Raw.bits));
        case RawRead32::Status::Missing: return "no such property";
        case RawRead32::Status::NotRaw: return "unread (a getter, or not 4 bytes)";
        case RawRead32::Status::Fault: return FaultText(s_Raw.faultCode);
    }

    return "?";
}

} // namespace

// backup_guard and footsteps_investigate: the helper's sound ranges (root
// m_fSoundRange, the descriptors' m_fRange) set to p_Radius, its sphere radius (root m_fRadius,
// the sphere's) and perceptible range to fr::kBackupPerceptibleRange
// (property names from the template: Investigations.h), each read before and
// after; the dynamic enforcer event's type and range logged. The sound alone
// calls the backup (PlanBackupReach: kBackupRange, or out to a guard on the spot's floor); the
// perceptible part drew a civilian who walked into it. Its sound
// events (both descriptors' m_eType) become p_SoundEvent: the footstep case's
// curious sound or thrown item's landing, the backup's sounds of a struggle.
void InvestigationFeatures::WriteHelperRanges(ZEntityType** p_Root,
                                              const std::vector<std::pair<ZEntityType**, std::string>>& p_Subs,
                                              float p_Radius, int32_t p_SoundEvent) {
    std::vector<std::pair<ZEntityType**, std::string>> s_Targets; // entity, property
    std::vector<std::string> s_Labels;
    std::vector<float> s_Values;
    const float s_Perceptible = fr::kBackupPerceptibleRange;

    s_Targets.emplace_back(p_Root, "m_fSoundRange");
    s_Labels.emplace_back("root");
    s_Values.push_back(p_Radius);
    s_Targets.emplace_back(p_Root, "m_fRadius");
    s_Labels.emplace_back("root");
    s_Values.push_back(s_Perceptible);
    std::string s_Enforcers;
    std::string s_Events;
    size_t s_Descriptors = 0;
    size_t s_EventsSet = 0;

    for (size_t i = 0; i < p_Subs.size(); ++i) {
        const auto& [s_Entity, s_Class] = p_Subs[i];

        if (Contains(s_Class, "SoundEventDescriptor")) {
            ++s_Descriptors;
            const std::string_view s_Property = fr::kSoundEventTypeProperty;
            bool s_Missing = false;
            const std::string s_Before = EventTypeText(s_Entity, s_Property, &s_Missing);

            if (s_Missing) {
                s_Events += fmt::format("{}sub {} {}: no property {} (CRC32 {:08x}): event type NOT changed",
                                        s_Events.empty() ? "" : "; ", i, s_Class, s_Property,
                                        Hash::Crc32(s_Property.data(), s_Property.size()));
            }
            else {
                bool s_Ok = false;
                const uint32_t s_Code = WriteAIEventTypeProperty(s_Entity, s_Property, p_SoundEvent, &s_Ok);
                bool s_GoneAfter = false;
                s_EventsSet += s_Ok ? 1 : 0;
                s_Events += fmt::format("{}sub {} {} {}: {} -> {} ({})", s_Events.empty() ? "" : "; ", i, s_Class,
                                        s_Property, s_Before, EventTypeText(s_Entity, s_Property, &s_GoneAfter),
                                        s_Code ? FaultText(s_Code)
                                        : s_Ok ? std::string("set")
                                               : std::string("not set"));
            }
        }

        if (Contains(s_Class, "SoundEventDescriptor")) {
            s_Targets.emplace_back(s_Entity, "m_fRange");
            s_Labels.push_back(fmt::format("sub {} {}", i, s_Class));
            s_Values.push_back(p_Radius);
        }
        else if (Contains(s_Class, "PerceptibleEntity")) {
            s_Targets.emplace_back(s_Entity, "m_fRange");
            s_Labels.push_back(fmt::format("sub {} {}", i, s_Class));
            s_Values.push_back(s_Perceptible);
        }
        else if (Contains(s_Class, "SphereVolume")) {
            s_Targets.emplace_back(s_Entity, "m_fRadius");
            s_Labels.push_back(fmt::format("sub {} {}", i, s_Class));
            s_Values.push_back(s_Perceptible);
        }
        else if (Contains(s_Class, "DynamicEnforcer")) {
            s_Enforcers += fmt::format("{}sub {} {}: m_eventType {}, m_fRange {}", s_Enforcers.empty() ? "" : "; ", i,
                                       s_Class, IntText(s_Entity, "m_eventType"), FloatText(s_Entity, "m_fRange"));
        }
    }

    std::string s_Done;

    for (size_t i = 0; i < s_Targets.size(); ++i) {
        const auto& [s_Entity, s_Property] = s_Targets[i];
        const std::string s_Before = FloatText(s_Entity, s_Property);
        bool s_Ok = false;
        const uint32_t s_Code = WriteFloatProperty(s_Entity, s_Property, s_Values[i], &s_Ok);
        s_Done += fmt::format("{}{} {}: {} -> {} ({})", s_Done.empty() ? "" : "; ", s_Labels[i], s_Property, s_Before,
                              FloatText(s_Entity, s_Property),
                              s_Code ? FaultText(s_Code)
                              : s_Ok ? std::string("set")
                                     : std::string("not set"));
    }

    LogIf(true,
          "{} helper #{}: sound ranges to {:.1f} m, sphere and perceptible range to {:.1f} m (sound only: "
          "a civilian walking by is not drawn): {}; dynamic enforcer event: {}",
          HelperTag(), m_Helper.number, p_Radius, s_Perceptible, s_Done,
          s_Enforcers.empty() ? std::string("none found") : s_Enforcers);
    LogIf(true, "{} helper #{}: sound events to {} ({}), {} of {} set: {}", HelperTag(), m_Helper.number,
          fr::SoundEventName(p_SoundEvent), p_SoundEvent, s_EventsSet, s_Descriptors,
          s_Events.empty() ? std::string("no ZAISoundEventDescriptor found: event type NOT changed") : s_Events);
}

// --- footsteps_investigate: the case (fr::FootstepCase) ---------------------------------------------------------

namespace {

std::string BehaviorText(const std::optional<int32_t>& p_Type) {
    return p_Type ? fr::BehaviorName(*p_Type) : std::string("unread");
}

} // namespace

// A hit: the gate is marked now (at the hearing), the spot is 47's position now, the case opens and its curious
// helper goes out at once (else the case starts waiting for his reaction).
void InvestigationFeatures::StartFootstep(const fr::SituationEvent& p_Event, const Npc* p_Npc) {
    const std::string s_Who = p_Npc ? NpcText(*p_Npc) : fr::FormatGuid(p_Event.repo.value_or(fr::Guid {}));
    // Either way the gate counts it: a failing case is not retried every footstep.
    m_Gate.Mark(*p_Event.repo, m_Now);
    const std::optional<std::array<float, 3>> s_Hero = HeroPosition();

    if (!s_Hero) {
        ++m_FootstepOutcomes["no spot: 47's position unread"];
        LogIf(true, "footstep investigate: heard footsteps at {:.1f} s of play ({}): no helper: 47's position unread",
              m_Now, s_Who);
        return;
    }

    if (!p_Npc || !m_Followed.contains(*p_Event.repo)) {
        ++m_FootstepOutcomes["not followed"];
        LogIf(true,
              "footstep investigate: heard footsteps at {:.1f} s of play ({}): no helper: he is {}, so his "
              "answer and the end of his reaction cannot be seen",
              m_Now, s_Who, p_Npc ? "not followed (the follow list is full)" : "not found among the NPCs");
        return;
    }

    Footstep s_Case;
    s_Case.number = ++m_FootstepCount;
    s_Case.repo = *p_Event.repo;
    s_Case.npc = *p_Npc;
    s_Case.who = s_Who;
    s_Case.spot = *s_Hero;
    const std::optional<std::array<float, 3>> s_At = PositionOf(p_Npc->entity);

    if (s_At)
        s_Case.heardFrom = static_cast<float>(fr::Distance(*s_At, *s_Hero));

    s_Case.behaviorAtHearing = BehaviorOf(p_Npc->actor);
    s_Case.lastLogged = s_Case.behaviorAtHearing;
    m_Footstep = std::move(s_Case);

    LogIf(true,
          "footstep investigate: case #{} for {}: heard at {:.1f} s of play from {} ({} from 47); the spot (47 "
          "now) {}; his behavior {}",
          m_Footstep->number, s_Who, m_Now, PosText(s_At),
          m_Footstep->heardFrom ? fmt::format("{:.1f} m", *m_Footstep->heardFrom) : std::string("?"),
          PosText(m_Footstep->spot), BehaviorText(m_Footstep->behaviorAtHearing));

    const bool s_Curious = SpawnCurious();
    m_Footstep->state = fr::FootstepCase(m_Now, s_Curious);

    LogIf(true, "footstep investigate: case #{}: {}", m_Footstep->number,
          s_Curious ? fmt::format("curious: helper #{} out; if he answers it within {:.0f} s, no other noise; else "
                                  "the {} noise once his reaction is over (at most {:.0f} s after the hearing)",
                                  m_Footstep->curiousHelper, fr::kFootstepCuriousWait,
                                  fr::SoundEventName(fr::kFootstepNoiseEvent), fr::kFootstepWaitMax)
                    : fmt::format("waiting: no curious helper; the {} noise once his reaction is over (at most "
                                  "{:.0f} s after the hearing)",
                                  fr::SoundEventName(fr::kFootstepNoiseEvent), fr::kFootstepWaitMax));

    // The hearing's own behavior is the first sample.
    if (m_Footstep->behaviorAtHearing)
        ApplyFootstep(m_Footstep->state.Behavior(*m_Footstep->behaviorAtHearing, m_Now),
                      "on " + BehaviorText(m_Footstep->behaviorAtHearing) + " at the hearing");
}

bool InvestigationFeatures::SpawnCurious() {
    Footstep& f = *m_Footstep;
    const float s_Reach = fr::FootstepReach(f.heardFrom);
    std::string s_Why;

    // No attractor fallback: the radio's sound is not a curious one.
    if (!TrySpawn(HelperKind::Helper, m_Resources[0], f.spot, &s_Why, false, s_Reach, fr::kFootstepCuriousEvent)) {
        ++m_FootstepOutcomes["curious helper not spawned"];
        LogIf(true, "footstep investigate: case #{}: no curious helper: {}", f.number, s_Why);
        return false;
    }

    f.curiousHelper = m_Helper.number;
    m_Helper.forActor = f.who;
    ++m_FootstepOutcomes["curious helper spawned"];
    LogIf(true,
          "footstep investigate: helper #{} ({}, curious) for {} (case #{}) out at {:.1f} s of play at the spot "
          "{}; sound {} reaching {:.1f} m (he heard 47 from {}); pins fired: {}",
          m_Helper.number, kHelperNames[0], f.who, f.number, m_Now, PosText(m_Helper.at),
          fr::SoundEventName(fr::kFootstepCuriousEvent), s_Reach,
          f.heardFrom ? fmt::format("{:.1f} m", *f.heardFrom) : std::string("an unread distance"),
          m_Helper.fired.empty() ? std::string("none") : Join(m_Helper.fired));
    return true;
}

bool InvestigationFeatures::CuriousOut() const {
    return m_Footstep && m_Footstep->curiousHelper != 0 && HelperOut() && !m_Helper.backup &&
           m_Helper.number == m_Footstep->curiousHelper;
}

void InvestigationFeatures::FootstepBehavior(int32_t p_Type) {
    if (!FootstepOpen())
        return;

    Footstep& f = *m_Footstep;

    if (f.lastLogged != p_Type) {
        const std::optional<std::array<float, 3>> s_At = PositionOf(f.npc.entity);
        LogIf(true,
              "footstep investigate: case #{}: {} {} -> {} at {:.2f} s of play ({:.1f} s after the hearing, {}), "
              "{} from the spot",
              f.number, f.who, BehaviorText(f.lastLogged), fr::BehaviorName(p_Type), m_Now, m_Now - f.state.HeardAt(),
              fr::FootstepPhaseName(f.state.Phase()), DistText(s_At, f.spot));
        f.lastLogged = p_Type;
    }

    ApplyFootstep(f.state.Behavior(p_Type, m_Now), "on " + fr::BehaviorName(p_Type));
}

void InvestigationFeatures::FootstepTick() {
    if (!FootstepOpen())
        return;

    ApplyFootstep(m_Footstep->state.Tick(m_Now), "time");
}

void InvestigationFeatures::CancelFootstep(fr::FootstepOutcome p_Why, const std::string& p_Detail) {
    if (!FootstepOpen())
        return;

    ApplyFootstep(m_Footstep->state.Cancel(p_Why), p_Detail);
}

void InvestigationFeatures::ApplyFootstep(const fr::FootstepStep& p_Step, const std::string& p_Detail) {
    if (p_Step.outcome == fr::FootstepOutcome::None || !m_Footstep)
        return;

    Footstep& f = *m_Footstep;
    const double s_Since = m_Now - f.state.HeardAt();
    ++m_FootstepOutcomes[fr::FootstepOutcomeName(p_Step.outcome)];

    const std::string s_Reacted = f.state.ReactedAt() ? fmt::format("reacting from {:.1f} s after the hearing",
                                                                    *f.state.ReactedAt() - f.state.HeardAt())
                                                      : std::string("never seen reacting");

    switch (p_Step.outcome) {
        case fr::FootstepOutcome::Answered:
            LogIf(true,
                  "footstep investigate: case #{}: answered: {} {:.2f} s after the hearing ({}; behavior {}); "
                  "no other noise",
                  f.number, f.who, s_Since, p_Detail, BehaviorText(f.state.Last()));
            break;
        case fr::FootstepOutcome::CuriousUnanswered:
            LogIf(true,
                  "footstep investigate: case #{}: no answer to the curious sound in {:.0f} s ({} {}; {}); "
                  "waiting for his reaction to end (at most {:.0f} s after the hearing), then the {} noise",
                  f.number, fr::kFootstepCuriousWait, f.who, BehaviorText(f.state.Last()), s_Reacted,
                  fr::kFootstepWaitMax, fr::SoundEventName(fr::kFootstepNoiseEvent));
            break;
        case fr::FootstepOutcome::ReactionOver:
            LogIf(true,
                  "footstep investigate: case #{}: {}'s reaction is over {:.2f} s after the hearing ({}; {}); "
                  "the noise goes now",
                  f.number, f.who, s_Since, p_Detail, s_Reacted);
            break;
        case fr::FootstepOutcome::GaveUp:
            LogIf(true,
                  "footstep investigate: case #{}: given up: {} still {} {:.0f} s after the hearing ({}); no "
                  "noise",
                  f.number, f.who, BehaviorText(f.state.Last()), s_Since, s_Reacted);
            break;
        default:
            LogIf(true,
                  "footstep investigate: case #{}: canceled while {} {:.2f} s after the hearing: {} ({}); last "
                  "behavior {}; no noise",
                  f.number, f.state.WaitingAt() ? "waiting" : "curious", s_Since,
                  fr::FootstepOutcomeName(p_Step.outcome), p_Detail, BehaviorText(f.state.Last()));
            break;
    }

    if (p_Step.spawnNoise)
        SpawnHelper();
}

// The noise: the curious helper retired, then the investigate-curious helper at the spot (where 47 was heard) with a
// thrown item's landing, its sound reaching the hearer.
void InvestigationFeatures::SpawnHelper() {
    Footstep& f = *m_Footstep;

    if (CuriousOut())
        RetireHelper("the thrown-item noise replaces the curious sound");

    if (HelperOut()) {
        ++m_FootstepOutcomes["noise not spawned: another helper out"];
        LogIf(true, "footstep investigate: case #{}: no noise: {} helper #{} is out", f.number,
              m_Helper.backup ? "the backup's" : "footstep", m_Helper.number);
        return;
    }

    const std::optional<std::array<float, 3>> s_HearerAt = PositionOf(f.npc.entity);
    const std::optional<float> s_HearerDistance =
        s_HearerAt ? std::optional<float>(static_cast<float>(fr::Distance(*s_HearerAt, f.spot))) : std::nullopt;
    const float s_Reach = fr::FootstepReach(s_HearerDistance);
    std::string s_Why;
    HelperKind s_Kind = HelperKind::Helper;
    bool s_Spawned =
        TrySpawn(HelperKind::Helper, m_Resources[0], f.spot, &s_Why, false, s_Reach, fr::kFootstepNoiseEvent);

    if (!s_Spawned) {
        LogIf(true, "footstep investigate: the {} did not spawn ({}); trying the {}", kHelperNames[0], s_Why,
              kHelperNames[1]);
        s_Why.clear();
        s_Kind = HelperKind::Attractor;
        s_Spawned = TrySpawn(HelperKind::Attractor, m_Resources[1], f.spot, &s_Why);
    }

    if (!s_Spawned) {
        ++m_FootstepOutcomes["noise not spawned"];
        LogIf(true, "footstep investigate: case #{}: no helper: {}", f.number, s_Why);
        return;
    }

    ++m_FootstepOutcomes["noise spawned"];
    m_Helper.forActor = f.who;
    m_Helper.kind = s_Kind;
    LogIf(true,
          "footstep investigate: helper #{} ({}, noise) for {} (case #{}) out at {:.1f} s of play ({:.1f} s after "
          "the hearing) at the spot {} (47 now {} from it); sound {} reaching {:.1f} m (he is {} from it; heard from "
          "{}); pins fired: {}",
          m_Helper.number, kHelperNames[static_cast<int>(s_Kind)], f.who, f.number, m_Now, m_Now - f.state.HeardAt(),
          PosText(m_Helper.at), DistText(HeroPosition(), f.spot),
          s_Kind == HelperKind::Helper ? fr::SoundEventName(fr::kFootstepNoiseEvent) : std::string("the radio's"),
          s_Reach, s_HearerDistance ? fmt::format("{:.1f} m", *s_HearerDistance) : std::string("an unread distance"),
          f.heardFrom ? fmt::format("{:.1f} m", *f.heardFrom) : std::string("?"),
          m_Helper.fired.empty() ? std::string("none") : Join(m_Helper.fired));
}

void InvestigationFeatures::RetireHelper(const char* p_Why) {
    if (!m_Helper.root || m_Helper.deleted)
        return;

    if (!m_Helper.disabled) {
        m_Helper.disabled = true;
        const char* s_Pin = m_Helper.kind == HelperKind::Helper ? "DisablePerception" : "Deactivate";
        const DWORD s_Code = SendInputPin(m_Helper.root, PinId(s_Pin));
        LogIf(true, "{} helper #{} switched off early ({}): {}{}", HelperTag(), m_Helper.number, p_Why, s_Pin,
              s_Code ? " " + FaultText(s_Code) : std::string());
    }

    RemoveWatches(true);
    m_Helper.deleted = true;
    const DWORD s_Code = SafeDelete(m_Helper.root);
    LogIf(true, "{} helper #{} deleted: {}", HelperTag(), m_Helper.number,
          s_Code ? FaultText(s_Code) : std::string("DeleteEntity called"));
    m_Helper.root = nullptr;
    m_HelperNumber.store(0, std::memory_order_relaxed);
}

void InvestigationFeatures::HelperTick() {
    if (!m_Helper.root || m_Helper.deleted)
        return;

    const double s_Age = m_Now - m_Helper.spawnedAt;
    const double s_Watch = m_Helper.backup ? fr::kBackupWatchSeconds : fr::kFootstepWatchSeconds;
    const double s_DeleteAfter = m_Helper.backup ? fr::kBackupDeleteAfter : fr::kFootstepDeleteAfter;

    if (!m_Helper.disabled && s_Age >= s_Watch) {
        m_Helper.disabled = true;
        std::vector<std::string> s_Stops;

        if (m_Helper.kind == HelperKind::Helper)
            s_Stops.emplace_back(fr::kHelperDisablePin);
        else
            s_Stops = {"Deactivate", "Stop", "Disable", "SetFalse"};

        std::string s_Done;

        for (const std::string& s_Pin : s_Stops) {
            const DWORD s_Code = SendInputPin(m_Helper.root, PinId(s_Pin));
            s_Done += fmt::format("{}{}{}", s_Done.empty() ? "" : ", ", s_Pin, s_Code ? " " + FaultText(s_Code) : "");
        }

        LogIf(true,
              "{} helper #{} after {:.0f} s: {} NPCs joined an investigation in the window{}, "
              "{} output pins, OnInvestigated {}; signaled {}",
              HelperTag(), m_Helper.number, s_Watch, m_Helper.joins,
              m_Helper.backup ? fmt::format(" ({} guards)", m_Helper.answer.guards) : std::string(),
              m_HelperOutputs.load(std::memory_order_relaxed),
              m_HelperInvestigatedAt.load(std::memory_order_relaxed) >= 0.0
                  ? fmt::format("at {:.1f} s after the spawn",
                                m_HelperInvestigatedAt.load(std::memory_order_relaxed) - m_Helper.spawnedAt)
                  : std::string("not seen"),
              s_Done);

        if (m_Helper.backup && m_Helper.answer.guards == 0) {
            ++m_BackupCounts["no guard answered"];
            LogIf(true, "backup: backup #{}: no guard answered the {} in {:.0f} s ({} civilians did); {}",
                  m_Helper.backupNumber, fr::SoundEventName(m_Helper.sound), s_Watch, m_Helper.answer.others,
                  m_Helper.answer.others > 0 ? "the civilian notice was the one shown" : "no notice shown");
        }
    }

    if (s_Age >= s_DeleteAfter)
        RetireHelper("its time is up");
}

// --- Followed NPCs -----------------------------------------------------------------------------------------------

void InvestigationFeatures::SampleFollowed() {
    for (auto s_It = m_Followed.begin(); s_It != m_Followed.end();) {
        Followed& f = s_It->second;

        if (m_Now - f.joinedAt > fr::kInvestigatorWatchSeconds) {
            Line(f.fromHelper,
                 "investigations follow: {} no longer followed ({:.0f} s after his last join); last behavior {}",
                 NpcText(f.npc), fr::kInvestigatorWatchSeconds,
                 m_Behaviors.Last(reinterpret_cast<uintptr_t>(f.npc.actor))
                     ? fr::BehaviorName(*m_Behaviors.Last(reinterpret_cast<uintptr_t>(f.npc.actor)))
                     : std::string("unread"));

            if (FootstepOpen() && s_It->first == m_Footstep->repo)
                CancelFootstep(fr::FootstepOutcome::LostFollow,
                               fmt::format("{:.0f} s after his last join", fr::kInvestigatorWatchSeconds));

            if (const std::optional<fr::BackupCase> s_Case = m_Backup.Drop(s_It->first)) {
                ++m_BackupCounts["lost track"];
                LogIf(m_BackupOn,
                      "backup: {} disarmed: no longer followed ({:.0f} s after his last join) and not seen "
                      "back on his routine; no backup",
                      NpcText(f.npc), fr::kInvestigatorWatchSeconds);
            }

            s_It = m_Followed.erase(s_It);
            continue;
        }

        if (m_Now >= f.nextAt) {
            f.nextAt = m_Now + fr::kInvestigatorSampleEvery;

            if (const std::optional<int32_t> s_Type = BehaviorOf(f.npc.actor)) {
                // footsteps_investigate: every sample of an open case's hearer, changed or not (its timeouts need them).
                if (FootstepOpen() && s_It->first == m_Footstep->repo)
                    FootstepBehavior(*s_Type);

                const fr::BehaviorChange s_Change =
                    m_Behaviors.Observe(reinterpret_cast<uintptr_t>(f.npc.actor), *s_Type);

                if (s_Change.changed) {
                    const std::optional<std::array<float, 3>> s_Pos = PositionOf(f.npc.entity);
                    Line(f.fromHelper,
                         "investigations follow: {} at {:.2f} s of play ({:.1f} s after joining {}): {} -> {} "
                         "at {}, {} from where he joined, {} from 47{}",
                         NpcText(f.npc), m_Now, m_Now - f.joinedAt, fr::InvestigationTypeName(f.type),
                         s_Change.before ? fr::BehaviorName(*s_Change.before) : std::string("?"),
                         fr::BehaviorName(*s_Type), PosText(s_Pos), DistText(s_Pos, f.joinedPos),
                         DistText(s_Pos, HeroPosition()),
                         HelperOut() ? fmt::format(", {} from {} helper #{}", DistText(s_Pos, m_Helper.at),
                                                   m_Helper.backup ? "backup" : "footstep", m_Helper.number)
                                     : std::string());

                    if (m_BackupOn)
                        BackupBehavior(s_It->first, f, *s_Type);
                }
            }
        }

        ++s_It;
    }
}

// --- backup_guard ---------------------------------------------------------------------------------------------

void InvestigationFeatures::BackupJoin(const fr::SituationEvent& p_Event, const Npc* p_Npc,
                                       const std::optional<std::array<float, 3>>& p_Pos, bool p_Window) {
    const int s_Type = p_Event.investigationType.value_or(-1);
    const std::string s_Who = p_Npc ? NpcText(*p_Npc) : fr::FormatGuid(*p_Event.repo);
    const std::string s_Kind = p_Npc ? ActorTypeText(p_Npc->actor) : std::string("kind unread");

    // A joiner of a backup's helper: who answered it, and with which type (logged whatever it is).
    if (p_Window && m_Helper.backup) {
        const bool s_Guard = p_Npc && IsGuard(p_Npc->actor);
        const bool s_Answer = p_Event.investigationType && fr::AnswersBackupNoise(s_Type);
        const std::string s_TypeText =
            p_Event.investigationType ? fr::InvestigationTypeName(s_Type) : std::string("(no investigation type)");
        const fr::BackupNotice s_Notice = s_Answer ? m_Helper.answer.Join(s_Guard) : fr::BackupNotice::None;
        ++m_BackupCounts[s_Guard ? "helper joiner: guard" : "helper joiner: not a guard"];
        ++m_BackupCounts[fmt::format("helper joiner type: {}{}", s_TypeText, s_Answer ? "" : " (not an answer)")];
        LogIf(true,
              "backup: helper #{} (backup #{}) joiner at {:.1f} s of play ({:.1f} s after the spawn): {} [{}]{} "
              "{} {}, {} from the spot, {} from 47{}",
              m_Helper.number, m_Helper.backupNumber, m_Now, m_Now - m_Helper.spawnedAt, s_Who, s_Kind,
              s_Guard ? "" : " (NOT a guard)", p_Event.situation, s_TypeText, DistText(p_Pos, m_Helper.at),
              DistText(p_Pos, HeroPosition()),
              (m_Helper.joins > 1 ? fmt::format("; joiner {} of this helper", m_Helper.joins) : std::string()) +
                  (s_Answer ? fmt::format("; {} counts as an answer to the {} sound", s_TypeText,
                                          fr::SoundEventName(m_Helper.sound))
                            : fmt::format("; joined {} near the backup spot; not counted as an answer (no notice)",
                                          s_TypeText)));

        ShowBackupNotice(s_Notice, s_Who, s_Guard, p_Pos);

        return;
    }

    if (!fr::IsBackupNoise(s_Type)) {
        // Footsteps are counted only (they are many; footsteps_investigate has its own lines).
        if (s_Type == fr::kInvestigationFootsteps) {
            ++m_BackupCounts["not armed: footsteps"];
            return;
        }

        ++m_BackupCounts[fmt::format("not armed: {}", fr::InvestigationTypeName(s_Type))];
        LogIf(true, "backup: {} [{}] joined {} ({}) at {:.1f} s of play: not a noise distraction, not armed", s_Who,
              s_Kind, fr::InvestigationTypeName(s_Type), fr::NoiseKindName(fr::InvestigationKind(s_Type)), m_Now);
        return;
    }

    if (!m_Backup.Join(*p_Event.repo, s_Type, m_Now)) {
        LogIf(true, "backup: {} joined {} again at {:.1f} s of play: still armed from his first join", s_Who,
              fr::InvestigationTypeName(s_Type), m_Now);
        return;
    }

    ++m_BackupCounts["armed"];
    LogIf(true, "backup: {} [{}] armed at {:.1f} s of play: joined {} at {}, {} from 47{}", s_Who, s_Kind, m_Now,
          fr::InvestigationTypeName(s_Type), PosText(p_Pos), DistText(p_Pos, HeroPosition()),
          p_Npc && m_Followed.contains(*p_Event.repo) ? std::string()
                                                      : std::string(" (NOT followed: his behavior is not read, so "
                                                                    "his call and finish cannot be seen)"));
}

void InvestigationFeatures::BackupBehavior(const fr::Guid& p_Repo, const Followed& p_Follow, int32_t p_Type) {
    const fr::BackupCase* s_Before = m_Backup.Armed(p_Repo);

    if (!s_Before)
        return;

    const fr::BackupCase s_Case = *s_Before;
    const fr::BackupStep s_Step = m_Backup.Behavior(p_Repo, p_Type, m_Now);

    if (s_Step == fr::BackupStep::None)
        return;

    const std::optional<std::array<float, 3>> s_Pos = PositionOf(p_Follow.npc.entity);
    const std::string s_Who = NpcText(p_Follow.npc);
    const fr::BackupCase* s_After = m_Backup.Armed(p_Repo);

    switch (s_Step) {
        case fr::BackupStep::CallStarted:
            LogIf(true,
                  "backup: {} speaking at {:.2f} s of play ({:.1f} s after his join): {}; a radio call if it "
                  "lasts {:.1f} s",
                  s_Who, m_Now, m_Now - s_Case.joinedAt, fr::BehaviorName(p_Type), fr::kBackupCallMinSeconds);
            break;
        case fr::BackupStep::CallDone:
            LogIf(true,
                  "backup: {} radio call finished at {:.2f} s of play (the call took {:.1f} s; {:.1f} s after his "
                  "join): {}; a backup for him would set off {}-{} s from now",
                  s_Who, m_Now, m_Now - s_Case.callStartAt.value_or(m_Now), m_Now - s_Case.joinedAt,
                  fr::BehaviorName(p_Type), m_BackupDelays.min, m_BackupDelays.max);
            break;
        case fr::BackupStep::Bark:
            ++m_BackupCounts["short line, not a call"];
            LogIf(true,
                  "backup: {} stopped speaking at {:.2f} s of play after {:.1f} s: {}; under {:.1f} s, a short "
                  "line, not a radio call",
                  s_Who, m_Now, s_After ? s_After->lastBarkSeconds : 0.0, fr::BehaviorName(p_Type),
                  fr::kBackupCallMinSeconds);
            break;
        case fr::BackupStep::Moving:
            LogIf(true, "backup: {} heading over at {:.2f} s of play from {}: {}{}", s_Who, m_Now, PosText(s_Pos),
                  fr::BehaviorName(p_Type), s_Case.callDoneAt ? "" : " (no radio call so far)");
            break;
        case fr::BackupStep::Arrived: break; // below
        case fr::BackupStep::Finished: {
            const bool s_Animation = !fr::IsRoutineBehavior(p_Type);
            ++m_BackupCounts[s_Case.callDoneAt || s_Case.arrived ? "finished normally"
                                                                 : "back to routine before the call"];
            LogIf(true,
                  "backup: {} finished at {:.2f} s of play ({:.1f} s after his join, {}{}): {}{}; disarmed, no "
                  "backup",
                  s_Who, m_Now, m_Now - s_Case.joinedAt,
                  s_Case.arrived      ? "after checking the spot"
                  : s_Case.callDoneAt ? "after his call, without reaching the spot"
                                      : "before any radio call",
                  s_Case.arrived && !s_Case.callDoneAt ? ", with no radio call" : "", fr::BehaviorName(p_Type),
                  s_Animation ? " (out of his search animation)" : "");
            break;
        }
        case fr::BackupStep::None: break;
    }

    // A change can also set him off or bring him to the spot (e.g. out of the call straight into the walk).
    if (s_After && s_After->moved && !s_Case.moved && s_Step != fr::BackupStep::Moving)
        LogIf(true, "backup: {} heading over at {:.2f} s of play from {}{}", s_Who, m_Now, PosText(s_Pos),
              s_After->callDoneAt ? "" : " (no radio call so far)");

    if (s_After && s_After->arrived && !s_Case.arrived) {
        if (s_Pos)
            m_Backup.SetSpot(p_Repo, *s_Pos);

        LogIf(true,
              "backup: {} reached the spot at {:.2f} s of play: {} at {} ({} from where he joined); this is "
              "where a backup would go",
              s_Who, m_Now, fr::BehaviorName(p_Type), PosText(s_Pos), DistText(s_Pos, p_Follow.joinedPos));
    }
}

void InvestigationFeatures::BackupTakedown(const fr::Guid& p_Repo, const std::string& p_Who,
                                           const std::optional<int32_t>& p_Last,
                                           const std::optional<std::array<float, 3>>& p_Pos) {
    const fr::BackupTakedown s_Result =
        m_Backup.Takedown(p_Repo, m_Now, m_Seed ? m_Seed->seed : 0, m_RollScene, m_BackupDelays, p_Pos);
    const std::string s_Last = p_Last ? fr::BehaviorName(*p_Last) : std::string("unread");

    if (s_Result.verdict != fr::BackupVerdict::Scheduled) {
        ++m_BackupCounts[fmt::format("takedown: {}", fr::BackupVerdictName(s_Result.verdict))];
        const std::string s_Joined =
            s_Result.armed ? fmt::format(", {:.1f} s after he joined {}", m_Now - s_Result.armed->joinedAt,
                                         fr::InvestigationTypeName(s_Result.armed->type))
                           : std::string();

        if (s_Result.verdict == fr::BackupVerdict::NoCall && s_Result.armed) {
            const fr::BackupCase& c = *s_Result.armed;
            std::string s_Why = c.callStartAt ? fmt::format("cut off {:.1f} s into a line, under {:.1f} s",
                                                            m_Now - *c.callStartAt, fr::kBackupCallMinSeconds)
                                              : std::string("none seen");

            if (c.barks > 0)
                s_Why += fmt::format("; {} short line(s) before, the last {:.1f} s", c.barks, c.lastBarkSeconds);

            LogIf(true, "backup: takedown {} at {:.1f} s of play{}; last behavior {}: no radio call, no backup ({})",
                  p_Who, m_Now, s_Joined, s_Last, s_Why);
            return;
        }

        LogIf(true, "backup: takedown {} at {:.1f} s of play{}; last behavior {}: no backup: {}", p_Who, m_Now,
              s_Joined, s_Last, fr::BackupVerdictName(s_Result.verdict));
        return;
    }

    const fr::BackupPlan& p = *s_Result.plan;
    ++m_BackupCounts["takedown: scheduled"];
    LogIf(true,
          "backup: takedown {} at {:.1f} s of play while investigating {} (last behavior {}); his call {} at "
          "{:.1f} s; backup #{} rolled {:.3f} -> {:.1f} s after the call ({}-{} s; {}): {}; spot {} ({})",
          p_Who, m_Now, fr::InvestigationTypeName(p.type), s_Last, p.cutOff ? "was cut off" : "finished", p.callDoneAt,
          p.number, p.roll.u, p.roll.seconds, m_BackupDelays.min, m_BackupDelays.max, SeedText(),
          p.immediate ? fmt::format("already past, dispatched now ({:.1f} s)", p.dueAt)
                      : fmt::format("scheduled at {:.1f} s of play ({:.1f} s from now)", p.dueAt, p.dueAt - m_Now),
          PosText(p.spot), fr::BackupSpotSourceName(p.spotSource));
}

void InvestigationFeatures::BackupTick() {
    if (!m_BackupOn)
        return;

    for (fr::BackupPlan& s_Plan : m_Backup.Due(m_Now))
        m_BackupWaiting.push_back(std::move(s_Plan));

    if (m_BackupWaiting.empty())
        return;

    // One helper at a time: a footstep helper makes way, another backup's is waited for.
    if (HelperOut()) {
        if (m_Helper.backup) {
            if (!m_BackupWaitLogged) {
                m_BackupWaitLogged = true;
                LogIf(true, "backup: backup #{} is due at {:.1f} s of play; waiting for backup #{}'s helper to go",
                      m_BackupWaiting.front().number, m_Now, m_Helper.backupNumber);
            }

            return;
        }

        RetireHelper("a backup guard is due");
    }

    m_BackupWaitLogged = false;
    const fr::BackupPlan s_Plan = m_BackupWaiting.front();
    m_BackupWaiting.pop_front();
    Dispatch(s_Plan); // counts its own outcome
}

bool InvestigationFeatures::Dispatch(const fr::BackupPlan& p_Plan) {
    if (!p_Plan.spot) {
        ++m_BackupCounts["not sent: no spot"];
        LogIf(true,
              "backup: backup #{} due at {:.1f} s of play: no spot (neither where the guard reached the noise "
              "nor where he went down was read); not sent",
              p_Plan.number, m_Now);
        return false;
    }

    const std::array<float, 3> s_Spot = *p_Plan.spot;

    // Who could come: living, standing NPCs (those taken down this scene are left out: a pacified one stays alive).
    // The guards on the spot's floor can widen the sound's range (civilians are read for the log). Each guard is
    // kept with his height above the spot.
    struct Guard {
        float distance;
        float height;
        const ZActor* actor;
    };
    std::vector<Guard> s_Guards;
    std::vector<float> s_SameFloorDistances;
    std::vector<std::pair<float, const ZActor*>> s_Civilians;
    std::vector<ActorRef> s_Actors;
    size_t s_Total = 0;
    size_t s_DownedSkipped = 0;

    if (Globals::ActorManager && !ReadActivatedActors(Globals::ActorManager, s_Actors, &s_Total)) {
        for (const auto& [s_Entity, s_Actor] : s_Actors) {
            if (!s_Entity || !s_Actor)
                continue;

            ActorCore s_Core {};

            if (SafeActorCore(s_Actor, &s_Core) || !s_Core.alive || !s_Core.enabled)
                continue;

            if (const std::optional<fr::Guid> s_Repo = ReadActorRepo(s_Entity); s_Repo && m_Downed.contains(*s_Repo)) {
                ++s_DownedSkipped;
                continue;
            }

            const std::optional<std::array<float, 3>> s_Pos = PositionOf(s_Entity);

            if (!s_Pos)
                continue;

            const float s_Distance = static_cast<float>(fr::Distance(*s_Pos, s_Spot));

            if (!IsGuard(s_Actor)) {
                s_Civilians.emplace_back(s_Distance, s_Actor);
                continue;
            }

            const float s_Height = (*s_Pos)[2] - s_Spot[2];
            s_Guards.push_back({s_Distance, s_Height, s_Actor});

            if (std::abs(s_Height) <= fr::kBackupFloorHeight)
                s_SameFloorDistances.push_back(s_Distance);
        }
    }

    std::sort(s_Guards.begin(), s_Guards.end(), [](const Guard& a, const Guard& b) { return a.distance < b.distance; });
    std::sort(s_Civilians.begin(), s_Civilians.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<float> s_GuardDistances;
    std::vector<float> s_CivilianDistances;
    std::string s_Nearest;
    std::string s_NearestSameFloor = "none";

    for (size_t i = 0; i < s_Guards.size(); ++i) {
        const Guard& s_Guard = s_Guards[i];
        s_GuardDistances.push_back(s_Guard.distance);
        const bool s_SameFloor = std::abs(s_Guard.height) <= fr::kBackupFloorHeight;
        const std::string s_Text =
            fmt::format("{} at {:.1f} m ({:+.1f} m above the spot{})", ReadActor(s_Guard.actor).name, s_Guard.distance,
                        s_Guard.height, s_SameFloor ? ", same floor" : "");

        if (i < fr::kBackupNearestLogged)
            s_Nearest += (s_Nearest.empty() ? "" : ", ") + s_Text;

        if (s_SameFloor && s_NearestSameFloor == "none")
            s_NearestSameFloor = s_Text;
    }

    for (const auto& s_Civilian : s_Civilians)
        s_CivilianDistances.push_back(s_Civilian.first);

    const fr::BackupReach s_Reach = fr::PlanBackupReach(s_GuardDistances, s_CivilianDistances, s_SameFloorDistances);
    const std::string s_NearestCivilian =
        s_Civilians.empty() ? std::string("none")
                            : fmt::format("{} [{}] at {:.1f} m", ReadActor(s_Civilians.front().second).name,
                                          ActorTypeText(s_Civilians.front().second), s_Civilians.front().first);
    const std::string s_Wanted =
        s_Guards.empty() ? std::string("no guard read")
        : !s_Reach.widened
            ? fmt::format(
                  "{:.1f} m (the set range; it reaches the nearest guard on the spot's floor, or none is on it)",
                  s_Reach.wanted)
            : fmt::format(
                  "{:.1f} m: widened past {:.0f} m to the nearest guard {}({:.1f} m) + {:.0f} m, at most {:.0f} m",
                  s_Reach.wanted, fr::kBackupRange, s_Reach.nearestSameFloor ? "on the spot's floor " : "",
                  s_Reach.nearestSameFloor.value_or(s_Reach.nearestGuard.value_or(0.0f)), fr::kBackupRadiusMargin,
                  fr::kBackupRadiusMax);
    const std::string s_Facts = fmt::format(
        "{} living guards ({} on the spot's floor, within {:.0f} m of its height), {} civilians ({} taken "
        "down left out); nearest guards {}; nearest on the floor {}; nearest civilian {}; wanted reach {}",
        s_Guards.size(), s_SameFloorDistances.size(), fr::kBackupFloorHeight, s_Civilians.size(), s_DownedSkipped,
        s_Nearest.empty() ? std::string("none") : s_Nearest, s_NearestSameFloor, s_NearestCivilian, s_Wanted);

    // No guard in reach: no backup.
    if (!s_Reach.go) {
        ++m_BackupCounts[s_Guards.empty() ? "not sent: no guard read" : "not sent: no guard in reach"];
        LogIf(true,
              "backup: backup #{} due at {:.1f} s of play ({:.1f} s after the takedown) at {} ({}): NOT sent, {}; "
              "{}",
              p_Plan.number, m_Now, m_Now - p_Plan.takedownAt, PosText(s_Spot),
              fr::BackupSpotSourceName(p_Plan.spotSource),
              s_Guards.empty() ? std::string("no living guard read")
                               : fmt::format("no guard closer than {:.1f} m", s_Reach.radius),
              s_Facts);
        return false;
    }

    const float s_Radius = s_Reach.radius;
    LogIf(true,
          "backup: backup #{} dispatched at {:.1f} s of play ({:.1f} s after the takedown) to {} ({}): {}; sound "
          "range {:.1f} m, {} guards in it",
          p_Plan.number, m_Now, m_Now - p_Plan.takedownAt, PosText(s_Spot), fr::BackupSpotSourceName(p_Plan.spotSource),
          s_Facts, s_Radius, s_Reach.guardsInReach);

    const int32_t s_Sound = fr::kBackupSoundEvent;
    std::string s_Why;
    HelperKind s_Kind = HelperKind::Helper;
    bool s_Spawned = TrySpawn(HelperKind::Helper, m_Resources[0], s_Spot, &s_Why, true, s_Radius, s_Sound);

    if (!s_Spawned) {
        LogIf(true, "backup: the {} did not spawn ({}); trying the {}", kHelperNames[0], s_Why, kHelperNames[1]);
        s_Why.clear();
        s_Kind = HelperKind::Attractor;
        s_Spawned = TrySpawn(HelperKind::Attractor, m_Resources[1], s_Spot, &s_Why, true, s_Radius, s_Sound);
    }

    if (!s_Spawned) {
        ++m_BackupCounts["not sent: the helper did not spawn"];
        LogIf(true, "backup: backup #{} not sent: {}", p_Plan.number, s_Why);
        return false;
    }

    m_Helper.backupNumber = p_Plan.number;
    m_Helper.forActor = fmt::format("backup #{}", p_Plan.number);

    // The notice waits for a guard to answer (BackupJoin): a helper nobody answers must not promise a backup.
    LogIf(true, "backup: backup #{}: helper #{} ({}) out at {}; pins fired: {}; notice {}", p_Plan.number,
          m_Helper.number, kHelperNames[static_cast<int>(s_Kind)], PosText(m_Helper.at),
          m_Helper.fired.empty() ? std::string("none") : Join(m_Helper.fired),
          m_NoticeOn ? fmt::format("when a guard answers within {:.0f} s", fr::kBackupWatchSeconds)
                     : std::string("off"));
    ++m_BackupCounts["dispatched"];
    return true;
}

// Every NPC's behavior: an NPC entering or leaving an investigation behavior
// while a situation (or a footstep helper) is followed.
void InvestigationFeatures::ScanNpcs() {
    if (!Globals::ActorManager || !Globals::BehaviorService)
        return;

    std::vector<ActorRef> s_Actors;
    size_t s_Total = 0;

    if (ReadActivatedActors(Globals::ActorManager, s_Actors, &s_Total))
        return;

    std::optional<std::array<float, 3>> s_Hero;
    bool s_HeroRead = false;
    std::vector<const void*> s_Followed;

    for (const auto& [s_Repo, s_Follow] : m_Followed)
        s_Followed.push_back(s_Follow.npc.actor);

    for (const auto& [s_Entity, s_Actor] : s_Actors) {
        // The followed NPCs have their own, faster lines.
        if (!s_Entity || !s_Actor || std::find(s_Followed.begin(), s_Followed.end(), s_Actor) != s_Followed.end())
            continue;

        const std::optional<int32_t> s_Type = BehaviorOf(s_Actor);

        if (!s_Type)
            continue;

        const uint64_t s_Key = reinterpret_cast<uintptr_t>(s_Actor);
        const std::optional<int32_t> s_Before = m_ScanBehaviors.Last(s_Key);
        const bool s_In = fr::IsInvestigationBehavior(*s_Type);
        const bool s_WasIn = s_Before && fr::IsInvestigationBehavior(*s_Before);
        m_ScanBehaviors.Observe(s_Key, *s_Type);

        if ((!s_In && !s_WasIn) || (s_Before && *s_Before == *s_Type))
            continue;

        Npc s_Npc {s_Entity, s_Actor, 0, ReadActor(s_Actor).name};
        std::string s_Class;
        ReadEntityClassName(s_Entity, s_Class, &s_Npc.id);
        const std::optional<std::array<float, 3>> s_Pos = PositionOf(s_Entity);

        if (s_In && !s_WasIn)
            ScanBackupAnswer(s_Npc, *s_Type, s_Pos);

        // The scan's own budget, so a busy scene leaves lines for the followed NPCs and takedowns.
        if (++m_ScanLines > kMaxScanLines) {
            if (m_ScanLines == kMaxScanLines + 1)
                Line(HelperWindow(),
                     "investigations scan: {} lines logged this scene; further scan lines are not logged",
                     kMaxScanLines);

            continue;
        }

        if (!s_HeroRead) {
            s_Hero = HeroPosition();
            s_HeroRead = true;
        }

        Line(HelperWindow(), "investigations scan: {} [{}] at {:.1f} s of play: {} -> {} at {}, {} from 47{}",
             NpcText(s_Npc), ActorTypeText(s_Actor), m_Now, s_Before ? fr::BehaviorName(*s_Before) : std::string("?"),
             fr::BehaviorName(*s_Type), PosText(s_Pos), DistText(s_Pos, s_Hero),
             HelperOut() ? fmt::format(", {} from footstep helper #{}", DistText(s_Pos, m_Helper.at), m_Helper.number)
                         : std::string());
    }
}

void InvestigationFeatures::ScanBackupAnswer(const Npc& p_Npc, int32_t p_Type,
                                             const std::optional<std::array<float, 3>>& p_Pos) {
    if (!m_Helper.backup || !HelperOut() || m_Helper.disabled || m_Now - m_Helper.spawnedAt > fr::kBackupWatchSeconds)
        return;

    if (!p_Pos || fr::Distance(*p_Pos, m_Helper.at) > m_Helper.reach)
        return;

    const bool s_Guard = IsGuard(p_Npc.actor);
    const fr::BackupNotice s_Notice = m_Helper.answer.Join(s_Guard);
    ++m_BackupCounts[s_Guard ? "searcher: guard" : "searcher: not a guard"];
    LogIf(true,
          "backup: helper #{} (backup #{}) searcher at {:.1f} s of play ({:.1f} s after the spawn): {} [{}]{} "
          "into {}, {} from the spot; counts as an answer to the {} sound",
          m_Helper.number, m_Helper.backupNumber, m_Now, m_Now - m_Helper.spawnedAt, NpcText(p_Npc),
          ActorTypeText(p_Npc.actor), s_Guard ? "" : " (NOT a guard)", fr::BehaviorName(p_Type),
          DistText(p_Pos, m_Helper.at), fr::SoundEventName(m_Helper.sound));
    ShowBackupNotice(s_Notice, NpcText(p_Npc), s_Guard, p_Pos);
}

// The first to answer decides the notice, one per backup: a guard's "Backup is on the way", a civilian's
// "Someone is coming to investigate".
void InvestigationFeatures::ShowBackupNotice(fr::BackupNotice p_Notice, const std::string& p_Who, bool p_Guard,
                                             const std::optional<std::array<float, 3>>& p_Pos) {
    if (p_Notice == fr::BackupNotice::None)
        return;

    ++m_BackupCounts[p_Guard ? "answered first by a guard" : "answered first by a civilian"];

    if (m_NoticeOn) {
        m_NoticeKind.store(static_cast<uint32_t>(p_Notice), std::memory_order_relaxed);
        m_NoticeSerial.fetch_add(1, std::memory_order_release);
    }

    LogIf(true,
          "backup: backup #{}: {} ({}) answered the {} first at {:.1f} s of play ({:.1f} s after the spawn), {} "
          "from the spot (sound range {:.1f} m); notice {}",
          m_Helper.backupNumber, p_Who, p_Guard ? "a guard" : "a CIVILIAN", fr::SoundEventName(m_Helper.sound), m_Now,
          m_Now - m_Helper.spawnedAt, DistText(p_Pos, m_Helper.at), m_Helper.reach,
          m_NoticeOn ? fmt::format("\"{}\" shown", fr::BackupNoticeText(p_Notice)) : std::string("off"));
}

std::optional<InvestigationFeatures::Npc> InvestigationFeatures::FindNpc(const fr::Guid& p_Repo) {
    auto s_It = m_Index.find(p_Repo);

    if (s_It == m_Index.end() && m_Now - m_IndexBuiltAt >= kIndexRebuildEvery) {
        RebuildIndex();
        s_It = m_Index.find(p_Repo);
    }

    if (s_It == m_Index.end())
        return std::nullopt;

    return s_It->second;
}

void InvestigationFeatures::RebuildIndex() {
    m_IndexBuiltAt = m_Now;

    if (!Globals::ActorManager)
        return;

    std::vector<ActorRef> s_Actors;
    size_t s_Total = 0;

    if (ReadActivatedActors(Globals::ActorManager, s_Actors, &s_Total))
        return;

    std::vector<ActorRef> s_Spawned;
    size_t s_SpawnedTotal = 0;

    if (!ReadSpawnedActors(Globals::ActorManager, s_Spawned, &s_SpawnedTotal))
        s_Actors.insert(s_Actors.end(), s_Spawned.begin(), s_Spawned.end());

    for (const auto& [s_Entity, s_Actor] : s_Actors) {
        if (!s_Entity || !s_Actor)
            continue;

        const std::optional<fr::Guid> s_Repo = ReadActorRepo(s_Entity);

        if (!s_Repo || m_Index.contains(*s_Repo))
            continue;

        Npc s_Npc {s_Entity, s_Actor, 0, ReadActor(s_Actor).name};
        std::string s_Class;
        ReadEntityClassName(s_Entity, s_Class, &s_Npc.id);
        m_Index.emplace(*s_Repo, std::move(s_Npc));
    }
}

std::string InvestigationFeatures::NpcText(const Npc& p_Npc) const {
    return fmt::format("{} {}", p_Npc.name, IdText(p_Npc.id));
}

std::optional<std::array<float, 3>> InvestigationFeatures::PositionOf(ZEntityType** p_Entity) {
    if (!p_Entity)
        return std::nullopt;

    const PositionRead s_Read = ReadPosition(p_Entity, true);

    if (!s_Read.at)
        return std::nullopt;

    return std::array<float, 3> {s_Read.at->x, s_Read.at->y, s_Read.at->z};
}

std::optional<std::array<float, 3>> InvestigationFeatures::HeroPosition() {
    return PositionOf(ReadHeroEntity());
}

std::optional<int32_t> InvestigationFeatures::BehaviorOf(const ZActor* p_Actor) {
    const ZBehaviorService* s_Service = Globals::BehaviorService;

    if (!p_Actor || !s_Service)
        return std::nullopt;

    ActorCore s_Core {};

    if (SafeActorCore(p_Actor, &s_Core) || s_Core.runtimeId < 0 || s_Core.runtimeId >= 500)
        return std::nullopt;

    BehaviorOut s_Out {};

    if (SafeBehaviorRead(s_Service, s_Core.runtimeId, &s_Out) || !s_Out.hasBehavior)
        return std::nullopt;

    return s_Out.type;
}
