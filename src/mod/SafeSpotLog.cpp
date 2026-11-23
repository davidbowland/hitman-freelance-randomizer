// Safe spot log (see SafeSpotLog.h). Engine reads only through the
// guarded helpers of EntityWalk.h and EntityReads.h.

#include "SafeSpotLog.h"

#include "../core/LootMerces.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "EntityReads.h"
#include "Scene.h"
#include "SceneOf.h"

#include <Glacier/EntityFactory.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <optional>
#include <span>

namespace {

// More sub-entities than this in one brick is a bad read: the brick is skipped.
constexpr int64_t kMaxBrickSubEntities = 2'000'000;
// Each spot's member search when a value is not a direct sub-entity of the template.
constexpr size_t kMemberWalkCap = 5'000;
// ZEntityBlueprintFactoryBase::m_rootEntityIndex (int32; the SDK header
// asserts this offset). A constant, since offsetof on a class with virtual
// functions is not standard.
constexpr uintptr_t kRootEntityIndexOffset = 0x20;

constexpr std::string_view kTag = "safe spots";

// A position: the entity's own world transform, or, for a template instance
// that is not spatial itself, its root entity's. Never a stale cache or (0, 0, 0).
struct Placed {
    std::optional<fr::SpotPoint> at;
    bool spatial = false;   // it, or its root entity, has the spatial interface
    bool refreshed = false; // the engine refreshed a stale cache for it
    bool fromRoot = false;
    const char* why = nullptr; // when `at` is empty
};

// Why a spatial entity's read gave no usable position.
const char* UnreadWhy(const PositionRead& p_Read, bool p_Refresh) {
    if (p_Read.faultCode)
        return "fault";

    if (p_Read.stale)
        return p_Refresh ? "world transform still stale after the engine refresh" : "world transform not computed yet";

    return "at the origin";
}

// One entity's read into p_Out; true when it gave a usable position.
bool Take(const PositionRead& p_Read, Placed& p_Out) {
    p_Out.refreshed = p_Out.refreshed || p_Read.refreshed;

    if (!p_Read.at)
        return false;

    const fr::SpotPoint s_At {p_Read.at->x, p_Read.at->y, p_Read.at->z};

    if (!fr::IsReadPoint(s_At))
        return false;

    p_Out.at = s_At;
    return true;
}

// p_Refresh: a stale cache is refreshed by the engine first (in play only;
// see ReadPosition).
Placed PlacedOf(ZEntityType** p_Entity, ZEntityBlueprintFactoryBase* p_Factory, bool p_Refresh) {
    Placed s_Out;
    const PositionRead s_Own = ReadPosition(p_Entity, p_Refresh);
    s_Out.spatial = s_Own.spatial;

    if (Take(s_Own, s_Out))
        return s_Out;

    if (s_Own.spatial) {
        s_Out.why = UnreadWhy(s_Own, p_Refresh);
        return s_Out;
    }

    s_Out.why = s_Own.faultCode ? "fault" : "not spatial";

    if (!p_Factory)
        return s_Out;

    int32_t s_RootIndex = -1;

    if (ReadEngineBytes(reinterpret_cast<uintptr_t>(p_Factory) + kRootEntityIndexOffset, &s_RootIndex,
                        sizeof(s_RootIndex))) {
        s_Out.why = "not spatial, root index unreadable";
        return s_Out;
    }

    int64_t s_Count = 0;

    if (s_RootIndex < 0 || ReadSubEntityCount(p_Factory, &s_Count) || s_RootIndex >= s_Count) {
        s_Out.why = "not spatial, no root entity";
        return s_Out;
    }

    ZEntityType** s_Root = nullptr;
    ZEntityBlueprintFactoryBase* s_RootFactory = nullptr;

    if (ReadSubEntityAt(p_Factory, p_Entity, s_RootIndex, &s_Root, &s_RootFactory) || !s_Root) {
        s_Out.why = "not spatial, root entity unreadable";
        return s_Out;
    }

    const PositionRead s_RootRead = ReadPosition(s_Root, p_Refresh);
    s_Out.spatial = s_RootRead.spatial;

    if (Take(s_RootRead, s_Out)) {
        s_Out.fromRoot = true;
        s_Out.why = nullptr;
        return s_Out;
    }

    if (!s_RootRead.spatial) {
        s_Out.why = "neither it nor its root entity is spatial";
        return s_Out;
    }

    s_Out.why = s_RootRead.faultCode ? "root entity's position faulted"
                : s_RootRead.stale   ? (p_Refresh ? "root entity's world transform still stale after the engine refresh"
                                                  : "root entity's world transform not computed yet")
                                     : "root entity at the origin";
    return s_Out;
}

std::optional<int32_t> ReadInt(ZEntityType** p_Entity, std::string_view p_Name) {
    if (!p_Entity)
        return std::nullopt;

    const RawRead32 s_Read = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);

