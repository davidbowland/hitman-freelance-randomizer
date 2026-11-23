// Door lock shuffle and lockpick grant (see DoorLockShuffle.h). Every raw
// engine read and write follows the guard pattern in Guard.h.

#include "DoorLockShuffle.h"

#include "../core/SceneKinds.h" // mild scenes, showdowns, mission brick
#include "EngineText.h"
#include "EntityReads.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "PinIds.h"
#include "Scene.h"
#include "SceneOf.h"

#include <Glacier/ZResourceID.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr const char* kSingleDoorClass = "ZHM5SingleDoor2";
constexpr const char* kDoubleDoorClass = "ZHM5DoubleDoor2";
constexpr size_t kMaxSampleLines = 6; // per kind of unreadable door
constexpr size_t kMaxChildrenPerPart = 8;

constexpr std::string_view kTag = "door locks";

using Door = DoorLockShuffle::Door;

// Where an entity's m_bValue byte lives (the entity itself, else the first
// listed child that has it), or why not.
struct BoolSlot {
    uintptr_t address = 0;
    ZEntityType** entity = nullptr; // the entity that holds it
    std::string why;                // empty when found
};

BoolSlot FindBoolSlot(ZEntityType** p_Entity, const std::vector<ZEntityType**>* p_Children) {
    BoolSlot s_Out;

    if (!p_Entity) {
        s_Out.why = "no entity";
        return s_Out;
    }

    const auto s_Try = [&](ZEntityType** p_Candidate) -> bool {
        const PropertyLocation s_Location = LocateProperty(p_Candidate, "m_bValue");

        switch (s_Location.status) {
            case PropertyLocation::Status::Ok:
                if (s_Location.typeSize != 1) {
                    s_Out.why =
                        fmt::format("m_bValue is {} ({} bytes)",
                                    s_Location.typeName[0] ? s_Location.typeName : "<unknown>", s_Location.typeSize);
                    return false;
                }

                s_Out.address = s_Location.address;
                s_Out.entity = p_Candidate;
                s_Out.why.clear();
                return true;
            case PropertyLocation::Status::Missing: s_Out.why = "no m_bValue"; return false;
            case PropertyLocation::Status::HasGetter: s_Out.why = "m_bValue has a getter"; return false;
            case PropertyLocation::Status::Fault:
                s_Out.why = "m_bValue " + FaultText(s_Location.faultCode);
                return false;
        }

        return false;
    };

    if (s_Try(p_Entity))
        return s_Out;

    if (p_Children) {
        const std::string s_Own = s_Out.why;

        for (ZEntityType** s_Child : *p_Children) {
            if (s_Try(s_Child))
                return s_Out;
        }

        s_Out.why = s_Own + " (nor its children)";
    }

    return s_Out;
}

std::optional<bool> ReadSlot(uintptr_t p_Address, std::string* p_Why) {
    uint8_t s_Byte = 0;

    if (const uint32_t s_Code = ReadEngineBytes(p_Address, &s_Byte, 1)) {
        if (p_Why)
            *p_Why = FaultText(s_Code);
        return std::nullopt;
    }

    if (s_Byte > 1) {
        if (p_Why)
            *p_Why = fmt::format("m_bValue holds {}", s_Byte);
        return std::nullopt;
    }

    return s_Byte != 0;
}

std::optional<int32_t> ReadInt32(ZEntityType** p_Entity, std::string_view p_Name) {
    const RawRead32 s_Read = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);

    if (s_Read.status != RawRead32::Status::Ok)
        return std::nullopt;

    return static_cast<int32_t>(s_Read.bits);
}

// The template id is shared by every door of a template; the entity pointer
// is unique in a load, and the position finds the door in the level.
std::string DoorLine(const Door& p_Door) {
    return fmt::format("{} {} @{:X} {} (brick {:016X})", p_Door.doubleDoor ? "double door" : "door", IdText(p_Door.id),
                       reinterpret_cast<uintptr_t>(p_Door.entity), PositionText(p_Door.entity), p_Door.brickRid);
}

std::string BoolText(const std::optional<bool>& p_Value) {
    return p_Value ? (*p_Value ? "yes" : "no") : "?";
}

// Sets one door's lock to p_Value: the door's LockDoor / UnlockDoor pin
// first, then the value if the pin did not take. Returns how, or empty.
std::optional<std::string> SetLock(const Door& p_Door, bool p_Value) {
    const BoolSlot s_Slot = FindBoolSlot(p_Door.lock, nullptr);

    if (!s_Slot.address)
        return std::nullopt;

    if (const uint32_t s_Code = SendInputPin(p_Door.entity, p_Value ? fr::kPinLockDoor : fr::kPinUnlockDoor))
        Log("door locks: {} pin {} {}", DoorLine(p_Door), p_Value ? "LockDoor" : "UnlockDoor", FaultText(s_Code));

    if (ReadSlot(s_Slot.address, nullptr) == std::optional<bool>(p_Value))
        return std::string("pin");

    const uint8_t s_Byte = p_Value ? 1 : 0;

    if (!WriteEngineBytes(s_Slot.address, &s_Byte, 1) &&
        ReadSlot(s_Slot.address, nullptr) == std::optional<bool>(p_Value))
        return std::string("pin had no effect on the value; value written");

    return std::nullopt;
}

} // namespace

void DoorLockShuffle::Configure(bool p_On, double p_ShareMin, double p_ShareMax) {
    m_On = p_On;
    m_Shares = fr::ClampDoorLockShares(p_ShareMin, p_ShareMax);
}

void DoorLockShuffle::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                     const std::optional<fr::SeedResolution>& p_Seed) noexcept {
    if (m_On || m_LockpickFree)
        Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context, p_Seed); });
}

void DoorLockShuffle::OnSceneClear() noexcept {
    if (m_Watched > 0) {
        Contained(kTag, "reopening summary", [&] {
            LogTo(
                fr::LogTopic::DoorLocks,
                "door locks: {}: reopening: {} doors this mod locked were watched; {} opened and unlocked for the rest "
                "of the mission, {} opened already unlocked, {} could not be unlocked, {} never seen open (still "
                "locked)",
                m_Scene, m_Watched, m_Reopened, m_OpenedUnlocked, m_ReopenFailed, m_Watch.size());
        });
    }

    LockpickSceneClear();
    m_Watch.clear();
    m_WatchClock.Reset();
    m_PlayingSeconds = 0.0;
    m_Watched = 0;
    m_Reopened = 0;
    m_OpenedUnlocked = 0;
    m_ReopenFailed = 0;
    m_Done = false;
    m_Doors.clear();
    m_Pending.clear();
    m_Written.clear();
    m_Scene.clear();
}

void DoorLockShuffle::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                   const std::optional<fr::SeedResolution>& p_Seed) {
    if (!p_Context)
        return;

    const char* s_Stage = nullptr;

    switch (p_Stage) {
        case ESceneLoadingStage::eLoading_AssetsLoaded: OnSceneClear(); return;
        case ESceneLoadingStage::eLoading_SceneAllocated: s_Stage = "eLoading_SceneAllocated"; break;
        // Only when the bricks had no doors at eLoading_SceneAllocated.
        case ESceneLoadingStage::eLoading_SceneStarted: s_Stage = "eLoading_SceneStarted"; break;
        case ESceneLoadingStage::eLoading_SceneActivated:
            // lockpick_grant's lockpick check needs the world inventory: found behind the loading screen.
            if (m_LockpickFree && !m_Doors.empty() && m_PrepareInventory)
                Contained(kTag, "world inventory", [&] { m_PrepareInventory(); });
            return;
        case ESceneLoadingStage::eLoading_ScenePlaying:
            if (!m_Doors.empty()) {
                Contained(kTag, "pins", [&] { ApplyPins(); });

                if (!m_Written.empty())
                    Contained(kTag, "scene playing check", [&] { CheckAtPlaying(); });

                if (m_LockpickFree)
                    Contained(kTag, "lockpick_grant", [&] { ApplyLockpickFree(); });
            }
            return;
        default: return;
    }

    if (m_Done)
        return;

    const std::string s_Scene =
        SceneOf(p_Context); // a full variant (Landslide, The Author) counts as its contract's scene
    const std::string_view s_Type = p_Context->m_SceneInitParameters.m_Type.ToStringView();

    if (!fr::IsFreelancerMission(s_Type, s_Scene) ||
        (!fr::IsMildMissionScene(s_Scene) && !fr::IsShowdownScene(s_Scene))) {
        m_Done = true;
        return;
    }

    m_Done = Run(p_Context, p_Seed, s_Stage);
}

