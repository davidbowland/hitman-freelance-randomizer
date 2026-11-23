#include "Investigations.h"

#include "Merces.h" // JsonScalarsWithKeyContaining
#include "Rng.h"
#include "TextNoCase.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string>
#include <system_error>

namespace fr {

namespace {

constexpr std::array<std::string_view, 18> kInvestigationTypes {
    "OEIT_Generic",      "OEIT_HearBulletImpact", "OEIT_HearFootsteps", "OEIT_CarAlarm",    "OEIT_HearBark",
    "OEIT_HearPain",     "OEIT_HearRadio",        "OEIT_HearItemToss",  "OEIT_SeeItemToss", "OEIT_HearAccident",
    "OEIT_CuriousSound", "OEIT_CuriousItemSound", "OEIT_SeeWeapon",     "OEIT_SeeItem",     "OEIT_Perceptible",
    "OEIT_Toss",         "OEIT_CrowdCurious",     "OEIT_CameraDefect",
};

constexpr std::array<std::string_view, 48> kPinNames {
    // The investigate-curious helper (map users' graphs) and the perceptible entity.
    "EnablePerception",
    "DisablePerception",
    "OnInvestigated",
    "OnInvestigating",
    "OnInvestigatorAssigned",
    "OnActorInvestigated",
    "OnActorInvestigating",
    "OnActorInvestigationTerminated",
    "OnStandDown",
    "InvestigateCurious",
    "InvestigateCautious",
    "SoundSignalEmitted",
    "SoundEvent01",
    "SoundEvent02",
    // Generic starts and stops (the sound attractor's are unknown).
    "Activate",
    "Deactivate",
    "Start",
    "Stop",
    "Enable",
    "Disable",
    "Trigger",
    "Play",
    "Emit",
    "On",
    "Off",
    "SetTrue",
    "SetFalse",
    "OnTrue",
    "OnFalse",
    "Show",
    "Hide",
    "In",
    "Out",
    "Reset",
    // Situations and reports.
    "OnActivated",
    "OnDeactivated",
    "OnStarted",
    "OnStopped",
    "OnEnabled",
    "OnDisabled",
    "OnDone",
    "Completed",
    "ReportItemUsed",
    "ReportOwner",
    "ReportOwnerPosition",
    "reportSetpieceUsed",
    "DisguiseBeingInvestigated",
    "OnEvacuationStandDown",
};

constexpr std::array<std::string_view, 7> kAttractorGuesses {"Activate", "Start",   "Enable", "Trigger",
                                                             "On",       "SetTrue", "Emit"};

// The number at the start of text (leading spaces and a '+' allowed), read
// the same in every locale; empty when there is none or it is not finite.
std::optional<double> Number(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);

    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);

    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

    if (error != std::errc() || end == text.data() || !std::isfinite(value))
        return std::nullopt;

    return value;
}

std::string_view Unquote(std::string_view text) {
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        return text.substr(1, text.size() - 2);

    return text;
}

} // namespace

std::string InvestigationTypeName(int type) {
    if (type >= 0 && static_cast<size_t>(type) < kInvestigationTypes.size())
        return std::string(kInvestigationTypes[static_cast<size_t>(type)]);

    return "OEIT_#" + std::to_string(type);
}

bool AnswersBackupNoise(int type) {
    switch (type) {
        case 5:  // OEIT_HearPain
        case 9:  // OEIT_HearAccident
        case 10: // OEIT_CuriousSound
        case 14: // OEIT_Perceptible
            return true;
        default: return false;
    }
}

bool AnswersCuriousNoise(int type) {
    switch (type) {
        case 9:  // OEIT_HearAccident
        case 10: // OEIT_CuriousSound
        case 11: // OEIT_CuriousItemSound
        case 14: // OEIT_Perceptible
            return true;
        default: return false;
    }
}

