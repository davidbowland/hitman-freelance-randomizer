#pragma once

// The pure parts of two features built on AI investigations (src/mod/InvestigationFeatures.h):
//
// - footsteps_investigate (on by default; also the switch of its log lines): when
//   a guard hears 47's footsteps (an Investigate_Curious event,
//   AISE_ActorJoined, InvestigationType 2 = OEIT_HearFootsteps) and the
//   cooldowns pass, footsteps_investigate_chance is rolled once for that
//   hearing (DecideFootstepInvestigate, drawn at the time); a hit marks the
//   gate then (FootstepGate::Mark) and opens a FootstepCase (below): at once
//   the mod spawns the game's "investigate here" setpiece helper
//   (setpiecehelper_investigatecurious, the noise the game's own setpieces
//   use) where 47 stood when he was heard, its sound events a curious sound
//   (kFootstepCuriousEvent) reaching him (FootstepReach), and turns its
//   perception on: a guard still inside his footstep reaction joins that
//   sound and walks over. When he does not answer it
//   within kFootstepCuriousWait s, his reaction is waited out
//   (kFootstepWaitMax s at most) and the curious helper is replaced by one
//   whose sound is a thrown item's landing (kFootstepNoiseEvent). Per-NPC and
//   global cooldowns; one helper (or pending case) at a time. Its pins, the
//   AI events and the responders are logged for kFootstepWatchSeconds, then
//   it is switched off and deleted.
// - backup_guard (on by default): a guard who hears a noise distraction
//   (a thrown item or coin, OEIT_HearItemToss; a radio, OEIT_HearRadio; not
//   footsteps, sinks, found weapons or perceptible setpieces) is armed at his
//   join (BackupBook). His radio call (BT_SpeakWait / BT_RadioCall, with
//   BT_WaitForDialog between two lines) is followed; the call ends when he
//   leaves it (SpeakWait -> MoveToLocation) and counts only when it lasted
//   kBackupCallMinSeconds (real calls take 6-9 s; a SpeakWait under a second
//   is a bark with no radio animation). BT_PlayConversation is not
//   a call. He is disarmed (no backup) back at his routine (BT_Act, ambient,
//   patrol) after reacting, or out of the search animation (BT_PlayAnimation)
//   after he reached the spot: the "nothing here" reaction that follows
//   (BT_WaitForDialog, BT_PlayReaction) is not waited for. Taken down (Pacify,
//   Kill) while still investigating after a call that counts (or 2 s or more
//   into one), a backup is scheduled at max(takedown, call end + delay), delay
//   rolled per backup between backup_guard_delay_min and _max s
//   (RollBackupDelay, seeded); with no call that counts, none. When it is due
//   the mod spawns the investigate-curious helper at the spot (where he
//   reached the noise, else where he went down) with both its sound events
//   turned into sounds of a struggle (kBackupSoundEvent).
//   The sound reaches kBackupRange, farther when that misses every guard on
//   the spot's floor (PlanBackupReach); civilians in range are not cut out (the game gives a
//   loud noise to a guard first; a civilian answers only with every guard far
//   off), and with no guard read no backup is sent. The
//   helper's perceptible part (the sphere, ZAIPerceptibleEntity m_fRange),
//   which drew a civilian walking past (OEIT_Perceptible), shrinks to
//   kBackupPerceptibleRange. The notice (backup_guard_notice) shows only when
//   a guard joins an investigation in the helper's window, once
//   (BackupAnswer): "Backup is on the way" ("Someone is coming to investigate"
//   when a civilian answers first). A civilian who answers anyway
//   (walked into reach after the spawn) shows nothing; nobody, no notice.
//
// The helper's template (entity_dump.py 00A824E80BF70DFE; property names
// from their CRC32 against ResourceLib_HM3's strings): root (aliases)
// m_bSoundCuriousEnabled true, m_fSoundRange 10 (the descriptors' m_fRange),
// m_fRadius 12 (the sphere's); a ZSphereVolumeEntity m_fRadius 12; two
// ZAISoundEventDescriptor m_eType (EAIEventType, d6af974f: AIET_SoundAccident
// 54, AIET_SoundCurious 55; civilians answer those; so does a guard still
// inside his footstep reaction, for the curious sound)
// m_fLoudness 0.5, m_fRange 10, m_bPulsing false; a ZAISoundEventProducer
// (m_rEventProcessor, m_rEventLocation and m_rEventSender the perceptible); a
// ZAIPerceptibleEntity m_aPerceptionVolumes [the sphere],
// m_bPerceptibleEnabled false, m_bSoundCuriousEnabled false, m_bVisible
// false, m_fRange 10, m_eAcknowledgeSoundDef 39, m_eInvestigateSoundDef 76,
// m_eStandDownSoundDef 46, m_eReactionBehaviorMode (FullBehavior,
// StopOnInvestigate, StopOnReacted); and a ZAIDynamicEnforcerEventEntity
// m_eventType AISET_DynamicEnforcer_Setpiece (390), m_fRange -1. None of them
// filters who answers: the dynamic enforcer events (PrivateArea,
// SuspiciousMovement, DisguiseEnforcer in other templates) make a witness
// treat 47 as trespassing, they fetch no guard; the perceptible's
// m_rPerceptorKeywordHolder (set in setpiece_trap_watercooler to a
// profession entity) needs a keyword holder entity the helper does not have;
// SSCCuriousConfiguration (m_CanGuardsInvestigate, m_CanCiviliansInvestigate)
// belongs to ZSCCuriousConfigurationEntity, which no template on the Deck
// uses (AIST_SC_*: sniper challenge). EAIEventType has no guard-only sound
// (civilians hear thrown items too).
//
// The helper's pins come from the map chunks' users of the template
// (tools/rpkg/template_users.py: EnablePerception, DisablePerception in;
// OnInvestigated out). Its own blueprint is in chunk0.rpkg, which the Deck
// copy lacks, so the mod also logs its graph at the first spawn.

