#pragma once

// Prestige card shuffle (prestige_shuffle): which three prestige objectives
// the safehouse laptop offers, drawn from the campaign seed and a pick counter
// instead of the game's own random pick. The mod then sets m_Weight to 0 on
// every choice of each card picker's ZRandomSelector except the drawn
// objective's, before the pickers run, so the game's own pick, card text, art
// and CPD write stay consistent (src/mod/PrestigeShuffle.*). The weights stay
// forced for the whole safehouse visit, a card its picker will not show is
// redrawn through the game's own reset (see PrestigeRedrawPath), and a taken
// card's objective is corrected to what the card shows (see
// PrestigeTakeCorrection). m_Choices itself
// is never written: pointing every element at one choice makes the selector
// signal OnPickFailed, which stops the picker chain and leaves all three
// cards blank.
//
// The safehouse brick chains the three
// card pickers: SignalFork_Void_04 .Out02 -> Picker1 .Pick, Picker1
// .OnPickDone -> Picker2 .Pick, Picker2 .OnPickDone -> Picker3 .Pick.
// SignalFork_Void_04 is fired by a TimerConditional, or by SignalFork_Void
// .Out06, which a return to the safehouse (through SignalPipe_Conditional_Void)
// or the laptop's SetPieceHelpers_Activator_SingleTool_B_01 (a re-pick) fires.
// Taking card A, B or C fires SignalFork_Void_01/02/03 .Out02 -> PickerN
// .Poll, whose ObjectiveId reaches Setpiece_evergreen_PrestigeObjective_Unique
// and SetCPD_OptinPayoutObjective.
//
// Two displays per card: the laptop screen is the picker's
// ZEvergreenPrestigeObjectiveInfoDataProvider, which every pick refills with
// the picked entry's text; the card's interaction text and the entry PickerN
// .Poll reaches are those of the first pick since the game's reset.
// SignalFork_Void resets (Unique OnValue 0) before its .Out06; a pick started
// at SignalFork_Void_04 .In (its timer) does not, so a re-pick there moves
// the laptop screen but not the text or the take.
//
// Each picker (template 00D2255195F8E5C6) holds the RandomSelector
// 1a029cfb6fe05b78 and one choice entry per objective (the entry's sub-entity
// id below; the selector exposes each entry's "RandomSelectorEntityChoice").
// m_Choices holds 32 refs at run time (the template lists 36 entries).

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Vanilla rule (IOI, May 2023): at most one card per category.
enum class PrestigeCategory : uint8_t {
    None,
    SilentAssassin, // 101, 115, 116
    Timed,          // 117-120 (and the showdown-only 126)
    Multikill,      // 121-123 (collateral kills)
};

struct PrestigeChoice {
    int32_t objective; // 101 to 136 (the CPD's OptinPayoutObjective number)
    uint64_t entryId;  // the choice entry's sub-entity id in the picker template
    const char* name;
    PrestigeCategory category;
    const char* excluded; // why the shuffle never draws it whatever prestige_exclude says, or nullptr
};

// prestige_exclude's default: 115 Silent Assassin - Suit Only, which the
// shuffle always left out before the setting, and the four Timed objectives
// 117-120.
inline constexpr std::string_view kDefaultPrestigeExclude = "115,117,118,119,120";
// The same as a set: {115, 117, 118, 119, 120}.
const std::set<int32_t>& DefaultPrestigeExclude();
// Why an objective in prestige_exclude is not drawn (for the log). Pointers to
// literals: compare the text, not the pointer (a build need not merge them).
inline constexpr const char* kExcludedSetting = "in prestige_exclude";
inline constexpr const char* kExcludedPerfectRun = "disabled by IOI: no laptop screen, cannot be taken";
inline constexpr const char* kExcludedShowdown = "showdown only";
inline constexpr const char* kExcludedNotInGame = "not in the game: no picker offers it";

