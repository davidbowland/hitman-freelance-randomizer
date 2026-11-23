#pragma once

// Pure part of the door lock shuffle: which
// lockpickable doors trade their locked state. A seeded share (drawn per
// mission, at least kDefaultDoorLockShareMin) of the eligible locked doors is
// unlocked, and as many eligible unlocked doors are locked, preferring a door
// of the same brick, so the number of locked doors never changes. Only the
// locked state moves: a door's key (the keywords its RequiredKey holds) is
// never touched, so a key opens the same door it opened before (and a door
// that had a key and is now unlocked simply needs none). Deterministic in
// (seed, scene), independent of engine pointers and walk order.
//
// Where the state lives (HITMAN 3's door templates, access_helper_singledoor /
// access_helper_doubledoor; see DoorLockShuffle.h): the door entity
// (ZHM5SingleDoor2 / ZHM5DoubleDoor2) refers to its lock through m_rDoorLocked,
// a ZValueBool sub-entity ("VB_Locked") of the template's
// Access_Helper_DoorLogic, whose sibling "Can use lockpick" (a ZValueBool,
// m_CanUseLockpick on the door template) says whether the lockpick prompt is
// offered. Keycard and keypad doors are not lockpickable, so requiring that
// flag keeps them as they are.

#include "ShareRange.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fr {

// The least and most share of a mission's eligible locked doors that move; the
// share is drawn uniformly between them (settings door_locks_share_min, _max).
constexpr double kDefaultDoorLockShareMin = 0.5;
constexpr double kDefaultDoorLockShareMax = 1.0;

// Sub-entity ids inside access_helper_doorlogic.entitytemplate (the template
// both door helpers use; ids from its QuickEntity dump in charc0al's
// HM3_NoHoldButtons). The walk finds them by id under one parent: that
// DoorLogic instance.
constexpr uint64_t kDoorLogicLockedId = 0xa034'a938'ab5b'3598ull;      // VB_Locked: m_bValue is the locked state
constexpr uint64_t kDoorLogicCanLockpickId = 0xe7b3'96a7'f26c'6109ull; // "Can use lockpick": m_bValue
constexpr uint64_t kDoorLogicCanCrowbarId = 0xe18b'd450'5b69'a88aull;  // "Can use crowbar": m_bValue (logged only)

// IHM5Door_EOpenMode values.
constexpr int32_t kDoorOpenTwoWay = 0;
constexpr int32_t kDoorOpenDisabled = 3;

// One scene door. Every fact the rules need is passed in; an unread fact is
// empty and rejects the door (it keeps its state).
struct DoorInput {
    uint64_t handle = 0;              // identity within this load (the mod passes the door entity); 0 = none
    uint64_t lockHandle = 0;          // its lock value entity (VB_Locked); 0 = not found
    uint64_t brickRid = 0;            // the brick the walk found it in: sort key, same-brick tier, mission-brick rule
    uint64_t entityId = 0;            // its sub-entity id: sort key
    bool doorClass = true;            // a ZHM5SingleDoor2 or ZHM5DoubleDoor2
    std::optional<bool> locked;       // VB_Locked's m_bValue
    std::optional<bool> lockpickable; // "Can use lockpick" m_bValue (false on keycard and keypad doors)
    std::optional<bool> closed;       // not open now (m_rDoorOpen's m_bValue, else m_InitialState)
    std::optional<int32_t> openMode;  // m_OpenMode (IHM5Door_EOpenMode); empty when not readable (not a rule then)
    std::optional<uint64_t> key;      // whatever identifies its key (carried through, never changed)
};

enum class DoorReject {
    None,
    NoHandle,        // no entity
    Duplicate,       // the same door passed twice (only the first counts)
    NotDoorClass,    // not a ZHM5SingleDoor2 / ZHM5DoubleDoor2
    Unreadable,      // no lock entity, brick, locked state, lockpick flag or open state
    MissionBrick,    // in the mission brick: Freelancer's own setpieces
    NotLockpickable, // no lockpick prompt: keycard, keypad and scripted doors
    Open,            // open now: locking it would mean nothing
    OneWay,          // opens from one side only (an exit or shortcut)
    Disabled,        // its open mode is OM_DISABLED
    SharedLock,      // its lock entity is another door's too
};

