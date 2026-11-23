#include "doctest.h"

#include "Config.h"
#include "Investigations.h"
#include "Rng.h"
#include "SettingsTable.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

// An Investigate_Curious event as the game sends it, user and session ids cut.
constexpr std::string_view kFootsteps =
    R"({"Timestamp":9.835279,"Name":"Investigate_Curious","ContractSessionId":"x","ContractId":"89500fa3-2466-4c52-8b90-9890cc5039ee","Value":{"ActorId":3741843000.000000,"RepositoryId":"db85e020-98d2-485e-ba21-ebfc53b91daa","SituationType":"AIS_INVESTIGATE_CURIOUS","EventType":"AISE_ActorJoined","JoinReason":"AISJR_Default","InvestigationType":2.000000},"Origin":"gameclient"})";

fr::Guid G(const char* p_Text) {
    return *fr::ParseGuid(p_Text);
}

fr::SituationEvent Footsteps(const char* p_Repo) {
    fr::SituationEvent e;
    e.name = "Investigate_Curious";
    e.eventType = "AISE_ActorJoined";
    e.investigationType = fr::kInvestigationFootsteps;
    e.repo = G(p_Repo);
    return e;
}

constexpr const char* kA = "db85e020-98d2-485e-ba21-ebfc53b91daa";
constexpr const char* kB = "4fa7758f-14d4-4563-a336-e8c4e6fdbe8a";

} // namespace

TEST_CASE("investigations: an Investigate_Curious event parses") {
    const auto e = fr::ParseSituationEvent(kFootsteps);
    REQUIRE(e);
    CHECK(e->name == "Investigate_Curious");
    CHECK(e->situation == "AIS_INVESTIGATE_CURIOUS");
    CHECK(e->eventType == "AISE_ActorJoined");
    CHECK(e->joinReason == "AISJR_Default");
    REQUIRE(e->investigationType);
    CHECK(*e->investigationType == 2);
    REQUIRE(e->repo);
    CHECK(fr::FormatGuid(*e->repo) == kA);
    REQUIRE(e->actorId);
    CHECK(*e->actorId == doctest::Approx(3741843000.0));
    REQUIRE(e->timestamp);
    CHECK(*e->timestamp == doctest::Approx(9.835279));
}

TEST_CASE("investigations: numbers that are not finite are not read") {
    const auto e = fr::ParseSituationEvent(
        R"({"Name":"Investigate_Curious","Value":{"SituationType":"AIS_INVESTIGATE_CURIOUS","ActorId":nan,"Timestamp":inf,"InvestigationType":2}})");
    REQUIRE(e);
    CHECK_FALSE(e->actorId);
    CHECK_FALSE(e->timestamp);
    REQUIRE(e->investigationType);
    CHECK(*e->investigationType == 2);
}

TEST_CASE("investigations: an event without SituationType is not a situation event") {
    CHECK_FALSE(fr::ParseSituationEvent(
        R"({"Name":"Pacify","Value":{"RepositoryId":"db85e020-98d2-485e-ba21-ebfc53b91daa"}})"));
    CHECK_FALSE(fr::ParseSituationEvent(""));
}

TEST_CASE("investigations: type names and kinds") {
    CHECK(fr::InvestigationTypeName(2) == "OEIT_HearFootsteps");
    CHECK(fr::InvestigationTypeName(7) == "OEIT_HearItemToss");
    CHECK(fr::InvestigationTypeName(17) == "OEIT_CameraDefect");
    CHECK(fr::InvestigationTypeName(18) == "OEIT_#18");
    CHECK(fr::InvestigationTypeName(-1) == "OEIT_#-1");
    CHECK(fr::InvestigationKind(2) == fr::NoiseKind::Heard);
    CHECK(fr::InvestigationKind(7) == fr::NoiseKind::Heard);
    CHECK(fr::InvestigationKind(6) == fr::NoiseKind::Heard);
    CHECK(fr::InvestigationKind(12) == fr::NoiseKind::Seen);
    CHECK(fr::InvestigationKind(14) == fr::NoiseKind::Other);
    CHECK(fr::InvestigationKind(0) == fr::NoiseKind::Other);
}

TEST_CASE("investigations: investigation behaviors") {
    CHECK(fr::IsInvestigationBehavior(30));       // the stand-down line
    CHECK(fr::IsInvestigationBehavior(65));       // BT_InvestigateWeapon
    CHECK(fr::IsInvestigationBehavior(69));       // BT_RadioCall
    CHECK_FALSE(fr::IsInvestigationBehavior(57)); // BT_Act
    CHECK_FALSE(fr::IsInvestigationBehavior(52)); // BT_AmbientWalk
}

TEST_CASE("investigations: takedown and investigation event names") {
    CHECK(fr::IsTakedownEventName("Pacify"));
    CHECK(fr::IsTakedownEventName("Kill"));
    CHECK_FALSE(fr::IsTakedownEventName("Unnoticed_Pacified"));
    CHECK(fr::IsInvestigationEventName("Guard_FoundItem"));
    CHECK(fr::IsInvestigationEventName("NPC_Distracted"));
    CHECK_FALSE(fr::IsInvestigationEventName("ItemPickedUp"));
}

TEST_CASE("footstep gate: only footsteps of a joining NPC with a repository id") {
    fr::FootstepGate gate;
    fr::SituationEvent e = Footsteps(kA);
    CHECK(gate.Check(e, 1.0, false) == fr::FootstepVerdict::Go);

    e.investigationType = 7;
    CHECK(gate.Check(e, 1.0, false) == fr::FootstepVerdict::NotFootsteps);
    e.investigationType.reset();
    CHECK(gate.Check(e, 1.0, false) == fr::FootstepVerdict::NotFootsteps);

    e = Footsteps(kA);
    e.eventType = "AISE_ActorLeft";
    CHECK(gate.Check(e, 1.0, false) == fr::FootstepVerdict::NotJoin);

    e = Footsteps(kA);
    e.repo.reset();
    CHECK(gate.Check(e, 1.0, false) == fr::FootstepVerdict::NoActor);
    CHECK(gate.Check(Footsteps(kA), 1.0, true) == fr::FootstepVerdict::Busy);
}

TEST_CASE("footstep gate: global and per-NPC cooldowns") {
    fr::FootstepGate gate;
    gate.Mark(G(kA), 10.0);
    CHECK(gate.Check(Footsteps(kB), 10.0 + fr::kFootstepGlobalCooldown - 0.1, false) ==
          fr::FootstepVerdict::GlobalCooldown);
    CHECK(gate.Check(Footsteps(kB), 10.0 + fr::kFootstepGlobalCooldown, false) == fr::FootstepVerdict::Go);
    CHECK(gate.Check(Footsteps(kA), 10.0 + fr::kFootstepGlobalCooldown, false) == fr::FootstepVerdict::ActorCooldown);
    CHECK(gate.Check(Footsteps(kA), 10.0 + fr::kFootstepActorCooldown, false) == fr::FootstepVerdict::Go);
    CHECK(gate.Spawned() == 1);
    gate.Reset();
    CHECK(gate.Spawned() == 0);
    CHECK(gate.Check(Footsteps(kA), 10.5, false) == fr::FootstepVerdict::Go);
}

TEST_CASE("footstep gate: at most kFootstepMaxPerScene helpers a scene") {
    fr::FootstepGate gate;

    for (int i = 0; i < fr::kFootstepMaxPerScene; ++i)
        gate.Mark(G(kA), i * 100.0);

    CHECK(gate.Check(Footsteps(kB), 1e6, false) == fr::FootstepVerdict::Spent);
}

TEST_CASE("footstep gate: a missed roll starts only that NPC's cooldown") {
    fr::FootstepGate gate;
    gate.MarkMiss(G(kA), 10.0);
    CHECK(gate.Check(Footsteps(kA), 10.0 + fr::kFootstepActorCooldown - 0.1, false) ==
          fr::FootstepVerdict::ActorCooldown);
    CHECK(gate.Check(Footsteps(kA), 10.0 + fr::kFootstepActorCooldown, false) == fr::FootstepVerdict::Go);
    // No global cooldown, no helper counted.
    CHECK(gate.Check(Footsteps(kB), 10.0, false) == fr::FootstepVerdict::Go);
    CHECK(gate.Spawned() == 0);
}

