#pragma once

// Pure logic of the merces payouts: the known Freelancer payout amounts, line caps, which sent events are payout
// events (with the JSON helpers to read them), the payout table's ids,
// scaling an entry (prestige entries get a bonus added after the write),
// classifying entries, planning a write that never doubles twice, learning
// the prestige bonus from what changed, and the payout chain's pin entities.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fr {

// Objective payouts seen in Activate_* events (7000 down to 500), the
// per-target elimination payouts 250 and 200,
// the prestige objectives' levels (from 3,500 in steps of 500), the
// prestige-level bonuses for completing prestige objectives (500 down to 300),
// and the payout table's probable prestige bases 2500 and 2000. Only table
// entries holding one of these are written.
inline constexpr std::array<int32_t, 22> kPayoutAmounts {8000, 7500, 7000, 6500, 6000, 5500, 5000, 4500,
                                                         4000, 3500, 3000, 2500, 2000, 1500, 1000, 500,
                                                         450,  400,  350,  300,  250,  200};

// Index of value in kPayoutAmounts, or nothing.
std::optional<size_t> PayoutAmountIndex(int64_t value);

// A numeric engine type (property values read raw).
enum class NumericKind {
    Other,
    Int32,
    Int64,
    Float32,
};

// The kind of an engine type name ("int32", "int64", "float32"); Other for
// anything else.
NumericKind NumericKindOf(std::string_view typeName);

// Bytes the kind occupies (0 for Other).
size_t NumericKindSize(NumericKind kind);

// The event's "Name" string in its JSON text, or empty.
std::string_view JsonEventName(std::string_view json);

// A key and its value's text in single-line JSON.
struct JsonScalar {
    std::string_view key;
    std::string_view value;
};

// The keys containing any of parts whose value is a scalar (true, false,
// null, a number, or a string with its quotes), each once, in text order, in
// compact single-line JSON such as a CpdSet's. Keys whose value is an object
// or array are skipped.
std::vector<JsonScalar> JsonScalarsWithKeyContaining(std::string_view json, std::span<const std::string_view> parts);

// The text with control characters (line breaks, tabs) replaced by spaces,
// cut after maxChars characters with a "... (<total> chars)" note.
std::string OneLine(std::string_view text, size_t maxChars);

// The payout table: its instance entity has this sub-entity id; its ZValueInt children hold one
// objective amount each in m_nValue.
inline constexpr uint64_t kPayoutTableId = 0x1c8a3cd106f999feull;

// CRC32 of "m_nValue" (ZValueInt's int32 value, no getter).
inline constexpr uint32_t kValueIntValueId = 0xd33b1d5a;

// The largest normal (non-prestige) base in the payout table; every prestige
// base is 2000 or more.
inline constexpr int32_t kNormalPayoutMax = 1500;

// The largest value a payout write stores.
inline constexpr int32_t kPayoutLimit = 1'000'000;

enum class PayoutKind {
    Normal,   // the amount shown is m_nValue
    Prestige, // the game adds the prestige bonus B at eLoading_SceneActivated
    Unknown,
};

const char* PayoutKindName(PayoutKind kind);

// What observation has shown about the table's entries: the prestige bonus
// and which entry ids get it. Kept for the session and persisted.
struct PrestigeMemory {
    std::optional<int32_t> bonus;
    std::set<uint64_t> prestige;
    std::set<uint64_t> normal;

    // Records id as prestige or normal (moving it from the other set).
    // Returns true when that changed anything.
    bool Observe(uint64_t id, bool isPrestige);
};

// How an entry is classified, and whether by observation.
struct PayoutClass {
    PayoutKind kind;
    bool observed; // from memory, not from the value
};

// An observed id wins (prestige only with a known bonus, else unknown);
// otherwise a base in 1..kNormalPayoutMax is normal by value, anything else
// unknown.
PayoutClass ClassifyPayout(const PrestigeMemory& memory, uint64_t id, int32_t base);

enum class PayoutAction {
    Write,          // store target
    AlreadyWritten, // the value is this mod's last write for the id (or that plus the bonus): not rebuilt
    Unchanged,      // target equals the current value
    NotAnAmount,    // the value is not in kPayoutAmounts
    Unknown,        // the kind is unknown (not doubled until observed)
    NoTarget,       // the target function gives no value
};

struct PayoutPlan {
    PayoutAction action;
    int32_t target; // for Write and Unchanged
};

