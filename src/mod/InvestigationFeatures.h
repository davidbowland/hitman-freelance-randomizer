#pragma once

// Two features built on AI investigations, Freelancer missions only (the pure
// parts and the plan are in core/Investigations.h):
//
// footsteps_investigate (on by default; its lines are written while it is on): an
// Investigate_Curious join with InvestigationType 2 (a guard heard 47's
// footsteps) that passes the cooldowns is rolled once against
// footsteps_investigate_chance (fr::DecideFootstepInvestigate, drawn at the
// time, not from the seed, so a replay of the same hearings rolls anew; a miss starts that guard's cooldown, so his next
// footsteps are not rolled again within it). A hit marks the gate and opens a
// fr::FootstepCase for the hearer (he must be followed: his behavior is read
// by the follow sampler every kInvestigatorSampleEvery s), 47's position at
// the hearing kept as the spot. At once the game's investigate-curious
// setpiece helper (setpiecehelper_investigatecurious) is spawned at the spot,
// both its sound events a curious sound (fr::kFootstepCuriousEvent), its
// sound reaching him (fr::FootstepReach of his distance at the hearing), its
// sphere and perceptible range fr::kBackupPerceptibleRange, then its
// EnablePerception (or, as read from its graph at runtime, its starting input
// pins). His join of its investigation (fr::AnswersCuriousNoise, while it is
// out) answers the case: no other noise. Unanswered after
// fr::kFootstepCuriousWait s, his reaction is waited out (fr::kFootstepWaitMax s
// at most); then the curious helper is retired and the noise spawned at the
// spot: the same helper with a thrown item's landing (fr::kFootstepNoiseEvent;
// the radio's aisoundattractor when the helper is not loaded), its sound
// reaching him from where he is then. A takedown, another investigation or a
// lost follow cancels the case. For kFootstepWatchSeconds after each spawn the
// helper's pins (and its sub-entities'), the AI events, the joining NPCs and
// their distance to it are logged; then DisablePerception, and the helper is
// deleted 2 s later. Cooldowns: kFootstepGlobalCooldown between hits,
// kFootstepActorCooldown per guard; one case or helper at a time. Lines start
// with "footstep investigate".
//
// backup_guard (on by default; its lines are written while it is on): a
// guard joining an Investigate_Curious for a noise distraction
// (fr::IsBackupNoise: a thrown item or coin, a radio) is armed
// (fr::BackupBook); his sampled behavior says when his radio call ends,
// when he heads over, reaches the spot (its position is read then) and
// finishes (disarmed: back to his routine, or out of the search animation at
// the spot). A Pacify or Kill of an armed guard after a radio call that counts
// (fr::kBackupCallMinSeconds; a shorter line is a bark) schedules a backup
// (fr::RollBackupDelay after the call's end, at once when that has passed).
// When it is due, the living NPCs are read (those taken down this scene left
// out) and fr::PlanBackupReach sets the sound range (the nearest guard not on
// a post, plus a margin: posted are checkpoint sentries (m_bActiveSentry) and
// guards seen in a BT_Sentry* behavior this scene); with no guard read, no backup is sent. Else the investigate-curious helper is spawned at
// the spot (a footstep helper out is retired first; another backup's helper
// is waited for) with its sound ranges written to that range, its sphere and
// perceptible range to fr::kBackupPerceptibleRange (so a civilian walking by
// is not drawn), and its two sound events' m_eType to sounds of a struggle
// (fr::kBackupSoundEvent), each read back, then EnablePerception. Its joiners
// are logged with their kind (guard or civilian), their investigation type
// (also a join with none) and distance; a cautious search has no join
// event, so a guard in reach who goes into an
// investigation behavior in the window (ScanNpcs) answers it too; it is switched off and deleted as the
// footstep helper is. backup_guard_notice: the overlay shows a notice
// (NoticeSerial, NoticeText) when the first NPC answers in the helper's window
// (fr::AnswersBackupNoise; fr::BackupAnswer), one per backup: "Backup is on the way"
// for a guard, "Someone is coming to investigate" for a civilian; the
// window's end logs when no guard answered. Lines start with "backup:".

