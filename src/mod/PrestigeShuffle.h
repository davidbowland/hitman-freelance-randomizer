#pragma once

// Prestige card shuffle (prestige_shuffle, on by default): the three prestige
// objectives the safehouse laptop offers come from the campaign seed and a
// pick counter (fr::DrawPrestigeCards; ids and chain in core/PrestigeCards.h).
//
// In the Freelancer safehouse the mod finds the three card pickers' selectors
// and the m_Weight of each choice. Right before Picker1 .Pick it draws three
// cards (fr::ChoosePrestigeCards: card A a Silent Assassin or Timed
// objective, at most one per category, prestige_exclude kept out where it
// can be) and writes m_Weight 0 on every choice of picker N except card N's
// objective's, so the game's own pick path can only land on the drawn
// choice. m_Choices is never written. A picker that shows another objective
// than the one forced rejected it (its m_Condition): the cards are drawn
// again through the game's reset, at most kMaxRedraws times per visit. Every
// ObjectiveId emitted during a take is rewritten to what the card shows
// (fr::PrestigeTakeCorrection). The weights are written back at the scene
// clear and when the mod unloads (Restore).
//
// Guard: after a forced draw, every picker must signal OnPickDone within
// kGuardSeconds of game time; otherwise (or on OnPickFailed, or a selector
// picking or signaling too often) the original weights go back and the game
// picks its own cards for the rest of the visit ("GUARD TRIPPED").
//
// Pick state ([campaign] prestige_shuffle): the cards stay the same across
// reloads, sessions and the laptop opened again; they change for a new
// campaign, or once a card was taken and a Freelancer mission started.
// Anything unexpected (no seed, a picker or weight not found, too few cards
// drawable, a write that does not read back) leaves the game's own cards.
// Perfect Run (136) is never drawn: its entry has no laptop screen.

#include "PrestigeCards.h"

#include <Glacier/ZEntity.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

class ZObjectRef;

class PrestigeShuffle {
public:
    using SeedSource = std::function<std::optional<int32_t>()>;
    using StateSaver = std::function<void(const std::string&)>;

    // p_Exclude: prestige_exclude's ids, never drawn.
    void Configure(bool p_On, std::set<int32_t> p_Exclude, std::optional<fr::PrestigePickState> p_State,
                   SeedSource p_Seed, StateSaver p_Save);

    bool On() const { return m_On; }

    // The campaign whose pick state is stored as seed p_From is now known by
    // its own seed p_To (fr::RekeyPrestigePick); saved. Logs what it did.
    void RekeyCampaign(int32_t p_From, int32_t p_To);

    // Game thread, from the SetLoadingStage detour. Catches what it throws.
    void OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept;

    // Game thread, at a scene clear: writes the original elements back and forgets the scene.
    // p_Optin: the CPD's OptinPayoutObjective now (for the no-pick line), or empty.
    void OnSceneClear(std::optional<int32_t> p_Optin) noexcept;

    // Game thread, every play-mode frame: the report after a draw.
    void Tick(double p_Dt) noexcept;

    // When the mod unloads (possibly during a safehouse visit, before the scene
    // clears): turns the shuffle off and writes back every weight still forced,
    // so the laptop offers the game's own cards again.
    void Restore(const char* p_Why) noexcept;

    // Hot path: one relaxed load outside the safehouse.
    bool PinsOn() const noexcept { return m_PinsOn.load(std::memory_order_relaxed); }

    void OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept;
    void OnInputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept;

private:
    static constexpr size_t kElementSize = 16; // TEntityRef<T>: the entity pointer and the interface pointer
    static constexpr int kMaxSelectorPicks = 3;
    static constexpr int kMaxSelectorOutputs = 40;
    static constexpr int kMaxPinLines = 200;
    static constexpr double kGuardSeconds = 1.0; // game time for all three pickers to be done after a draw
    static constexpr int kMaxRedraws = 16;       // picks again after a rejected card, per visit

    enum Role : size_t {
        kPickFork,
        kRepickFork,
        kReturnPipe,
        kTakeFork1,
        kTakeFork2,
        kTakeFork3,
        kUnique,
        kSetCpd,
        kPicker1,
        kPicker2,
        kPicker3,
        kSelector1,
        kSelector2,
        kSelector3,
        kRoleCount,
    };

    using Bytes = std::array<uint8_t, kElementSize>;

    struct Element {
        Bytes bytes {};
        ZEntityType** entity = nullptr;
        std::optional<int32_t> objective;
        std::optional<float> weight; // m_Weight at load
        uintptr_t weightAddress = 0; // m_Weight's storage (a raw float32); 0 when not located
        std::string how;             // how the objective was named, or why not
        uint64_t id = 0;             // the element entity's own sub-entity id (0: not read)
        size_t depth = 0;            // how far up its owner chain the choice entry is (when named)
    };

    // A weight the mod set to 0, with the value it had.
    struct Zeroed {
        uintptr_t address = 0;
        float original = 0.0f;
    };