bool DoorLockShuffle::Run(ZEntitySceneContext* p_Context, const std::optional<fr::SeedResolution>& p_Seed,
                          const char* p_Stage) {
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    const std::string s_Scene = SceneOf(p_Context);
    const bool s_LastChance = std::string_view(p_Stage) == "eLoading_SceneStarted";

    // One walk: the doors, the DoorLogic parts by parent, and other classes named like doors.
    const auto s_Start = std::chrono::steady_clock::now();
    std::vector<Door> s_Doors;

    struct Parts {
        ZEntityType** locked = nullptr;
        ZEntityType** lockpick = nullptr;
        ZEntityType** crowbar = nullptr;
        // lockpick_grant
        ZEntityType** useLockpick = nullptr;
        ZEntityType** animating = nullptr;        // "Animating"
        ZEntityType** unlockVisible = nullptr;    // InteactionGroupVisible
        ZEntityType** unlockConditions = nullptr; // ConditionListUnlockDoor
    };

    std::unordered_map<ZEntityType**, Parts> s_Logic;                     // DoorLogic instance -> parts
    std::unordered_map<ZEntityType**, ZEntityType**> s_PartLogic;         // part (or a part's child) -> DoorLogic
    std::unordered_map<ZEntityType**, std::vector<ZEntityType**>> s_Kids; // part -> its children
    std::string s_Class;
    size_t s_ClassFaults = 0;

    const WalkStats s_Walk = VisitAllEntities(s_Bricks, [&](const VisitedEntity& p_Visit) {
        uint64_t s_Id = 0;

        if (ReadEntityClassName(p_Visit.entity, s_Class, &s_Id)) {
            ++s_ClassFaults;
            return false;
        }

        // A part's child: a template instance's inner value entity.
        if (p_Visit.parent) {
            const auto s_Part = s_Kids.find(p_Visit.parent);

            if (s_Part != s_Kids.end() && s_Part->second.size() < kMaxChildrenPerPart) {
                s_Part->second.push_back(p_Visit.entity);
                s_PartLogic.emplace(p_Visit.entity, s_PartLogic[p_Visit.parent]);
            }
        }

        if (s_Id == fr::kDoorLogicLockedId || s_Id == fr::kDoorLogicCanLockpickId ||
            s_Id == fr::kDoorLogicCanCrowbarId) {
            if (p_Visit.parent) {
                Parts& s_Parts = s_Logic[p_Visit.parent];
                (s_Id == fr::kDoorLogicLockedId        ? s_Parts.locked
                 : s_Id == fr::kDoorLogicCanLockpickId ? s_Parts.lockpick
                                                       : s_Parts.crowbar) = p_Visit.entity;
                s_PartLogic[p_Visit.entity] = p_Visit.parent;
                s_Kids.try_emplace(p_Visit.entity);
            }
        }
        else if (m_LockpickFree && p_Visit.parent && s_Id == fr::kDoorLogicUseLockpickId) {
            Parts& s_Parts = s_Logic[p_Visit.parent];
            s_Parts.useLockpick = p_Visit.entity;
            s_PartLogic[p_Visit.entity] = p_Visit.parent;
            s_Kids.try_emplace(p_Visit.entity);
        }
        else if (m_LockpickFree && p_Visit.parent &&
                 (s_Id == fr::kDoorLogicAnimatingId || s_Id == fr::kDoorLogicUnlockVisibleId ||
                  s_Id == fr::kDoorLogicUnlockConditionsId)) {
            // The Unlock entry's hide during a held pick: same parent as "Can use lockpick".
            Parts& s_Parts = s_Logic[p_Visit.parent];
            (s_Id == fr::kDoorLogicAnimatingId       ? s_Parts.animating
             : s_Id == fr::kDoorLogicUnlockVisibleId ? s_Parts.unlockVisible
                                                     : s_Parts.unlockConditions) = p_Visit.entity;
        }

        const bool s_Single = s_Class == kSingleDoorClass;

        if (s_Single || s_Class == kDoubleDoorClass) {
            Door s_Door;
            s_Door.entity = p_Visit.entity;
            s_Door.id = s_Id;
            s_Door.brickRid = p_Visit.brickRid;
            s_Door.doubleDoor = !s_Single;
            s_Doors.push_back(std::move(s_Door));
            return true;
        }

        return false;
    });
    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();

    if (s_Walk.visited == 0 || s_Doors.empty()) {
        if (!s_LastChance) {
            LogTo(fr::LogTopic::DoorLocks,
                  "door locks: {} at {}: {} entities, no doors yet; tried again at eLoading_SceneStarted", s_Scene,
                  p_Stage, s_Walk.visited);
            return false;
        }
    }

    // Each door's state.
    std::map<std::string, size_t> s_Whys;

    for (Door& s_Door : s_Doors) {
        const RefRead s_Lock = ReadEntityRefProperty(s_Door.entity, "m_rDoorLocked");

        if (!s_Lock.entity) {
            s_Door.why = "m_rDoorLocked: " + (s_Lock.why.empty() ? std::string("empty") : s_Lock.why);
        }
        else {
            s_Door.lock = s_Lock.entity;
            uint64_t s_LockId = 0;
            std::string s_LockClass;

            if (ReadEntityClassName(s_Lock.entity, s_LockClass, &s_LockId))
                s_LockClass = "<fault>";

            const auto s_KidsIt = s_Kids.find(s_Lock.entity);
            const BoolSlot s_Slot = FindBoolSlot(s_Lock.entity, s_KidsIt == s_Kids.end() ? nullptr : &s_KidsIt->second);

            if (s_Slot.address)
                s_Door.locked = ReadSlot(s_Slot.address, &s_Door.why);
            else
                s_Door.why = "lock: " + s_Slot.why;

            // Its DoorLogic's lockpick and crowbar flags.
            const auto s_LogicIt = s_PartLogic.find(s_Lock.entity);

            if (s_LogicIt == s_PartLogic.end()) {
                if (s_Door.why.empty())
                    s_Door.why = fmt::format("lock entity {} ({}) is not a known DoorLogic part", IdText(s_LockId),
                                             s_LockClass.empty() ? "<no class>" : s_LockClass);
            }
            else {
                const Parts& s_Parts = s_Logic[s_LogicIt->second];
                s_Door.lockpick = s_Parts.lockpick;
                s_Door.logic = s_LogicIt->second;
                s_Door.useLockpick = s_Parts.useLockpick;
                s_Door.animating = s_Parts.animating;
                s_Door.unlockVisible = s_Parts.unlockVisible;
                s_Door.unlockConditions = s_Parts.unlockConditions;

                if (s_Parts.useLockpick) {
                    const auto s_UseKids = s_Kids.find(s_Parts.useLockpick);

                    if (s_UseKids != s_Kids.end())
                        s_Door.useLockpickKids = s_UseKids->second;
                }

                for (auto [s_Entity, s_Out] : {std::pair {s_Parts.lockpick, &s_Door.lockpickable},
                                               std::pair {s_Parts.crowbar, &s_Door.crowbarable}}) {
                    if (!s_Entity)
                        continue;

                    const auto s_It = s_Kids.find(s_Entity);
                    const BoolSlot s_Flag = FindBoolSlot(s_Entity, s_It == s_Kids.end() ? nullptr : &s_It->second);

                    if (s_Flag.address) {
                        *s_Out = ReadSlot(s_Flag.address, nullptr);

                        if (s_Entity == s_Parts.lockpick)
                            s_Door.lockpickValue = s_Flag.entity;
                    }
                }

                if (!s_Door.lockpickable && s_Door.why.empty())
                    s_Door.why = s_Parts.lockpick ? "lockpick flag unreadable" : "no lockpick flag in its DoorLogic";
            }
        }

        const RefRead s_Open = ReadEntityRefProperty(s_Door.entity, "m_rDoorOpen");

        if (s_Open.entity) {
            s_Door.openValue = s_Open.entity;
            const BoolSlot s_Slot = FindBoolSlot(s_Open.entity, nullptr);

            if (s_Slot.address)
                s_Door.open = ReadSlot(s_Slot.address, nullptr);
        }

        s_Door.initialState = ReadInt32(s_Door.entity, "m_InitialState");
        s_Door.openMode = ReadInt32(s_Door.entity, "m_OpenMode");

        if (!s_Door.why.empty())
            ++s_Whys[s_Door.why];
    }

    // The mission brick (its doors are Freelancer's own setpieces).
    const auto s_BrickPath = MissionBrickOf(s_Scene); // a full variant's own (SceneOf.h)
    uint64_t s_MissionRid = 0;
    bool s_MissionLoaded = false;

    if (s_BrickPath) {
        s_MissionRid = ZRuntimeResourceID::FromString(fmt::format("[{}].pc_entitytype", *s_BrickPath)).GetID();

        for (size_t i = 0; i < s_Bricks.size(); ++i)
            s_MissionLoaded = s_MissionLoaded || s_Bricks[i].m_RuntimeResourceID.GetID() == s_MissionRid;
    }

    // The plan.
    std::vector<fr::DoorInput> s_Input;
    s_Input.reserve(s_Doors.size());

    for (const Door& s_Door : s_Doors) {
        fr::DoorInput s_In;
        s_In.handle = reinterpret_cast<uint64_t>(s_Door.entity);
        s_In.lockHandle = reinterpret_cast<uint64_t>(s_Door.lock);
        s_In.brickRid = s_Door.brickRid;
        s_In.entityId = s_Door.id;
        s_In.locked = s_Door.locked;
        s_In.lockpickable = s_Door.lockpickable;

        if (s_Door.open)
            s_In.closed = !*s_Door.open;
        else if (s_Door.initialState)
            s_In.closed = *s_Door.initialState == 0; // IS_CLOSED

        s_In.openMode = s_Door.openMode;
        s_Input.push_back(s_In);
    }

    const bool s_SeedKnown = p_Seed && p_Seed->source != fr::SeedSource::Fallback && p_Seed->seed != 0;
    const int32_t s_Seed = s_SeedKnown ? p_Seed->seed : 0;
    const double s_Share = s_SeedKnown ? fr::DrawDoorLockShare(s_Seed, s_Scene, m_Shares) : 0.0;
    const fr::DoorPlan s_Plan = fr::PlanDoorLocks(s_Seed, s_Scene, s_Input, s_MissionRid, s_Share);

    const auto s_Join = [](const auto& p_Map, auto&& p_Format, size_t p_Max) {
        std::string s_Text;
        size_t s_N = 0;

        for (const auto& s_Entry : p_Map) {
            if (s_N++ == p_Max) {
                s_Text += fmt::format(", +{} more", p_Map.size() - p_Max);
                break;
            }

            s_Text += (s_Text.empty() ? "" : ", ") + p_Format(s_Entry);
        }

        return s_Text.empty() ? std::string("none") : s_Text;
    };

    if (!s_Whys.empty())
        LogTo(fr::LogTopic::DoorLocks, "door locks: {}: doors left out, unreadable: {}", s_Scene,
              s_Join(
                  s_Whys, [](const auto& e) { return fmt::format("\"{}\" x{}", e.first, e.second); }, kMaxSampleLines));

    std::string s_Rejected = s_Join(
        s_Plan.rejected, [](const auto& e) { return fmt::format("{} {}", fr::DoorRejectName(e.first), e.second); }, 16);
    std::string s_Tiers = s_Join(
        s_Plan.movesByTier, [](const auto& e) { return fmt::format("{} {}", fr::DoorTierName(e.first), e.second); }, 4);

    // The door lock swap's plan (why nothing moves, when nothing does).
    if (m_On)
        LogTo(fr::LogTopic::DoorLocks,
              "door locks: {} at {}: {} doors (walk {} entities, {} class faults, {:.1f} ms); {} eligible "
              "(locked {}, unlocked {}); rejected: {}; untouched locked {}; share {:.3f} (random in [{}, {}]) -> "
              "wanted {}{}, "
              "moving {} ({})",
              s_Scene, p_Stage, s_Doors.size(), s_Walk.visited, s_ClassFaults, s_Ms, s_Plan.eligible,
              s_Plan.eligibleLocked, s_Plan.eligibleUnlocked, s_Rejected, s_Plan.untouchedLocked, s_Plan.share,
              m_Shares.min, m_Shares.max, s_Plan.wanted, s_Plan.capped ? " (capped by unlocked doors)" : "",
              s_Plan.moves.size(), s_Tiers);

    m_Scene = s_Scene;
    m_Doors = std::move(s_Doors);

    const auto s_Untouched = [&](const std::string& p_Why) {
        LogTo(fr::LogTopic::DoorLocks, "door locks: {} at {}: {}; doors left as they are", s_Scene, p_Stage, p_Why);
        return true;
    };

    if (!m_On)
        return true;

    if (!s_SeedKnown)
        return s_Untouched(
            p_Seed ? fmt::format("seed {} ({}) is unknown", p_Seed->seed, fr::SeedSourceName(p_Seed->source))
                   : std::string("no seed resolved for this mission"));

    if (s_Walk.capped)
        return s_Untouched("the walk stopped at its cap, so some doors were not seen");

    if (!s_BrickPath)
        return s_Untouched("no mission brick path");

    if (!s_MissionLoaded)
        return s_Untouched(fmt::format("mission brick {} not loaded", *s_BrickPath));

    // Not rebuilt since this mod wrote it: leave it alone.
    if (!m_LastWritten.empty()) {
        std::map<ZEntityType**, const Door*> s_ByEntity;

        for (const Door& s_Door : m_Doors)
            s_ByEntity.emplace(s_Door.entity, &s_Door);

        size_t s_Holding = 0;

        for (const Written& s_Written : m_LastWritten) {
            const auto s_It = s_ByEntity.find(s_Written.door);
            s_Holding += s_It != s_ByEntity.end() && s_It->second->lock == s_Written.lock &&
                                 s_It->second->locked == s_Written.value
                             ? 1
                             : 0;
        }

        if (s_Holding == m_LastWritten.size()) {
            // Its locked doors are still the mod's: watch them again.
            for (const Written& s_Written : m_LastWritten) {
                if (s_Written.value)
                    Watch(static_cast<size_t>(s_ByEntity[s_Written.door] - m_Doors.data()));
            }

            LogWatching();
            return s_Untouched(fmt::format("all {} doors this mod changed at an earlier load still hold what it wrote "
                                           "(the scene was not rebuilt)",
                                           s_Holding));
        }
    }

    m_LastWritten.clear();

    if (s_Plan.moves.empty())
        return s_Untouched("nothing to move");

    for (const fr::DoorMove& s_Move : s_Plan.moves) {
        const Door& s_From = m_Doors[s_Move.unlock];
        const Door& s_To = m_Doors[s_Move.lock];
        LogTo(fr::LogTopic::DoorLocks, "door locks plan: unlock {} (crowbar {}), lock {} (crowbar {}; {})",
              DoorLine(s_From), BoolText(s_From.crowbarable), DoorLine(s_To), BoolText(s_To.crowbarable),
              fr::DoorTierName(s_Move.tier));
    }

    m_Pending = s_Plan.moves;
    LogTo(fr::LogTopic::DoorLocks,
          "door locks: {} moves wait for the scene to start playing (pins UnlockDoor / LockDoor)", m_Pending.size());
    return true;
}

