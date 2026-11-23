#include "FishingScript.h"

#include "TextNoCase.h"

#include <cmath>
#include <utility>

namespace fr {

namespace {

std::string ItemText(const std::optional<FishCatch>& p_Item) {
    return p_Item ? std::string(FishCatchName(*p_Item)) : std::string("an unknown item (no OnPicked seen)");
}

} // namespace

const char* FishCatchName(FishCatch p_Catch) {
    switch (p_Catch) {
        case FishCatch::Fish: return "fish";
        case FishCatch::FishingLine: return "fishing line";
        case FishCatch::PirateSaber: return "pirate saber";
        case FishCatch::Other: return "other";
    }

    return "?";
}

std::optional<FishCatch> ScriptedCatch(uint32_t p_Number) {
    if (p_Number < 1 || p_Number > kFishingScript.size())
        return std::nullopt;

    return kFishingScript[p_Number - 1];
}

FishCatch ClassifyFishItem(const std::optional<Guid>& p_Repository, uint64_t p_KeyId, std::string_view p_Title) {
    static const std::optional<Guid> s_Fish = ParseGuid(kFishRepositoryId);
    static const std::optional<Guid> s_Line = ParseGuid(kFishingLineRepositoryId);

    if (p_Repository && s_Fish && *p_Repository == *s_Fish)
        return FishCatch::Fish;

    if (p_Repository && s_Line && *p_Repository == *s_Line)
        return FishCatch::FishingLine;

    if (p_KeyId == kPirateSaberKeyId || ContainsNoCase(p_Title, "pirate"))
        return FishCatch::PirateSaber;

    return FishCatch::Other;
}

FishingSteerPlan PlanFishingSteer(std::span<const FishCatch> p_Items, std::span<const std::optional<float>> p_Weights,
                                  FishCatch p_Want) {
    FishingSteerPlan s_Plan;
    const std::string s_Want = FishCatchName(p_Want);

    if (p_Items.size() != p_Weights.size() || p_Items.empty()) {
        s_Plan.why = "the choices and their weights do not match: vanilla";
        return s_Plan;
    }

    for (size_t i = 0; i < p_Items.size(); ++i) {
        if (!p_Weights[i] || !std::isfinite(*p_Weights[i])) {
            s_Plan.why = "choice " + std::to_string(i + 1) + "'s weight is unreadable: vanilla";
            return s_Plan;
        }

        s_Plan.targets += p_Items[i] == p_Want ? 1 : 0;
    }

    if (s_Plan.targets == 0) {
        s_Plan.why = "no choice holds the " + s_Want + ": vanilla";
        return s_Plan;
    }

    s_Plan.weights.resize(p_Items.size());

    for (size_t i = 0; i < p_Items.size(); ++i)
        s_Plan.weights[i] = p_Items[i] != p_Want ? 0.0f : *p_Weights[i] > 0.0f ? *p_Weights[i] : 1.0f;

    s_Plan.steer = true;
    s_Plan.why = "the " + s_Want + " is the only weighted catch (" + std::to_string(s_Plan.targets) + " of " +
                 std::to_string(p_Items.size()) + " choices)";
    return s_Plan;
}

FishingRolls::FishingRolls(bool p_On, uint32_t p_Catches, std::vector<FishCatch> p_Items)
    : m_On(p_On), m_Catches(p_Catches), m_Items(std::move(p_Items)) {}

FishingRollStart FishingRolls::Start(std::span<const std::optional<float>> p_Weights, double p_Now) {
    FishingRollStart s_Out;

    if (m_Open)
        return s_Out;

    m_Open = true;
    m_Steering = false;
    m_OpenedAt = p_Now;
    m_Picked.reset();
    m_IsRetry = m_NextVanilla;
    m_NextVanilla = false;
    m_Number = m_Catches + 1;
    m_Wanted = m_On ? ScriptedCatch(m_Number) : std::nullopt;
    ++m_Rolls;

    s_Out.opened = true;
    s_Out.number = m_Number;
    s_Out.wanted = m_Wanted;

    if (!m_On) {
        s_Out.why = "fishing_script off: vanilla";
    }
    else if (!m_Wanted) {
        s_Out.why = "catch " + std::to_string(m_Number) + " is past the script: vanilla";
    }
    else if (m_IsRetry) {
        s_Out.why = std::string("re-pick after the ") + FishCatchName(*m_Wanted) + " could not be drawn: vanilla";
    }
    else {
        s_Out.plan = PlanFishingSteer(m_Items, p_Weights, *m_Wanted);
        s_Out.steer = s_Out.plan.steer;
        s_Out.why = s_Out.plan.why;
        m_Steering = s_Out.steer;
        m_SteeredRolls += m_Steering ? 1 : 0;
    }

    return s_Out;
}

void FishingRolls::Picked(size_t p_Choice) {
    if (m_Open && !m_Picked)
        m_Picked = p_Choice;
}

FishingRollEnd FishingRolls::Done(double) {
    return Close(true, "OnPickDone");
}

FishingRollEnd FishingRolls::Failed(double) {
    return Close(false, "OnPickFailed");
}

FishingRollEnd FishingRolls::Tick(double p_Now) {
    if (!m_Open || p_Now - m_OpenedAt < kFishingRollTimeout)
        return {};

    FishingRollEnd s_End;
    s_End.closed = true;
    s_End.restore = m_Steering;
    s_End.number = m_Number;
    s_End.catches = m_Catches;
    s_End.picked = m_Picked;
    s_End.verdict = "no OnPickDone or OnPickFailed within " + std::to_string(static_cast<int>(kFishingRollTimeout)) +
                    " s of the Pick: nothing counted" + (m_Steering ? ", weights restored" : "");
    m_Open = false;
    m_Steering = false;
    m_IsRetry = false;
    return s_End;
}

bool FishingRolls::TakeRetry() {
    if (!m_RetryPending)
        return false;

    m_RetryPending = false;
    m_NextVanilla = true;
    return true;
}

FishingRollEnd FishingRolls::Close(bool p_Done, const char* p_How) {
    FishingRollEnd s_End;

    if (!m_Open)
        return s_End;

    s_End.closed = true;
    s_End.restore = m_Steering;
    s_End.number = m_Number;
    s_End.picked = m_Picked;

    std::optional<FishCatch> s_Item;

    if (m_Picked && *m_Picked < m_Items.size())
        s_Item = m_Items[*m_Picked];

    const std::string s_Caught = ItemText(s_Item);

    if (p_Done) {
        if (m_On) {
            ++m_Catches;
            s_End.counted = true;
        }

        if (m_Steering) {
            if (s_Item == m_Wanted) {
                ++m_AsPlanned;
                s_End.verdict = "caught the " + s_Caught + " as planned";
            }
            else {
                ++m_Missed;
                s_End.verdict = "caught " + s_Caught + ", NOT the planned " + ItemText(m_Wanted);
            }
        }
        else if (m_Wanted && m_IsRetry) {
            s_End.verdict = "vanilla re-pick (the " + ItemText(m_Wanted) + " could not be drawn): caught " + s_Caught;
        }
        else if (m_Wanted) {
            s_End.verdict = "not steered (the " + ItemText(m_Wanted) + " was planned): caught " + s_Caught;
        }
        else {
            s_End.verdict = "vanilla: caught " + s_Caught;
        }
    }
    else if (m_Steering && !m_IsRetry) {
        m_RetryPending = true;
        ++m_RetryCount;
        s_End.retry = true;
        s_End.verdict =
            std::string(p_How) + ": the " + ItemText(m_Wanted) +
            " could not be drawn (its condition hides it: owned already?); weights restored, the selector picks "
            "again unsteered (vanilla)";
    }
    else {
        s_End.verdict =
            std::string(p_How) + ": nothing caught" + (m_IsRetry ? " (the vanilla re-pick failed too)" : "");
    }

    s_End.catches = m_Catches;
    m_Open = false;
    m_Steering = false;
    m_IsRetry = false;
    return s_End;
}

} // namespace fr
