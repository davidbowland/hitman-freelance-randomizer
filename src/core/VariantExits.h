#pragma once

// Pure part of the variant exit fix. A map variant's own exits end the
// mission outside Freelancer's exit flow, and a variant can break Freelancer's
// own exits.
//
// How Freelancer exits work (from chunk0's setpieces_evergreen.template and
// Hokkaido's override_shogi): each exit is an
// instance of an exit_evergreen_* template (exit_evergreen_pushbutton,
// _usekey, ...). Its interaction's confirmation (the "leave the mission?"
// modal, Helper_Evergreen_MissionExitModal's confirmed) signals the template's
// Helper_Evergreen_MissionExitHijacker exitActivated input (its exposed
// exitActivated value turns true; the mission brick's "Any exit used", a
// ValueBool_Operation ANY over the exits, then starts Freelancer's own flow:
// EvergreenExitTriggered, mission completed, payout) and the template's Exit
// output, which starts the exit's sequence in the location brick (pin
// connection overrides in the mission's override brick). The sequence's end
// (ExitSequenceOver) signals the template's Trigger input, forwarded to the
// hijacker's sequenceDone: the level ends and the game goes back to the
// safehouse. Both halves are needed.
//
// A vanilla exit (levelflow.template's exit_pushbutton and the like) has no
// hijacker: its end signals the template's ZContractsExitEntity Trigger
// (exit_pushbutton: ValueBool_Poll_IsExitOnManualAllowed PollTrue after the
// manual sequence, ValueBool_Poll_IsExitAllowed PollFalse without one), which
// ends the contract directly: no Freelancer flow, no payout, no campaign step,
// and the end screen does not lead back to the safehouse (The Reflection's
// exit_gate 9176f6ac "Bellini Exit": ContractEnd, no EvergreenExitTriggered).
// Its start (the manual sequence starting) is the
// template's Exit output, forwarded from ValueBool_Poll_UseManualTrigger01
// PollTrue.
//
// reflection's scenario_bellini deletes every exit sequence Freelancer's five
// Hokkaido exits start (location's ExitPoints: CableCar_Exit, Snowscooter_Exit,
// Mountain_Exit, Helicopter_Exit, with the exit event transmitter and
// VB_HelicopterExitStarted), so a Freelancer exit there would start
// Freelancer's flow but never end the level.
//
// The fix, once the scene's entities exist: every Freelancer exit (a hijacker
// instance) and every contract exit of the variant's bricks (a
// ZContractsExitEntity whose Trigger something feeds) is found. With a
// Freelancer exit to route to, each variant exit's contract end is blocked at
// the output pins that feed its Trigger (FindVariantExitPins; the signal goes
// on along those pins' other links) and its start is watched: the start sends
// the Freelancer exit's hijacker exitActivated, the blocked end its
// sequenceDone (ExitClock), so the variant's exit runs Freelancer's flow. A
// confirmed Freelancer exit whose sequence the variant is known to delete
// (KnownBrokenExit) gets its sequenceDone kExitGapSeconds later; any other gets
// it after kExitEndWaitSeconds if the level has not ended by then. With no
// Freelancer exit found, the variant's exits are left as they are (logged
// loudly): the mission is never left without a way out. A Freelancer exit
// the variant leaves unusable (FindExitSwitchOff: its getaway vehicle, its key
// or its conditions deleted, or its area sealed off; The Guru's brick and
// Sapienza's road car first) is switched off instead, so the player cannot
// start it; the map's other Freelancer exits stay. With every Freelancer exit
// switched off, a variant exit is still routed through one: the hijacker of a
// switched-off exit still ends the mission.

#include "DianaBriefing.h" // BriefingLink

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Helper_Evergreen_MissionExitHijacker (setpieces_evergreen.template): one in
// every Freelancer exit. An instance's own blueprint factory has one of these
// resource ids (the TBLU, or the TEMP, depending on the factory).
constexpr uint64_t kExitHijackerTblu = 0x00035825D034B443ull;
constexpr uint64_t kExitHijackerTemp = 0x00E114B877A7C3DA;
bool IsExitHijackerResource(uint64_t rid);

// The hijacker's inputs: the exit was confirmed (Freelancer's flow starts),
// and its sequence is over (the level ends).
constexpr std::string_view kHijackerActivatePin = "exitActivated";
constexpr std::string_view kHijackerDonePin = "sequenceDone";

// A vanilla exit's contract end: this class's Trigger input.
constexpr std::string_view kContractsExitClass = "ZContractsExitEntity";
constexpr std::string_view kContractsExitEndPin = "Trigger";

// A vanilla exit template's Exit output: its exit sequence starts.
constexpr std::string_view kExitStartPin = "Exit";

// The exit templates of levelflow.template (vanilla) and
// setpieces_evergreen.template (Freelancer), by TEMP or TBLU resource id
// (ZHMTools' hash list): "exit_pushbutton", "exit_evergreen_usekey", or empty.
std::string_view ExitTemplateName(uint64_t rid);

