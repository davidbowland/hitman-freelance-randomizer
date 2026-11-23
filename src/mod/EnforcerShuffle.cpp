// Enforcer shuffle (see EnforcerShuffle.h). Every raw engine read
// and write follows the guard pattern in Guard.h.

#include "EnforcerShuffle.h"

#include "../core/GuardWeapons.h"   // fr::ActorTypeName
#include "../core/SceneKinds.h"     // mild scenes, mission brick, role names
#include "../core/VariantCleanup.h" // fr::VariantRemovedRepos
#include "ActorReads.h"
#include "Collections.h" // fr::ArrayElements
#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "Scene.h"
#include "SceneOf.h"

#include <Globals.h>

#include <Glacier/ZActor.h>
#include <Glacier/ZOutfit.h>
#include <Glacier/ZResourceID.h>
#include <Glacier/ZTypeRegistry.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <map>
#include <set>
#include <string>

namespace {

constexpr size_t kMaxListElements = 4'096;  // m_aActors lists
constexpr size_t kMaxEnforced = 64;         // m_aEnforcedOutfits elements
constexpr size_t kEnforcedElementSize = 16; // TEntityRef: the entity, then the interface
constexpr size_t kMaxIdsLogged = 8;
constexpr const char* kSentryClass = "ZSentryZoneEntity";

using Header = EnforcerShuffle::Header;
static_assert(sizeof(Header) == 24);

struct ActorBits {
    bool startEnabled;
    bool crowd;
    bool activeSentry;
    bool hasKit;
    int32_t actorType;
    uint8_t outfit[16];
};

__declspec(noinline) void ActorBitsOf(const ZActor* p_Actor, ActorBits* p_Out) {
    p_Out->startEnabled = p_Actor->m_bStartEnabled;
    p_Out->crowd = p_Actor->m_bCrowdCharacter;
    p_Out->activeSentry = p_Actor->m_bActiveSentry;
    const ZGlobalOutfitKit* s_Kit = p_Actor->m_rOutfit.m_pInterfaceRef;
    p_Out->hasKit = s_Kit != nullptr;
    p_Out->actorType = s_Kit ? static_cast<int32_t>(s_Kit->m_eActorType) : 0;
    std::memcpy(p_Out->outfit, &p_Actor->m_OutfitRepositoryID, sizeof(p_Out->outfit));
}

__declspec(noinline) DWORD SafeActorBits(const ZActor* p_Actor, ActorBits* p_Out) {
    DWORD s_Code = 0;

    __try {
        ActorBitsOf(p_Actor, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// The header's address: the member's address only (no read).
uintptr_t HeaderAddress(const ZActor* p_Actor) {
    return reinterpret_cast<uintptr_t>(&p_Actor->m_aEnforcedOutfits);
}

// What one header holds where it is stored (inline elements live in the header itself).
struct List {
    bool readable = false;
    Header header;
    size_t count = 0;
    bool isInline = false;
    uintptr_t elements = 0;
};

List ReadList(uintptr_t p_Address) {
    List s_Out;

    if (ReadEngineBytes(p_Address, &s_Out.header, sizeof(Header)))
        return s_Out;

    const auto s_Span = fr::ArrayElements(p_Address, s_Out.header.q[0], s_Out.header.q[1], s_Out.header.q[2],
                                          kEnforcedElementSize, kMaxEnforced);

    if (!s_Span)
        return s_Out;

    s_Out.readable = true;
    s_Out.count = s_Span->count;
    s_Out.isInline = s_Span->isInline;
    s_Out.elements = s_Span->begin;
    return s_Out;
}

// The profession entities' ids of a list, the first kMaxIdsLogged.
std::string ProfessionIds(const List& p_List) {
    std::string s_Text;

    for (size_t i = 0; i < p_List.count && i < kMaxIdsLogged; ++i) {
        ZEntityType** s_Entity = nullptr;
        std::string s_Class;
        uint64_t s_Id = 0;

        if (!ReadEngineBytes(p_List.elements + i * kEnforcedElementSize, &s_Entity, sizeof(s_Entity)) && s_Entity &&
            !ReadEntityClassName(s_Entity, s_Class, &s_Id))
            s_Text += fmt::format("{}{}", s_Text.empty() ? "" : ", ", IdText(s_Id));
        else
            s_Text += fmt::format("{}<unreadable>", s_Text.empty() ? "" : ", ");
    }

    if (p_List.count > kMaxIdsLogged)
        s_Text += fmt::format(", +{} more", p_List.count - kMaxIdsLogged);

    return s_Text;
}

// The actors one m_aActors list holds (entity pointers), or why not.
struct ActorList {
    std::vector<ZEntityType**> entities;
    std::string why; // empty when read
};

ActorList ReadActorList(ZEntityType** p_Owner) {
    ActorList s_Out;
    const PropertyLocation s_Location = LocateProperty(p_Owner, "m_aActors");

    if (s_Location.status != PropertyLocation::Status::Ok) {
        s_Out.why = s_Location.status == PropertyLocation::Status::Fault
                        ? "m_aActors " + FaultText(s_Location.faultCode)
                        : std::string("m_aActors not readable");
        return s_Out;
    }

    if (!s_Location.isArray || (s_Location.elementSize != 16 && s_Location.elementSize != 8)) {
        s_Out.why = fmt::format("m_aActors is {} (element {} bytes)",
                                s_Location.typeName[0] ? s_Location.typeName : "<unknown>", s_Location.elementSize);
        return s_Out;
    }

    Header s_Array;

    if (const uint32_t s_Code = ReadEngineBytes(s_Location.address, &s_Array, sizeof(s_Array))) {
        s_Out.why = "array " + FaultText(s_Code);
        return s_Out;
    }

    const auto s_Span = fr::ArrayElements(s_Location.address, s_Array.q[0], s_Array.q[1], s_Array.q[2],
                                          s_Location.elementSize, kMaxListElements);

    if (!s_Span) {
        s_Out.why = "implausible array";
        return s_Out;
    }

    for (size_t i = 0; i < s_Span->count; ++i) {
        ZEntityType** s_Entity = nullptr;

        if (const uint32_t s_Code =
                ReadEngineBytes(s_Span->begin + i * s_Location.elementSize, &s_Entity, sizeof(s_Entity))) {
            s_Out.why = fmt::format("element {} {}", i, FaultText(s_Code));
            s_Out.entities.clear();
            return s_Out;
        }

        s_Out.entities.push_back(s_Entity);
    }

    return s_Out;
}

// What the shuffle knows about one actor.
struct ActorInfo {
    ZEntityType** entity = nullptr;
    const ZActor* actor = nullptr;
    uint64_t id = 0;
    uint64_t brickRid = 0;
    std::optional<std::string> name;
    std::optional<ActorBits> bits;
    List list;
};

std::string ActorLine(const ActorInfo& p_Info) {
    const std::string s_Kind =
        p_Info.bits && p_Info.bits->hasKit ? fr::ActorTypeName(p_Info.bits->actorType) : std::string("kind unknown");
    return fmt::format("{} {} [{}]", p_Info.name ? fmt::format("\"{}\"", *p_Info.name) : "<name unreadable>",
                       IdText(p_Info.id), s_Kind);
}

// The inclusions an actor needed, for its move line: " (possible target, start disabled, sentry)", or empty.
std::string Tags(const fr::EnforcerActor& p_Actor) {
    std::string s_Text;

    for (const auto& [s_On, s_Name] :
         {std::pair {p_Actor.inTargetPool, "possible target"},
          std::pair {!p_Actor.startEnabled.value_or(true), "start disabled"}, std::pair {p_Actor.sentry, "sentry"}}) {
        if (s_On)
            s_Text += fmt::format("{}{}", s_Text.empty() ? " (" : ", ", s_Name);
    }

    return s_Text.empty() ? s_Text : s_Text + ")";
}

fr::Guid OutfitOf(const ActorBits& p_Bits) {
    fr::Guid s_Guid;
    std::memcpy(&s_Guid, p_Bits.outfit, sizeof(s_Guid));
    return s_Guid;
}

constexpr std::string_view kTag = "enforcers";

} // namespace

void EnforcerShuffle::Configure(bool p_On, double p_ShareMin, double p_ShareMax, bool p_AnyBrick,
                                const fr::EnforcerInclude& p_Include) {
    m_On = p_On;
    m_Shares = fr::ClampEnforcerShares(p_ShareMin, p_ShareMax);
    m_AnyBrick = p_AnyBrick;
    m_Include = p_Include;
}

void EnforcerShuffle::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                     const std::optional<fr::SeedResolution>& p_Seed,
                                     bool p_TargetPoolWaiting) noexcept {
    if (m_On)
        Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context, p_Seed, p_TargetPoolWaiting); });
}