NoiseKind InvestigationKind(int type) {
    switch (type) {
        case 1:  // HearBulletImpact
        case 2:  // HearFootsteps
        case 3:  // CarAlarm
        case 4:  // HearBark
        case 5:  // HearPain
        case 6:  // HearRadio
        case 7:  // HearItemToss (a coin)
        case 9:  // HearAccident
        case 10: // CuriousSound
        case 11: // CuriousItemSound
            return NoiseKind::Heard;
        case 8:  // SeeItemToss
        case 12: // SeeWeapon
        case 13: // SeeItem
        case 15: // Toss
            return NoiseKind::Seen;
        default: return NoiseKind::Other;
    }
}

const char* NoiseKindName(NoiseKind kind) {
    switch (kind) {
        case NoiseKind::Heard: return "heard";
        case NoiseKind::Seen: return "seen";
        case NoiseKind::Other: break;
    }

    return "other";
}

bool IsInvestigationBehavior(int32_t type) {
    switch (type) {
        case 17:  // BT_SituationAct
        case 18:  // BT_SituationApproach
        case 19:  // BT_SituationGetHelp
        case 20:  // BT_SituationFace
        case 21:  // BT_SituationConversation
        case 28:  // BT_SpeakCustomOrDefaultDistractionAckSoundDef
        case 29:  // BT_SpeakCustomOrDefaultDistractionInvestigationSoundDef
        case 30:  // BT_SpeakCustomOrDefaultDistractionStndSoundDef
        case 31:  // BT_Pickup
        case 42:  // BT_PickUpItem
        case 43:  // BT_GrabItem
        case 45:  // BT_Search
        case 46:  // BT_LimitedSearch
        case 49:  // BT_SituationMoveTo
        case 64:  // BT_CuriousIdle
        case 65:  // BT_InvestigateWeapon
        case 66:  // BT_DeliverWeapon
        case 69:  // BT_RadioCall
        case 72:  // BT_CautiousSearchPosition
        case 78:  // BT_CuriousBystander
        case 105: // BT_JumpyInvestigation
        case 123: // BT_SentryCheckItem
        case 137: // BT_RunToHelp
            return true;
        default: return false;
    }
}

std::optional<SituationEvent> ParseSituationEvent(std::string_view json) {
    static constexpr std::array<std::string_view, 9> kKeys {"SituationType",     "EventType",    "JoinReason",
                                                            "InvestigationType", "RepositoryId", "ActorId",
                                                            "Timestamp",         "ActorName",    "Name"};
    const std::vector<JsonScalar> scalars = JsonScalarsWithKeyContaining(json, kKeys);
    SituationEvent out;
    bool situation = false;

    for (const JsonScalar& s : scalars) {
        const std::string_view value = Unquote(s.value);

        if (s.key == "SituationType") {
            out.situation = std::string(value);
            situation = true;
        }
        else if (s.key == "EventType") {
            out.eventType = std::string(value);
        }
        else if (s.key == "JoinReason") {
            out.joinReason = std::string(value);
        }
        else if (s.key == "InvestigationType") {
            if (const auto n = Number(value))
                out.investigationType = static_cast<int>(std::lround(*n));
        }
        else if (s.key == "RepositoryId") {
            out.repo = ParseGuid(value);
        }
        else if (s.key == "ActorId") {
            out.actorId = Number(value);
        }
        else if (s.key == "Timestamp") {
            out.timestamp = Number(value);
        }
        else if (s.key == "ActorName") {
            out.actorName = std::string(value);
        }
        else if (s.key == "Name" && out.name.empty()) {
            out.name = std::string(value);
        }
    }

    if (!situation)
        return std::nullopt;

    return out;
}

bool IsTakedownEventName(std::string_view name) {
    return name == "Pacify" || name == "Kill";
}

