#include "Footsteps.h"

#include <Glacier/Enums.h>

#include <cmath>

namespace fr {

namespace {

#define FR_GAIT(name, key, on, range)                                                                                  \
    FootstepGait {                                                                                                     \
        static_cast<int32_t>(EDifficultyParameter::name), key, on, range                                               \
    }

// Ranges: Death Awaits' HR_FS_* entries. The crouched gaits are off by default.
constexpr FootstepGait kGaits[kFootstepGaits] = {
    FR_GAIT(EDP_HearRangeFootstepWalk, "walk", true, 10.0),
    FR_GAIT(EDP_HearRangeFootstepWalkSlow, "walk_slow", true, 6.0),
    FR_GAIT(EDP_HearRangeFootstepRun, "run", true, 18.0),
    FR_GAIT(EDP_HearRangeFootstepCrouchWalk, "crouch_walk", false, 4.0),
    FR_GAIT(EDP_HearRangeFootstepCrouchWalkSlow, "crouch_walk_slow", false, 2.0),
    FR_GAIT(EDP_HearRangeFootstepCrouchRun, "crouch_run", false, 8.0),
};

#undef FR_GAIT

} // namespace

std::span<const FootstepGait> FootstepGaits() {
    return kGaits;
}

int FootstepGaitIndex(int32_t param) {
    for (size_t i = 0; i < kFootstepGaits; ++i) {
        if (kGaits[i].param == param)
            return static_cast<int>(i);
    }
    return -1;
}

FootstepConfig DefaultFootstepConfig() {
    FootstepConfig cfg;
    for (size_t i = 0; i < kFootstepGaits; ++i) {
        cfg.enabled[i] = kGaits[i].defaultOn;
        cfg.range[i] = kGaits[i].defaultRange;
    }
    return cfg;
}

FootstepConfig SanitizeFootstepConfig(FootstepConfig cfg) {
    for (size_t i = 0; i < kFootstepGaits; ++i) {
        if (!std::isfinite(cfg.range[i]) || cfg.range[i] <= 0.0)
            cfg.range[i] = kGaits[i].defaultRange;
    }

    const RollConfig def = kDefaultFootstepRoll;
    RollConfig& roll = cfg.roll;
    if (!std::isfinite(roll.sigma) || roll.sigma < 0.0)
        roll.sigma = def.sigma;
    if (!std::isfinite(roll.lean))
        roll.lean = def.lean;
    if (!std::isfinite(roll.clampMin) || !std::isfinite(roll.clampMax) || roll.clampMin <= 0.0 ||
        roll.clampMax <= 0.0 || roll.clampMin > roll.clampMax) {
        roll.clampMin = def.clampMin;
        roll.clampMax = def.clampMax;
    }
    return cfg;
}

const char* FootstepSkipName(FootstepSkip skip) {
    switch (skip) {
        case FootstepSkip::None: return "written";
        case FootstepSkip::NotFootstep: return "not a footstep gait";
        case FootstepSkip::SwitchOff: return "footsteps = false";
        case FootstepSkip::GaitOff: return "gait off";
    }
    return "?";
}

FootstepDecision DecideFootstep(int32_t seed, std::string_view scene, int32_t param, const FootstepConfig& cfg,
                                const std::set<int32_t>& off) {
    const int index = FootstepGaitIndex(param);
    if (index < 0)
        return {FootstepSkip::NotFootstep};
    if (!cfg.on)
        return {FootstepSkip::SwitchOff};
    if (!cfg.enabled[index])
        return {FootstepSkip::GaitOff};

    const FootstepConfig safe = SanitizeFootstepConfig(cfg);
    const double base = safe.range[index];
    // The AI dial roll on the configured base (never 0, so never the zero-base skip), with footsteps' own roll.
    const DialDecision dial = DecideDial(seed, scene, param, static_cast<float>(base), safe.roll, off);

    if (!dial.apply)
        return {FootstepSkip::None, base, 1.0, base, false, 0, false};

    return {FootstepSkip::None, base, dial.multiplier, base * dial.multiplier, true, dial.attempts, dial.fellBack};
}

AlwaysEmitDecision DecideAlwaysEmit(const FootstepConfig& cfg) {
    if (!cfg.on)
        return {false, "footsteps = false"};

    for (const bool enabled : cfg.enabled) {
        if (enabled)
            return {true, ""};
    }
    return {false, "every gait off"};
}

} // namespace fr