#include "Guid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

constexpr int kInvestigationFootsteps = 2; // EOnlineEventInvestigationType::OEIT_HearFootsteps

// footsteps_investigate
constexpr int kDefaultFootstepInvestigateChance = 70; // footsteps_investigate_chance, percent
constexpr double kFootstepActorCooldown = 20.0;       // s of play before the same NPC's footsteps spawn a helper again
constexpr double kFootstepGlobalCooldown = 8.0;       // s of play between two helpers
constexpr double kFootstepWatchSeconds = 30.0;        // the window logged after a spawn; then DisablePerception
constexpr double kFootstepDeleteAfter = 32.0;         // s after the spawn: the helper is deleted
constexpr int kFootstepMaxPerScene = 30;              // helpers spawned per scene at most
constexpr float kFootstepReachMargin = 3.0f;          // the footstep helper's sound reaches the hearer + this
constexpr float kFootstepReachMin = 6.0f;             // ... at least
constexpr float kFootstepReachMax = 30.0f;            // ... at most

// Following investigators (both features)
constexpr double kInvestigatorWatchSeconds = 60.0; // an NPC is followed this long after he joins an investigation
constexpr double kInvestigatorSampleEvery = 0.25;  // s of play between behavior reads of a followed NPC
constexpr double kSituationScanEvery = 1.0;        // s between scans of every NPC while a helper is out
constexpr double kBackupDelayMin = 10.0;           // the takedown line's backup window: this long after the join ...
constexpr double kBackupDelayMax = 30.0;           // ... at most (log only; backup_guard has its own, below)
constexpr size_t kMaxFollowedInvestigators = 32;
constexpr uint32_t kInvestigationMaxLines = 2'000; // detail lines per scene, both features together
constexpr size_t kInvestigationMaxChars = 500;     // event JSON in one line