TEST_CASE("footstep gate: hearings are numbered per scene") {
    fr::FootstepGate gate;
    CHECK(gate.TakeHearing() == 0);
    CHECK(gate.TakeHearing() == 1);
    CHECK(gate.TakeHearing() == 2);
    gate.Reset();
    CHECK(gate.TakeHearing() == 0);
}

TEST_CASE("footstep roll: 0 never, 100 always, about the chance in between") {
    int hits = 0;

    for (uint32_t n = 0; n < 2000; ++n) {
        const double u = (n + 0.5) / 2000.0;
        CHECK(fr::DecideFootstepInvestigate(u, 50).u == u);
        CHECK_FALSE(fr::DecideFootstepInvestigate(u, 0).go);
        CHECK(fr::DecideFootstepInvestigate(u, 100).go);
        CHECK_FALSE(fr::DecideFootstepInvestigate(u, -5).go);
        CHECK(fr::DecideFootstepInvestigate(u, 150).go);
        hits += fr::DecideFootstepInvestigate(u, 50).go ? 1 : 0;
    }

    CHECK(hits == 1000);
    CHECK(fr::DecideFootstepInvestigate(0.0, 1).go);
    CHECK_FALSE(fr::DecideFootstepInvestigate(0.01, 1).go);
    CHECK(fr::ClampFootstepInvestigateChance(-1) == 0);
    CHECK(fr::ClampFootstepInvestigateChance(55) == 55);
    CHECK(fr::ClampFootstepInvestigateChance(101) == 100);
}

TEST_CASE("footsteps_investigate: settings rows and defaults") {
    const fr::SettingInfo* on = fr::FindSetting("footsteps_investigate");
    REQUIRE(on != nullptr);
    CHECK(on->label == "Guards investigate footsteps");
    CHECK(on->group == fr::SettingGroup::Footsteps);
    CHECK(on->def == "true");
    CHECK(fr::Config {}.footstepsInvestigate);

    const fr::SettingInfo* chance = fr::FindSetting("footsteps_investigate_chance");
    REQUIRE(chance != nullptr);
    CHECK(chance->group == fr::SettingGroup::Footsteps);
    CHECK(chance->parent == "footsteps_investigate");
    CHECK(chance->type == fr::SettingType::Int);
    CHECK(chance->def == "70");
    CHECK(chance->min == 0.0);
    CHECK(chance->max == 100.0);
    CHECK(chance->step == 5.0);
    CHECK(fr::Config {}.footstepsInvestigateChance == fr::kDefaultFootstepInvestigateChance);
    CHECK(fr::kDefaultFootstepInvestigateChance == 70);
}

TEST_CASE("investigation book: a takedown within the watch gives the backup window") {
    fr::InvestigationBook book;
    CHECK(book.Join(G(kA), 7, 100.0));
    CHECK_FALSE(book.Join(G(kA), 7, 101.0)); // the same follow
    const auto t = book.Takedown(G(kA), 105.0);
    REQUIRE(t);
    CHECK(t->type == 7);
    CHECK(t->kind == fr::NoiseKind::Heard);
    CHECK(t->joinedAt == doctest::Approx(101.0));
    CHECK(t->since == doctest::Approx(4.0));
    CHECK(t->backupFrom == doctest::Approx(111.0));
    CHECK(t->backupTo == doctest::Approx(131.0));
    CHECK(t->joins == 2);
    CHECK_FALSE(book.Takedown(G(kA), 106.0)); // forgotten
}

TEST_CASE("investigation book: a late takedown starts the backup no earlier than now") {
    fr::InvestigationBook book;
    book.Join(G(kA), 2, 0.0);
    const auto t = book.Takedown(G(kA), 25.0);
    REQUIRE(t);
    CHECK(t->backupFrom == doctest::Approx(25.0));
    CHECK(t->backupTo == doctest::Approx(30.0));

    book.Join(G(kB), 2, 0.0);
    CHECK_FALSE(book.Takedown(G(kB), fr::kInvestigatorWatchSeconds + 1.0));
}

TEST_CASE("investigation book: followed NPCs, newest first, past joins dropped") {
    fr::InvestigationBook book;
    book.Join(G(kA), 2, 0.0);
    book.Join(G(kB), 7, 5.0);
    std::vector<fr::Guid> f = book.Followed(10.0);
    REQUIRE(f.size() == 2);
    CHECK(f[0] == G(kB));
    CHECK(f[1] == G(kA));
    f = book.Followed(fr::kInvestigatorWatchSeconds + 1.0);
    REQUIRE(f.size() == 1);
    CHECK(f[0] == G(kB));
    CHECK(book.TypeOf(G(kB)) == 7);
    CHECK_FALSE(book.TypeOf(fr::Guid {}));
    // A join after the watch ran out is a new follow.
    CHECK(book.Join(G(kA), 2, fr::kInvestigatorWatchSeconds + 1.0));
}

TEST_CASE("behavior track: changes only") {
    fr::BehaviorTrack track;
    auto c = track.Observe(1, 57);
    CHECK(c.changed);
    CHECK_FALSE(c.before);
    c = track.Observe(1, 57);
    CHECK_FALSE(c.changed);
    c = track.Observe(1, 30);
    CHECK(c.changed);
    REQUIRE(c.before);
    CHECK(*c.before == 57);
    CHECK(track.Last(1) == 30);
    track.Forget(1);
    CHECK_FALSE(track.Last(1));
}

TEST_CASE("investigations: distance") {
    CHECK(fr::Distance({0, 0, 0}, {3, 4, 0}) == doctest::Approx(5.0));
    CHECK(fr::Distance({1, 1, 1}, {1, 1, 1}) == doctest::Approx(0.0));
}

TEST_CASE("pins to fire: EnablePerception when the helper has it") {
    const std::vector<std::string> helper {"DisablePerception", "EnablePerception", "SetPosition"};
    CHECK(fr::PinsToFire(helper) == std::vector<std::string> {"EnablePerception"});
}

TEST_CASE("pins to fire: else the starting pins, never a stopping one") {
    const std::vector<std::string> pins {"Activate", "Deactivate", "Start", "Stop",     "Enable", "Disable", "OnTrue",
                                         "SetTrue",  "SetFalse",   "Reset", "Activate", "Show",   "Hide",    "Offset"};
    CHECK(fr::PinsToFire(pins) ==
          std::vector<std::string> {"Activate", "Start", "Enable", "OnTrue", "SetTrue", "Show"});
    CHECK(fr::PinsToFire(std::vector<std::string> {}).empty());
}

TEST_CASE("investigations: pin names hold the helper's known pins") {
    bool enable = false;
    bool investigated = false;

    for (const std::string_view name : fr::InvestigationPinNames()) {
        enable = enable || name == fr::kHelperEnablePin;
        investigated = investigated || name == "OnInvestigated";
    }

    CHECK(enable);
    CHECK(investigated);
    CHECK_FALSE(fr::AttractorPinGuesses().empty());
}

// --- backup_guard ---------------------------------------------------------------------------------------------

namespace {

constexpr int32_t kAct = 57;
constexpr int32_t kPlayReaction = 15;
constexpr int32_t kSpeakWait = 23;
constexpr int32_t kSpeakWaitFallback = 24;
constexpr int32_t kWaitForDialog = 138;
constexpr int32_t kPlayConversation = 33;
constexpr int32_t kPlayAnimation = 34;
constexpr int32_t kMoveToLocation = 35;
constexpr int32_t kMoveToTargetActual = 37;
constexpr int32_t kWaitForStanding = 8;
constexpr int32_t kPickUpItem = 42;
constexpr int32_t kWait = 7;

const fr::BackupDelays kDefaultDelays {fr::kDefaultBackupDelayMin, fr::kDefaultBackupDelayMax};
constexpr std::string_view kScene = "assembly:/_pro/scenes/missions/bangkok/_scene_tiger.entity";

// Game data, Hans Graub: joined OEIT_HearItemToss at 144.7 s, SpeakWait at 147.0,
// MoveToLocation at 155.0, pacified at 164.3 s.
fr::BackupBook HansUpToTheWalk() {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 144.7));
    CHECK(book.Behavior(G(kA), kPlayReaction, 144.9) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kA), kSpeakWait, 147.0) == fr::BackupStep::CallStarted);
    CHECK(book.Behavior(G(kA), kMoveToLocation, 155.0) == fr::BackupStep::CallDone);
    return book;
}

} // namespace

