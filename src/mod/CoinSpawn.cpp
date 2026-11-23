// Safehouse coin (see CoinSpawn.h). Every raw engine read
// and write follows the guard pattern in Guard.h.

#include "CoinSpawn.h"

#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "PinIds.h"
#include "EntityReads.h"
#include "Scene.h"
#include "SceneOf.h"

#include <Glacier/Hash.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <span>
#include <vector>

namespace {

// "Out1".."Out16" -> 1..16, else 0.
int OutNumber(uint32_t p_PinId) {
    static const std::array<uint32_t, fr::kCoinMaxWatchedOut> s_Ids = [] {
        std::array<uint32_t, fr::kCoinMaxWatchedOut> s_Out {};

        for (int i = 0; i < fr::kCoinMaxWatchedOut; ++i) {
            const std::string s_Name = "Out" + std::to_string(i + 1);
            s_Out[i] = Hash::Crc32(s_Name.data(), s_Name.size());
        }

        return s_Out;
    }();

    for (int i = 0; i < fr::kCoinMaxWatchedOut; ++i) {
        if (s_Ids[i] == p_PinId)
            return i + 1;
    }

    return 0;
}

std::optional<int32_t> ReadInt(ZEntityType** p_Entity, std::string_view p_Name) {
    const RawRead32 s_Read = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);

    if (s_Read.status != RawRead32::Status::Ok)
        return std::nullopt;

    return static_cast<int32_t>(s_Read.bits);
}

// A spawner's item (its ZRepositoryID property kCoinItemPropertyId), or why not.
struct ItemRead {
    std::optional<fr::Guid> id;
    std::string why;
};

ItemRead ReadSpawnerItem(ZEntityType** p_Entity) {
    ItemRead s_Out;
    const PropertyLocation s_Location = LocatePropertyById(p_Entity, fr::kCoinItemPropertyId);

    switch (s_Location.status) {
        case PropertyLocation::Status::Fault: s_Out.why = FaultText(s_Location.faultCode); return s_Out;
        case PropertyLocation::Status::Missing:
            s_Out.why = fmt::format("no property {:08x}", fr::kCoinItemPropertyId);
            return s_Out;
        case PropertyLocation::Status::HasGetter:
            s_Out.why = fmt::format("property {:08x} has a getter", fr::kCoinItemPropertyId);
            return s_Out;
        case PropertyLocation::Status::Ok: break;
    }

    fr::Guid s_Id;

    if (std::string_view(s_Location.typeName) != "ZRepositoryID" || s_Location.typeSize != sizeof(s_Id)) {
        s_Out.why = fmt::format("property {:08x} is {}", fr::kCoinItemPropertyId,
                                s_Location.typeName[0] ? s_Location.typeName : "?");
        return s_Out;
    }

    if (const uint32_t s_Code = ReadEngineBytes(s_Location.address, &s_Id, sizeof(s_Id))) {
        s_Out.why = FaultText(s_Code);
        return s_Out;
    }

    s_Out.id = s_Id;
    return s_Out;
}

std::string OptText(const std::optional<int32_t>& p_Value) {
    return p_Value ? std::to_string(*p_Value) : std::string("?");
}

std::string TimeText(const std::optional<double>& p_Time) {
    return p_Time ? fmt::format("{:.1f} s", *p_Time) : std::string("not seen");
}

constexpr std::string_view kTag = "safehouse coin";

} // namespace

void CoinSpawn::Configure(bool p_On) {
    m_On = p_On;
}

void CoinSpawn::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    if (m_On)
        Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context); });
}

void CoinSpawn::Reset() {
    m_PinsOn.store(false, std::memory_order_relaxed);
    m_Listener.store(nullptr, std::memory_order_relaxed);
    m_Route.store(nullptr, std::memory_order_relaxed);

    for (size_t i = 0; i < fr::kCoinSpots; ++i) {
        m_Spawners[i].store(nullptr, std::memory_order_relaxed);
    }

    m_Done = false;
    m_IsSafehouse = false;
    m_Scene.clear();
    m_HasItem = fr::kCoinVanillaItemSpots;
    m_ItemsNote.clear();

    std::lock_guard s_Lock(m_Mutex);
    m_Planner.reset();
    m_Playing = false;
    m_Now = 0.0;
    m_Spawned = {};
    m_RouteOuts = 0;
    m_LoggedPlaced = false;
    m_ReportedCheck = false;
}