#include "../core/Investigations.h"
#include "../core/Seed.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ZActor;
class ZDynamicObject;
class ZObjectRef;

class InvestigationFeatures {
public:
    void Configure(bool p_FootstepInvestigate, int p_FootstepChance, bool p_BackupGuard,
                   fr::BackupDelays p_BackupDelays, bool p_BackupNotice);

    // Whether the SignalInputPin hook is needed.
    bool Wanted() const { return m_StepOn || m_BackupOn; }

    // Any thread: counts the backup notices (the overlay shows one when it changes); NoticeText is the
    // latest one's text ("Backup is on the way": only a guard's answer shows one).
    uint32_t NoticeSerial() const { return m_NoticeSerial.load(std::memory_order_acquire); }
    std::string_view NoticeText() const {
        return fr::BackupNoticeText(static_cast<fr::BackupNotice>(m_NoticeKind.load(std::memory_order_relaxed)));
    }

    // One relaxed load each (hot paths).
    bool EventsWanted() const { return m_SceneOn.load(std::memory_order_relaxed); }
    bool PinsOn() const { return m_PinsOn.load(std::memory_order_relaxed); }

    // Game thread, from the SetLoadingStage detour; p_Seed: the mission's seed when resolved (the footstep rolls').
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                        const std::optional<fr::SeedResolution>& p_Seed) noexcept;
    // Game thread, at a scene clear: a summary, then the scene is forgotten (the helper goes with the scene).
    void OnSceneClear() noexcept;
    // Game thread, every play-mode frame.
    void Tick(double p_Dt) noexcept;

    // Any thread, from the OnEventSent detour.
    void OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept;
    // The pin detours: a pin of a watched entity (the helper and its sub-entities) is logged.
    void OnPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data, bool p_Input) noexcept;