TEST_CASE("backup: only noise distractions arm a guard") {
    CHECK(fr::IsBackupNoise(fr::kInvestigationHearItemToss));
    CHECK(fr::IsBackupNoise(fr::kInvestigationHearRadio));
    CHECK(fr::kInvestigationHearItemToss == 7);
    CHECK(fr::kInvestigationHearRadio == 6);

    for (const int type : {0, 1, 2, 3, 8, 10, 11, 12, 13, 14, 15, 16, 17, -1}) {
        CAPTURE(type);
        CHECK_FALSE(fr::IsBackupNoise(type));
    }

    fr::BackupBook book;
    CHECK_FALSE(book.Join(G(kA), fr::kInvestigationFootsteps, 1.0)); // footsteps
    CHECK_FALSE(book.Join(G(kA), 14, 1.0));                          // OEIT_Perceptible (a sink)
    CHECK_FALSE(book.Join(G(kA), 0, 1.0));                           // OEIT_Generic
    CHECK_FALSE(book.Join(G(kA), 12, 1.0));                          // OEIT_SeeWeapon
    CHECK(book.Armed(G(kA)) == nullptr);
    CHECK(book.Join(G(kA), fr::kInvestigationHearItemToss, 1.0));
    REQUIRE(book.Armed(G(kA)) != nullptr);
    // A second noise join keeps the first case.
    CHECK_FALSE(book.Join(G(kA), fr::kInvestigationHearRadio, 5.0));
    CHECK(book.Armed(G(kA))->joinedAt == doctest::Approx(1.0));
    CHECK(book.Armed(G(kA))->type == fr::kInvestigationHearItemToss);
}

TEST_CASE("backup: behavior kinds") {
    CHECK(fr::IsRoutineBehavior(kAct));
    CHECK(fr::IsRoutineBehavior(52)); // BT_AmbientWalk
    CHECK(fr::IsRoutineBehavior(58)); // BT_Patrol
    CHECK_FALSE(fr::IsRoutineBehavior(kMoveToLocation));
    CHECK_FALSE(fr::IsRoutineBehavior(kPlayReaction));
    CHECK(fr::IsCallBehavior(kSpeakWait));
    CHECK(fr::IsCallBehavior(69)); // BT_RadioCall
    CHECK(fr::IsCallBehavior(kWaitForDialog));
    CHECK_FALSE(fr::IsCallBehavior(kSpeakWaitFallback)); // said at the spot, not the call
    CHECK_FALSE(fr::IsCallBehavior(kPlayConversation));
    CHECK(fr::IsCallStartBehavior(kSpeakWait));
    CHECK(fr::IsCallStartBehavior(69));
    CHECK_FALSE(fr::IsCallStartBehavior(kWaitForDialog)); // only between two lines
}

TEST_CASE("backup: a thrown item checked to the end disarms (Walt Gervers)") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 245.7));
    // The join is read while he is still in BT_Act: not a finish.
    CHECK(book.Behavior(G(kA), kAct, 245.8) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kA), kPlayReaction, 245.9) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kA), kSpeakWait, 248.8) == fr::BackupStep::CallStarted);
    CHECK(book.Armed(G(kA))->callStartAt == doctest::Approx(248.8));
    CHECK(book.Behavior(G(kA), kMoveToLocation, 255.6) == fr::BackupStep::CallDone);
    CHECK(book.Armed(G(kA))->callDoneAt == doctest::Approx(255.6));
    CHECK(book.Armed(G(kA))->moved);
    CHECK(book.Behavior(G(kA), kSpeakWaitFallback, 260.6) == fr::BackupStep::Arrived);
    book.SetSpot(G(kA), {119.1f, -80.7f, 54.9f});
    CHECK(book.Behavior(G(kA), kPlayAnimation, 260.9) == fr::BackupStep::None);
    // Out of the search animation: done (the reaction before BT_Act is not waited for).
    CHECK(book.Behavior(G(kA), kPlayReaction, 267.7) == fr::BackupStep::Finished);
    CHECK(book.Armed(G(kA)) == nullptr);
    CHECK(book.Behavior(G(kA), kAct, 269.3) == fr::BackupStep::None);

    // Taken down afterwards: no backup.
    const fr::BackupTakedown t = book.Takedown(G(kA), 280.0, 1, kScene, kDefaultDelays, std::nullopt);
    CHECK(t.verdict == fr::BackupVerdict::NotArmed);
    CHECK_FALSE(t.plan);
    CHECK(book.Pending() == 0);
}

TEST_CASE("backup: a coin picked up disarms (Kriang Sak Taksin)") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kB), fr::kInvestigationHearItemToss, 292.4));
    CHECK(book.Behavior(G(kB), kPlayReaction, 292.6) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kB), kSpeakWait, 295.0) == fr::BackupStep::CallStarted);
    CHECK(book.Behavior(G(kB), kMoveToLocation, 301.2) == fr::BackupStep::CallDone);
    CHECK(book.Behavior(G(kB), kMoveToTargetActual, 302.5) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kB), kWaitForStanding, 303.0) == fr::BackupStep::Arrived);
    CHECK(book.Behavior(G(kB), kPickUpItem, 304.0) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kB), kWait, 305.0) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kB), kPlayReaction, 306.0) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kB), kAct, 307.0) == fr::BackupStep::Finished);
    CHECK(book.Armed(G(kB)) == nullptr);
}

TEST_CASE("backup: the pause between two speak lines is still the call") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearRadio, 10.0));
    CHECK(book.Behavior(G(kA), kSpeakWait, 11.0) == fr::BackupStep::CallStarted);
    CHECK(book.Behavior(G(kA), kWaitForDialog, 12.0) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kA), kSpeakWait, 13.0) == fr::BackupStep::None);
    CHECK_FALSE(book.Armed(G(kA))->callDoneAt);
    CHECK(book.Behavior(G(kA), kMoveToLocation, 14.0) == fr::BackupStep::CallDone);
    CHECK(book.Armed(G(kA))->callStartAt == doctest::Approx(11.0));
    CHECK(book.Armed(G(kA))->callDoneAt == doctest::Approx(14.0));
}

// Game data, Nathan 'Napalm' Palmer: joined OEIT_HearItemToss at 744.7 s, PlayConversation
// 747.0-750.4 s (no SpeakWait), then off to the spot.
TEST_CASE("backup: off with no call: no call is assumed, and a takedown brings no backup (Nathan Palmer)") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 744.7));
    CHECK(book.Behavior(G(kA), kPlayReaction, 744.95) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kA), kPlayConversation, 747.0) == fr::BackupStep::None); // not a call
    CHECK(book.Behavior(G(kA), kMoveToLocation, 750.4) == fr::BackupStep::Moving);
    REQUIRE(book.Armed(G(kA)) != nullptr);
    CHECK_FALSE(book.Armed(G(kA))->callStartAt);
    CHECK_FALSE(book.Armed(G(kA))->callDoneAt);
    CHECK(book.Armed(G(kA))->moved);
    CHECK(book.Behavior(G(kA), kMoveToTargetActual, 752.0) == fr::BackupStep::None);

    const fr::BackupTakedown t = book.Takedown(G(kA), 754.0, 7, kScene, kDefaultDelays, std::nullopt);
    CHECK(t.verdict == fr::BackupVerdict::NoCall);
    CHECK(t.armed);
    CHECK_FALSE(t.plan);
    CHECK(book.Pending() == 0);
    CHECK(book.Scheduled() == 0);
}