void CoinSpawn::OnSceneClear() noexcept {
    Contained(kTag, "scene clear", [&] {
        std::lock_guard s_Lock(m_Mutex);

        if (!m_IsSafehouse || !m_Planner)
            return;

        std::string s_Spots;

        for (size_t i = 0; i < fr::kCoinSpots; ++i)
            s_Spots += fmt::format("{}spot {} ({}): itemSpawned {}", i ? "; " : "", i + 1,
                                   m_HasItem[i] ? "coin" : "empty", TimeText(m_Spawned[i]));

        const fr::CoinPlanner& p = *m_Planner;
        LogTo(fr::LogTopic::Safehouse,
              "safehouse coin: summary of the visit: listener {}, {} route output(s), {} empty pick(s), {} re-pick(s), "
              "{} blocked; coin {}; {}",
              TimeText(p.ListenerAt()), m_RouteOuts, p.EmptyPicks(), p.Retries(), p.Blocked(),
              p.Spot() ? fmt::format("at spot {} ({} at {})", *p.Spot() + 1,
                                     p.ByMod() ? "spawned by the mod" : "the game's pick", TimeText(p.PlacedAt()))
                       : std::string(p.GaveUp() ? "NONE (the mod's spawn failed)" : "NONE"),
              s_Spots);
    });

    Reset();
}

void CoinSpawn::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) {
    if (!p_Context)
        return;

    const char* s_Stage = nullptr;

    switch (p_Stage) {
        case ESceneLoadingStage::eLoading_AssetsLoaded: Reset(); return;
        case ESceneLoadingStage::eLoading_SceneAllocated: s_Stage = "eLoading_SceneAllocated"; break;
        case ESceneLoadingStage::eLoading_SceneStarted: s_Stage = "eLoading_SceneStarted"; break;
        case ESceneLoadingStage::eLoading_ScenePlaying:
            if (m_Route.load(std::memory_order_relaxed)) {
                std::lock_guard s_Lock(m_Mutex);
                m_Playing = true;
                m_Now = 0.0;
                LogTo(fr::LogTopic::Safehouse, "safehouse coin: scene playing: route m_nMaxOutPinNum {}, coin spots {}",
                      OptText(ReadInt(m_Route.load(std::memory_order_relaxed), "m_nMaxOutPinNum")), m_ItemsNote);
            }
            return;
        default: return;
    }

    if (m_Done)
        return;

    const std::string s_Scene =
        SceneOf(p_Context); // a full variant (Landslide, The Author) counts as its contract's scene
    const std::string_view s_Type = p_Context->m_SceneInitParameters.m_Type.ToStringView();

    if (!fr::IsFreelancerScene(s_Type) || fr::IsFreelancerMission(s_Type, s_Scene)) {
        m_Done = true;
        return;
    }

    m_IsSafehouse = true;
    m_Scene = s_Scene;
    m_Done = Find(p_Context, s_Stage);

    if (!m_Route.load(std::memory_order_relaxed))
        return;

    ReadItems();
    LogRoute();

    {
        std::lock_guard s_Lock(m_Mutex);
        m_Planner.emplace(m_On, m_HasItem);
    }

    LogTo(fr::LogTopic::Safehouse, "safehouse coin: {}",
          m_On ? "safehouse_coin on: a pick of an empty spot is blocked and the route picks again; "
                 "exactly one coin spawns"
               : "safehouse_coin off: watching only");
    m_PinsOn.store(true, std::memory_order_release);
}