private:
    struct QueuedEvent {
        uint32_t index = 0;
        std::string name;
        std::string json; // redacted, one line, cut
        std::optional<fr::SituationEvent> situation;
        std::optional<fr::Guid> repo; // a takedown's
    };

    struct Npc {
        ZEntityType** entity = nullptr;
        const ZActor* actor = nullptr;
        uint64_t id = 0;
        std::string name; // quoted, or a note
    };

    // An NPC followed after he joined an investigation.
    struct Followed {
        Npc npc;
        int type = 0;
        double joinedAt = 0.0;
        double nextAt = 0.0;
        std::optional<std::array<float, 3>> joinedPos;
        bool fromHelper = false; // joined while a footstep helper was out
    };

    struct Watch {
        std::string label;
        bool helper = false;
        uint32_t pins = 0; // pins seen (under m_WatchMutex)
    };

    // footsteps_investigate: a hit, its curious helper out, then waiting for the hearer (fr::FootstepCase).
    struct Footstep {
        uint32_t number = 0; // hits this scene
        fr::Guid repo;
        Npc npc;
        std::string who;
        std::array<float, 3> spot {};   // 47 at the hearing: where both helpers go
        std::optional<float> heardFrom; // his distance to 47 at the hearing
        fr::FootstepCase state;
        std::optional<int32_t> behaviorAtHearing;
        std::optional<int32_t> lastLogged; // his last behavior logged under "footstep investigate:"
        uint32_t curiousHelper = 0;        // the curious helper's number (0: it did not spawn)
    };

    enum class HelperKind { Helper, Attractor };

    struct Helper {
        ZEntityType** root = nullptr;
        HelperKind kind = HelperKind::Helper;
        uint32_t number = 0;
        double spawnedAt = 0.0;
        std::array<float, 3> at {};
        std::string forActor;
        bool disabled = false;
        bool deleted = false;
        std::vector<std::string> fired;
        uint32_t joins = 0;
        bool backup = false;       // a backup guard's helper (else a footstep helper)
        uint32_t backupNumber = 0; // its plan's number
        fr::BackupAnswer answer;   // backup: its joiners, guards or not; the notice
        float reach = 0.0f;        // backup: its sound range
        int32_t sound = 0;         // its sound events' type (0: the template's)
    };

    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                      const std::optional<fr::SeedResolution>& p_Seed);
    std::string SeedText() const;
    void FrameTick(double p_Dt);
    void Forget();
    void Acquire();
    void Release();
    void ProcessEvents();
    void ProcessEvent(const QueuedEvent& p_Event);
    void OnSituationEvent(const QueuedEvent& p_Event);
    void OnTakedown(const QueuedEvent& p_Event);
    // footsteps_investigate: a hit opens the case and spawns the curious helper.
    void StartFootstep(const fr::SituationEvent& p_Event, const Npc* p_Npc);
    // The case's curious helper at its spot (fr::kFootstepCuriousEvent, reaching the hearer's distance at the
    // hearing); false when it did not spawn.
    bool SpawnCurious();
    // The case's curious helper is the one out.
    bool CuriousOut() const;
    // His behavior sampled (each sample, from SampleFollowed).
    void FootstepBehavior(int32_t p_Type);
    // Every frame: the case's timeouts.
    void FootstepTick();
    // An open case canceled (logged with p_Detail).
    void CancelFootstep(fr::FootstepOutcome p_Why, const std::string& p_Detail);
    // Acts on a case's step: spawn the noise, log.
    void ApplyFootstep(const fr::FootstepStep& p_Step, const std::string& p_Detail);
    // The noise for the case: the curious helper retired, then the helper at its spot with a thrown item's landing
    // (fr::kFootstepNoiseEvent), its sound reaching the hearer (fr::FootstepReach).
    void SpawnHelper();
    // p_Backup: a backup guard's helper. p_Radius > 0: its sound ranges written to p_Radius, its sound events to
    // p_SoundEvent, its sphere and perceptible range to fr::kBackupPerceptibleRange, before its pins are signaled
    // (a backup's and a footstep's helper alike).
    bool TrySpawn(HelperKind p_Kind, int32_t p_Index, const std::array<float, 3>& p_At, std::string* p_Why,
                  bool p_Backup = false, float p_Radius = 0.0f, int32_t p_SoundEvent = 0);
    void WriteHelperRanges(ZEntityType** p_Root, const std::vector<std::pair<ZEntityType**, std::string>>& p_Subs,
                           float p_Radius, int32_t p_SoundEvent);
    void HelperTick();
    // Switches the helper out off and deletes it now (p_Why logged).
    void RetireHelper(const char* p_Why);
    const char* HelperTag() const { return m_Helper.backup ? "backup:" : "footstep investigate:"; }
    // backup_guard
    void BackupJoin(const fr::SituationEvent& p_Event, const Npc* p_Npc,
                    const std::optional<std::array<float, 3>>& p_Pos, bool p_Window);
    void BackupBehavior(const fr::Guid& p_Repo, const Followed& p_Follow, int32_t p_Type);
    void BackupTakedown(const fr::Guid& p_Repo, const std::string& p_Who, const std::optional<int32_t>& p_Last,
                        const std::optional<std::array<float, 3>>& p_Pos);
    void BackupTick();
    bool Dispatch(const fr::BackupPlan& p_Plan);
    void SampleFollowed();
    void ScanNpcs();
    // ScanNpcs: an NPC into an investigation behavior while a backup's helper listens: a guard in its reach
    // answers the backup sound (a cautious search has no join event).
    void ScanBackupAnswer(const Npc& p_Npc, int32_t p_Type, const std::optional<std::array<float, 3>>& p_Pos);
    // The first answer of a backup: the notice (backup_guard_notice) and its line.
    void ShowBackupNotice(fr::BackupNotice p_Notice, const std::string& p_Who, bool p_Guard,
                          const std::optional<std::array<float, 3>>& p_Pos);
    std::optional<Npc> FindNpc(const fr::Guid& p_Repo);
    void RebuildIndex();
    std::optional<std::array<float, 3>> PositionOf(ZEntityType** p_Entity);
    std::optional<std::array<float, 3>> HeroPosition();
    std::optional<int32_t> BehaviorOf(const ZActor* p_Actor);
    std::string PinName(uint32_t p_Pin) const;
    std::string NpcText(const Npc& p_Npc) const;
    void AddWatch(ZEntityType** p_Entity, std::string p_Label, bool p_Helper);
    void RemoveWatches(bool p_Helper);
    bool HelperOut() const { return m_Helper.root != nullptr && !m_Helper.deleted; }
    bool FootstepOpen() const { return m_Footstep && m_Footstep->state.Open(); }
    bool HelperWindow() const;
    bool TakeLine();
    // A line written only when p_Helper (a helper is out) and the helper's
    // feature (footsteps_investigate or backup_guard) is on.
    template <class... A>
    void Line(bool p_Helper, fmt::format_string<A...> p_Format, A&&... p_Args);

    bool m_StepOn = true;
    int m_StepChance = fr::kDefaultFootstepInvestigateChance; // percent, 0..100
    bool m_BackupOn = true;
    fr::BackupDelays m_BackupDelays;
    bool m_NoticeOn = true;

    // Game thread, per scene.
    fr::BackupBook m_Backup;
    std::deque<fr::BackupPlan> m_BackupWaiting; // due, waiting for another backup's helper to go
    bool m_BackupWaitLogged = false;
    std::map<std::string, uint32_t> m_BackupCounts; // summary: what happened to armed guards and plans
    std::set<fr::Guid> m_Downed; // NPCs pacified or killed this scene: left out of a backup's guards and civilians
    bool m_Mission = false;
    bool m_Playing = false;
    double m_Now = 0.0; // s of play
    std::string m_Scene;
    std::string m_RollScene;                     // the logical scene (SceneOf), hashed into the footstep rolls
    std::optional<fr::SeedResolution> m_Seed;    // this load's, for the footstep rolls (0 while unknown)
    std::array<int32_t, 2> m_Resources {-1, -1}; // resource index of the helper and the attractor
    std::array<bool, 2> m_SpawnFaulted {};
    fr::FootstepGate m_Gate;
    std::optional<Footstep> m_Footstep; // the open case, or the last one
    uint32_t m_FootstepCount = 0;
    std::map<std::string, uint32_t> m_FootstepOutcomes; // summary: how the cases ended
    fr::InvestigationBook m_Book;
    fr::BehaviorTrack m_Behaviors;     // the followed NPCs', by ZActor
    fr::BehaviorTrack m_ScanBehaviors; // the NPC scan's, by ZActor
    std::map<fr::Guid, Followed> m_Followed;
    Helper m_Helper;
    uint32_t m_HelperCount = 0;
    double m_NextScanAt = 0.0;
    std::map<fr::Guid, Npc> m_Index;
    double m_IndexBuiltAt = -1e9;
    std::map<std::string, uint32_t> m_Verdicts;
    uint32_t m_ScanLines = 0;

    // Any thread.
    std::atomic<bool> m_SceneOn {false};
    std::atomic<bool> m_PinsOn {false};
    std::atomic<uint32_t> m_Lines {0};
    std::atomic<double> m_PlaySeconds {0.0};
    std::atomic<uint32_t> m_HelperNumber {0}; // the helper out (0: none), for the pin lines
    std::atomic<double> m_HelperSpawnedAt {0.0};
    std::atomic<uint32_t> m_HelperOutputs {0};         // output pins of the helper out
    std::atomic<double> m_HelperInvestigatedAt {-1.0}; // its OnInvestigated, s of play (-1: not seen)
    std::atomic<bool> m_HelperIsBackup {false};        // the helper out is a backup's (pin lines' prefix)
    std::atomic<uint32_t> m_NoticeSerial {0};          // backup notices shown (backup_guard_notice)
    std::atomic<uint32_t> m_NoticeKind {0};            // the latest one's fr::BackupNotice (stored before the serial)
    std::mutex m_QueueMutex;
    std::deque<QueuedEvent> m_Queue;
    uint32_t m_QueueDropped = 0;
    std::shared_mutex m_WatchMutex;
    std::unordered_map<ZEntityType**, Watch> m_Watches;
};
