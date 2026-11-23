// Sick spot steer (see SickSpotSteer.h). Engine reads and writes only through
// the guarded helpers of EntityWalk.h, EntityReads.h, ActorReads.h and
// BlueprintRead.h, or the guard pattern in Guard.h.

#include "SickSpotSteer.h"

#include "../core/SafeSpots.h" // fr::IsReadPoint
#include "ActorReads.h"
#include "BlueprintRead.h"
#include "EntityWalk.h"
#include "EngineText.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "PinIds.h"
#include "EntityReads.h"
#include "Scene.h"
#include "SceneOf.h"

#include <Globals.h>

#include <Glacier/Reflection.h>
#include <Glacier/ZActor.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZString.h>
#include <Glacier/ZTypeRegistry.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <mutex>
#include <string_view>

namespace {

constexpr const char* kKillGuideClass = "ZContextKillGuide";
constexpr const char* kDramaClass = "ZDramaSituationEntity";
constexpr std::string_view kPriorityProperty = "m_nPriority";

// PointOfInterest in each template: a position when the act and the root have none.
constexpr uint64_t kSickPoiId = 0xfa79722ce51dc727ull;
constexpr uint64_t kSickHighPoiId = 0x9a4b0bb62171aa3full;

// Entities of one spot's tree taken at most (the normal template has 32 sub-entities, its nested
// templates a few more each).
constexpr size_t kMaxSpotEntities = 512;

// The output pins that tell an NPC took a spot: the role's actor, the drama's start and end, the act's.
struct WatchedPin {
    uint32_t id;
    const char* name;
    bool actor; // the payload may carry the actor
};

constexpr std::array kWatchedPins {
    WatchedPin {fr::kPinOnIActorChanged, "OnIActorChanged", true},
    WatchedPin {fr::kPinOnActorCast, "OnActorCast", true},
    WatchedPin {fr::kPinOnCastRole, "OnCastRole", true},
    WatchedPin {fr::kPinOnEnterDrama, "OnEnterDrama", true},
    WatchedPin {fr::kPinOnLeaveDrama, "OnLeaveDrama", true},
    WatchedPin {fr::kPinOnPauseDrama, "OnPauseDrama", false},
    WatchedPin {fr::kPinOnStart, "OnStart", false},
    WatchedPin {fr::kPinOnReached, "OnReached", false},
    WatchedPin {fr::kPinOnActTimeout, "OnActTimeout", false},
    WatchedPin {fr::kPinFXActivatePuke, "FXActivatePuke", false},
    WatchedPin {fr::kPinOnDone, "OnDone", false},
    WatchedPin {fr::kPinOnEnd, "OnEnd", false},
};

const WatchedPin* FindWatchedPin(uint32_t p_Id) {
    for (const WatchedPin& s_Pin : kWatchedPins) {
        if (s_Pin.id == p_Id)
            return &s_Pin;
    }

    return nullptr;
}

constexpr std::string_view kTag = "sick spots";

std::string PointText(const std::optional<fr::SpotPoint>& p_Point) {
    return p_Point ? fmt::format("({:.2f}, {:.2f}, {:.2f})", p_Point->x, p_Point->y, p_Point->z)
                   : std::string("(position unread)");
}

// A world position, the engine refreshing a stale cache first (game thread); (0, 0, 0) is never one.
std::optional<fr::SpotPoint> PointOf(ZEntityType** p_Entity, bool p_Refresh, std::string* p_Why) {
    if (!p_Entity) {
        *p_Why = "no entity";
        return std::nullopt;
    }

    const PositionRead s_Read = ReadPosition(p_Entity, p_Refresh);

    if (!s_Read.at) {
        *p_Why = s_Read.faultCode  ? FaultText(s_Read.faultCode)
                 : !s_Read.spatial ? "not spatial"
                 : s_Read.stale    ? "stale transform"
                                   : "unread";
        return std::nullopt;
    }

    const fr::SpotPoint s_Point {s_Read.at->x, s_Read.at->y, s_Read.at->z};

    if (!fr::IsReadPoint(s_Point)) {
        *p_Why = "at the origin";
        return std::nullopt;
    }

    return s_Point;
}

// An int32 property read raw (empty with *p_Why when missing, behind a getter, another size or a fault).
std::optional<int32_t> ReadInt(ZEntityType** p_Entity, std::string_view p_Name, std::string* p_Why) {
    const RawRead32 s_Read = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);