bool CoinSpawn::Find(ZEntitySceneContext* p_Context, const char* p_Stage) {
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    std::vector<size_t> s_Order;

    for (size_t i = 0; i < s_Bricks.size(); ++i) {
        if (s_Bricks[i].m_RuntimeResourceID.GetID() == fr::kCoinBrickRid)
            s_Order.push_back(i);
    }

    const bool s_LastChance = std::string_view(p_Stage) == "eLoading_SceneStarted";

    if (s_Order.empty()) {
        LogTo(fr::LogTopic::Safehouse, "safehouse coin: {} at {}: safehouse brick {:016X} not loaded{}", m_Scene,
              p_Stage, fr::kCoinBrickRid,
              s_LastChance ? "; nothing found or changed" : "; trying again at eLoading_SceneStarted");
        return s_LastChance;
    }

    struct Hit {
        ZEntityType** entity = nullptr;
        size_t count = 0;
        std::string cls;
    };

    std::vector<uint64_t> s_Ids {fr::kCoinListenerId, fr::kCoinRouteId};
    s_Ids.insert(s_Ids.end(), fr::kCoinSpawnerIds.begin(), fr::kCoinSpawnerIds.end());
    s_Ids.insert(s_Ids.end(), fr::kCoinNestIds.begin(), fr::kCoinNestIds.end());
    std::vector<Hit> s_Hits(s_Ids.size());
    std::string s_Class;
    const auto s_Start = std::chrono::steady_clock::now();

    const WalkStats s_Walk = VisitEntitiesInOrder(
        s_Bricks, s_Order,
        [&](const VisitedEntity& p_Visit) {
            uint64_t s_Id = 0;

            if (ReadEntityClassName(p_Visit.entity, s_Class, &s_Id))
                return false;

            for (size_t i = 0; i < s_Ids.size(); ++i) {
                if (s_Ids[i] != s_Id)
                    continue;

                if (s_Hits[i].count++ == 0) {
                    s_Hits[i].entity = p_Visit.entity;
                    s_Hits[i].cls = s_Class;
                }

                return true;
            }

            return false;
        },
        kWideWalkEntityCap, nullptr);
    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();

    std::string s_Found;
    bool s_Ambiguous = false;
    size_t s_Missing = 0;

    for (size_t i = 0; i < s_Ids.size(); ++i) {
        s_Found +=
            fmt::format("{}{} {}", i ? ", " : "", IdText(s_Ids[i]),
                        s_Hits[i].count == 0   ? std::string("MISSING")
                        : s_Hits[i].count == 1 ? s_Hits[i].cls
                                               : fmt::format("{} (x{}: AMBIGUOUS)", s_Hits[i].cls, s_Hits[i].count));
        s_Ambiguous = s_Ambiguous || s_Hits[i].count > 1;
        s_Missing += s_Hits[i].count == 0 ? 1 : 0;
    }

    LogTo(fr::LogTopic::Safehouse,
          "safehouse coin: {} at {}: walked the safehouse brick: {} entities, {} faults{}, {:.1f} ms; {}", m_Scene,
          p_Stage, s_Walk.visited, s_Walk.faults, s_Walk.capped ? " (capped)" : "", s_Ms, s_Found);

    if (s_Walk.visited == 0 && !s_LastChance) {
        LogTo(fr::LogTopic::Safehouse, "safehouse coin: no entities yet; trying again at eLoading_SceneStarted");
        return false;
    }

    const Hit& s_Route = s_Hits[1];

    if (s_Ambiguous || !s_Route.entity || s_Route.cls != fr::kCoinRouteClass) {
        LogTo(fr::LogTopic::Safehouse,
              "safehouse coin: the chain is not as expected (route {}, {} ids missing{}); nothing watched or changed",
              s_Route.entity ? s_Route.cls : std::string("missing"), s_Missing,
              s_Ambiguous ? ", some ids ambiguous" : "");
        return true;
    }

    for (size_t i = 0; i < fr::kCoinSpots; ++i)
        m_Spawners[i].store(s_Hits[2 + i].entity, std::memory_order_relaxed);

    m_Listener.store(s_Hits[0].entity, std::memory_order_relaxed);
    m_Route.store(s_Route.entity, std::memory_order_relaxed);
    return true;
}