    if (s_Read.status != RawRead32::Status::Ok)
        return std::nullopt;

    return static_cast<int32_t>(s_Read.bits);
}

// The loaded brick's root entity and blueprint factory, or why not.
const char* BrickRoot(const SBrickAllocationInfo& p_Brick, ZEntityType*** p_Root,
                      ZEntityBlueprintFactoryBase** p_Factory, int64_t* p_Count) {
    *p_Root = p_Brick.m_EntityRef.m_pObj;
    *p_Factory = nullptr;
    *p_Count = 0;

    if (!*p_Root)
        return "no root entity";

    if (ReadOwnBlueprintFactory(*p_Root, p_Factory))
        return "blueprint factory faulted";

    if (!*p_Factory)
        return "no blueprint factory";

    if (ReadSubEntityCount(*p_Factory, p_Count))
        return "sub-entity count faulted";

    if (*p_Count < 0 || *p_Count > kMaxBrickSubEntities)
        return "implausible sub-entity count";

    return nullptr;
}

double MsSince(std::chrono::steady_clock::time_point p_Start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_Start).count();
}

} // namespace

void SafeSpotLog::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                 const AppliedVariant& p_Variant) noexcept {
    if (!m_On || !p_Context)
        return;

    switch (p_Stage) {
        case ESceneLoadingStage::eLoading_AssetsLoaded: OnSceneClear(); return;
        case ESceneLoadingStage::eLoading_SceneStarted:
            Contained(kTag, "scene start", [&] {
                OnSceneClear();
                const std::string s_Scene = SceneOf(p_Context); // a full variant counts as its contract's scene

                if (!fr::IsFreelancerMission(p_Context->m_SceneInitParameters.m_Type.ToStringView(), s_Scene))
                    return;

                m_Scene = s_Scene;
                m_Variant = p_Variant.scene == s_Scene ? p_Variant : AppliedVariant {};
                Find(p_Context);
                Read(false);
                Report("scene start", true);
                m_Armed = !m_Spots.empty();

                if (m_Armed && !m_Variant.scene.empty())
                    LogTo(fr::LogTopic::SafeSpots,
                          "safe spots: map variant {}{} on this load; its entities near each spot are listed after "
                          "{:.0f} s of play",
                          m_Variant.option, m_Variant.full ? " (full version)" : "", fr::kSafeCheckSeconds);
            });
            return;
        case ESceneLoadingStage::eLoading_ScenePlaying:
            if (!m_Armed)
                return;

            Contained(kTag, "scene playing", [&] {
                m_Playing = true;
                m_Play = 0.0;
                Read(false);
                Report("scene playing", false);
            });
            return;
        default: return;
    }
}

void SafeSpotLog::Tick(double p_GameSeconds) noexcept {
    if (!m_On || !m_Armed || !m_Playing)
        return;

    m_Play += p_GameSeconds;

    if (!m_Checked && m_Play >= fr::kSafeCheckSeconds) {
        m_Checked = true;
        Contained(kTag, "play check", [&] {
            const std::string s_Moment = fmt::format("{:.0f} s of play", fr::kSafeCheckSeconds);
            Read(true);
            Report(s_Moment.c_str(), true);
            Near(s_Moment.c_str());

            // Nothing picked yet: once more later.
            m_LateChecked = std::any_of(m_Spots.begin(), m_Spots.end(), [](const Spot& p_Spot) {
                return fr::ActivityOf(p_Spot.read.signals) == fr::SafeActivity::Active;
            });
        });
    }

    if (m_Checked && !m_LateChecked && m_Play >= fr::kSafeLateCheckSeconds) {
        m_LateChecked = true;
        Contained(kTag, "late check", [&] {
            const std::string s_Moment = fmt::format("{:.0f} s of play", fr::kSafeLateCheckSeconds);
            Read(true);
            Report(s_Moment.c_str(), true);
        });
    }
}