// One output pin of a template's sub-entity: blocked (a variant exit's end) or
// watched (a variant exit's start, a Freelancer exit's confirmation).
struct ExitPin {
    int32_t entity;             // sub-entity index in the template's blueprint
    std::string pin;            // "PollTrue", "confirmed"
    std::vector<size_t> resend; // blocked pins: the pin's other links (indices), the signal is sent on along
};

struct VariantExitPins {
    std::vector<ExitPin> ends;   // the pins feeding the contract exit's Trigger: blocked
    std::vector<ExitPin> starts; // the pins forwarded to the template's Exit output: watched
    size_t skipped = 0;          // feeds of the Trigger that cannot be blocked
    std::string skipWhy;         // the first one's reason
};

// A vanilla exit's pins from its template's blueprint links: contractsExit is
// the ZContractsExitEntity's sub-entity index among count sub-entities. A feed
// is skipped when it is an input forwarding (the template's own input) or
// when its pin starts more than kLineMaxResend other links.
VariantExitPins FindVariantExitPins(int32_t contractsExit, size_t count, const std::vector<BriefingLink>& links);

// A Freelancer exit's confirmation: the pins connected to its hijacker's
// exitActivated (hijacker: its sub-entity index), each watched.
std::vector<ExitPin> FindFreelancerConfirmPins(int32_t hijacker, size_t count, const std::vector<BriefingLink>& links);

// The same pins by sub-entity id for the exit templates decoded from the game
// files, for when the template's blueprint was
// not copied while the scene loaded (BriefingGraphs.h). Template sub-entity
// ids are the same in every instance.
struct KnownExitPin {
    uint64_t sub;         // the sub-entity's id in the template
    std::string_view pin; // its output pin
};

struct KnownExitTemplate {
    uint64_t temp;
    uint64_t tblu;
    uint64_t anchor; // the template's ZContractsExitEntity (vanilla) or hijacker (Freelancer): checked first
    std::span<const KnownExitPin>
        ends; // vanilla: the pins feeding the contract exit's Trigger (each feeds nothing else)
    std::span<const KnownExitPin> starts;   // vanilla: the pins forwarded to the template's Exit output
    std::span<const KnownExitPin> confirms; // Freelancer: the pins connected to the hijacker's exitActivated
    // Freelancer: the input pins that switch the exit off (each a ValueBoolBasic's SetFalse), as the game does
    // once the exit was confirmed. exit_evergreen_pushbutton: its
    // Interaction_Interact's m_ConditionList_Visible is "Visible", an AND of ActionEnabled (true until the
    // modal's confirmed sends it SetFalse), the instance's m_aEnableConditions and the inventory having loaded,
    // so ActionEnabled false takes the prompt away; "Show Spatial" (the instance's Show3dExitSpatial) is in
    // every condition that shows its map tracker and 3D icon. The hijacker is left alone: its exitActivated
    // never comes, so the mission's "Any exit used" just never counts this exit.
    std::span<const KnownExitPin> offs = {};
};

// exit_pushbutton, exit_evergreen_pushbutton, exit_evergreen_usekey; null for any other.
const KnownExitTemplate* FindKnownExitTemplate(uint64_t rid);

// A variant known to delete the exit sequences some Freelancer exits start
// (by the exit instance's entity id in its brick): reflection, lust, envy,
// harbinger.
struct BrokenExitSequences {
    std::string_view option; // "reflection"
    std::span<const uint64_t> exits;
    std::string_view why;
};

std::span<const BrokenExitSequences> KnownBrokenExitSequences();

// Why the Freelancer exit's sequence is gone under this variant, or empty.
std::string_view KnownBrokenExit(std::string_view option, uint64_t exitId,
                                 std::span<const BrokenExitSequences> known = KnownBrokenExitSequences());

// A Freelancer exit a variant leaves unusable: its exit sequence plays on a
// vehicle the variant deletes (The Guru's car, Sloth's hearse), its key or
// conditions are deleted (Lust's key exits), or its area is
// sealed off (The Reflection's garage). Switched off at scene playing whenever
// one of bricks is loaded (KnownExitTemplate::offs). Keyed on the bricks, not
// the option, so every option that loads them (The Deceivers' wrapper loads
// The Guru's brick) is covered. An exit whose sequence alone is deleted stays
// usable: KnownBrokenExit ends the level for it.
struct ExitSwitchOff {
    uint64_t exit;                    // the exit instance's entity id in its brick
    std::span<const uint64_t> bricks; // runtime resource ids (as VariantBrick::rid); any one loaded
    std::string_view why;
};

std::span<const ExitSwitchOff> KnownExitSwitchOffs();

// The switch-off for this Freelancer exit under the loaded bricks, or null.
const ExitSwitchOff* FindExitSwitchOff(std::span<const uint64_t> loadedBrickRids, uint64_t exitId,
                                       std::span<const ExitSwitchOff> known = KnownExitSwitchOffs());