// Which spots hold an item. A spawner not found never counts; when no
// spawner's item is readable, the vanilla spots (2 and 3) are assumed.
void CoinSpawn::ReadItems() {
    const auto s_Coin = fr::ParseGuid(fr::kCoinRepositoryId);
    std::array<bool, fr::kCoinSpots> s_Has {};
    size_t s_Readable = 0;
    std::string s_Text;

    for (size_t i = 0; i < fr::kCoinSpots; ++i) {
        ZEntityType** const s_Spawner = m_Spawners[i].load(std::memory_order_relaxed);
        std::string s_What;

        if (!s_Spawner) {
            s_What = "spawner not found";
        }
        else {
            const ItemRead s_Item = ReadSpawnerItem(s_Spawner);

            if (!s_Item.id) {
                s_What = "item unreadable: " + s_Item.why;
            }
            else {
                ++s_Readable;
                s_Has[i] = !s_Item.id->IsEmpty();
                s_What = s_Item.id->IsEmpty()
                             ? std::string("no item (empty nest)")
                             : fmt::format("{} ({})", fr::FormatGuid(*s_Item.id),
                                           s_Coin && *s_Item.id == *s_Coin ? "the coin" : "NOT the coin");
            }
        }

        s_Text += fmt::format("{}spot {} spawner {}: {}", i ? "; " : "", i + 1, IdText(fr::kCoinSpawnerIds[i]), s_What);
    }

    if (s_Readable == 0) {
        for (size_t i = 0; i < fr::kCoinSpots; ++i)
            s_Has[i] = fr::kCoinVanillaItemSpots[i] && m_Spawners[i].load(std::memory_order_relaxed);
    }

    m_HasItem = s_Has;
    m_ItemsNote.clear();

    for (size_t i = 0; i < fr::kCoinSpots; ++i) {
        if (m_HasItem[i])
            m_ItemsNote += fmt::format("{}{}", m_ItemsNote.empty() ? "" : ", ", i + 1);
    }

    m_ItemsNote = m_ItemsNote.empty() ? std::string("none") : m_ItemsNote;

    if (s_Readable == 0)
        m_ItemsNote += " (assumed: no item readable)";

    LogTo(fr::LogTopic::Safehouse, "safehouse coin: items: {}; coin spots {}", s_Text, m_ItemsNote);
}

void CoinSpawn::LogRoute() {
    ZEntityType** const s_Route = m_Route.load(std::memory_order_relaxed);
    LogTo(fr::LogTopic::Safehouse, "safehouse coin: route {}: m_nMaxOutPinNum {}, m_nSeed {}; Out1..Out{} -> spawners",
          IdText(fr::kCoinRouteId), OptText(ReadInt(s_Route, "m_nMaxOutPinNum")), OptText(ReadInt(s_Route, "m_nSeed")),
          fr::kCoinSpots);
}

bool CoinSpawn::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept {
    ZEntityType** const s_Entity = p_Entity.m_pObj;

    if (!s_Entity)
        return false;

    const bool s_IsRoute = s_Entity == m_Route.load(std::memory_order_relaxed);
    const bool s_IsListener = s_Entity == m_Listener.load(std::memory_order_relaxed);
    std::optional<size_t> s_Spawner;

    for (size_t i = 0; i < fr::kCoinSpots && !s_IsRoute && !s_IsListener; ++i) {
        if (s_Entity == m_Spawners[i].load(std::memory_order_relaxed))
            s_Spawner = i;
    }

    if (!s_IsRoute && !s_IsListener && !s_Spawner)
        return false;

    bool s_Block = false;

    Contained(kTag, "output pin", [&] {
        std::lock_guard s_Lock(m_Mutex);

        if (!m_Planner)
            return;

        const double s_Now = m_Playing ? m_Now : 0.0;
        const char* s_When = m_Playing ? "" : " (before playing)";

        if (s_IsListener) {
            if (p_PinId == fr::kPinEventOccurred)
                m_Planner->OnListener(s_Now);

            return;
        }

        if (s_IsRoute) {
            const int s_Out = OutNumber(p_PinId);

            if (s_Out <= 0)
                return;

            ++m_RouteOuts;
            std::string s_Why;
            s_Block = m_Planner->OnRouteOut(s_Out, s_Now, &s_Why) == fr::CoinPinVerdict::Block;
            LogTo(fr::LogTopic::Safehouse, "safehouse coin pin: route Out{} at {:.1f} s{}: {}", s_Out, s_Now, s_When,
                  s_Why);
            return;
        }

        if (p_PinId == fr::kPinSpawnerItemSpawned && !m_Spawned[*s_Spawner])
            m_Spawned[*s_Spawner] = s_Now;
    });

    return s_Block;
}