// Game data, Kriang Sak Taksin: his search animation at the spot ended at 670.3 s, then
// WaitForDialog and PlayReaction ("nothing here"); he was taken down at 672.5 s, after he had finished.
TEST_CASE("backup: out of the search animation at the spot he is done (Kriang Sak Taksin)") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kB), fr::kInvestigationHearItemToss, 646.5));
    CHECK(book.Behavior(G(kB), kPlayReaction, 646.8) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kB), kSpeakWait, 650.2) == fr::BackupStep::CallStarted);
    CHECK(book.Behavior(G(kB), kMoveToLocation, 657.4) == fr::BackupStep::CallDone);
    CHECK(book.Behavior(G(kB), kPlayAnimation, 663.9) == fr::BackupStep::Arrived);
    CHECK(book.Behavior(G(kB), kWaitForDialog, 670.3) == fr::BackupStep::Finished);
    CHECK(book.Armed(G(kB)) == nullptr);
    CHECK(book.Behavior(G(kB), kPlayReaction, 670.5) == fr::BackupStep::None);

    const fr::BackupTakedown t = book.Takedown(G(kB), 672.5, 7, kScene, kDefaultDelays, std::nullopt);
    CHECK(t.verdict == fr::BackupVerdict::NotArmed);
    CHECK(book.Pending() == 0);
}

// Game data, Sean Collins: reached the spot, the search animation, then 47's grab
// (BT_CloseCombat) and the pacify 1 s later: the grab does not finish him.
TEST_CASE("backup: 47's grab out of the search animation does not disarm him (Sean Collins)") {
    constexpr int32_t kCloseCombat = 83;
    fr::BackupBook book;
    REQUIRE(book.Join(G(kB), fr::kInvestigationHearItemToss, 183.1));
    CHECK(book.Behavior(G(kB), kSpeakWait, 186.5) == fr::BackupStep::CallStarted);
    CHECK(book.Behavior(G(kB), kMoveToLocation, 193.0) == fr::BackupStep::CallDone);
    CHECK(book.Behavior(G(kB), kSpeakWaitFallback, 197.2) == fr::BackupStep::Arrived);
    CHECK(book.Behavior(G(kB), kPlayAnimation, 197.4) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kB), kCloseCombat, 197.9) == fr::BackupStep::None);
    CHECK(book.Armed(G(kB)) != nullptr);

    const fr::BackupTakedown t = book.Takedown(G(kB), 198.9, 7, kScene, kDefaultDelays, std::nullopt);
    CHECK(t.verdict == fr::BackupVerdict::Scheduled);
}

TEST_CASE("backup: taken down during the search animation still brings a backup") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kB), fr::kInvestigationHearItemToss, 646.5));
    book.Behavior(G(kB), kSpeakWait, 650.2);
    CHECK(book.Behavior(G(kB), kMoveToLocation, 657.4) == fr::BackupStep::CallDone);
    CHECK(book.Behavior(G(kB), kPlayAnimation, 663.9) == fr::BackupStep::Arrived);
    book.SetSpot(G(kB), {72.3f, -62.6f, 60.1f});
    const fr::BackupTakedown t = book.Takedown(G(kB), 668.0, 7, kScene, kDefaultDelays, std::nullopt);
    REQUIRE(t.verdict == fr::BackupVerdict::Scheduled);
    CHECK(t.plan->spotSource == fr::BackupSpotSource::Arrival);
}

TEST_CASE("backup: the search animation before reaching the spot does not finish him") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 10.0));
    CHECK(book.Behavior(G(kA), kPlayAnimation, 10.5) == fr::BackupStep::None); // not moved yet: no arrival
    CHECK(book.Behavior(G(kA), kSpeakWait, 12.0) == fr::BackupStep::CallStarted);
    CHECK(book.Armed(G(kA)) != nullptr);
}

// Game data, Hans Graub: a 0.8 s SpeakWait (no radio animation), then off; pacified on his way.
TEST_CASE("backup: a short line is a bark, not a radio call (Hans Graub)") {
    CHECK(fr::kBackupCallMinSeconds == doctest::Approx(2.0));
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 104.4));
    CHECK(book.Behavior(G(kA), kPlayReaction, 104.65) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kA), kSpeakWait, 106.47) == fr::BackupStep::CallStarted);
    CHECK(book.Behavior(G(kA), kMoveToLocation, 107.24) == fr::BackupStep::Bark);
    REQUIRE(book.Armed(G(kA)) != nullptr);
    CHECK_FALSE(book.Armed(G(kA))->callStartAt);
    CHECK_FALSE(book.Armed(G(kA))->callDoneAt);
    CHECK(book.Armed(G(kA))->barks == 1);
    CHECK(book.Armed(G(kA))->lastBarkSeconds == doctest::Approx(0.77));
    CHECK(book.Armed(G(kA))->moved);

    const fr::BackupTakedown t = book.Takedown(G(kA), 115.4, 7, kScene, kDefaultDelays, std::nullopt);
    CHECK(t.verdict == fr::BackupVerdict::NoCall);
    CHECK_FALSE(t.plan);
    CHECK(book.Pending() == 0);
}

TEST_CASE("backup: the call threshold is 2 s, and a call after a bark still counts") {
    // Exactly kBackupCallMinSeconds counts; just under does not.
    {
        fr::BackupBook book;
        REQUIRE(book.Join(G(kA), fr::kInvestigationHearRadio, 10.0));
        book.Behavior(G(kA), kSpeakWait, 11.0);
        CHECK(book.Behavior(G(kA), kMoveToLocation, 13.0) == fr::BackupStep::CallDone);
    }
    {
        fr::BackupBook book;
        REQUIRE(book.Join(G(kA), fr::kInvestigationHearRadio, 10.0));
        book.Behavior(G(kA), kSpeakWait, 11.0);
        CHECK(book.Behavior(G(kA), kMoveToLocation, 12.9) == fr::BackupStep::Bark);
    }

    // A bark, then a real call: the call counts from its own start.
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 10.0));
    CHECK(book.Behavior(G(kA), kSpeakWait, 11.0) == fr::BackupStep::CallStarted);
    CHECK(book.Behavior(G(kA), kPlayReaction, 11.5) == fr::BackupStep::Bark);
    CHECK(book.Behavior(G(kA), kSpeakWait, 12.0) == fr::BackupStep::CallStarted);
    CHECK(book.Behavior(G(kA), kMoveToLocation, 18.0) == fr::BackupStep::CallDone);
    CHECK(book.Armed(G(kA))->callStartAt == doctest::Approx(12.0));
    CHECK(book.Armed(G(kA))->callDoneAt == doctest::Approx(18.0));
    CHECK(book.Takedown(G(kA), 20.0, 7, kScene, kDefaultDelays, std::nullopt).verdict == fr::BackupVerdict::Scheduled);
}

TEST_CASE("backup: a BT_WaitForDialog on its own starts no call") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 10.0));
    CHECK(book.Behavior(G(kA), kWaitForDialog, 10.5) == fr::BackupStep::None);
    CHECK_FALSE(book.Armed(G(kA))->callStartAt);
    CHECK(book.Takedown(G(kA), 14.0, 7, kScene, kDefaultDelays, std::nullopt).verdict == fr::BackupVerdict::NoCall);
}

TEST_CASE("backup: taken down under 2 s into a line: no radio call, no backup") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 10.0));
    CHECK(book.Behavior(G(kA), kSpeakWait, 11.0) == fr::BackupStep::CallStarted);
    const fr::BackupTakedown t = book.Takedown(G(kA), 12.5, 7, kScene, kDefaultDelays, std::nullopt);
    CHECK(t.verdict == fr::BackupVerdict::NoCall);
    CHECK_FALSE(t.plan);
}

TEST_CASE("backup: back to routine before the call disarms") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 10.0));
    CHECK(book.Behavior(G(kA), kPlayReaction, 10.5) == fr::BackupStep::None);
    CHECK(book.Behavior(G(kA), kAct, 12.0) == fr::BackupStep::Finished);
    CHECK(book.Armed(G(kA)) == nullptr);
}

