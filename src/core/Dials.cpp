#include "Dials.h"

#include "Rng.h"

#include <Glacier/Enums.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace fr {

namespace {

#define FR_DIAL(name, dir)                                                                                             \
    DialSpec {                                                                                                         \
        static_cast<int32_t>(EDifficultyParameter::name), dir                                                          \
    }

// The dial table, wildcards expanded by hand.
constexpr DialSpec kDials[] = {
    // Hearing: all EDP_HearRange* (37), larger is harder.
    FR_DIAL(EDP_HearRangeBulletImpact, +1),
    FR_DIAL(EDP_HearRangeBulletImpactOnBody, +1),
    FR_DIAL(EDP_HearRangeCloseCombatBreakNeck, +1),
    FR_DIAL(EDP_HearRangeCloseCombatChoke, +1),
    FR_DIAL(EDP_HearRangeCloseCombatFiberWire, +1),
    FR_DIAL(EDP_HearRangeCloseCombatHitFromBehind, +1),
    FR_DIAL(EDP_HearRangeCloseCombatL1, +1),
    FR_DIAL(EDP_HearRangeCloseCombatL2, +1),
    FR_DIAL(EDP_HearRangeCloseCombatL3, +1),
    FR_DIAL(EDP_HearRangeEventAccident, +1),
    FR_DIAL(EDP_HearRangeEventAlarm, +1),
    FR_DIAL(EDP_HearRangeEventExplosion, +1),
    FR_DIAL(EDP_HearRangeEventRadio, +1),
    FR_DIAL(EDP_HearRangeEventStrange, +1),
    FR_DIAL(EDP_HearRangeEventThrownItem, +1),
    FR_DIAL(EDP_HearRangeFootstepWalkSlow, +1),
    FR_DIAL(EDP_HearRangeFootstepWalk, +1),
    FR_DIAL(EDP_HearRangeFootstepRun, +1),
    FR_DIAL(EDP_HearRangeFootstepCrouchWalkSlow, +1),
    FR_DIAL(EDP_HearRangeFootstepCrouchWalk, +1),
    FR_DIAL(EDP_HearRangeFootstepCrouchRun, +1),
    FR_DIAL(EDP_HearRangeFootstepVegetationLVAWalkSlow, +1),
    FR_DIAL(EDP_HearRangeFootstepVegetationLVAWalk, +1),
    FR_DIAL(EDP_HearRangeFootstepVegetationLVARun, +1),
    FR_DIAL(EDP_HearRangeFootstepVegetationLVACrouchWalkSlow, +1),
    FR_DIAL(EDP_HearRangeFootstepVegetationLVACrouchWalk, +1),
    FR_DIAL(EDP_HearRangeFootstepVegetationLVACrouchRun, +1),
    FR_DIAL(EDP_HearRangeGunEmpty, +1),
    FR_DIAL(EDP_HearRangeGunShot, +1),
    FR_DIAL(EDP_HearRangeGunSilenced, +1),
    FR_DIAL(EDP_HearRangeGunSuperSilenced, +1),
    FR_DIAL(EDP_HearRangeSpeakAngry, +1),
    FR_DIAL(EDP_HearRangeSpeakHelp, +1),
    FR_DIAL(EDP_HearRangeSpeakHelpLoud, +1),
    FR_DIAL(EDP_HearRangeSpeakPain, +1),
    FR_DIAL(EDP_HearRangeSpeakPainLow, +1),
    FR_DIAL(EDP_HearRangeSpeakWarning, +1),

    // Attention.
    FR_DIAL(EDP_AIAttentionGainMultiplier, +1),
    FR_DIAL(EDP_AIIllegalActionAttentionModifier, +1),
    FR_DIAL(EDP_AIRunningAttentionModifier, +1),
    FR_DIAL(EDP_AISuspiciousMovementDisguiseAttentionModifier, +1),

    // Thresholds.
    FR_DIAL(EDP_MinAttentionToForceLookAt, -1),
    FR_DIAL(EDP_MinAttentionToForceLookAtIfIncreasing, -1),
    FR_DIAL(EDP_SightReactionMinAttention, -1),
    FR_DIAL(EDP_SightInvestigationMinAttention, -1),

    // Search.
    FR_DIAL(EDP_ManhuntGroupMaxSearchTime, +1),
    FR_DIAL(EDP_ManhuntGroupSearchAreaHackMaxDistance, +1),
    FR_DIAL(EDP_ManhuntGroupSearchAreaRadius, +1),
    FR_DIAL(EDP_AISuspiciousLostTargetTime, +1),
    FR_DIAL(EDP_ArrestTargetLostTime, +1),
    FR_DIAL(EDP_AIInvestigateCautious_Disturbance_Radius, +1),
    FR_DIAL(EDP_AIInvestigateCautious_Search_ClearDisturbanceBackRadius, +1),
    FR_DIAL(EDP_AIInvestigateCautious_Search_ClearDisturbanceFrontRadius, +1),
    FR_DIAL(EDP_AIInvestigateDisguiseMembersFollowDistance, +1),

    // Warnings.
    FR_DIAL(EDP_AIDisguise_FollowWarningsDelay, -1),
    FR_DIAL(EDP_AISentry_WarningDelay, -1),
    FR_DIAL(EDP_AIEscortOut_MaxTimeToHonorWarning, -1),
    FR_DIAL(EDP_AIEscortOut_MaxForbiddenMovement, -1),

    // Combat timing.
    FR_DIAL(EDP_AICombat_ShootingAccuracyInitialBuildUp_MinimumDuration, -1),
    FR_DIAL(EDP_AICombat_ShootingAccuracyInitialBuildUp_MaximumDuration, -1),
    FR_DIAL(EDP_AICombat_FlashGrenadeThrow_GlobalCooldown, -1),
    FR_DIAL(EDP_AICombat_FlashGrenadeThrow_AggressionTriggerCooldown, -1),
    FR_DIAL(EDP_AICombat_FlashGrenadeThrow_TargetCampingTriggerCooldown, -1),
    FR_DIAL(EDP_AICombat_FlashGrenadeThrow_LargeRelocationTriggerCooldown, -1),

    // Combat other: EDP_AICombat_ShootingAccuracyResetPeriod_* (4) and distances.
    FR_DIAL(EDP_AICombat_ShootingAccuracyResetPeriod_Minimum, 0),
    FR_DIAL(EDP_AICombat_ShootingAccuracyResetPeriod_Maximum_FewCombatants, 0),
    FR_DIAL(EDP_AICombat_ShootingAccuracyResetPeriod_Maximum_MediumCombatants, 0),
    FR_DIAL(EDP_AICombat_ShootingAccuracyResetPeriod_Maximum_ManyCombatants, 0),
    FR_DIAL(EDP_AICombat_ShootingAccuracy_MinimumDistance, 0),
    FR_DIAL(EDP_AIEscortOut_EscortEscalateDistance, 0),
    FR_DIAL(EDP_AIEscortOut_EscortHardWarningDistance, 0),

    // Damage: EDP_DamageMultiplier*_NPC (9) and related.
    FR_DIAL(EDP_HitmanDamageReceivedMultiplier, +1),
    FR_DIAL(EDP_DamageMultiplierHandgun_NPC, +1),
    FR_DIAL(EDP_DamageMultiplierSlowgun_NPC, +1),
    FR_DIAL(EDP_DamageMultiplierAssaultRifle_NPC, +1),
    FR_DIAL(EDP_DamageMultiplierSMG_NPC, +1),
    FR_DIAL(EDP_DamageMultiplierSniper_NPC, +1),
    FR_DIAL(EDP_DamageMultiplierRPG_NPC, +1),
    FR_DIAL(EDP_DamageMultiplierKnife_NPC, +1),
    FR_DIAL(EDP_DamageMultiplierShotgun_NPC, +1),
    FR_DIAL(EDP_DamageMultiplierSpotter_NPC, +1),
    FR_DIAL(EDP_SpotHitmanShootingDistance, +1),
    FR_DIAL(EDP_MaxCoverShootActorCountMultiplier, +1),

    // Damage (player side).
    FR_DIAL(EDP_NPCDamageReceivedMultiplier, -1),

    // Player health.
    FR_DIAL(EDP_PlayerHitPointsMax, -1),
    FR_DIAL(EDP_PlayerHitPointsRegenerationPerSecond, -1),
    FR_DIAL(EDP_PlayerHitPointsRegenerationDelay, +1),

    // Instinct.
    FR_DIAL(EDP_InstinctModeBurnRate, +1),
    FR_DIAL(EDP_InstinctRegenRate, -1),
    FR_DIAL(EDP_InstinctRegenCap, -1),

    // Social: all EDP_Social* (16), symmetric.
    FR_DIAL(EDP_SocialGreetingAngle, 0),
    FR_DIAL(EDP_SocialGreetingCooldown, 0),
    FR_DIAL(EDP_SocialGreetingCooldownAlertLevel, 0),
    FR_DIAL(EDP_SocialGreetingCooldownLastSpeak, 0),
    FR_DIAL(EDP_SocialGreetingCooldownPlayer, 0),
    FR_DIAL(EDP_SocialGreetingCooldownSequence, 0),
    FR_DIAL(EDP_SocialGreetingDistance, 0),
    FR_DIAL(EDP_SocialLookAtTimeMax, 0),
    FR_DIAL(EDP_SocialLookAtTimeMin, 0),
    FR_DIAL(EDP_SocialSillyCooldown, 0),
    FR_DIAL(EDP_SocialSillyDistance, 0),
    FR_DIAL(EDP_SocialSillyTriggerTime, 0),
    FR_DIAL(EDP_SocialStandingCooldown, 0),
    FR_DIAL(EDP_SocialStandingDistance, 0),
    FR_DIAL(EDP_SocialStandingDistanceImmediate, 0),
    FR_DIAL(EDP_SocialStandingTriggerTime, 0),
};

#undef FR_DIAL

} // namespace