// OEIT_* name of an InvestigationType ("OEIT_#<n>" past the enum).
std::string InvestigationTypeName(int type);

// What made the NPC curious: a sound (footsteps, a thrown item landing, a
// radio, ...), something seen (an item, a weapon), or neither (generic,
// perceptible setpieces, crowd, a camera defect). Only a label for the log.
enum class NoiseKind { Heard, Seen, Other };
NoiseKind InvestigationKind(int type);

// True when a join of this type can be an answer to the backup helper's
// sounds (kBackupSoundEvent): OEIT_HearPain (fighting sounds
// like pain), OEIT_HearAccident, OEIT_CuriousSound (the curious setpiece
// helper's own type) and OEIT_Perceptible (its perceptible part). Not
// OEIT_Generic: it is what a guard joins for a body or for 47 (26 such
// joins in the logs), and the backup's spot is where a guard was taken down,
// so a body there would show the notice for a guard who never heard the
// noise; nor a bark, a thrown item (47's own coin near the spot) or a seen
// weapon or item. The joiner line logs every type, so a guard who answers
// with another one shows in the log.
bool AnswersBackupNoise(int type);

// True when the hearer's join of this type answers the footstep case's
// curious sound (kFootstepCuriousEvent): OEIT_HearAccident,
// OEIT_CuriousSound (what a guard still reacting joins), OEIT_CuriousItemSound,
// OEIT_Perceptible. Any other type is another investigation.
bool AnswersCuriousNoise(int type);
const char* NoiseKindName(NoiseKind kind);

// Behaviors that belong to an investigation, a report or a weapon pick-up
// (situation acts, the distraction speech lines, radio calls, pick-ups,
// curious idles, searches, ...): the NPC scan logs an NPC entering or leaving one.
bool IsInvestigationBehavior(int32_t type);

// One sent event with a "SituationType" member (Investigate_Curious so far).
struct SituationEvent {
    std::string name;
    std::optional<Guid> repo;
    std::optional<double> actorId;
    std::optional<double> timestamp;
    std::string situation;  // AIS_*
    std::string eventType;  // AISE_*
    std::string joinReason; // AISJR_*
    std::optional<int> investigationType;
    std::string actorName; // when the event names him
};

// Empty when the JSON has no "SituationType" member.
std::optional<SituationEvent> ParseSituationEvent(std::string_view json);

// An event that takes an NPC down (Pacify, Kill).
bool IsTakedownEventName(std::string_view name);

// Events logged in full while a footstep helper or a followed situation is
// active: the ones the weapon and distraction mechanisms send
// (Guard_FoundItem, NPC_Distracted, SituationContained, ItemThrown, ...).
bool IsInvestigationEventName(std::string_view name);

// footsteps_investigate's gate.
enum class FootstepVerdict { Go, NotFootsteps, NotJoin, NoActor, Busy, Spent, GlobalCooldown, ActorCooldown };
const char* FootstepVerdictName(FootstepVerdict verdict);

class FootstepGate {
public:
    // p_Busy: a helper is still out, or a footstep case is pending (FootstepCase).
    FootstepVerdict Check(const SituationEvent& p_Event, double p_Now, bool p_Busy) const;
    // p_Actor's hearing at p_Now was rolled a hit (marked at the hearing,
    // whether or not a helper follows: a failing case is not retried every footstep).
    void Mark(const Guid& p_Actor, double p_Now);
    // The chance roll for p_Actor's hearing at p_Now missed: his own cooldown
    // starts (his next footsteps within kFootstepActorCooldown are not rolled
    // again), the global cooldown and the per-scene count do not.
    void MarkMiss(const Guid& p_Actor, double p_Now);
    // The hearings that passed the gate this scene so far (rolled or not):
    // the next one's roll index.
    uint32_t TakeHearing() { return m_Hearings++; }
    int Spawned() const { return m_Spawned; }
    void Reset();

private:
    std::map<Guid, double> m_Last;
    std::optional<double> m_LastAny;
    int m_Spawned = 0;
    uint32_t m_Hearings = 0;
};

