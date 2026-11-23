#include "SensorDials.h"

#include "Rng.h"

#include <Glacier/Enums.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace fr {

namespace {

constexpr Basis N = Basis::Named;
constexpr Basis I = Basis::Inferred;
constexpr std::string_view kNone = "";
constexpr std::string_view kCone = "m_fFocusConeRange";

#define FR_SENSOR(index, key, property, sensitivity, vanilla, maxValue, dir, name, unit, unitBasis, description)       \
    SensorDial {                                                                                                       \
        key, property, sensitivity, vanilla, maxValue, dir, DialInfo {                                                 \
            kSensorDialParamBase + index, name, DialUnit::unit, unitBasis, description, I, kNone                       \
        }                                                                                                              \
    }

// Vanilla values: the professional sensor as measured in game (every float
// listed there). Directions: Death Awaits' harder settings move every one of
// these the way marked (thresholds and the decay time down, the rest up),
// except the decay time, whose meaning is unclear, so it is rolled for variety
// only. Keys are part of the roll: never rename one.
constexpr SensorDial kSensors[] = {
    FR_SENSOR(0, "force_look_at", "m_fMinAttentionToForceLookAt", kNone, 0.25, kMaxAttentionThreshold, -1,
              "Sensor: attention to look at 47", Attention, I,
              "How much attention a guard needs before it turns to look at 47; lower means sooner."),
    FR_SENSOR(1, "force_look_at_rising", "m_fMinAttentionToForceLookAtIfIncreasing", kNone, 0.25,
              kMaxAttentionThreshold, -1, "Sensor: attention to look at 47 (rising)", Attention, I,
              "The same look-at threshold while the guard's attention is still rising; lower means sooner."),
    FR_SENSOR(2, "sight_reaction", "m_fSightReactionMinAttention", kNone, 0.35, kMaxAttentionThreshold, -1,
              "Sensor: attention to react to sight", Attention, I,
              "How much attention a guard needs before it reacts to what it sees of 47; lower means sooner."),
    FR_SENSOR(3, "sight_investigation", "m_fSightInvestigationMinAttention", kNone, 0.5, kMaxAttentionThreshold, -1,
              "Sensor: attention to investigate", Attention, I,
              "How much attention a guard needs before it goes to investigate what it saw; lower means sooner."),
    FR_SENSOR(4, "attention_decrease_time", "m_fAttentionDecreaseTime", kNone, 0.5, 0.0, 0,
              "Sensor: attention decay time", Seconds, N,
              "Sets how a guard's attention fades once 47 is out of sight (Death Awaits halves it)."),
    FR_SENSOR(5, "trespassing_run_penalty", "m_fTrespassingAttractionRunPenalty", kNone, 1.5, 0.0, +1,
              "Sensor: running while trespassing", Multiplier, I,
              "How much more attention 47 draws by running in a restricted area."),
    FR_SENSOR(6, "hunted_run_penalty", "m_fHuntedAttractionRunPenalty", kNone, 1.5, 0.0, +1,
              "Sensor: running while hunted", Multiplier, I,
              "How much more attention 47 draws by running while guards are hunting him."),
    FR_SENSOR(7, "disguise_follow_time", "m_fDisguiseFollowTime", kNone, 15.0, 0.0, +1, "Sensor: enforcer follow time",
              Seconds, N, "How long a guard who doubts 47's disguise keeps following him."),
    FR_SENSOR(8, "crowd_distance_factor", "m_fDisguiseAttractionDistanceInCrowdFactor", kNone, 1.0, 0.0, +1,
              "Sensor: crowd distance factor", Multiplier, I,
              "Scales the distance at which enforcers notice 47's disguise while he is in a crowd."),
    FR_SENSOR(9, "crowd_time_factor", "m_fDisguiseAttractionTimeInCrowdFactor", kNone, 1.0, 0.0, +1,
              "Sensor: crowd time factor", Multiplier, I,
              "Scales how quickly enforcers see through 47's disguise while he is in a crowd."),
    FR_SENSOR(10, "cone_ambient", kCone, "APS_AMBIENT", 25.0, 0.0, +1, "Sensor: sight range, calm", Meters, N,
              "How far a calm guard sees 47 in its focus cone."),
    FR_SENSOR(11, "cone_alert_low", kCone, "APS_ALERT_LOW", 25.0, 0.0, +1, "Sensor: sight range, wary", Meters, N,
              "How far a slightly alerted guard sees 47 in its focus cone."),
    FR_SENSOR(12, "cone_alert", kCone, "APS_ALERT", 35.0, 0.0, +1, "Sensor: sight range, alerted", Meters, N,
              "How far an alerted (searching) guard sees 47 in its focus cone."),
    FR_SENSOR(13, "cone_combat", kCone, "APS_COMBAT", 50.0, 0.0, +1, "Sensor: sight range, combat", Meters, N,
              "How far a guard in combat sees 47 in its focus cone."),
    FR_SENSOR(14, "cone_spider_sense", kCone, "APS_SPIDER_SENSE", 30.0, 0.0, +1, "Sensor: sight range, spider sense",
              Meters, N,
              "How far a guard sees 47 in a heightened state the game calls spider sense; what sets it off is not "
              "known (Death Awaits triples it)."),
    FR_SENSOR(15, "cone_preoccupied", kCone, "APS_AMBIENT_PREOCCUPIED", 6.0, 0.0, +1,
              "Sensor: sight range, preoccupied", Meters, N,
              "How far a busy guard (on the phone, working) sees 47 in its focus cone."),
};

#undef FR_SENSOR

// Per-dial multiplier bounds, inside the shared clamps (a 2.5x wary and 2.8x
// alerted sight range let a guard see through 47 in 2-3 s): either way of
// vanilla, so a mission can as well have slow guards.
struct SensorBounds {
    std::string_view key;
    double minFactor;
    double maxFactor;
};

constexpr SensorBounds kSensorBounds[] = {
    {"attention_decrease_time", 0.5, 2.0},
    {"crowd_distance_factor", 0.5, 1.5},
    {"cone_alert_low", 0.5, 1.8},
    {"cone_alert", 0.7, 2.0},
};

const SensorBounds* BoundsOf(const SensorDial& dial) {
    for (const SensorBounds& bounds : kSensorBounds) {
        if (bounds.key == dial.key)
            return &bounds;
    }
    return nullptr;
}

std::string_view Trim(std::string_view s) {
    constexpr std::string_view ws = " \t\r\n";
    const size_t first = s.find_first_not_of(ws);
    if (first == std::string_view::npos)
        return {};
    const size_t last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

} // namespace

std::span<const SensorDial> SensorDialTable() {
    return kSensors;
}

const SensorDial* FindSensorDial(std::string_view key) {
    for (const auto& dial : kSensors) {
        if (dial.key == key)
            return &dial;
    }
    return nullptr;
}

std::optional<int32_t> SensorDialParam(const SensorDial& dial) {
    using enum EDifficultyParameter;
    constexpr std::pair<std::string_view, EDifficultyParameter> kStandsIn[] = {
        {"force_look_at", EDP_MinAttentionToForceLookAt},
        {"force_look_at_rising", EDP_MinAttentionToForceLookAtIfIncreasing},
        {"sight_reaction", EDP_SightReactionMinAttention},
        {"sight_investigation", EDP_SightInvestigationMinAttention},
    };

    for (const auto& [key, param] : kStandsIn) {
        if (dial.key == key)
            return static_cast<int32_t>(param);
    }
    return std::nullopt;
}

std::string SensorDialLabel(const SensorDial& dial) {
    if (dial.sensitivity.empty())
        return std::string(dial.property);
    return "m_VisibilitySetting[" + std::string(dial.sensitivity) + "]." + std::string(dial.property);
}

std::set<std::string> ParseSensorOffList(std::string_view csv, std::vector<std::string>& unknownOut) {
    std::set<std::string> result;
    while (true) {
        const size_t comma = csv.find(',');
        const std::string_view key = Trim(csv.substr(0, comma));
        if (!key.empty()) {
            if (FindSensorDial(key))
                result.emplace(key);
            else
                unknownOut.emplace_back(key);
        }
        if (comma == std::string_view::npos)
            break;
        csv.remove_prefix(comma + 1);
    }
    return result;
}

RollConfig SensorRollConfig(const SensorDial& dial, const RollConfig& cfg) {
    RollConfig out = SanitizeRollConfig(cfg);
    if (dial.maxValue > 0.0 && dial.vanilla > 0.0) {
        out.clampMax = std::min(out.clampMax, dial.maxValue / dial.vanilla);
        out.clampMin = std::min(out.clampMin, out.clampMax);
    }
    if (const SensorBounds* bounds = BoundsOf(dial)) {
        out.clampMin = std::max(out.clampMin, bounds->minFactor);
        out.clampMax = std::min(out.clampMax, bounds->maxFactor);
        out.clampMin = std::min(out.clampMin, out.clampMax);
    }
    return out;
}

SensorDecision DecideSensorDial(int32_t seed, std::string_view scene, const SensorDial& dial, const RollConfig& cfg,
                                const std::set<std::string>& off) {
    if (off.contains(std::string(dial.key)))
        return {false, dial.vanilla, 1.0, static_cast<float>(dial.vanilla), 0, false};

    const RollResult roll =
        RollFromHash(DialHash(seed, scene, "sensor", Fnv1a64(dial.key)), dial.dir, SensorRollConfig(dial, cfg));
    return {true,          dial.vanilla, roll.multiplier, static_cast<float>(dial.vanilla * roll.multiplier),
            roll.attempts, roll.fellBack};
}

} // namespace fr