const DialSpec* FindDial(int32_t param) {
    for (const auto& dial : kDials) {
        if (dial.param == param)
            return &dial;
    }
    return nullptr;
}

RollConfig SanitizeRollConfig(RollConfig cfg, const RollConfig& def) {
    if (!std::isfinite(cfg.sigma) || cfg.sigma < 0.0)
        cfg.sigma = def.sigma;
    if (!std::isfinite(cfg.lean))
        cfg.lean = def.lean;
    if (!std::isfinite(cfg.clampMin) || !std::isfinite(cfg.clampMax) || cfg.clampMin <= 0.0 || cfg.clampMax <= 0.0 ||
        cfg.clampMin > cfg.clampMax) {
        cfg.clampMin = def.clampMin;
        cfg.clampMax = def.clampMax;
    }
    return cfg;
}

RollResult RollMultiplier(int32_t seed, std::string_view scene, int32_t param, int8_t dir, const RollConfig& cfg) {
    return RollFromHash(DialHash(seed, scene, "ai", param), dir, cfg);
}

RollResult RollFromHash(uint64_t first, int8_t dir, const RollConfig& cfg) {
    for (int k = 0; k < kMaxRollAttempts; ++k) {
        uint64_t h = first;
        if (k > 0) {
            uint64_t st = first ^ (static_cast<uint64_t>(k) * 0x9E3779B97F4A7C15ull);
            h = SplitMix64(st);
        }
        const double m = std::exp(cfg.sigma * NormalFromHash(h) + dir * cfg.lean * cfg.sigma);
        // Bounds apply to the multiplier, not the value, so a negative base
        // never inverts them. A NaN draw is never in range.
        if (m >= cfg.clampMin && m <= cfg.clampMax)
            return {m, k + 1, false};
    }
    return {1.0, kMaxRollAttempts, true};
}