// footsteps_investigate_chance, clamped to 0..100.
int ClampFootstepInvestigateChance(int p_Percent);

// One hearing's roll: u in [0, 1), drawn when he hears (not seeded: a replay
// of the same hearings rolls anew). go: u * 100 < chance (0: never; 100:
// always).
struct FootstepRoll {
    double u = 0.0;
    bool go = false;
};

FootstepRoll DecideFootstepInvestigate(double p_U, int p_ChancePercent);

// The footstep helper's sound range: the hearer's distance from the spot (where 47 was heard) plus
// kFootstepReachMargin, within kFootstepReachMin-kFootstepReachMax; the
// margin alone (at least the minimum) when his distance is unread. Both the
// curious helper (at the hearing, his distance then) and the thrown-item
// noise (after his reaction, his distance then) use it.
float FootstepReach(std::optional<float> p_HearerDistance);

// --- footsteps_investigate: the curious sound, then the noise -----------------------------------------------------
//
// The guard who heard 47 is inside his own footstep reaction (a
// ZHandleDistractionSituation: BT_PlayReaction -> BT_AlertedStand -> a line ->
// BT_PlayAnimation -> BT_Act, 4-33 s), and the guard behavior tree ignores a
// new distraction of the same kind until he is done ("Interrupted by the same
// distraction type! / Distractions ignored!"): a thrown item's landing is a
// distraction like footsteps, so it only works after his reaction. A curious
// sound is another kind: a guard still reacting joins it (OEIT_CuriousSound)
// within seconds and walks over. (ZAIModifierActor's m_bIgnoreDistractions
// faults on Set.) So a hit is a FootstepCase:
//   Curious: the curious helper is spawned at the hearing, where 47 stood
//     (kFootstepCuriousEvent). He joins it (AnswersCuriousNoise) within
//     kFootstepCuriousWait s: Answered, no noise. Else, at the wait's end, on to
//   Waiting: the noise (the curious helper retired first, then the helper with
//     a thrown item's landing, kFootstepNoiseEvent) once his reaction is over
//     (ReactionOver): out of a reaction behavior he was seen in, or on a
//     non-reaction behavior kFootstepReactGrace s after the hearing when he
//     was never seen reacting; at most kFootstepWaitMax s after the hearing
//     (GaveUp). A late answer to the curious helper, still out, ends it too
//     (Answered).
//   Taken down, another investigation (a join of another type, or an
//   investigation behavior that is not part of the reaction), or no longer
//   followed: Canceled.
// A case whose curious helper did not spawn starts in Waiting.
constexpr double kFootstepCuriousWait = 4.0; // s after the hearing: his chance to answer the curious sound
constexpr double kFootstepWaitMax = 20.0;    // s after the hearing: his reaction waited for this long at most
constexpr double kFootstepReactGrace =
    2.0; // never seen reacting: a non-reaction behavior this long after the hearing ends it
constexpr int32_t kFootstepCuriousEvent = 55; // AIET_SoundCurious: the curious helper's sound events (both)
constexpr int32_t kFootstepNoiseEvent = 26;   // AIET_SoundThrownItemCollision: the noise's, after his reaction

// The footstep reaction's behaviors: BT_Wait, BT_PlayReaction,
// BT_SimpleReaction, BT_SpeakWait, BT_SpeakWaitWithFallbackIfAlone, the three
// distraction lines (BT_SpeakCustomOrDefaultDistraction*SoundDef),
// BT_PlayAnimation, BT_AlertedStand, BT_WaitForDialog.
bool IsFootstepReactionBehavior(int32_t type);

// The name of a sound event type the helpers are given ("AIET_#<n>" for others).
std::string SoundEventName(int32_t type);

