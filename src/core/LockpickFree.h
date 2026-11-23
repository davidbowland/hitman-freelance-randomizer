#pragma once

// Pure part of the lockpick grant (lockpick_grant, nails by default; glue in
// mod/RandomLoadout.cpp, which gives the items, and mod/DoorLockShuffle.cpp,
// which times the picks).
//
// What the door logic template (access_helper_doorlogic, 00376B0AC9245C86,
// read from its QuickEntity dump) does for a lockpick: the subaction
// IS_UseLockpick (6a8ea3ece8fadd40) inside the "Unlock" inventory group
// DoorSubaction (5b51167e20a6ed53) is usable when 47 carries an item with
// the lockpick keyword (HeroKeywordCondition01, which also counts items
// hidden in the inventory) and the door's "Can use lockpick" is true. The
// entry names its item: m_RequiredItem, the repository key
// b39d7a0d-e839-417b-880c-cf9b165d4e11 ("Tool_Lockpick", the classic
// lockpick). The pick's length is the animation (hm_interact_lockpick_90cm):
// its AnimEvent_Generic00 starts two timers (200 + 300 ms) whose Out is
// forwarded as the DoorLogic's LockpickCompleted output, wired by the door
// template to the door's UnlockDoor. No property sets the pick's duration.
//
// Why items are given instead of doors changed: pointing the lockpick
// subaction's usable condition at its visible one and clearing its item
// requirements makes the "Unlock" menu's lockpick entry show the weapon in
// 47's hand (an entry with no item and no item keywords takes any item).
// So the doors stay vanilla, and 47 is given a lockpick the game knows:
//
// slow_lockpick: one lockpick, always Tool_Lockpick (kSlowLockpickId: the
// main game's classic lockpick and the very item the
// door's IS_UseLockpick names as m_RequiredItem, so it works on every
// lockpickable door; no Freelancer supplier, crate or loadout offers it; Freelancer's own are Lockpick Mk III d3387f28, which is
// also the main game's Lockpick MK III, and the Collector's Lockpick
// 3c52b8c3, which 47 brings himself: the random loadout's lockpick first takes
// them from the gear wall). With it, a lock takes lockpick_slow_picks (N)
// times as long to open: 47's pick cannot be slowed or restarted from code, so
// a pick done while he carries no lockpick but the given one plays as normal
// and the DoorLogic's LockpickCompleted is held back (N - 1) x the time from
// the pick's start, then signaled again so the door's UnlockDoor fires that
// much later (LockpickTiming). The start is when 47 chose the lockpick in the
// Unlock menu (IS_UseLockpick's ItemUsed), not the animation's Started, which
// comes about 1.2 s later (timed from Started, a doubled lock takes 1.7x, not
// 2x); with no ItemUsed in the kLockpickChoiceToStartSeconds before Started,
// the Started time. While a completion is held, the door's "Animating" bool
// is set true (its SetTrue input pin, so the conditions that read it are told)
// as the vanilla key, lockpick and crowbar actions do while they run: the
// Unlock entry's visible condition (InteactionGroupVisible, all of
// ConditionListUnlockDoor, which is none of 8 conditions, Animating among them,
// and HasNoKeyOrIsVR) is then false and the whole Unlock entry hides, as during
// a normal pick. Vanilla clears Animating at the timer's Out that becomes
// LockpickCompleted and at the subaction's Completed, so it is set after those
// (shortly after the hold begins, see kLockpickHideDelaySeconds), set again
// each frame it reads false during the hold (AnimatingResets), and SetFalse
// just before the completion is signaled again, at a fault and when a hold is
// dropped at the scene's end. The door's own "Can use lockpick" (which only
// grays the menu's lockpick entry: the entry stays selectable, with only the
// crowbar) is set false only as a fallback: when Animating was not found, or
// the Unlock entry's conditions still read true after it was set
// (LockpickFlagFallback). With any other lockpick
// (packed, found, bought, nails) a pick is vanilla. The given item is told
// apart by the item ledger, not its id.
//
// nails: kRustyNail ("Tool_Rusty_Nail_OneTimeUse", "Rusty Old Nail" in game,
// a vanilla one-use lockpick from Freelancer's stash crates), N of them, each
// its own item; the game uses one up per pick. No slowdown, and no save
// protection: vanilla nails never come back to the safehouse (the save is
// still logged).
//
// Either is given only when 47 carries no lockpick-like item. The slow
// lockpick never becomes his: while it exists, the mission's "Inventory
// items" list (the InventoryItems the mission saves) takes no
// PushRepositoryId or RemoveRepositoryId of its id (GrantListBlocks).