    switch (s_Read.status) {
        case RawRead32::Status::Ok: break;
        case RawRead32::Status::Missing: *p_Why = "no " + std::string(p_Name); return std::nullopt;
        case RawRead32::Status::NotRaw:
            *p_Why = fmt::format("{} not raw ({} bytes or a getter)", p_Name, s_Read.typeSize);
            return std::nullopt;
        case RawRead32::Status::Fault:
            *p_Why = std::string(p_Name) + " " + FaultText(s_Read.faultCode);
            return std::nullopt;
    }

    int32_t s_Value = 0;
    std::memcpy(&s_Value, &s_Read.bits, sizeof(s_Value));
    return s_Value;
}

std::optional<float> ReadFloat(ZEntityType** p_Entity, std::string_view p_Name) {
    const RawRead32 s_Read = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);

    if (s_Read.status != RawRead32::Status::Ok)
        return std::nullopt;

    float s_Value = 0.0f;
    std::memcpy(&s_Value, &s_Read.bits, sizeof(s_Value));
    return s_Value;
}

// m_nPriority through the engine's property setter (change handlers run).
struct IntSetArgs {
    ZEntityType** entity;
    uint32_t propertyId;
    int32_t value;
    bool ok;
};

__declspec(noinline) void IntSetOf(IntSetArgs* p_Args) {
    ZEntityRef s_Entity(p_Args->entity);
    p_Args->ok = s_Entity.SetProperty(p_Args->propertyId, ZObjectRef::From<int32_t>(p_Args->value), true);
}

__declspec(noinline) DWORD SafeIntSet(IntSetArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        IntSetOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// A pin payload's type name, size and first 8 bytes (an entity reference's entity pointer).
struct PayloadOut {
    char typeName[64];
    uint16_t size;
    bool hasData;
    uint64_t first;
};

__declspec(noinline) void PayloadOf(const ZObjectRef* p_Data, PayloadOut* p_Out) {
    const STypeID* s_Type = p_Data->GetTypeID();

    if (!s_Type)
        return;

    const IType* s_Info = s_Type->GetTypeInfo();

    if (s_Info) {
        if (const char* s_Name = s_Info->pszTypeName) {
            size_t i = 0;

            for (; i + 1 < sizeof(p_Out->typeName) && s_Name[i]; ++i)
                p_Out->typeName[i] = s_Name[i] >= 0x20 && s_Name[i] < 0x7f ? s_Name[i] : '?';

            p_Out->typeName[i] = '\0';
        }

        p_Out->size = s_Info->m_nTypeSize;
    }

    const void* s_Data = p_Data->GetData();

    if (!s_Data || p_Out->size < sizeof(uint64_t))
        return;

    p_Out->hasData = true;
    std::memcpy(&p_Out->first, s_Data, sizeof(p_Out->first));
}

__declspec(noinline) DWORD SafePayload(const ZObjectRef* p_Data, PayloadOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        PayloadOf(p_Data, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

std::string DistanceText(const std::optional<double>& p_Distance) {
    return p_Distance ? fmt::format("{:.1f} m", *p_Distance) : std::string("?");
}

} // namespace

void SickSpotSteer::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    if (!p_Context || p_Stage != ESceneLoadingStage::eLoading_ScenePlaying)
        return;

    Contained(kTag, "scene playing", [&] {
        OnSceneClear();
        const std::string s_Scene = SceneOf(p_Context); // a full variant counts as its contract's scene

        if (!fr::IsFreelancerMission(p_Context->m_SceneInitParameters.m_Type.ToStringView(), s_Scene))
            return;

        std::unique_lock s_Lock(m_Mutex);
        m_Scene = SceneLabelOf(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView());

        // The raises of another scene are forgotten; a restart of this one keeps them.
        if (m_RaisedScene != s_Scene) {
            m_Raised.clear();
            m_RaisedScene = s_Scene;
        }
        m_PlayingAt = std::chrono::steady_clock::now();

        if (*Globals::TypeRegistry)
            m_ActorType = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("ZActor")));

        const auto s_Start = std::chrono::steady_clock::now();
        Find(p_Context);
        Classify();

        fr::SickTally s_Tally;
        s_Tally.guides = m_Guides.size();

        for (const fr::KillGuide& s_Guide : m_Guides)
            s_Tally.guidesCounting += fr::KillGuideCounts(s_Guide.type) ? 1 : 0;

        for (Spot& s_Spot : m_Spots) {
            ++s_Tally.spots;
            s_Tally.normal += s_Spot.kind == fr::SickTemplate::Normal ? 1 : 0;
            s_Tally.high += s_Spot.kind == fr::SickTemplate::High ? 1 : 0;

            switch (s_Spot.verdict.kind) {
                case fr::SickSpotKind::Toilet: ++s_Tally.toilet; break;
                case fr::SickSpotKind::KillGuide: ++s_Tally.killGuide; break;
                case fr::SickSpotKind::Plain: ++s_Tally.plain; break;
                case fr::SickSpotKind::Unknown: ++s_Tally.unknown; break;
            }

            Raise(s_Spot, s_Tally);
        }

        const double s_Ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();
        Report(s_Tally, s_Ms);
        m_PinLines.store(0, std::memory_order_relaxed);
        m_PinsOn.store(!m_Watch.empty(), std::memory_order_release);
    });
}