bool IsInvestigationEventName(std::string_view name) {
    static constexpr std::array<std::string_view, 12> kNames {
        "Guard_FoundItem", "NPC_Distracted", "SituationContained",  "ItemThrown", "ItemDropped", "Spotted",
        "Witnesses",       "DisguiseBlown",  "47_FoundTrespassing", "BodyFound",  "ShotsFired",  "Trespassing",
    };
    return std::find(kNames.begin(), kNames.end(), name) != kNames.end();
}

const char* FootstepVerdictName(FootstepVerdict verdict) {
    switch (verdict) {
        case FootstepVerdict::Go: return "go";
        case FootstepVerdict::NotFootsteps: return "not footsteps";
        case FootstepVerdict::NotJoin: return "not an actor joining";
        case FootstepVerdict::NoActor: return "no repository id";
        case FootstepVerdict::Busy: return "a helper is still out";
        case FootstepVerdict::Spent: return "the per-scene limit is reached";
        case FootstepVerdict::GlobalCooldown: return "global cooldown";
        case FootstepVerdict::ActorCooldown: return "this NPC's cooldown";
    }

    return "?";
}

FootstepVerdict FootstepGate::Check(const SituationEvent& p_Event, double p_Now, bool p_Busy) const {
    if (p_Event.investigationType != kInvestigationFootsteps)
        return FootstepVerdict::NotFootsteps;

    if (p_Event.eventType != "AISE_ActorJoined")
        return FootstepVerdict::NotJoin;

    if (!p_Event.repo)
        return FootstepVerdict::NoActor;

    if (p_Busy)
        return FootstepVerdict::Busy;

    if (m_Spawned >= kFootstepMaxPerScene)
        return FootstepVerdict::Spent;

    if (m_LastAny && p_Now - *m_LastAny < kFootstepGlobalCooldown)
        return FootstepVerdict::GlobalCooldown;

    const auto it = m_Last.find(*p_Event.repo);

    if (it != m_Last.end() && p_Now - it->second < kFootstepActorCooldown)
        return FootstepVerdict::ActorCooldown;

    return FootstepVerdict::Go;
}

void FootstepGate::Mark(const Guid& p_Actor, double p_Now) {
    m_Last[p_Actor] = p_Now;
    m_LastAny = p_Now;
    ++m_Spawned;
}

void FootstepGate::MarkMiss(const Guid& p_Actor, double p_Now) {
    m_Last[p_Actor] = p_Now;
}

void FootstepGate::Reset() {
    m_Last.clear();
    m_LastAny.reset();
    m_Spawned = 0;
    m_Hearings = 0;
}

int ClampFootstepInvestigateChance(int p_Percent) {
    return std::clamp(p_Percent, 0, 100);
}

FootstepRoll DecideFootstepInvestigate(double p_U, int p_ChancePercent) {
    FootstepRoll r;
    r.u = p_U;
    const int chance = ClampFootstepInvestigateChance(p_ChancePercent);
    r.go = chance > 0 && r.u * 100.0 < static_cast<double>(chance);
    return r;
}

float FootstepReach(std::optional<float> p_HearerDistance) {
    if (!p_HearerDistance || !std::isfinite(*p_HearerDistance))
        return kFootstepReachMin;

    return std::clamp(*p_HearerDistance + kFootstepReachMargin, kFootstepReachMin, kFootstepReachMax);
}

bool IsFootstepReactionBehavior(int32_t type) {
    switch (type) {
        case 7:   // BT_Wait
        case 15:  // BT_PlayReaction
        case 16:  // BT_SimpleReaction
        case 23:  // BT_SpeakWait
        case 24:  // BT_SpeakWaitWithFallbackIfAlone
        case 28:  // BT_SpeakCustomOrDefaultDistractionAckSoundDef
        case 29:  // BT_SpeakCustomOrDefaultDistractionInvestigationSoundDef
        case 30:  // BT_SpeakCustomOrDefaultDistractionStndSoundDef
        case 34:  // BT_PlayAnimation
        case 60:  // BT_AlertedStand
        case 138: // BT_WaitForDialog
            return true;
        default: return false;
    }
}