void EnforcerShuffle::OnSceneClear() noexcept {
    m_Done = false;
}

void EnforcerShuffle::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                   const std::optional<fr::SeedResolution>& p_Seed, bool p_TargetPoolWaiting) {
    if (!p_Context)
        return;

    const char* s_Stage = nullptr;

    switch (p_Stage) {
        case ESceneLoadingStage::eLoading_AssetsLoaded: m_Done = false; return;
        case ESceneLoadingStage::eLoading_SceneAllocated: s_Stage = "eLoading_SceneAllocated"; break;
        // Only when the bricks had no entities at eLoading_SceneAllocated.
        case ESceneLoadingStage::eLoading_SceneStarted: s_Stage = "eLoading_SceneStarted"; break;
        default: return;
    }

    if (m_Done)
        return;

    const std::string s_Scene =
        SceneOf(p_Context); // a full variant (Landslide, The Author) counts as its contract's scene
    const std::string_view s_Type = p_Context->m_SceneInitParameters.m_Type.ToStringView();

    if (!fr::IsFreelancerMission(s_Type, s_Scene)) {
        m_Done = true;
        return;
    }

    if (!fr::IsMildMissionScene(s_Scene) && !fr::IsShowdownScene(s_Scene)) {
        m_Done = true;
        LogTo(fr::LogTopic::Targets, "enforcers: {}: neither a mild mission nor a showdown; enforcers left as they are",
              s_Scene);
        return;
    }

    // Rolls follow the dials' scene seed; an unknown one would give every campaign the same enforcers.
    if (!p_Seed || p_Seed->source == fr::SeedSource::Fallback || p_Seed->seed == 0) {
        m_Done = true;
        LogTo(fr::LogTopic::Targets, "enforcers: {}: {}; enforcers left as they are", s_Scene,
              p_Seed ? fmt::format("seed {} ({}) is unknown", p_Seed->seed, fr::SeedSourceName(p_Seed->source))
                     : std::string("no seed resolved for this mission"));
        return;
    }

    // The target pool shuffle retries at eLoading_SceneStarted: wait for it, so the pool read is the final one.
    if (p_TargetPoolWaiting && p_Stage == ESceneLoadingStage::eLoading_SceneAllocated) {
        LogTo(fr::LogTopic::Targets,
              "enforcers: {} at {}: the target pool shuffle has not written yet; tried again at "
              "eLoading_SceneStarted",
              s_Scene, s_Stage);
        return;
    }

    m_Done = Run(p_Context, *p_Seed, s_Stage);
}

