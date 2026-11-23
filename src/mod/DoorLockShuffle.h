#pragma once

// Door lock shuffle (door_locks, on by default).
//
// What holds a door's lock (HITMAN 3's door templates access_helper_singledoor
// and access_helper_doubledoor, read from their QuickEntity dumps): the door
// entity, ZHM5SingleDoor2 or ZHM5DoubleDoor2, has m_rDoorLocked, a reference
// to the ZValueBool "VB_Locked" (m_bValue: locked) inside the template's
// Access_Helper_DoorLogic; the door template's m_bIsLocked is an alias of that
// value. Its sibling "Can use lockpick" (m_bValue, the template's
// m_CanUseLockpick) gates the lockpick prompt: keycard and keypad doors have it
// false (they open through their reader's UnlockDoor). The door's input pins
// LockDoor and UnlockDoor set the lock at play time. A key is matched by the
// keywords of the DoorLogic's RequiredKey, which this never touches.
//
// In a Freelancer mild mission or showdown, one walk of the loaded bricks (at
// eLoading_SceneAllocated, or eLoading_SceneStarted when there are no doors
// yet) finds every door, its lock value, lockpick and crowbar flags, open
// state and open mode. With the shuffle on and a known
// seed, fr::PlanDoorLocks (core/DoorLocks.h) picks which locked lockpickable
// doors unlock and which unlocked ones lock (same count). When the scene
// starts playing, UnlockDoor / LockDoor are signaled on the door entities (the
// game's own lock logic), then the lock values are read back and, for a door
// whose value did not follow the pin, written directly. Every write is logged
// with its read-back; the locked count is logged before and after.
//
// Reopening: every door this mod locked is watched (its open value polled
// every fr::kDoorWatchPollSeconds of game time from the frame update). The
// first time one is seen open, by an NPC or the player, it is unlocked
// (UnlockDoor, with the same value fallback) and no longer watched: it stays
// unlocked for the rest of the mission, as a lockpicked door does in vanilla,
// so nobody follows an NPC through it and is shut in behind it. Doors this
// mod unlocked are not watched. Door log lines name the door by its template
// id (shared by every door of that template), entity pointer (unique in a
// load) and position.
//
// Lockpick grant (lockpick_grant, nails by default, independent of
// door_locks; pure part and the door logic's wiring in
// core/LockpickFree.h; RandomLoadout gives the items): the same walk also
// finds each door's lockpick subaction, its DoorLogic and its "Unlock" group.
// The doors stay vanilla. The pin hook sees each pick's start and the
// DoorLogic's LockpickCompleted; with slow_lockpick, a pick done with only the
// given lockpick plays as normal but has its LockpickCompleted held back and
// signaled again later, so the lock takes lockpick_slow_picks times as long
// to open, timed from the lockpick's choice in the Unlock menu. While held,
// the door's "Animating" is true (as during a normal pick), so the Unlock
// entry is hidden and offers no second pick; "Can use lockpick" is set off
// only as a fallback.

#include "DoorLocks.h"
#include "LockpickFree.h"
#include "Seed.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class DoorLockShuffle {
public:
    // At config load. p_On: the shuffle; p_ShareMin, p_ShareMax: the least and most share
    // of locked doors swapped (door_locks_share_min, _max).
    void Configure(bool p_On, double p_ShareMin, double p_ShareMax);

    // Game thread, from the SetLoadingStage detour. Catches and logs whatever
    // its body throws.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                        const std::optional<fr::SeedResolution>& p_Seed) noexcept;

    // Game thread, at a scene clear: forgets the load (not what was written)
    // and logs the reopening summary.
    void OnSceneClear() noexcept;

    // Game thread, every play-mode frame: polls the watched doors when due.
    void Tick(double p_Seconds) noexcept;

    // lockpick_grant, at config load. p_Prepare finds the world inventory
    // (behind the loading screen, game thread); p_HeroLockpicks counts 47's
    // own and given lockpicks (empty with why when it cannot tell), on the
    // game thread and from the pin hook.
    using PrepareInventory = std::function<void()>;
    using HeroLockpicks = std::function<std::optional<fr::LockpickCarry>(std::string*)>;
    void ConfigureLockpickFree(fr::LockpickGrantMode p_Mode, int32_t p_SlowPicks, PrepareInventory p_Prepare,
                               HeroLockpicks p_HeroLockpicks);

    // SignalOutputPin detour (hot: one relaxed load when nothing is armed;
    // then a locked lookup only for the five lockpick pins).
    bool LockpickPinsOn() const { return m_LpArmed.load(std::memory_order_relaxed); }
    // True: block this signal (a free pick's LockpickCompleted, given later).
    bool OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept;

    // One door found by the walk, with the entities its state lives in.
    struct Door {
        ZEntityType** entity = nullptr;
        uint64_t id = 0;
        uint64_t brickRid = 0;
        bool doubleDoor = false;
        ZEntityType** lock = nullptr;     // m_rDoorLocked
        ZEntityType** lockpick = nullptr; // "Can use lockpick"
        std::optional<bool> locked;
        std::optional<bool> lockpickable;
        std::optional<bool> crowbarable;
        ZEntityType** openValue = nullptr;   // m_rDoorOpen
        std::optional<bool> open;            // m_rDoorOpen's value
        std::optional<int32_t> initialState; // m_InitialState
        std::optional<int32_t> openMode;     // m_OpenMode
        // lockpick_grant: the door's DoorLogic and its interaction parts.
        ZEntityType** logic = nullptr;       // the DoorLogic instance
        ZEntityType** useLockpick = nullptr; // IS_UseLockpick
        std::vector<ZEntityType**> useLockpickKids;
        ZEntityType** lockpickValue = nullptr;    // the entity holding "Can use lockpick"'s m_bValue
        ZEntityType** animating = nullptr;        // "Animating" (ZValueBoolBasic): true hides the Unlock entry
        ZEntityType** unlockVisible = nullptr;    // InteactionGroupVisible (the Unlock group's visible condition)
        ZEntityType** unlockConditions = nullptr; // ConditionListUnlockDoor
        std::string why;                          // why the lock state was not read
    };

