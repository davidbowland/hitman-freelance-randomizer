#pragma once

// A map variant's own escalation commentary muted, and Sloth's vitality kept
// full (generic hints found at runtime; Sloth's vitality helper by its ids).
//
// Sloth's ancestral_harebell (Dartmoor) has a "Handler"
// group: the sin's voice comments on 47 through twelve ZHintEntity, queued on
// the brick's one ZHintManagerEntity ("HintManagerEntity"). Each hint's
// OnHintStarted output starts an ActorOneLiner (or a sweetdialog) and aborts
// its own timeout; the line's WaitDone signals the hint's HintCompleted:
// - "Comment on vitality at 90/70/50/30/10/0" (pdlc41003 to pdlc41008
//   vitalityhits*), queued by the WentBelowMin of six FloatRangeComparator
//   ("Reach 90" ... "Reach 10", "Hitman damage") that watch the vitality;
// - "Comment on standing" / "Comment on running" (47standstill, 47sprinting),
//   queued by "Standing cooldown" / "Running cooldown" (ValueBool_Timeout);
// - "Comment on takedown" (subduewarn), "Comment on target spoteted",
//   "Report target down", "Report mission complete" (vitoffgraveyard): the
//   escalation's own targets and finish.
// No other variant brick the mod loads has a ZHintEntity (every brick of
// core/Variants.cpp, read from the game's packages). Freelancer has no
// vitality, so every ZHintEntity in the variant's own bricks is muted: its
// OnHintStarted output is blocked (no line starts; its timeout is not
// aborted, and only unqueues it). The hint manager then waits on that hint
// for good, which holds back only the other hints, all muted alike.
//
// The vitality itself (helper_harebell_vitality, a valuefloat template
// instance at 100, clamped 0 to 100) changes only through its AddRemove
// input, fed by ten pins of the brick: "Movement cost", "Takedown cost",
// "Agility cost", "Shooting cost" (Change), "Graceful throwing", "body
// dumping", "pushing", "melee", "kicking" and "Jogger eliminated or pacified"
// (OnTrue). Each of those pins is blocked as a mission story's feed is
// (fr::FindStoryMutes: every pin connection into the instance), its signal
// sent on along its other links ("Damage effect" Trigger, "JoggerCompleted"
// Trigger, "Float to void"), so the vitality stays at 100: none of the
// comments' thresholds is crossed, the music's tension ("HighTension_Intensity")
// does not rise, "47RanOutofStamina" never reaches "harebell_dream", the
// threshold stinger ("47TakesDamage_Sound") never plays, and "Depleted"
// never switches the timed damage on (core/VariantDamage.h stays as the
// safety net). Freelancer's own bricks are never searched.
//
// Envy and Lust talk without hints (decoded with tools/rpkg): their lines are ActorOneLiner and sweetdialog
// instances started straight from a pin, found by their entity ids
// (KnownSinLines).
// - Envy's scenario_frangipani (Mendoza) has a "HandlerSpeak" group: nine
//   lines (pdlc44007 to pdlc44015: the rival's kills, 47's kills, who wins, 47
//   killing the rival, 47's score low or high), each spoken by its own
//   ZHandlerStandIn and started by one ZEventReceiverEntity_void's
//   EventReceived, which also sets "Envy is Talking"; the line's end
//   (Completed, OnDone) clears it. Nothing in the brick reads that flag but an
//   unused inverted condition. Its intro and level won/lost lines (pdlc44000
//   to pdlc44006) live in the level bricks, which are not loaded.
// - Lust's scenario_ambrosia (Berlin): thirteen lines in Ambrosia's voice
//   (pdlc42000 to pdlc42012: the intro dream and the rules, clues found, the
//   bouquet, the explosives, keys and dart gun picked up, the admirer found,
//   a pretender killed or wrongly named, idle chat). The intro dream starts
//   from the "Ambrosia intro cut director" OnPlay (with the dream's effects),
//   the rules from a TimerSimple's Out, every other line from its drama
//   situation's DramaSituationProxy OnStart, and each of those lines' end
//   signals the proxy's Done, which ends the situation.
// Each pin that starts a line is blocked (FindSinLineMutes), its signal sent
// on along its other links and then along the line's own end links, so the
// line is skipped as if it had played: Envy's flag is set and cleared, Lust's
// drama situations end at once instead of waiting for good. The rival's own
// sounds (Envy's "assassin trash talk") and Lust's people and items stay.
// Both bricks' own blueprints are copied while the scene loads (each has a
// game event listener and a Trigger input: fr::MayHoldBriefing).