void SafeSpotLog::OnSceneClear() noexcept {
    m_Armed = false;
    m_Scene.clear();
    m_Spots.clear();
    m_Variant = {};
    m_Playing = false;
    m_Play = 0.0;
    m_Checked = false;
    m_LateChecked = false;
}

// One pass over each loaded brick's own sub-entities: the safe template's instances.
void SafeSpotLog::Find(ZEntitySceneContext* p_Context) {
    const auto s_Start = std::chrono::steady_clock::now();
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    size_t s_Looked = 0;
    size_t s_Faults = 0;
    std::string s_Skipped;
    std::string s_Class;

    for (size_t b = 0; b < s_Bricks.size(); ++b) {
        const SBrickAllocationInfo& s_Brick = s_Bricks[b];
        ZEntityType** s_Root = nullptr;
        ZEntityBlueprintFactoryBase* s_Factory = nullptr;
        int64_t s_Count = 0;

        if (const char* s_Why = BrickRoot(s_Brick, &s_Root, &s_Factory, &s_Count)) {
            s_Skipped += fmt::format("{}{:016X} ({})", s_Skipped.empty() ? "" : ", ",
                                     s_Brick.m_RuntimeResourceID.GetID(), s_Why);
            continue;
        }

        for (int i = 0; i < s_Count; ++i) {
            ZEntityType** s_Sub = nullptr;
            ZEntityBlueprintFactoryBase* s_SubFactory = nullptr;
            uint64_t s_Rid = 0;
            ++s_Looked;

            if (ReadSubEntityAt(s_Factory, s_Root, i, &s_Sub, &s_SubFactory) || ReadFactoryRid(s_SubFactory, &s_Rid)) {
                ++s_Faults;
                continue;
            }

            const fr::LootTemplate* s_Template = fr::FindLootTemplate(s_Rid);

            if (!s_Sub || !s_Template || s_Template->kind != fr::LootKind::Safe)
                continue;

            Spot s_Spot;
            s_Spot.entity = s_Sub;
            s_Spot.factory = s_SubFactory;
            s_Spot.read.brickRid = s_Brick.m_RuntimeResourceID.GetID();

            if (ReadEntityClassName(s_Sub, s_Class, &s_Spot.read.id))
                ++s_Faults;

            m_Spots.push_back(std::move(s_Spot));
        }
    }

    size_t s_Members = 0;

    for (Spot& s_Spot : m_Spots) {
        FindMembers(s_Spot);
        s_Members += (s_Spot.route ? 1 : 0) + (s_Spot.state ? 1 : 0) + (s_Spot.count ? 1 : 0);
    }

    LogTo(fr::LogTopic::SafeSpots,
          "safe spots: {}: {} safe spots in {} loaded bricks ({} brick entities looked at{}{}), {} of {} values found, "
          "in {:.1f} ms",
          m_Scene, m_Spots.size(), s_Bricks.size(), s_Looked, s_Faults ? fmt::format(", {} faults", s_Faults) : "",
          s_Skipped.empty() ? "" : fmt::format("; bricks skipped: {}", s_Skipped), s_Members, m_Spots.size() * 3,
          MsSince(s_Start));
}