#include "Guid.h"
#include "PinIds.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Sub-entity ids in access_helper_doorlogic (children of the DoorLogic instance).
inline constexpr uint64_t kDoorLogicUseLockpickId = 0x6a8e'a3ec'e8fa'dd40ull; // IS_UseLockpick
// IS_UseLockpick's output when 47 chooses the lockpick in the Unlock menu:
// kPinItemUsed (PinIds.h).
// "Animating" (ZValueBoolBasic): true while a key, lockpick or crowbar action
// runs; read only by ConditionListUnlockDoor. Same parent as "Can use lockpick".
inline constexpr uint64_t kDoorLogicAnimatingId = 0xfcbf'4e21'905e'891bull;
// The Unlock group's m_rVisibleCondition (ZValueBool_Operation_Signal, ALL).
inline constexpr uint64_t kDoorLogicUnlockVisibleId = 0x02ec'9363'5839'56bcull; // InteactionGroupVisible
// Its first operand (ZValueBool_Operation_Signal, NONE over 8 conditions incl. Animating).
inline constexpr uint64_t kDoorLogicUnlockConditionsId = 0x13b1'dc7a'eb14'068eull; // ConditionListUnlockDoor
// A held door's Animating re-set during the hold is logged at most this often (the first at once).
inline constexpr double kAnimatingResetLogSeconds = 2.0;

// lockpick_slow_picks: how many times as long a lock takes with the slow lockpick.
inline constexpr int32_t kDefaultLockpickSlowPicks = 2;
inline constexpr int32_t kMinLockpickSlowPicks = 1;
inline constexpr int32_t kMaxLockpickSlowPicks = 4;
// A Started-to-LockpickCompleted span outside (0, this] is not a pick.
inline constexpr double kMaxLockpickSpanSeconds = 30.0;
// Holds shorter than this are not worth holding.
inline constexpr double kMinLockpickHoldSeconds = 0.05;
// A Started with no LockpickCompleted after this long is forgotten (canceled pick).
inline constexpr double kLockpickStartedExpirySeconds = 30.0;
// A Started this long or less after the door's ItemUsed is timed from the
// ItemUsed (the logged gap is 1.2 s); otherwise from the Started.
inline constexpr double kLockpickChoiceToStartSeconds = 3.0;
// A held door's Unlock entry is hidden (Animating set true) this long after
// the hold begins, or at once when the lockpick subaction's Completed comes
// first (it came 0.22 s after LockpickCompleted): not while the pick's
// animation still runs, and after vanilla's own clears of Animating.
inline constexpr double kLockpickHideDelaySeconds = 0.5;

// The items given (pro.repo ids).
inline constexpr std::string_view kSlowLockpickId = "b39d7a0d-e839-417b-880c-cf9b165d4e11"; // Tool_Lockpick
inline constexpr std::string_view kRustyNailId = "14bf3a3c-54c7-4c98-9452-1e7f67da09bf";    // Rusty Old Nail

enum class LockpickGrantMode : uint8_t { Off, SlowLockpick, Nails };

// "off", "slow_lockpick", "nails" (any case, trimmed); empty for anything else.
std::optional<LockpickGrantMode> ParseLockpickGrantMode(std::string_view text);
const char* LockpickGrantModeName(LockpickGrantMode mode);
// The pro.repo id given in the mode by default (empty for Off).
std::string_view LockpickGrantItemId(LockpickGrantMode mode);
// Only the slow lockpick slows a pick.
bool LockpickGrantSlows(LockpickGrantMode mode);
// Only the slow lockpick is kept out of the save (nails never persist).
bool LockpickGrantGuardsSave(LockpickGrantMode mode);

inline constexpr int32_t kDefaultLockpickNails = 5;
inline constexpr int32_t kMinLockpickNails = 1;
inline constexpr int32_t kMaxLockpickNails = 20;
int32_t ClampLockpickNails(int32_t nails);

// How many items the mode gives (0 for Off).
int32_t LockpickGrantCount(LockpickGrantMode mode, int32_t nails);

// A lockpick-like pro.repo item by its names (Title / CommonName): "lockpick",
// "lock pick", "lock_pick" or "rusty nail" / "rusty_nail" (any case). Used to
// list the candidates in the log and to tell whether 47 carries one.
bool IsLockpickLikeName(std::string_view title, std::string_view commonName);

