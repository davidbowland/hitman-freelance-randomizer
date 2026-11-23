#include "doctest.h"

#include "DoorLocks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace {

// Each door's state after the plan, in input order.
struct DoorState {
    std::optional<bool> locked;
    std::optional<uint64_t> key;
};

std::vector<DoorState> ApplyDoorMoves(std::span<const fr::DoorInput> doors, const fr::DoorPlan& plan) {
    std::vector<DoorState> out;
    out.reserve(doors.size());

    for (const fr::DoorInput& d : doors)
        out.push_back({d.locked, d.key});

    for (const fr::DoorMove& move : plan.moves) {
        if (move.unlock < out.size() && move.lock < out.size()) {
            out[move.unlock].locked = false;
            out[move.lock].locked = true;
        }
    }

    return out;
}

constexpr const char* kScene = "assembly:/_pro/scenes/missions/skunk/scene_skunk_mild_solitaire.entity";
constexpr uint64_t kBrickA = 0x77;
constexpr uint64_t kBrickB = 0x78;
constexpr uint64_t kMissionBrick = 0x99;

fr::DoorInput Door(uint64_t handle, bool locked, uint64_t brick = kBrickA) {
    fr::DoorInput d;
    d.handle = handle;
    d.lockHandle = handle + 0x1000;
    d.brickRid = brick;
    d.entityId = handle * 13;
    d.locked = locked;
    d.lockpickable = true;
    d.closed = true;
    d.openMode = fr::kDoorOpenTwoWay;
    d.key = handle % 3 == 0 ? std::optional<uint64_t>(0xC0DE + handle) : std::nullopt;
    return d;
}

// n doors in one brick, the first `locked` of them locked.
std::vector<fr::DoorInput> Doors(size_t n, size_t locked, uint64_t firstHandle = 1, uint64_t brick = kBrickA) {
    std::vector<fr::DoorInput> out;

    for (size_t i = 0; i < n; ++i)
        out.push_back(Door(firstHandle + i, i < locked, brick));

    return out;
}

size_t LockedCount(const std::vector<DoorState>& states) {
    return static_cast<size_t>(
        std::count_if(states.begin(), states.end(), [](const DoorState& s) { return s.locked.value_or(false); }));
}

std::set<uint64_t> LockedHandles(const std::vector<fr::DoorInput>& doors, const std::vector<DoorState>& states) {
    std::set<uint64_t> out;

    for (size_t i = 0; i < doors.size(); ++i) {
        if (states[i].locked.value_or(false))
            out.insert(doors[i].handle);
    }

    return out;
}

} // namespace

TEST_CASE("door locks: the locked count never changes and the share decides the moves") {
    const auto doors = Doors(20, 6);

    for (const double share : {0.0, 0.33, 0.5, 1.0}) {
        const fr::DoorPlan plan = fr::PlanDoorLocks(7, kScene, doors, kMissionBrick, share);
        const auto after = ApplyDoorMoves(doors, plan);

        CHECK(LockedCount(after) == 6);
        CHECK(plan.eligibleLocked == 6);
        CHECK(plan.eligibleUnlocked == 14);
        CHECK(plan.wanted == static_cast<size_t>(std::floor(share * 6 + 0.5)));
        CHECK(plan.moves.size() == plan.wanted);
        CHECK_FALSE(plan.capped);
    }

    // Share 1: every locked door unlocks.
    const fr::DoorPlan all = fr::PlanDoorLocks(7, kScene, doors, kMissionBrick, 1.0);
    const auto after = ApplyDoorMoves(doors, all);

    for (size_t i = 0; i < 6; ++i)
        CHECK_FALSE(*after[i].locked);
}

TEST_CASE("door locks: each move unlocks a locked door and locks an unlocked one, each door at most once") {
    const auto doors = Doors(30, 10);
    const fr::DoorPlan plan = fr::PlanDoorLocks(123, kScene, doors, kMissionBrick, 0.8);
    std::set<size_t> touched;

    for (const fr::DoorMove& m : plan.moves) {
        CHECK(*doors[m.unlock].locked);
        CHECK_FALSE(*doors[m.lock].locked);
        CHECK(touched.insert(m.unlock).second);
        CHECK(touched.insert(m.lock).second);
    }
}