// The names are the game's own; 124 is not on the game's prestige list and
// no picker offers it: excluded, so the shuffle never draws it even if a
// picker listed it.
inline constexpr std::array<PrestigeChoice, 36> kPrestigeChoices {{
    {101, 0x9b97127ea6e1ae87ull, "Silent Assassin", PrestigeCategory::SilentAssassin, nullptr},
    {102, 0xe3f800bc9c7a263full, "Melee Kill - Epic", PrestigeCategory::None, nullptr},
    {103, 0x3b02668960a3a4f8ull, "Sniper Kill - Epic", PrestigeCategory::None, nullptr},
    {104, 0xff0235162a39b068ull, "Shotgun Kill - Epic", PrestigeCategory::None, nullptr},
    {105, 0x8be3189041b605a1ull, "Sub Machine Gun Kill - Epic", PrestigeCategory::None, nullptr},
    {106, 0xf239d77121d109dcull, "Silenced Pistol Kill - Epic", PrestigeCategory::None, nullptr},
    {107, 0xbbc2e5d5ee2b5f93ull, "Unsilenced Pistol Kill - Epic", PrestigeCategory::None, nullptr},
    {108, 0x19974dc3351a3f58ull, "Assault Rifle Kill - Epic", PrestigeCategory::None, nullptr},
    {109, 0x29b64020f77bda46ull, "Headshot Kill - Legendary", PrestigeCategory::None, nullptr},
    {110, 0x924e9edceccb053eull, "Icepick Kill", PrestigeCategory::None, nullptr},
    {111, 0x43aeef70d54f7e9dull, "Katana Kill", PrestigeCategory::None, nullptr},
    {112, 0x076581a5383136bbull, "Shuriken Kill", PrestigeCategory::None, nullptr},
    {113, 0xcb2812589bac9e88ull, "Axe Kill", PrestigeCategory::None, nullptr},
    {114, 0xe6a60953ebf17fc6ull, "Knife Kill", PrestigeCategory::None, nullptr},
    {115, 0x6c9e057bf1722e55ull, "Silent Assassin - Suit Only", PrestigeCategory::SilentAssassin, nullptr},
    {116, 0x52c5abe4483bcd05ull, "Silent Assassin - No Firearms", PrestigeCategory::SilentAssassin, nullptr},
    {117, 0x0d1c293eeae33cf3ull, "Timed - Get Disguises", PrestigeCategory::Timed, nullptr},
    {118, 0x51aabf81a790fe1dull, "Timed - Hide and Seek", PrestigeCategory::Timed, nullptr},
    {119, 0x127cac3fcc421b01ull, "Timed - Time Trial", PrestigeCategory::Timed, nullptr},
    {120, 0x50fba0929ef004b4ull, "Timed - Silent Takedown", PrestigeCategory::Timed, nullptr},
    {121, 0x64b20bff335772dbull, "Collateral Kill - Firearms", PrestigeCategory::Multikill, nullptr},
    {122, 0xceac0e17f2876c58ull, "Collateral Kill - Accident", PrestigeCategory::Multikill, nullptr},
    {123, 0x84c8d9ae802103f6ull, "Collateral Kill - Explosion", PrestigeCategory::Multikill, nullptr},
    {124, 0x51607fd0a9b00a19ull, "Shoot Target into Water", PrestigeCategory::None, kExcludedNotInGame},
    {125, 0xfd261f342e61afd9ull, "Pacify - Baseball Bat", PrestigeCategory::None, nullptr},
    {126, 0x02b3da6037391e5cull, "Timed - Pacify Suspects - Silent", PrestigeCategory::Timed, kExcludedShowdown},
    // Not in the task's showdown list, but the leader only exists in showdowns
    // (the safehouse names its helper "Showdown Kill Meeting").
    {127, 0xa8e34ea0af2245b8ull, "Eliminate Leader - Heading for a Meeting", PrestigeCategory::None, kExcludedShowdown},
    {128, 0x577028b6c5a23080ull, "Eliminate Leader - During Tell", PrestigeCategory::None, kExcludedShowdown},
    {129, 0x9ae96239ae91344dull, "Eliminate Leader - Escaping", PrestigeCategory::None, kExcludedShowdown},
    {130, 0xe94f3aa849f9b76aull, "Pacify Suspects - Silent", PrestigeCategory::None, kExcludedShowdown},
    {131, 0x090cf5683f1781a7ull, "Eliminate Leader - Oil or Water Canister Trap", PrestigeCategory::None,
     kExcludedShowdown},
    {132, 0x58a46205bc0da5dfull, "Neutralize an Assassin", PrestigeCategory::None, kExcludedShowdown},
    {133, 0x8a31b1be4e250847ull, "Neutralize Lookout", PrestigeCategory::None, kExcludedShowdown},
    {134, 0x7f5805c1f5644663ull, "Open Safe - Explosive", PrestigeCategory::None, nullptr},
    {135, 0x57c047415a7dbd37ull, "Arrange Meeting", PrestigeCategory::None, kExcludedShowdown},
    {136, 0xf3917c0e5b1f94d8ull, "Perfect Run", PrestigeCategory::None, kExcludedPerfectRun},
}};

