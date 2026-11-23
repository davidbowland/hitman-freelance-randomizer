#pragma once

// Gear wall loans (random_loadout): every item the random loadout takes from
// the gear wall (CPD PersistentItems) comes back to the wall after the
// mission, whatever happens to it there.
//
// How: the mod never takes a wall item off the wall. The mission's
// "Persistent items" list is what the mission saves, so an item 47 drops,
// throws, places, loses or uses up stays on the wall. An item 47 carries out
// is moved by the game itself: into InventoryItems and off the wall (never
// both), so nothing is doubled.
// Freelancer tools (TransientItems) keep the vanilla rule: taken off the tools
// list once 47 holds them, lost unless carried out.
//
// The loans are a safety net on top: each wall item given is remembered (in
// the state file, so a crash or quit keeps it) until a CPD shows it back on the
// wall. While it sits in 47's loadout (carried out) the loan stays open. If it
// is ever in neither list (lost by some other path), the mod
// pushes it back onto the mission's "Persistent items" list at the next
// Freelancer mission (the pin the game's own "Add persistent item" drives),
// which that mission's CpdSet saves; at most kMaxLoanRepairs departures.

#include "Guid.h"
#include "Loadout.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// After this many departures with a push sent, a still-missing item is given up (logged).
inline constexpr int32_t kMaxLoanRepairs = 3;

// A source list whose entry the mod removes once 47 holds the item: the
// Freelancer tools only. A gear wall item is never removed (it always comes
// back; the game moves it to the loadout itself when it is carried out).
bool RemovesFromSource(LoadoutSource source);

struct WallLoan {
    Guid id;
    std::string name;                // for the log (single line, no commas needed)
    int32_t repairs = 0;             // departures with a push back onto the wall
    int64_t lastRepairDeparture = 0; // the departure snapshot's updatedUnix of the last push
};

struct WallLoans {
    std::vector<WallLoan> loans;

    const WallLoan* Find(const Guid& id) const;
    // Adds a loan (once per id; a second add keeps the first). True when new.
    bool Add(const Guid& id, std::string_view name);
};

// Where a loaned item is by a CPD state.
enum class LoanStatus : uint8_t {
    OnWall,    // in PersistentItems (back on the wall): the loan is closed
    InLoadout, // in InventoryItems only (carried out): the loan stays open
    Missing,   // in neither: pushed back onto the wall at the next mission
    Unknown,   // no gear wall list in the state, or an empty one (a wiped wall is never refilled)
};

LoanStatus JudgeLoan(const LoadoutState& state, const Guid& id);
const char* LoanStatusName(LoanStatus status);

// One loan after a review.
struct LoanOutcome {
    WallLoan loan;
    LoanStatus status = LoanStatus::Unknown;
    bool closed = false; // removed from the loans (OnWall, or given up)
    bool repair = false; // push it onto "Persistent items" in this mission (only with a departure)
    bool gaveUp = false; // missing after kMaxLoanRepairs departures with a push
};

// Reviews every loan against a CPD state: OnWall closes it. With
// departureUnix (a mission load: the departure snapshot's updatedUnix), a
// Missing loan gets repair = true and counts one more repair when this
// departure differs from its last (a restart of the same departure pushes
// again without counting); past kMaxLoanRepairs it is given up and closed.
// Without it (a safehouse load) nothing is repaired or counted.
std::vector<LoanOutcome> ReviewLoans(WallLoans& loans, const LoadoutState& state, std::optional<int64_t> departureUnix);

// The loans in the state file: one "loan=<guid>,<repairs>,<last departure>,<name>" line each, and back
// (other lines are ignored, malformed ones skipped).
std::string FormatWallLoans(const WallLoans& loans);
WallLoans ParseWallLoans(std::string_view text);

// The plain log line for a review outcome ("loadout: ..."), for the player.
std::string LoanOutcomeText(const LoanOutcome& outcome);

// --- The return at the exit -------------------------------------------------
//
// A lent item 47 carries out comes back in the player's loadout: the
// end-of-mission CpdSet saves it in InventoryItems and takes it off
// PersistentItems, so the safehouse has it packed and the player has to take
// it out by hand. (AwardedItems is not involved: it holds wall items too, most
// never lent: Luxurious and Ancestral sets, collection items.)
//
// So from EvergreenExitTriggered until the mission's CpdSet, every
// kExitRemoveEverySeconds (as the slow lockpick's exit rounds, which keep a
// carried item out of the save), each lent item
// gets: RemoveRepositoryId on "Inventory items", and RemoveRepositoryId then
// PushRepositoryId on "Persistent items". After a round the wall holds
// max(n, 1) of it, whatever n was (before or after the game takes it off), so
// a round never doubles it; the loadout holds none. Count-safe only with one
// copy in play, hence the skips below.

enum class WallReturnPlan : uint8_t {
    Return,       // send the rounds
    NotDelivered, // never given to 47 in this mission
    Packed,       // the player packed this id too (not the mod's to move)
    NotOnWall,    // not on the wall at departure (nothing to return it to)
    WallCopies,   // two or more on the wall: a round would leave one
    ExtraCopy,    // 47 picked up another copy in the mission (the remove could take his)
};

struct WallReturnInput {
    bool delivered = false; // the mod gave it to 47 in this mission
    size_t packed = 0;      // copies in the departure's InventoryItems
    size_t wall = 0;        // copies in the departure's PersistentItems
    size_t heroCopies = 0;  // 47's copies by the item events (the lent one included)
};

WallReturnPlan PlanWallReturn(const WallReturnInput& input);
// For the log: "returned at the exit", "not returned: ..." (plain).
std::string WallReturnPlanText(WallReturnPlan plan);

// The mission's CpdSet after the rounds.
enum class WallReturnVerdict : uint8_t {
    Back,      // the wall has its departure count, the loadout no new copy
    InLoadout, // still saved in the loadout, off the wall (the save came before a round)
    Missing,   // in neither (the loan puts it back at the next mission)
    Doubled,   // more on the wall than at departure, or on the wall and in the loadout
    Partial,   // a list missing from this CpdSet
};

WallReturnVerdict JudgeWallReturn(size_t wall, size_t packed, std::optional<size_t> savedWall,
                                  std::optional<size_t> savedInventory);
const char* WallReturnVerdictText(WallReturnVerdict verdict);

// Copies of id in list (0 for none or no list).
size_t CountOf(const std::vector<Guid>* list, const Guid& id);

} // namespace fr