TEST_CASE("door locks: keys stay with their doors") {
    const auto doors = Doors(24, 8);
    const fr::DoorPlan plan = fr::PlanDoorLocks(5, kScene, doors, kMissionBrick, 1.0);
    const auto after = ApplyDoorMoves(doors, plan);

    REQUIRE(after.size() == doors.size());

    for (size_t i = 0; i < doors.size(); ++i)
        CHECK(after[i].key == doors[i].key);
}

TEST_CASE("door locks: eligibility rules") {
    std::vector<fr::DoorInput> doors = Doors(4, 2);

    auto add = [&](fr::DoorInput d) {
        doors.push_back(d);
        return doors.size() - 1;
    };

    fr::DoorInput keypad = Door(100, true);
    keypad.lockpickable = false;
    const size_t iKeypad = add(keypad);

    fr::DoorInput keypadOpen = Door(101, false);
    keypadOpen.lockpickable = false;
    const size_t iKeypadOpen = add(keypadOpen);

    fr::DoorInput mission = Door(102, true, kMissionBrick);
    const size_t iMission = add(mission);

    fr::DoorInput open = Door(103, false);
    open.closed = false;
    const size_t iOpen = add(open);

    fr::DoorInput oneWay = Door(104, false);
    oneWay.openMode = 1;
    const size_t iOneWay = add(oneWay);

    fr::DoorInput disabled = Door(105, false);
    disabled.openMode = fr::kDoorOpenDisabled;
    const size_t iDisabled = add(disabled);

    fr::DoorInput noLock = Door(106, true);
    noLock.lockHandle = 0;
    const size_t iNoLock = add(noLock);

    fr::DoorInput unreadState = Door(107, true);
    unreadState.locked.reset();
    const size_t iUnread = add(unreadState);

    fr::DoorInput gate = Door(108, false);
    gate.doorClass = false;
    const size_t iGate = add(gate);

    fr::DoorInput sharedA = Door(109, true);
    fr::DoorInput sharedB = Door(110, false);
    sharedB.lockHandle = sharedA.lockHandle;
    const size_t iSharedA = add(sharedA);
    const size_t iSharedB = add(sharedB);

    const size_t iDup = add(Door(1, false));

    fr::DoorInput noOpenMode = Door(111, false);
    noOpenMode.openMode.reset(); // unread open mode is not a rule
    const size_t iNoOpenMode = add(noOpenMode);

    const fr::DoorPlan plan = fr::PlanDoorLocks(9, kScene, doors, kMissionBrick, 1.0);

    CHECK(plan.verdicts[iKeypad] == fr::DoorReject::NotLockpickable);
    CHECK(plan.verdicts[iKeypadOpen] == fr::DoorReject::NotLockpickable);
    CHECK(plan.verdicts[iMission] == fr::DoorReject::MissionBrick);
    CHECK(plan.verdicts[iOpen] == fr::DoorReject::Open);
    CHECK(plan.verdicts[iOneWay] == fr::DoorReject::OneWay);
    CHECK(plan.verdicts[iDisabled] == fr::DoorReject::Disabled);
    CHECK(plan.verdicts[iNoLock] == fr::DoorReject::Unreadable);
    CHECK(plan.verdicts[iUnread] == fr::DoorReject::Unreadable);
    CHECK(plan.verdicts[iGate] == fr::DoorReject::NotDoorClass);
    CHECK(plan.verdicts[iSharedA] == fr::DoorReject::SharedLock);
    CHECK(plan.verdicts[iSharedB] == fr::DoorReject::SharedLock);
    CHECK(plan.verdicts[iDup] == fr::DoorReject::Duplicate);
    CHECK(plan.verdicts[iNoOpenMode] == fr::DoorReject::None);

    CHECK(plan.eligible == 5);
    CHECK(plan.eligibleLocked == 2);
    CHECK(plan.untouchedLocked ==
          4); // keypad, mission, no lock entity, shared A (an unread state counts as not locked)
    CHECK(plan.moves.size() == 2);

    // Rejected doors never move.
    const auto after = ApplyDoorMoves(doors, plan);

    for (const size_t i :
         {iKeypad, iKeypadOpen, iMission, iOpen, iOneWay, iDisabled, iNoLock, iGate, iSharedA, iSharedB})
        CHECK(after[i].locked == doors[i].locked);

    CHECK(LockedCount(after) == LockedCount(ApplyDoorMoves(doors, fr::DoorPlan {})));
}

