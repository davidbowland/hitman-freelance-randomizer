#pragma once

// Supplier force pick (setting supplier_force_pick, on by default): the pure
// part. The game side
// is src/mod/StockSteer.* (the steering's slots and pick signals).
//
// At a supplier visit the five gun selectors (m_Seed b73d9349324f8074) send their pick
// signals in a fixed order within ~130 ms (#8 shotgun, #7 SMG, #6 pistol, #4
// assault rifle, #5 sniper); an empty slot fires OnPickFailed twice, one
// frame apart. The pistol selector sometimes sends nothing at all although
// eligible pistols exist (with or without steering), while the 67 ms of its stage still pass between #7 and #4: whatever
// gates its Pick sits upstream (its input pins are engine-wired, invisible to
// the SignalInputPin hook) or in the selector.
//
// The fallback: once the gun selectors' burst of pick signals has been quiet
// for kForcePickQuietSeconds, every gun selector that sent neither signal in
// that burst, never picked before this scene, has at least one eligible gun
// (not owned, entitled: fr::StockEligible) and was not forced in this burst
// already gets its Pick input signaled by the mod. Its answer (OnPickDone,
// OnPickFailed or nothing) and any offer under it are logged
// kForcePickOutcomeSeconds later.
//
// The double item ("several items in one slot, only one selectable"): a
// silent gun selector is the game's own choice, not a stuck one. When the
// pistol selector stays silent, in the same frames the supplier can offer
// items from the "alternative stands" among the
// options in no selector (blocks C and E: (COMIC) Knife, (COMIC) Sword; see
// LooseBlockOf in SupplierRules.h) next to the usual gear stands (A, F); a
// forced Pick then puts a pistol on a stand that already holds one of them.
// Blocks B to E (tools, melee, the Kalmer dartgun) only offer at visits where
// some gun slot came up without a gun (failed or silent), never when the gun
// slots picked: the game fills a gun stand it leaves empty with
// such an item. Which stand each one goes to is not known, so a visit where
// any alternative stand offered something is never forced
// (ForcePickVerdict::StandFilled): an empty stand is better than a doubled
// one. The burst is judged kForcePickQuietSeconds after its last signal, late
// enough for those offers (they come 0.1-0.3 s after the burst).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fr {

// The burst counts as over this long (s of play) after its last pick signal
// (the five slots' signals spanned 130 ms; frames are ~16-33 ms; the
// alternative stands' offers came up to 0.3 s after the burst's first signal,
// so 0.3 s was too tight: Ambrose Island judged 0.31 s after, 0.05 s after
// the last of them).
inline constexpr double kForcePickQuietSeconds = 1.0;

// A block of the options in no selector (LooseBlockOf in SupplierRules.h) that
// is one of the supplier's alternative stands: B and D (tools / pistols), C
// and E (melee). A and F are the gear stands every visit offers from. '?' (a
// layout the logs have not shown) counts as an alternative stand: forcing a
// Pick there could double a stand, leaving it alone cannot.
bool IsAlternativeStandBlock(char block);

// The forced pick's answer is judged this long (s of play) after the Pick.
inline constexpr double kForcePickOutcomeSeconds = 1.0;

// One gun selector at the end of a burst.
struct ForcePickSlot {
    uint32_t done = 0;         // OnPickDone in this burst
    uint32_t failed = 0;       // OnPickFailed in this burst
    bool pickedBefore = false; // an OnPickDone earlier in this scene (any burst)
    size_t eligible = 0;       // guns not owned and entitled, read now
    bool forced = false;       // already forced in this burst
    size_t standOffers = 0;    // options of the alternative stands offered in this burst (IsAlternativeStandBlock)
};

enum class ForcePickVerdict {
    Force,        // silent, eligible guns, not picked before, no alternative stand offered: signal its Pick
    Signaled,     // it sent OnPickDone or OnPickFailed in this burst
    PickedBefore, // it picked in an earlier burst of this scene
    NoEligible,   // nothing it could offer: silent is as good as failed
    AlreadyForced,
    StandFilled, // silent, but the game filled the supplier's stands with other items this visit
};

ForcePickVerdict JudgeForcePick(const ForcePickSlot& slot);

const char* ForcePickVerdictText(ForcePickVerdict verdict);

// The gun selectors' burst of pick signals (play time, s). A signal while
// closed opens a burst; Due once kForcePickQuietSeconds have passed since its
// last signal; Close ends it. Visits counts the bursts opened.
class GunPickBurst {
public:
    explicit GunPickBurst(double quietSeconds = kForcePickQuietSeconds) : m_Quiet(quietSeconds) {}

    // True when this signal opened a new burst.
    bool OnSignal(double at);

    bool Due(double now) const { return m_Open && now - m_Last >= m_Quiet; }

    void Close() { m_Open = false; }

    bool Open() const { return m_Open; }

    double First() const { return m_First; }

    double Last() const { return m_Last; }

    uint32_t Visits() const { return m_Visits; }

private:
    double m_Quiet;
    bool m_Open = false;
    double m_First = 0.0;
    double m_Last = 0.0;
    uint32_t m_Visits = 0;
};

enum class ForcePickOutcome {
    Picked,    // OnPickDone came
    Failed,    // only OnPickFailed came
    Swallowed, // nothing came: the Pick was ignored (or the selector is gated inside)
};

ForcePickOutcome ForcePickOutcomeOf(uint32_t done, uint32_t failed);

const char* ForcePickOutcomeText(ForcePickOutcome outcome);

} // namespace fr