std::string SoundEventName(int32_t type) {
    switch (type) {
        case 26: return "AIET_SoundThrownItemCollision";
        case 43: return "AIET_SoundCloseCombat";
        case 54: return "AIET_SoundAccident";
        case 55: return "AIET_SoundCurious";
        default: return "AIET_#" + std::to_string(type);
    }
}

const char* FootstepPhaseName(FootstepPhase phase) {
    switch (phase) {
        case FootstepPhase::Curious: return "curious";
        case FootstepPhase::Waiting: return "waiting";
        case FootstepPhase::Noise: return "noise";
        case FootstepPhase::Answered: return "answered";
        case FootstepPhase::Canceled: return "canceled";
    }

    return "?";
}

const char* FootstepOutcomeName(FootstepOutcome outcome) {
    switch (outcome) {
        case FootstepOutcome::None: return "none";
        case FootstepOutcome::Answered: return "he answered the curious sound";
        case FootstepOutcome::CuriousUnanswered: return "no answer to the curious sound";
        case FootstepOutcome::ReactionOver: return "his reaction is over";
        case FootstepOutcome::GaveUp: return "still reacting at the wait's end";
        case FootstepOutcome::TakenDown: return "taken down";
        case FootstepOutcome::OtherInvestigation: return "another investigation";
        case FootstepOutcome::LostFollow: return "no longer followed";
        case FootstepOutcome::Abandoned: return "abandoned";
    }

    return "?";
}

FootstepCase::FootstepCase(double p_HeardAt, bool p_Curious)
    : m_Phase(p_Curious ? FootstepPhase::Curious : FootstepPhase::Waiting), m_HeardAt(p_HeardAt) {
    if (!p_Curious)
        m_WaitingAt = p_HeardAt;
}

FootstepStep FootstepCase::Finish(FootstepPhase p_Phase, FootstepOutcome p_Outcome, bool p_Spawn) {
    FootstepStep step;
    step.outcome = p_Outcome;
    step.spawnNoise = p_Spawn;
    m_Phase = p_Phase;
    return step;
}

FootstepStep FootstepCase::Behavior(int32_t p_Type, double p_Now) {
    if (!Open())
        return {};

    m_Last = p_Type;

    if (IsFootstepReactionBehavior(p_Type)) {
        if (!m_ReactedAt)
            m_ReactedAt = p_Now;

        return {};
    }

    // A search, a pick-up, a situation act, a radio call...: not this reaction.
    if (IsInvestigationBehavior(p_Type))
        return Finish(FootstepPhase::Canceled, FootstepOutcome::OtherInvestigation, false);

    // The curious sound has its kFootstepCuriousWait s; his reaction's end is seen from the next sample after it.
    if (m_Phase == FootstepPhase::Curious)
        return {};

    // Still in his routine before the reaction shows (the join is read 0-0.3 s before BT_PlayReaction).
    if (!m_ReactedAt && p_Now - m_HeardAt < kFootstepReactGrace)
        return {};

    return Finish(FootstepPhase::Noise, FootstepOutcome::ReactionOver, true);
}

FootstepStep FootstepCase::Tick(double p_Now) {
    if (m_Phase == FootstepPhase::Curious && p_Now - m_HeardAt >= kFootstepCuriousWait) {
        FootstepStep step;
        step.outcome = FootstepOutcome::CuriousUnanswered;
        m_Phase = FootstepPhase::Waiting;
        m_WaitingAt = p_Now;
        return step;
    }

    if (m_Phase == FootstepPhase::Waiting && p_Now - m_HeardAt >= kFootstepWaitMax)
        return Finish(FootstepPhase::Canceled, FootstepOutcome::GaveUp, false);

    return {};
}

FootstepStep FootstepCase::Answer() {
    if (!Open())
        return {};

    return Finish(FootstepPhase::Answered, FootstepOutcome::Answered, false);
}