void DoorLockShuffle::ApplyPins() {
    if (m_Pending.empty())
        return;

    const std::vector<fr::DoorMove> s_Moves = std::move(m_Pending);
    m_Pending.clear();

    size_t s_ByPin = 0;
    size_t s_ByValue = 0;
    size_t s_Failed = 0;
    size_t s_Before = 0;

    for (const Door& s_Door : m_Doors) {
        const BoolSlot s_Slot = FindBoolSlot(s_Door.lock, nullptr);
        s_Before += s_Slot.address && ReadSlot(s_Slot.address, nullptr).value_or(false) ? 1 : 0;
    }

    for (const fr::DoorMove& s_Move : s_Moves) {
        const Door& s_From = m_Doors[s_Move.unlock];
        const Door& s_To = m_Doors[s_Move.lock];
        const BoolSlot s_FromSlot = FindBoolSlot(s_From.lock, nullptr);
        const BoolSlot s_ToSlot = FindBoolSlot(s_To.lock, nullptr);

        // The level may have changed a lock since the walk: then the pair stays as it is.
        if (!s_FromSlot.address || !s_ToSlot.address ||
            ReadSlot(s_FromSlot.address, nullptr) != std::optional<bool>(true) ||
            ReadSlot(s_ToSlot.address, nullptr) != std::optional<bool>(false)) {
            ++s_Failed;
            LogTo(fr::LogTopic::DoorLocks,
                  "door locks: pair {} / {}: lock state changed since the walk (now {} / {}); skipped",
                  DoorLine(s_From), DoorLine(s_To),
                  BoolText(s_FromSlot.address ? ReadSlot(s_FromSlot.address, nullptr) : std::nullopt),
                  BoolText(s_ToSlot.address ? ReadSlot(s_ToSlot.address, nullptr) : std::nullopt));
            continue;
        }

        const auto s_Unlocked = SetLock(s_From, false);

        if (!s_Unlocked) {
            ++s_Failed;
            LogTo(fr::LogTopic::DoorLocks, "door locks: pair {} / {}: could not unlock the first; pair skipped",
                  DoorLine(s_From), DoorLine(s_To));
            continue;
        }

        const auto s_LockedHow = SetLock(s_To, true);

        if (!s_LockedHow) {
            // Keep the count: lock the first again.
            const auto s_Back = SetLock(s_From, true);
            ++s_Failed;
            LogTo(fr::LogTopic::DoorLocks, "door locks: pair {} / {}: could not lock the second; first {}",
                  DoorLine(s_From), DoorLine(s_To),
                  s_Back ? "locked again (" + *s_Back + ")" : std::string("NOT locked again: one locked door fewer"));
            continue;
        }

        for (const std::string* s_How : {&*s_Unlocked, &*s_LockedHow})
            (*s_How == "pin" ? s_ByPin : s_ByValue)++;

        m_Written.push_back(s_Move.unlock);
        m_Written.push_back(s_Move.lock);
        Watch(s_Move.lock);
        m_LastWritten.push_back({s_From.entity, s_From.lock, false});
        m_LastWritten.push_back({s_To.entity, s_To.lock, true});
        LogTo(fr::LogTopic::DoorLocks, "door locks move: unlocked {} ({}), locked {} ({})", DoorLine(s_From),
              *s_Unlocked, DoorLine(s_To), *s_LockedHow);
    }

    size_t s_After = 0;

    for (const Door& s_Door : m_Doors) {
        const BoolSlot s_Slot = FindBoolSlot(s_Door.lock, nullptr);
        s_After += s_Slot.address && ReadSlot(s_Slot.address, nullptr).value_or(false) ? 1 : 0;
    }

    LogTo(
        fr::LogTopic::DoorLocks,
        "door locks: {} at scene playing: {} pairs moved ({} doors set by pin, {} by value), {} pairs skipped; locked "
        "doors {} before, {} after",
        m_Scene, s_Moves.size() - s_Failed, s_ByPin, s_ByValue, s_Failed, s_Before, s_After);
    LogWatching();
}

