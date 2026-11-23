// The variant cleanup (see VariantCleanup.h). Every raw engine step follows
// the guard pattern in Guard.h.

#include "VariantCleanup.h"

#include "../core/SceneKinds.h" // fr::IsShowdownScene
#include "../core/Scene.h"      // fr::BrickResourcePath, fr::ToLower
#include "ActorReads.h"
#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "PinIds.h"
#include "SceneOf.h"

#include <Globals.h>

#include <Glacier/ZActor.h>
#include <Glacier/ZResourceID.h>
#include <Glacier/ZString.h>
#include <Glacier/ZTypeRegistry.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <set>

namespace {

constexpr size_t kNpcWalkCap = 2'000'000;

const STypeID* TypeOf(const char* p_Name) {
    if (!*Globals::TypeRegistry || !IsTypeRegistered(p_Name))
        return nullptr;

    return (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view(p_Name)));
}

// m_bStartEnabled through the engine (its change handlers run, as for a
// property set in the editor).
struct StartEnabledArgs {
    ZEntityType** entity;
    bool ok;
};

__declspec(noinline) void StartDisableOf(StartEnabledArgs* p_Args) {
    ZEntityRef s_Entity(p_Args->entity);
    p_Args->ok = s_Entity.SetProperty(fr::kPropStartEnabled, ZObjectRef::From<bool>(false));
}

__declspec(noinline) DWORD SafeStartDisable(StartEnabledArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        StartDisableOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

std::string ActorBitsText(const ActorFacts& p_Facts) {
    return p_Facts.hasCore ? fmt::format("start enabled {}, enabled {}, activated {}", p_Facts.core.startEnabled,
                                         p_Facts.core.enabled, p_Facts.core.activated)
                           : fmt::format("bits unreadable ({})", FaultText(p_Facts.fault));
}

std::string NamesText(const std::vector<fr::StrandedNpc>& p_Npcs) {
    std::string s_Text;

    for (const fr::StrandedNpc& s_Npc : p_Npcs)
        s_Text += fmt::format("{}\"{}\"", s_Text.empty() ? "" : ", ", s_Npc.name);

    return s_Text;
}

} // namespace

void VariantCleanup::Arm(std::string_view p_Scene, const fr::VariantOption& p_Option) {
    if (p_Option.removed.empty())
        return;

    m_Scene = std::string(p_Scene);
    m_Done = false;
    LogTo(fr::LogTopic::MapVariants,
          "variant cleanup: {}: {} stranded NPCs ({}) are switched off once the scene's entities exist, unless in the "
          "target pool",
          p_Option.name, p_Option.removed.size(), NamesText(p_Option.removed));
}

void VariantCleanup::Forget() noexcept {
    m_Scene.clear();
    m_Done = false;
    m_SwitchedOff.clear();
}

void VariantCleanup::OnSceneClear() noexcept {
    m_Done = false;
    m_SwitchedOff.clear();
}

void VariantCleanup::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    if (m_Scene.empty())
        return;

    Contained("variant cleanup", "loading stage", [&] {
        if (p_Stage != ESceneLoadingStage::eLoading_SceneAllocated &&
            p_Stage != ESceneLoadingStage::eLoading_ScenePlaying)
            return;

        if (!p_Context)
            p_Context = CurrentSceneContext();

        if (!p_Context ||
            fr::ToLower(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView()) != fr::ToLower(m_Scene))
            return;

        // Once the entities exist (the target pool swap ran just before).
        if (p_Stage == ESceneLoadingStage::eLoading_SceneAllocated) {
            if (!m_Done) {
                m_Done = true;
                Clear(p_Context);
            }
            return;
        }

        if (!m_SwitchedOff.empty())
            Check("eLoading_ScenePlaying");
    });
}

