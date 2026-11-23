// The variant exit fix (see VariantExits.h). Every raw engine step follows
// the guard pattern in Guard.h.

#include "VariantExits.h"

#include "../core/Guid.h"
#include "../core/Merces.h"   // fr::JsonEventName, fr::JsonScalarsWithKeyContaining
#include "../core/Scene.h"    // fr::ToLower
#include "../core/Variants.h" // fr::FindVariantBrick
#include "BriefingGraphs.h"
#include "EngineText.h"
#include "EntityReads.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "PinIds.h"

#include <Glacier/Hash.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZString.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <set>
#include <span>

namespace {

constexpr const char* kRepositoryItemClassName = "ZRepositoryItemEntity";

// A template this large is not an exit template (the decoded ones have 35 to 59 entities).
constexpr int64_t kMaxExitTemplateEntities = 400;

constexpr std::string_view kTag = "variant exits";

static_assert(Hash::Crc32(fr::kHijackerActivatePin.data(), fr::kHijackerActivatePin.size()) == fr::kPinExitActivated);
static_assert(Hash::Crc32(fr::kHijackerDonePin.data(), fr::kHijackerDonePin.size()) == fr::kPinSequenceDone);

// A pin name's id (CRC-32), for names read from a blueprint or a table.
uint32_t PinId(std::string_view p_Name) {
    return Hash::Crc32(p_Name.data(), p_Name.size());
}

// A ValueBool's m_bValue set through the engine, with its change handlers (the conditions that read it are told).
struct BoolSetArgs {
    ZEntityType** entity;
    bool value;
    bool ok;
};

__declspec(noinline) void SetBoolOf(BoolSetArgs* p_Args) {
    p_Args->ok = ZEntityRef(p_Args->entity).SetProperty(fr::kPropValue, ZObjectRef::From<bool>(p_Args->value), true);
}

__declspec(noinline) DWORD SafeSetBool(BoolSetArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SetBoolOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// A ValueBool's m_bValue read raw, or nothing.
std::optional<bool> ReadBool(ZEntityType** p_Entity) {
    const PropertyLocation s_Location = LocateProperty(p_Entity, "m_bValue");

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != 1)
        return std::nullopt;

    uint8_t s_Byte = 0;

    if (ReadEngineBytes(s_Location.address, &s_Byte, 1))
        return std::nullopt;

    return s_Byte != 0;
}

std::string BoolText(const std::optional<bool>& p_Value) {
    return p_Value ? (*p_Value ? "true" : "false") : "unreadable";
}

std::string BrickName(uint64_t p_Rid) {
    const fr::VariantBrick* s_Brick = fr::FindVariantBrick(p_Rid);
    return s_Brick ? std::string(s_Brick->name) : fmt::format("brick {:016X}", p_Rid);
}

using briefing_graphs::SubName;

// The blueprint links of the factory p_Factory (the exit's template, or the brick a contract exit is placed in),
// checked against the anchor the walk read (briefing_graphs::GraphOf).
std::optional<fr::BriefingGraph> GraphOf(ZEntityBlueprintFactoryBase* p_Factory, int p_Index, uint64_t p_Id,
                                         std::string* p_Why) {
    return briefing_graphs::GraphOf(p_Factory, {{p_Index, p_Id}}, p_Why);
}

// A ZGuid property read raw ("m_sId"), formatted, or empty.
std::string ReadGuidText(ZEntityType** p_Entity, std::string_view p_Name) {
    const PropertyLocation s_Location = LocateProperty(p_Entity, p_Name);

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != sizeof(fr::Guid))
        return {};

    fr::Guid s_Id;

    if (ReadEngineBytes(s_Location.address, &s_Id, sizeof(s_Id)) || s_Id.IsEmpty())
        return {};

    return fr::FormatGuid(s_Id);
}

// One exit's part the walk found.
struct Seen {
    ZEntityType** entity;
    ZEntityType** parent;
    ZEntityBlueprintFactoryBase* parentFactory;
    int index;
    uint64_t id;
    uint64_t brickRid;
};

// A pin to arm, resolved to its entity.
struct PinPlan {
    ZEntityType** entity = nullptr;
    std::string pin;
    std::string label;
    std::vector<std::pair<ZEntityType**, std::pair<uint32_t, bool>>> resend; // entity, (pin id, output)
};

} // namespace

void VariantExits::Arm(std::string_view p_Scene, std::string_view p_Option, std::vector<uint64_t> p_VariantBricks,
                       bool p_Route) {
    m_Scene = std::string(p_Scene);
    m_Option = std::string(p_Option);
    m_VariantBricks = std::move(p_VariantBricks);
    m_Route = p_Route;
    m_Done = false;
}

void VariantExits::Forget() noexcept {
    m_Scene.clear();
    m_Option.clear();
    m_VariantBricks.clear();
    m_Route = false;
    OnSceneClear();
}

void VariantExits::ForgetPins() noexcept {
    m_PinsOn.store(false, std::memory_order_release);

    for (WatchPin& s_Pin : m_Pins) {
        s_Pin.entity.store(nullptr, std::memory_order_relaxed);
        s_Pin.pin.store(0, std::memory_order_relaxed);
        s_Pin.logged.store(false, std::memory_order_relaxed);
        s_Pin.resendCount = 0;
    }

    m_PinCount = 0;
}