void DoorLockShuffle::CheckAtPlaying() {
    // Whether this load's written values held.
    const std::set<size_t> s_Written(m_Written.begin(), m_Written.end());
    size_t s_Held = 0;

    for (const size_t i : s_Written) {
        const Door& s_Door = m_Doors[i];
        const BoolSlot s_Slot = FindBoolSlot(s_Door.lock, nullptr);
        const std::optional<bool> s_Now = s_Slot.address ? ReadSlot(s_Slot.address, nullptr) : std::nullopt;
        s_Held += s_Now && s_Door.locked && *s_Now != *s_Door.locked ? 1 : 0;
    }

    LogTo(fr::LogTopic::DoorLocks, "door locks: {} at scene playing: {} of {} changed doors hold the mod's value",
          m_Scene, s_Held, s_Written.size());
}

void DoorLockShuffle::Watch(size_t p_Door) {
    const Door& s_Door = m_Doors[p_Door];
    const BoolSlot s_Open = FindBoolSlot(s_Door.openValue, nullptr);
    const BoolSlot s_Lock = FindBoolSlot(s_Door.lock, nullptr);

    if (!s_Open.address || !s_Lock.address) {
        LogTo(fr::LogTopic::DoorLocks, "door locks: {} not watched ({}): it stays locked until lockpicked",
              DoorLine(s_Door), !s_Open.address ? "open state: " + s_Open.why : "lock: " + s_Lock.why);
        return;
    }

    m_Watch.push_back({p_Door, s_Open.address, s_Lock.address});
    ++m_Watched;
}

void DoorLockShuffle::LogWatching() {
    if (!m_Watch.empty())
        LogTo(fr::LogTopic::DoorLocks,
              "door locks: watching {} doors this mod locked: each unlocks for the rest of the mission once seen open",
              m_Watch.size());
}

void DoorLockShuffle::Tick(double p_Seconds) noexcept {
    if (m_LpArmed.load(std::memory_order_relaxed))
        Contained(kTag, "lockpick_grant tick", [&] { LockpickTick(p_Seconds); });

    if (m_Watch.empty())
        return;

    if (std::isfinite(p_Seconds) && p_Seconds > 0.0)
        m_PlayingSeconds += p_Seconds;

    if (!m_WatchClock.Due(p_Seconds))
        return;

    Contained(kTag, "reopen watch", [&] {
        try {
            PollWatch();
        }
        catch (...) {
            // Not every quarter second: the doors still watched stay locked.
            m_Watch.clear();
            throw;
        }
    });
}

