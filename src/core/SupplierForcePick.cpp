#include "SupplierForcePick.h"

#include <algorithm>

namespace fr {

ForcePickVerdict JudgeForcePick(const ForcePickSlot& slot) {
    if (slot.done > 0 || slot.failed > 0)
        return ForcePickVerdict::Signaled;

    if (slot.pickedBefore)
        return ForcePickVerdict::PickedBefore;

    if (slot.forced)
        return ForcePickVerdict::AlreadyForced;

    if (slot.eligible == 0)
        return ForcePickVerdict::NoEligible;

    if (slot.standOffers > 0)
        return ForcePickVerdict::StandFilled;

    return ForcePickVerdict::Force;
}

bool IsAlternativeStandBlock(char block) {
    return block == 'B' || block == 'C' || block == 'D' || block == 'E' || block == '?';
}

const char* ForcePickVerdictText(ForcePickVerdict verdict) {
    switch (verdict) {
        case ForcePickVerdict::Force: return "silent with eligible guns: forced";
        case ForcePickVerdict::Signaled: return "answered";
        case ForcePickVerdict::PickedBefore: return "silent, but picked earlier this scene: left alone";
        case ForcePickVerdict::NoEligible: return "silent, no eligible gun: left alone";
        case ForcePickVerdict::AlreadyForced: return "already forced this burst";
        case ForcePickVerdict::StandFilled:
            return "silent, but the supplier filled its stands with other items this visit: left alone (a forced "
                   "pick would put a second item on a stand)";
    }

    return "?";
}

bool GunPickBurst::OnSignal(double at) {
    const bool s_New = !m_Open;

    if (s_New) {
        m_Open = true;
        m_First = at;
        m_Last = at;
        ++m_Visits;
    }
    else {
        m_Last = std::max(m_Last, at);
    }

    return s_New;
}

ForcePickOutcome ForcePickOutcomeOf(uint32_t done, uint32_t failed) {
    if (done > 0)
        return ForcePickOutcome::Picked;

    return failed > 0 ? ForcePickOutcome::Failed : ForcePickOutcome::Swallowed;
}

const char* ForcePickOutcomeText(ForcePickOutcome outcome) {
    switch (outcome) {
        case ForcePickOutcome::Picked: return "PICKED (OnPickDone)";
        case ForcePickOutcome::Failed: return "FAILED (OnPickFailed only)";
        case ForcePickOutcome::Swallowed: return "SWALLOWED (no OnPickDone / OnPickFailed)";
    }

    return "?";
}

} // namespace fr