TEST_CASE("backup: taken down on his way over: a backup 20-45 s after the call (Hans Graub)") {
    fr::BackupBook book = HansUpToTheWalk();
    const fr::BackupTakedown t =
        book.Takedown(G(kA), 164.3, 12345, kScene, kDefaultDelays, std::array<float, 3> {115.0f, -81.6f, 44.7f});
    REQUIRE(t.verdict == fr::BackupVerdict::Scheduled);
    REQUIRE(t.plan);
    const fr::BackupPlan& p = *t.plan;
    CHECK(p.number == 1);
    CHECK(p.actor == G(kA));
    CHECK(p.type == fr::kInvestigationHearItemToss);
    CHECK(p.takedownAt == doctest::Approx(164.3));
    CHECK(p.callDoneAt == doctest::Approx(155.0));
    CHECK_FALSE(p.cutOff);
    CHECK(p.roll.seconds >= 20.0);
    CHECK(p.roll.seconds <= 45.0);
    CHECK(p.dueAt == doctest::Approx(std::max(164.3, 155.0 + p.roll.seconds)));
    CHECK(p.immediate == (155.0 + p.roll.seconds <= 164.3));
    // He never reached the noise: the backup goes where he went down.
    REQUIRE(p.spot);
    CHECK((*p.spot)[0] == doctest::Approx(115.0));
    CHECK(p.spotSource == fr::BackupSpotSource::Takedown);
    CHECK(book.Armed(G(kA)) == nullptr);
    CHECK(book.Pending() == 1);
    CHECK(book.Scheduled() == 1);

    // Not before it is due; then once.
    CHECK(book.Due(p.dueAt - 0.01).empty());
    const std::vector<fr::BackupPlan> due = book.Due(p.dueAt);
    REQUIRE(due.size() == 1);
    CHECK(due[0].number == 1);
    CHECK(book.Due(p.dueAt + 100.0).empty());
    CHECK(book.Pending() == 0);
}

TEST_CASE("backup: the delay counts from the end of the call, not the takedown") {
    fr::BackupBook book = HansUpToTheWalk();
    const fr::BackupTakedown t = book.Takedown(G(kA), 155.5, 7, kScene, {20, 20}, std::nullopt);
    REQUIRE(t.plan);
    CHECK(t.plan->roll.seconds == doctest::Approx(20.0));
    CHECK(t.plan->dueAt == doctest::Approx(175.0));
    CHECK_FALSE(t.plan->immediate);
}

TEST_CASE("backup: a takedown after the delay ran out dispatches at once") {
    fr::BackupBook book = HansUpToTheWalk();
    const fr::BackupTakedown t = book.Takedown(G(kA), 200.0, 7, kScene, {10, 30}, std::nullopt);
    REQUIRE(t.plan);
    CHECK(t.plan->immediate);
    CHECK(t.plan->dueAt == doctest::Approx(200.0));
    CHECK(book.Due(200.0).size() == 1);
}

TEST_CASE("backup: taken down at the spot: the backup goes where he reached the noise") {
    fr::BackupBook book = HansUpToTheWalk();
    CHECK(book.Behavior(G(kA), kSpeakWaitFallback, 160.0) == fr::BackupStep::Arrived);
    book.SetSpot(G(kA), {119.1f, -80.7f, 54.9f});
    CHECK(book.Behavior(G(kA), kPlayAnimation, 160.3) == fr::BackupStep::None);
    const fr::BackupTakedown t =
        book.Takedown(G(kA), 162.0, 7, kScene, kDefaultDelays, std::array<float, 3> {0.0f, 0.0f, 0.0f});
    REQUIRE(t.plan);
    REQUIRE(t.plan->spot);
    CHECK((*t.plan->spot)[0] == doctest::Approx(119.1));
    CHECK(t.plan->spotSource == fr::BackupSpotSource::Arrival);
}

TEST_CASE("backup: taken down mid-call: the call counts as ended at the takedown") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 10.0));
    CHECK(book.Behavior(G(kA), kSpeakWait, 11.0) == fr::BackupStep::CallStarted);
    const fr::BackupTakedown t = book.Takedown(G(kA), 13.5, 7, kScene, {15, 15}, std::nullopt);
    REQUIRE(t.plan);
    CHECK(t.plan->cutOff);
    CHECK(t.plan->callDoneAt == doctest::Approx(13.5));
    CHECK(t.plan->dueAt == doctest::Approx(28.5));
    CHECK_FALSE(t.plan->spot);
    CHECK(t.plan->spotSource == fr::BackupSpotSource::None);
}

TEST_CASE("backup: taken down before his call: no backup") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 10.0));
    CHECK(book.Behavior(G(kA), kPlayReaction, 10.3) == fr::BackupStep::None);
    const fr::BackupTakedown t = book.Takedown(G(kA), 11.0, 7, kScene, kDefaultDelays, std::nullopt);
    CHECK(t.verdict == fr::BackupVerdict::NoCall);
    CHECK(t.armed);
    CHECK_FALSE(t.plan);
    CHECK(book.Pending() == 0);
    CHECK(book.Armed(G(kA)) == nullptr);
}

TEST_CASE("backup: a guard who never joined a noise brings no backup") {
    fr::BackupBook book;
    CHECK(book.Takedown(G(kA), 11.0, 7, kScene, kDefaultDelays, std::nullopt).verdict == fr::BackupVerdict::NotArmed);
    // Behavior of an unarmed guard changes nothing.
    CHECK(book.Behavior(G(kA), kSpeakWait, 12.0) == fr::BackupStep::None);
}

TEST_CASE("backup: at most kBackupMaxPerScene a scene; Reset starts over") {
    fr::BackupBook book;

    for (int i = 0; i < fr::kBackupMaxPerScene + 1; ++i) {
        const double t0 = i * 100.0;
        REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, t0));
        book.Behavior(G(kA), kSpeakWait, t0 + 1.0);
        const fr::BackupTakedown t = book.Takedown(G(kA), t0 + 4.0, 7, kScene, kDefaultDelays, std::nullopt);
        CAPTURE(i);
        CHECK(t.verdict == (i < fr::kBackupMaxPerScene ? fr::BackupVerdict::Scheduled : fr::BackupVerdict::Spent));
    }

    CHECK(book.Scheduled() == static_cast<uint32_t>(fr::kBackupMaxPerScene));
    book.Reset();
    CHECK(book.Scheduled() == 0);
    CHECK(book.Pending() == 0);
}

TEST_CASE("backup: Drop forgets an armed guard") {
    fr::BackupBook book;
    REQUIRE(book.Join(G(kA), fr::kInvestigationHearItemToss, 10.0));
    CHECK(book.Drop(G(kA)));
    CHECK_FALSE(book.Drop(G(kA)));
    CHECK(book.Takedown(G(kA), 11.0, 7, kScene, kDefaultDelays, std::nullopt).verdict == fr::BackupVerdict::NotArmed);
}

TEST_CASE("backup: plans come due oldest first") {
    fr::BackupBook book;

    for (const char* repo : {kA, kB}) {
        REQUIRE(book.Join(G(repo), fr::kInvestigationHearItemToss, 0.0));
        book.Behavior(G(repo), kSpeakWait, 1.0);
        book.Behavior(G(repo), kMoveToLocation, repo == kA ? 9.0 : 3.0);
    }

    REQUIRE(book.Takedown(G(kA), 10.0, 7, kScene, {10, 10}, std::nullopt).plan); // due 19
    REQUIRE(book.Takedown(G(kB), 11.0, 7, kScene, {10, 10}, std::nullopt).plan); // due 13
    const std::vector<fr::BackupPlan> due = book.Due(30.0);
    REQUIRE(due.size() == 2);
    CHECK(due[0].actor == G(kB));
    CHECK(due[1].actor == G(kA));
}