void SickSpotSteer::OnSceneClear() noexcept {
    m_PinsOn.store(false, std::memory_order_relaxed);
    std::unique_lock s_Lock(m_Mutex);
    m_Scene.clear();
    m_Spots.clear();
    m_Guides.clear();
    m_Watch.clear();
}

// One walk over the loaded bricks: the sick spots (with their trees) and the kill guides.
void SickSpotSteer::Find(ZEntitySceneContext* p_Context) {
    // Every entity of a spot's tree, by the spot it belongs to (a parent is visited before its children).
    std::unordered_map<ZEntityType**, size_t> s_Owner;
    std::vector<ZEntityType**> s_GuideEntities;
    size_t s_Faults = 0;
    size_t s_SpotsCut = 0;

    const auto s_Visitor = [&](const VisitedEntity& p_Visit) -> bool {
        // Inside a spot's tree: its id and class, for the pin watch and the priority write.
        if (p_Visit.parent) {
            if (const auto s_It = s_Owner.find(p_Visit.parent); s_It != s_Owner.end()) {
                const size_t s_SpotIndex = s_It->second;
                Spot& s_Spot = m_Spots[s_SpotIndex];

                if (m_Watch.size() < kMaxSpotEntities * fr::kSickMaxSpots) {
                    s_Owner.emplace(p_Visit.entity, s_SpotIndex);
                    std::string s_Class;
                    uint64_t s_Id = 0;

                    if (ReadEntityClassName(p_Visit.entity, s_Class, &s_Id)) {
                        ++s_Faults;
                        return false;
                    }

                    const bool s_Direct = p_Visit.parent == s_Spot.entity;
                    const bool s_High = s_Spot.kind == fr::SickTemplate::High;
                    const char* s_Part = "";

                    if (s_Direct && s_Id == (s_High ? fr::kSickHighActId : fr::kSickActId)) {
                        s_Spot.act = p_Visit.entity;
                        s_Part = "act";
                    }
                    else if (s_Direct && s_Id == (s_High ? fr::kSickHighSituationId : fr::kSickSituationId)) {
                        s_Spot.situation = p_Visit.entity;
                        s_Part = "drama situation";
                    }
                    else if (s_Direct && s_Id == (s_High ? fr::kSickHighRoleId : fr::kSickRoleId))
                        s_Part = "role";

                    if (s_Class == kDramaClass) {
                        s_Spot.dramaEntities.push_back(p_Visit.entity);

                        if (!*s_Part)
                            s_Part = "drama situation entity";
                    }

                    m_Watch.emplace(p_Visit.entity, Watch {s_SpotIndex, *s_Part ? s_Part : "spot entity"});
                }

                return false;
            }
        }

        EntityClassCheck s_Check {};

        if (CheckEntityClass(p_Visit.entity, kKillGuideClass, &s_Check)) {
            ++s_Faults;
            return false;
        }

        if (s_Check.matches) {
            if (s_GuideEntities.size() < fr::kSickMaxGuides) {
                s_GuideEntities.push_back(p_Visit.entity);
                fr::KillGuide s_Guide;
                s_Guide.id = s_Check.id;
                m_Guides.push_back(s_Guide);
            }

            return true;
        }

        if (!p_Visit.factory)
            return false;

        uint64_t s_Rid = 0;

        if (ReadFactoryRid(p_Visit.factory, &s_Rid)) {
            ++s_Faults;
            return false;
        }

        const fr::SickTemplate s_Kind = fr::SickTemplateOf(s_Rid);

        if (s_Kind == fr::SickTemplate::None)
            return false;

        if (m_Spots.size() >= fr::kSickMaxSpots) {
            ++s_SpotsCut;
            return false;
        }

        Spot s_Spot;
        s_Spot.entity = p_Visit.entity;
        s_Spot.factory = p_Visit.factory;
        s_Spot.brickRid = p_Visit.brickRid;
        s_Spot.blueprintRid = s_Rid;
        s_Spot.kind = s_Kind;
        s_Spot.id = s_Check.id;

        uint64_t s_ParentRid = 0;

        if (p_Visit.parentFactory && !ReadFactoryRid(p_Visit.parentFactory, &s_ParentRid))
            s_Spot.inToiletCore = fr::IsToiletCore(s_ParentRid);

        const size_t s_Index = m_Spots.size();
        m_Spots.push_back(std::move(s_Spot));
        s_Owner.emplace(p_Visit.entity, s_Index);
        m_Watch.emplace(p_Visit.entity, Watch {s_Index, "spot"});
        return true;
    };

    const WalkStats s_Stats = VisitAllEntities(p_Context->m_aLoadedBricks, s_Visitor);

    // Positions (the engine refreshes stale transforms), types and reach of the kill guides.
    for (size_t i = 0; i < m_Guides.size(); ++i) {
        std::string s_Why;
        m_Guides[i].at = PointOf(s_GuideEntities[i], true, &s_Why);
        std::string s_TypeWhy;
        m_Guides[i].type = ReadInt(s_GuideEntities[i], "m_eType", &s_TypeWhy);
        m_Guides[i].npcMaxDistance = ReadFloat(s_GuideEntities[i], "m_fNPCMaxDistance");
    }

    // Each spot's name and position: where the NPC throws up (the act), else the spot's root, else its point of
    // interest.
    for (Spot& s_Spot : m_Spots) {
        std::string s_NameWhy;
        const std::optional<OwnedSubEntity> s_Owned =
            s_Spot.id ? ReadOwnedSubEntity(s_Spot.entity, s_Spot.id, &s_NameWhy) : std::nullopt;
        s_Spot.name = s_Owned && !s_Owned->name.empty()
                          ? s_Owned->name
                          : std::string(s_Spot.kind == fr::SickTemplate::High ? "SetPiece_Trap_Poison_SickSpot_High"
                                                                              : "SetPiece_Trap_Poison_SickSpot");

        std::string s_Why;
        std::string s_Whys;

        if ((s_Spot.at = PointOf(s_Spot.act, true, &s_Why))) {
            s_Spot.atFrom = "act";
            continue;
        }

        s_Whys = "act: " + s_Why;

        if ((s_Spot.at = PointOf(s_Spot.entity, true, &s_Why))) {
            s_Spot.atFrom = "spot";
            continue;
        }

        s_Whys += ", spot: " + s_Why;
        const uint64_t s_PoiId = s_Spot.kind == fr::SickTemplate::High ? kSickHighPoiId : kSickPoiId;
        const SubEntityLookup s_Poi = LookUpSubEntity(s_Spot.entity, s_Spot.factory, s_PoiId);

        if ((s_Spot.at = PointOf(s_Poi.entity, true, &s_Why))) {
            s_Spot.atFrom = "point of interest";
            continue;
        }

        s_Spot.atFrom = s_Whys + ", point of interest: " + s_Why;
    }

    if (s_Faults || s_Stats.capped || s_SpotsCut)
        LogTo(fr::LogTopic::AiDifficulty, "sick spots: {}: the walk had {} read faults{}{}", m_Scene, s_Faults,
              s_Stats.capped ? "; it stopped at its entity cap, so spots may be missing" : "",
              s_SpotsCut ? fmt::format("; {} spots past the first {} left alone", s_SpotsCut, fr::kSickMaxSpots)
                         : std::string());
}