void VariantExits::OnSceneClear() noexcept {
    m_Done = false;
    m_OffDone = false;
    m_EventsWanted.store(false, std::memory_order_relaxed);
    ForgetPins();
    std::lock_guard s_Lock(m_Mutex);
    m_Freelancer.clear();
    m_Variant.clear();
    m_Plan = {};
    m_Clock = {};
    m_Played = 0.0;
    m_EvergreenExit = false;
    m_EndLogged = false;
}

void VariantExits::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    if (m_Scene.empty())
        return;

    // Once the exits' logic runs: the exits the loaded bricks leave without their getaway are switched off.
    if (p_Stage == ESceneLoadingStage::eLoading_ScenePlaying) {
        if (m_Done && !m_OffDone) {
            m_OffDone = true;
            Contained(kTag, "switching exits off", [&] { SwitchOff(); });
        }
        return;
    }

    if (m_Done || (p_Stage != ESceneLoadingStage::eLoading_SceneAllocated &&
                   p_Stage != ESceneLoadingStage::eLoading_SceneStarted))
        return;

    Contained(kTag, "scene allocation", [&] {
        if (!p_Context)
            p_Context = CurrentSceneContext();

        if (!p_Context ||
            fr::ToLower(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView()) != fr::ToLower(m_Scene))
            return;

        Find(p_Context, p_Stage == ESceneLoadingStage::eLoading_SceneStarted);
    });
}

std::string VariantExits::Label(const Exit& p_Exit, bool p_Freelancer) const {
    const std::string_view s_Template = fr::ExitTemplateName(p_Exit.templateRid);
    std::string s_Label = p_Freelancer ? "Freelancer exit " : "variant exit ";

    if (!s_Template.empty())
        s_Label += fmt::format("{} {:016x}", s_Template, p_Exit.rootId);
    else if (p_Exit.templateRid != 0)
        s_Label += fmt::format("(template {:016X}) {:016x}", p_Exit.templateRid, p_Exit.rootId);
    else
        s_Label += fmt::format("\"{}\" {:016x} (placed in its brick)", p_Exit.name, p_Exit.anchorId);

    if (!p_Exit.repo.empty())
        s_Label += fmt::format(" repo {}", p_Exit.repo);

    return s_Label;
}

void VariantExits::Describe(Exit& p_Exit) {
    // The repository id: the template's ZRepositoryItemEntity (its m_sId is the brick's m_sId alias).
    int64_t s_Count = 0;

    if (p_Exit.templateRid != 0 && !ReadSubEntityCount(p_Exit.factory, &s_Count) && s_Count > 0 &&
        s_Count <= kMaxExitTemplateEntities) {
        for (int i = 0; i < static_cast<int>(s_Count) && p_Exit.repo.empty(); ++i) {
            ZEntityType** s_Sub = nullptr;
            ZEntityBlueprintFactoryBase* s_SubFactory = nullptr;
            EntityClassCheck s_Check {};

            if (ReadSubEntityAt(p_Exit.factory, p_Exit.root, i, &s_Sub, &s_SubFactory) || !s_Sub ||
                CheckEntityClass(s_Sub, kRepositoryItemClassName, &s_Check) || !s_Check.matches)
                continue;

            p_Exit.repo = ReadGuidText(s_Sub, "m_sId");
        }
    }

    // Where it stands: the instance's root (the brick's root says nothing for a contract exit placed in a brick).
    if (p_Exit.templateRid == 0) {
        p_Exit.where = "(no position: placed in its brick)";
        return;
    }

    const PositionRead s_Read = ReadPosition(p_Exit.root, true);
    p_Exit.where = s_Read.at ? fmt::format("({:.2f}, {:.2f}, {:.2f})", s_Read.at->x, s_Read.at->y, s_Read.at->z)
                             : std::string("(position not read)");
}

bool VariantExits::AddPin(PinKind p_Kind, size_t p_ExitIndex, ZEntityType** p_Entity, const std::string& p_Pin,
                          const std::string& p_Label, const std::vector<Resend>& p_Resend) {
    if (!p_Entity || m_PinCount >= kMaxPins || p_Resend.size() > fr::kLineMaxResend)
        return false;

    for (size_t i = 0; i < m_PinCount; ++i) {
        if (m_Pins[i].entity.load(std::memory_order_relaxed) == p_Entity &&
            m_Pins[i].pin.load(std::memory_order_relaxed) == PinId(p_Pin))
            return false; // armed already
    }

    WatchPin& s_Pin = m_Pins[m_PinCount];
    s_Pin.kind = p_Kind;
    s_Pin.exit = p_ExitIndex;
    s_Pin.resendCount = p_Resend.size();

    for (size_t r = 0; r < p_Resend.size(); ++r)
        s_Pin.resend[r] = p_Resend[r];

    s_Pin.label = p_Label;
    s_Pin.logged.store(false, std::memory_order_relaxed);
    s_Pin.pin.store(PinId(p_Pin), std::memory_order_relaxed);
    s_Pin.entity.store(p_Entity, std::memory_order_relaxed);
    ++m_PinCount;
    return true;
}