inline constexpr size_t kPrestigeCards = 3;

// The safehouse brick and the ids of the chain around the card pickers
// (g_snug.txt connections 1378-1433).
inline constexpr uint64_t kPrestigeBrickRid = 0x00A9'B93A'825F'5BDCull;
inline constexpr std::array<uint64_t, kPrestigeCards> kPrestigePickerIds {
    0xd7ebe074ac4b64a3ull, // Helper_Evergreen_OptInObj_Picker1
    0x8b754413bc7f64e2ull, // Helper_Evergreen_OptInObj_Picker2
    0x45df1f848528bca7ull, // Helper_Evergreen_OptInObj_Picker3
};
inline constexpr uint64_t kPrestigeSelectorId = 0x1a029cfb6fe05b78ull; // RandomSelector in each picker
inline constexpr uint64_t kPrestigePickForkId = 0x3fbdb58157483acdull; // SignalFork_Void_04 (.Out02 -> Picker1 .Pick)
inline constexpr uint64_t kPrestigeRepickForkId =
    0x8f5f72e15778433dull; // SignalFork_Void (.Out06 -> SignalFork_Void_04)
inline constexpr uint64_t kPrestigeReturnPipeId =
    0x0ceaa7879182287eull; // SignalPipe_Conditional_Void (.Out -> SignalFork_Void)
inline constexpr std::array<uint64_t, kPrestigeCards> kPrestigeTakeForkIds {
    0x322222697997a1f1ull, // SignalFork_Void_01 (.Out02 -> Picker1 .Poll; card A taken)
    0x0e11660f05ffd647ull, // SignalFork_Void_02 (card B)
    0xf60f83a4c04c0666ull, // SignalFork_Void_03 (card C)
};
inline constexpr uint64_t kPrestigeUniqueId = 0x6a4e045a20a04493ull; // Setpiece_evergreen_PrestigeObjective_Unique
inline constexpr uint64_t kPrestigeSetCpdId = 0x97d22183de786b93ull; // SetCPD_OptinPayoutObjective

// Perfect Run (136) is never drawn (the choice table excludes it). IOI left it out of every picker's
// m_Choices; its choice entry exists, but swapped into m_Choices the entry is
// only half wired: when picked it emits its display poll but fills no laptop
// screen (no ZEvergreenPrestigeObjectiveInfoDataProvider text, so no icon),
// and taking it reaches no ObjectiveId (PickerN .Poll emits nothing further),
// so the take does nothing and the other cards stay selectable. It also
// became the fallback a rejecting picker showed. Nothing the mod writes can
// add those connections.

// Whether the draw may pick a choice: not excluded by the table (136 and the
// showdown-only objectives) or in p_Excluded (prestige_exclude's ids).
bool PrestigeChoiceDrawable(const PrestigeChoice& p_Choice,
                            const std::set<int32_t>& p_Excluded = DefaultPrestigeExclude());

// The objectives prestige_exclude can leave out (the settings menu's list),
// in table order: those the table never excludes, less 113, 114 and 124,
// which no picker offers (so they are never drawn anyway).
std::span<const int32_t> PrestigeExcludeOptions();

// Whether prestige_exclude leaves enough to fill the three cards: the
// PrestigeExcludeOptions() not excluded can be put on the cards under the
// rules of DrawPrestigeCards (card A a Silent Assassin or Timed objective, at
// most one per category). What the pickers offer and reject in a visit can
// still leave too few; AssignPrestigeCards then fills from prestige_exclude.
bool PrestigeExcludeLeavesEnough(const std::set<int32_t>& p_Excluded);

const PrestigeChoice* PrestigeChoiceByObjective(int32_t objective);
const PrestigeChoice* PrestigeChoiceByEntry(uint64_t entryId);

// The choice entry an m_Choices element belongs to, from the sub-entity ids
// of its owner chain (p_Chain[0] the element's own id, then its owning
// entity's, and so on; 0 ends the chain): the first id that is a choice
// entry's, with its depth in the chain. Empty when none is.
struct ChainChoice {
    const PrestigeChoice* choice;
    size_t depth; // 0: the element's own id; 1: its owning entity's; ...
};
std::optional<ChainChoice> PrestigeChoiceInChain(std::span<const uint64_t> p_Chain);