void CoinSpawn::Tick(double p_Dt) noexcept {
    if (!m_PinsOn.load(std::memory_order_relaxed))
        return;

    Contained(kTag, "tick", [&] {
        fr::CoinAction s_Action;
        double s_Now = 0.0;

        {
            std::lock_guard s_Lock(m_Mutex);

            if (!m_Playing || !m_Planner)
                return;

            m_Now += p_Dt;
            s_Now = m_Now;
            fr::CoinPlanner& p = *m_Planner;

            if (p.Spot() && !m_LoggedPlaced) {
                m_LoggedPlaced = true;
                LogTo(fr::LogTopic::Safehouse,
                      "safehouse coin: coin placed at spot {} ({}, at {}) after {} empty pick(s)", *p.Spot() + 1,
                      p.ByMod() ? "spawned by the mod" : "the game's pick", TimeText(p.PlacedAt()), p.EmptyPicks());
            }

            // The spawn check (log only): the placed spot's itemSpawned.
            if (p.Spot() && p.PlacedAt() && !m_ReportedCheck && s_Now - *p.PlacedAt() >= fr::kCoinSpawnGrace) {
                m_ReportedCheck = true;
                const size_t s = *p.Spot();
                LogTo(fr::LogTopic::Safehouse, "safehouse coin: spot {} check after {:.0f} s: itemSpawned {}{}", s + 1,
                      fr::kCoinSpawnGrace, TimeText(m_Spawned[s]), m_Spawned[s] ? "" : " (NO SPAWN SEEN)");
            }

            const auto s_Entropy = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            s_Action = p.Tick(s_Now, s_Entropy);
        }

        // Signals outside the lock: they run the graph now, which comes back
        // into the pin hooks (they take m_Mutex).
        switch (s_Action.kind) {
            case fr::CoinActionKind::None: return;
            case fr::CoinActionKind::SignalRouteIn: {
                const uint32_t s_Code = SendInputPin(m_Route.load(std::memory_order_relaxed), fr::kPinIn);
                LogTo(fr::LogTopic::Safehouse, "safehouse coin: {} at {:.1f} s: route In {}", s_Action.why, s_Now,
                      s_Code ? FaultText(s_Code) : std::string("signaled"));
                return;
            }
            case fr::CoinActionKind::SpawnDirect: {
                ZEntityType** const s_Spawner = m_Spawners[s_Action.spot].load(std::memory_order_relaxed);
                const uint32_t s_Code = s_Spawner ? SendInputPin(s_Spawner, fr::kPinDebugSpawnItem) : 0;
                const bool s_Signaled = s_Spawner && s_Code == 0;

                {
                    std::lock_guard s_Lock(m_Mutex);

                    if (m_Planner)
                        m_Planner->OnSpawnDirect(s_Action.spot, s_Now, s_Signaled);
                }

                LogTo(fr::LogTopic::Safehouse,
                      "safehouse coin: {} at {:.1f} s: debug_SpawnItem on spot {} (spawner {}, nest {}) {}; later "
                      "route outputs are blocked",
                      s_Action.why, s_Now, s_Action.spot + 1, IdText(fr::kCoinSpawnerIds[s_Action.spot]),
                      IdText(fr::kCoinNestIds[s_Action.spot]),
                      !s_Spawner ? std::string("not signaled (spawner missing)")
                      : s_Code   ? FaultText(s_Code)
                                 : std::string("signaled"));
                return;
            }
        }
    });
}