std::string RollNote(const RollResult& roll) {
    if (roll.fellBack)
        return "no valid roll, kept default";
    if (roll.attempts > 1)
        return "re-rolled " + std::to_string(roll.attempts - 1) + (roll.attempts == 2 ? " time" : " times");
    return {};
}

DialDecision DecideDial(int32_t seed, std::string_view scene, int32_t param, float base, const RollConfig& cfg,
                        const std::set<int32_t>& off) {
    const DialSpec* dial = FindDial(param);
    if (!dial)
        return {false, SkipReason::NotInTable, 1.0, base, 0, false};
    if (off.contains(param))
        return {false, SkipReason::Off, 1.0, base, 0, false};
    if (base == 0.f)
        return {false, SkipReason::ZeroBase, 1.0, base, 0, false};

    const RollResult roll = RollMultiplier(seed, scene, param, dial->dir, cfg);
    return {true,          SkipReason::None, roll.multiplier, static_cast<float>(base * roll.multiplier),
            roll.attempts, roll.fellBack};
}

DialBands BandsFor(const RollConfig& cfg) {
    const RollConfig safe = SanitizeRollConfig(cfg);
    // No room on a side (clamp at or past x1): the bands sit at x1, so any change there is extreme.
    const double hi = std::max(std::log(safe.clampMax), 0.0);
    const double lo = std::min(std::log(safe.clampMin), 0.0);
    return {std::exp(kNotableReach * hi), std::exp(kNotableReach * lo), std::exp(kExtremeReach * hi),
            std::exp(kExtremeReach * lo)};
}