TEST_CASE("backup: delay roll seeded per backup, within the settings") {
    const fr::BackupDelayRoll a = fr::RollBackupDelay(12345, kScene, 0, kDefaultDelays);
    const fr::BackupDelayRoll b = fr::RollBackupDelay(12345, kScene, 0, kDefaultDelays);
    CHECK(a.u == b.u);
    CHECK(a.u == fr::UniformFromHash(fr::DialHash(12345, kScene, fr::kBackupGuardFeature, int32_t {0})));
    CHECK(a.seconds == doctest::Approx(20.0 + a.u * 25.0));
    CHECK(fr::RollBackupDelay(12345, kScene, 1, kDefaultDelays).u != a.u);
    CHECK(fr::RollBackupDelay(12346, kScene, 0, kDefaultDelays).u != a.u);

    double lo = 1e9;
    double hi = -1e9;

    for (uint32_t n = 0; n < 2000; ++n) {
        const double s = fr::RollBackupDelay(777, "scene", n, kDefaultDelays).seconds;
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }

    CHECK(lo >= 20.0);
    CHECK(hi <= 45.0);
    CHECK(lo < 21.0);
    CHECK(hi > 44.0);
    CHECK(fr::RollBackupDelay(1, "scene", 0, {5, 5}).seconds == doctest::Approx(5.0));
}

TEST_CASE("backup: delay settings are clamped") {
    CHECK(fr::ClampBackupDelays(10, 30).min == 10);
    CHECK(fr::ClampBackupDelays(10, 30).max == 30);
    CHECK(fr::ClampBackupDelays(-5, 30).min == 0);
    CHECK(fr::ClampBackupDelays(10, 500).max == fr::kBackupDelayLimit);
    CHECK(fr::ClampBackupDelays(40, 20).min == 40);
    CHECK(fr::ClampBackupDelays(40, 20).max == 40);
    // A roll with max below min never goes below min.
    CHECK(fr::RollBackupDelay(3, "scene", 2, {40, 20}).seconds == doctest::Approx(40.0));
}

TEST_CASE("backup: the reach is 30 m, widened to the nearest guard on the spot's floor") {
    using V = std::vector<float>;
    CHECK(fr::kBackupRange == doctest::Approx(30.0));

    // Game data, a backup: a guard at 11.2 m, the next at 18.6 and 19.2 m; 30 m reaches all three.
    fr::BackupReach r = fr::PlanBackupReach(V {11.2f, 18.6f, 19.2f, 40.0f}, V {10.5f}, V {18.6f, 19.2f});
    CHECK(r.go);
    CHECK_FALSE(r.widened);
    CHECK(r.radius == doctest::Approx(30.0));
    CHECK(r.guardsInReach == 3);
    REQUIRE(r.nearestSameFloor);
    CHECK(*r.nearestSameFloor == doctest::Approx(18.6));

    // Guards near but on other floors: out to the nearest one on the spot's floor + 5 m (others in range hear it too).
    r = fr::PlanBackupReach(V {8.0f, 12.0f, 41.0f}, V {}, V {41.0f});
    CHECK(r.go);
    CHECK(r.widened);
    CHECK(r.radius == doctest::Approx(46.0));
    CHECK(r.guardsInReach == 3);

    // Nobody on the floor: 30 m, or out to the nearest guard + 5 m when he is farther.
    r = fr::PlanBackupReach(V {8.0f, 50.0f}, V {});
    CHECK_FALSE(r.widened);
    CHECK(r.radius == doctest::Approx(30.0));
    r = fr::PlanBackupReach(V {46.3f, 47.7f, 48.8f}, V {18.2f, 25.8f});
    CHECK(r.go);
    CHECK(r.widened);
    CHECK(r.radius == doctest::Approx(51.3));
    CHECK(r.guardsInReach == 3);

    // Capped: a guard on the floor past the cap leaves the range at it; nobody inside it, no backup.
    r = fr::PlanBackupReach(V {10.0f, 120.0f}, V {}, V {120.0f});
    CHECK(r.go);
    CHECK(r.radius == doctest::Approx(fr::kBackupRadiusMax));
    r = fr::PlanBackupReach(V {90.0f}, V {});
    CHECK_FALSE(r.go);
    CHECK(r.wanted == doctest::Approx(fr::kBackupRadiusMax));
}

TEST_CASE("backup: civilians do not cut the reach; no guard read, no backup") {
    using V = std::vector<float>;

    fr::BackupReach r = fr::PlanBackupReach(V {5.0f, 10.0f, 20.0f}, V {14.0f, 60.0f}, V {5.0f});
    CHECK(r.go);
    CHECK(r.radius == doctest::Approx(30.0));
    CHECK(r.guardsInReach == 3);
    REQUIRE(r.nearestGuard);
    CHECK(*r.nearestGuard == doctest::Approx(5.0));
    REQUIRE(r.nearestCivilian);
    CHECK(*r.nearestCivilian == doctest::Approx(14.0));

    r = fr::PlanBackupReach(V {}, V {5.0f});
    CHECK_FALSE(r.go);
    CHECK_FALSE(r.nearestGuard);
    r = fr::PlanBackupReach(V {std::nanf("")}, V {50.0f}, V {std::nanf("")});
    CHECK_FALSE(r.go);

    // Unread distances are skipped everywhere.
    r = fr::PlanBackupReach(V {std::nanf(""), 4.0f}, V {std::nanf(""), 30.0f}, V {std::nanf("")});
    CHECK(r.go);
    CHECK_FALSE(r.nearestSameFloor);
    CHECK(r.radius == doctest::Approx(30.0));
    REQUIRE(r.nearestCivilian);
    CHECK(*r.nearestCivilian == doctest::Approx(30.0));
}

TEST_CASE("backup: the first to answer decides the notice, once per backup") {
    using N = fr::BackupNotice;
    CHECK(fr::BackupNoticeText(N::Guard) == "Backup is on the way");
    CHECK(fr::BackupNoticeText(N::Civilian) == "Someone is coming to investigate");
    CHECK(fr::BackupNoticeText(N::None).empty());

    // A guard first: "Backup is on the way"; nothing more after it.
    fr::BackupAnswer a;
    CHECK(a.Join(true) == N::Guard);
    CHECK(a.shown == N::Guard);
    CHECK(a.Join(true) == N::None);
    CHECK(a.Join(false) == N::None);
    CHECK(a.guards == 2);
    CHECK(a.others == 1);

    // A civilian first (game data, Gregory M. Yeager): "Someone is coming to investigate";
    // a guard after him shows nothing more.
    fr::BackupAnswer b;
    CHECK(b.Join(false) == N::Civilian);
    CHECK(b.shown == N::Civilian);
    CHECK(b.Join(true) == N::None);
    CHECK(b.shown == N::Civilian);
    CHECK(b.guards == 1);
    CHECK(b.others == 1);

    // Nobody answers: no notice.
    const fr::BackupAnswer c;
    CHECK(c.shown == N::None);
    CHECK(c.guards == 0);
    CHECK(c.others == 0);
}

TEST_CASE("backup_guard: settings rows and defaults") {
    const fr::Config c {};
    const fr::SettingInfo* on = fr::FindSetting("backup_guard");
    REQUIRE(on != nullptr);
    CHECK(on->type == fr::SettingType::Bool);
    CHECK(on->def == "true");
    CHECK(on->label == "Backup checks on a guard who never reported back");
    CHECK(on->group == fr::SettingGroup::Backup);
    CHECK(fr::SettingSectionOf(on->group) == fr::SettingSection::Guards);
    CHECK(c.backupGuard);

    const fr::SettingInfo* lo = fr::FindSetting("backup_guard_delay_min");
    const fr::SettingInfo* hi = fr::FindSetting("backup_guard_delay_max");
    const fr::SettingInfo* notice = fr::FindSetting("backup_guard_notice");
    REQUIRE(lo != nullptr);
    REQUIRE(hi != nullptr);
    REQUIRE(notice != nullptr);
    CHECK(lo->def == "20");
    CHECK(hi->def == "45");
    CHECK(lo->def == std::to_string(fr::kDefaultBackupDelayMin));
    CHECK(hi->def == std::to_string(fr::kDefaultBackupDelayMax));
    CHECK(lo->unit == "s");
    CHECK(lo->max == static_cast<double>(fr::kBackupDelayLimit));
    CHECK(lo->parent == "backup_guard");
    CHECK(hi->parent == "backup_guard");
    CHECK(notice->parent == "backup_guard");
    CHECK(notice->def == "false");
    CHECK(c.backupGuardDelayMin == fr::kDefaultBackupDelayMin);
    CHECK(c.backupGuardDelayMax == fr::kDefaultBackupDelayMax);
    CHECK_FALSE(c.backupGuardNotice);
    CHECK(fr::kBackupNoticeText == "Backup is on the way");
}