enum class FootstepPhase { Curious, Waiting, Noise, Answered, Canceled };
const char* FootstepPhaseName(FootstepPhase phase);

enum class FootstepOutcome {
    None,
    Answered,           // he joined the curious helper's investigation: no noise
    CuriousUnanswered,  // kFootstepCuriousWait passed without an answer: on to waiting
    ReactionOver,       // waiting: his reaction is over: the noise now
    GaveUp,             // waiting: kFootstepWaitMax passed: no noise
    TakenDown,          // canceled
    OtherInvestigation, // canceled
    LostFollow,         // canceled: no longer followed (or never was)
    Abandoned,          // canceled: the scene ends, no spot or helper
};
const char* FootstepOutcomeName(FootstepOutcome outcome);

// What to do after a FootstepCase step.
struct FootstepStep {
    FootstepOutcome outcome = FootstepOutcome::None;
    bool spawnNoise = false; // retire the curious helper and spawn the noise now, at the spot where he heard 47
};

class FootstepCase {
public:
    FootstepCase() = default;
    // p_Curious: the curious helper is out; else straight to waiting.
    FootstepCase(double p_HeardAt, bool p_Curious);

    // His behavior, each sample (changed or not).
    FootstepStep Behavior(int32_t p_Type, double p_Now);
    // Every frame: the curious wait's and the reaction wait's ends.
    FootstepStep Tick(double p_Now);
    // He joined the curious helper's investigation (AnswersCuriousNoise; an open case only).
    FootstepStep Answer();
    // Taken down, another investigation, lost follow, scene end: canceled (an open case only).
    FootstepStep Cancel(FootstepOutcome p_Why);

    FootstepPhase Phase() const { return m_Phase; }
    bool Open() const { return m_Phase == FootstepPhase::Curious || m_Phase == FootstepPhase::Waiting; }
    std::optional<double> ReactedAt() const { return m_ReactedAt; }
    std::optional<double> WaitingAt() const { return m_WaitingAt; } // when waiting began
    std::optional<int32_t> Last() const { return m_Last; }
    double HeardAt() const { return m_HeardAt; }

private:
    FootstepStep Finish(FootstepPhase p_Phase, FootstepOutcome p_Outcome, bool p_Spawn);

    FootstepPhase m_Phase = FootstepPhase::Canceled;
    double m_HeardAt = 0.0;
    std::optional<double> m_ReactedAt;
    std::optional<double> m_WaitingAt;
    std::optional<int32_t> m_Last;
};

// Who is in an investigation, for the takedown lines.
struct TakedownFacts {
    int type = 0;          // the InvestigationType he joined with
    double joinedAt = 0.0; // s of play
    double since = 0.0;    // s from the join to the takedown
    NoiseKind kind = NoiseKind::Other;
    double backupFrom = 0.0; // s of play: the planned backup window (log only)
    double backupTo = 0.0;
    int joins = 0; // joins of his within the watch
};

class InvestigationBook {
public:
    // p_Actor joined an investigation of p_Type at p_Now. True when he was
    // not followed already (a new follow).
    bool Join(const Guid& p_Actor, int p_Type, double p_Now);
    // A takedown of p_Actor at p_Now: his last join within
    // kInvestigatorWatchSeconds, then forgotten.
    std::optional<TakedownFacts> Takedown(const Guid& p_Actor, double p_Now);
    // The NPCs followed at p_Now (joined within kInvestigatorWatchSeconds),
    // the newest first, at most kMaxFollowedInvestigators.
    std::vector<Guid> Followed(double p_Now) const;
    std::optional<int> TypeOf(const Guid& p_Actor) const;
    void Reset() { m_Joins.clear(); }

private:
    struct Entry {
        int type = 0;
        double first = 0.0;
        double last = 0.0;
        int joins = 0;
    };

    std::map<Guid, Entry> m_Joins;
};