private:
    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                      const std::optional<fr::SeedResolution>& p_Seed);
    // False when there is nothing to walk yet (tried again at the next stage).
    bool Run(ZEntitySceneContext* p_Context, const std::optional<fr::SeedResolution>& p_Seed, const char* p_Stage);
    void ApplyPins();
    // Whether the doors this load changed still hold the mod's value.
    void CheckAtPlaying();
    // Starts watching a door this mod locked (index into m_Doors).
    void Watch(size_t p_Door);
    void LogWatching();
    void PollWatch();

    // lockpick_grant (the end of DoorLockShuffle.cpp).
    void ApplyLockpickFree();
    void LockpickTick(double p_Seconds);
    void LockpickSceneClear();
    void ReleaseLockpick(size_t p_Door);
    // A held door's Unlock entry hidden (Animating true; "Can use lockpick"
    // off as a fallback), kept hidden each frame, and shown again (game thread).
    std::string SetLockpickFlag(ZEntityType** p_Value, bool p_On);
    std::string SetAnimating(ZEntityType** p_Entity, bool p_Value, std::optional<bool>* p_Back);
    void HideLockpick(size_t p_Door);
    void KeepAnimating();
    void ShowLockpick(size_t p_Door, const char* p_When);
    void ShowAllLockpicks(const char* p_When);

    bool m_On = false;
    fr::ShareRange m_Shares {fr::kDefaultDoorLockShareMin, fr::kDefaultDoorLockShareMax};

    // This load.
    bool m_Done = false;
    std::string m_Scene;
    std::vector<Door> m_Doors;
    std::vector<fr::DoorMove> m_Pending; // applied at scene playing
    std::vector<size_t> m_Written;       // doors written this load (indices)

    // Reopening, this load: the doors this mod locked, until first seen open.
    struct Watched {
        size_t door;        // index into m_Doors
        uintptr_t openSlot; // m_rDoorOpen's m_bValue
        uintptr_t lockSlot; // VB_Locked's m_bValue
    };
    std::vector<Watched> m_Watch;
    fr::DoorWatchClock m_WatchClock;
    double m_PlayingSeconds = 0.0; // game time since the watch started
    size_t m_Watched = 0;          // doors watched this load
    size_t m_Reopened = 0;         // unlocked when seen open
    size_t m_OpenedUnlocked = 0;   // seen open already unlocked
    size_t m_ReopenFailed = 0;     // seen open, unlock failed

    // Across loads: what this mod wrote, by door and lock entity. A scene
    // whose doors still hold all of them was not rebuilt and is left alone.
    struct Written {
        ZEntityType** door;
        ZEntityType** lock;
        bool value; // false again once the watch unlocked it
    };
    std::vector<Written> m_LastWritten;

    // lockpick_grant.
    bool m_LockpickFree = false; // any mode but off
    int32_t m_LockpickSlowPicks = fr::kDefaultLockpickSlowPicks;
    PrepareInventory m_PrepareInventory;
    std::atomic<bool> m_LpArmed {false};
    std::mutex m_LpMutex; // guards everything below (pin hook and game thread)
    // Written under the lock; the game thread may read them without it.
    fr::LockpickGrantMode m_LpMode = fr::LockpickGrantMode::Off;
    HeroLockpicks m_HeroLockpicks;
    std::unordered_map<ZEntityType**, size_t> m_LpSubactions; // lockpick subaction (root and inner) -> door
    std::unordered_map<ZEntityType**, size_t> m_LpLogic;      // DoorLogic -> door
    std::unordered_map<size_t, std::string> m_LpDoorLines;    // door -> its log name (the hook never reads m_Doors)
    std::unordered_map<size_t, double> m_LpLastStart;         // door -> clock of its last start
    fr::LockpickTiming m_LpTiming;
    double m_LpClock = 0.0; // game seconds since the scene started playing
    struct LpVerify {
        size_t door;
        double at;
        const char* what;
    };
    std::vector<LpVerify> m_LpVerify;
    // Held doors whose Unlock entry this mod hid (game thread only): Animating
    // set true, and/or "Can use lockpick" set off (the fallback).
    struct LpHidden {
        size_t door;
        ZEntityType** animating = nullptr; // set true by this mod (SetFalse due)
        ZEntityType** value = nullptr;     // "Can use lockpick" set off by this mod
    };
    std::vector<LpHidden> m_LpHidden;
    fr::AnimatingResets m_LpAnimating; // the hidden doors' Animating, re-set when it reads false
    size_t m_LpMade = 0, m_LpStarted = 0, m_LpHeld = 0, m_LpPassed = 0, m_LpReleased = 0, m_LpLines = 0;
};