TEST_CASE("backup_guard: the helper's sound is a struggle, answered by sound joins only") {
    CHECK(fr::kBackupSoundEvent == 43); // AIET_SoundCloseCombat
    CHECK(fr::SoundEventName(fr::kBackupSoundEvent) == "AIET_SoundCloseCombat");
    CHECK(fr::AnswersBackupNoise(5));        // OEIT_HearPain
    CHECK(fr::AnswersBackupNoise(9));        // OEIT_HearAccident
    CHECK(fr::AnswersBackupNoise(10));       // OEIT_CuriousSound
    CHECK(fr::AnswersBackupNoise(14));       // OEIT_Perceptible
    CHECK_FALSE(fr::AnswersBackupNoise(0));  // OEIT_Generic: a body or 47
    CHECK_FALSE(fr::AnswersBackupNoise(4));  // OEIT_HearBark
    CHECK_FALSE(fr::AnswersBackupNoise(7));  // OEIT_HearItemToss: 47's own coin near the spot
    CHECK_FALSE(fr::AnswersBackupNoise(12)); // OEIT_SeeWeapon
    CHECK_FALSE(fr::AnswersBackupNoise(-1));
}

TEST_CASE("footstep case: the curious helper's sound and who answers it") {
    CHECK(fr::kFootstepCuriousEvent == 55); // AIET_SoundCurious
    CHECK(fr::kFootstepNoiseEvent == 26);   // AIET_SoundThrownItemCollision
    CHECK(fr::SoundEventName(fr::kFootstepCuriousEvent) == "AIET_SoundCurious");
    CHECK(fr::SoundEventName(fr::kFootstepNoiseEvent) == "AIET_SoundThrownItemCollision");
    CHECK(fr::SoundEventName(54) == "AIET_SoundAccident");
    CHECK(fr::SoundEventName(7) == "AIET_#7");
    CHECK(fr::AnswersCuriousNoise(10)); // OEIT_CuriousSound: what a guard still reacting joins
    CHECK(fr::AnswersCuriousNoise(9));  // OEIT_HearAccident
    CHECK(fr::AnswersCuriousNoise(11)); // OEIT_CuriousItemSound
    CHECK(fr::AnswersCuriousNoise(14)); // OEIT_Perceptible
    CHECK_FALSE(fr::AnswersCuriousNoise(fr::kInvestigationFootsteps));
    CHECK_FALSE(fr::AnswersCuriousNoise(0)); // OEIT_Generic
    CHECK_FALSE(fr::AnswersCuriousNoise(7)); // OEIT_HearItemToss
    CHECK_FALSE(fr::AnswersCuriousNoise(-1));
}

TEST_CASE("footstep helper: its sound reaches the hearer") {
    CHECK(fr::FootstepReach(10.0f) == doctest::Approx(13.0));
    CHECK(fr::FootstepReach(1.0f) == doctest::Approx(fr::kFootstepReachMin));
    CHECK(fr::FootstepReach(50.0f) == doctest::Approx(fr::kFootstepReachMax));
    CHECK(fr::FootstepReach(std::nullopt) == doctest::Approx(fr::kFootstepReachMin));
    CHECK(fr::FootstepReach(std::nanf("")) == doctest::Approx(fr::kFootstepReachMin));
}

// Behavior ids (ECompiledBehaviorType), as the footstep follow lines name them.
namespace {
constexpr int32_t kBtWait = 7;
constexpr int32_t kBtPlayReaction = 15;
constexpr int32_t kBtSpeakWait = 23;
constexpr int32_t kBtPlayAnimation = 34;
constexpr int32_t kBtMoveToLocation = 35;
constexpr int32_t kBtSearch = 45;
constexpr int32_t kBtAct = 57;
constexpr int32_t kBtAlertedStand = 60;
constexpr int32_t kBtWaitForDialog = 138;
} // namespace

TEST_CASE("footstep case: the reaction's behaviors") {
    for (const int32_t t : {kBtWait, kBtPlayReaction, 16, kBtSpeakWait, 24, 28, 29, 30, kBtPlayAnimation,
                            kBtAlertedStand, kBtWaitForDialog})
        CHECK(fr::IsFootstepReactionBehavior(t));

    CHECK_FALSE(fr::IsFootstepReactionBehavior(kBtAct));
    CHECK_FALSE(fr::IsFootstepReactionBehavior(58)); // BT_Patrol
    CHECK_FALSE(fr::IsFootstepReactionBehavior(kBtSearch));
    CHECK_FALSE(fr::IsFootstepReactionBehavior(kBtMoveToLocation));
}

TEST_CASE("footstep case: he answers the curious sound inside the wait: done, no noise") {
    fr::FootstepCase c(100.0, true);
    CHECK(c.Phase() == fr::FootstepPhase::Curious);
    CHECK(c.Open());
    CHECK_FALSE(c.WaitingAt());

    // The join is read while he still stands in his routine; then his footstep reaction.
    CHECK(c.Behavior(kBtAct, 100.0).outcome == fr::FootstepOutcome::None);
    CHECK_FALSE(c.ReactedAt());
    CHECK(c.Behavior(kBtPlayReaction, 100.3).outcome == fr::FootstepOutcome::None);
    CHECK(c.ReactedAt());
    CHECK(*c.ReactedAt() == doctest::Approx(100.3));
    CHECK(c.Tick(101.0).outcome == fr::FootstepOutcome::None);

    // Game data: he joins OEIT_CuriousSound 2.2 s after the spawn, still in BT_PlayReaction.
    fr::FootstepStep s = c.Answer();
    CHECK(s.outcome == fr::FootstepOutcome::Answered);
    CHECK_FALSE(s.spawnNoise);
    CHECK(c.Phase() == fr::FootstepPhase::Answered);
    CHECK_FALSE(c.Open());

    // Done: his walk over, the waits' ends and a takedown change nothing.
    CHECK(c.Behavior(kBtMoveToLocation, 104.3).outcome == fr::FootstepOutcome::None);
    CHECK(c.Tick(100.0 + fr::kFootstepCuriousWait).outcome == fr::FootstepOutcome::None);
    CHECK(c.Tick(100.0 + fr::kFootstepWaitMax).outcome == fr::FootstepOutcome::None);
    CHECK(c.Cancel(fr::FootstepOutcome::TakenDown).outcome == fr::FootstepOutcome::None);
    CHECK(c.Answer().outcome == fr::FootstepOutcome::None);
    CHECK(c.Phase() == fr::FootstepPhase::Answered);
}