// What to do with an entry holding current at scene start, checked in this
// order: AlreadyWritten, NotAnAmount, Unknown, NoTarget, then Unchanged or
// Write. The target is target(current, kind, bonus) (the payout scale's,
// PayoutScale.h); an empty target is NoTarget. lastWritten is what this mod
// last wrote for the entry's id this session, if anything, and lastBase the
// vanilla base that write was computed from: a current equal to it is a
// rebuilt table's vanilla value, never AlreadyWritten. Without it, a factor
// below 1 on a prestige entry can write lastWritten = base - bonus, and the
// rebuilt vanilla base would then look like that write plus the bonus and be
// left unscaled.
PayoutPlan
PlanPayoutWriteTo(int32_t current, PayoutKind kind, int32_t bonus, std::optional<int32_t> lastWritten,
                  std::optional<int32_t> lastBase,
                  const std::function<std::optional<int32_t>(int32_t base, PayoutKind kind, int32_t bonus)>& target);

// The one positive change every changed entry shares (the prestige bonus), or
// empty when none changed or the changes differ (*disagree set then).
// Changes of 0 or less are ignored.
std::optional<int32_t> CommonBonus(std::span<const int32_t> deltas, bool* disagree);

// One table entry's change between scene start (or this mod's write) and a
// later read.
struct EntryChange {
    uint64_t id;
    int32_t delta;
};

// Entries that must share one positive change before it is taken as the
// prestige bonus (unless most known prestige entries show it).
inline constexpr size_t kMinBonusEntries = 10;

// What LearnFromScene concluded.
struct LearnResult {
    std::optional<int32_t> bonus; // the common positive change, if any
    bool disagree = false;        // positive changes differ: nothing learned
    size_t withBonus = 0;         // entries that changed by bonus
    size_t knownRead = 0;         // entries already known as prestige
    size_t knownWithBonus = 0;    // of those, changed by bonus
    bool trusted = false;         // bonus learned (enough entries, or most known prestige entries)
    bool bonusZero = false;       // nothing rose and every known prestige entry read is unchanged: bonus set to 0
    size_t keptPrestige = 0;      // known prestige entries unchanged in a scene whose bonus differs: kept
    bool changed = false;         // memory changed
};

// Learns from one scene's changes. A common positive change B is trusted
// when at least kMinBonusEntries entries show it or more than half of the
// entries already known as prestige do; then memory.bonus becomes B, entries
// changed by B are prestige and unchanged ones normal, except that an entry
// known as prestige is never made normal when the known bonus differs from B.
// With no positive change, a known non-zero bonus becomes 0 when known
// prestige entries were read and every one of them is unchanged (the game no
// longer adds a bonus). Anything else learns nothing.
LearnResult LearnFromScene(PrestigeMemory& memory, std::span<const EntryChange> changes);

// "1c8a3cd106f999fe,d9788bdba0da37cc" (16 lowercase hex digits each, in order).
std::string FormatIdList(const std::set<uint64_t>& ids);

// Parses FormatIdList's text; entries are whitespace-trimmed, empty ones
// skipped, entries that are not 1-16 hex digits appended to bad.
std::set<uint64_t> ParseIdList(std::string_view text, std::vector<std::string>& bad);

// The payout table search walks the loaded bricks most likely
// first, so the walk's entity cap never cuts it off before the table.
enum class TableBrickTier {
    Held,  // held a table instance in an earlier scene this session
    Named, // an additional brick named like an evergreen, freelancer or mission brick
    Other, // the rest, in load order
};

const char* TableBrickTierName(TableBrickTier tier);

struct TableBrickOrder {
    std::vector<size_t> order;         // indices into the loaded list, each once
    std::vector<TableBrickTier> tiers; // per entry of order
};

// The walk order: held bricks, then named ones, then the rest; load order
// within each tier.
TableBrickOrder OrderTableBricks(std::span<const uint64_t> loaded, const std::set<uint64_t>& held,
                                 const std::set<uint64_t>& named);

// The stop rule, checked after each fully walked brick: the table was found
// (TableFound; with all its children: a brick's walk finishes before the
// check) and no brick still to walk held an instance earlier this session.
bool TableSearchDone(bool found, std::span<const uint64_t> unwalked, const std::set<uint64_t>& held);

// In missions no entity has kPayoutTableId (the table template is
// instantiated under another parent id there), but the entries keep their
// template sub-entity ids, so the search also matches entries by the ids
// observation learned (memory.prestige and memory.normal).
class LearnedEntryIds {
public:
    explicit LearnedEntryIds(const PrestigeMemory& memory);

    // Learned ids (prestige and normal, each once).
    size_t Size() const { return m_Ids.size(); }

    bool Contains(uint64_t id) const { return m_Ids.contains(id); }