void DoorLockShuffle::PollWatch() {
    std::vector<fr::DoorWatchRead> s_Reads;
    s_Reads.reserve(m_Watch.size());

    for (const Watched& s_Watched : m_Watch)
        s_Reads.push_back({ReadSlot(s_Watched.openSlot, nullptr), ReadSlot(s_Watched.lockSlot, nullptr)});

    const std::vector<fr::DoorWatchAction> s_Actions = fr::DecideWatchedDoors(s_Reads);
    std::vector<Watched> s_Keep;

    for (size_t i = 0; i < m_Watch.size(); ++i) {
        const Door& s_Door = m_Doors[m_Watch[i].door];

        switch (s_Actions[i]) {
            case fr::DoorWatchAction::Keep: s_Keep.push_back(m_Watch[i]); break;
            case fr::DoorWatchAction::Drop:
                ++m_OpenedUnlocked;
                LogTo(fr::LogTopic::DoorLocks,
                      "door locks: {} opened {:.1f} s into the mission, already unlocked (lockpick, key or the level); "
                      "no "
                      "longer watched",
                      DoorLine(s_Door), m_PlayingSeconds);
                break;
            case fr::DoorWatchAction::Unlock: {
                const auto s_How = SetLock(s_Door, false);

                if (!s_How) {
                    ++m_ReopenFailed;
                    LogTo(fr::LogTopic::DoorLocks,
                          "door locks: {} opened {:.1f} s into the mission; could NOT unlock it; no longer watched",
                          DoorLine(s_Door), m_PlayingSeconds);
                    break;
                }

                ++m_Reopened;

                // A scene that is not rebuilt must not count it as the mod's locked door.
                for (Written& s_Written : m_LastWritten) {
                    if (s_Written.door == s_Door.entity)
                        s_Written.value = false;
                }

                LogTo(fr::LogTopic::DoorLocks,
                      "door locks: {} opened {:.1f} s into the mission; unlocked for the rest of the mission ({})",
                      DoorLine(s_Door), m_PlayingSeconds, *s_How);
                break;
            }
        }
    }

    m_Watch = std::move(s_Keep);
}

// --- lockpick_grant: pick timing (see core/LockpickFree.h) ----------------------------