// The fallback walk's match: which picker (an index into p_Pickers, the card
// pickers' entity addresses; null entries never match) an entity with
// sub-entity id p_Id, reached by the walk under p_Parent, is the RandomSelector
// (kPrestigeSelectorId) of. Empty for any other entity.
std::optional<size_t> PickerOfSelector(uint64_t p_Id, uintptr_t p_Parent, std::span<const uintptr_t> p_Pickers);

// "<number> <name>", or "<number> (unknown)".
std::string PrestigeChoiceText(int32_t objective);

// Whether card p_Card's picker shows p_Objective when forced to it, as far
// as is known without trying: Picker1 (card A) shows only a Silent Assassin or
// a Timed objective: forced to 101, 116, 117, 118 or 120 it shows them;
// forced to 103, 104, 105, 106, 109, 112, 123, 125 or 134 it shows another
// objective (its choice's m_Condition, a ZValueBool_Operation_Signal, reads
// false). (136 Perfect Run shows when swapped into its m_Choices, but is
// never drawn.) Picker2 and Picker3 show every forced card of a category no
// earlier card shows.
bool PrestigeCardAccepts(size_t p_Card, int32_t p_Objective);

// Three distinct objectives, in card order (Picker1, 2, 3), drawn from the
// objectives in p_Available (any order; duplicates and objectives outside the
// table count once) that are not excluded, each accepted by its card
// (PrestigeCardAccepts), with at most one per category. Deterministic in
// (seed, counter) and the available set. Empty when fewer than three can be
// drawn under those rules.
std::optional<std::array<int32_t, kPrestigeCards>> DrawPrestigeCards(int32_t seed, uint32_t counter,
                                                                     std::span<const int32_t> p_Available);

// Objectives a card's picker would not show when forced to them in this
// safehouse visit (per card: the picker's choice condition was false, so its
// selector fell back to another choice). Cleared at each scene load.
using PrestigeRejected = std::array<std::vector<int32_t>, kPrestigeCards>;

// The same draw, also leaving out what each card's picker rejected. The
// objectives are shuffled from (seed, counter), and every way of putting
// three of them on the three cards is tried: the first in the shuffled order
// (card A first) that every card accepts. p_Excluded: prestige_exclude's ids,
// never drawn.
std::optional<std::array<int32_t, kPrestigeCards>>
DrawPrestigeCards(int32_t seed, uint32_t counter, std::span<const int32_t> p_Available,
                  const PrestigeRejected& p_Rejected, const std::set<int32_t>& p_Excluded = DefaultPrestigeExclude());

// The draw above when it finds three cards; else the cards with as few
// objectives from p_Excluded as possible (the least bad: an objective only
// prestige_exclude leaves out, that the card accepts; shuffled apart from the
// allowed ones). Never an objective the table excludes (showdown-only, 136).
// Empty only when no three available objectives fit the cards at all.
std::optional<std::array<int32_t, kPrestigeCards>>
AssignPrestigeCards(int32_t seed, uint32_t counter, std::span<const int32_t> p_Available,
                    const PrestigeRejected& p_Rejected, const std::set<int32_t>& p_Excluded = DefaultPrestigeExclude());

// How many of the cards are in p_Excluded (filled from outside the allowed list).
size_t PrestigeCardsOutside(const std::array<int32_t, kPrestigeCards>& p_Cards, const std::set<int32_t>& p_Excluded);

// One element of a picker's m_Choices as the shuffle sees it: the objective
// of its choice entry (empty when not named) and its choice's m_Weight now.
struct PrestigeSlot {
    std::optional<int32_t> objective;
    float weight = 0.0f;
};

// Objectives (table order) every picker can pick: each picker in p_Pickers has
// at least one element of that objective with a weight above 0. A selector
// with no weight above 0 signals OnPickFailed.
std::vector<int32_t> PrestigeObjectivesOffered(std::span<const std::vector<PrestigeSlot>> p_Pickers);

// The weights that leave a picker's selector only p_Drawn to pick, element by
// element: every element of another objective (or not named) gets 0, the
// elements of p_Drawn keep theirs. Empty when no element of p_Drawn has a
// weight above 0 (the selector would fail to pick).
std::optional<std::vector<float>> ForcedPrestigeWeights(std::span<const PrestigeSlot> p_Slots, int32_t p_Drawn);

// After a forced draw: what each picker has signaled since.
struct PrestigePickerProgress {
    bool pickDone = false;   // the picker's OnPickDone
    bool pickFailed = false; // its selector's OnPickFailed (or the loop guard)
};

