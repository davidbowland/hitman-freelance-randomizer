// Target pool shuffle (see TargetPoolSwap.h). Every raw engine read and
// write follows the guard pattern in Guard.h.

#include "TargetPoolSwap.h"

#include "../core/GuardWeapons.h"  // fr::ActorTypeName
#include "../core/LandslideFull.h" // fr::FullVariant
#include "../core/MoverTable.h"    // the scene's movers
#include "../core/SceneKinds.h"    // mild scenes, mission brick, known helper ids, role names
#include "../core/SettingsTable.h" // fr::PercentText
#include "../core/Variants.h"      // fr::TargetPoolExclusions, fr::MoverTableVariants
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
#include <span>

namespace {

constexpr size_t kMaxPoolElements = 4'096;
constexpr size_t kElementSize = 16; // TEntityRef<ZActor>: the entity, then the interface
struct ActorBits {
    bool startEnabled;
    bool crowd;
    bool hasKit;
    int32_t actorType;
};

std::optional<ActorBits> ReadActorBits(const ZActor* p_Actor) {
    ActorCore s_Core {};
    KitTypeOut s_Kit {};

    if (SafeActorCore(p_Actor, &s_Core) || SafeKitType(p_Actor, &s_Kit))
        return std::nullopt;

    return ActorBits {s_Core.startEnabled, s_Core.crowd, s_Kit.hasKit, s_Kit.actorType};
}

// What the shuffle knows about one actor entity.
struct ActorInfo {
    ZEntityType** entity = nullptr;
    const ZActor* actor = nullptr;
    uint64_t id = 0;
    uint64_t brickRid = 0;
    std::optional<std::string> name; // empty when unreadable
    std::optional<ActorBits> bits;
    std::optional<fr::Guid> repo;
};

ActorInfo ReadActorInfo(ZEntityType** p_Entity, const ZActor* p_Actor, uint64_t p_Id, uint64_t p_BrickRid) {
    ActorInfo s_Out {p_Entity, p_Actor, p_Id, p_BrickRid, {}, {}, {}};
    NameOut s_Name {};

    if (!SafeActorName(p_Actor, &s_Name))
        s_Out.name = s_Name.text;

    s_Out.bits = ReadActorBits(p_Actor);
    s_Out.repo = ReadActorRepo(p_Entity);
    return s_Out;
}

int32_t KindOf(const ActorInfo& p_Info) {
    return p_Info.bits && p_Info.bits->hasKit ? p_Info.bits->actorType : fr::kUnknownActorKind;
}

std::string KindText(int32_t p_Kind) {
    return p_Kind == fr::kUnknownActorKind ? std::string("kind unknown") : fr::ActorTypeName(p_Kind);
}

// "\"Name\" 0123456789abcdef repo <guid> [eAT_Guard]"
std::string ActorLine(const ActorInfo& p_Info) {
    return fmt::format("{} {} repo {} [{}]", p_Info.name ? fmt::format("\"{}\"", *p_Info.name) : "<name unreadable>",
                       IdText(p_Info.id), p_Info.repo ? fr::FormatGuid(*p_Info.repo) : std::string("<none>"),
                       KindText(KindOf(p_Info)));
}

// "mission brick (dedicated target)", "scene brick 00CDEB..." or "brick unknown".
std::string BrickText(uint64_t p_Brick, uint64_t p_MissionBrick) {
    if (p_Brick == 0)
        return "brick unknown";

    return p_Brick == p_MissionBrick ? std::string("mission brick (dedicated target)")
                                     : fmt::format("scene NPC brick {:016X}", p_Brick);
}

// "start disabled, mission brick (dedicated target)"
std::string PlaceText(const ActorInfo& p_Info, uint64_t p_MissionBrick) {
    const char* s_Start = !p_Info.bits                ? "start enabled unread"
                          : p_Info.bits->startEnabled ? "start enabled"
                                                      : "start disabled";
    return fmt::format("{}, {}", s_Start, BrickText(p_Info.brickRid, p_MissionBrick));
}

// Why the TArray header at p_Header cannot be shortened to fewer elements,
// or empty: it must hold p_Count elements in one of the pool's storages, not inline.
std::optional<std::string> HeaderProblem(uintptr_t p_Header, const std::vector<uintptr_t>& p_Storages, size_t p_Count) {
    struct {
        uintptr_t begin;
        uintptr_t end;
        uint64_t flags;
    } s_Array {};

    if (const uint32_t s_Code = ReadEngineBytes(p_Header, &s_Array, sizeof(s_Array)))
        return "m_aActors " + FaultText(s_Code);

    if (s_Array.flags & (uint64_t {1} << 62))
        return std::string("m_aActors is stored inline");

    if (std::find(p_Storages.begin(), p_Storages.end(), s_Array.begin) == p_Storages.end())
        return fmt::format("m_aActors at 0x{:X} holds elements at 0x{:X}, not the pool's", p_Header, s_Array.begin);

    if (s_Array.end != s_Array.begin + p_Count * kElementSize)
        return fmt::format("m_aActors at 0x{:X} ends at 0x{:X}, not after {} elements", p_Header, s_Array.end, p_Count);

    return std::nullopt;
}

constexpr std::string_view kTag = "target pool";

} // namespace

void TargetPoolSwap::Configure(bool p_On, double p_ShareMin, double p_ShareMax, bool p_AnyGuard) {
    m_On = p_On;
    m_AnyGuard = p_AnyGuard;
    m_Shares =
        fr::ClampShareRange(p_ShareMin, p_ShareMax, {fr::kDefaultTargetPoolSwapMin, fr::kDefaultTargetPoolSwapMax});
}

void TargetPoolSwap::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                    const std::optional<fr::SeedResolution>& p_Seed) noexcept {
    // Off, it still takes the people who can never be targets out of a mild
    // pool (the main story mission's targets, a map variant's own targets,
    // the NPCs it strands or shuts in).
    Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context, p_Seed); });
}

void TargetPoolSwap::OnSceneClear() noexcept {
    m_Done = false;
    m_ExclusionsOnly = false;
}