// The values that tell a picked spot: direct sub-entities of the template, or
// found by a bounded walk of the instance.
void SafeSpotLog::FindMembers(Spot& p_Spot) {
    const auto s_LookUp = [&](uint64_t p_Id) { return LookUpSubEntity(p_Spot.entity, p_Spot.factory, p_Id).entity; };
    p_Spot.route = s_LookUp(fr::kSafeRouteId);
    p_Spot.state = s_LookUp(fr::kSafeStateId);
    p_Spot.count = s_LookUp(fr::kSafeCountId);

    if (p_Spot.route && p_Spot.state && p_Spot.count)
        return;

    std::string s_Class;
    VisitEntityTree(
        p_Spot.entity, p_Spot.read.id,
        [&](const VisitedEntity& p_Visit) {
            uint64_t s_Id = 0;

            if (ReadEntityClassName(p_Visit.entity, s_Class, &s_Id))
                return false;

            ZEntityType*** s_Slot = s_Id == fr::kSafeRouteId   ? &p_Spot.route
                                    : s_Id == fr::kSafeStateId ? &p_Spot.state
                                    : s_Id == fr::kSafeCountId ? &p_Spot.count
                                                               : nullptr;

            if (!s_Slot || *s_Slot)
                return false;

            *s_Slot = p_Visit.entity;
            return true;
        },
        kMemberWalkCap);
}

void SafeSpotLog::Read(bool p_Refresh) {
    for (Spot& s_Spot : m_Spots) {
        const Placed s_Placed = PlacedOf(s_Spot.entity, s_Spot.factory, p_Refresh);
        s_Spot.read.at = s_Placed.at;
        s_Spot.read.noPosition = s_Placed.why ? s_Placed.why : "";
        s_Spot.read.signals.routeOuts = ReadInt(s_Spot.route, "m_nMaxOutPinNum");
        s_Spot.read.signals.state = ReadInt(s_Spot.state, "m_nValue");
        s_Spot.read.signals.count = ReadInt(s_Spot.count, "m_nValue");
    }
}

void SafeSpotLog::Report(const char* p_Moment, bool p_Spots) {
    std::vector<fr::SafeSpot> s_Reads;

    for (const Spot& s_Spot : m_Spots)
        s_Reads.push_back(s_Spot.read);

    if (p_Spots) {
        for (size_t i = 0; i < s_Reads.size() && i < fr::kSafeMaxSpots; ++i)
            LogTo(fr::LogTopic::SafeSpots, "safe spots: {}: {}", p_Moment,
                  fr::SafeSpotText(i, s_Reads.size(), s_Reads[i]));
    }

    LogTo(fr::LogTopic::SafeSpots, "safe spots: {} at {}: {}", m_Scene, p_Moment, fr::SafeSummaryText(s_Reads));
}