// Once the scene's entities exist: every Freelancer exit and every contract
// exit of the variant's bricks; the variant's routed into Freelancer's flow.
void VariantExits::Find(ZEntitySceneContext* p_Context, bool p_LastChance) {
    const auto s_Start = std::chrono::steady_clock::now();
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    const std::set<uint64_t> s_VariantBricks(m_VariantBricks.begin(), m_VariantBricks.end());
    std::vector<uint64_t> s_LoadedRids;

    for (size_t i = 0; i < s_Bricks.size(); ++i)
        s_LoadedRids.push_back(s_Bricks[i].m_RuntimeResourceID.GetID());

    const auto s_IsVariantBrick = [&](uint64_t p_Rid) {
        return s_VariantBricks.contains(p_Rid) || fr::FindVariantBrick(p_Rid) != nullptr;
    };

    std::vector<Seen> s_Hijackers;
    std::vector<Seen> s_Contracts;
    size_t s_ReadFaults = 0;

    const WalkStats s_Walk = VisitAllEntities(s_Bricks, [&](const VisitedEntity& p_Visit) {
        if (!p_Visit.parentFactory || p_Visit.index < 0)
            return false;

        uint64_t s_Rid = 0;

        if (p_Visit.factory && ReadFactoryRid(p_Visit.factory, &s_Rid)) {
            ++s_ReadFaults;
            s_Rid = 0;
        }

        if (s_Rid != 0 && fr::IsExitHijackerResource(s_Rid)) {
            std::string s_Class;
            uint64_t s_Id = 0;

            if (ReadEntityClassName(p_Visit.entity, s_Class, &s_Id))
                ++s_ReadFaults;

            s_Hijackers.push_back(
                {p_Visit.entity, p_Visit.parent, p_Visit.parentFactory, p_Visit.index, s_Id, p_Visit.brickRid});
            return true;
        }

        EntityClassCheck s_Check {};

        if (CheckEntityClass(p_Visit.entity, fr::kContractsExitClass.data(), &s_Check)) {
            ++s_ReadFaults;
            return false;
        }

        if (!s_Check.matches)
            return false;

        s_Contracts.push_back(
            {p_Visit.entity, p_Visit.parent, p_Visit.parentFactory, p_Visit.index, s_Check.id, p_Visit.brickRid});
        return true;
    });

    // Nothing at scene allocation: looked for again at scene start.
    if (s_Hijackers.empty() && s_Contracts.empty() && !p_LastChance)
        return;

    m_Done = true;
    ForgetPins();

    std::vector<Exit> s_Freelancer;
    std::vector<Exit> s_Variant;
    std::vector<std::vector<PinPlan>> s_Confirms; // per Freelancer exit
    std::vector<std::vector<PinPlan>> s_Ends;     // per variant exit
    std::vector<std::vector<PinPlan>> s_Starts;   // per variant exit
    std::set<ZEntityType**> s_FreelancerRoots;

    const auto s_Resolve = [](const Exit& p_Exit, int32_t p_Index) -> ZEntityType** {
        ZEntityType** s_Entity = nullptr;
        ZEntityBlueprintFactoryBase* s_Sub = nullptr;
        return ReadSubEntityAt(p_Exit.factory, p_Exit.root, p_Index, &s_Entity, &s_Sub) ? nullptr : s_Entity;
    };
    const auto s_Lookup = [](const Exit& p_Exit, uint64_t p_Id) -> ZEntityType** {
        return LookUpSubEntity(p_Exit.root, p_Exit.factory, p_Id).entity;
    };
    const auto s_Common = [&](Exit& p_Exit, const Seen& p_Seen) {
        p_Exit.root = p_Seen.parent;
        p_Exit.factory = p_Seen.parentFactory;
        p_Exit.anchor = p_Seen.entity;
        p_Exit.anchorIndex = p_Seen.index;
        p_Exit.anchorId = p_Seen.id;
        p_Exit.brickRid = p_Seen.brickRid;
        p_Exit.inVariantBrick = s_IsVariantBrick(p_Seen.brickRid);

        if (ReadFactoryRid(p_Seen.parentFactory, &p_Exit.templateRid))
            p_Exit.templateRid = 0;

        // A contract exit placed straight in a brick: its parent is the brick (no exit template).
        if (fr::ExitTemplateName(p_Exit.templateRid).empty()) {
            int64_t s_Count = 0;

            if (ReadSubEntityCount(p_Seen.parentFactory, &s_Count) || s_Count > kMaxExitTemplateEntities)
                p_Exit.templateRid = 0;
        }

        std::string s_Class;

        if (p_Exit.templateRid != 0 && ReadEntityClassName(p_Exit.root, s_Class, &p_Exit.rootId))
            p_Exit.rootId = 0;
    };

    // Freelancer exits: one per hijacker.
    for (const Seen& s_Seen : s_Hijackers) {
        Exit s_Exit;
        s_Common(s_Exit, s_Seen);
        s_FreelancerRoots.insert(s_Seen.parent);
        std::vector<PinPlan> s_Pins;
        std::string s_Why;

        if (const std::optional<fr::BriefingGraph> s_Graph = GraphOf(s_Exit.factory, s_Seen.index, s_Seen.id, &s_Why)) {
            s_Exit.pinsFrom = "its template's blueprint links";

            for (const fr::ExitPin& s_Pin :
                 fr::FindFreelancerConfirmPins(s_Seen.index, s_Graph->ids.size(), s_Graph->links))
                s_Pins.push_back({s_Resolve(s_Exit, s_Pin.entity),
                                  s_Pin.pin,
                                  fmt::format("{} {}", SubName(*s_Graph, s_Pin.entity), s_Pin.pin),
                                  {}});
        }
        else if (const fr::KnownExitTemplate* s_Known = fr::FindKnownExitTemplate(s_Exit.templateRid);
                 s_Known && s_Known->anchor == s_Seen.id) {
            s_Exit.pinsFrom = "the template's known sub-entity ids";

            for (const fr::KnownExitPin& s_Pin : s_Known->confirms)
                s_Pins.push_back({s_Lookup(s_Exit, s_Pin.sub),
                                  std::string(s_Pin.pin),
                                  fmt::format("{:016x} {}", s_Pin.sub, s_Pin.pin),
                                  {}});
        }
        else
            s_Exit.pinsFrom = fmt::format("its confirmation is not watched ({})", s_Why);

        s_Exit.broken = std::string(fr::KnownBrokenExit(m_Option, s_Exit.rootId));

        // Its getaway gone with the loaded bricks: its switches, by their ids in the template.
        if (const fr::ExitSwitchOff* s_Off = fr::FindExitSwitchOff(s_LoadedRids, s_Exit.rootId)) {
            s_Exit.switchOff = std::string(s_Off->why);
            const fr::KnownExitTemplate* s_Known = fr::FindKnownExitTemplate(s_Exit.templateRid);

            if (!s_Known || s_Known->offs.empty())
                s_Exit.offsFrom = fmt::format("its template {:016X} has no known switches", s_Exit.templateRid);
            else {
                for (const fr::KnownExitPin& s_Pin : s_Known->offs) {
                    if (ZEntityType** s_Entity = s_Lookup(s_Exit, s_Pin.sub))
                        s_Exit.offs.emplace_back(s_Entity, s_Pin);
                }

                if (s_Exit.offs.empty())
                    s_Exit.offsFrom = "none of its template's switches was found in the instance";
            }
        }

        s_Exit.facts = {};
        Describe(s_Exit);
        s_Freelancer.push_back(std::move(s_Exit));
        s_Confirms.push_back(std::move(s_Pins));
    }

    // The variant's contract exits (not a Freelancer exit's own, which nothing triggers).
    size_t s_OutsideVariant = 0;

    for (const Seen& s_Seen : s_Contracts) {
        if (s_FreelancerRoots.contains(s_Seen.parent))
            continue;

        Exit s_Exit;
        s_Common(s_Exit, s_Seen);

        // An appended variant: its own bricks only (the map's are Freelancer's to keep).
        if (!m_VariantBricks.empty() && !s_Exit.inVariantBrick) {
            ++s_OutsideVariant;
            continue;
        }

        std::vector<PinPlan> s_EndPins;
        std::vector<PinPlan> s_StartPins;
        std::string s_Why;

        if (const std::optional<fr::BriefingGraph> s_Graph = GraphOf(s_Exit.factory, s_Seen.index, s_Seen.id, &s_Why)) {
            s_Exit.pinsFrom = "its blueprint links";
            s_Exit.name = SubName(*s_Graph, s_Seen.index);
            s_Exit.facts.graphRead = true;
            const fr::VariantExitPins s_Pins =
                fr::FindVariantExitPins(s_Seen.index, s_Graph->ids.size(), s_Graph->links);
            s_Exit.facts.skipped = s_Pins.skipped;

            if (s_Pins.skipped > 0)
                s_Exit.pinsFrom += fmt::format(" ({} feed{} of its end cannot be blocked: {})", s_Pins.skipped,
                                               s_Pins.skipped == 1 ? "" : "s", s_Pins.skipWhy);

            for (const fr::ExitPin& s_Pin : s_Pins.ends) {
                PinPlan s_Plan {s_Resolve(s_Exit, s_Pin.entity),
                                s_Pin.pin,
                                fmt::format("{} {}", SubName(*s_Graph, s_Pin.entity), s_Pin.pin),
                                {}};
                bool s_AllResend = s_Plan.entity != nullptr;

                for (const size_t s_Link : s_Pin.resend) {
                    const fr::BriefingLink& s_To = s_Graph->links[s_Link];
                    ZEntityType** s_Target = s_Resolve(s_Exit, s_To.to);
                    s_AllResend = s_AllResend && s_Target;
                    s_Plan.resend.push_back(
                        {s_Target, {PinId(s_To.toPin), s_To.kind == fr::BriefingLinkKind::OutputForwarding}});
                }

                if (!s_AllResend) {
                    ++s_Exit.facts.skipped;
                    continue;
                }

                s_EndPins.push_back(std::move(s_Plan));
            }

            for (const fr::ExitPin& s_Pin : s_Pins.starts)
                s_StartPins.push_back({s_Resolve(s_Exit, s_Pin.entity),
                                       s_Pin.pin,
                                       fmt::format("{} {}", SubName(*s_Graph, s_Pin.entity), s_Pin.pin),
                                       {}});
        }
        else if (const fr::KnownExitTemplate* s_Known = fr::FindKnownExitTemplate(s_Exit.templateRid);
                 s_Known && s_Known->anchor == s_Seen.id) {
            s_Exit.pinsFrom = "the template's known sub-entity ids";
            s_Exit.facts.graphRead = true;

            for (const fr::KnownExitPin& s_Pin : s_Known->ends) {
                ZEntityType** s_Entity = s_Lookup(s_Exit, s_Pin.sub);

                if (!s_Entity) {
                    ++s_Exit.facts.skipped;
                    continue;
                }

                s_EndPins.push_back(
                    {s_Entity, std::string(s_Pin.pin), fmt::format("{:016x} {}", s_Pin.sub, s_Pin.pin), {}});
            }

            for (const fr::KnownExitPin& s_Pin : s_Known->starts)
                s_StartPins.push_back({s_Lookup(s_Exit, s_Pin.sub),
                                       std::string(s_Pin.pin),
                                       fmt::format("{:016x} {}", s_Pin.sub, s_Pin.pin),
                                       {}});
        }
        else
            s_Exit.pinsFrom = fmt::format("its wiring could not be read ({})", s_Why);

        if (s_Exit.name.empty())
            s_Exit.name = "Exit";

        s_Exit.facts.id = s_Exit.rootId ? s_Exit.rootId : s_Exit.anchorId;
        s_Exit.facts.ends = s_EndPins.size();
        s_Exit.facts.starts = static_cast<size_t>(std::count_if(s_StartPins.begin(), s_StartPins.end(),
                                                                [](const PinPlan& p) { return p.entity != nullptr; }));
        Describe(s_Exit);
        s_Variant.push_back(std::move(s_Exit));
        s_Ends.push_back(std::move(s_EndPins));
        s_Starts.push_back(std::move(s_StartPins));
    }

    std::vector<fr::FreelancerExitFacts> s_FreelancerFacts;

    for (size_t i = 0; i < s_Freelancer.size(); ++i) {
        Exit& s_Exit = s_Freelancer[i];
        s_Exit.facts.id = s_Exit.rootId;
        s_FreelancerFacts.push_back(
            {s_Exit.rootId, s_Exit.inVariantBrick, s_Confirms[i].size(), !s_Exit.broken.empty(), !s_Exit.offs.empty()});
    }

    std::vector<fr::VariantExitFacts> s_VariantFacts;

    for (const Exit& s_Exit : s_Variant)
        s_VariantFacts.push_back(s_Exit.facts);

    const fr::VariantExitPlan s_Plan = fr::PlanVariantExits(s_FreelancerFacts, s_VariantFacts);

    for (size_t i = 0; i < s_Variant.size(); ++i)
        s_Variant[i].action = s_Plan.actions[i];

    // Arm: Freelancer exits' confirmations (watched), routed variant exits' ends (blocked) and starts (watched).
    size_t s_NoRoom = 0;

    if (m_Route) {
        for (size_t i = 0; i < s_Freelancer.size(); ++i) {
            for (const PinPlan& s_Pin : s_Confirms[i]) {
                if (AddPin(PinKind::FreelancerConfirm, i, s_Pin.entity, s_Pin.pin,
                           fmt::format("{}: {}", Label(s_Freelancer[i], true), s_Pin.label), {}))
                    ++s_Freelancer[i].watched;
                else if (s_Pin.entity)
                    ++s_NoRoom;
            }
        }

        for (size_t i = 0; i < s_Variant.size(); ++i) {
            if (s_Variant[i].action != fr::VariantExitAction::Route)
                continue;

            for (const PinPlan& s_Pin : s_Ends[i]) {
                std::vector<Resend> s_Resend;

                for (const auto& [s_Entity, s_To] : s_Pin.resend)
                    s_Resend.push_back({s_Entity, s_To.first, s_To.second});

                if (AddPin(PinKind::VariantEnd, i, s_Pin.entity, s_Pin.pin,
                           fmt::format("{}: {}", Label(s_Variant[i], false), s_Pin.label), s_Resend))
                    ++s_Variant[i].blocked;
                else
                    ++s_NoRoom;
            }

            for (const PinPlan& s_Pin : s_Starts[i]) {
                if (AddPin(PinKind::VariantStart, i, s_Pin.entity, s_Pin.pin,
                           fmt::format("{}: {}", Label(s_Variant[i], false), s_Pin.label), {}))
                    ++s_Variant[i].watched;
                else if (s_Pin.entity)
                    ++s_NoRoom;
            }
        }
    }

    {
        std::lock_guard s_Lock(m_Mutex);
        m_Freelancer = std::move(s_Freelancer);
        m_Variant = std::move(s_Variant);
        m_Plan = s_Plan;
        m_Clock = {};
        m_Played = 0.0;
        m_EvergreenExit = false;
        m_EndLogged = false;
    }

    if (m_PinCount > 0)
        m_PinsOn.store(true, std::memory_order_release);

    m_EventsWanted.store(true, std::memory_order_relaxed);

    // The log: a summary, then one line per exit.
    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();
    const std::string s_Stats =
        fmt::format("walk {} entities in {} bricks, {} faults{}{}; {} pins armed{}; {:.1f} ms", s_Walk.visited,
                    s_Walk.bricks.size(), s_Walk.faults + s_ReadFaults, s_Walk.capped ? " (capped)" : "",
                    s_OutsideVariant ? fmt::format("; {} contract exits outside the variant's bricks "
                                                   "not counted",
                                                   s_OutsideVariant)
                                     : std::string(),
                    m_PinCount, s_NoRoom ? fmt::format(" ({} could not be)", s_NoRoom) : std::string(), s_Ms);
    const fr::VariantExitPlan& s_Done = m_Plan;
    const std::string s_Via = s_Done.hasVia ? Label(m_Freelancer[s_Done.via], true) : std::string("none");

    if (!m_Route)
        LogTo(fr::LogTopic::MapVariants,
              "variant exits: {}: {} Freelancer exits, {} contract exits in the scene; listed only (a full "
              "version: its package handles exits); {}",
              m_Option, m_Freelancer.size(), m_Variant.size(), s_Stats);
    else if (!s_Done.hasVia && s_Done.kept > 0)
        LogTo(fr::LogTopic::MapVariants,
              "variant exits: {}: NO Freelancer exit found in the scene; the variant's own {} exit{} left as {} (they "
              "end "
              "the mission outside Freelancer's exit flow: no payout, the campaign may not move on, the end screen may "
              "not lead back to the safehouse); {}",
              m_Option, s_Done.kept, s_Done.kept == 1 ? " is" : "s are", s_Done.kept == 1 ? "it is" : "they are",
              s_Stats);
    else if (!s_Done.hasVia)
        LogTo(fr::LogTopic::MapVariants,
              "variant exits: {}: NO Freelancer exit found in the scene, and no exit of the variant's bricks either; "
              "this mission may have no way out; {}",
              m_Option, s_Stats);
    else if (s_Done.kept > 0)
        LogTo(fr::LogTopic::MapVariants,
              "variant exits: {}: {} Freelancer exits; {} of the variant's {} exits routed into Freelancer's exit flow "
              "(through {}), {} LEFT AS {} (they end the mission outside Freelancer's flow; see below); {}",
              m_Option, m_Freelancer.size(), s_Done.routed, s_Done.routed + s_Done.kept, s_Via, s_Done.kept,
              s_Done.kept == 1 ? "IT IS" : "THEY ARE", s_Stats);
    else
        LogTo(fr::LogTopic::MapVariants,
              "variant exits: {}: {} Freelancer exits; {} exit{} of the variant's bricks routed into "
              "Freelancer's exit flow (through {}); {}",
              m_Option, m_Freelancer.size(), s_Done.routed, s_Done.routed == 1 ? "" : "s", s_Via, s_Stats);

    for (const Exit& s_Exit : m_Freelancer) {
        std::string s_State;

        if (!s_Exit.switchOff.empty())
            s_State = s_Exit.offs.empty()
                          ? fmt::format("should be switched off ({}), but cannot be: {}; using it may hang the end",
                                        s_Exit.switchOff, s_Exit.offsFrom)
                          : fmt::format("switched off once the scene plays ({}; {} of its switches found)",
                                        s_Exit.switchOff, s_Exit.offs.size());
        else if (!m_Route)
            s_State = "listed only";
        else if (s_Exit.watched == 0)
            s_State = fmt::format("its confirmation is not watched ({}); if its sequence is gone, it may not end "
                                  "the level",
                                  s_Exit.pinsFrom);
        else if (!s_Exit.broken.empty())
            s_State = fmt::format("its exit sequence is gone under this variant ({}): once the player confirms "
                                  "it, its end is sent {:.0f} s later; {} confirm pin{} watched ({})",
                                  s_Exit.broken, fr::kExitGapSeconds, s_Exit.watched, s_Exit.watched == 1 ? "" : "s",
                                  s_Exit.pinsFrom);
        else
            s_State =
                fmt::format("works as usual; if the level has not ended {:.0f} s after the player confirms it, "
                            "its end is sent; {} confirm pin{} watched ({})",
                            fr::kExitEndWaitSeconds, s_Exit.watched, s_Exit.watched == 1 ? "" : "s", s_Exit.pinsFrom);

        LogTo(fr::LogTopic::MapVariants, "variant exits: {} at {} in {}{}: {}", Label(s_Exit, true), s_Exit.where,
              BrickName(s_Exit.brickRid), s_Exit.inVariantBrick ? " (the variant's brick)" : "", s_State);
    }

    for (const Exit& s_Exit : m_Variant) {
        std::string s_State;

        switch (s_Exit.action) {
            case fr::VariantExitAction::Route:
                s_State = fmt::format(
                    "routed: its own end ({} pin{} into its ZContractsExitEntity Trigger) blocked{}, "
                    "{}; it now ends the mission through {} ({})",
                    s_Exit.blocked, s_Exit.blocked == 1 ? "" : "s", m_Route ? "" : " (not armed: listed only)",
                    s_Exit.watched
                        ? fmt::format("its start watched ({} pin{})", s_Exit.watched, s_Exit.watched == 1 ? "" : "s")
                        : std::string("no start pin (Freelancer's flow starts at its end)"),
                    s_Via, s_Exit.pinsFrom);
                break;
            default: s_State = fmt::format("{}; {}", fr::VariantExitActionText(s_Exit.action), s_Exit.pinsFrom); break;
        }

        LogTo(fr::LogTopic::MapVariants, "variant exits: {} at {} in {}: {}", Label(s_Exit, false), s_Exit.where,
              BrickName(s_Exit.brickRid), s_State);
    }
}