FootstepStep FootstepCase::Cancel(FootstepOutcome p_Why) {
    if (!Open())
        return {};

    return Finish(FootstepPhase::Canceled, p_Why, false);
}

bool InvestigationBook::Join(const Guid& p_Actor, int p_Type, double p_Now) {
    auto it = m_Joins.find(p_Actor);
    const bool fresh = it == m_Joins.end() || p_Now - it->second.last > kInvestigatorWatchSeconds;

    if (fresh) {
        m_Joins[p_Actor] = Entry {p_Type, p_Now, p_Now, 1};
        return true;
    }

    it->second.type = p_Type;
    it->second.last = p_Now;
    ++it->second.joins;
    return false;
}

std::optional<TakedownFacts> InvestigationBook::Takedown(const Guid& p_Actor, double p_Now) {
    const auto it = m_Joins.find(p_Actor);

    if (it == m_Joins.end())
        return std::nullopt;

    const Entry entry = it->second;
    m_Joins.erase(it);

    if (p_Now - entry.last > kInvestigatorWatchSeconds)
        return std::nullopt;

    TakedownFacts facts;
    facts.type = entry.type;
    facts.joinedAt = entry.last;
    facts.since = p_Now - entry.last;
    facts.kind = InvestigationKind(entry.type);
    // The original call-in is the join; the backup goes 10 to 30 s after it, never before the takedown.
    facts.backupFrom = std::max(entry.last + kBackupDelayMin, p_Now);
    facts.backupTo = std::max(entry.last + kBackupDelayMax, facts.backupFrom);
    facts.joins = entry.joins;
    return facts;
}

std::vector<Guid> InvestigationBook::Followed(double p_Now) const {
    std::vector<std::pair<double, Guid>> live;

    for (const auto& [actor, entry] : m_Joins) {
        if (p_Now - entry.last <= kInvestigatorWatchSeconds)
            live.emplace_back(entry.last, actor);
    }

    std::sort(live.begin(), live.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });

    std::vector<Guid> out;

    for (size_t i = 0; i < live.size() && i < kMaxFollowedInvestigators; ++i)
        out.push_back(live[i].second);

    return out;
}

std::optional<int> InvestigationBook::TypeOf(const Guid& p_Actor) const {
    const auto it = m_Joins.find(p_Actor);
    return it == m_Joins.end() ? std::nullopt : std::optional<int>(it->second.type);
}

BehaviorChange BehaviorTrack::Observe(uint64_t p_Key, int32_t p_Type) {
    BehaviorChange out;
    const auto it = m_Last.find(p_Key);

    if (it == m_Last.end()) {
        m_Last.emplace(p_Key, p_Type);
        out.changed = true;
        return out;
    }

    out.before = it->second;
    out.changed = it->second != p_Type;
    it->second = p_Type;
    return out;
}

std::optional<int32_t> BehaviorTrack::Last(uint64_t p_Key) const {
    const auto it = m_Last.find(p_Key);
    return it == m_Last.end() ? std::nullopt : std::optional<int32_t>(it->second);
}

double Distance(const std::array<float, 3>& p_A, const std::array<float, 3>& p_B) {
    const double x = static_cast<double>(p_A[0]) - p_B[0];
    const double y = static_cast<double>(p_A[1]) - p_B[1];
    const double z = static_cast<double>(p_A[2]) - p_B[2];
    return std::sqrt(x * x + y * y + z * z);
}