// The listed NPCs out of the scene before it starts: m_bStartEnabled false on
// each one found that is not in Freelancer's target pool (read here, as the
// pool swap left it).
void VariantCleanup::Clear(ZEntitySceneContext* p_Context) {
    m_SwitchedOff.clear();
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    const std::string s_Scene(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView());
    const auto s_Start = std::chrono::steady_clock::now();
    std::vector<uint64_t> s_LoadedRids;

    for (size_t i = 0; i < s_Bricks.size(); ++i)
        s_LoadedRids.push_back(s_Bricks[i].m_RuntimeResourceID.GetID());

    const std::vector<fr::VariantRemoval> s_Removals = fr::VariantRemovals(s_LoadedRids, IsSwappedScene(p_Context));

    if (s_Removals.empty()) {
        LogTo(fr::LogTopic::MapVariants,
              "variant cleanup: at eLoading_SceneAllocated: no option that strands NPCs has all its bricks loaded ({} "
              "bricks); nobody switched off",
              s_Bricks.size());
        return;
    }

    const auto s_Label = [&](size_t i) {
        return fmt::format("{}: \"{}\"", s_Removals[i].option->name, s_Removals[i].npc->name);
    };
    const auto s_LeaveAll = [&](const std::string& p_Why) {
        LogTo(fr::LogTopic::MapVariants,
              "variant cleanup: at eLoading_SceneAllocated: {}; the {} stranded NPCs are left in", p_Why,
              s_Removals.size());
    };
    const STypeID* s_ActorType = TypeOf("ZActor");

    if (!s_ActorType)
        return s_LeaveAll("ZActor type unknown");

    // Freelancer's target pool: the mission brick's ZVsTargetPicker lists.
    const std::string s_Logical = SceneOf(p_Context);
    const auto s_MissionPath = MissionBrickOf(s_Scene);
    const uint64_t s_MissionRid =
        s_MissionPath ? ZRuntimeResourceID::FromString(fr::BrickResourcePath(*s_MissionPath)).GetID() : 0;
    std::vector<size_t> s_MissionOrder;

    for (size_t i = 0; i < s_Bricks.size(); ++i) {
        if (s_MissionRid && s_LoadedRids[i] == s_MissionRid)
            s_MissionOrder.push_back(i);
    }

    const std::vector<ZEntityType**> s_Pickers = FindTargetPickers(s_Bricks, s_MissionOrder).pickers;
    std::string s_Class;

    std::set<ZEntityType**> s_PoolEntities;
    std::set<fr::Guid> s_PoolRepos;
    size_t s_PoolsRead = 0;

    for (ZEntityType** s_Picker : s_Pickers) {
        const ActorRefList s_List = ReadActorRefs(s_Picker);

        if (!s_List.why.empty()) {
            LogTo(fr::LogTopic::MapVariants, "variant cleanup: a {} is unreadable: {}", kTargetPickerClass, s_List.why);
            continue;
        }

        ++s_PoolsRead;

        for (const ActorRef& s_Ref : s_List.items) {
            if (!s_Ref.first)
                continue;

            s_PoolEntities.insert(s_Ref.first);

            if (const auto s_Repo = ReadActorRepo(s_Ref.first))
                s_PoolRepos.insert(*s_Repo);
        }
    }

    // Unread, any of them could be a target: nobody is switched off.
    const fr::CleanupPool s_Pool =
        fr::DecideCleanupPool(fr::IsShowdownScene(s_Logical), !s_MissionOrder.empty(), s_Pickers.size(), s_PoolsRead);

    if (s_Pool == fr::CleanupPool::Unread)
        return s_LeaveAll(fmt::format("Freelancer's target pool {} ({} {} in mission brick {}{})",
                                      s_PoolsRead == 0 ? "not found" : "not fully read", s_Pickers.size(),
                                      kTargetPickerClass,
                                      s_MissionPath ? *s_MissionPath : std::string("<none for this scene>"),
                                      s_MissionOrder.empty() && s_MissionPath ? ", not loaded" : ""));

    // The NPCs' bricks, each walked once for their actors.
    const std::vector<size_t> s_Order = fr::CleanupBrickOrder(s_LoadedRids, s_Removals);

    if (s_Order.empty()) {
        std::string s_Wanted;

        for (const fr::VariantRemoval& s_Removal : s_Removals) {
            const std::string s_Rid = fmt::format("{:016X}", s_Removal.npc->brickRid);

            if (s_Wanted.find(s_Rid) == std::string::npos)
                s_Wanted += (s_Wanted.empty() ? "" : ", ") + s_Rid;
        }

        return s_LeaveAll(fmt::format("brick {} is not loaded, so nobody to switch off", s_Wanted));
    }

    struct Found {
        ZEntityType** entity = nullptr;
        const ZActor* actor = nullptr;
        uint64_t id = 0;
    };

    std::vector<Found> s_Found(s_Removals.size());
    size_t s_Left = s_Removals.size();
    const WalkStats s_Walk = VisitEntitiesInOrder(
        s_Bricks, s_Order,
        [&](const VisitedEntity& p_Visit) {
            void* s_Actor = nullptr;

            if (SafeInterface(p_Visit.entity, s_ActorType, &s_Actor) || !s_Actor)
                return false;

            const auto s_Repo = ReadActorRepo(p_Visit.entity);

            if (!s_Repo)
                return false;

            for (size_t i = 0; i < s_Removals.size(); ++i) {
                if (!s_Found[i].entity && fr::IsStrandedNpc(*s_Removals[i].npc, *s_Repo, p_Visit.brickRid)) {
                    uint64_t s_Id = 0;
                    ReadEntityClassName(p_Visit.entity, s_Class, &s_Id);
                    s_Found[i] = {p_Visit.entity, static_cast<const ZActor*>(s_Actor), s_Id};
                    --s_Left;
                    return true;
                }
            }

            return false;
        },
        kNpcWalkCap, [&](size_t) { return s_Left == 0; });

    std::map<fr::CleanupAction, size_t> s_Counts;

    for (size_t i = 0; i < s_Removals.size(); ++i) {
        const fr::StrandedNpc& s_Npc = *s_Removals[i].npc;
        const Found& s_It = s_Found[i];
        const ActorFacts s_Facts = s_It.actor ? ReadActor(s_It.actor) : ActorFacts {};
        const auto s_Repo = fr::ParseGuid(s_Npc.repo);
        const bool s_InPool = s_PoolEntities.contains(s_It.entity) || (s_Repo && s_PoolRepos.contains(*s_Repo));
        const fr::CleanupFacts s_In {s_It.entity != nullptr, s_Facts.hasCore, s_It.entity && s_InPool,
                                     s_Facts.core.startEnabled};
        const fr::CleanupAction s_Action = fr::DecideCleanup(s_In);
        std::string s_Result(fr::CleanupActionText(s_Action));

        if (s_Action == fr::CleanupAction::SwitchOff) {
            StartEnabledArgs s_Args {s_It.entity, false};
            const DWORD s_Code = SafeStartDisable(&s_Args);
            const ActorFacts s_Back = ReadActor(s_It.actor);

            if (s_Code)
                s_Result = fmt::format("SetProperty m_bStartEnabled {}; left in", FaultText(s_Code));
            else if (!s_Args.ok || !s_Back.hasCore || s_Back.core.startEnabled)
                s_Result = fmt::format("SetProperty m_bStartEnabled {}, read back {}; may still appear",
                                       s_Args.ok ? "ok" : "returned false", ActorBitsText(s_Back));
            else {
                m_SwitchedOff.push_back({s_Label(i), s_It.entity, s_It.actor});
                s_Result = "m_bStartEnabled true -> false (read back), switched off";
            }
        }

        ++s_Counts[s_Action];
        LogTo(fr::LogTopic::MapVariants, "variant cleanup: {} {} repo {}{}: {}{}", s_Label(i),
              s_It.entity
                  ? fmt::format("{:016x}", s_It.id)
                  : fmt::format("<not found{}>",
                                s_Npc.brickRid ? fmt::format(" in brick {:016X}", s_Npc.brickRid) : std::string()),
              s_Npc.repo, fr::StrandedNpcWhere(s_Npc), s_Result,
              s_It.actor && s_Action != fr::CleanupAction::SwitchOff ? fmt::format(" ({})", ActorBitsText(s_Facts))
                                                                     : std::string());
    }

    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();
    LogTo(fr::LogTopic::MapVariants,
          "variant cleanup: at eLoading_SceneAllocated: {} of {} stranded NPCs switched off ({} in the target pool, {} "
          "already off, {} not found, {} unreadable); pool {}; brick walk {} entities in {} bricks, {} faults{}; "
          "{:.1f} ms",
          m_SwitchedOff.size(), s_Removals.size(), s_Counts[fr::CleanupAction::InPool],
          s_Counts[fr::CleanupAction::AlreadyOff], s_Counts[fr::CleanupAction::NotFound],
          s_Counts[fr::CleanupAction::Unreadable],
          s_Pool == fr::CleanupPool::None
              ? std::string("none (a showdown: its targets are the mission brick's own)")
              : fmt::format("{} actors in {} {}", s_PoolEntities.size(), s_PoolsRead, kTargetPickerClass),
          s_Walk.visited, s_Walk.bricks.size(), s_Walk.faults, s_Walk.capped ? " (capped)" : "", s_Ms);
}

// Whether the switched-off NPCs stayed out.
void VariantCleanup::Check(const char* p_Moment) {
    size_t s_Out = 0;

    for (const SwitchedOff& s_Off : m_SwitchedOff) {
        const ActorFacts s_Facts = ReadActor(s_Off.actor);
        const bool s_Stayed = s_Facts.hasCore && !s_Facts.core.startEnabled && !s_Facts.core.enabled;
        s_Out += s_Stayed ? 1 : 0;

        if (!s_Stayed)
            LogTo(fr::LogTopic::MapVariants, "variant cleanup: at {}: {} is back: {}", p_Moment, s_Off.label,
                  ActorBitsText(s_Facts));
    }

    LogTo(fr::LogTopic::MapVariants,
          "variant cleanup: at {}: {} of {} switched-off NPCs still out (start disabled, not enabled)", p_Moment, s_Out,
          m_SwitchedOff.size());
    m_SwitchedOff.clear();
}