// SetFalse on each switch of an exit the loaded bricks leave without its
// getaway, as the game does once an exit was used (its prompt and icons go;
// its hijacker, and the mission's "Any exit used", are left alone). When a
// pin did not take, m_bValue is set with its change handlers. Game thread.
void VariantExits::SwitchOff() {
    struct ToSwitch {
        std::string label;
        std::string why;
        std::vector<std::pair<ZEntityType**, fr::KnownExitPin>> offs;
    };

    std::vector<ToSwitch> s_Exits;

    {
        std::lock_guard s_Lock(m_Mutex);

        for (const Exit& s_Exit : m_Freelancer) {
            if (!s_Exit.offs.empty())
                s_Exits.push_back({Label(s_Exit, true), s_Exit.switchOff, s_Exit.offs});
        }
    }

    for (const auto& [s_Label, s_Why, s_Offs] : s_Exits) {
        size_t s_Off = 0;
        std::string s_Steps;

        for (const auto& [s_Entity, s_Pin] : s_Offs) {
            const uint32_t s_Code = SendInputPin(s_Entity, PinId(s_Pin.pin));
            std::optional<bool> s_Back = ReadBool(s_Entity);
            std::string s_How = fmt::format("{:016x} {} {}", s_Pin.sub, s_Pin.pin, s_Code ? FaultText(s_Code) : "sent");

            if (s_Back != std::optional<bool>(false)) {
                BoolSetArgs s_Args {s_Entity, false, false};
                const DWORD s_SetCode = SafeSetBool(&s_Args);
                s_How += fmt::format(" (reads {}; SetProperty m_bValue {})", BoolText(s_Back),
                                     s_SetCode   ? FaultText(s_SetCode)
                                     : s_Args.ok ? "set"
                                                 : "refused");
                s_Back = ReadBool(s_Entity);
            }

            s_Off += s_Back == std::optional<bool>(false) ? 1 : 0;
            s_Steps += fmt::format("{}{}, reads {}", s_Steps.empty() ? "" : "; ", s_How, BoolText(s_Back));
        }

        LogTo(fr::LogTopic::MapVariants, "variant exits: {} {}: {} ({} of {} switches read off: {})", s_Label,
              s_Off == s_Offs.size() ? "switched off, the player cannot use it"
                                     : "may NOT be switched off, using it may hang the end",
              s_Why, s_Off, s_Offs.size(), s_Steps);
    }
}