std::vector<std::string> PinsToFire(std::span<const std::string> p_Forwarded) {
    for (const std::string& pin : p_Forwarded) {
        if (pin == kHelperEnablePin)
            return {pin};
    }

    static constexpr std::array<std::string_view, 10> kStops {"Disable", "Stop", "Off",      "Deactivate", "Abort",
                                                              "Reset",   "Hide", "SetFalse", "Kill",       "Destroy"};
    static constexpr std::array<std::string_view, 9> kStarts {"Activate", "Start", "Enable",  "Trigger", "Play",
                                                              "Emit",     "On",    "SetTrue", "Show"};
    std::vector<std::string> out;

    for (const std::string& pin : p_Forwarded) {
        const bool stop =
            std::any_of(kStops.begin(), kStops.end(), [&](std::string_view s) { return StartsWithNoCase(pin, s); });
        const bool start =
            std::any_of(kStarts.begin(), kStarts.end(), [&](std::string_view s) { return StartsWithNoCase(pin, s); });

        if (start && !stop && std::find(out.begin(), out.end(), pin) == out.end())
            out.push_back(pin);
    }

    return out;
}

std::span<const std::string_view> InvestigationPinNames() {
    return kPinNames;
}

std::span<const std::string_view> AttractorPinGuesses() {
    return kAttractorGuesses;
}

// --- backup_guard ---------------------------------------------------------------------------------------------

namespace {

constexpr int32_t kPlayAnimation = 34; // BT_PlayAnimation: the search animation at the spot
constexpr int32_t kCloseCombat = 83;   // BT_CloseCombat: 47 grabbing him (a takedown follows)

// Walking or running somewhere (not a pick-up, a search or an ambient walk).
bool IsGoingBehavior(int32_t type) {
    return (type >= 35 && type <= 41) || (type >= 47 && type <= 51) || type == 59;
}

} // namespace

bool IsBackupNoise(int type) {
    return type == kInvestigationHearItemToss || type == kInvestigationHearRadio;
}

bool IsRoutineBehavior(int32_t type) {
    switch (type) {
        case 13: // BT_PlayAct
        case 14: // BT_ConfiguredAct
        case 52: // BT_AmbientWalk
        case 53: // BT_AmbientStand
        case 54: // BT_CrowdAmbientStand
        case 55: // BT_AmbientItemUse
        case 56: // BT_AmbientLook
        case 57: // BT_Act
        case 58: // BT_Patrol
            return true;
        default: return false;
    }
}

bool IsCallBehavior(int32_t type) {
    return IsCallStartBehavior(type) || type == 138 /* BT_WaitForDialog */;
}

bool IsCallStartBehavior(int32_t type) {
    return type == 23 /* BT_SpeakWait */ || type == 69 /* BT_RadioCall */;
}

BackupDelays ClampBackupDelays(int p_Min, int p_Max) {
    BackupDelays d;
    d.min = std::clamp(p_Min, 0, kBackupDelayLimit);
    d.max = std::max(std::clamp(p_Max, 0, kBackupDelayLimit), d.min);
    return d;
}

BackupDelayRoll RollBackupDelay(int32_t p_Seed, std::string_view p_Scene, uint32_t p_Index, BackupDelays p_Delays) {
    const BackupDelays d = ClampBackupDelays(p_Delays.min, p_Delays.max);
    BackupDelayRoll r;
    r.u = UniformFromHash(DialHash(p_Seed, p_Scene, kBackupGuardFeature, static_cast<int32_t>(p_Index)));
    r.seconds = d.min + r.u * static_cast<double>(d.max - d.min);
    return r;
}

BackupReach PlanBackupReach(std::span<const float> p_GuardDistances, std::span<const float> p_CivilianDistances,
                            std::span<const float> p_SameFloorDistances) {
    BackupReach r;
    std::vector<float> guards;

    for (const float d : p_GuardDistances) {
        if (std::isfinite(d))
            guards.push_back(d);
    }

    for (const float d : p_CivilianDistances) {
        if (std::isfinite(d) && (!r.nearestCivilian || d < *r.nearestCivilian))
            r.nearestCivilian = d;
    }

    if (guards.empty())
        return r;

    r.nearestGuard = *std::min_element(guards.begin(), guards.end());

    for (const float d : p_SameFloorDistances) {
        if (std::isfinite(d) && (!r.nearestSameFloor || d < *r.nearestSameFloor))
            r.nearestSameFloor = d;
    }

    const float target = r.nearestSameFloor.value_or(*r.nearestGuard) + kBackupRadiusMargin;
    r.widened = target > kBackupRange;
    r.wanted = std::clamp(target, kBackupRange, kBackupRadiusMax);
    const float radius = r.wanted;

    r.guardsInReach = static_cast<size_t>(
        std::count_if(guards.begin(), guards.end(), [radius](const float d) { return d < radius; }));

    if (r.guardsInReach == 0)
        return r;

    r.go = true;
    r.radius = radius;
    return r;
}