// The last behavior read per NPC: Observe says whether it changed.
struct BehaviorChange {
    bool changed = false;
    std::optional<int32_t> before; // empty the first time
};

class BehaviorTrack {
public:
    BehaviorChange Observe(uint64_t p_Key, int32_t p_Type);
    std::optional<int32_t> Last(uint64_t p_Key) const;
    void Forget(uint64_t p_Key) { m_Last.erase(p_Key); }
    void Reset() { m_Last.clear(); }

private:
    std::map<uint64_t, int32_t> m_Last;
};

double Distance(const std::array<float, 3>& p_A, const std::array<float, 3>& p_B);

// Which of a spawned helper's input pins (its template's input forwardings,
// as read at runtime) to signal: EnablePerception when it has one; else
// every pin whose name starts something (Activate, Start, Enable..., Trigger,
// Play, Emit, On, SetTrue, Show) and none that stops (Disable, Stop, Off,
// Deactivate, Abort, Reset, Hide, SetFalse, Kill, Destroy).
std::vector<std::string> PinsToFire(std::span<const std::string> p_Forwarded);

// Pin names the features look for (inputs and outputs of the helper, the sound
// attractor and the situation entities), to name pin ids in the log.
std::span<const std::string_view> InvestigationPinNames();

// When the forwarded input pins could not be read: what is signaled on the
// helper (its known pin) and on the sound attractor (guesses).
constexpr std::string_view kHelperEnablePin = "EnablePerception";
constexpr std::string_view kHelperDisablePin = "DisablePerception";
std::span<const std::string_view> AttractorPinGuesses();

// --- backup_guard ---------------------------------------------------------------------------------------------

constexpr int kInvestigationHearRadio = 6;    // OEIT_HearRadio
constexpr int kInvestigationHearItemToss = 7; // OEIT_HearItemToss (a thrown item, a coin)
constexpr int kDefaultBackupDelayMin = 20;    // backup_guard_delay_min, s after the first guard's call ended
constexpr int kDefaultBackupDelayMax = 45;    // backup_guard_delay_max
constexpr int kBackupDelayLimit = 120;        // both clamped to 0..this
constexpr int kBackupMaxPerScene = 10;        // backups scheduled per scene at most
constexpr double kBackupWatchSeconds = 30.0;  // the backup helper's perception is on this long
constexpr double kBackupDeleteAfter = 32.0;   // then it is deleted
constexpr double kBackupNoticeSeconds = 4.0;  // "Backup is on the way" stays this long
constexpr double kBackupCallMinSeconds = 2.0; // a radio call this long at least; shorter is a bark
// The struggle's range at least: at 64 m the guards who came stood 22-28 m off; at 16 m the one guard in it did not
// come.
constexpr float kBackupRange = 30.0f;
constexpr float kBackupFloorHeight = 3.0f;  // a guard within this height of the spot is on its floor
constexpr float kBackupRadiusMargin = 5.0f; // past the guard it is widened to
constexpr float kBackupRadiusMax = 80.0f;
constexpr size_t kBackupNearestLogged = 3;      // the nearest guards named in the dispatch line
constexpr float kBackupPerceptibleRange = 1.0f; // the backup helper's sphere and perceptible range (sound only)
// The backup helper's sound events (both): AIET_SoundCloseCombat, sounds of a
// struggle (EAIEventType 43, Glacier/Enums.h). Guards answer it with a
// cautious search (no join event: ScanBackupAnswer reads it); at a 64 m
// range half the map searches, hence the tight reach. The curious sound (55)
// is answered only sometimes (guards 12 m off can ignore it); the thrown
// item's landing (26) makes the answering guard radio "somebody is throwing
// things around here".
constexpr int32_t kBackupSoundEvent = 43;
constexpr std::string_view kSoundEventTypeProperty = "m_eType"; // ZAISoundEventDescriptor's EAIEventType
constexpr std::string_view kBackupGuardFeature = "backup_guard";
constexpr std::string_view kBackupNoticeText = "Backup is on the way";                     // a guard answered
constexpr std::string_view kBackupCivilianNoticeText = "Someone is coming to investigate"; // a civilian answered first