bool EnforcerShuffle::Run(ZEntitySceneContext* p_Context, const fr::SeedResolution& p_Seed, const char* p_Stage) {
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    const std::string s_Scene = SceneOf(p_Context);
    const bool s_LastChance = std::string_view(p_Stage) == "eLoading_SceneStarted";
    const auto s_Untouched = [&](const std::string& p_Why) {
        LogTo(fr::LogTopic::Targets, "enforcers: {} at {}: {}; enforcers left as they are", s_Scene, p_Stage, p_Why);
        return true;
    };

    // The mission brick's actors are never touched: in a showdown that keeps the suspects, lookouts and assassins.
    // A full variant's is the package's copy of mission_mild_bingo (SceneOf.h).
    const auto s_BrickPath = MissionBrickOf(s_Scene);

    if (!s_BrickPath)
        return s_Untouched("no mission brick path");

    const uint64_t s_MissionRid =
        ZRuntimeResourceID::FromString(fmt::format("[{}].pc_entitytype", *s_BrickPath)).GetID();
    bool s_MissionLoaded = false;

    for (size_t i = 0; i < s_Bricks.size(); ++i)
        s_MissionLoaded = s_MissionLoaded || s_Bricks[i].m_RuntimeResourceID.GetID() == s_MissionRid;

    if (!s_MissionLoaded) {
        if (s_LastChance)
            return s_Untouched(fmt::format("mission brick {} not loaded", *s_BrickPath));

        LogTo(fr::LogTopic::Targets,
              "enforcers: {} at {}: mission brick not loaded yet; tried again at eLoading_SceneStarted", s_Scene,
              p_Stage);
        return false;
    }

    if (!*Globals::TypeRegistry)
        return s_Untouched("no type registry");

    const STypeID* s_ActorType = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("ZActor")));

    if (!s_ActorType)
        return s_Untouched("ZActor is not in the type registry");

    // One walk: every actor, every target picker and every sentry zone.
    const auto s_Start = std::chrono::steady_clock::now();
    std::vector<ActorInfo> s_Actors;
    std::vector<std::pair<ZEntityType**, uint64_t>> s_Pickers; // entity, id
    std::vector<std::pair<ZEntityType**, uint64_t>> s_Sentries;
    std::string s_Class;
    size_t s_InterfaceFaults = 0;

    const WalkStats s_Walk = VisitAllEntities(s_Bricks, [&](const VisitedEntity& p_Visit) {
        uint64_t s_Id = 0;

        if (ReadEntityClassName(p_Visit.entity, s_Class, &s_Id))
            return false;

        if (s_Class == kTargetPickerClass) {
            s_Pickers.emplace_back(p_Visit.entity, s_Id);
            return true;
        }

        if (s_Class == kSentryClass) {
            s_Sentries.emplace_back(p_Visit.entity, s_Id);
            return true;
        }

        void* s_Actor = nullptr;

        if (SafeInterface(p_Visit.entity, s_ActorType, &s_Actor)) {
            ++s_InterfaceFaults;
            return false;
        }

        if (!s_Actor)
            return false;

        ActorInfo s_Info;
        s_Info.entity = p_Visit.entity;
        s_Info.actor = static_cast<const ZActor*>(s_Actor);
        s_Info.id = s_Id;
        s_Info.brickRid = p_Visit.brickRid;
        s_Actors.push_back(std::move(s_Info));
        return true;
    });
    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();

    if (s_Walk.visited == 0) {
        if (s_LastChance)
            return s_Untouched("the bricks have no entities");

        LogTo(fr::LogTopic::Targets, "enforcers: {} at {}: no entities yet; tried again at eLoading_SceneStarted",
              s_Scene, p_Stage);
        return false;
    }

    LogTo(
        fr::LogTopic::Targets,
        "enforcers: {} at {}: seed {} ({}); walk: {} bricks, {} entities, {} faults{}, {} interface faults, {:.1f} ms: "
        "{} actors, {} {}, {} {}",
        s_Scene, p_Stage, p_Seed.seed, fr::SeedSourceName(p_Seed.source), s_Bricks.size(), s_Walk.visited,
        s_Walk.faults, s_Walk.capped ? " (capped)" : "", s_InterfaceFaults, s_Ms, s_Actors.size(), s_Pickers.size(),
        kTargetPickerClass, s_Sentries.size(), kSentryClass);

    if (s_Walk.capped)
        return s_Untouched("the walk stopped at its cap, so some actors were not seen");

    // Possible targets and sentries: while they are excluded, every list must be read, or who they are is
    // unknown. Included, an unread list only leaves the summary's counts short. A showdown has no target picker
    // (its suspects are dedicated actors in the mission brick, rejected by that rule).
    if (s_Pickers.empty() && fr::IsMildMissionScene(s_Scene)) {
        if (!m_Include.targets)
            return s_Untouched(fmt::format("no {}, so the possible targets are unknown", kTargetPickerClass));

        LogTo(fr::LogTopic::Targets,
              "enforcers: {} at {}: no {}; possible targets take part, so the shuffle goes on (their count unknown)",
              s_Scene, p_Stage, kTargetPickerClass);
    }

    std::set<ZEntityType**> s_Targets;
    std::set<ZEntityType**> s_SentryActors;
    // False when an excluded category's list could not be read.
    const auto s_Collect = [&](const auto& p_Owners, const char* p_Class, bool p_Included,
                               std::set<ZEntityType**>& p_Out) {
        for (const auto& [s_Owner, s_Id] : p_Owners) {
            const ActorList s_List = ReadActorList(s_Owner);

            if (s_List.why.empty()) {
                p_Out.insert(s_List.entities.begin(), s_List.entities.end());
                continue;
            }

            if (!p_Included) {
                s_Untouched(fmt::format("{} {}: {}", p_Class, IdText(s_Id), s_List.why));
                return false;
            }

            LogTo(fr::LogTopic::Targets, "enforcers: {} at {}: {} {}: {}; its actors take part anyway (not counted)",
                  s_Scene, p_Stage, p_Class, IdText(s_Id), s_List.why);
        }

        return true;
    };

    if (!s_Collect(s_Pickers, kTargetPickerClass, m_Include.targets, s_Targets) ||
        !s_Collect(s_Sentries, kSentryClass, m_Include.sentries, s_SentryActors))
        return true;

    // Each actor's facts and list.
    std::map<uint64_t, size_t> s_HeapLists; // heap storage -> actors using it
    size_t s_Before = 0;

    for (ActorInfo& s_Info : s_Actors) {
        NameOut s_Name {};

        if (!SafeActorName(s_Info.actor, &s_Name))
            s_Info.name = s_Name.text;

        ActorBits s_Bits {};

        if (!SafeActorBits(s_Info.actor, &s_Bits))
            s_Info.bits = s_Bits;

        s_Info.list = ReadList(HeaderAddress(s_Info.actor));

        if (s_Info.list.readable && s_Info.list.count > 0) {
            ++s_Before;

            if (!s_Info.list.isInline)
                ++s_HeapLists[s_Info.list.elements];
        }
    }

    // A restart may reach eLoading_SceneAllocated before the lists are filled.
    if (s_Before == 0) {
        if (s_LastChance)
            return s_Untouched(fmt::format("no actor has an enforced-outfit list ({} actors)", s_Actors.size()));

        LogTo(fr::LogTopic::Targets,
              "enforcers: {} at {}: no actor has an enforced-outfit list yet ({} actors); tried again at "
              "eLoading_SceneStarted",
              s_Scene, p_Stage, s_Actors.size());
        return false;
    }

    // Not rebuilt since this mod wrote it: leave it alone.
    if (!m_LastWritten.empty()) {
        std::map<ZEntityType**, const ActorInfo*> s_ByEntity;

        for (const ActorInfo& s_Info : s_Actors)
            s_ByEntity.emplace(s_Info.entity, &s_Info);

        size_t s_Holding = 0;

        for (const Written& s_Written : m_LastWritten) {
            const auto s_It = s_ByEntity.find(s_Written.entity);

            if (s_It == s_ByEntity.end() || !s_It->second->list.readable ||
                s_It->second->list.header != s_Written.header)
                continue;

            ++s_Holding;

            // The original enforcer: still empty, or refilled since (then it enforces again).
            const auto s_From = s_ByEntity.find(s_Written.from);

            if (s_From == s_ByEntity.end())
                LogTo(fr::LogTopic::Targets,
                      "enforcers: not rebuilt: {} still holds the moved list ({} professions); its original holder "
                      "was not found",
                      ActorLine(*s_It->second), s_Written.count);
            else if (!s_From->second->list.readable)
                LogTo(fr::LogTopic::Targets,
                      "enforcers: not rebuilt: {} still holds the moved list; original holder {} unreadable",
                      ActorLine(*s_It->second), ActorLine(*s_From->second));
            else if (s_From->second->list.count != 0)
                LogTo(fr::LogTopic::Targets,
                      "enforcers: not rebuilt: {} still holds the moved list ({} professions), but its original "
                      "holder {} was refilled: 0 -> {} professions",
                      ActorLine(*s_It->second), s_Written.count, ActorLine(*s_From->second),
                      s_From->second->list.count);
        }

        if (s_Holding > 0)
            return s_Untouched(
                fmt::format("{} of the {} lists this mod moved at an earlier load are still where it put "
                            "them (the scene was not rebuilt)",
                            s_Holding, m_LastWritten.size()));
    }

    // The NPCs a loaded map variant switches off (stranded; VariantCleanup.h, at this same stage, after this):
    // a list moved onto one of them would leave the scene with it.
    std::vector<uint64_t> s_LoadedRids;

    for (size_t i = 0; i < s_Bricks.size(); ++i)
        s_LoadedRids.push_back(s_Bricks[i].m_RuntimeResourceID.GetID());

    const std::set<fr::Guid> s_Removed = fr::VariantRemovedRepos(s_LoadedRids, IsSwappedScene(p_Context));
    std::string s_RemovedFound;

    // The plan.
    std::vector<fr::EnforcerActor> s_Input;
    s_Input.reserve(s_Actors.size());

    for (const ActorInfo& s_Info : s_Actors) {
        fr::EnforcerActor s_Actor;
        s_Actor.handle = reinterpret_cast<uint64_t>(s_Info.entity);
        s_Actor.brickRid = s_Info.brickRid;
        s_Actor.entityId = s_Info.id;

        if (s_Info.bits) {
            s_Actor.startEnabled = s_Info.bits->startEnabled;
            s_Actor.crowd = s_Info.bits->crowd;

            if (s_Info.bits->hasKit)
                s_Actor.kind = s_Info.bits->actorType;

            s_Actor.outfit = OutfitOf(*s_Info.bits);
        }

        if (s_Info.list.readable)
            s_Actor.enforced = s_Info.list.count;

        if (!s_Info.name)
            s_Actor.role = "name unreadable";
        else if (const auto s_Role = fr::RoleFromActorName(*s_Info.name))
            s_Actor.role = std::string(*s_Role);

        s_Actor.inTargetPool = s_Targets.contains(s_Info.entity);
        s_Actor.sentry = s_SentryActors.contains(s_Info.entity) || (s_Info.bits && s_Info.bits->activeSentry);
        s_Actor.sharedList = s_Info.list.readable && s_Info.list.count > 0 && !s_Info.list.isInline &&
                             s_HeapLists[s_Info.list.elements] > 1;

        if (!s_Removed.empty()) {
            const auto s_Repo = ReadActorRepo(s_Info.entity);
            s_Actor.removed = s_Repo && s_Removed.contains(*s_Repo);

            if (s_Actor.removed)
                s_RemovedFound += fmt::format("{}{}", s_RemovedFound.empty() ? "" : ", ", ActorLine(s_Info));
        }

        s_Input.push_back(std::move(s_Actor));
    }

    if (!s_Removed.empty())
        LogTo(fr::LogTopic::Targets,
              "enforcers: {} at {}: {} actors a map variant switches off (stranded) are left out of the shuffle: {}",
              s_Scene, p_Stage, s_Removed.size(), s_RemovedFound.empty() ? std::string("none found") : s_RemovedFound);

    const double s_Share = fr::DrawEnforcerShare(p_Seed.seed, s_Scene, m_Shares);
    const fr::EnforcerPlan s_Plan =
        fr::PlanEnforcerShuffle(p_Seed.seed, s_Scene, s_Input, s_MissionRid, s_Share, m_AnyBrick, m_Include);

    // In a showdown, the mission brick rule is what keeps the suspects, lookouts and assassins: with no actor
    // found there, they are somewhere else, so nothing moves. Counted whatever the verdict: with
    // enforcers_include_late off, a start-disabled mission brick actor is rejected as start disabled first.
    if (fr::IsShowdownScene(s_Scene) && s_Plan.missionBrickActors == 0)
        return s_Untouched("a showdown with no actor in its mission brick, so its lookouts can't be told apart");

    std::string s_Rejected;

    for (const auto& [s_Reject, s_Count] : s_Plan.rejected)
        s_Rejected += fmt::format("{}{} {}", s_Rejected.empty() ? "" : ", ", fr::EnforcerRejectName(s_Reject), s_Count);

    std::string s_Tiers;

    for (const auto& [s_Tier, s_Count] : s_Plan.movesByTier)
        s_Tiers += fmt::format("{}{} {}", s_Tiers.empty() ? "" : ", ", fr::EnforcerTierName(s_Tier), s_Count);

    // Per inclusion: on or off, and when on what it let in and what moved.
    std::string s_Included;

    for (const auto& [s_Inclusion, s_On] : {std::pair {fr::EnforcerInclusion::Target, m_Include.targets},
                                            std::pair {fr::EnforcerInclusion::Late, m_Include.late},
                                            std::pair {fr::EnforcerInclusion::Sentry, m_Include.sentries}}) {
        const auto s_It = s_Plan.included.find(s_Inclusion);
        const fr::EnforcerInclusionCount s_Count =
            s_It == s_Plan.included.end() ? fr::EnforcerInclusionCount {} : s_It->second;
        s_Included += fmt::format("{}{} ", s_Included.empty() ? "" : "; ", fr::EnforcerInclusionName(s_Inclusion));
        s_Included += s_On ? fmt::format("{} ({} enforcers; lists moved from {}, to {})", s_Count.actors,
                                         s_Count.enforcers, s_Count.movedFrom, s_Count.movedTo)
                           : std::string("off");
    }

    LogTo(fr::LogTopic::Targets,
          "enforcers: {} actors, {} enforcers; {} eligible in {} (brick, kind, start state, outfit) groups ({} "
          "enforcers); "
          "untouched enforcers {} (rejected: {}); included: {}; share {:.3f} (random in [{}, {}]; any brick {}) of {} "
          "enforcers -> wanted {}, possible {}, moving {} ({}); {} eligible enforcers kept",
          s_Actors.size(), s_Before, s_Plan.eligible, s_Plan.groups, s_Plan.eligibleEnforcers,
          s_Plan.untouchedEnforcers, s_Rejected.empty() ? std::string("none") : s_Rejected, s_Included, s_Plan.share,
          m_Shares.min, m_Shares.max, m_AnyBrick, s_Plan.eligibleEnforcers + s_Plan.untouchedEnforcers, s_Plan.wanted,
          s_Plan.available, s_Plan.moves.size(), s_Tiers.empty() ? std::string("none") : s_Tiers, s_Plan.keptEnforcers);

    if (s_Plan.moves.empty())
        return s_Untouched("nothing to move");

    // The lines are built before writing (the profession ids are read through the original headers).
    std::vector<std::string> s_Lines;

    for (const fr::EnforcerMove& s_Move : s_Plan.moves) {
        const ActorInfo& s_From = s_Actors[s_Move.from];
        const ActorInfo& s_To = s_Actors[s_Move.to];
        const std::string s_ToOutfit = s_Move.tier == fr::EnforcerTier::Group
                                           ? std::string()
                                           : fmt::format(" -> {}", fr::FormatGuid(OutfitOf(*s_To.bits)));
        s_Lines.push_back(fmt::format(
            "enforcers move: {}{} -> {}{} ({}): outfit {}{}, {} professions [{}]", ActorLine(s_From),
            Tags(s_Input[s_Move.from]), ActorLine(s_To), Tags(s_Input[s_Move.to]), fr::EnforcerTierName(s_Move.tier),
            fr::FormatGuid(OutfitOf(*s_From.bits)), s_ToOutfit, s_From.list.count, ProfessionIds(s_From.list)));
    }

    // Swap each pair's headers, checking each still holds what was read. A
    // pair is always left either fully swapped or fully restored, never with
    // two actors holding one list; any failure undoes every swapped pair.
    struct Pair {
        uintptr_t fromAddress;
        uintptr_t toAddress;
        Header fromOriginal; // the list
        Header toOriginal;   // the empty one
    };

    enum class Undo { Restored, StillSwapped, Shared };

    // from first: if that fails the pair is still a clean swap; if `to` then
    // fails, `from` is swapped back so the pair stays a clean swap.
    const auto s_UndoPair = [](const Pair& p_Pair) {
        if (WriteEngineBytes(p_Pair.fromAddress, &p_Pair.fromOriginal, sizeof(Header)))
            return Undo::StillSwapped;

        if (!WriteEngineBytes(p_Pair.toAddress, &p_Pair.toOriginal, sizeof(Header)))
            return Undo::Restored;

        return WriteEngineBytes(p_Pair.fromAddress, &p_Pair.toOriginal, sizeof(Header)) ? Undo::Shared
                                                                                        : Undo::StillSwapped;
    };

    std::vector<Pair> s_Swapped;
    size_t s_Shared = 0; // pairs left with both actors holding the list (failed mid-pair and could not be fixed)
    std::string s_Failure;

    // Undoes every swapped pair, newest first; true when the scene is as it was.
    const auto s_Rollback = [&](const std::string& p_Why) {
        size_t s_Restored = 0;
        size_t s_StillSwapped = 0;

        for (auto s_It = s_Swapped.rbegin(); s_It != s_Swapped.rend(); ++s_It) {
            switch (s_UndoPair(*s_It)) {
                case Undo::Restored: ++s_Restored; break;
                case Undo::StillSwapped: ++s_StillSwapped; break;
                case Undo::Shared: ++s_Shared; break;
            }
        }

        if (s_StillSwapped == 0 && s_Shared == 0)
            return s_Untouched(fmt::format("{}; all {} swapped pairs restored", p_Why, s_Restored));

        Log("enforcers: {} at {}: {}; partially restored, scene state altered: {} of {} swapped pairs restored, {} "
            "still swapped (each a clean swap), {} with two actors holding one list",
            s_Scene, p_Stage, p_Why, s_Restored, s_Swapped.size(), s_StillSwapped, s_Shared);
        return true;
    };

    for (const fr::EnforcerMove& s_Move : s_Plan.moves) {
        const ActorInfo& s_From = s_Actors[s_Move.from];
        const ActorInfo& s_To = s_Actors[s_Move.to];
        const Pair s_Pair {HeaderAddress(s_From.actor), HeaderAddress(s_To.actor), s_From.list.header,
                           s_To.list.header};
        Header s_FromNow;
        Header s_ToNow;

        if (ReadEngineBytes(s_Pair.fromAddress, &s_FromNow, sizeof(Header)) ||
            ReadEngineBytes(s_Pair.toAddress, &s_ToNow, sizeof(Header))) {
            s_Failure = fmt::format("reading {} or {} faulted", ActorLine(s_From), ActorLine(s_To));
            break;
        }

        if (s_FromNow != s_Pair.fromOriginal || s_ToNow != s_Pair.toOriginal) {
            s_Failure = fmt::format("{} or {} changed since it was read", ActorLine(s_From), ActorLine(s_To));
            break;
        }

        if (const DWORD s_Code = WriteEngineBytes(s_Pair.toAddress, &s_Pair.fromOriginal, sizeof(Header))) {
            // Put back whatever part of the write landed.
            if (WriteEngineBytes(s_Pair.toAddress, &s_Pair.toOriginal, sizeof(Header)))
                ++s_Shared;

            s_Failure = fmt::format("writing {} {}", ActorLine(s_To), FaultText(s_Code));
            break;
        }

        if (const DWORD s_Code = WriteEngineBytes(s_Pair.fromAddress, &s_Pair.toOriginal, sizeof(Header))) {
            // Both hold the list now: give `to` its empty header back.
            if (WriteEngineBytes(s_Pair.toAddress, &s_Pair.toOriginal, sizeof(Header)))
                ++s_Shared;

            s_Failure = fmt::format("writing {} {}", ActorLine(s_From), FaultText(s_Code));
            break;
        }

        s_Swapped.push_back(s_Pair);
    }

    if (!s_Failure.empty())
        return s_Rollback(s_Failure);

    // Read back: every written header as written, each moved list readable with
    // its count at its new place, and the number of enforcers unchanged.
    size_t s_Mismatches = 0;

    for (const Pair& s_Pair : s_Swapped) {
        Header s_FromAfter;
        Header s_ToAfter;

        if (ReadEngineBytes(s_Pair.fromAddress, &s_FromAfter, sizeof(Header)) || s_FromAfter != s_Pair.toOriginal ||
            ReadEngineBytes(s_Pair.toAddress, &s_ToAfter, sizeof(Header)) || s_ToAfter != s_Pair.fromOriginal)
            ++s_Mismatches;
    }

    for (const fr::EnforcerMove& s_Move : s_Plan.moves) {
        const List s_To = ReadList(HeaderAddress(s_Actors[s_Move.to].actor));
        const List s_From = ReadList(HeaderAddress(s_Actors[s_Move.from].actor));

        if (!s_To.readable || s_To.count != s_Actors[s_Move.from].list.count || !s_From.readable || s_From.count != 0)
            ++s_Mismatches;
    }

    size_t s_After = 0;

    for (const ActorInfo& s_Info : s_Actors) {
        const List s_List = ReadList(HeaderAddress(s_Info.actor));
        s_After += s_List.readable && s_List.count > 0 ? 1 : 0;
    }

    if (s_Mismatches > 0 || s_After != s_Before)
        return s_Rollback(
            fmt::format("read back found {} mismatches, {} enforcers (was {})", s_Mismatches, s_After, s_Before));

    m_LastWritten.clear();

    for (const fr::EnforcerMove& s_Move : s_Plan.moves) {
        m_LastWritten.push_back({s_Actors[s_Move.to].entity, s_Actors[s_Move.from].list.header,
                                 s_Actors[s_Move.from].entity, s_Actors[s_Move.from].list.count});
    }

    for (const std::string& s_Line : s_Lines)
        LogTo(fr::LogTopic::Targets, "{}", s_Line);

    LogTo(fr::LogTopic::Targets, "enforcers: moved {} lists ({}) at {}; {} enforcers before and after; read back ok",
          s_Plan.moves.size(), s_Tiers.empty() ? std::string("none") : s_Tiers, p_Stage, s_Before);
    return true;
}