#include "MissionStories.h" // fr::StoryFind

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// The engine class of the commentary's hints.
constexpr std::string_view kHintEntityClass = "ZHintEntity";

// Whether an engine class name is ZHintEntity (any case).
bool IsHintClass(std::string_view className);

// The hint's output pin that starts its line: blocked.
constexpr std::string_view kHintStartedPin = "OnHintStarted";

// The hint's input pins that queue it: a blueprint with a connection into one
// is copied while the scene loads (BriefingGraphs.h), so the vitality helper's
// feeds next to it can be read.
constexpr std::string_view kHintQueuePins[] = {"QueueHint", "QueueHintWithoutContext"};
bool IsHintQueuePin(std::string_view pin);

// helpers_harebell.template?/helper_harebell_vitality.entitytemplate: an
// instance's own blueprint factory has one of these resource ids.
constexpr uint64_t kHarebellVitalityTblu = 0x00903F63441229D8ull;
constexpr uint64_t kHarebellVitalityTemp = 0x007917CA28F13B3Eull;
bool IsVitalityResource(uint64_t rid);

// A line of a sin's own voice (Envy's, Lust's), by its entity id in its brick.
struct SinLine {
    uint64_t id;           // the line's sub-entity id (m_nEntityID)
    std::string_view sin;  // "Envy"
    std::string_view name; // its entity name ("AOL_pdlc44007_rivalkillwarn")
};

// Envy's nine and Lust's thirteen.
std::span<const SinLine> KnownSinLines();

// The known line with this entity id, or null.
const SinLine* FindSinLine(uint64_t id, std::span<const SinLine> known = KnownSinLines());

// The line's output pins that say it ended (an ActorOneLiner's, a
// sweetdialog's): their links are signaled when the line is skipped.
constexpr std::string_view kLineEndPins[] = {"Completed", "OnDone"};
bool IsLineEndPin(std::string_view pin);

// FindStoryMutes over the lines (every pin connection into one), each mute's
// resend then extended by the end links of the lines it no longer reaches, in
// link order, so the line is skipped as if it had played. A mute whose resend
// would then pass kLineMaxResend moves to skipped (TooManyLinks).
StoryFind FindSinLineMutes(const std::vector<int32_t>& lines, size_t count, const std::vector<BriefingLink>& links);

// The one log line's counts.
struct CommentTally {
    size_t hints = 0;        // ZHintEntity found
    size_t hintsMuted = 0;   // their OnHintStarted blocked
    size_t vitality = 0;     // vitality helper instances found
    size_t feedsBlocked = 0; // pins into them blocked
    size_t feedsLeft = 0;    // pins into them left on (too many links, forwarded, or not armed)
    std::string vitalityWhy; // why a vitality helper's feeds were not read ("its blueprint was not read: ...")
    size_t lines = 0;        // known sin lines found (KnownSinLines)
    size_t linesMuted = 0;   // every pin that starts them blocked
    size_t lineFeeds = 0;    // pins that start them, blocked
    std::string linesWhy;    // why their blueprint's pins were not read ("its blueprint was not read: ...")
};

// The log line's text after "variant: <option>: ": "vitality comments muted
// (12 ZHintEntity, blocked at OnHintStarted; vitality kept full: 10 pins into
// helper_harebell_vitality blocked, their other logic sent on)", "the sin's
// lines muted (9, each skipped as if it had played: 9 pins that start them
// blocked)", with what was left on; "commentary: none in this variant's
// bricks" when nothing was found.
std::string CommentTallyText(const CommentTally& tally);

} // namespace fr