const char* DoorRejectName(DoorReject reject);

// How a move's two doors relate.
enum class DoorTier {
    Brick, // the newly locked door is in the unlocked door's brick
    Other, // another brick (no unused unlocked door was left in the same one)
};

const char* DoorTierName(DoorTier tier);

// One move: door `unlock` (locked) becomes unlocked and door `lock` (unlocked)
// becomes locked (indices into the doors passed in).
struct DoorMove {
    size_t unlock = 0;
    size_t lock = 0;
    DoorTier tier = DoorTier::Brick;
};

struct DoorPlan {
    std::vector<DoorMove> moves;            // by `unlock`, ascending
    std::vector<DoorReject> verdicts;       // per door passed in
    std::map<DoorReject, size_t> rejected;  // counts, rejected doors only
    size_t eligible = 0;                    // doors passing every rule
    size_t eligibleLocked = 0;              // of those, locked
    size_t eligibleUnlocked = 0;            // of those, unlocked
    size_t untouchedLocked = 0;             // rejected doors that are locked (kept)
    double share = 1.0;                     // the share asked for (clamped to [0, 1])
    size_t wanted = 0;                      // share of the eligible locked doors, rounded
    bool capped = false;                    // fewer eligible unlocked doors than wanted
    std::map<DoorTier, size_t> movesByTier; // of the moves made
};

// The least and most share, clamped (ClampShareRange; NaN: the defaults).
ShareRange ClampDoorLockShares(double minShare, double maxShare);

// The share of a mission's eligible locked doors to move: uniform in the
// range (clamped as ClampDoorLockShares) from (seed, scene).
double DrawDoorLockShare(int32_t seed, std::string_view scene, ShareRange range);

// The rules, then: `wanted` = share of the eligible locked doors (rounded,
// at most the eligible unlocked ones). In a canonical order ((brickRid,
// entityId), input order for exact ties) a seeded shuffle of the locked doors
// picks which unlock; each of those (in canonical order) takes a seeded
// unused unlocked door of its own brick to lock, else one of any brick. Every
// door appears in at most one move.
DoorPlan PlanDoorLocks(int32_t seed, std::string_view scene, std::span<const DoorInput> doors, uint64_t missionBrickRid,
                       double share);

// Reopening: a door this mod locked stays locked only until it is first seen
// open (by an NPC or the player); then the mod unlocks it for the rest of the
// mission, as a lockpicked door stays unlocked in vanilla. Without this, a
// door followed through behind an NPC closes behind the player still locked.
// The watched doors are polled every kDoorWatchPollSeconds of game time.
constexpr double kDoorWatchPollSeconds = 0.25;

// Game-time accumulator: Due adds the frame's seconds and says whether a poll
// is due (at most one per call; a long frame does not queue polls).
class DoorWatchClock {
public:
    bool Due(double seconds);
    void Reset() { m_Elapsed = 0.0; }

private:
    double m_Elapsed = 0.0;
};

// One watched door at a poll: whether it is open now and whether its lock
// value reads locked (empty when not readable).
struct DoorWatchRead {
    std::optional<bool> open;
    std::optional<bool> locked;
};

enum class DoorWatchAction {
    Keep,   // closed, or its open state unreadable: still watched
    Unlock, // seen open and still locked: unlock it, stop watching
    Drop,   // seen open and already unlocked (a lockpick, a key or the level): stop watching
};

DoorWatchAction DecideWatchedDoor(const DoorWatchRead& read);

// Per watched door, in order.
std::vector<DoorWatchAction> DecideWatchedDoors(std::span<const DoorWatchRead> reads);

} // namespace fr