void SickSpotSteer::Classify() {
    for (Spot& s_Spot : m_Spots)
        s_Spot.verdict = fr::ClassifySickSpot(s_Spot.inToiletCore, s_Spot.at, m_Guides);
}

// A normal kill spot's drama situation priority raised by the boost, read back. Plain spots and bins only read.
void SickSpotSteer::Raise(Spot& p_Spot, fr::SickTally& p_Tally) {
    const bool s_Kill = fr::IsKillSpot(p_Spot.verdict.kind);

    if (s_Kill && p_Spot.kind == fr::SickTemplate::High)
        ++p_Tally.highKill;

    // Where m_nPriority lives at run time: the ZDramaSituationEntity in the spot's tree; else the spot's
    // DramaSituation_SickSpot, else the spot itself (the template's alias), whichever has it.
    std::vector<ZEntityType**> s_Targets = p_Spot.dramaEntities;

    if (s_Targets.empty()) {
        for (ZEntityType** s_Candidate : {p_Spot.situation, p_Spot.entity}) {
            std::string s_Why;

            if (s_Candidate && ReadInt(s_Candidate, kPriorityProperty, &s_Why)) {
                s_Targets.push_back(s_Candidate);
                break;
            }
        }
    }

    if (s_Targets.empty()) {
        p_Spot.priority = "priority unread (no m_nPriority found in the spot)";

        if (s_Kill && p_Spot.kind == fr::SickTemplate::Normal && m_On && m_Boost > 0)
            ++p_Tally.notRaised;

        return;
    }

    const bool s_Write = s_Kill && p_Spot.kind == fr::SickTemplate::Normal && m_On && m_Boost > 0;
    std::string s_Text;
    bool s_AllTook = true;

    for (ZEntityType** s_Target : s_Targets) {
        std::string s_Why;
        const std::optional<int32_t> s_Before = ReadInt(s_Target, kPriorityProperty, &s_Why);
        std::string s_One;

        if (!s_Before) {
            s_One = "unread (" + s_Why + ")";
            s_AllTook = false;
        }
        else if (!s_Write) {
            s_One = fmt::format("{}", *s_Before);
            const auto s_Earlier = m_Raised.find(s_Target);

            // Raised at an earlier load of this scene, the boost off now: written back.
            if (s_Earlier != m_Raised.end()) {
                if (const std::optional<int32_t> s_Original = fr::SickPriorityToRestore(*s_Before, s_Earlier->second)) {
                    IntSetArgs s_Args {s_Target, fr::kPropPriority, *s_Original, false};
                    const DWORD s_Code = SafeIntSet(&s_Args);
                    std::string s_BackWhy;
                    const std::optional<int32_t> s_Back = ReadInt(s_Target, kPriorityProperty, &s_BackWhy);
                    const bool s_Took = !s_Code && s_Back && *s_Back == *s_Original;
                    s_One += s_Back ? fmt::format(" -> {} (boost undone{})", *s_Back, s_Took ? "" : ", not taken")
                                    : fmt::format(" -> unread ({})", s_BackWhy);

                    // Kept while the boosted value stays, so a later load can try again.
                    if (s_Took)
                        m_Raised.erase(s_Earlier);
                }
                else {
                    m_Raised.erase(s_Earlier);
                }
            }
        }
        else {
            const auto s_Earlier = m_Raised.find(s_Target);
            const int32_t s_Target32 = fr::SickPriorityToWrite(
                *s_Before, s_Earlier != m_Raised.end() ? std::optional(s_Earlier->second) : std::nullopt, m_Boost);
            IntSetArgs s_Args {s_Target, fr::kPropPriority, s_Target32, false};
            const DWORD s_Code = SafeIntSet(&s_Args);
            std::string s_BackWhy;
            const std::optional<int32_t> s_Back = ReadInt(s_Target, kPriorityProperty, &s_BackWhy);
            const bool s_Took = !s_Code && s_Back && *s_Back == s_Target32;
            s_AllTook = s_AllTook && s_Took;

            if (s_Took) {
                const int32_t s_Original = s_Earlier != m_Raised.end() && s_Earlier->second.written == *s_Before
                                               ? s_Earlier->second.original
                                               : *s_Before;
                m_Raised[s_Target] = {s_Original, s_Target32};
            }

            s_One = fmt::format("{} -> {}", *s_Before,
                                s_Back ? fmt::format("{}", *s_Back) : fmt::format("unread ({})", s_BackWhy));

            if (s_Code)
                s_One += fmt::format(" (SetProperty {})", FaultText(s_Code));
            else if (!s_Args.ok)
                s_One += " (SetProperty returned false)";
            else if (!s_Took)
                s_One += fmt::format(" (wanted {})", s_Target32);
        }

        s_Text += (s_Text.empty() ? "" : ", ") + s_One;
    }

    if (s_Write) {
        if (s_AllTook)
            ++p_Tally.raised;
        else
            ++p_Tally.notRaised;
    }

    p_Spot.priority =
        fmt::format("priority {}{}", s_Text,
                    s_Targets.size() > 1 ? fmt::format(" ({} drama entities)", s_Targets.size()) : std::string());
}