enum class PrestigeGuard {
    Wait,   // not all pickers done yet, within the time allowed
    Done,   // every picker signaled OnPickDone
    Failed, // a selector failed to pick, or not every picker was done in time
};

// The guard after a forced draw, p_Elapsed seconds in (p_Timeout allowed).
// A failure wins over done.
PrestigeGuard PrestigeGuardVerdict(std::span<const PrestigePickerProgress> p_Pickers, double p_Elapsed,
                                   double p_Timeout);

// The campaign's pick state, persisted in [campaign] prestige_shuffle.
struct PrestigePickState {
    int32_t seed = 0;     // campaign seed the counter belongs to (0: none yet)
    uint32_t counter = 0; // which draw of this campaign the cards show
    bool taken = false;   // a card is taken and its mission not started yet
    // The cards last shown for this seed and counter (card A, B, C), so a
    // reload, a new session or the laptop shows them again.
    std::optional<std::array<int32_t, kPrestigeCards>> cards;

    bool operator==(const PrestigePickState&) const = default;
};

// The state for a pick of the cards in campaign p_Seed: a new campaign starts
// at counter 0 with no cards; otherwise the state stays as it is (a reload, a
// new session, the laptop opened again, a card taken: the same cards).
PrestigePickState AdvancePrestigePick(const PrestigePickState& p_State, int32_t p_Seed);

// A Freelancer mission starts: a taken card is played, so the next safehouse
// pick draws new cards (the next counter, no cards, not taken). Without a
// taken card (or a second start of the same mission, a restart) the state
// stays.
PrestigePickState ConsumePrestigePick(const PrestigePickState& p_State);

// The campaign stored as seed p_From is now known by its own seed p_To (its
// generated seed was replaced at the pick): the counter, the taken card and
// the cards shown carry over, so the next pick is not "a new campaign".
// Another seed (or a 0) leaves the state as it is.
PrestigePickState RekeyPrestigePick(const PrestigePickState& p_State, int32_t p_From, int32_t p_To);

// The cards to show: the state's cards when they could still be shown (three
// distinct objectives of the table, each available, accepted by its card and
// not rejected for it, at most one per category; cards in p_Excluded count as
// fills) with
// no more fills than AssignPrestigeCards needs now, else AssignPrestigeCards
// for the state's seed and counter. Empty when nothing can be assigned.
// Kept cards holding 136 (never drawn) are drawn again.
std::optional<std::array<int32_t, kPrestigeCards>>
ChoosePrestigeCards(const PrestigePickState& p_State, std::span<const int32_t> p_Available,
                    const PrestigeRejected& p_Rejected, const std::set<int32_t>& p_Excluded = DefaultPrestigeExclude());

// A card's picker showed p_Shown after it was forced to p_Forced: the forced
// objective was rejected (its choice condition was false).
bool PrestigeCardRejected(std::optional<int32_t> p_Forced, std::optional<int32_t> p_Shown);

// How the cards are picked again after a rejected card: only through the
// game's reset (SignalFork_Void .In, as the laptop opening does: it resets the
// pick, then .Out06 -> SignalFork_Void_04 -> Picker1 .Pick), so the card text
// and the take follow the new pick as the laptop screen does. Never through
// SignalFork_Void_04 .In alone (see the chain notes above). Without the reset
// fork, or with p_Redraws at p_MaxRedraws, the cards stay as shown, and the
// text and the take agree with them (no re-pick since the reset).
enum class PrestigeRedraw {
    ThroughReset, // signal SignalFork_Void .In
    Keep,         // no new pick
};
PrestigeRedraw PrestigeRedrawPath(bool p_ResetForkFound, int p_Redraws, int p_MaxRedraws);

// When a card is taken, the objective its picker's Poll emits (p_Emitted)
// becomes the CPD's OptinPayoutObjective. The value to send instead: what the
// card shows (p_Shown) when it is known, not 0 and differs; empty to leave it
// (a picker's Poll can emit another objective than its card shows).
std::optional<int32_t> PrestigeTakeCorrection(std::optional<int32_t> p_Shown, int32_t p_Emitted);

// "<seed> <counter> <taken 0|1>", then " <card A> <card B> <card C>" when the
// state holds cards.
std::string FormatPrestigePickState(const PrestigePickState& p_State);

// Empty for text that is not three (or six) whitespace-separated fields of
// that form (an empty text included); the cards must be table objectives.
std::optional<PrestigePickState> ParsePrestigePickState(std::string_view p_Text);

} // namespace fr