    struct Picker {
        ZEntityType** root = nullptr;
        // The picker template's factory (the brick factory's GetSubEntityBlueprint):
        // a nested template instance has none of its own (GetBlueprintFactory is null).
        ZEntityBlueprintFactoryBase* factory = nullptr;
        ZEntityType** selector = nullptr;
        std::string selectorFrom; // how the selector was found
        uintptr_t property = 0;   // the TArray's address
        uintptr_t begin = 0;      // its elements
        size_t count = 0;
        std::vector<Element> original;
        std::optional<int32_t> forced; // the objective forced, while the weights are written
        std::vector<Zeroed> zeroed;    // the weights written 0, while written
        // Since the last draw.
        int selectorPicks = 0;
        int selectorOutputs = 0;
        int pickerOutputs = 0;
        bool pickDone = false;
        bool pickFailed = false; // its selector's OnPickFailed, or the loop guard
    };

    void LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context);
    bool Find(ZEntitySceneContext* p_Context, const char* p_Stage); // false: try again at the next stage
    bool ReadPicker(size_t p_Index, std::string* p_Why);
    void FindSelectorsByWalk(ZEntityType** p_BrickRoot);
    void LogPickerShape(size_t p_Index);
    void Reset();
    void ClearWatch(); // under m_Mutex
    // Any thread: the role of a watched entity.
    std::optional<size_t> WatchedRole(ZEntityType** p_Entity) const;

    // Under m_Mutex.
    void Handle(size_t p_Role, uint32_t p_PinId, const ZObjectRef& p_Data, bool p_Input);
    void Apply(const char* p_Trigger);
    bool LiveSlots(size_t p_Index, std::vector<fr::PrestigeSlot>* p_Out, std::string* p_Why) const;
    bool WriteAll(const std::array<int32_t, fr::kPrestigeCards>& p_Cards,
                  const std::array<std::vector<fr::PrestigeSlot>, fr::kPrestigeCards>& p_Slots, std::string* p_Why);
    bool WeightsHold(const std::array<int32_t, fr::kPrestigeCards>& p_Cards) const;
    void RestoreAll(const char* p_Why);
    bool ArrayUnmoved(const Picker& p_Picker, std::string* p_Why) const;
    void Guard(double p_Dt);
    void NoteObjective(const char* p_Where, int32_t p_Value);
    bool TakeLine();
    void SaveState();
    void Played(const std::string& p_Scene);
    void NoteShown(ZEntityType** p_Poll, int32_t p_Value);
    void CorrectTake(const ZObjectRef& p_Data, uint64_t p_Id, int32_t p_Value);
    bool Redraw();

    bool m_On = true;
    std::set<int32_t> m_Exclude = fr::DefaultPrestigeExclude(); // prestige_exclude
    SeedSource m_Seed;
    StateSaver m_Save;
    fr::PrestigePickState m_State;

    // This load (game thread; the pin hooks read m_Watch once m_PinsOn is set,
    // without the lock, and check their match again under it).
    bool m_Done = false;
    bool m_IsSafehouse = false;
    std::string m_Scene;
    std::atomic<bool> m_PinsOn {false};
    std::array<std::atomic<ZEntityType**>, kRoleCount> m_Watch {};
    std::array<Picker, fr::kPrestigeCards> m_Pickers {};
    bool m_Ready = false;  // the arrays were read and every element named
    bool m_GaveUp = false; // the guard ran: vanilla for the rest of the scene

    // Shared with the pin hooks.
    std::recursive_mutex m_Mutex;
    bool m_FromReturn = false; // SignalPipe_Conditional_Void .Out seen, SignalFork_Void not yet
    std::optional<std::array<int32_t, fr::kPrestigeCards>> m_Cards; // the current draw
    // What each card shows since the last draw (its display poll's objective).
    std::array<std::optional<int32_t>, fr::kPrestigeCards> m_Shown {};
    std::optional<size_t> m_Taking; // a card's take (Poll) is running: rewrite its ObjectiveId
    int m_Redraws = 0;              // picks again after a rejected card in this visit
    std::string m_LastTrigger;
    bool m_ReportPending = false;
    double m_ReportIn = 0.0;
    bool m_GuardPending = false; // a forced draw waits for every picker's OnPickDone
    double m_GuardElapsed = 0.0;
    std::vector<std::string> m_Emitted; // ObjectiveId / CPD values seen since the draw
    int m_PinLines = 0;
    bool m_WarnedNotReady = false;
    bool m_Picked = false;    // SignalFork_Void_04 fired (Apply ran) in this visit
    bool m_Redrawing = false; // the mod signaled SignalFork_Void .In; its .Out06 not seen yet

    // Weights written 0 and not yet written back, by address, with their
    // original: a brick kept loaded across a scene clear is not taken as
    // vanilla. Game thread, under m_Mutex.
    std::map<uintptr_t, float> m_SessionWeights;
    // Objectives each card's picker would not show (this visit; cleared by Reset).
    fr::PrestigeRejected m_Rejected {};
    bool m_LoggedChain = false; // the owner chain and properties of one m_Choices element, once per session
};