    // True when id is learned; records it as found (Found counts each id once,
    // however many entities carry it).
    bool Match(uint64_t id);

    // Distinct learned ids matched so far.
    size_t Found() const { return m_Found.size(); }

    // At least kLearnedFoundPercent percent of a non-empty set was found.
    bool MostlyFound() const;

private:
    std::unordered_set<uint64_t> m_Ids;
    std::unordered_set<uint64_t> m_Found;
};

inline constexpr size_t kLearnedFoundPercent = 90;

// The search has found the table: an instance with kPayoutTableId, or most
// learned entry ids (a parent id remembered from an earlier scene does not
// count by itself: its learned children do).
bool TableFound(size_t tableIdInstances, const LearnedEntryIds& learned);

// An instance found through learned entry ids (as their parent, or by a
// remembered parent id) is written and remembered only when it holds at least
// max(kMinBonusEntries, kLearnedFoundPercent percent of learnedSize) learned
// children: one stray match never makes a write target.
bool LearnedInstanceTrusted(size_t learnedChildren, size_t learnedSize);

// True when the brick's file name (after the last '/') contains "evergreen",
// "freelancer" or "mission", ASCII case-insensitive.
bool IsLikelyTableBrickPath(std::string_view path);

// The safehouse's prestige objective wall (the laptop's three cards). The
// ZValueInt_Poll kPrestigeDisplayPollId sits in a small template
// (one "display entry" per prestige objective) of which the safehouse holds
// 108 instances: 36 entries (one per objective, each with its own sub-entity
// id, the poll's owning entity) in each of the three
// Helper_Evergreen_OptInObj_Picker templates (the three cards). Each poll reads
// its own copy (m_rValueEntity, sub-entity `a23a5844ade90a33`), which is 0 at
// scene start and gets the objective's vanilla amount, bonus included, during
// eLoading_SceneActivated, right before the poll emits it (the table's
// vanilla prestige amounts). The copies are set from the
// entries' own data, not from the payout table, so the payout scale never
// reaches them: the cards show vanilla amounts while the objective pays the
// scaled one.
inline constexpr uint64_t kPrestigeDisplayPollId = 0x5c0244fb1fcd5432ull;

// The ZEvergreenPrestigeObjectiveInfoEntity in every Help_Prestige_* helper,
// and the three ZEvergreenPrestigeObjectiveInfoDataProvider entities (Left,
// Middle, Right) whose Title, Description and ImageRID output pins drive the
// cards' text boxes. The payout table search skips them: never table entries.
inline constexpr uint64_t kPrestigeInfoEntityId = 0xbb4db88c9223d0afull;
inline constexpr std::array<uint64_t, 3> kPrestigeInfoProviderIds {
    0x75046270bd9a2023ull, // Left
    0xbf9d30fbf8100999ull, // Middle
    0xe23333c15d12f44bull, // Right
};

// A payout table entry's int32 property holding its objective number (101 to
// 136 for prestige objectives: the CPD's OptinPayoutObjective numbers).
inline constexpr uint32_t kObjectiveNumberPropertyId = 0x72e32d8d;

// The prestige objective numbers.
inline constexpr int32_t kPrestigeObjectiveFirst = 101;
inline constexpr int32_t kPrestigeObjectiveLast = 136;

// One Help_Prestige_<Name><number> helper of the safehouse brick (g_snug.txt;
// "Help_Prestige_BlowOpenSafe" has no number: 134, its tile image is
// 134_OpenSafe).
struct PrestigeHelper {
    uint64_t id; // sub-entity id
    int32_t objective;
    const char* name; // the helper's own name, for reference (players see PrestigeObjectiveName)
};