// One Freelancer exit found at scene allocation.
struct FreelancerExitFacts {
    uint64_t id = 0; // the exit instance's entity id in its brick
    bool inVariantBrick = false;
    size_t confirms = 0;      // FindFreelancerConfirmPins found
    bool knownBroken = false; // KnownBrokenExit
    bool switchedOff = false; // FindExitSwitchOff, and its switches were found
};

// One contract exit of the variant's bricks.
struct VariantExitFacts {
    uint64_t id = 0;
    bool graphRead = false; // its template's (or brick's) blueprint links were read
    size_t ends = 0;
    size_t starts = 0;
    size_t skipped = 0;
};

enum class VariantExitAction : uint8_t {
    Route,            // its end is blocked and replaced by Freelancer's
    NotAnExit,        // nothing feeds its Trigger: it only tells whether exits are open
    KeepNoFreelancer, // no Freelancer exit to route to: left as it is
    KeepUnreadable,   // its wiring could not be read, or a feed cannot be blocked: left as it is
};

std::string_view VariantExitActionText(VariantExitAction action);

struct VariantExitPlan {
    bool hasVia = false;
    // The Freelancer exit routed to: the first neither switched off nor in the variant's bricks, else the first
    // not switched off, else the first (a switched-off exit's hijacker still ends the mission).
    size_t via = 0;
    std::vector<VariantExitAction> actions; // per variant exit
    size_t routed = 0;
    size_t kept = 0; // KeepNoFreelancer and KeepUnreadable
};

// What to do with each variant exit. A variant exit is routed only when there
// is a Freelancer exit to route to and every feed of its contract end is
// blocked; otherwise it is left as it is, so the mission keeps a way out.
VariantExitPlan PlanVariantExits(std::span<const FreelancerExitFacts> freelancer,
                                 std::span<const VariantExitFacts> variant);

// Seconds between exitActivated and sequenceDone when nothing else paces them
// (a broken Freelancer exit, a variant exit without a start): Freelancer's
// flow sends its events in between. The shortest Freelancer exit in the logs
// took 2.9 s from EvergreenExitTriggeredOrWounded to the level's end.
constexpr double kExitGapSeconds = 3.0;

// Seconds after an exit started before sequenceDone is sent when the level has
// not ended on its own (the longest in the logs took 10 s).
constexpr double kExitEndWaitSeconds = 15.0;

// The exit in progress (at most one per mission), in seconds of play.
enum class ExitPhase : uint8_t {
    Idle,
    Started, // exitActivated sent (by Freelancer's exit itself, or by the fix); sequenceDone due or awaited
    Done,    // sequenceDone sent by the fix
    Ended,   // the level ended (ContractEnd)
};

enum class ExitSource : uint8_t {
    None,
    Freelancer, // a Freelancer exit was confirmed
    Variant,    // a routed variant exit started or ended
};

enum class DoneWhy : uint8_t {
    None,
    Broken, // the Freelancer exit's sequence is known gone
    Waited, // the level did not end within kExitEndWaitSeconds
    Ended,  // the variant exit's own end came (blocked)
    Gap,    // the variant exit ended with no start: kExitGapSeconds after exitActivated
};

std::string_view DoneWhyText(DoneWhy why);

struct ExitClock {
    ExitPhase phase = ExitPhase::Idle;
    ExitSource source = ExitSource::None;
    size_t exit = 0; // the Freelancer exit (Freelancer) or variant exit (Variant) it started at
    double startedAt = 0.0;
    double doneAt = -1.0; // when sequenceDone is due; < 0: not due
    DoneWhy doneWhy = DoneWhy::None;
};

// What to send now: the routed-to hijacker's exitActivated and/or sequenceDone.
struct ExitSteps {
    bool activate = false;
    bool done = false;
    DoneWhy why = DoneWhy::None;
    bool ignored = false; // another exit is already in progress (or the level ended)
};

// A Freelancer exit was confirmed (its own flow already sent exitActivated).
ExitSteps OnFreelancerConfirm(ExitClock& clock, size_t exit, double now, bool knownBroken);
// A routed variant exit started: exitActivated now, sequenceDone awaited.
ExitSteps OnVariantStart(ExitClock& clock, size_t exit, double now);
// A routed variant exit's end came (and was blocked): sequenceDone now (or
// kExitGapSeconds after exitActivated, when that was just sent).
ExitSteps OnVariantEnd(ExitClock& clock, size_t exit, double now);
// A frame: sequenceDone when due.
ExitSteps OnExitTick(ExitClock& clock, double now);
// The level ended: nothing more is sent.
void OnMissionEnd(ExitClock& clock);

// The game events that mean the level is ending ("ContractEnd", "ExitInventory").
bool IsLevelEndEvent(std::string_view name);

} // namespace fr