DialRating RateMultiplier(double multiplier, int8_t dir, const RollConfig& cfg) {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    if (!std::isfinite(multiplier) || multiplier <= 0.0)
        return {DialLevel::Extreme, DialSide::Neutral, kInf};
    if (multiplier == 1.0)
        return {DialLevel::Normal, DialSide::Neutral, 0.0};

    const RollConfig safe = SanitizeRollConfig(cfg);
    const DialBands bands = BandsFor(safe);
    const bool up = multiplier > 1.0;

    DialLevel level = DialLevel::Normal;
    if (up ? multiplier >= bands.extremeHigh : multiplier <= bands.extremeLow)
        level = DialLevel::Extreme;
    else if (up ? multiplier >= bands.notableHigh : multiplier <= bands.notableLow)
        level = DialLevel::Notable;

    DialSide side = DialSide::Neutral;
    if (dir != 0)
        side = up == (dir > 0) ? DialSide::Harder : DialSide::Easier;

    const double room = up ? std::log(safe.clampMax) : -std::log(safe.clampMin);
    const double reach = room > 0.0 ? std::abs(std::log(multiplier)) / room : kInf;
    return {level, side, reach};
}

DialRating RateDial(int32_t param, double multiplier, const RollConfig& cfg) {
    const DialSpec* dial = FindDial(param);
    return RateMultiplier(multiplier, dial ? dial->dir : 0, cfg);
}

std::string DialMarker(const DialRating& rating) {
    if (rating.level == DialLevel::Normal)
        return {};
    std::string marker = rating.level == DialLevel::Extreme ? "!!" : "!";
    switch (rating.side) {
        case DialSide::Neutral: break;
        case DialSide::Harder: marker += " harder"; break;
        case DialSide::Easier: marker += " easier"; break;
    }
    return marker;
}

bool WilderFirst(const DialRating& a, const DialRating& b) {
    const auto sideRank = [](DialSide side) {
        switch (side) {
            case DialSide::Harder: return 0;
            case DialSide::Neutral: return 1;
            case DialSide::Easier: return 2;
        }
        return 1;
    };
    if (a.level != b.level)
        return a.level > b.level;
    if (a.side != b.side)
        return sideRank(a.side) < sideRank(b.side);
    return a.reach > b.reach;
}

} // namespace fr