TEST_CASE("door locks: capped by the unlocked doors") {
    const auto doors = Doors(5, 4);
    const fr::DoorPlan plan = fr::PlanDoorLocks(1, kScene, doors, kMissionBrick, 1.0);

    CHECK(plan.capped);
    CHECK(plan.wanted == 1);
    CHECK(plan.moves.size() == 1);
    CHECK(LockedCount(ApplyDoorMoves(doors, plan)) == 4);
}

TEST_CASE("door locks: nothing to do") {
    CHECK(fr::PlanDoorLocks(1, kScene, Doors(5, 0), kMissionBrick, 1.0).moves.empty());
    CHECK(fr::PlanDoorLocks(1, kScene, Doors(5, 5), kMissionBrick, 1.0).moves.empty());
    CHECK(fr::PlanDoorLocks(1, kScene, std::vector<fr::DoorInput> {}, kMissionBrick, 1.0).moves.empty());
}

TEST_CASE("door locks: same brick first") {
    // Brick A: 3 locked, 3 unlocked; brick B: 3 unlocked.
    std::vector<fr::DoorInput> doors = Doors(6, 3, 1, kBrickA);
    const auto b = Doors(3, 0, 50, kBrickB);
    doors.insert(doors.end(), b.begin(), b.end());

    const fr::DoorPlan plan = fr::PlanDoorLocks(42, kScene, doors, kMissionBrick, 1.0);

    REQUIRE(plan.moves.size() == 3);

    for (const fr::DoorMove& m : plan.moves) {
        CHECK(m.tier == fr::DoorTier::Brick);
        CHECK(doors[m.lock].brickRid == kBrickA);
    }

    // Brick A: 3 locked, 1 unlocked: two moves go to brick B.
    std::vector<fr::DoorInput> tight = Doors(4, 3, 1, kBrickA);
    tight.insert(tight.end(), b.begin(), b.end());
    const fr::DoorPlan tightPlan = fr::PlanDoorLocks(42, kScene, tight, kMissionBrick, 1.0);

    REQUIRE(tightPlan.moves.size() == 3);
    CHECK(tightPlan.movesByTier.at(fr::DoorTier::Brick) == 1);
    CHECK(tightPlan.movesByTier.at(fr::DoorTier::Other) == 2);
}

TEST_CASE("door locks: deterministic in (seed, scene), independent of input order") {
    std::vector<fr::DoorInput> doors = Doors(40, 12, 1, kBrickA);
    const auto b = Doors(20, 5, 200, kBrickB);
    doors.insert(doors.end(), b.begin(), b.end());

    const auto plan1 = fr::PlanDoorLocks(77, kScene, doors, kMissionBrick, 0.6);
    const auto plan2 = fr::PlanDoorLocks(77, kScene, doors, kMissionBrick, 0.6);
    const auto locked1 = LockedHandles(doors, ApplyDoorMoves(doors, plan1));
    CHECK(locked1 == LockedHandles(doors, ApplyDoorMoves(doors, plan2)));

    // Reversed input (and other pointers would be other handles: only ids and bricks order them).
    std::vector<fr::DoorInput> reversed(doors.rbegin(), doors.rend());
    const auto plan3 = fr::PlanDoorLocks(77, kScene, reversed, kMissionBrick, 0.6);
    CHECK(locked1 == LockedHandles(reversed, ApplyDoorMoves(reversed, plan3)));

    // Another seed or scene gives another result (for this many doors).
    const auto other = fr::PlanDoorLocks(78, kScene, doors, kMissionBrick, 0.6);
    const auto otherScene =
        fr::PlanDoorLocks(77, "assembly:/_pro/scenes/missions/other.entity", doors, kMissionBrick, 0.6);
    CHECK(locked1 != LockedHandles(doors, ApplyDoorMoves(doors, other)));
    CHECK(locked1 != LockedHandles(doors, ApplyDoorMoves(doors, otherScene)));
}

