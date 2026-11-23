#include "CrateGunMath.h"

#include "Rng.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fr {

namespace {

constexpr std::array<double, 4> kRarityValues {70.0, 20.0, 9.0, 1.0};
constexpr double kDefaultCost = 100.0;
constexpr double kGunsOnlyTotal = 100.0;
constexpr double kMaxMult = 1000.0;

double Clamp01(double v, double fallback) {
    if (std::isnan(v))
        return fallback;

    return std::clamp(v, 0.0, 1.0);
}

double CostOf(double cost) {
    return std::isfinite(cost) && cost > 0.0 ? cost : kDefaultCost;
}

std::string Num(double v) {
    char s_Buf[32];
    std::snprintf(s_Buf, sizeof(s_Buf), "%g", v);
    return s_Buf;
}

} // namespace

CratePool CratePoolOfState(int32_t state) {
    switch (state) {
        case 0: return CratePool::None;
        case kCrateStateToolPool: return CratePool::Tool;
        case kCrateStateWeaponPool: return CratePool::Weapon;
        default: return CratePool::Other;
    }
}

size_t CrateRarityIndex(int32_t rarity) {
    return rarity >= 1 && rarity <= 4 ? static_cast<size_t>(rarity - 1) : 0;
}

const char* CrateRarityName(int32_t rarity) {
    switch (rarity) {
        case 1: return "common";
        case 2: return "rare";
        case 3: return "epic";
        case 4: return "legendary";
        default: return "no rarity";
    }
}

double CrateRarityValue(int32_t rarity) {
    return kRarityValues[CrateRarityIndex(rarity)];
}

CrateGunsConfig SanitizeCrateGunsConfig(CrateGunsConfig cfg) {
    const CrateGunsConfig s_Default;
    cfg.poolChance = Clamp01(cfg.poolChance, s_Default.poolChance);
    cfg.gunShare = Clamp01(cfg.gunShare, s_Default.gunShare);

    for (size_t i = 0; i < cfg.rarityMult.size(); ++i) {
        const double v = cfg.rarityMult[i];
        cfg.rarityMult[i] = std::isnan(v) || v < 0.0 ? s_Default.rarityMult[i] : std::min(v, kMaxMult);
    }

    return cfg;
}

std::string DescribeCrateGunsConfig(const CrateGunsConfig& cfg) {
    std::string s_Out = "gun chance " + Num(cfg.poolChance) + " per opened crate, gun share " + Num(cfg.gunShare) +
                        " of the weapon pool on a hit (vanilla 0), rarity x" + Num(cfg.rarityMult[0]) + "/x" +
                        Num(cfg.rarityMult[1]) + "/x" + Num(cfg.rarityMult[2]) + "/x" + Num(cfg.rarityMult[3]) +
                        " (value";

    for (int32_t r = 1; r <= 4; ++r) {
        s_Out += std::string(r == 1 ? " " : ", ") + CrateRarityName(r) + " " +
                 Num(CrateRarityValue(r) * cfg.rarityMult[CrateRarityIndex(r)]);
    }

    return s_Out + ")";
}

CrateBranchDraw DrawCrateBranch(int32_t seed, std::string_view scene, uint32_t openIndex, double poolChance) {
    CrateBranchDraw s_Draw;
    s_Draw.u = UniformFromHash(DialHash(seed, scene, kCrateBranchFeature, static_cast<uint64_t>(openIndex)));
    s_Draw.weaponPool = s_Draw.u < Clamp01(poolChance, 0.0);
    return s_Draw;
}

CrateBranchWeights CrateBranchWeightsFor(bool weaponPool) {
    return weaponPool ? CrateBranchWeights {0.0f, 1.0f}
                      : CrateBranchWeights {kVanillaJunkBranchWeight, kVanillaWeaponBranchWeight};
}

CrateHitVerdict JudgeCrateHit(bool hit, size_t firearms, size_t flagsRead, size_t eligible) {
    if (!hit)
        return CrateHitVerdict::Miss;

    if (firearms > 0 && flagsRead >= firearms && eligible == 0)
        return CrateHitVerdict::NoGunToOffer;

    return CrateHitVerdict::Steer;
}

const char* CrateHitVerdictText(CrateHitVerdict verdict) {
    switch (verdict) {
        case CrateHitVerdict::Miss: return "miss (vanilla)";
        case CrateHitVerdict::Steer: return "HIT (weapon pool, weighted firearms)";
        case CrateHitVerdict::NoGunToOffer:
            return "HIT, but you own every gun a crate can offer: left vanilla (a forced weapon pool would only "
                   "offer merces)";
    }

    return "?";
}

CratePool CratePoolTaken(const CrateOpenSeen& seen) {
    if (seen.weaponOnlyOffers > 0)
        return CratePool::Weapon;

    if (seen.offers > 0)
        return CratePool::Tool;

    if (seen.weaponStates > seen.toolStates)
        return CratePool::Weapon;

    return seen.toolStates > 0 ? CratePool::Tool : CratePool::None;
}

std::vector<double> CrateGunWeights(std::span<const CrateGun> guns, double otherWeight, const CrateGunsConfig& cfg) {
    std::vector<double> s_Out(guns.size(), 0.0);
    const double s_Share = Clamp01(cfg.gunShare, 0.0);

    if (guns.empty() || s_Share <= 0.0)
        return s_Out;

    double s_Sum = 0.0;

    for (size_t i = 0; i < guns.size(); ++i) {
        const size_t r = CrateRarityIndex(guns[i].rarity);
        const double s_Mult = std::isfinite(cfg.rarityMult[r]) && cfg.rarityMult[r] > 0.0 ? cfg.rarityMult[r] : 0.0;
        s_Out[i] = kRarityValues[r] * s_Mult / CostOf(guns[i].cost);
        s_Sum += s_Out[i];
    }

    // Every multiplier 0: the firearms share equally.
    if (s_Sum <= 0.0) {
        std::fill(s_Out.begin(), s_Out.end(), 1.0);
        s_Sum = static_cast<double>(guns.size());
    }

    const bool s_GunsOnly = s_Share >= 1.0 || !std::isfinite(otherWeight) || otherWeight <= 0.0;
    const double s_Total = s_GunsOnly ? kGunsOnlyTotal : s_Share / (1.0 - s_Share) * otherWeight;

    for (double& w : s_Out)
        w = w / s_Sum * s_Total;

    return s_Out;
}

CrateGunShares CrateGunSharesOf(std::span<const CrateGun> guns, std::span<const double> weights, double otherWeight) {
    CrateGunShares s_Out;
    double s_Guns = 0.0;
    const size_t n = std::min(guns.size(), weights.size());

    for (size_t i = 0; i < n; ++i) {
        const double w = std::max(weights[i], 0.0);
        s_Guns += w;
        s_Out.rarityShare[CrateRarityIndex(guns[i].rarity)] += w;
    }

    const double s_Other = std::isfinite(otherWeight) ? std::max(otherWeight, 0.0) : 0.0;

    if (s_Guns + s_Other > 0.0)
        s_Out.gunShare = s_Guns / (s_Guns + s_Other);

    for (double& s : s_Out.rarityShare)
        s = s_Guns > 0.0 ? s / s_Guns : 0.0;

    return s_Out;
}

double CrateMercesWeight(double cost) {
    return 100.0 / (std::isfinite(cost) && cost > 0.0 ? cost : 1.0);
}

} // namespace fr