namespace {

// The lockpick subaction's Started and Completed; the DoorLogic's
// StartedLockPick (the subaction's Started) and LockpickCompleted (its
// TimerSimple's Out); ItemUsed (the lockpick chosen in the Unlock menu).
using fr::kPinCompleted;
using fr::kPinItemUsed;
using fr::kPinLockpickCompleted;
using fr::kPinStarted;
using fr::kPinStartedLockPick;

constexpr size_t kMaxLockpickLines = 200;      // per scene: starts, completions, holds, checks
constexpr double kDuplicateStartSeconds = 0.5; // Started and StartedLockPick of one pick
constexpr double kVerifyAfterSeconds = 1.0;

// Set while this mod re-signals a held LockpickCompleted (game thread), so the hook lets it through.
thread_local bool t_LockpickReleasing = false;

__declspec(noinline) void SignalOutputOf(ZEntityType** p_Entity, uint32_t p_Pin) {
    ZEntityRef(p_Entity).SignalOutputPin(p_Pin);
}

__declspec(noinline) DWORD SafeSignalOutput(ZEntityType** p_Entity, uint32_t p_Pin) {
    DWORD s_Code = 0;

    __try {
        SignalOutputOf(p_Entity, p_Pin);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// A ZValueBool's m_bValue set through the engine, with its change handlers
// (the conditions that read it are told).
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

std::string CarryText(const std::optional<fr::LockpickCarry>& p_Carry, const std::string& p_Why) {
    return p_Carry ? fmt::format("own lockpicks {}, given {}{}", p_Carry->own, p_Carry->given,
                                 p_Why.empty() ? std::string() : " (" + p_Why + ")")
                   : "lockpicks unknown (" + p_Why + ")";
}

} // namespace

void DoorLockShuffle::ConfigureLockpickFree(fr::LockpickGrantMode p_Mode, int32_t p_SlowPicks,
                                            PrepareInventory p_Prepare, HeroLockpicks p_HeroLockpicks) {
    std::lock_guard s_Lock(m_LpMutex);
    m_LpMode = p_Mode;
    m_LockpickFree = p_Mode != fr::LockpickGrantMode::Off;
    m_LockpickSlowPicks = fr::ClampLockpickSlowPicks(p_SlowPicks);
    m_PrepareInventory = std::move(p_Prepare);
    m_HeroLockpicks = std::move(p_HeroLockpicks);
    m_LpTiming = fr::LockpickTiming(m_LockpickSlowPicks);
}

// The doors stay vanilla: this only learns each lockpickable door's lockpick
// subaction, its inner entities and its DoorLogic, for the pin hook (pick
// starts, LockpickCompleted).
void DoorLockShuffle::ApplyLockpickFree() {
    LockpickSceneClear(); // a scene that was not rebuilt starts its count again

    size_t s_Lockpickable = 0, s_Watched = 0, s_NoSubaction = 0, s_Animating = 0, s_UnlockConditions = 0;
    std::unordered_map<ZEntityType**, size_t> s_Subactions;
    std::unordered_map<ZEntityType**, size_t> s_Logic;
    std::unordered_map<size_t, std::string> s_Lines;

    for (size_t i = 0; i < m_Doors.size(); ++i) {
        const Door& s_Door = m_Doors[i];

        if (!s_Door.lockpickable.value_or(false))
            continue;

        ++s_Lockpickable;
        s_Animating += s_Door.animating ? 1 : 0;
        s_UnlockConditions += s_Door.unlockVisible && s_Door.unlockConditions ? 1 : 0;

        if (!s_Door.useLockpick || !s_Door.logic) {
            ++s_NoSubaction;
            continue;
        }

        ++s_Watched;
        s_Subactions[s_Door.useLockpick] = i;

        for (ZEntityType** s_Kid : s_Door.useLockpickKids)
            s_Subactions[s_Kid] = i;

        s_Logic[s_Door.logic] = i;
        s_Lines[i] = DoorLine(s_Door);
    }

    std::string s_Hero = "not checked";

    if (m_HeroLockpicks) {
        std::string s_Why;
        s_Hero = CarryText(m_HeroLockpicks(&s_Why), s_Why);
    }

    LogTo(
        fr::LogTopic::Loadout,
        "lockpick grant: {} at scene playing (mode {}): {} lockpickable doors, {} watched (the doors are vanilla), {} "
        "without a lockpick subaction found; 47 at scene start: {} (the given items come a few seconds into play, "
        "\"loadout lockpick grant:\" lines); {}",
        m_Scene, fr::LockpickGrantModeName(m_LpMode), s_Lockpickable, s_Watched, s_NoSubaction, s_Hero,
        fr::LockpickGrantSlows(m_LpMode)
            ? fmt::format("with only the given lockpick a lock takes {} times as long to open (LockpickCompleted "
                          "held back, then signaled again); with any other lockpick it is vanilla",
                          m_LockpickSlowPicks)
            : std::string("picks are vanilla speed; after each pick the nails left are logged"));

    if (fr::LockpickGrantSlows(m_LpMode))
        LogTo(
            fr::LogTopic::Loadout,
            "lockpick grant: {}: Animating ({:016x}) found on {} of {} lockpickable doors (set true during a held pick "
            "to hide the Unlock entry; a door without it falls back to \"Can use lockpick\" off); the Unlock entry's "
            "InteactionGroupVisible and ConditionListUnlockDoor found on {} (read back at the hide)",
            m_Scene, fr::kDoorLogicAnimatingId, s_Animating, s_Lockpickable, s_UnlockConditions);

    std::lock_guard s_Lock(m_LpMutex);
    m_LpSubactions = std::move(s_Subactions);
    m_LpLogic = std::move(s_Logic);
    m_LpDoorLines = std::move(s_Lines);
    m_LpMade = s_Watched;
    m_LpArmed.store(!m_LpLogic.empty(), std::memory_order_relaxed);
}

bool DoorLockShuffle::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept {
    if (t_LockpickReleasing)
        return false;

    const bool s_Known = p_PinId == kPinStarted || p_PinId == kPinCompleted || p_PinId == kPinStartedLockPick ||
                         p_PinId == kPinLockpickCompleted || p_PinId == kPinItemUsed;

    if (!s_Known)
        return false;

    bool s_Block = false;

    Contained(kTag, "lockpick pin", [&] {
        ZEntityType** const s_Entity = p_Entity.m_pObj;
        const bool s_FromLogic = p_PinId == kPinStartedLockPick || p_PinId == kPinLockpickCompleted;
        size_t s_Door = 0;
        double s_Now = 0;
        std::string s_Line;
        HeroLockpicks s_HeroLockpicks;
        fr::LockpickGrantMode s_Mode = fr::LockpickGrantMode::Off;

        {
            std::lock_guard s_Lock(m_LpMutex);
            const auto& s_Map = s_FromLogic ? m_LpLogic : m_LpSubactions;
            const auto s_It = s_Map.find(s_Entity);

            if (s_It == s_Map.end())
                return;

            const auto s_LineIt = m_LpDoorLines.find(s_It->second);

            if (s_LineIt == m_LpDoorLines.end())
                return;

            s_Door = s_It->second;
            s_Now = m_LpClock;
            s_Line = s_LineIt->second;
            s_HeroLockpicks = m_HeroLockpicks;
            s_Mode = m_LpMode;
        }

        const char* s_PinName = p_PinId == kPinStarted           ? "Started"
                                : p_PinId == kPinCompleted       ? "Completed"
                                : p_PinId == kPinStartedLockPick ? "StartedLockPick"
                                                                 : "LockpickCompleted";

        if (p_PinId == kPinItemUsed) {
            // 47 chose the lockpick in the Unlock menu: the pick is timed from here.
            std::lock_guard s_Lock(m_LpMutex);
            m_LpTiming.Chosen(s_Door, s_Now);

            if (++m_LpLines <= kMaxLockpickLines)
                LogTo(fr::LogTopic::Loadout,
                      "lockpick grant: {}: lockpick chosen in the Unlock menu (ItemUsed) at {:.2f} s", s_Line, s_Now);
            return;
        }

        if (p_PinId == kPinCompleted) {
            std::lock_guard s_Lock(m_LpMutex);
            m_LpTiming.SubactionDone(s_Door, s_Now); // a pending hide of the Unlock prompt is due now

            if (++m_LpLines <= kMaxLockpickLines)
                LogTo(fr::LogTopic::Loadout, "lockpick grant: {}: lockpick subaction Completed at {:.2f} s", s_Line,
                      s_Now);
            return;
        }

        if (p_PinId == kPinStarted || p_PinId == kPinStartedLockPick) {
            // The subaction's Started and the DoorLogic's StartedLockPick come together: one start.
            {
                std::lock_guard s_Lock(m_LpMutex);
                const auto s_Last = m_LpLastStart.find(s_Door);

                if (s_Last != m_LpLastStart.end() && s_Now - s_Last->second < kDuplicateStartSeconds)
                    return;

                m_LpLastStart[s_Door] = s_Now;
            }

            std::string s_Why;
            const std::optional<fr::LockpickCarry> s_Carry = s_HeroLockpicks ? s_HeroLockpicks(&s_Why) : std::nullopt;
            const std::optional<bool> s_Free = fr::IsFreePick(s_Carry);

            std::lock_guard s_Lock(m_LpMutex);
            const std::optional<double> s_ChoiceAt = m_LpTiming.Started(s_Door, s_Now, s_Free);
            ++m_LpStarted;

            if (++m_LpLines <= kMaxLockpickLines)
                LogTo(fr::LogTopic::Loadout,
                      "lockpick grant: {}: lockpick started ({}) at {:.2f} s, timed from {}; 47: {}: {} (checked again "
                      "when LockpickCompleted arrives; that check decides)",
                      s_Line, s_PinName, s_Now,
                      s_ChoiceAt ? fmt::format("the choice at {:.2f} s", *s_ChoiceAt)
                                 : fmt::format("here (no lockpick choice seen in the {:.0f} s before)",
                                               fr::kLockpickChoiceToStartSeconds),
                      CarryText(s_Carry, s_Why), fr::LockpickCarryVerdict(s_Carry));
            return;
        }

        // LockpickCompleted: which lockpicks 47 carries is checked now, at pick time (it decides the hold).
        std::string s_Why;
        const std::optional<fr::LockpickCarry> s_Carry = s_HeroLockpicks ? s_HeroLockpicks(&s_Why) : std::nullopt;
        const bool s_Slows = fr::LockpickGrantSlows(s_Mode);
        const std::optional<bool> s_FreeNow = s_Slows ? fr::IsFreePick(s_Carry) : std::optional<bool>(false);
        const std::string s_CarryText = CarryText(s_Carry, s_Why);

        std::lock_guard s_Lock(m_LpMutex);
        const fr::LockpickTiming::Completion s_Done = m_LpTiming.Completed(s_Door, s_Now, s_FreeNow);

        if (s_Done.hold) {
            s_Block = true;
            ++m_LpHeld;

            if (++m_LpLines <= kMaxLockpickLines)
                LogTo(fr::LogTopic::Loadout,
                      "lockpick grant: {}: LockpickCompleted at {:.2f} s, {:.2f} s after the {}, with the given "
                      "lockpick only (47 now: {}): held back {:.2f} s (the door stays locked until then; {:.2f} s "
                      "from the {} to the release)",
                      s_Line, s_Now, s_Done.span, s_Done.fromChoice ? "choice" : "start", s_CarryText,
                      s_Done.holdSeconds, s_Done.span + s_Done.holdSeconds, s_Done.fromChoice ? "choice" : "start");
            return;
        }

        ++m_LpPassed;
        m_LpVerify.push_back({s_Door, s_Now + kVerifyAfterSeconds, "passed"});

        if (++m_LpLines <= kMaxLockpickLines)
            LogTo(fr::LogTopic::Loadout, "lockpick grant: {}: LockpickCompleted at {:.2f} s passed (47 now: {}; {})",
                  s_Line, s_Now, s_CarryText,
                  !s_Done.seenStart ? "no start seen"
                  : !s_Slows        ? fmt::format("{:.2f} s after the start; mode {}: never held", s_Done.span,
                                                  fr::LockpickGrantModeName(s_Mode))
                  : !s_Done.free
                      ? fmt::format("{}, {:.2f} s after the start", fr::LockpickCarryVerdict(s_Carry), s_Done.span)
                      : fmt::format("with the given lockpick, {:.2f} s after the start: no hold", s_Done.span));
    });

    return s_Block;
}

// A held door's "Can use lockpick" set off (p_On false) or back on. Game
// thread, never under m_LpMutex: the property's change handlers may signal
// pins the hook sees.
std::string DoorLockShuffle::SetLockpickFlag(ZEntityType** p_Value, bool p_On) {
    BoolSetArgs s_Args {p_Value, p_On, false};
    const DWORD s_Code = SafeSetBool(&s_Args);
    const BoolSlot s_Slot = FindBoolSlot(p_Value, nullptr);
    std::optional<bool> s_Back = s_Slot.address ? ReadSlot(s_Slot.address, nullptr) : std::nullopt;
    std::string s_How = s_Code      ? "setting it " + FaultText(s_Code)
                        : s_Args.ok ? std::string("set")
                                    : std::string("the engine refused the set");

    // Back on: it must not stay off; the value is written when the set did not take.
    if (p_On && s_Back != std::optional<bool>(true) && s_Slot.address) {
        const uint8_t s_Byte = 1;

        if (!WriteEngineBytes(s_Slot.address, &s_Byte, 1))
            s_How += "; value written directly";

        s_Back = ReadSlot(s_Slot.address, nullptr);
    }

    return fmt::format("{}; reads back {}", s_How, BoolText(s_Back));
}

// A door's Animating set through its SetTrue / SetFalse input pin, as the
// vanilla key, lockpick and crowbar actions do (so the conditions that read it,
// ConditionListUnlockDoor, are told). When the pin did not take: SetProperty
// with its change handlers; and SetFalse only, as a last resort, the value
// written directly (it must never stay true: a relocked door would never offer
// Unlock again). Game thread, never under m_LpMutex. p_Back: the read-back.
std::string DoorLockShuffle::SetAnimating(ZEntityType** p_Entity, bool p_Value, std::optional<bool>* p_Back) {
    const char* s_PinName = p_Value ? "SetTrue" : "SetFalse";
    const uint32_t s_Pin = p_Value ? fr::kPinSetTrue : fr::kPinSetFalse;
    const uint32_t s_Code = SendInputPin(p_Entity, s_Pin);
    const BoolSlot s_Slot = FindBoolSlot(p_Entity, nullptr);
    std::optional<bool> s_Back = s_Slot.address ? ReadSlot(s_Slot.address, nullptr) : std::nullopt;
    std::string s_How =
        fmt::format("{} (0x{:08X}) {}", s_PinName, s_Pin, s_Code ? FaultText(s_Code) : std::string("sent"));

    if (!s_Slot.address) {
        s_How += "; its m_bValue not found (" + s_Slot.why + ")";
    }
    else if (s_Back != std::optional<bool>(p_Value)) {
        BoolSetArgs s_Args {p_Entity, p_Value, false};
        const DWORD s_SetCode = SafeSetBool(&s_Args);
        s_How += fmt::format("; the pin did not take (reads {}); SetProperty {}", BoolText(s_Back),
                             s_SetCode   ? FaultText(s_SetCode)
                             : s_Args.ok ? std::string("set")
                                         : std::string("refused"));
        s_Back = ReadSlot(s_Slot.address, nullptr);

        if (!p_Value && s_Back != std::optional<bool>(false)) {
            const uint8_t s_Byte = 0;

            if (!WriteEngineBytes(s_Slot.address, &s_Byte, 1))
                s_How += "; value written directly (the Unlock entry's conditions may not be told)";

            s_Back = ReadSlot(s_Slot.address, nullptr);
        }
    }

    if (p_Back)
        *p_Back = s_Back;

    return fmt::format("{}; reads back {}", s_How, BoolText(s_Back));
}

// The Unlock entry of a held door hidden: its Animating set true, as during a
// normal pick (after the subaction's Completed: vanilla's own clears of
// Animating come before it). "Can use lockpick" off only as a fallback.
void DoorLockShuffle::HideLockpick(size_t p_Door) {
    if (p_Door >= m_Doors.size())
        return;

    const Door& s_Door = m_Doors[p_Door];
    const std::string s_Line = DoorLine(s_Door);
    bool s_AnimatingSet = false;
    std::string s_AnimatingHow = "Animating not found for this door";

    if (s_Door.animating) {
        // Recorded before anything else: Animating is cleared on every path from here.
        std::erase_if(m_LpHidden, [&](const LpHidden& h) { return h.door == p_Door; }); // never two of a door
        m_LpHidden.push_back({p_Door, s_Door.animating, nullptr});
        m_LpAnimating.Start(p_Door);
        std::optional<bool> s_Back;
        s_AnimatingHow = "Animating " + SetAnimating(s_Door.animating, true, &s_Back);
        s_AnimatingSet = s_Back == std::optional<bool>(true);
    }

    const auto s_ReadCached = [](ZEntityType** p_Entity) -> std::optional<bool> {
        if (!p_Entity)
            return std::nullopt;

        const BoolSlot s_Slot = FindBoolSlot(p_Entity, nullptr);
        return s_Slot.address ? ReadSlot(s_Slot.address, nullptr) : std::nullopt;
    };

    const std::optional<bool> s_Visible = s_ReadCached(s_Door.unlockVisible);
    const std::optional<bool> s_Conditions = s_ReadCached(s_Door.unlockConditions);
    const fr::UnlockEntryState s_Entry = fr::UnlockEntryAfterAnimating(s_Visible, s_Conditions);

    LogTo(fr::LogTopic::Loadout,
          "lockpick grant: {}: Unlock entry hidden at {:.2f} s while the pick is held ({}); cached m_bValue: "
          "InteactionGroupVisible {}{}, ConditionListUnlockDoor {}{} (expect no, no): {}",
          s_Line, m_LpClock, s_AnimatingHow, BoolText(s_Visible), s_Door.unlockVisible ? "" : " (not found)",
          BoolText(s_Conditions), s_Door.unlockConditions ? "" : " (not found)", fr::UnlockEntryStateName(s_Entry));

    if (!fr::LockpickFlagFallback(s_AnimatingSet, s_Entry))
        return;

    // The fallback: the menu's lockpick entry grayed (still selectable, with only the crowbar).
    const char* s_Why = !s_Door.animating ? "Animating was not found"
                        : !s_AnimatingSet ? "Animating did not read back true"
                                          : "the Unlock entry's conditions still read true with Animating set";

    if (!s_Door.lockpickValue) {
        LogTo(fr::LogTopic::Loadout,
              "lockpick grant: {}: fallback wanted ({}), but this door's \"Can use lockpick\" value was not found: the "
              "Unlock menu may still offer the lockpick during the hold",
              s_Line, s_Why);
        return;
    }

    const BoolSlot s_Slot = FindBoolSlot(s_Door.lockpickValue, nullptr);
    const std::optional<bool> s_Before = s_Slot.address ? ReadSlot(s_Slot.address, nullptr) : std::nullopt;

    if (s_Before != std::optional<bool>(true)) {
        LogTo(fr::LogTopic::Loadout,
              "lockpick grant: {}: fallback wanted ({}); \"Can use lockpick\" reads {}; left alone", s_Line, s_Why,
              BoolText(s_Before));
        return;
    }

    const auto s_It =
        std::find_if(m_LpHidden.begin(), m_LpHidden.end(), [&](const LpHidden& h) { return h.door == p_Door; });

    if (s_It != m_LpHidden.end())
        s_It->value = s_Door.lockpickValue;
    else
        m_LpHidden.push_back({p_Door, nullptr, s_Door.lockpickValue});

    const std::string s_How = SetLockpickFlag(s_Door.lockpickValue, false);
    LogTo(fr::LogTopic::Loadout,
          "lockpick grant: {}: fallback ({}): \"Can use lockpick\" off at {:.2f} s while the pick is held, so the "
          "menu's lockpick entry is grayed ({})",
          s_Line, s_Why, m_LpClock, s_How);
}

// Each frame of a hold: a hidden door's Animating that reads false (cleared by
// something) is set true again.
void DoorLockShuffle::KeepAnimating() {
    for (const size_t s_Door : m_LpAnimating.Doors()) {
        const auto s_It =
            std::find_if(m_LpHidden.begin(), m_LpHidden.end(), [&](const LpHidden& h) { return h.door == s_Door; });

        if (s_It == m_LpHidden.end() || !s_It->animating) {
            m_LpAnimating.Stop(s_Door);
            continue;
        }

        const BoolSlot s_Slot = FindBoolSlot(s_It->animating, nullptr);
        const std::optional<bool> s_Now = s_Slot.address ? ReadSlot(s_Slot.address, nullptr) : std::nullopt;
        const fr::AnimatingResets::Step s_Step = m_LpAnimating.Read(s_Door, s_Now, m_LpClock);

        if (!s_Step.setTrue)
            continue;

        const std::string s_How = SetAnimating(s_It->animating, true, nullptr);

        if (s_Step.log && s_Door < m_Doors.size())
            LogTo(fr::LogTopic::Loadout,
                  "lockpick grant: {}: Animating read no during the hold at {:.2f} s: set true again ({}); {} re-sets "
                  "this hold, {} since the last such line (at most one line every {:.0f} s)",
                  DoorLine(m_Doors[s_Door]), m_LpClock, s_How, s_Step.resets, s_Step.unlogged,
                  fr::kAnimatingResetLogSeconds);
    }
}

void DoorLockShuffle::ShowLockpick(size_t p_Door, const char* p_When) {
    const auto s_It =
        std::find_if(m_LpHidden.begin(), m_LpHidden.end(), [&](const LpHidden& h) { return h.door == p_Door; });

    if (s_It == m_LpHidden.end())
        return;

    const LpHidden s_Hidden = *s_It;
    m_LpHidden.erase(s_It);
    const std::optional<size_t> s_Resets = m_LpAnimating.Stop(p_Door);
    const std::string s_Line =
        s_Hidden.door < m_Doors.size() ? DoorLine(m_Doors[s_Hidden.door]) : std::string("door ?");

    if (s_Hidden.animating) {
        const std::string s_How = SetAnimating(s_Hidden.animating, false, nullptr);
        LogTo(fr::LogTopic::Loadout,
              "lockpick grant: {}: Animating cleared {} ({}; set true again {} times during the hold)", s_Line, p_When,
              s_How, s_Resets.value_or(0));
    }

    if (s_Hidden.value) {
        const std::string s_How = SetLockpickFlag(s_Hidden.value, true);
        LogTo(fr::LogTopic::Loadout, "lockpick grant: {}: \"Can use lockpick\" back on {} ({})", s_Line, p_When, s_How);
    }
}

void DoorLockShuffle::ShowAllLockpicks(const char* p_When) {
    while (!m_LpHidden.empty())
        ShowLockpick(m_LpHidden.front().door, p_When);

    m_LpAnimating.Clear();
}

void DoorLockShuffle::ReleaseLockpick(size_t p_Door) {
    if (p_Door >= m_Doors.size())
        return;

    // Animating cleared (and the fallback's "Can use lockpick" back on) before
    // the door unlocks: so a fault below leaves the Unlock entry offered too.
    ShowLockpick(p_Door, "just before the held LockpickCompleted is signaled again");

    const Door& s_Door = m_Doors[p_Door];
    t_LockpickReleasing = true;
    const DWORD s_Code = SafeSignalOutput(s_Door.logic, kPinLockpickCompleted);
    t_LockpickReleasing = false;

    std::string s_How = "LockpickCompleted signaled";

    if (s_Code) {
        // The door must not stay locked: unlock it as the reopen watch does.
        const auto s_Unlocked = SetLock(s_Door, false);
        s_How = fmt::format("signaling LockpickCompleted {}; {}", FaultText(s_Code),
                            s_Unlocked ? "unlocked directly (" + *s_Unlocked + ")" : std::string("could NOT unlock"));
    }

    std::lock_guard s_Lock(m_LpMutex);
    ++m_LpReleased;
    m_LpVerify.push_back({p_Door, m_LpClock + kVerifyAfterSeconds, "released"});

    if (++m_LpLines <= kMaxLockpickLines)
        LogTo(fr::LogTopic::Loadout, "lockpick grant: {}: held LockpickCompleted released at {:.2f} s ({})",
              DoorLine(s_Door), m_LpClock, s_How);
}

void DoorLockShuffle::LockpickTick(double p_Seconds) {
    std::vector<size_t> s_Due;
    std::vector<size_t> s_Hides;
    std::vector<size_t> s_Expired;
    std::vector<LpVerify> s_Verify;

    {
        std::lock_guard s_Lock(m_LpMutex);

        if (std::isfinite(p_Seconds) && p_Seconds > 0.0)
            m_LpClock += p_Seconds;

        s_Due = m_LpTiming.Due(m_LpClock);
        s_Hides = m_LpTiming.HidesDue(m_LpClock); // after Due: a released door is not hidden
        s_Expired = m_LpTiming.Expired(m_LpClock);

        std::erase_if(m_LpVerify, [&](const LpVerify& p_Verify) {
            if (p_Verify.at > m_LpClock)
                return false;

            s_Verify.push_back(p_Verify);
            return true;
        });
    }

    for (const size_t s_Door : s_Due)
        ReleaseLockpick(s_Door);

    for (const size_t s_Door : s_Hides)
        HideLockpick(s_Door);

    if (m_LpAnimating.Size() > 0)
        KeepAnimating();

    for (const size_t s_Door : s_Expired) {
        if (s_Door < m_Doors.size())
            LogTo(fr::LogTopic::Loadout,
                  "lockpick grant: {}: started, no LockpickCompleted within {:.0f} s (canceled, or the DoorLogic's "
                  "forwarded output does not pass the pin hook: then no pick is held)",
                  DoorLine(m_Doors[s_Door]), fr::kLockpickStartedExpirySeconds);
    }

    for (const LpVerify& s_Check : s_Verify) {
        if (s_Check.door >= m_Doors.size())
            continue;

        const Door& s_Door = m_Doors[s_Check.door];
        const BoolSlot s_Slot = FindBoolSlot(s_Door.lock, nullptr);
        const std::optional<bool> s_Locked = s_Slot.address ? ReadSlot(s_Slot.address, nullptr) : std::nullopt;
        std::string s_Why;
        // With nails: whether the game used one up (the given count drops by one per pick).
        const std::string s_Carry =
            m_HeroLockpicks ? CarryText(m_HeroLockpicks(&s_Why), s_Why) : std::string("not checked");
        LogTo(fr::LogTopic::Loadout, "lockpick grant: {}: {:.1f} s after LockpickCompleted {}: locked {}; 47: {}",
              DoorLine(s_Door), kVerifyAfterSeconds, s_Check.what, BoolText(s_Locked), s_Carry);
    }
}

void DoorLockShuffle::LockpickSceneClear() {
    // Holds are dropped here (scene clear, a restart's new walk): a door's
    // Animating set true for one is cleared, and the fallback's "Can use
    // lockpick" goes back on, first (game thread, not under the lock). Doors
    // of a scene already gone are not touched.
    std::erase_if(m_LpHidden, [&](const LpHidden& h) {
        if (h.door < m_Doors.size() && (!h.animating || m_Doors[h.door].animating == h.animating) &&
            (!h.value || m_Doors[h.door].lockpickValue == h.value))
            return false;

        m_LpAnimating.Stop(h.door);
        LogTo(fr::LogTopic::Loadout,
              "lockpick grant: a held door's Unlock entry was hidden (Animating {}, \"Can use lockpick\" {}) when its "
              "scene went away; not set back (the scene's doors are new)",
              h.animating ? "set true" : "untouched", h.value ? "off" : "untouched");
        return true;
    });

    if (!m_LpHidden.empty())
        LogTo(fr::LogTopic::Loadout,
              "lockpick grant: {} held doors' Unlock entries still hidden as the scene ends or restarts: clearing",
              m_LpHidden.size());

    ShowAllLockpicks("as the scene ends or restarts with the pick still held");

    std::lock_guard s_Lock(m_LpMutex);

    if (m_LpMade > 0)
        LogTo(fr::LogTopic::Loadout,
              "lockpick grant: {}: {} lockpickable doors watched; {} picks started, {} completions held and {} "
              "released, {} passed{}",
              m_Scene, m_LpMade, m_LpStarted, m_LpHeld, m_LpReleased, m_LpPassed,
              m_LpTiming.Held() ? fmt::format("; {} still held at the scene's end", m_LpTiming.Held()) : std::string());

    m_LpArmed.store(false, std::memory_order_relaxed);
    m_LpSubactions.clear();
    m_LpLogic.clear();
    m_LpDoorLines.clear();
    m_LpLastStart.clear();
    m_LpTiming.Clear();
    m_LpVerify.clear();
    m_LpClock = 0.0;
    m_LpMade = m_LpStarted = m_LpHeld = m_LpPassed = m_LpReleased = m_LpLines = 0;
}