std::string SickSpotSteer::SpotLabel(size_t p_Index) const {
    const Spot& s_Spot = m_Spots[p_Index];
    return fmt::format("spot {}/{} \"{}\"", p_Index + 1, m_Spots.size(), s_Spot.name);
}

void SickSpotSteer::Report(const fr::SickTally& p_Tally, double p_WalkMs) {
    LogTo(fr::LogTopic::AiDifficulty, "sick spots: {}: {} (found in {:.0f} ms; kill guide radius {:.1f} m)", m_Scene,
          fr::SickSummaryText(p_Tally, m_Boost, m_On), p_WalkMs, fr::kSickKillGuideRadius);

    for (size_t i = 0; i < m_Spots.size(); ++i) {
        const Spot& s_Spot = m_Spots[i];
        std::string s_Guide = "no kill guide with a position";

        if (s_Spot.verdict.nearest) {
            const fr::KillGuide& g = m_Guides[*s_Spot.verdict.nearest];
            s_Guide = fmt::format("nearest kill guide {:016x} ({}, reach {}) {:.2f} m away", g.id,
                                  fr::KillGuideTypeName(g.type),
                                  g.npcMaxDistance ? fmt::format("{:.2f} m", *g.npcMaxDistance) : std::string("?"),
                                  *s_Spot.verdict.distance);
        }

        LogTo(fr::LogTopic::AiDifficulty,
              "sick spots: {}: {} ({} template, blueprint {:016X}, entity {:016x} in brick "
              "{:016X}{}) at {}{}: {}; {}; {}",
              m_Scene, SpotLabel(i), fr::SickTemplateName(s_Spot.kind), s_Spot.blueprintRid, s_Spot.id, s_Spot.brickRid,
              s_Spot.inToiletCore ? ", in a toilet core" : "", PointText(s_Spot.at), " (" + s_Spot.atFrom + ")",
              fr::SickSpotKindName(s_Spot.verdict.kind), s_Guide, s_Spot.priority);
    }
}