void TargetPoolSwap::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                  const std::optional<fr::SeedResolution>& p_Seed) {
    if (!p_Context)
        return;

    const char* s_Stage = nullptr;

    switch (p_Stage) {
        case ESceneLoadingStage::eLoading_AssetsLoaded:
            m_Done = false;
            m_ExclusionsOnly = false;
            return;
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

    // Hot (alerted) missions and showdowns: log every picker's pool, never swap (off: nothing).
    if (!fr::IsMildMissionScene(s_Scene)) {
        m_Done = m_On ? LogOtherPools(p_Context, s_Stage) : true;
        return;
    }

    // Rolls follow the dials' scene seed; an unknown one would give every
    // campaign the same pool: no swaps then, only the people who can never be
    // targets are taken out.
    const bool s_KnownSeed = p_Seed && p_Seed->source != fr::SeedSource::Fallback && p_Seed->seed != 0;

    if (m_On && !s_KnownSeed)
        LogTo(fr::LogTopic::Targets, "target pool: {}: {}; no swaps", s_Scene,
              p_Seed ? fmt::format("seed {} ({}) is unknown", p_Seed->seed, fr::SeedSourceName(p_Seed->source))
                     : std::string("no seed resolved for this mission"));

    const bool s_Swaps = m_On && s_KnownSeed;
    m_ExclusionsOnly = !s_Swaps;
    m_Done = Run(p_Context, p_Seed ? *p_Seed : fr::SeedResolution {fr::SeedSource::Fallback, 0}, s_Stage, s_Swaps);
}

bool TargetPoolSwap::LogOtherPools(ZEntitySceneContext* p_Context, const char* p_Stage) {
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    const std::string s_Scene = SceneOf(p_Context);
    std::vector<uint64_t> s_All;

    for (size_t i = 0; i < s_Bricks.size(); ++i)
        s_All.push_back(s_Bricks[i].m_RuntimeResourceID.GetID());

    std::vector<ClassEntity> s_Pickers;
    const WalkStats s_Walk = FindEntitiesOfClass(s_Bricks, s_All, kTargetPickerClass, s_Pickers);

    if (s_Walk.visited == 0 && std::string_view(p_Stage) != "eLoading_SceneStarted")
        return false;

    LogTo(fr::LogTopic::Targets,
          "target pool (log only, not a mild mission): {} at {}: {} bricks, {} entities, {} faults: {} {}", s_Scene,
          p_Stage, s_Bricks.size(), s_Walk.visited, s_Walk.faults, s_Pickers.size(), kTargetPickerClass);

    if (!*Globals::TypeRegistry)
        return true;

    const STypeID* s_ActorType = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("ZActor")));
    constexpr size_t kMaxLogged = 128;

    for (const ClassEntity& s_Picker : s_Pickers) {
        const RawPool s_Pool = ReadPool(s_Picker.entity);
        LogTo(fr::LogTopic::Targets, "target pool (log only): {} {} in brick {:016X}: {} elements{}",
              kTargetPickerClass, IdText(s_Pool.ownerId), s_Picker.brickRid, s_Pool.items.size(),
              s_Pool.why.empty() ? std::string() : " <" + s_Pool.why + ">");

        for (size_t i = 0; i < s_Pool.items.size() && i < kMaxLogged; ++i) {
            const RawElement& s_Element = s_Pool.items[i];
            void* s_Actor = nullptr;

            if (!s_Element.entity || !s_ActorType || SafeInterface(s_Element.entity, s_ActorType, &s_Actor) ||
                !s_Actor) {
                LogTo(fr::LogTopic::Targets, "target pool (log only): [{}] unreadable", i);
                continue;
            }

            std::string s_Class;
            uint64_t s_Id = 0;
            ReadEntityClassName(s_Element.entity, s_Class, &s_Id);
            const ActorInfo s_Info = ReadActorInfo(s_Element.entity, static_cast<const ZActor*>(s_Actor), s_Id, 0);
            LogTo(fr::LogTopic::Targets, "target pool (log only): [{}] {} ({})", i, ActorLine(s_Info),
                  PlaceText(s_Info, s_Picker.brickRid));
        }
    }

    return true;
}