TEST_CASE("door locks: share draw") {
    for (int32_t seed = 1; seed < 200; ++seed) {
        const double s = fr::DrawDoorLockShare(seed, kScene, {0.33, 1.0});
        CHECK(s >= 0.33);
        CHECK(s <= 1.0);
        const double capped = fr::DrawDoorLockShare(seed, kScene, {0.33, 0.6});
        CHECK(capped >= 0.33);
        CHECK(capped <= 0.6);
    }

    CHECK(fr::DrawDoorLockShare(5, kScene, {0.33, 1.0}) == fr::DrawDoorLockShare(5, kScene, {0.33, 1.0}));
    CHECK(fr::DrawDoorLockShare(5, kScene, {1.0, 1.0}) == 1.0);
    CHECK(fr::ClampDoorLockShares(-1.0, 1.0).min == 0.0);
    CHECK(fr::ClampDoorLockShares(2.0, 2.0).min == 1.0);
    CHECK(fr::ClampDoorLockShares(0.5, 0.2).max == 0.5);
    CHECK(fr::ClampDoorLockShares(std::nan(""), std::nan("")).min == fr::kDefaultDoorLockShareMin);
    CHECK(fr::ClampDoorLockShares(std::nan(""), std::nan("")).max == fr::kDefaultDoorLockShareMax);
}

TEST_CASE("door locks: a watched door unlocks the first time it is seen open") {
    using fr::DoorWatchAction;
    const std::optional<bool> yes = true, no = false, unknown;

    // Closed or unreadable: still watched, whatever the lock reads.
    CHECK(fr::DecideWatchedDoor({no, yes}) == DoorWatchAction::Keep);
    CHECK(fr::DecideWatchedDoor({no, no}) == DoorWatchAction::Keep);
    CHECK(fr::DecideWatchedDoor({unknown, yes}) == DoorWatchAction::Keep);
    CHECK(fr::DecideWatchedDoor({unknown, unknown}) == DoorWatchAction::Keep);

    // Open and locked (an NPC went through): unlock. An unreadable lock counts as locked.
    CHECK(fr::DecideWatchedDoor({yes, yes}) == DoorWatchAction::Unlock);
    CHECK(fr::DecideWatchedDoor({yes, unknown}) == DoorWatchAction::Unlock);

    // Open and already unlocked (lockpicked, a key, the level): nothing to do, stop watching.
    CHECK(fr::DecideWatchedDoor({yes, no}) == DoorWatchAction::Drop);

    const std::vector<fr::DoorWatchRead> reads = {{no, yes}, {yes, yes}, {yes, no}, {unknown, yes}};
    CHECK(fr::DecideWatchedDoors(reads) == std::vector<DoorWatchAction> {DoorWatchAction::Keep, DoorWatchAction::Unlock,
                                                                         DoorWatchAction::Drop, DoorWatchAction::Keep});
    CHECK(fr::DecideWatchedDoors({}).empty());
}

TEST_CASE("door locks: the watch polls a few times a second of game time") {
    fr::DoorWatchClock clock;

    // One second at 64 fps (exact in binary): one poll per kDoorWatchPollSeconds.
    size_t polls = 0;

    for (int i = 0; i < 64; ++i)
        polls += clock.Due(1.0 / 64.0) ? 1 : 0;

    CHECK(polls == static_cast<size_t>(1.0 / fr::kDoorWatchPollSeconds));

    // Paused (no game time) or a bad delta: no poll.
    clock.Reset();
    CHECK_FALSE(clock.Due(0.0));
    CHECK_FALSE(clock.Due(-1.0));
    CHECK_FALSE(clock.Due(std::nan("")));

    // A long frame polls once, not a backlog.
    CHECK(clock.Due(5.0));
    CHECK_FALSE(clock.Due(0.0));

    clock.Reset();
    CHECK_FALSE(clock.Due(fr::kDoorWatchPollSeconds / 2));
}