std::string_view BackupNoticeText(BackupNotice notice) {
    switch (notice) {
        case BackupNotice::Guard: return kBackupNoticeText;
        case BackupNotice::Civilian: return kBackupCivilianNoticeText;
        case BackupNotice::None: break;
    }

    return {};
}

BackupNotice BackupAnswer::Join(bool p_Guard) {
    ++(p_Guard ? guards : others);

    if (shown != BackupNotice::None)
        return BackupNotice::None;

    shown = p_Guard ? BackupNotice::Guard : BackupNotice::Civilian;
    return shown;
}

const char* BackupVerdictName(BackupVerdict verdict) {
    switch (verdict) {
        case BackupVerdict::Scheduled: return "scheduled";
        case BackupVerdict::NotArmed: return "not armed (no noise investigation, or it was finished)";
        case BackupVerdict::NoCall: return "no radio call";
        case BackupVerdict::Spent: return "the per-scene limit is reached";
    }

    return "?";
}

bool BackupBook::Join(const Guid& p_Actor, int p_Type, double p_Now) {
    if (!IsBackupNoise(p_Type) || m_Armed.contains(p_Actor))
        return false;

    BackupCase c;
    c.type = p_Type;
    c.joinedAt = p_Now;
    m_Armed.emplace(p_Actor, c);
    return true;
}

BackupStep BackupBook::Behavior(const Guid& p_Actor, int32_t p_Type, double p_Now) {
    const auto it = m_Armed.find(p_Actor);

    if (it == m_Armed.end())
        return BackupStep::None;

    BackupCase& c = it->second;

    // 47's grab (out of the guard's search animation): the takedown decides, not this.
    if (p_Type == kCloseCombat)
        return BackupStep::None;

    const std::optional<int32_t> before = c.last;
    c.last = p_Type;

    // The join is read while he still stands in his routine: only a return to it after reacting finishes.
    if (IsRoutineBehavior(p_Type)) {
        if (!c.reacted)
            return BackupStep::None;

        m_Armed.erase(it);
        return BackupStep::Finished;
    }

    // Out of the search animation at the spot: done. The "nothing here"
    // reaction (BT_WaitForDialog, BT_PlayReaction) before BT_Act is not waited for.
    if (c.arrived && before == kPlayAnimation && p_Type != kPlayAnimation) {
        m_Armed.erase(it);
        return BackupStep::Finished;
    }

    c.reacted = true;

    if (IsCallBehavior(p_Type)) {
        // The pause between two lines, a later line after the call, or a BT_WaitForDialog on its own.
        if (c.callStartAt || c.callDoneAt || !IsCallStartBehavior(p_Type))
            return BackupStep::None;

        c.callStartAt = p_Now;
        return BackupStep::CallStarted;
    }

    BackupStep step = BackupStep::None;

    if (c.callStartAt && !c.callDoneAt) {
        if (p_Now - *c.callStartAt >= kBackupCallMinSeconds) {
            c.callDoneAt = p_Now;
            step = BackupStep::CallDone;
        }
        else {
            // A bark, not a radio call: a later line may still be one.
            c.lastBarkSeconds = p_Now - *c.callStartAt;
            ++c.barks;
            c.callStartAt.reset();
            step = BackupStep::Bark;
        }
    }

    if (IsGoingBehavior(p_Type)) {
        if (!c.moved) {
            c.moved = true;

            if (step == BackupStep::None)
                step = BackupStep::Moving;
        }

        return step;
    }

    if (c.moved && !c.arrived) {
        c.arrived = true;

        if (step == BackupStep::None)
            step = BackupStep::Arrived;
    }

    return step;
}