TargetPoolSwap::RawPool TargetPoolSwap::ReadPool(ZEntityType** p_Owner) const {
    RawPool s_Out;
    s_Out.owner = p_Owner;

    if (const uint32_t s_Code = ReadEntityClassName(p_Owner, s_Out.ownerClass, &s_Out.ownerId)) {
        s_Out.why = "owner " + FaultText(s_Code);
        return s_Out;
    }

    const PropertyLocation s_Location = LocateProperty(p_Owner, "m_aActors");

    switch (s_Location.status) {
        case PropertyLocation::Status::Ok: break;
        case PropertyLocation::Status::Missing: s_Out.why = "no m_aActors"; return s_Out;
        case PropertyLocation::Status::HasGetter: s_Out.why = "m_aActors has a getter"; return s_Out;
        case PropertyLocation::Status::Fault: s_Out.why = "m_aActors " + FaultText(s_Location.faultCode); return s_Out;
    }

    if (!s_Location.isArray || s_Location.elementSize != kElementSize ||
        std::string_view(s_Location.elementTypeName) != "TEntityRef<ZActor>") {
        s_Out.why = fmt::format("m_aActors is {} (element {}, {} bytes), not TArray<TEntityRef<ZActor>>",
                                s_Location.typeName[0] ? s_Location.typeName : "<unknown>",
                                s_Location.elementTypeName[0] ? s_Location.elementTypeName : "<unknown>",
                                s_Location.elementSize);
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

    const auto s_Span = fr::ArrayElements(s_Location.address, s_Array.begin, s_Array.end, s_Array.flags, kElementSize,
                                          kMaxPoolElements);

    if (!s_Span) {
        s_Out.why = fmt::format("implausible array (begin 0x{:X}, end 0x{:X}, flags 0x{:X})", s_Array.begin,
                                s_Array.end, s_Array.flags);
        return s_Out;
    }

    s_Out.property = s_Location.address;
    s_Out.elements = s_Span->begin;

    for (size_t i = 0; i < s_Span->count; ++i) {
        RawElement s_Element;
        static_assert(sizeof(RawElement) == kElementSize);

        if (const uint32_t s_Code = ReadEngineBytes(s_Span->begin + i * kElementSize, &s_Element, kElementSize)) {
            s_Out.why = fmt::format("element {} {}", i, FaultText(s_Code));
            s_Out.items.clear();
            return s_Out;
        }

        s_Out.items.push_back(s_Element);
    }

    return s_Out;
}

bool TargetPoolSwap::Run(ZEntitySceneContext* p_Context, const fr::SeedResolution& p_Seed, const char* p_Stage,
                         bool p_Swaps) {
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    const std::string s_Scene = SceneOf(p_Context);
    const auto s_Untouched = [&](const std::string& p_Why) {
        LogTo(fr::LogTopic::Targets, "target pool: {} at {}: {}; pool left untouched", s_Scene, p_Stage, p_Why);
        return true;
    };

    // The mover table lists the repository ids of the NPCs recorded moving in
    // the scene as loaded. The full Landslide's crowd is not mild Sapienza's
    // (mission01's day crowd is not loaded there), so it is looked up by
    // Landslide's own scene (its _scene_mamba.entity rows; any case). The full
    // Author keeps mild Sapienza's day crowd bricks: its own scene's rows once
    // recorded, mild Sapienza's until then (fr::MoverTableScene).
    const std::string s_Loaded(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView());
    const bool s_Swapped = IsSwappedScene(s_Loaded);
    const std::optional<fr::SceneAlias> s_Alias = CurrentSceneAlias();
    // By the alias's key: the full Icon and the full Bad Boy share a scene, as
    // do the full Marrakesh night and the full Entertainer.
    const fr::FullVariant* s_Full =
        s_Swapped ? fr::FullVariantOfLoad(s_Alias ? std::string_view(s_Alias->fullKey) : std::string_view(), s_Loaded)
                  : nullptr;
    const std::string s_TableScene =
        s_Swapped ? fr::MoverTableScene(s_Alias ? &*s_Alias : nullptr, s_Loaded, fr::SceneMovers(s_Loaded).has_value())
                  : s_Scene;
    const std::string s_FullName = s_Full ? std::string(s_Full->fullName) : std::string("the swapped scene");

    std::vector<uint64_t> s_LoadedRids;

    for (size_t i = 0; i < s_Bricks.size(); ++i)
        s_LoadedRids.push_back(s_Bricks[i].m_RuntimeResourceID.GetID());

    // The main story mission's targets at this location (fr::StoryTargets:
    // the Washington twins at Sgail, ...), always; a loaded map variant's own
    // story targets (VariantOption::notTargets: its elusive target, ...), the
    // actors it strands (removed) or shuts in (fr::SealedActors: the Garden
    // Show's closed mansion) and, under a full variant, its own target
    // (fr::FullVariant::neverTargets: Marco Abiatti, Craig Black, Bartholomew
    // Argus): never targets. Without swaps (the shuffle off, or no known seed)
    // they are only taken out of the pool.
    const std::set<fr::Guid> s_Excluded = fr::TargetPoolExclusions(s_Scene, s_LoadedRids, s_Full);
    const std::string s_NoSwapsWhy = m_On ? "the campaign seed is not known" : "the target shuffle is off";

    if (!p_Swaps && s_Excluded.empty()) {
        LogTo(fr::LogTopic::Targets, "target pool: {}: {} and no one here is ruled out as a target; pool left as it is",
              s_Scene, s_NoSwapsWhy);
        return true;
    }

    if (!p_Swaps)
        LogTo(fr::LogTopic::Targets,
              "target pool: {}: {}: nothing is swapped in, only the {} people who can never be "
              "targets here are taken out of the pool",
              s_Scene, s_NoSwapsWhy, s_Excluded.size());

    // Swaps only: the guard tier.
    const bool s_AnyGuard = p_Swaps && m_AnyGuard;

    // A loaded map variant brings its own crowd: its own rows when it was
    // recorded (The Drop), otherwise the normal map's. A lighting-only
    // variant (Night, The Icon's lighting) has the normal map's crowd and rows.
    // A layer full variant (the full Bad Boy) has its base's scene plus its own
    // people: its rows are tagged with its key, the scene's untagged rows being
    // its base's (the full Icon's).
    const std::vector<std::string_view> s_Variants = fr::MoverTableVariants(s_LoadedRids, s_Full);
    const fr::MoverTableChoice s_Choice = fr::ChooseMoverTable(s_TableScene, s_Variants);

    // Civilians must be known movers (stationary ones may be real crowds: party
    // guests in groups); guards need no table with target_pool_any_guard.
    // None without swaps: no candidate, no stationary entry.
    const auto s_MoverTiers = p_Swaps ? fr::SceneMovers(s_TableScene, s_Choice.variant) : std::nullopt;
    // Off: the old rule, good movers only.
    const auto s_Movers =
        p_Swaps
            ? fr::KnownMovers(s_TableScene, m_AnyGuard ? fr::MoverTier::Mover : fr::MoverTier::Good, s_Choice.variant)
            : std::nullopt;
    const std::string s_Listed =
        s_MoverTiers ? fmt::format("{} movers listed", s_MoverTiers->size()) : std::string("none recorded yet");

    if (p_Swaps) {
        if (!s_Choice.variant.empty())
            LogTo(fr::LogTopic::Targets, "target pool: {}: map variant {} loaded: its own mover table is used ({})",
                  s_Scene, s_Choice.variant, s_Listed);
        else if (s_Choice.fallback)
            LogTo(fr::LogTopic::Targets,
                  "target pool: {}: map variant {} loaded, but no mover table was recorded for it: the normal map's is "
                  "used "
                  "({}); its own crowd is not listed yet",
                  s_Scene, s_Choice.loaded, s_Listed);
        else if (s_Choice.lightingOnly)
            LogTo(fr::LogTopic::Targets,
                  "target pool: {}: map variant {} loaded (lighting only: the normal map's crowd): the normal map's "
                  "mover "
                  "table is used ({})",
                  s_Scene, s_Choice.loaded, s_Listed);
        else if (!s_Swapped)
            LogTo(fr::LogTopic::Targets, "target pool: {}: normal map: the normal map's mover table is used ({})",
                  s_Scene, s_Listed);
    }

    // "mild Sapienza's", "mild Marrakesh's": the contract scene's table.
    const std::string s_ContractTable =
        s_Full ? fmt::format("mild {}'s", s_Full->location) : std::string("the contract scene's");

    if (p_Swaps) {
        if (s_Swapped && s_TableScene != s_Loaded)
            LogTo(fr::LogTopic::Targets,
                  "target pool: {}: {}: {} mover table is used ({} keeps its day crowd; {}); no table recorded for {} "
                  "yet",
                  s_Scene, s_FullName, s_ContractTable, s_Full ? s_Full->display : std::string_view("it"), s_Listed,
                  s_Loaded);
        else if (s_Swapped)
            LogTo(fr::LogTopic::Targets, "target pool: {}: {}: {} mover table is not used ({}); {}", s_Scene,
                  s_FullName, s_ContractTable,
                  s_Full && s_Full->sharesCrowd ? "its own was recorded" : "its NPCs are not loaded here",
                  s_MoverTiers ? fmt::format("the table recorded for {} is used ({} movers listed)", s_Loaded,
                                             s_MoverTiers->size())
                               : fmt::format("no table recorded for {} yet", s_Loaded));
    }

    if (p_Swaps && !s_Movers && !m_AnyGuard)
        return s_Untouched("no mover table for this mission's scene and target_pool_any_guard is off");

    if (p_Swaps && !s_Movers)
        LogTo(fr::LogTopic::Targets, "target pool: {}: no mover table for this scene: guards only", s_Scene);

    // A full variant's is its package's copy of the contract's mission brick (SceneOf.h).
    const auto s_BrickPath = MildMissionBrickOf(s_Scene);

    if (!s_BrickPath)
        return s_Untouched("no mission brick path");

    const uint64_t s_BrickRid = ZRuntimeResourceID::FromString(fmt::format("[{}].pc_entitytype", *s_BrickPath)).GetID();
    ZEntityType** s_Root = nullptr;
    size_t s_BrickIndex = 0;

    for (size_t i = 0; i < s_Bricks.size(); ++i) {
        if (s_Bricks[i].m_RuntimeResourceID.GetID() == s_BrickRid) {
            s_Root = s_Bricks[i].m_EntityRef.m_pObj;
            s_BrickIndex = i;
        }
    }

    const bool s_LastChance = std::string_view(p_Stage) == "eLoading_SceneStarted";

    if (!s_Root) {
        if (s_LastChance)
            return s_Untouched(fmt::format("mission brick {} not loaded", *s_BrickPath));

        LogTo(fr::LogTopic::Targets,
              "target pool: {} at {}: mission brick {} not loaded yet; tried again at eLoading_SceneStarted", s_Scene,
              p_Stage, *s_BrickPath);
        return false;
    }

    if (!*Globals::TypeRegistry)
        return s_Untouched("no type registry");

    const STypeID* s_ActorType = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("ZActor")));

    if (!s_ActorType)
        return s_Untouched("ZActor is not in the type registry");

    // The picker(s) in the mission brick.
    const TargetPickers s_Found = FindTargetPickers(s_Bricks, std::span<const size_t>(&s_BrickIndex, 1));
    const WalkStats& s_PickerWalk = s_Found.walk;

    if (s_PickerWalk.visited == 0) {
        if (s_LastChance)
            return s_Untouched("the mission brick has no entities");

        LogTo(fr::LogTopic::Targets,
              "target pool: {} at {}: mission brick has no entities yet; tried again at eLoading_SceneStarted", s_Scene,
              p_Stage);
        return false;
    }

    // Drawn from the scene seed, so a restart gets the same shares (and the same swaps).
    const double s_Share = !p_Swaps ? 0.0 : fr::ShareAt(m_Shares, fr::DrawSwapShare(p_Seed.seed, s_Scene));
    const double s_GuardShare =
        !p_Swaps ? 0.0
                 : fr::ShareAt(m_Shares, fr::DrawSwapShare(p_Seed.seed, s_Scene, fr::kTargetPoolGuardShareFeature));
    size_t s_GoodListed = 0;

    if (s_MoverTiers) {
        for (const auto& [s_Repo, s_Tier] : *s_MoverTiers)
            s_GoodListed += s_Tier == fr::MoverTier::Good ? 1 : 0;
    }

    LogTo(
        fr::LogTopic::Targets,
        "target pool: {} at {}: seed {} ({}); mover table {}; any guard {}; mission brick {} ({} entities, {} faults): "
        "{} {}",
        s_Scene, p_Stage, p_Seed.seed, fr::SeedSourceName(p_Seed.source),
        s_Movers ? fmt::format("{} actors ({} good movers)", s_Movers->size(), s_GoodListed) : std::string("none"),
        s_AnyGuard, *s_BrickPath, s_PickerWalk.visited, s_PickerWalk.faults, s_Found.pickers.size(),
        kTargetPickerClass);

    if (p_Swaps && m_AnyGuard)
        LogTo(fr::LogTopic::Targets, "target pool: shares: civilian movers {:.3f}, guards {:.3f} (random in [{}, {}])",
              s_Share, s_GuardShare, m_Shares.min, m_Shares.max);
    else if (p_Swaps)
        LogTo(fr::LogTopic::Targets, "target pool: share {:.3f} (random in [{}, {}])", s_Share, m_Shares.min,
              m_Shares.max);

    // The template helper, by its known id, as a cross-check.
    std::optional<RawPool> s_Helper;

    if (const auto s_HelperId = fr::KnownTargetPoolHelper(s_BrickRid)) {
        const SubEntityLookup s_Lookup = LookUpSubEntity(s_Root, nullptr, *s_HelperId);

        if (s_Lookup.faultStep && !s_Lookup.entity)
            LogTo(fr::LogTopic::Targets, "target pool: helper {}: lookup ({}) {}", IdText(*s_HelperId),
                  s_Lookup.faultStep, FaultText(s_Lookup.faultCode));
        else if (!s_Lookup.entity)
            LogTo(fr::LogTopic::Targets, "target pool: helper {}: not in the mission brick", IdText(*s_HelperId));
        else
            s_Helper = ReadPool(s_Lookup.entity);

        if (s_Helper && !s_Helper->why.empty()) {
            LogTo(fr::LogTopic::Targets, "target pool: helper {}: {}", IdText(*s_HelperId), s_Helper->why);
            s_Helper.reset();
        }
    }
    else {
        LogTo(fr::LogTopic::Targets,
              "target pool: no known helper id for this mission brick; the picker alone decides");
    }

    std::vector<RawPool> s_Pickers;

    for (ZEntityType** s_Entity : s_Found.pickers) {
        RawPool s_Pool = ReadPool(s_Entity);
        LogTo(fr::LogTopic::Targets, "target pool: {} {}: m_aActors at 0x{:X}, elements at 0x{:X}, {} elements{}",
              kTargetPickerClass, IdText(s_Pool.ownerId), s_Pool.property, s_Pool.elements, s_Pool.items.size(),
              s_Pool.why.empty() ? std::string() : " <" + s_Pool.why + ">");

        if (!s_Pool.why.empty())
            continue;

        if (s_Helper && s_Pool.items != s_Helper->items) {
            LogTo(fr::LogTopic::Targets, "target pool: {} {}: its list differs from the helper's; not the target pool",
                  kTargetPickerClass, IdText(s_Pool.ownerId));
            continue;
        }

        s_Pickers.push_back(std::move(s_Pool));
    }

    if (s_Pickers.empty())
        return s_Untouched(fmt::format("no readable {} {}", kTargetPickerClass,
                                       s_Helper ? "holding the helper's list" : "in the mission brick"));

    if (!s_Helper && s_Pickers.size() > 1)
        return s_Untouched(fmt::format("{} readable {} and no helper to tell which is the target pool",
                                       s_Pickers.size(), kTargetPickerClass));

    const std::vector<RawElement> s_Original = s_Pickers.front().items;

    // Every place the list is stored: each picker, and the helper when it is a separate copy.
    std::vector<uintptr_t> s_Storages;

    for (const RawPool& s_Pool : s_Pickers)
        s_Storages.push_back(s_Pool.elements);

    if (s_Helper) {
        const bool s_Shared = std::find(s_Storages.begin(), s_Storages.end(), s_Helper->elements) != s_Storages.end();
        LogTo(fr::LogTopic::Targets, "target pool: helper {}: m_aActors at 0x{:X}, elements at 0x{:X}, {} elements: {}",
              IdText(s_Helper->ownerId), s_Helper->property, s_Helper->elements, s_Helper->items.size(),
              s_Shared ? "the picker's own storage" : "a separate copy (written too)");

        if (!s_Shared)
            s_Storages.push_back(s_Helper->elements);
    }

    std::sort(s_Storages.begin(), s_Storages.end());
    s_Storages.erase(std::unique(s_Storages.begin(), s_Storages.end()), s_Storages.end());

    // Not rebuilt since this mod wrote it: leave it alone.
    if (!m_LastWritten.empty()) {
        std::vector<ZEntityType**> s_Now;

        for (const RawElement& s_Element : s_Original)
            s_Now.push_back(s_Element.entity);

        if (s_Now == m_LastWritten)
            return s_Untouched("it still holds the list this mod wrote at an earlier load (not rebuilt)");
    }

    // The m_aVolumes beside the actors: a list as long as the pool may run
    // parallel to it (then each swapped slot's volume is logged).
    std::vector<uint64_t> s_VolumeIds; // per slot, when as long as the pool
    bool s_VolumesEmpty = false;       // read, and no volumes: nothing runs parallel to the pool
    {
        const PropertyLocation s_Volumes = LocateProperty(s_Pickers.front().owner, "m_aVolumes");
        struct {
            uintptr_t begin;
            uintptr_t end;
            uint64_t flags;
        } s_Array {};

        if (s_Volumes.status == PropertyLocation::Status::Ok && s_Volumes.isArray && s_Volumes.elementSize > 0 &&
            !ReadEngineBytes(s_Volumes.address, &s_Array, sizeof(s_Array))) {
            const auto s_Span = fr::ArrayElements(s_Volumes.address, s_Array.begin, s_Array.end, s_Array.flags,
                                                  s_Volumes.elementSize, kMaxPoolElements);
            const bool s_Parallel = s_Span && s_Span->count == s_Original.size() && s_Volumes.elementSize >= 8;
            s_VolumesEmpty = s_Span && s_Span->count == 0;
            LogTo(fr::LogTopic::Targets, "target pool: VOLUMES: {} m_aVolumes {} vs m_aActors {}: {}",
                  kTargetPickerClass,
                  s_Span ? fmt::format("{} elements", s_Span->count) : std::string("implausible array"),
                  s_Original.size(),
                  s_Parallel ? "EQUAL LENGTH, may be paired by index (each swap logs its slot's volume)"
                             : "different lengths, not paired by index");

            for (size_t i = 0; s_Parallel && i < s_Span->count; ++i) {
                ZEntityType** s_Volume = nullptr;
                uint64_t s_Id = 0;
                std::string s_VolumeClass;

                if (!ReadEngineBytes(s_Span->begin + i * s_Volumes.elementSize, &s_Volume, sizeof(s_Volume)) &&
                    s_Volume)
                    ReadEntityClassName(s_Volume, s_VolumeClass, &s_Id);

                s_VolumeIds.push_back(s_Id);
            }
        }
        else {
            LogTo(fr::LogTopic::Targets, "target pool: VOLUMES: {} m_aVolumes not read (m_aActors {} elements)",
                  kTargetPickerClass, s_Original.size());
        }
    }

    // Every TArray header holding the pool (each picker's, the helper's): a
    // trim shortens each one (its end only: no allocation, the storage stays).
    std::vector<uintptr_t> s_Headers;

    for (const RawPool& s_Pool : s_Pickers)
        s_Headers.push_back(s_Pool.property);

    if (s_Helper)
        s_Headers.push_back(s_Helper->property);

    std::sort(s_Headers.begin(), s_Headers.end());
    s_Headers.erase(std::unique(s_Headers.begin(), s_Headers.end()), s_Headers.end());

    // Why the pool cannot be shortened, or empty when it can.
    std::string s_NoTrim;

    if (!s_VolumesEmpty)
        s_NoTrim = "the picker's m_aVolumes is not known to be empty";

    for (const uintptr_t s_Header : s_Headers) {
        if (!s_NoTrim.empty())
            break;

        if (const auto s_Why = HeaderProblem(s_Header, s_Storages, s_Original.size()))
            s_NoTrim = *s_Why;
    }

    // Every actor in the scene.
    const auto s_Start = std::chrono::steady_clock::now();
    std::vector<ActorInfo> s_Actors;
    std::map<ZEntityType**, size_t> s_ByEntity;
    std::string s_Class;
    size_t s_InterfaceFaults = 0;

    // Without swaps no candidate is needed: no walk (the pool's own actors are read below).
    const WalkStats s_Walk = !p_Swaps ? WalkStats {} : VisitAllEntities(s_Bricks, [&](const VisitedEntity& p_Visit) {
        void* s_Actor = nullptr;

        if (SafeInterface(p_Visit.entity, s_ActorType, &s_Actor)) {
            ++s_InterfaceFaults;
            return false;
        }

        if (!s_Actor)
            return false;

        uint64_t s_Id = 0;
        ReadEntityClassName(p_Visit.entity, s_Class, &s_Id);
        s_ByEntity.emplace(p_Visit.entity, s_Actors.size());
        s_Actors.push_back(ReadActorInfo(p_Visit.entity, static_cast<const ZActor*>(s_Actor), s_Id, p_Visit.brickRid));
        return true;
    });
    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();

    LogTo(fr::LogTopic::Targets,
          "target pool: actor walk: {} bricks, {} entities, {} faults{}, {} interface faults, {:.1f} ms: {} actors",
          s_Bricks.size(), s_Walk.visited, s_Walk.faults, s_Walk.capped ? " (capped)" : "", s_InterfaceFaults, s_Ms,
          s_Actors.size());

    if (s_Walk.capped)
        return s_Untouched("the actor walk stopped at its cap, so the candidates depend on where it stopped");

    // The pool as the planner sees it. Each element's interface must be the actor's ZActor.
    std::vector<fr::PoolEntry> s_Entries;
    std::vector<ActorInfo> s_PoolInfo;

    for (size_t i = 0; i < s_Original.size(); ++i) {
        const RawElement& s_Element = s_Original[i];
        fr::PoolEntry s_Entry;
        ActorInfo s_Info;
        s_Info.entity = s_Element.entity;

        if (s_Element.entity) {
            void* s_Actor = nullptr;

            if (SafeInterface(s_Element.entity, s_ActorType, &s_Actor) || s_Actor != s_Element.actor)
                return s_Untouched(fmt::format("element {}'s interface is not its entity's ZActor", i));

            if (const auto s_It = s_ByEntity.find(s_Element.entity); s_It != s_ByEntity.end()) {
                s_Info = s_Actors[s_It->second];
            }
            else {
                uint64_t s_Id = 0;
                ReadEntityClassName(s_Element.entity, s_Class, &s_Id);
                s_Info = ReadActorInfo(s_Element.entity, static_cast<const ZActor*>(s_Actor), s_Id, 0);
            }

            s_Entry.handle = reinterpret_cast<uint64_t>(s_Element.entity);
            s_Entry.replaceable = s_Element.actor != nullptr && s_Info.bits.has_value();
            s_Entry.kind = KindOf(s_Info);
            s_Entry.repo = s_Info.repo;
            s_Entry.brickRid = s_Info.brickRid;

            if (s_Info.bits)
                s_Entry.startEnabled = s_Info.bits->startEnabled;
        }

        s_Entries.push_back(s_Entry);
        s_PoolInfo.push_back(std::move(s_Info));
    }

    std::vector<fr::TargetCandidate> s_Candidates;

    for (const ActorInfo& s_Info : s_Actors) {
        fr::TargetCandidate s_Candidate;
        s_Candidate.handle = reinterpret_cast<uint64_t>(s_Info.entity);
        s_Candidate.brickRid = s_Info.brickRid;
        s_Candidate.entityId = s_Info.id;
        s_Candidate.repo = s_Info.repo;
        s_Candidate.kind = KindOf(s_Info);

        if (s_Info.bits) {
            s_Candidate.startEnabled = s_Info.bits->startEnabled;
            s_Candidate.crowd = s_Info.bits->crowd;
        }

        // A role the name tells; an unreadable name may hide one.
        if (!s_Info.name)
            s_Candidate.role = "name unreadable";
        else if (const auto s_Role = fr::RoleFromActorName(*s_Info.name))
            s_Candidate.role = std::string(*s_Role);

        s_Candidates.push_back(std::move(s_Candidate));
    }

    // Stationary entries: replaced beyond the shares, and dropped when no
    // eligible actor is left (never an actor twice: a duplicate breaks the target count).
    fr::StationaryRule s_Rule;
    s_Rule.fill = p_Swaps;
    s_Rule.trim = s_NoTrim.empty();
    const fr::TargetPoolPlan s_Plan =
        fr::PlanTargetSwaps(p_Seed.seed, s_Scene, s_Entries, s_Candidates, s_Share, s_BrickRid,
                            s_Movers ? &*s_Movers : nullptr, s_AnyGuard, s_GuardShare, &s_Excluded, s_Rule);

    // NPCs a loaded map variant shuts in (fr::SealedActors: the Garden Show's
    // closed mansion), by variant: named, as they are the reason most often.
    {
        std::map<std::string_view, std::vector<fr::SealedActor>> s_Sealed;

        for (const fr::SealedActor& s_Row : fr::LoadedSealedActors(s_LoadedRids)) {
            if (!s_Full || s_Row.option != s_Full->option)
                s_Sealed[s_Row.option].push_back(s_Row);
        }

        for (const auto& [s_Option, s_Rows] : s_Sealed) {
            std::string s_Names;
            std::string s_InPool;
            size_t s_PoolCount = 0;

            for (const fr::SealedActor& s_Row : s_Rows) {
                s_Names += fmt::format("{}{}", s_Names.empty() ? "" : ", ", s_Row.name);
                const auto s_Guid = fr::ParseGuid(s_Row.repo);
                const bool s_InThePool =
                    s_Guid && std::any_of(s_Entries.begin(), s_Entries.end(), [&](const fr::PoolEntry& p_Entry) {
                        return p_Entry.repo && *p_Entry.repo == *s_Guid;
                    });

                if (s_InThePool) {
                    ++s_PoolCount;
                    s_InPool += fmt::format("{}{}", s_InPool.empty() ? "" : ", ", s_Row.name);
                }
            }

            LogTo(fr::LogTopic::Targets,
                  "target pool: map variant {} closes off part of the map: {} NPCs stand or walk "
                  "where 47 cannot go, so they are never targets here ({}); {} of them in the pool{}{}",
                  s_Option, s_Rows.size(), s_Names, s_PoolCount, s_PoolCount ? ": " : "", s_InPool);
        }
    }

    // The main story mission's targets here, named.
    if (const std::vector<fr::StoryTarget> s_Story = fr::SceneStoryTargets(s_Scene); !s_Story.empty()) {
        std::string s_Names;
        std::string s_InPool;

        for (const fr::StoryTarget& s_Row : s_Story) {
            s_Names += fmt::format("{}{}", s_Names.empty() ? "" : ", ", s_Row.name);
            const auto s_Guid = fr::ParseGuid(s_Row.repo);

            if (s_Guid && std::any_of(s_Entries.begin(), s_Entries.end(), [&](const fr::PoolEntry& p_Entry) {
                    return p_Entry.repo && *p_Entry.repo == *s_Guid;
                }))
                s_InPool += fmt::format("{}{}", s_InPool.empty() ? "" : ", ", s_Row.name);
        }

        LogTo(fr::LogTopic::Targets,
              "target pool: {}: the main story mission's targets are never targets here ({}); "
              "in the pool: {}",
              s_Scene, s_Names, s_InPool.empty() ? std::string("none") : s_InPool);
    }

    if (!s_Excluded.empty()) {
        std::string s_Ids;

        for (const fr::Guid& s_Repo : s_Excluded)
            s_Ids += fmt::format("{}{}", s_Ids.empty() ? "" : ", ", fr::FormatGuid(s_Repo));

        // The main story's targets, and a loaded map variant's or the full variant's.
        LogTo(fr::LogTopic::Targets,
              "target pool: {}: {} actors are never targets here (main story targets, map variant "
              "exclusions: {}); {} in the pool: {} replaced beyond the shares, {} left in{}",
              s_Full ? s_FullName : s_Scene, s_Excluded.size(), s_Ids, s_Plan.excludedInPool, s_Plan.excludedForced,
              s_Plan.excludedKept,
              s_Plan.excludedKept ? " (NO ELIGIBLE ACTOR OR UNREADABLE ENTRY: they may still be picked)" : "");
    }

    // Where the pool's actors live (at Whittleton, the start-disabled ones are in the mission brick).
    {
        std::map<std::pair<uint64_t, bool>, size_t> s_Where; // (brick, start enabled) -> count

        for (const fr::PoolEntry& s_Entry : s_Entries) {
            if (s_Entry.handle)
                ++s_Where[{s_Entry.brickRid, s_Entry.startEnabled.value_or(false)}];
        }

        std::string s_Text;

        for (const auto& [s_Key, s_Count] : s_Where)
            s_Text += fmt::format("{}{} {} in {}", s_Text.empty() ? "" : ", ", s_Count,
                                  s_Key.second ? "start enabled" : "start disabled or unread",
                                  BrickText(s_Key.first, s_BrickRid));

        std::string s_Sources;

        for (const uint64_t s_Brick : s_Plan.sourceBricks)
            s_Sources += fmt::format("{}{:016X}", s_Sources.empty() ? "" : ", ", s_Brick);

        LogTo(fr::LogTopic::Targets,
              "target pool: pool actors by brick: {}; candidate bricks (start-enabled pool actors, not the mission "
              "brick): {}",
              s_Text.empty() ? std::string("none") : s_Text, s_Sources.empty() ? std::string("none") : s_Sources);
    }

    // No candidate then; the people who can never be targets here are still taken out.
    if (s_Plan.sourceBricks.empty() && s_Plan.drops.empty())
        return s_Untouched("no brick other than the mission brick supplies a start-enabled pool actor");

    std::string s_Rejected;

    for (const auto& [s_Reject, s_Count] : s_Plan.rejected)
        s_Rejected +=
            fmt::format("{}{} {}", s_Rejected.empty() ? "" : ", ", fr::CandidateRejectName(s_Reject), s_Count);

    std::map<int32_t, size_t> s_EligibleKinds;

    for (size_t i = 0; i < s_Candidates.size(); ++i) {
        if (s_Plan.verdicts[i] == fr::CandidateReject::None)
            ++s_EligibleKinds[s_Candidates[i].kind];
    }

    std::string s_Kinds;

    for (const auto& [s_Kind, s_Count] : s_EligibleKinds)
        s_Kinds += fmt::format("{}{} {}", s_Kinds.empty() ? "" : ", ", KindText(s_Kind), s_Count);

    LogTo(fr::LogTopic::Targets,
          "target pool: {} entries, {} replaceable, {} wanted; {} actors: {} eligible ({}), rejected: {}",
          s_Entries.size(), s_Plan.replaceable, s_Plan.wanted, s_Actors.size(), s_Plan.eligible,
          s_Kinds.empty() ? std::string("none") : s_Kinds, s_Rejected.empty() ? std::string("none") : s_Rejected);

    if (!p_Swaps) {
        // No candidates, no shares.
    }
    else if (m_AnyGuard) {
        LogTo(fr::LogTopic::Targets, "target pool: eligible: {} guards, {} civilian movers", s_Plan.eligibleGuards,
              s_Plan.eligibleMovers);
        LogTo(
            fr::LogTopic::Targets,
            "target pool: civilian movers share {:.3f} of {} non-guard entries -> wanted {}, swapped {}; guards share "
            "{:.3f} of the remaining {} -> wanted {}, swapped {}{}",
            s_Share, s_Plan.moverSlots, s_Plan.moverWanted, s_Plan.moverSwapped, s_GuardShare, s_Plan.guardRemaining,
            s_Plan.guardWanted, s_Plan.guardSwapped,
            s_Plan.eligibleMovers == 0 ? " (no civilian mover eligible: guards may take civilian entries)" : "");
    }
    else {
        LogTo(fr::LogTopic::Targets,
              "target pool: eligible: {} listed movers (target_pool_any_guard off: no guard outside the table)",
              s_Plan.eligibleMovers);
        LogTo(fr::LogTopic::Targets, "target pool: movers share {:.3f} of {} entries -> wanted {}, swapped {}", s_Share,
              s_Plan.moverSlots, s_Plan.moverWanted, s_Plan.moverSwapped);
    }

    // Variety: with few eligible civilian movers per wanted slot, every campaign gets nearly the same actors.
    if (s_Movers)
        LogTo(fr::LogTopic::Targets,
              "target pool: mover table: {} movers listed for this scene, {} eligible here, {} wanted{}",
              s_Movers->size(), s_Plan.eligibleMovers, s_Plan.moverWanted,
              s_Plan.eligibleMovers < 2 * s_Plan.moverWanted
                  ? "; VARIETY WARNING: fewer than 2 eligible movers per wanted entry, so most campaigns get the same "
                    "actors"
                  : "");

    if (!p_Swaps) {
        // Only the ones who can never be targets here are taken out (the "target pool drop:" lines).
    }
    else if (!s_Movers) {
        LogTo(fr::LogTopic::Targets,
              "target pool: stationary NPCs: not known without a mover table; none replaced or dropped");
    }
    else {
        std::string s_Left;

        if (s_Plan.stationaryKept > 0)
            s_Left = !s_Rule.trim ? fmt::format(" (the pool could not be shortened: {})", s_NoTrim)
                                  : fmt::format(" (the pool keeps at least {} entries)", s_Rule.minLength);

        LogTo(
            fr::LogTopic::Targets,
            "target pool: stationary NPCs (civilians the mover table does not list{}): {} in the pool; {} replaced by "
            "the shares, {} replaced beyond them, {} dropped (pool {} -> {}), {} left in{}",
            m_AnyGuard ? "" : ", guards too", s_Plan.stationaryInPool, s_Plan.stationaryByShares,
            s_Plan.stationaryFilled, s_Plan.stationaryDropped, s_Entries.size(), s_Plan.length, s_Plan.stationaryKept,
            s_Left);
    }

    const bool s_Trimmed = !s_Plan.drops.empty();

    if (s_Plan.wanted == 0 && s_Plan.swaps.empty() && !s_Trimmed)
        return s_Untouched(
            !p_Swaps ? (s_Plan.excludedKept > 0 ? fmt::format("the pool could not be shortened ({})", s_NoTrim)
                                                : std::string("no one in it is ruled out"))
                     : fmt::format("the shares of {} replaceable entries want no swaps", s_Plan.replaceable));

    if (s_Plan.capped)
        LogTo(fr::LogTopic::Targets, "target pool: capped by eligible actors: wanted {}, swapped {}", s_Plan.wanted,
              s_Plan.swaps.size());

    if (s_Plan.swaps.empty() && !s_Trimmed)
        return s_Untouched("nothing to swap");

    // The pool as written: the swaps, then the trim's moves, plan.length long.
    const std::vector<RawElement> s_Final = fr::ApplyTargetSwaps(s_Original, s_Plan, [&](size_t p_Candidate) {
        const ActorInfo& s_New = s_Actors[p_Candidate];
        return RawElement {s_New.entity, const_cast<ZActor*>(s_New.actor)};
    });

    // Write every storage, checking each element still holds the original;
    // any mismatch or fault restores what was written.
    struct Written {
        uintptr_t address;
        RawElement original;
    };

    std::vector<Written> s_Written;
    std::string s_Failure;

    for (const uintptr_t s_Storage : s_Storages) {
        for (size_t i = 0; i < s_Final.size(); ++i) {
            if (s_Final[i] == s_Original[i])
                continue;

            const uintptr_t s_Address = s_Storage + i * kElementSize;
            RawElement s_Now;

            if (const uint32_t s_Code = ReadEngineBytes(s_Address, &s_Now, kElementSize)) {
                s_Failure = fmt::format("reading 0x{:X} {}", s_Address, FaultText(s_Code));
                break;
            }

            if (s_Now != s_Original[i]) {
                s_Failure = fmt::format("element {} at 0x{:X} changed since it was read", i, s_Address);
                break;
            }

            if (const DWORD s_Code = WriteEngineBytes(s_Address, &s_Final[i], kElementSize)) {
                s_Failure = fmt::format("writing 0x{:X} {}", s_Address, FaultText(s_Code));
                break;
            }

            s_Written.push_back({s_Address, s_Now});
        }

        if (!s_Failure.empty())
            break;
    }

    // The trim: each header's end moves down (the elements past it stay as they were).
    struct WrittenEnd {
        uintptr_t address;
        uintptr_t original;
    };

    std::vector<WrittenEnd> s_WrittenEnds;

    for (const uintptr_t s_Header : s_Headers) {
        if (!s_Trimmed || !s_Failure.empty())
            break;

        if (const auto s_Why = HeaderProblem(s_Header, s_Storages, s_Original.size())) {
            s_Failure = fmt::format("header 0x{:X}: {}", s_Header, *s_Why);
            break;
        }

        // Read just now by HeaderProblem: begin, then end.
        uintptr_t s_Ends[2] {};

        if (const uint32_t s_Code = ReadEngineBytes(s_Header, s_Ends, sizeof(s_Ends))) {
            s_Failure = fmt::format("reading header 0x{:X} {}", s_Header, FaultText(s_Code));
            break;
        }

        const uintptr_t s_End = s_Ends[1];
        const uintptr_t s_NewEnd = s_Ends[0] + s_Plan.length * kElementSize;

        if (const DWORD s_Code = WriteEngineBytes(s_Header + 8, &s_NewEnd, sizeof(s_NewEnd))) {
            s_Failure = fmt::format("writing the end of header 0x{:X} {}", s_Header, FaultText(s_Code));
            break;
        }

        s_WrittenEnds.push_back({s_Header + 8, s_End});
    }

    // Undoes every write (elements and header ends): how many of them.
    const auto s_Restore = [&] {
        size_t s_Restored = 0;

        for (auto s_It = s_WrittenEnds.rbegin(); s_It != s_WrittenEnds.rend(); ++s_It)
            s_Restored += WriteEngineBytes(s_It->address, &s_It->original, sizeof(s_It->original)) ? 0 : 1;

        for (auto s_It = s_Written.rbegin(); s_It != s_Written.rend(); ++s_It)
            s_Restored += WriteEngineBytes(s_It->address, &s_It->original, kElementSize) ? 0 : 1;

        return s_Restored;
    };

    if (!s_Failure.empty()) {
        const size_t s_Restored = s_Restore();
        return s_Untouched(fmt::format("{}; {} of {} writes restored", s_Failure, s_Restored,
                                       s_Written.size() + s_WrittenEnds.size()));
    }

    // Read back.
    std::vector<ZEntityType**> s_Expected;

    for (const RawElement& s_Element : s_Final)
        s_Expected.push_back(s_Element.entity);

    size_t s_Mismatches = 0;
    std::vector<ZEntityType**> s_ReadBack;

    for (const RawPool& s_Pool : s_Pickers)
        s_ReadBack.push_back(s_Pool.owner);

    if (s_Helper)
        s_ReadBack.push_back(s_Helper->owner);

    for (ZEntityType** const s_Owner : s_ReadBack) {
        const RawPool s_After = ReadPool(s_Owner);

        if (!s_After.why.empty() || s_After.items.size() != s_Expected.size()) {
            ++s_Mismatches;
            continue;
        }

        for (size_t i = 0; i < s_Expected.size(); ++i)
            s_Mismatches += s_After.items[i].entity != s_Expected[i] ? 1 : 0;
    }

    if (s_Mismatches > 0) {
        const size_t s_Restored = s_Restore();
        return s_Untouched(fmt::format("read back found {} mismatches; {} of {} writes restored", s_Mismatches,
                                       s_Restored, s_Written.size() + s_WrittenEnds.size()));
    }

    m_LastWritten = s_Expected;
    size_t s_SameKind = 0;

    for (const fr::TargetSwap& s_Swap : s_Plan.swaps) {
        s_SameKind += s_Swap.sameKind ? 1 : 0;
        const auto& s_NewRepo = s_Candidates[s_Swap.candidate].repo;
        std::string s_Tier = fr::CandidateTierName(s_Swap.tier);

        if (s_PoolInfo[s_Swap.slot].repo && s_Excluded.contains(*s_PoolInfo[s_Swap.slot].repo))
            s_Tier += ", replacing an actor who can never be a target here";
        else if (s_Swap.fill)
            s_Tier += ", replacing a stationary NPC beyond the shares";

        if (s_MoverTiers && s_NewRepo) {
            if (const auto s_It = s_MoverTiers->find(*s_NewRepo); s_It != s_MoverTiers->end())
                s_Tier += fmt::format(", table {}", fr::MoverTierName(s_It->second));
        }

        LogTo(fr::LogTopic::Targets, "target pool swap: [{}] {} ({}) -> {} ({}); tier {}{}{}", s_Swap.slot,
              ActorLine(s_PoolInfo[s_Swap.slot]), PlaceText(s_PoolInfo[s_Swap.slot], s_BrickRid),
              ActorLine(s_Actors[s_Swap.candidate]), PlaceText(s_Actors[s_Swap.candidate], s_BrickRid), s_Tier,
              s_Swap.sameKind ? "" : " (not like for like)",
              s_Swap.slot < s_VolumeIds.size()
                  ? fmt::format("; volume [{}] {}", s_Swap.slot, IdText(s_VolumeIds[s_Swap.slot]))
                  : std::string());
    }

    // Each dropped entry, and the kept one moved into its slot.
    for (const size_t s_Slot : s_Plan.drops) {
        const auto s_Move = std::find_if(s_Plan.moves.begin(), s_Plan.moves.end(),
                                         [&](const fr::PoolMove& p_Move) { return p_Move.to == s_Slot; });
        std::string s_Then = "past the new end";

        if (s_Move != s_Plan.moves.end()) {
            const auto s_Swap = std::find_if(s_Plan.swaps.begin(), s_Plan.swaps.end(),
                                             [&](const fr::TargetSwap& p_Swap) { return p_Swap.slot == s_Move->from; });
            s_Then = fmt::format(
                "[{}] {} moves into its place", s_Move->from,
                ActorLine(s_Swap != s_Plan.swaps.end() ? s_Actors[s_Swap->candidate] : s_PoolInfo[s_Move->from]));
        }

        LogTo(fr::LogTopic::Targets, "target pool drop: [{}] {} ({}); {}", s_Slot, ActorLine(s_PoolInfo[s_Slot]),
              s_PoolInfo[s_Slot].repo && s_Excluded.contains(*s_PoolInfo[s_Slot].repo) ? "never a target here"
                                                                                       : "stationary",
              s_Then);
    }

    LogTo(fr::LogTopic::Targets,
          "target pool: swapped {} of {} entries ({} like for like) in {} storage(s); {}; read back ok ({} lists)",
          s_Plan.swaps.size(), s_Entries.size(), s_SameKind, s_Storages.size(),
          s_Trimmed ? fmt::format("{} entries dropped, size {} -> {} ({} header(s) shortened)", s_Plan.drops.size(),
                                  s_Entries.size(), s_Plan.length, s_WrittenEnds.size())
                    : fmt::format("size unchanged at {}", s_Entries.size()),
          s_ReadBack.size());
    return true;
}