// 47's lockpick-like items at one moment: his own and the given ones.
struct LockpickCarry {
    size_t own = 0;
    size_t given = 0;
};

// A pick is a free one (the slow lockpick's, held back) when 47 carries a
// given lockpick and none of his own; empty when the carry is unknown.
std::optional<bool> IsFreePick(const std::optional<LockpickCarry>& carry);

// For the log: "unknown", "with the given lockpick only", "with his own
// lockpick (vanilla)" or "no lockpick seen (vanilla)".
const char* LockpickCarryVerdict(const std::optional<LockpickCarry>& carry);

// The game's item events for 47 (ZAchievementManagerSimple::OnEventSent).
enum class HeroItemEvent : uint8_t { PickedUp, Removed, Dropped, Thrown };

// "ItemPickedUp", "ItemRemovedFromInventory", "ItemDropped", "ItemThrown"; empty for any other name.
std::optional<HeroItemEvent> ParseHeroItemEvent(std::string_view name);

// A removal of another kind of the same id within this many seconds (the
// events' Timestamp) is the same removal: a throw sends ItemRemovedFromInventory
// and ItemThrown together.
inline constexpr double kHeroItemSameRemovalSeconds = 1.0;

// 47's items by the game's own events, per repository id: why the world
// inventory scan is not enough (ZWorldInventory::m_aItems reads 0 entries in
// every mission, so the scan sees no item). An item the mod knows reaches him (packed, the random loadout's
// CreateItem, the grant's: Expect) counts at once, and its ItemPickedUp, when
// one comes, is not counted again (a CreateItem'd item may send none: the
// Collector's Lockpick never did). Any other ItemPickedUp counts one more; a
// removal (ItemRemovedFromInventory, ItemDropped or ItemThrown, one per
// removal) one less, given items first (then a later pickup of the id is a
// given one coming back), never below 0. Unknown counts err toward "his
// own": a lockpick never seen to leave stays counted.
class HeroItemLedger {
public:
    void Clear() { m_Items.clear(); }

    // An item the mod knows is about to reach 47; p_Given: one the grant gives.
    void Expect(const Guid& id, bool given = false);
    // An Expect whose CreateItem faulted.
    void Retract(const Guid& id, bool given = false);
    // An item event at p_At seconds (the event's Timestamp).
    void Event(HeroItemEvent event, const Guid& id, double at);

    size_t Own(const Guid& id) const;
    size_t Given(const Guid& id) const;
    // Given items whose ItemPickedUp has not come (yet).
    size_t GivenPending(const Guid& id) const;

    // 47's items with an id isLockpick accepts (his lockpicks, or another
    // kind's items for the item grants): own = not given; given = given ones.
    LockpickCarry Carry(const std::function<bool(const Guid&)>& isLockpick) const;

private:
    struct Entry {
        size_t own = 0;
        size_t ownPending = 0; // own items counted whose ItemPickedUp has not come
        size_t given = 0;
        size_t givenPending = 0;
        size_t givenOut = 0; // given items that left him (a pickup of the id brings one back)
        std::optional<HeroItemEvent> lastRemoval;
        double lastRemovalAt = 0;
    };

    std::map<Guid, Entry> m_Items;
};

// Whether the hook blocks an "Inventory items" pin: a PushRepositoryId or
// RemoveRepositoryId whose id is the given one, while given items exist this
// scene (from just before the first CreateItem to the scene's end).
bool GrantListBlocks(bool givenThisScene, bool pushOrRemove, bool sameId);

// kMinLockpickSlowPicks to kMaxLockpickSlowPicks.
int32_t ClampLockpickSlowPicks(int32_t picks);

// How long to hold LockpickCompleted back: (picks - 1) x the span (from the
// lockpick's choice to LockpickCompleted), so that from the choice the lock
// takes picks x the span to open; 0 when the span is not a pick or the hold
// would be shorter than kMinLockpickHoldSeconds.
double LockpickHoldSeconds(double startedAt, double completedAt, int32_t picks);

// The picks in progress and the held completions, by door index, on one game
// clock (seconds). A lock picked with the given lockpick takes `picks` times
// as long to open.
class LockpickTiming {
public:
    explicit LockpickTiming(int32_t picks = kDefaultLockpickSlowPicks) : m_Picks(ClampLockpickSlowPicks(picks)) {}

