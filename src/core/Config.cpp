#include "Config.h"

#include "ParamNames.h"

#include <Glacier/Enums.h>

namespace fr {

namespace {

std::string_view Trim(std::string_view s) {
    constexpr std::string_view ws = " \t\r\n";
    const size_t first = s.find_first_not_of(ws);
    if (first == std::string_view::npos)
        return {};
    const size_t last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

} // namespace

std::set<int32_t> ParseOffList(std::string_view csv, std::vector<std::string>& unknownOut) {
    std::set<int32_t> result;
    while (true) {
        const size_t comma = csv.find(',');
        const std::string_view name = Trim(csv.substr(0, comma));
        if (!name.empty()) {
            if (const auto value = ParamFromName(name))
                result.insert(*value);
            else
                unknownOut.emplace_back(name);
        }
        if (comma == std::string_view::npos)
            break;
        csv.remove_prefix(comma + 1);
    }
    return result;
}

Mode ChooseMode(const Config& cfg, bool isMission) {
    if (!isMission)
        return Mode::NotMission;
    return cfg.dials ? Mode::Dials : Mode::DialsOff;
}

std::array<int32_t, 3> CheckedParams(Mode) {
    constexpr auto P = [](EDifficultyParameter param) { return static_cast<int32_t>(param); };
    return {P(EDifficultyParameter::EDP_AIAttentionGainMultiplier), P(EDifficultyParameter::EDP_HearRangeFootstepRun),
            P(EDifficultyParameter::EDP_HearRangeGunShot)};
}

const char* ModeName(Mode mode) {
    switch (mode) {
        case Mode::NotMission: return "No changes (not a Freelancer mission)";
        case Mode::DialsOff: return "AI values off";
        case Mode::Dials: return "Random AI values";
    }
    return "unknown";
}

bool ModeWrites(Mode mode) {
    return mode == Mode::Dials;
}

std::optional<double> ModeFactor(Mode mode, const Config& cfg, int32_t seed, std::string_view scene, int32_t param,
                                 double base) {
    if (mode != Mode::Dials)
        return std::nullopt;

    // Footstep gaits roll with footsteps' own narrow roll (Footsteps.h).
    const RollConfig& roll = FootstepGaitIndex(param) >= 0 ? SanitizeFootstepConfig(cfg.footsteps).roll : cfg.roll;
    const DialDecision decision = DecideDial(seed, scene, param, static_cast<float>(base), roll, cfg.off);
    return decision.apply ? std::optional<double>(decision.multiplier) : std::nullopt;
}

} // namespace fr