inline constexpr std::array<PrestigeHelper, 36> kPrestigeHelpers {{
    {0x3dc07436463f098full, 101, "Silent Assassin"},
    {0xc7adcb02c1800158ull, 102, "Epic Melee"},
    {0xce302fca08986511ull, 103, "Epic Sniper"},
    {0xf2dab5d700037d0full, 104, "Epic Shotgun"},
    {0x880eaee9097a61d9ull, 105, "Epic SMG"},
    {0xf5b6d76a7cebcf4bull, 106, "Epic Silenced Pistol"},
    {0x245b2c7a78b7d052ull, 107, "Epic Unsilenced Pistol"},
    {0x06936ffa3019bc97ull, 108, "Epic Assault Rifle"},
    {0x9d6f2f18ac7d0eaaull, 109, "Legendary Headshot"},
    {0x5ed2b21fceadbceaull, 110, "Icepick"},
    {0xeee495392c8905e5ull, 111, "Katana"},
    {0x383535992bcd1de0ull, 112, "Shuriken"},
    {0xc5b4f4b706cace80ull, 113, "Axe"},
    {0x888a171f781a57e6ull, 114, "Knife"},
    {0xb45577208753cbd2ull, 115, "SASO"},
    {0x2b8bfdeb5876e960ull, 116, "SA No Firearms"},
    {0xe2717728c6afe065ull, 117, "Timed Disguising"},
    {0x601f9b005b181c9bull, 118, "Timed Hiding"},
    {0x6845cd82ed44dec3ull, 119, "Timed Targets"},
    {0xabd0f7081de753d1ull, 120, "Timed Silent Takedown"},
    {0x95d412e61087ff52ull, 121, "Collateral Firearm"},
    {0x43891e4a061be61bull, 122, "Collateral Accident"},
    {0xa029d655034d8f09ull, 123, "Collateral Explosion"},
    {0xe1f36a47972b0751ull, 124, "Shoot In Water"},
    {0xce011484de9e0e29ull, 125, "Pacify Baseball Bat"},
    {0x1f93d0564bf26639ull, 126, "Showdown Timed Pacify Suspect"},
    {0xfaa4c38997eac9daull, 127, "Showdown Kill Meeting"},
    {0x211ea19ac57e58caull, 128, "Showdown Tell"},
    {0x1f36182e1f83fd4dull, 129, "Showdown Escape"},
    {0x29a2074a28879a0aull, 130, "Showdown Silent Takedown Suspect"},
    {0x84225029ba034f58ull, 131, "Showdown Oil Water Spill"},
    {0x682039a79f336e3full, 132, "Showdown Assassin"},
    {0x932879225974e212ull, 133, "Showdown Lookout"},
    {0x283ab5e07d1b5593ull, 134, "Blow Open A Safe"},
    {0x9a5167afb08a3427ull, 135, "Showdown Arrange Meeting"},
    {0x0bcb40aa6cc63afdull, 136, "Perfect Run"},
}};

// The objective number of a Help_Prestige_* helper's sub-entity id, or empty.
std::optional<int32_t> PrestigeHelperObjective(uint64_t id);

// The prestige objective's in-game name for an objective number
// (kPrestigeChoices, PrestigeCards.h: "Silent Assassin - Suit Only"), or nullptr.
const char* PrestigeObjectiveName(int32_t objective);

enum class DisplayAction {
    Write,        // store target
    InStep,       // already shows what the table pays (this mod's earlier write, with or without the bonus)
    Unchanged,    // the table entry was not changed, so the copy is right as it is
    Unrecognized, // neither the table's vanilla amount nor the scaled one: left alone
};

const char* DisplayActionName(DisplayAction action);

struct DisplayPlan {
    DisplayAction action;
    int32_t target; // for Write
    bool withBonus; // the copy already held the bonus (target includes it)
};

// What a display copy of a prestige table entry should hold so that it shows
// what the table pays. now: the copy's value; base: the table entry's vanilla
// stored value (before the bonus); written: what this mod stored in the table
// entry; bonus: the prestige bonus (added to the table entry, and maybe to the
// copy, at eLoading_SceneActivated); lastWrite: this mod's last write to this
// copy in this load. Checked in this order: written == base: Unchanged; now is
// lastWrite (or it plus bonus): InStep; now == base: Write written; now ==
// base + bonus: Write written + bonus; now == written (or plus bonus): InStep;
// else Unrecognized.
DisplayPlan PlanDisplayWrite(int32_t now, int32_t base, int32_t written, int32_t bonus,
                             std::optional<int32_t> lastWrite);

// The prestige objective number (101 to 136) a template sub-entity's name
// carries as a run of exactly three digits ("Help_Prestige_SilentAssassin101"
// -> 101), when every such number is the same; else empty.
std::optional<int32_t> ObjectiveFromName(std::string_view name);

// The one prestige objective number (101 to 136) every candidate names, or
// empty when there is none or two candidates disagree. Other numbers are
// ignored.
std::optional<int32_t> AgreedObjective(std::span<const int32_t> candidates);

// One prestige objective's vanilla stored amount in the payout table (before
// the prestige bonus).
struct PrestigeBase {
    int32_t objective;
    int32_t base;
};

// The objective whose vanilla amount plus bonus is shown, when exactly one
// objective has that amount (an objective listed twice counts once); else
// empty. The display entries' fallback when nothing else names their objective.
std::optional<int32_t> ObjectiveByUniqueAmount(int32_t shown, int32_t bonus, std::span<const PrestigeBase> bases);

} // namespace fr
