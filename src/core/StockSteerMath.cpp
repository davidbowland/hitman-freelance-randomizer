#include "StockSteerMath.h"

#include "CrateGunMath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fr {

namespace {

constexpr double kNoRarityValue = 100.0;
constexpr double kHeldTolerance = 1e-4;

bool Usable(double v) {
    return std::isfinite(v) && v > 0.0;
}

std::string Num(double v) {
    char s_Buf[32];
    std::snprintf(s_Buf, sizeof(s_Buf), "%.4g", v);
    return s_Buf;
}

} // namespace

double StockRarityValue(int32_t rarity, bool hasRarity) {
    return hasRarity && rarity >= 1 && rarity <= 4 ? CrateRarityValue(rarity) : kNoRarityValue;
}

bool StockEligible(const StockFlags& flags) {
    return flags.unownedTrue > 0 && flags.unownedFalse == 0 && flags.entitled == true;
}

SupplierSteerPlan PlanSupplierSteer(std::span<const SupplierSteerOption> options) {
    SupplierSteerPlan s_Plan;
    s_Plan.fB.reserve(options.size());
    s_Plan.target.assign(options.size(), -1.0);

    for (const SupplierSteerOption& s_Option : options) {
        s_Plan.fB.push_back(s_Option.fB);

        if (s_Option.eligible && Usable(s_Option.fA) && Usable(s_Option.fB)) {
            ++s_Plan.eligible;
            s_Plan.weight = std::max(s_Plan.weight, s_Option.fA / s_Option.fB);
        }
    }

    if (s_Plan.eligible < 2)
        return s_Plan;

    for (size_t i = 0; i < options.size(); ++i) {
        const SupplierSteerOption& s_Option = options[i];

        if (!s_Option.eligible || !Usable(s_Option.fA) || !Usable(s_Option.fB))
            continue;

        // Never above the game's own m_fB: the heaviest gun keeps its value.
        const double s_New = std::min(s_Option.fB, s_Option.fA / s_Plan.weight);
        s_Plan.lowered += s_New < s_Option.fB ? 1 : 0;
        s_Plan.fB[i] = s_New;
        s_Plan.target[i] = s_Plan.weight;
    }

    return s_Plan;
}

bool IsGearSteerOption(const GearCandidate& option) {
    return !option.inSelector && !option.firearm && option.hasWeightMath;
}

bool GearRestoreClock::Steered(double at) {
    m_Steered = true;
    return m_VisitedEarly && Earlier(at);
}

bool GearRestoreClock::Visit(double at) {
    if (!m_Steered && std::isfinite(at))
        m_VisitedEarly = true;

    return Earlier(at + kGearRestoreAfterVisit);
}

bool GearRestoreClock::Earlier(double at) {
    if (!m_Steered || !std::isfinite(at) || (m_At && *m_At <= at))
        return false;

    m_At = at;
    return true;
}

bool SteeredWeightHeld(double actual, double target) {
    if (std::isnan(actual) || std::isnan(target))
        return false;

    return std::fabs(actual - target) <= kHeldTolerance * std::max(1.0, std::fabs(target));
}

std::string SteeredWeightsText(std::span<const SteeredWeight> weights, size_t maxNames) {
    size_t s_Held = 0;
    std::string s_Names;

    for (size_t i = 0; i < weights.size(); ++i) {
        const bool s_Ok = SteeredWeightHeld(weights[i].actual, weights[i].target);
        s_Held += s_Ok ? 1 : 0;

        if (i < maxNames)
            s_Names += (s_Names.empty() ? "" : ", ") + weights[i].name + " " + Num(weights[i].actual) +
                       (s_Ok ? "" : " MISSED (steered " + Num(weights[i].target) + ")");
    }

    if (weights.size() > maxNames)
        s_Names += ", +" + std::to_string(weights.size() - maxNames) + " more";

    return std::to_string(s_Held) + " of " + std::to_string(weights.size()) + " held" +
           (s_Names.empty() ? std::string() : " (" + s_Names + ")");
}

} // namespace fr