// A noise distraction: a thrown item or coin, a radio. Not footsteps, sinks
// (perceptible setpieces), found weapons or anything seen.
bool IsBackupNoise(int type);

// BT_Act, the ambient behaviors, patrol, acts: what a guard goes back to after an investigation.
bool IsRoutineBehavior(int32_t type);
// The radio call: BT_SpeakWait, BT_RadioCall, and BT_WaitForDialog between two speak lines.
bool IsCallBehavior(int32_t type);
// What starts a call: BT_SpeakWait, BT_RadioCall (BT_WaitForDialog alone does not).
bool IsCallStartBehavior(int32_t type);

// backup_guard_delay_min / _max, each clamped to 0..kBackupDelayLimit, max raised to min when below it.
struct BackupDelays {
    int min = kDefaultBackupDelayMin;
    int max = kDefaultBackupDelayMax;
};
BackupDelays ClampBackupDelays(int p_Min, int p_Max);

// One backup's delay: u from DialHash(seed, scene, kBackupGuardFeature, index); seconds = min + u * (max - min).
struct BackupDelayRoll {
    double u = 0.0;
    double seconds = 0.0;
};
BackupDelayRoll RollBackupDelay(int32_t p_Seed, std::string_view p_Scene, uint32_t p_Index, BackupDelays p_Delays);

// The backup helper's sound range from the distances to the spot of the
// living, standing guards, of those of them on the spot's floor (within
// kBackupFloorHeight of it; the guards called kept being on another floor)
// and of everyone else (civilians, VIPs; any order, not finite ones ignored):
// kBackupRange, widened to the nearest guard on the floor + kBackupRadiusMargin
// (the nearest guard's when none is on it) when he is farther, up to
// kBackupRadiusMax. go: at least one guard stands inside it (closer than
// it); else (no guard read, or all past the cap) no backup. The nearest
// civilian is kept for the log only.
struct BackupReach {
    bool go = false;
    float radius = 0.0f;      // the sound range when go (0 otherwise)
    float wanted = 0.0f;      // the range planned
    size_t guardsInReach = 0; // guards closer than the range
    std::optional<float> nearestGuard;
    std::optional<float> nearestSameFloor; // the nearest guard on the spot's floor
    bool widened = false;                  // past kBackupRange to reach him (or the nearest guard)
    std::optional<float> nearestCivilian;
};
BackupReach PlanBackupReach(std::span<const float> p_GuardDistances, std::span<const float> p_CivilianDistances,
                            std::span<const float> p_SameFloorDistances = {});

// The notice a backup's helper shows: none, a guard answered (kBackupNoticeText)
// or a civilian answered first (kBackupCivilianNoticeText).
enum class BackupNotice : uint32_t { None, Guard, Civilian };
std::string_view BackupNoticeText(BackupNotice notice); // empty for None

// Who answered a backup's helper in its window. The first to answer decides
// the notice, once per backup: a guard (eAT_Guard) "Backup is on the way", a
// civilian "Someone is coming to investigate"; later answers are counted and
// show nothing; nobody answers: no notice.
struct BackupAnswer {
    uint32_t guards = 0;
    uint32_t others = 0;
    BackupNotice shown = BackupNotice::None;
    // A joiner; the notice to show now (None when one was shown already).
    BackupNotice Join(bool p_Guard);
};

// What a behavior change of an armed guard meant.
enum class BackupStep {
    None,
    CallStarted, // into a speak line that may be his radio call
    CallDone,    // out of a call that lasted kBackupCallMinSeconds: it counts
    Bark,        // out of a shorter line: not a call (a later line may still be)
    Moving,      // the first movement after the join
    Arrived,     // out of the movement into a search, a pick-up, ... (at the spot)
    Finished,    // back to his routine after reacting, or out of the search animation at the spot: disarmed, no backup
};