void SickSpotSteer::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept {
    const WatchedPin* s_Pin = FindWatchedPin(p_PinId);

    if (!s_Pin)
        return;

    Contained(kTag, "output pin", [&] {
        std::shared_lock s_Lock(m_Mutex);
        const auto s_It = m_Watch.find(p_Entity.m_pObj);

        if (s_It == m_Watch.end())
            return;

        if (m_PinLines.fetch_add(1, std::memory_order_relaxed) >= fr::kSickMaxPinLines)
            return;

        const Watch s_Watch = s_It->second;
        const Spot& s_Spot = m_Spots[s_Watch.spot];
        const double s_Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_PlayingAt).count();
        std::string s_Actor;

        if (s_Pin->actor) {
            PayloadOut s_Payload {};

            if (const DWORD s_Code = SafePayload(&p_Data, &s_Payload))
                s_Actor = "; payload " + FaultText(s_Code);
            else if (!s_Payload.hasData)
                s_Actor = s_Payload.typeName[0] ? fmt::format("; payload {} (no actor)", s_Payload.typeName) : "";
            else if (!std::strstr(s_Payload.typeName, "Entity") && !std::strstr(s_Payload.typeName, "Actor"))
                s_Actor = fmt::format("; payload {} (not an entity)", s_Payload.typeName);
            else {
                auto** s_ActorEntity = reinterpret_cast<ZEntityType**>(s_Payload.first);
                void* s_Interface = nullptr;
                std::string s_Name = "?";

                if (s_ActorEntity && m_ActorType && !SafeInterface(s_ActorEntity, m_ActorType, &s_Interface) &&
                    s_Interface)
                    s_Name = ReadActor(static_cast<const ZActor*>(s_Interface)).name;

                const std::optional<WorldPosition> s_Where =
                    s_ActorEntity ? ReadWorldPosition(s_ActorEntity) : std::nullopt;

                if (!s_ActorEntity)
                    s_Actor = fmt::format("; payload {}: no actor", s_Payload.typeName);
                else if (!s_Where)
                    s_Actor = fmt::format("; actor {} (position unread)", s_Name);
                else {
                    const fr::SpotPoint s_At {s_Where->x, s_Where->y, s_Where->z};
                    std::vector<fr::SickSpotPlace> s_Places;
                    s_Places.reserve(m_Spots.size());

                    for (const Spot& s_Other : m_Spots)
                        s_Places.push_back({s_Other.at, s_Other.verdict.kind});

                    const fr::SickNearest s_Near = fr::NearestSickSpots(s_Places, s_At);
                    const std::optional<double> s_ToThis =
                        s_Spot.at ? std::optional<double>(fr::SpotDistance(*s_Spot.at, s_At)) : std::nullopt;

                    s_Actor = fmt::format("; actor {} at {}: {} to this spot, nearest kill spot {} ({}), nearest "
                                          "plain spot {} ({})",
                                          s_Name, PointText(s_At), DistanceText(s_ToThis),
                                          s_Near.kill ? SpotLabel(*s_Near.kill) : std::string("none"),
                                          DistanceText(s_Near.killDistance),
                                          s_Near.plain ? SpotLabel(*s_Near.plain) : std::string("none"),
                                          DistanceText(s_Near.plainDistance));
                }
            }
        }

        LogTo(fr::LogTopic::AiDifficulty, "sick spots: {}: {} {} {} {:.0f} s after the level started ({}, {}{}){}",
              m_Scene, SpotLabel(s_Watch.spot), s_Watch.part, s_Pin->name, s_Seconds,
              fr::SickSpotKindName(s_Spot.verdict.kind),
              s_Spot.verdict.distance ? fmt::format("nearest kill guide {:.2f} m", *s_Spot.verdict.distance)
                                      : std::string("no kill guide"),
              s_Spot.priority.empty() ? "" : ", " + s_Spot.priority, s_Actor);
    });
}