TEST_CASE("footstep case: no answer in the wait: waiting, then the noise when his reaction is over") {
    fr::FootstepCase c(100.0, true);
    c.Behavior(kBtPlayReaction, 100.3);
    c.Behavior(kBtAlertedStand, 102.4);
    CHECK(c.Tick(100.0 + fr::kFootstepCuriousWait - 0.01).outcome == fr::FootstepOutcome::None);
    CHECK(c.Phase() == fr::FootstepPhase::Curious);

    fr::FootstepStep s = c.Tick(100.0 + fr::kFootstepCuriousWait);
    CHECK(s.outcome == fr::FootstepOutcome::CuriousUnanswered);
    CHECK_FALSE(s.spawnNoise);
    CHECK(c.Phase() == fr::FootstepPhase::Waiting);
    CHECK(c.Open());
    CHECK(*c.WaitingAt() == doctest::Approx(100.0 + fr::kFootstepCuriousWait));

    // Game data, a footstep reaction: alerted stand, a line, the look-around animation, a last reaction.
    for (const auto& [t, at] : std::vector<std::pair<int32_t, double>> {{kBtWaitForDialog, 105.4},
                                                                        {kBtWait, 105.7},
                                                                        {kBtSpeakWait, 106.7},
                                                                        {kBtWaitForDialog, 107.5},
                                                                        {kBtPlayAnimation, 108.5},
                                                                        {kBtPlayReaction, 112.3}}) {
        const fr::FootstepStep r = c.Behavior(t, at);
        CHECK(r.outcome == fr::FootstepOutcome::None);
        CHECK(c.Tick(at).outcome == fr::FootstepOutcome::None);
    }

    s = c.Behavior(kBtAct, 113.6);
    CHECK(s.outcome == fr::FootstepOutcome::ReactionOver);
    CHECK(s.spawnNoise);
    CHECK(c.Phase() == fr::FootstepPhase::Noise);
    CHECK_FALSE(c.Open());
}

TEST_CASE("footstep case: a short reaction over inside the curious wait: the noise only after the wait") {
    fr::FootstepCase c(0.0, true);
    c.Behavior(kBtPlayReaction, 0.3);
    // Back on his routine at 2 s: the curious sound keeps its time.
    CHECK(c.Behavior(kBtAct, 2.0).outcome == fr::FootstepOutcome::None);
    CHECK(c.Phase() == fr::FootstepPhase::Curious);
    CHECK(c.Tick(fr::kFootstepCuriousWait).outcome == fr::FootstepOutcome::CuriousUnanswered);

    // The next sample after the wait.
    const fr::FootstepStep s = c.Behavior(kBtAct, fr::kFootstepCuriousWait + 0.25);
    CHECK(s.outcome == fr::FootstepOutcome::ReactionOver);
    CHECK(s.spawnNoise);
}

TEST_CASE("footstep case: a late answer while waiting ends it too") {
    fr::FootstepCase c(0.0, true);
    c.Behavior(kBtPlayReaction, 0.3);
    CHECK(c.Tick(fr::kFootstepCuriousWait).outcome == fr::FootstepOutcome::CuriousUnanswered);

    const fr::FootstepStep s = c.Answer();
    CHECK(s.outcome == fr::FootstepOutcome::Answered);
    CHECK_FALSE(s.spawnNoise);
    CHECK(c.Phase() == fr::FootstepPhase::Answered);
    CHECK(c.Behavior(kBtAct, 9.0).outcome == fr::FootstepOutcome::None);
}

TEST_CASE("footstep case: no curious helper: waiting from the hearing") {
    fr::FootstepCase c(50.0, false);
    CHECK(c.Phase() == fr::FootstepPhase::Waiting);
    CHECK(*c.WaitingAt() == doctest::Approx(50.0));
    CHECK(c.Tick(50.0 + fr::kFootstepCuriousWait).outcome == fr::FootstepOutcome::None);
    c.Behavior(kBtPlayReaction, 50.3);

    const fr::FootstepStep s = c.Behavior(kBtAct, 54.4);
    CHECK(s.outcome == fr::FootstepOutcome::ReactionOver);
    CHECK(s.spawnNoise);
}

TEST_CASE("footstep case: never seen reacting: free on his routine after the grace, not before") {
    fr::FootstepCase c(10.0, false);
    CHECK(c.Behavior(kBtAct, 10.25).outcome == fr::FootstepOutcome::None);
    CHECK(c.Behavior(kBtAct, 10.0 + fr::kFootstepReactGrace - 0.25).outcome == fr::FootstepOutcome::None);

    const fr::FootstepStep s = c.Behavior(kBtAct, 10.0 + fr::kFootstepReactGrace);
    CHECK(s.outcome == fr::FootstepOutcome::ReactionOver);
    CHECK(s.spawnNoise);
    CHECK_FALSE(c.ReactedAt());
}

TEST_CASE("footstep case: a reaction that outlasts the wait gives up (no noise)") {
    fr::FootstepCase c(0.0, true);
    c.Behavior(kBtPlayReaction, 0.3);
    CHECK(c.Tick(fr::kFootstepCuriousWait).outcome == fr::FootstepOutcome::CuriousUnanswered);
    c.Behavior(kBtPlayAnimation, 10.0);
    CHECK(c.Tick(fr::kFootstepWaitMax - 0.01).outcome == fr::FootstepOutcome::None);

    const fr::FootstepStep s = c.Tick(fr::kFootstepWaitMax);
    CHECK(s.outcome == fr::FootstepOutcome::GaveUp);
    CHECK_FALSE(s.spawnNoise);
    CHECK(c.Phase() == fr::FootstepPhase::Canceled);
    CHECK(c.Behavior(kBtAct, 33.3).outcome == fr::FootstepOutcome::None);
    CHECK(c.Answer().outcome == fr::FootstepOutcome::None);
}

TEST_CASE("footstep case: another investigation cancels, in either phase") {
    fr::FootstepCase a(0.0, true);
    a.Behavior(kBtPlayReaction, 0.3);
    fr::FootstepStep s = a.Behavior(kBtSearch, 1.0);
    CHECK(s.outcome == fr::FootstepOutcome::OtherInvestigation);
    CHECK_FALSE(s.spawnNoise);
    CHECK(a.Phase() == fr::FootstepPhase::Canceled);

    fr::FootstepCase b(0.0, false);
    b.Behavior(kBtPlayReaction, 0.3);
    s = b.Cancel(fr::FootstepOutcome::OtherInvestigation);
    CHECK(s.outcome == fr::FootstepOutcome::OtherInvestigation);
    CHECK_FALSE(s.spawnNoise);
}

TEST_CASE("footstep case: a takedown or a lost follow cancels") {
    fr::FootstepCase a(0.0, true);
    fr::FootstepStep s = a.Cancel(fr::FootstepOutcome::TakenDown);
    CHECK(s.outcome == fr::FootstepOutcome::TakenDown);
    CHECK_FALSE(s.spawnNoise);
    CHECK_FALSE(a.Open());
    CHECK(a.Tick(fr::kFootstepWaitMax).outcome == fr::FootstepOutcome::None);

    fr::FootstepCase b(0.0, true);
    CHECK(b.Tick(fr::kFootstepCuriousWait).outcome == fr::FootstepOutcome::CuriousUnanswered);
    s = b.Cancel(fr::FootstepOutcome::LostFollow);
    CHECK(s.outcome == fr::FootstepOutcome::LostFollow);
    CHECK_FALSE(s.spawnNoise);

    // A default case is closed.
    fr::FootstepCase none;
    CHECK_FALSE(none.Open());
    CHECK(none.Cancel(fr::FootstepOutcome::Abandoned).outcome == fr::FootstepOutcome::None);
    CHECK(none.Answer().outcome == fr::FootstepOutcome::None);
}

TEST_CASE("footstep case: walking off (not an investigation behavior) after reacting is free") {
    fr::FootstepCase c(0.0, false);
    c.Behavior(kBtPlayReaction, 0.3);
    const fr::FootstepStep s = c.Behavior(kBtMoveToLocation, 3.0);
    CHECK(s.outcome == fr::FootstepOutcome::ReactionOver);
    CHECK(s.spawnNoise);
}

TEST_CASE("footstep case: names") {
    CHECK(std::string(fr::FootstepPhaseName(fr::FootstepPhase::Curious)) == "curious");
    CHECK(std::string(fr::FootstepPhaseName(fr::FootstepPhase::Waiting)) == "waiting");
    CHECK(std::string(fr::FootstepPhaseName(fr::FootstepPhase::Answered)) == "answered");
    CHECK(std::string(fr::FootstepOutcomeName(fr::FootstepOutcome::Answered)) == "he answered the curious sound");
    CHECK(std::string(fr::FootstepOutcomeName(fr::FootstepOutcome::CuriousUnanswered)) ==
          "no answer to the curious sound");
    CHECK(std::string(fr::FootstepOutcomeName(fr::FootstepOutcome::GaveUp)) == "still reacting at the wait's end");
}