// An armed guard (BackupBook).
struct BackupCase {
    int type = 0;
    double joinedAt = 0.0;
    std::optional<double> callStartAt; // the line in progress, or the call that counted
    std::optional<double> callDoneAt;  // set only for a call that counts
    uint32_t barks = 0;                // lines under kBackupCallMinSeconds
    double lastBarkSeconds = 0.0;
    std::optional<int32_t> last; // the last behavior seen
    bool reacted = false;        // a non-routine behavior was seen since the join
    bool moved = false;
    bool arrived = false;
    std::optional<std::array<float, 3>> spot; // where he arrived (the mod reads it)
};

// Why a takedown schedules no backup, or that it does. NoCall: no radio call
// that counts (none, only barks, or under kBackupCallMinSeconds into one).
enum class BackupVerdict { Scheduled, NotArmed, NoCall, Spent };
const char* BackupVerdictName(BackupVerdict verdict);

// Where the backup goes: where the first guard reached the noise (the thrown
// item's landing spot is in no event: ItemThrown and ItemDropped carry no
// position), else where he went down, else nowhere (no backup).
enum class BackupSpotSource { Arrival, Takedown, None };
const char* BackupSpotSourceName(BackupSpotSource source);

struct BackupPlan {
    uint32_t number = 0; // 1, 2, ... this scene
    Guid actor;          // the guard taken down
    int type = 0;
    double takedownAt = 0.0;
    double callDoneAt = 0.0; // the call's end (the takedown when he was cut off mid-call)
    bool cutOff = false;     // taken down during his call
    BackupDelayRoll roll;
    double dueAt = 0.0;                       // max(takedown, call end + delay)
    bool immediate = false;                   // the delay had already run out at the takedown
    std::optional<std::array<float, 3>> spot; // where the backup goes
    BackupSpotSource spotSource = BackupSpotSource::None;
};

struct BackupTakedown {
    BackupVerdict verdict = BackupVerdict::NotArmed;
    std::optional<BackupCase> armed; // his case, when he was armed
    std::optional<BackupPlan> plan;  // when Scheduled
};

class BackupBook {
public:
    // p_Actor joined an investigation of p_Type at p_Now: a noise arms him
    // (true). A noise join while he is armed keeps his case (false); any other
    // type changes nothing (false).
    bool Join(const Guid& p_Actor, int p_Type, double p_Now);
    // A behavior change of p_Actor (sampled). Finished disarms him.
    BackupStep Behavior(const Guid& p_Actor, int32_t p_Type, double p_Now);
    void SetSpot(const Guid& p_Actor, const std::array<float, 3>& p_At);
    // He was taken down at p_Now (p_DownedAt: where, when read): a backup is
    // scheduled when he was armed and made a radio call that counts (cut off
    // kBackupCallMinSeconds or more into it: the call ends at the takedown).
    BackupTakedown Takedown(const Guid& p_Actor, double p_Now, int32_t p_Seed, std::string_view p_Scene,
                            BackupDelays p_Delays, const std::optional<std::array<float, 3>>& p_DownedAt);
    // He is no longer followed (the follow window ran out): disarmed, his case returned.
    std::optional<BackupCase> Drop(const Guid& p_Actor);
    // The plans due at p_Now, oldest first, taken out.
    std::vector<BackupPlan> Due(double p_Now);
    const BackupCase* Armed(const Guid& p_Actor) const;
    size_t Pending() const { return m_Plans.size(); }
    uint32_t Scheduled() const { return m_Scheduled; }
    void Reset();

private:
    std::map<Guid, BackupCase> m_Armed;
    std::vector<BackupPlan> m_Plans;
    uint32_t m_Scheduled = 0;
};

} // namespace fr