void VariantExits::Send(const fr::ExitSteps& p_Steps, size_t p_Via, const std::string& p_Why) {
    if (!p_Steps.activate && !p_Steps.done)
        return;

    ZEntityType** s_Hijacker = nullptr;
    std::string s_Label;

    {
        std::lock_guard s_Lock(m_Mutex);

        if (p_Via < m_Freelancer.size()) {
            s_Hijacker = m_Freelancer[p_Via].anchor;
            s_Label = Label(m_Freelancer[p_Via], true);
        }
    }

    if (!s_Hijacker) {
        LogTo(fr::LogTopic::MapVariants, "variant exits: {}: no Freelancer exit to send it to", p_Why);
        return;
    }

    if (p_Steps.activate) {
        const uint32_t s_Code = SendInputPin(s_Hijacker, fr::kPinExitActivated);
        LogTo(fr::LogTopic::MapVariants,
              "variant exits: {}: Freelancer's exit flow started (exitActivated sent to {}'s hijacker{})", p_Why,
              s_Label, s_Code ? fmt::format(": {}", FaultText(s_Code)) : std::string());
    }

    if (p_Steps.done) {
        const uint32_t s_Code = SendInputPin(s_Hijacker, fr::kPinSequenceDone);
        const std::string s_Text = fmt::format("variant exits: {}: Freelancer's exit end sent (sequenceDone to {}'s "
                                               "hijacker; {}){}",
                                               p_Why, s_Label, fr::DoneWhyText(p_Steps.why),
                                               s_Code ? fmt::format(": {}", FaultText(s_Code)) : std::string());

        // A wait that ran out is worth seeing whatever the log switches say.
        if (p_Steps.why == fr::DoneWhy::Waited || s_Code)
            Log("{}", s_Text);
        else
            LogTo(fr::LogTopic::MapVariants, "{}", s_Text);
    }
}

