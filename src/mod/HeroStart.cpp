// The start fix (see HeroStart.h). Every raw engine step follows the guard
// pattern in Guard.h.

#include "HeroStart.h"

#include "../core/Merces.h" // fr::JsonEventName, fr::JsonScalarsWithKeyContaining
#include "../core/Scene.h"  // fr::ToLower
#include "BlueprintRead.h"
#include "EngineText.h"
#include "EntityReads.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "Repository.h"

#include <Globals.h>

#include <Glacier/Hash.h>
#include <Glacier/IComponentInterface.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZPrimitives.h>
#include <Glacier/ZString.h>
#include <Glacier/ZContract.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <exception>
#include <set>

namespace {

constexpr const char* kSpawnClass = "ZHeroSpawn";
constexpr std::string_view kEntranceProperty = fr::kHeroSpawnEntranceProperty;
constexpr std::string_view kDefaultProperty = "m_bDefaultHeroSpawn";

constexpr std::string_view kTag = "start fix";

// The contract's entrance (ZContractsManager::m_contractContext.m_EnabledEntranceId).
struct EntranceOut {
    bool manager;
    fr::Guid id;
};

__declspec(noinline) void EntranceOf(EntranceOut* p_Out) {
    const ZContractsManager* s_Manager = Globals::ContractsManager;
    p_Out->manager = s_Manager != nullptr;

    if (s_Manager)
        std::memcpy(&p_Out->id, &s_Manager->m_contractContext.m_EnabledEntranceId, sizeof(fr::Guid));
}

__declspec(noinline) DWORD SafeEntrance(EntranceOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        EntranceOf(p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// m_sRepositoryId (a ZRepositoryID) through the engine (its change handlers
// run, as for a property set in the editor).
struct EntranceSetArgs {
    ZEntityType** entity;
    fr::Guid id;
    bool ok;
};

__declspec(noinline) void EntranceSetOf(EntranceSetArgs* p_Args) {
    ZRepositoryID s_Id;
    std::memcpy(static_cast<void*>(&s_Id), &p_Args->id, sizeof(p_Args->id));
    p_Args->ok = ZEntityRef(p_Args->entity)
                     .SetProperty(Hash::Crc32(kEntranceProperty.data(), kEntranceProperty.size()),
                                  ZObjectRef::From<ZRepositoryID>(s_Id));
}

__declspec(noinline) DWORD SafeEntranceSet(EntranceSetArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        EntranceSetOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// The same 16 bytes straight into the property's storage (when SetProperty
// did not take).
struct RawWriteArgs {
    uintptr_t address;
    fr::Guid id;
};

__declspec(noinline) void RawWriteOf(const RawWriteArgs* p_Args) {
    std::memcpy(reinterpret_cast<void*>(p_Args->address), &p_Args->id, sizeof(p_Args->id));
}

__declspec(noinline) DWORD SafeRawWrite(const RawWriteArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        RawWriteOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// A ZGuid property's storage address, or why not.
struct GuidSlot {
    uintptr_t address = 0;
    std::string why; // empty when found
};

GuidSlot LocateGuid(ZEntityType** p_Entity, std::string_view p_Name) {
    const PropertyLocation s_Location = LocateProperty(p_Entity, p_Name);

    switch (s_Location.status) {
        case PropertyLocation::Status::Ok: break;
        case PropertyLocation::Status::Missing: return {0, fmt::format("no {}", p_Name)};
        case PropertyLocation::Status::HasGetter: return {0, fmt::format("{} has a getter", p_Name)};
        case PropertyLocation::Status::Fault: return {0, fmt::format("{} {}", p_Name, FaultText(s_Location.faultCode))};
    }

    if (s_Location.typeSize != sizeof(fr::Guid))
        return {0, fmt::format("{} is {} ({} bytes)", p_Name, s_Location.typeName, s_Location.typeSize)};

    return {s_Location.address, {}};
}

// A spawn's m_sRepositoryId, read raw.
std::optional<fr::Guid> ReadEntrance(ZEntityType** p_Entity, std::string* p_Why) {
    const GuidSlot s_Slot = LocateGuid(p_Entity, kEntranceProperty);

    if (!s_Slot.why.empty()) {
        *p_Why = s_Slot.why;
        return std::nullopt;
    }

    fr::Guid s_Id;

    if (const uint32_t s_Code = ReadEngineBytes(s_Slot.address, &s_Id, sizeof(s_Id))) {
        *p_Why = fmt::format("{} {}", kEntranceProperty, FaultText(s_Code));
        return std::nullopt;
    }

    return s_Id;
}

// A spawn's m_bDefaultHeroSpawn, read raw; false when unreadable.
bool ReadDefault(ZEntityType** p_Entity) {
    const PropertyLocation s_Location = LocateProperty(p_Entity, kDefaultProperty);
    uint8_t s_Value = 0;

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != 1 ||
        ReadEngineBytes(s_Location.address, &s_Value, 1))
        return false;

    return s_Value != 0;
}

// p_Refresh: a stale cached world transform is first refreshed by the engine
// (ReadPosition in EntityReads.h; game thread). (0, 0, 0) is never a position.
std::optional<fr::StartPoint> PointOf(ZEntityType** p_Entity, bool p_Refresh = false) {
    const std::optional<WorldPosition> s_Where =
        p_Refresh ? ReadPosition(p_Entity, true).at : ReadWorldPosition(p_Entity);

    if (!s_Where)
        return std::nullopt;

    const fr::StartPoint s_Point {s_Where->x, s_Where->y, s_Where->z};

    if (!fr::IsSpawnPointRead(s_Point))
        return std::nullopt;

    return s_Point;
}

// Template mates are read for a position only in a factory this small (a
// herospawn template instance has about eight entities; a ZHeroSpawn placed
// straight in a brick has the whole brick as mates).
constexpr int64_t kMaxPointMates = 32;

std::string PointText(const std::optional<fr::StartPoint>& p_Point) {
    return p_Point ? fmt::format("({:.2f}, {:.2f}, {:.2f})", p_Point->x, p_Point->y, p_Point->z)
                   : std::string("(position not read yet)");
}

std::string EntranceText(const std::optional<fr::Guid>& p_Id) {
    return p_Id ? fr::FormatGuid(*p_Id) : std::string("<unreadable>");
}

// The known deleted spawn with this entrance (fr::KnownDeletedSpawns).
std::string DeletedName(const fr::Guid& p_Entrance) {
    for (const fr::DeletedSpawn& s_Gone : fr::KnownDeletedSpawns()) {
        if (fr::ParseGuid(s_Gone.entrance) == p_Entrance)
            return std::string(s_Gone.name);
    }

    return "unknown";
}

// p_Id into a spawn's m_sRepositoryId: SetProperty, then (when that did not
// take) a raw write, each read back. Returns what it reads back; p_How says
// how it was written.
std::optional<fr::Guid> WriteEntrance(ZEntityType** p_Entity, const fr::Guid& p_Id, std::string& p_How,
                                      std::string& p_ReadWhy) {
    EntranceSetArgs s_Args {p_Entity, p_Id, false};
    const DWORD s_SetCode = SafeEntranceSet(&s_Args);
    std::optional<fr::Guid> s_Back = ReadEntrance(p_Entity, &p_ReadWhy);
    p_How = "SetProperty";

    if (!s_SetCode && s_Back == p_Id)
        return s_Back;

    const std::string s_SetText = s_SetCode ? FaultText(s_SetCode) : s_Args.ok ? "ok" : "returned false";
    const GuidSlot s_Slot = LocateGuid(p_Entity, kEntranceProperty);
    p_How = fmt::format("SetProperty {}, read back {}; raw write", s_SetText, EntranceText(s_Back));

    if (!s_Slot.why.empty())
        p_How += fmt::format(" impossible ({})", s_Slot.why);
    else {
        const RawWriteArgs s_Raw {s_Slot.address, p_Id};

        if (const DWORD s_Code = SafeRawWrite(&s_Raw))
            p_How += fmt::format(" {}", FaultText(s_Code));

        s_Back = ReadEntrance(p_Entity, &p_ReadWhy);
    }

    return s_Back;
}

} // namespace

void HeroStart::Arm(std::string_view p_Scene, std::string_view p_Option, std::vector<uint64_t> p_VariantBricks) {
    m_Scene = std::string(p_Scene);
    m_Option = std::string(p_Option);
    m_VariantBricks = std::move(p_VariantBricks);
    m_Done = false;
}

void HeroStart::Forget() noexcept {
    m_Scene.clear();
    m_Option.clear();
    m_VariantBricks.clear();
    OnSceneClear();
}

void HeroStart::OnSceneClear() noexcept {
    m_Done = false;
    m_Watching = false;
    m_Played = 0.0;
    m_Spawns.clear();
    m_EventsWanted.store(false, std::memory_order_relaxed);
    std::lock_guard s_Lock(m_EventMutex);
    m_Requested.reset();
    m_Answers.clear();
}

void HeroStart::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    if (m_Scene.empty() || m_Done || p_Stage != ESceneLoadingStage::eLoading_SceneAllocated)
        return;

    Contained(kTag, "scene allocation", [&] {
        if (!p_Context)
            p_Context = CurrentSceneContext();

        if (!p_Context ||
            fr::ToLower(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView()) != fr::ToLower(m_Scene))
            return;

        m_Done = true;
        Fix(p_Context);
    });
}

// Why this spawn is sealed off under the armed option (fr::KnownSealedSpawns).
std::string HeroStart::SealedWhy(size_t p_Index) const {
    const fr::SealedSpawn* s_Sealed = fr::FindSealedSpawn(m_Option, m_Spawns[p_Index].ownerId);
    return s_Sealed ? std::string(s_Sealed->why) : std::string("unknown");
}

std::string HeroStart::SpawnLabel(size_t p_Index) const {
    const Spawn& s_Spawn = m_Spawns[p_Index];
    return s_Spawn.ownerId ? fmt::format("{} {:016x}", s_Spawn.name, s_Spawn.ownerId)
                           : fmt::format("{} (ZHeroSpawn {:016x})", s_Spawn.name, s_Spawn.id);
}

// The spawn's position: its own, else (when it has none) another entity's of
// its herospawn template instance (fr::PickSpawnPoint). Read guarded;
// p_Refresh: stale cached transforms are refreshed by the engine first.
void HeroStart::ReadPoint(Spawn& p_Spawn, bool p_Refresh) {
    const std::optional<fr::StartPoint> s_Own = PointOf(p_Spawn.entity, p_Refresh);
    std::vector<std::optional<fr::StartPoint>> s_Mates;
    std::vector<int> s_MateIndex;
    std::string s_NoMates;

    if (!s_Own) {
        int64_t s_Count = 0;

        if (!p_Spawn.ownerFactory || !p_Spawn.owner)
            s_NoMates = "no template instance";
        else if (const uint32_t s_Code = ReadSubEntityCount(p_Spawn.ownerFactory, &s_Count))
            s_NoMates = FaultText(s_Code);
        else if (s_Count > kMaxPointMates)
            s_NoMates = fmt::format("not in a template instance ({} entities around it)", s_Count);
        else {
            for (int i = 0; i < static_cast<int>(s_Count); ++i) {
                ZEntityType** s_Mate = nullptr;
                ZEntityBlueprintFactoryBase* s_MateFactory = nullptr;

                if (i == p_Spawn.index ||
                    ReadSubEntityAt(p_Spawn.ownerFactory, p_Spawn.owner, i, &s_Mate, &s_MateFactory) || !s_Mate ||
                    s_Mate == p_Spawn.entity)
                    continue;

                s_Mates.push_back(PointOf(s_Mate, p_Refresh));
                s_MateIndex.push_back(i);
            }
        }
    }

    const fr::SpawnPoint s_Point = fr::PickSpawnPoint(s_Own, s_Mates);
    p_Spawn.facts.where = s_Point.where;

    switch (s_Point.source) {
        case fr::SpawnPointSource::Own: p_Spawn.pointFrom = "its own"; break;
        case fr::SpawnPointSource::TemplateMate:
            p_Spawn.pointFrom = fmt::format("template entity #{}", s_MateIndex[s_Point.mate]);
            break;
        case fr::SpawnPointSource::None:
            p_Spawn.pointFrom =
                s_NoMates.empty() ? fmt::format("none of {} template entities has one", s_Mates.size()) : s_NoMates;
            break;
    }
}

// Once the scene's entities exist: the requested entrance against every hero
// spawn of the loaded bricks; with no match, one spawn re-pointed to it.
void HeroStart::Fix(ZEntitySceneContext* p_Context) {
    const auto s_Start = std::chrono::steady_clock::now();
    m_Spawns.clear();

    // The contract's entrance.
    EntranceOut s_Entrance {};
    std::optional<fr::Guid> s_Requested;
    std::string s_RequestText;

    if (const DWORD s_Code = SafeEntrance(&s_Entrance))
        s_RequestText = fmt::format("unreadable (ZContractsManager {})", FaultText(s_Code));
    else if (!s_Entrance.manager)
        s_RequestText = "unreadable (no ZContractsManager)";
    else if (s_Entrance.id.IsEmpty())
        s_RequestText = "none (the contract names no entrance)";
    else {
        s_Requested = s_Entrance.id;
        s_RequestText = fr::FormatGuid(s_Entrance.id);
    }

    // Every ZHeroSpawn of the loaded bricks.
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    const std::set<uint64_t> s_VariantBricks(m_VariantBricks.begin(), m_VariantBricks.end());
    const WalkStats s_Walk = VisitAllEntities(s_Bricks, [&](const VisitedEntity& p_Visit) {
        EntityClassCheck s_Check {};

        if (CheckEntityClass(p_Visit.entity, kSpawnClass, &s_Check) || !s_Check.matches)
            return false;

        Spawn s_Spawn;
        s_Spawn.entity = p_Visit.entity;
        s_Spawn.id = s_Check.id;
        s_Spawn.owner = p_Visit.parent;
        s_Spawn.ownerFactory = p_Visit.parentFactory;
        s_Spawn.index = p_Visit.index;
        s_Spawn.facts.inVariantBrick = s_VariantBricks.contains(p_Visit.brickRid);
        m_Spawns.push_back(std::move(s_Spawn));
        return true;
    });

    size_t s_Defaults = 0;
    std::string s_Unreadable;

    for (Spawn& s_Spawn : m_Spawns) {
        std::string s_Why;
        s_Spawn.facts.entrance = ReadEntrance(s_Spawn.entity, &s_Why);
        s_Spawn.facts.isDefault = ReadDefault(s_Spawn.entity);
        ReadPoint(s_Spawn);
        s_Defaults += s_Spawn.facts.isDefault ? 1 : 0;

        // The template instance's authored name in its brick, for the log
        // only (the ZHeroSpawn's own name is the template's, the same in
        // every instance).
        std::string s_OwnerClass;
        std::string s_NameWhy;
        std::optional<OwnedSubEntity> s_Owned;

        if (s_Spawn.owner && !ReadEntityClassName(s_Spawn.owner, s_OwnerClass, &s_Spawn.ownerId) && s_Spawn.ownerId)
            s_Owned = ReadOwnedSubEntity(s_Spawn.owner, s_Spawn.ownerId, &s_NameWhy);

        s_Spawn.name = s_Owned && !s_Owned->name.empty() ? s_Owned->name : std::string("HeroSpawn");
        s_Spawn.facts.id = s_Spawn.ownerId;
        s_Spawn.facts.sealed = fr::FindSealedSpawn(m_Option, s_Spawn.ownerId) != nullptr;

        if (!s_Spawn.facts.entrance)
            s_Unreadable += fmt::format("{}{:016x} ({})", s_Unreadable.empty() ? "" : ", ", s_Spawn.id, s_Why);
    }

    std::vector<fr::HeroSpawnFacts> s_Facts;

    for (const Spawn& s_Spawn : m_Spawns)
        s_Facts.push_back(s_Spawn.facts);

    fr::StartDecision s_Decision = fr::DecideHeroStart(s_Facts, s_Requested);
    std::string s_PointsRead;

    // A spawn is about to be re-pointed and an off-map one must not be; cached
    // transforms are often stale at scene allocation, so unread positions are
    // refreshed and the decision redone.
    if (s_Decision.action == fr::StartAction::Repoint || s_Decision.action == fr::StartAction::OffMapOnly) {
        size_t s_Missing = 0;
        size_t s_Got = 0;

        for (Spawn& s_Spawn : m_Spawns) {
            if (s_Spawn.facts.where)
                continue;

            ++s_Missing;
            ReadPoint(s_Spawn, true);
            s_Got += s_Spawn.facts.where ? 1 : 0;
        }

        if (s_Missing > 0) {
            s_Facts.clear();

            for (const Spawn& s_Spawn : m_Spawns)
                s_Facts.push_back(s_Spawn.facts);

            s_Decision = fr::DecideHeroStart(s_Facts, s_Requested);
            s_PointsRead =
                fmt::format("; positions refreshed through the engine for {} of {} spawns", s_Got, s_Missing);
        }
    }
    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();
    const std::string s_Count = fmt::format(
        "{} hero spawns in the scene ({} default{}); walk {} entities in {} bricks, {} faults{}; {:.1f} ms{}",
        m_Spawns.size(), s_Defaults,
        s_Unreadable.empty() ? std::string() : fmt::format("; entrance unreadable: {}", s_Unreadable), s_Walk.visited,
        s_Walk.bricks.size(), s_Walk.faults, s_Walk.capped ? " (capped)" : "", s_Ms, s_PointsRead);
    // Every spawn with its entrance, flags, position and place, for the log.
    const auto s_SpawnList = [&] {
        std::string s_List;

        for (size_t i = 0; i < m_Spawns.size(); ++i)
            s_List +=
                fmt::format("{}{} entrance {}{}{}{} {} {}", s_List.empty() ? "" : "; ", SpawnLabel(i),
                            EntranceText(m_Spawns[i].facts.entrance), m_Spawns[i].facts.isDefault ? " default" : "",
                            m_Spawns[i].facts.inVariantBrick ? " (variant's brick)" : "",
                            m_Spawns[i].facts.sealed ? " (sealed off under this variant)" : "",
                            m_Spawns[i].facts.where ? PointText(m_Spawns[i].facts.where)
                                                    : fmt::format("(no position: {})", m_Spawns[i].pointFrom),
                            fr::SpawnPlaceText(fr::SpawnPlaceOf(s_Facts, i)));

        return s_List;
    };
    const auto s_Publish = [&] {
        std::lock_guard s_Lock(m_EventMutex);
        m_Requested = s_Requested;
        m_Answers.clear();

        for (size_t i = 0; i < m_Spawns.size(); ++i) {
            if (m_Spawns[i].facts.entrance)
                m_Answers.emplace_back(*m_Spawns[i].facts.entrance, SpawnLabel(i));
        }
    };

    m_Watching = true;
    m_Played = 0.0;
    m_EventsWanted.store(true, std::memory_order_relaxed);

    switch (s_Decision.action) {
        case fr::StartAction::NoRequest:
            LogTo(fr::LogTopic::MapVariants,
                  "start fix: {}: requested entrance {}; the game picks the start; nothing changed; {}", m_Option,
                  s_RequestText, s_Count);
            s_Publish();
            return;
        case fr::StartAction::Matched:
            LogTo(fr::LogTopic::MapVariants,
                  "start fix: {}: requested entrance {}: answered by {} {}{}; nothing changed; {}", m_Option,
                  s_RequestText, SpawnLabel(s_Decision.spawn), PointText(m_Spawns[s_Decision.spawn].facts.where),
                  s_Decision.matches > 1 ? fmt::format(" (and {} more)", s_Decision.matches - 1) : std::string(),
                  s_Count);
            s_Publish();
            return;
        case fr::StartAction::NoSpawn:
            LogTo(
                fr::LogTopic::MapVariants,
                "start fix: {}: requested entrance {}: no spawn answers it and none can be re-pointed; 47 may start in "
                "the sky (the game does not always fall back to the default spawn); {}",
                m_Option, s_RequestText, s_Count);
            s_Publish();
            return;
        case fr::StartAction::OffMapOnly:
            LogTo(fr::LogTopic::MapVariants,
                  "start fix: {}: requested entrance {}: no spawn answers it (the variant removed it), and every spawn "
                  "that could take it stands off the playable map ({} far up, down or out), so none is re-pointed; "
                  "nothing changed; 47 may start in the sky; {}; spawns: {}",
                  m_Option, s_RequestText, s_Decision.offMap, s_Count, s_SpawnList());
            s_Publish();
            return;
        case fr::StartAction::SealedOnly:
            LogTo(fr::LogTopic::MapVariants,
                  "start fix: {}: requested entrance {} is {}'s, which this variant seals off ({}), but no other spawn "
                  "can take it; nothing changed; 47 starts there; {}; spawns: {}",
                  m_Option, s_RequestText, SpawnLabel(s_Decision.sealedSpawn), SealedWhy(s_Decision.sealedSpawn),
                  s_Count, s_SpawnList());
            s_Publish();
            return;
        case fr::StartAction::Repoint: break;
    }

    // Before touching anything: is the read a real entrance? A wrong read
    // (the contract manager's layout changed) must not re-point a spawn the
    // real entrance may need.
    std::string s_RepoWhy;

    if (!m_Repo || !m_Repo->Ready(s_RepoWhy)) {
        LogTo(fr::LogTopic::MapVariants,
              "start fix: {}: requested entrance {}: no spawn answers it, but pro.repo is not readable ({}) to confirm "
              "the entrance, so nothing changed; 47 may start in the sky; {}",
              m_Option, s_RequestText, m_Repo ? s_RepoWhy : std::string("not set up"), s_Count);
        s_Publish();
        return;
    }

    std::vector<RepoLookup> s_Lookup;
    const std::array<fr::Guid, 1> s_Keys {*s_Requested};

    if (const uint32_t s_Code = m_Repo->Lookup(s_Keys, s_Lookup);
        s_Code || s_Lookup.empty() || !s_Lookup[0].entryFound) {
        LogTo(fr::LogTopic::MapVariants,
              "start fix: {}: requested entrance {}: no spawn answers it, but it is not in pro.repo ({}), so the "
              "contract read may be wrong; nothing changed; {}",
              m_Option, s_RequestText, s_Code ? FaultText(s_Code) : std::string("not found"), s_Count);
        s_Publish();
        return;
    }

    if (s_Decision.sealedRequest)
        LogTo(fr::LogTopic::MapVariants,
              "start fix: {}: requested entrance {} is {}'s, which this variant seals off ({}); "
              "47 is moved to another start; {}; spawns: {}",
              m_Option, s_RequestText, SpawnLabel(s_Decision.sealedSpawn), SealedWhy(s_Decision.sealedSpawn), s_Count,
              s_SpawnList());
    else
        LogTo(
            fr::LogTopic::MapVariants,
            "start fix: {}: requested entrance {}: no spawn answers it (the variant removed it); the game may leave 47 "
            "at the scene's placeholder, in the sky; {}; spawns: {}",
            m_Option, s_RequestText, s_Count, s_SpawnList());

    // Re-point the picked spawn: SetProperty, then raw, each read back.
    Spawn& s_Spawn = m_Spawns[s_Decision.spawn];
    const fr::Guid s_Old = *s_Spawn.facts.entrance;
    std::string s_ReadWhy;
    std::string s_How;
    const std::optional<fr::Guid> s_Back = WriteEntrance(s_Spawn.entity, *s_Requested, s_How, s_ReadWhy);

    const std::string s_Where = PointText(s_Spawn.facts.where);
    std::string s_Why = s_Decision.why == fr::RepointWhy::SameSpot
                            ? fmt::format("{} ({})", fr::RepointWhyText(s_Decision.why), DeletedName(*s_Requested))
                            : std::string(fr::RepointWhyText(s_Decision.why));

    if (s_Decision.offMap > 0)
        s_Why += fmt::format("; passed over {} spawn{} off the playable map", s_Decision.offMap,
                             s_Decision.offMap == 1 ? "" : "s");

    if (!s_Spawn.facts.where)
        s_Why += "; its position is unknown, so whether it is on the map is unchecked";

    if (s_Back == s_Requested) {
        s_Spawn.facts.entrance = s_Back;
        LogTo(fr::LogTopic::MapVariants,
              "start fixed: {} {} now answers entrance {} (was {}; picked: {}; written with {}, read back)",
              SpawnLabel(s_Decision.spawn), s_Where, fr::FormatGuid(*s_Requested), fr::FormatGuid(s_Old), s_Why, s_How);
    }
    else
        LogTo(fr::LogTopic::MapVariants,
              "start fix: re-pointing {} {} to entrance {} did not take ({}; read back {}{}); 47 may start in the sky; "
              "see the start check after {:.0f} s of play",
              SpawnLabel(s_Decision.spawn), s_Where, fr::FormatGuid(*s_Requested), s_How, EntranceText(s_Back),
              s_ReadWhy.empty() ? std::string() : fmt::format(", {}", s_ReadWhy), fr::kStartCheckPlaySeconds);

    // The sealed spawn takes the re-pointed one's old entrance, so it no
    // longer answers the request.
    if (s_Decision.sealedRequest && s_Back == s_Requested) {
        Spawn& s_Sealed = m_Spawns[s_Decision.sealedSpawn];
        std::string s_SealedHow;
        std::string s_SealedWhy;
        const std::optional<fr::Guid> s_SealedBack = WriteEntrance(s_Sealed.entity, s_Old, s_SealedHow, s_SealedWhy);

        if (s_SealedBack == s_Old) {
            s_Sealed.facts.entrance = s_SealedBack;
            LogTo(fr::LogTopic::MapVariants,
                  "start fix: sealed start {} switched off: it now answers entrance {} (the one {} "
                  "had; written with {}, read back)",
                  SpawnLabel(s_Decision.sealedSpawn), fr::FormatGuid(s_Old), SpawnLabel(s_Decision.spawn), s_SealedHow);
        }
        else
            LogTo(fr::LogTopic::MapVariants,
                  "start fix: sealed start {} still answers entrance {} ({}; read back {}{}); 47 may "
                  "start there, in the area this variant closes or makes hostile",
                  SpawnLabel(s_Decision.sealedSpawn), fr::FormatGuid(*s_Requested), s_SealedHow,
                  EntranceText(s_SealedBack), s_SealedWhy.empty() ? std::string() : fmt::format(", {}", s_SealedWhy));
    }

    s_Publish();
}

void HeroStart::Tick(double p_GameSeconds) noexcept {
    if (!m_Watching)
        return;

    Contained(kTag, "position check", [&] {
        m_Played += p_GameSeconds;

        if (m_Played < fr::kStartCheckPlaySeconds)
            return;

        m_Watching = false;
        m_EventsWanted.store(false, std::memory_order_relaxed); // HeroSpawn_Location comes before this, if at all
        CheckPosition();
    });
}

// After a few seconds of play: did 47 start at a spawn? Read only.
void HeroStart::CheckPosition() {
    ZEntityType** s_Hero = ReadHeroEntity();
    const std::optional<fr::StartPoint> s_At = s_Hero ? PointOf(s_Hero) : std::nullopt;

    if (!s_At) {
        LogTo(fr::LogTopic::MapVariants,
              "start check: after {:.0f} s of play 47's position is unreadable ({}); {} hero spawns", m_Played,
              s_Hero ? PositionText(s_Hero) : std::string("no hero entity"), m_Spawns.size());
        return;
    }

    std::vector<std::optional<fr::StartPoint>> s_Points;

    for (Spawn& s_Spawn : m_Spawns) {
        // Read again: at scene allocation the cached transforms may not have been set yet.
        const std::optional<fr::StartPoint> s_Before = s_Spawn.facts.where;
        const std::string s_BeforeFrom = s_Spawn.pointFrom;
        ReadPoint(s_Spawn);

        if (!s_Spawn.facts.where) {
            s_Spawn.facts.where = s_Before;
            s_Spawn.pointFrom = s_BeforeFrom;
        }

        s_Points.push_back(s_Spawn.facts.where);
    }

    const std::optional<fr::NearestStart> s_Nearest = fr::NearestSpawn(s_Points, *s_At);

    if (!s_Nearest) {
        std::string s_Why;

        for (size_t i = 0; i < m_Spawns.size(); ++i)
            s_Why += fmt::format("{}{}: {}", s_Why.empty() ? "" : "; ", SpawnLabel(i), m_Spawns[i].pointFrom);

        LogTo(fr::LogTopic::MapVariants,
              "start check: after {:.0f} s of play 47 is at {}; no hero spawn position to compare with ({} spawns{})",
              m_Played, PointText(s_At), m_Spawns.size(), s_Why.empty() ? std::string() : fmt::format(": {}", s_Why));
        return;
    }

    const Spawn& s_Spawn = m_Spawns[s_Nearest->index];
    const std::string s_Spawned = fmt::format("{} {} (entrance {})", SpawnLabel(s_Nearest->index),
                                              PointText(s_Spawn.facts.where), EntranceText(s_Spawn.facts.entrance));

    if (s_Nearest->distance <= fr::kHeroFarMeters)
        LogTo(fr::LogTopic::MapVariants,
              "start check: after {:.0f} s of play 47 is at {}, {:.1f} m from {}: he started at a spawn", m_Played,
              PointText(s_At), s_Nearest->distance, s_Spawned);
    else
        LogTo(
            fr::LogTopic::MapVariants,
            "start check: after {:.0f} s of play 47 is at {}, {:.0f} m from the nearest hero spawn, {}: he did NOT "
            "start at a spawn (likely the scene's placeholder, in the sky); the mod does not move him (it has no safe "
            "way to set 47's position); restart the mission, or turn this map variant off in Settings",
            m_Played, PointText(s_At), s_Nearest->distance, s_Spawned);
}

void HeroStart::OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept {
    Contained(kTag, "sent event", [&] {
        std::string s_Json;

        if (ReadDynamicObjectJson(p_Event, &s_Json))
            return;

        const std::string_view s_Text = s_Json;

        if (fr::JsonEventName(s_Text) != "HeroSpawn_Location")
            return;

        m_EventsWanted.store(false, std::memory_order_relaxed);
        static constexpr std::array<std::string_view, 1> kParts {"RepositoryId"};
        const std::vector<fr::JsonScalar> s_Scalars = fr::JsonScalarsWithKeyContaining(s_Text, kParts);
        std::string_view s_Value = s_Scalars.empty() ? std::string_view() : s_Scalars.front().value;

        if (s_Value.size() >= 2 && s_Value.front() == '"' && s_Value.back() == '"')
            s_Value = s_Value.substr(1, s_Value.size() - 2);

        const std::optional<fr::Guid> s_Id = fr::ParseGuid(s_Value);
        std::lock_guard s_Lock(m_EventMutex);
        std::string s_Who;

        for (const auto& [s_Entrance, s_Label] : m_Answers) {
            if (s_Id && s_Entrance == *s_Id)
                s_Who += fmt::format("{}{}", s_Who.empty() ? "" : ", ", s_Label);
        }

        LogTo(fr::LogTopic::MapVariants, "start check: HeroSpawn_Location #{}: the game reports entrance {}{}: {}",
              p_EventIndex, s_Value.empty() ? std::string_view("<none>") : s_Value,
              m_Requested ? (s_Id == m_Requested ? " (the requested one)"
                                                 : fmt::format(" (requested {})", fr::FormatGuid(*m_Requested)))
                          : std::string(),
              s_Who.empty() ? std::string("no hero spawn of this scene has it") : fmt::format("answered by {}", s_Who));
    });
}