void BackupBook::SetSpot(const Guid& p_Actor, const std::array<float, 3>& p_At) {
    if (const auto it = m_Armed.find(p_Actor); it != m_Armed.end())
        it->second.spot = p_At;
}

const char* BackupSpotSourceName(BackupSpotSource source) {
    switch (source) {
        case BackupSpotSource::Arrival: return "where the first guard reached the noise";
        case BackupSpotSource::Takedown: return "where the first guard went down (he had not reached the noise)";
        case BackupSpotSource::None: break;
    }

    return "none";
}

BackupTakedown BackupBook::Takedown(const Guid& p_Actor, double p_Now, int32_t p_Seed, std::string_view p_Scene,
                                    BackupDelays p_Delays, const std::optional<std::array<float, 3>>& p_DownedAt) {
    BackupTakedown out;
    const auto it = m_Armed.find(p_Actor);

    if (it == m_Armed.end())
        return out;

    const BackupCase c = it->second;
    m_Armed.erase(it);
    out.armed = c;

    // No call that counts: none, only barks, or cut off under kBackupCallMinSeconds into a line.
    if (!c.callDoneAt && !(c.callStartAt && p_Now - *c.callStartAt >= kBackupCallMinSeconds)) {
        out.verdict = BackupVerdict::NoCall;
        return out;
    }

    if (m_Scheduled >= static_cast<uint32_t>(kBackupMaxPerScene)) {
        out.verdict = BackupVerdict::Spent;
        return out;
    }

    BackupPlan plan;
    plan.actor = p_Actor;
    plan.type = c.type;
    plan.takedownAt = p_Now;
    plan.cutOff = !c.callDoneAt;
    plan.callDoneAt = c.callDoneAt.value_or(p_Now);
    plan.roll = RollBackupDelay(p_Seed, p_Scene, m_Scheduled, p_Delays);
    const double planned = plan.callDoneAt + plan.roll.seconds;
    plan.immediate = planned <= p_Now;
    plan.dueAt = std::max(p_Now, planned);
    if (c.spot) {
        plan.spot = c.spot;
        plan.spotSource = BackupSpotSource::Arrival;
    }
    else if (p_DownedAt) {
        plan.spot = p_DownedAt;
        plan.spotSource = BackupSpotSource::Takedown;
    }

    plan.number = ++m_Scheduled;
    m_Plans.push_back(plan);
    out.verdict = BackupVerdict::Scheduled;
    out.plan = plan;
    return out;
}

std::optional<BackupCase> BackupBook::Drop(const Guid& p_Actor) {
    const auto it = m_Armed.find(p_Actor);

    if (it == m_Armed.end())
        return std::nullopt;

    const BackupCase c = it->second;
    m_Armed.erase(it);
    return c;
}

std::vector<BackupPlan> BackupBook::Due(double p_Now) {
    std::vector<BackupPlan> due;

    for (auto it = m_Plans.begin(); it != m_Plans.end();) {
        if (it->dueAt <= p_Now) {
            due.push_back(*it);
            it = m_Plans.erase(it);
        }
        else {
            ++it;
        }
    }

    std::stable_sort(due.begin(), due.end(),
                     [](const BackupPlan& a, const BackupPlan& b) { return a.dueAt < b.dueAt; });
    return due;
}

const BackupCase* BackupBook::Armed(const Guid& p_Actor) const {
    const auto it = m_Armed.find(p_Actor);
    return it == m_Armed.end() ? nullptr : &it->second;
}

void BackupBook::Reset() {
    m_Armed.clear();
    m_Plans.clear();
    m_Scheduled = 0;
}

} // namespace fr