bool VariantExits::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept {
    ZEntityType** const s_Entity = p_Entity.m_pObj;

    if (!s_Entity || !m_PinsOn.load(std::memory_order_acquire))
        return false;

    for (size_t i = 0; i < kMaxPins; ++i) {
        ZEntityType** const s_Armed = m_Pins[i].entity.load(std::memory_order_relaxed);

        if (!s_Armed)
            break; // filled in order

        if (s_Armed != s_Entity || m_Pins[i].pin.load(std::memory_order_relaxed) != p_PinId)
            continue;

        WatchPin& s_Pin = m_Pins[i];
        const bool s_Block = s_Pin.kind == PinKind::VariantEnd;

        try {
            const bool s_First = !s_Pin.logged.exchange(true, std::memory_order_relaxed);
            fr::ExitSteps s_Steps;
            size_t s_Via = 0;
            std::string s_What;

            {
                std::lock_guard s_Lock(m_Mutex);

                switch (s_Pin.kind) {
                    case PinKind::FreelancerConfirm: {
                        const bool s_Broken =
                            s_Pin.exit < m_Freelancer.size() && !m_Freelancer[s_Pin.exit].broken.empty();
                        s_Steps = fr::OnFreelancerConfirm(m_Clock, s_Pin.exit, m_Played, s_Broken);
                        s_Via = s_Pin.exit;
                        s_What = s_Steps.ignored ? std::string()
                                 : s_Broken ? fmt::format("its sequence is gone under this variant: its end is sent "
                                                          "in {:.0f} s",
                                                          fr::kExitGapSeconds)
                                            : fmt::format("its own sequence ends the level (its end is sent after "
                                                          "{:.0f} s only if it has not)",
                                                          fr::kExitEndWaitSeconds);
                        break;
                    }
                    case PinKind::VariantStart:
                        s_Steps = fr::OnVariantStart(m_Clock, s_Pin.exit, m_Played);
                        s_Via = m_Plan.via;
                        break;
                    case PinKind::VariantEnd:
                        s_Steps = fr::OnVariantEnd(m_Clock, s_Pin.exit, m_Played);
                        s_Via = m_Plan.via;

                        if (!s_Steps.ignored && !s_Steps.done && m_Clock.doneAt >= 0.0)
                            s_What = fmt::format("its end is sent in {:.1f} s", m_Clock.doneAt - m_Played);
                        break;
                }
            }

            const char* const s_Kind = s_Pin.kind == PinKind::FreelancerConfirm ? "exit used (confirmed)"
                                       : s_Pin.kind == PinKind::VariantStart    ? "exit used (its own sequence starts)"
                                                                                : "its own contract end blocked";

            if (s_Steps.ignored) {
                if (s_First)
                    LogTo(fr::LogTopic::MapVariants,
                          "variant exits: {}: {} (another exit is already in progress, or the level "
                          "ended: nothing more sent{})",
                          s_Pin.label, s_Kind, s_Block ? "; still blocked" : "");
            }
            else
                LogTo(fr::LogTopic::MapVariants, "variant exits: {}: {}{}", s_Pin.label, s_Kind,
                      s_What.empty() ? std::string() : fmt::format("; {}", s_What));

            Send(s_Steps, s_Via, s_Pin.label);
        }
        catch (...) {
        }

        if (!s_Block)
            return false;

        // The pin's other logic still runs: the signal goes on along its other links (the contract end's is dropped).
        briefing_graphs::SendOn(std::span(s_Pin.resend.data(), std::min(s_Pin.resendCount, fr::kLineMaxResend)),
                                s_Entity, p_PinId, p_Data);

        return true;
    }

    return false;
}