// The applied variant's placed entities (its bricks' own sub-entities): see SafeSpotLog.h.
std::vector<fr::PlacedPiece> CollectVariantPieces(const AppliedVariant& p_Variant, std::string* p_Summary) {
    std::vector<fr::PlacedPiece> s_Pieces;
    p_Summary->clear();

    if (p_Variant.scene.empty() || p_Variant.full) {
        *p_Summary = p_Variant.scene.empty() ? "no map variant on this load" : "a full version (no variant bricks)";
        return s_Pieces;
    }

    const ZEntitySceneContext* s_Context = CurrentSceneContext();

    if (!s_Context) {
        *p_Summary = "no scene context";
        return s_Pieces;
    }

    const auto s_Start = std::chrono::steady_clock::now();
    const auto& s_Bricks = s_Context->m_aLoadedBricks;
    size_t s_VariantBricks = 0;
    size_t s_Looked = 0;
    size_t s_NotSpatial = 0;
    size_t s_Unread = 0;    // spatial, but no usable position
    size_t s_Refreshed = 0; // placed after the engine refreshed a stale cache
    size_t s_FromRoot = 0;
    size_t s_Faults = 0;
    bool s_Capped = false;
    std::string s_Skipped;
    std::string s_Class;

    for (size_t b = 0; b < s_Bricks.size() && !s_Capped; ++b) {
        const SBrickAllocationInfo& s_Brick = s_Bricks[b];
        const uint64_t s_BrickRid = s_Brick.m_RuntimeResourceID.GetID();

        if (std::find(p_Variant.rids.begin(), p_Variant.rids.end(), s_BrickRid) == p_Variant.rids.end())
            continue;

        ZEntityType** s_Root = nullptr;
        ZEntityBlueprintFactoryBase* s_Factory = nullptr;
        int64_t s_Count = 0;
        ++s_VariantBricks;

        if (const char* s_Why = BrickRoot(s_Brick, &s_Root, &s_Factory, &s_Count)) {
            s_Skipped += fmt::format("; variant brick {:016X} skipped: {}", s_BrickRid, s_Why);
            continue;
        }

        for (int i = 0; i < s_Count; ++i) {
            if (s_Pieces.size() >= fr::kSafeMaxPieces) {
                s_Capped = true;
                break;
            }

            ZEntityType** s_Sub = nullptr;
            ZEntityBlueprintFactoryBase* s_SubFactory = nullptr;
            ++s_Looked;

            if (ReadSubEntityAt(s_Factory, s_Root, i, &s_Sub, &s_SubFactory) || !s_Sub) {
                ++s_Faults;
                continue;
            }

            const Placed s_Placed = PlacedOf(s_Sub, s_SubFactory, true);

            if (!s_Placed.at) {
                ++(s_Placed.spatial ? s_Unread : s_NotSpatial);
                continue;
            }

            s_Refreshed += s_Placed.refreshed ? 1 : 0;
            s_FromRoot += s_Placed.fromRoot ? 1 : 0;
            fr::PlacedPiece s_Piece;
            s_Piece.at = *s_Placed.at;

            if (ReadEntityClassName(s_Sub, s_Class, &s_Piece.id))
                s_Class = "<class unread>";

            uint64_t s_Rid = 0;
            ReadFactoryRid(s_SubFactory, &s_Rid);
            s_Piece.what = s_Rid
                               ? fmt::format("{} (blueprint {:016X})", s_Class.empty() ? "<no class>" : s_Class, s_Rid)
                               : (s_Class.empty() ? std::string("<no class>") : s_Class);
            s_Pieces.push_back(std::move(s_Piece));
        }
    }

    *p_Summary = fmt::format("{} of {} bricks loaded, {} entities looked at, {} placed ({} by their template's root "
                             "entity, {} refreshed through the engine), {} position unread (not compared), {} not "
                             "spatial{}{}, in {:.1f} ms{}",
                             s_VariantBricks, p_Variant.rids.size(), s_Looked, s_Pieces.size(), s_FromRoot, s_Refreshed,
                             s_Unread, s_NotSpatial, s_Faults ? fmt::format(", {} faults", s_Faults) : "",
                             s_Capped ? fmt::format("; stopped at {} placed", fr::kSafeMaxPieces) : "",
                             MsSince(s_Start), s_Skipped);
    return s_Pieces;
}

// The applied variant's placed entities (its bricks' own sub-entities) near each spot.
void SafeSpotLog::Near(const char* p_Moment) {
    if (m_Variant.scene.empty()) {
        LogTo(fr::LogTopic::SafeSpots, "safe spots: {}: no map variant on this load; nothing to compare", p_Moment);
        return;
    }

    if (m_Variant.full) {
        LogTo(fr::LogTopic::SafeSpots,
              "safe spots: {}: {} is a full version (the whole scene is the variant's); no variant bricks to compare",
              p_Moment, m_Variant.option);
        return;
    }

    if (!CurrentSceneContext())
        return;

    std::string s_Summary;
    const std::vector<fr::PlacedPiece> s_Pieces = CollectVariantPieces(m_Variant, &s_Summary);
    LogTo(fr::LogTopic::SafeSpots, "safe spots: {}: {}: {}", p_Moment, m_Variant.option, s_Summary);

    for (size_t i = 0; i < m_Spots.size() && i < fr::kSafeMaxSpots; ++i) {
        const fr::SafeSpot& s_Read = m_Spots[i].read;
        const std::string s_Head = fmt::format("near spot {}/{} ({}, {})", i + 1, m_Spots.size(), fr::HexId(s_Read.id),
                                               fr::ActivityName(fr::ActivityOf(s_Read.signals)));

        if (!s_Read.at)
            LogTo(fr::LogTopic::SafeSpots, "safe spots: {}: {}: position unread ({}); not compared", p_Moment, s_Head,
                  s_Read.noPosition.empty() ? "?" : s_Read.noPosition);
        else
            LogTo(fr::LogTopic::SafeSpots, "safe spots: {}: {}: {}", p_Moment, s_Head,
                  fr::SafeNearText(*s_Read.at, m_Variant.option, s_Pieces));
    }
}