    // 47 chose the lockpick for door p_Door in the Unlock menu (ItemUsed).
    void Chosen(size_t door, double now);

    // The lockpick subaction of door p_Door started; p_Free: a free pick
    // (IsFreePick; empty: unknown, treated as not free). Timed from the
    // door's choice when it came at most kLockpickChoiceToStartSeconds
    // before (returned), else from now (empty).
    std::optional<double> Started(size_t door, double now, std::optional<bool> free);

    struct Completion {
        bool hold = false;             // block this LockpickCompleted; Release gives it later
        double holdSeconds = 0;        // when hold
        bool seenStart = false;        // a Started of this door was pending
        bool free = false;             // the pick was a free one
        bool freeAtCompletion = false; // free was decided by p_FreeNow (else by the start's check)
        double span = 0;               // the start to now, when seenStart
        bool fromChoice = false;       // the start is the lockpick's choice (else the Started)
    };

    // Door p_Door's LockpickCompleted output, about to pass. p_FreeNow: a free
    // pick, checked now (at pick time); it decides when known, else the
    // start's check does (empty there too: not free).
    Completion Completed(size_t door, double now, std::optional<bool> freeNow = std::nullopt);

    // The held completions due at p_Now (each given once), in hold order.
    std::vector<size_t> Due(double now);

    // Starts with no completion for longer than kLockpickStartedExpirySeconds
    // (old choices are forgotten too, silently).
    std::vector<size_t> Expired(double now);

    // The lockpick subaction of door p_Door completed: its hide, when one is
    // pending, is due now.
    void SubactionDone(size_t door, double now);

    // The held doors whose Unlock entry is due to be hidden at p_Now (each
    // given once; a door released first is never given).
    std::vector<size_t> HidesDue(double now);

    bool IsHeld(size_t door) const;
    size_t Held() const { return m_Held.size(); }
    void Clear();

private:
    struct Start {
        size_t door;
        double at;
        std::optional<bool> free;
        bool fromChoice;
    };

    struct Choice {
        size_t door;
        double at;
    };

    struct Hold {
        size_t door;
        double releaseAt;
    };

    struct Hide {
        size_t door;
        double at;
    };

    int32_t m_Picks;
    std::vector<Choice> m_Chosen;
    std::vector<Start> m_Started;
    std::vector<Hold> m_Held;
    std::vector<Hide> m_Hides;
};

// What the Unlock entry's conditions read (their cached m_bValue) right after
// a held door's Animating was set true: Hidden when either reads false and
// none true, StillShown when either reads true, Unknown when neither was read.
enum class UnlockEntryState : uint8_t { Hidden, StillShown, Unknown };
UnlockEntryState UnlockEntryAfterAnimating(std::optional<bool> groupVisible, std::optional<bool> conditionList);
const char* UnlockEntryStateName(UnlockEntryState state);

// Whether a held door's "Can use lockpick" is set false as well: only when
// Animating could not be set true (not found, or the set did not take) or the
// entry's conditions still read true after it was.
bool LockpickFlagFallback(bool animatingSet, UnlockEntryState entry);

// The held doors whose Animating this mod set true, by door index: each frame
// of the hold its value is read; one that reads false (something cleared it)
// is set true again. Game thread only.
class AnimatingResets {
public:
    // SetTrue was sent to door p_Door's Animating (a new hold starts its count again).
    void Start(size_t door);

    struct Step {
        bool setTrue = false; // send SetTrue again
        bool log = false;     // log this re-set (the first at once, then at most every kAnimatingResetLogSeconds)
        size_t resets = 0;    // re-sets of this door this hold, this one included
        size_t unlogged = 0;  // re-sets since the last logged one, this one included (when log)
    };

    // Door p_Door's Animating read p_Value at p_Now (empty: unreadable, no re-set).
    Step Read(size_t door, std::optional<bool> value, double now);

    // Stops watching door p_Door (release, fault, scene end): its re-set
    // count, or empty when it was not watched (then no SetFalse is due).
    std::optional<size_t> Stop(size_t door);

    std::vector<size_t> Doors() const;
    size_t Size() const { return m_Doors.size(); }
    void Clear() { m_Doors.clear(); }

private:
    struct Entry {
        size_t door;
        size_t resets = 0;
        size_t unlogged = 0;
        std::optional<double> lastLogAt;
    };

    std::vector<Entry> m_Doors;
};

} // namespace fr
