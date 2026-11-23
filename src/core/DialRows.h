#pragma once

// Which difficulty each pro.repo entry belongs to, and the overlay's one row
// per parameter built from the per-entry writes.

#include "Guid.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fr {

// Difficulty tiers of a pro.repo entry, as a bit mask (one entry can be in
// more than one collection).
constexpr uint8_t kCasual = 1;       // collection "Easy" (eDL_EASY)
constexpr uint8_t kProfessional = 2; // collection "Default" (eDL_NORMAL): the set Freelancer uses
constexpr uint8_t kMaster = 4;       // collection "Hard" (eDL_HARD)

// A pro.repo entry with a Parameter, as measured in game.
struct RepoEntryInfo {
    uint8_t tiers;                // 0: in no difficulty collection
    int32_t param;                // its Parameter text
    std::optional<int32_t> feeds; // the parameter of the collection entity built from it; empty when in none
    double value;                 // its Value then
};

// The entry by GUID, or empty when the table does not know it (for example
// after a game update rebuilt pro.repo).
std::optional<RepoEntryInfo> FindRepoEntry(const Guid& guid);

// The Values of the professional entries whose Parameter is param (usually one).
std::vector<double> ProfessionalRepoValues(int32_t param);

// "casual", "professional", "master", joined with '+' ("professional+master"),
// or "other" for 0.
std::string RepoTierLabel(uint8_t tiers);

// One written value: a pro.repo entry, or an entity (an uncollected entity
// or a professional member written while loading, or the scene-start fallback).
struct DialEntry {
    int32_t param;
    std::string copy;             // "pro.repo <guid prefix>", "entity" or "uncollected entity"
    std::optional<uint8_t> tiers; // empty: tier unknown (entry not in the table, entity without a repository id)
    std::optional<int32_t> feeds; // the collection entity's parameter (an entity's own parameter)
    double base;
    double multiplier;
    double written;
    std::string status;
    bool uncollected = false; // an entity in no difficulty collection (probably the value Freelancer reads
                              // when no professional entity has the parameter; unverified)
};

// How the parameter's Freelancer (professional) value turned out.
enum class FreelancerValue {
    Written,     // one professional entry chosen: before/after
    Ambiguous,   // several professional entries and none stands out
    Uncollected, // no professional entry, but the parameter's uncollected entities were written (probably
                 // what the game reads; unverified): before/after of the first, all in the text
    Zero,        // its professional entry is 0, so it was not written (unchanged)
    TierUnknown, // no professional entry written, but an entry of unknown tier was: before/after is that entry's
    Missing,     // no professional entry at all
};

// One parameter's writes.
struct DialSummary {
    int32_t param;
    double multiplier;      // the chosen entry's, else the first entry's
    bool multipliersDiffer; // some entry has another multiplier
    FreelancerValue freelancer;
    size_t professionalEntries;     // written professional entries
    size_t uncollectedEntries = 0;  // written uncollected entities
    std::optional<double> before;   // Written, Uncollected and TierUnknown: the entry's base
    std::optional<double> after;    // and its written value
    std::string status;             // the chosen entry's; else the status all entries share; else "mixed"
    std::vector<DialEntry> entries; // input order
};

// One summary per parameter, in the order each parameter first appears. The
// Freelancer entry is the only professional one, or else the only professional
// one whose collection entity is of this parameter (pro.repo has a cooldown
// entry named for one flashbang parameter that feeds another); without one,
// the first uncollected entity written.
std::vector<DialSummary> SummarizeDials(std::span<const DialEntry> entries);

// The roll almost certainly changes nothing in Freelancer (Zero or Missing).
bool NoEffect(const DialSummary& summary);

// How the overlay shows a parameter's row. coveredParams: the parameters a
// written sensor dial that still holds stands in for this mission
// (SensorDialParam). A row whose roll has no effect (NoEffect) but whose
// parameter such a dial covers is not shown: the sensor dial's row has the
// value the guards get. Any other NoEffect row is marked "(no effect?)".
struct DialRowView {
    bool shown = true;
    bool noEffect = false;
};

DialRowView ViewDialRow(const DialSummary& summary, std::span<const int32_t> coveredParams);

// The Freelancer value as the overlay shows it: "60 -> 120.1" (no unit; the
// overlay has a unit column), "2 professional entries", "5 -> 6.2 (entity,
// unverified)" or "7 -> 3.5, 6 -> 3 (2 entities, unverified)" for uncollected
// entities (each written entity, in input order), "professional
// entry is 0 (unchanged)", "60 -> 120.1 (tier unknown)" or "no professional entry".
// withUnit adds the unit as FormatDialChange does ("60 -> 120.1 m (?)"), for the log.
std::string FreelancerValueText(const DialSummary& summary, bool withUnit);

} // namespace fr