void VariantExits::Tick(double p_GameSeconds) noexcept {
    if (!m_EventsWanted.load(std::memory_order_relaxed))
        return;

    Contained(kTag, "frame", [&] {
        fr::ExitSteps s_Steps;
        size_t s_Via = 0;
        std::string s_Why;

        {
            std::lock_guard s_Lock(m_Mutex);
            m_Played += p_GameSeconds;
            s_Steps = fr::OnExitTick(m_Clock, m_Played);

            if (!s_Steps.done)
                return;

            const bool s_Freelancer = m_Clock.source == fr::ExitSource::Freelancer;
            s_Via = s_Freelancer ? m_Clock.exit : m_Plan.via;
            const std::vector<Exit>& s_From = s_Freelancer ? m_Freelancer : m_Variant;
            s_Why = m_Clock.exit < s_From.size()
                        ? fmt::format("{}, {:.1f} s after it started", Label(s_From[m_Clock.exit], s_Freelancer),
                                      m_Played - m_Clock.startedAt)
                        : std::string("an exit");
        }

        Send(s_Steps, s_Via, s_Why);
    });
}

void VariantExits::OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept {
    Contained(kTag, "sent event", [&] {
        std::string s_Json;

        if (ReadDynamicObjectJson(p_Event, &s_Json))
            return;

        const std::string_view s_Text = s_Json;
        const std::string_view s_Name = fr::JsonEventName(s_Text);

        if (s_Name == "exit_gate") {
            static constexpr std::array<std::string_view, 1> kParts {"RepositoryId"};
            const std::vector<fr::JsonScalar> s_Scalars = fr::JsonScalarsWithKeyContaining(s_Text, kParts);
            std::string_view s_Value = s_Scalars.empty() ? std::string_view() : s_Scalars.front().value;

            if (s_Value.size() >= 2 && s_Value.front() == '"' && s_Value.back() == '"')
                s_Value = s_Value.substr(1, s_Value.size() - 2);

            std::string s_Who;
            std::lock_guard s_Lock(m_Mutex);

            for (const Exit& s_Exit : m_Freelancer) {
                if (!s_Exit.repo.empty() && fr::ToLower(s_Exit.repo) == fr::ToLower(s_Value))
                    s_Who += fmt::format("{}{}", s_Who.empty() ? "" : ", ", Label(s_Exit, true));
            }

            for (const Exit& s_Exit : m_Variant) {
                if (!s_Exit.repo.empty() && fr::ToLower(s_Exit.repo) == fr::ToLower(s_Value))
                    s_Who += fmt::format("{}{}", s_Who.empty() ? "" : ", ", Label(s_Exit, false));
            }

            LogTo(fr::LogTopic::MapVariants, "variant exits: exit_gate #{}: the game reports exit {}: {}", p_EventIndex,
                  s_Value.empty() ? std::string_view("<none>") : s_Value,
                  s_Who.empty() ? std::string("none of the exits found has this repository id") : s_Who);
            return;
        }

        if (s_Name == "EvergreenExitTriggered") {
            std::lock_guard s_Lock(m_Mutex);
            m_EvergreenExit = true;
            LogTo(fr::LogTopic::MapVariants, "variant exits: EvergreenExitTriggered #{}: Freelancer's exit flow runs",
                  p_EventIndex);
            return;
        }

        if (!fr::IsLevelEndEvent(s_Name))
            return;

        std::lock_guard s_Lock(m_Mutex);

        if (m_EndLogged)
            return;

        m_EndLogged = true;
        const fr::ExitClock s_Was = m_Clock;
        fr::OnMissionEnd(m_Clock);
        std::string s_Exit = "no exit the fix watched";

        if (s_Was.source == fr::ExitSource::Freelancer && s_Was.exit < m_Freelancer.size())
            s_Exit = Label(m_Freelancer[s_Was.exit], true);
        else if (s_Was.source == fr::ExitSource::Variant && s_Was.exit < m_Variant.size())
            s_Exit = Label(m_Variant[s_Was.exit], false);

        if (m_EvergreenExit)
            LogTo(fr::LogTopic::MapVariants,
                  "variant exits: the level ends ({} #{}) after {}; Freelancer's exit flow ran "
                  "(EvergreenExitTriggered seen)",
                  s_Name, p_EventIndex, s_Exit);
        else
            LogTo(fr::LogTopic::MapVariants,
                  "variant exits: the level ends ({} #{}) after {}; Freelancer's exit flow did NOT run (no "
                  "EvergreenExitTriggered): the mission may not count, and the end screen may not lead back to the "
                  "safehouse",
                  s_Name, p_EventIndex, s_Exit);
    });
}
