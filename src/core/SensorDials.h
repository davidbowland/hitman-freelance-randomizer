#pragma once

// Sensor dials: the guards' senses as the professional difficulty's shared
// sensor (ZSharedSensorDef, the m_rSharedSensor of the professional
// collection "Default" (eDL_NORMAL), sub-entity 6b6889480b488495 in the
// globaldata brick 00097AA87B144150) holds them. These are the values Death
// Awaits patches for its Freelancer attentiveness (DAWMOD4.entity.patch.json):
// the four sight thresholds that have no professional pro.repo entry, how fast
// attention decays, the run and crowd penalties, how long a suspicious
// enforcer follows, and the focus-cone range of each perception state
// (m_VisibilitySetting[].m_fFocusConeRange, one element per m_eSensitivity).
//
// Each dial is rolled per mission like the AI dials (same RollConfig: sigma,
// lean toward harder, clamps, re-rolls) around the vanilla professional value
// (measured in game), from the campaign seed, the
// scene and the dial's key (feature "sensor", so the AI dials' rolls never
// move). The attention thresholds also never go above kMaxAttentionThreshold:
// at 1 or more a guard would never react at all.
//
// UNPROVEN in game: the values are written into the live sensor entity while
// the scene loads; whether actors read them after that (rather than having
// copied them already, or reading a cooked copy) is not known.

#include "DialInfo.h"
#include "Dials.h"

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// The written value of an attention threshold stays at or below this.
constexpr double kMaxAttentionThreshold = 0.9;

struct SensorDial {
    std::string_view key;         // ini name (sensor_dials_off), e.g. "sight_reaction"
    std::string_view property;    // the ZSharedSensorDef float, or "m_fFocusConeRange" of a visibility element
    std::string_view sensitivity; // the m_VisibilitySetting element's m_eSensitivity; empty for a plain float
    double vanilla;               // the professional sensor's value
    double maxValue;              // the written value never exceeds this; 0: no limit
    int8_t dir;                   // as DialSpec: +1 larger is harder, -1 smaller is harder
    DialInfo info;                // info.param is kSensorDialParamBase + table index (no EDifficultyParameter)
};

// Pseudo parameter numbers of the sensor dials' DialInfo (outside EDifficultyParameter).
constexpr int32_t kSensorDialParamBase = 0x10000;

std::span<const SensorDial> SensorDialTable();
const SensorDial* FindSensorDial(std::string_view key);

// The difficulty parameter a dial stands in for: the four sight thresholds
// have no professional pro.repo entry, so the AI dials' roll of them changes
// nothing in Freelancer, and this dial is what reaches the professional
// sensor. Empty for the other dials.
std::optional<int32_t> SensorDialParam(const SensorDial& dial);

// "m_fSightReactionMinAttention" or "m_VisibilitySetting[APS_AMBIENT].m_fFocusConeRange".
std::string SensorDialLabel(const SensorDial& dial);

// Parses a comma-separated list of sensor dial keys (whitespace-trimmed,
// empty entries ignored); unknown keys are appended to unknownOut and skipped.
std::set<std::string> ParseSensorOffList(std::string_view csv, std::vector<std::string>& unknownOut);

struct SensorDecision {
    bool apply;        // false: in the off list (left alone)
    double base;       // the vanilla value rolled from
    double multiplier; // 1.0 when not applied
    float value;       // to write
    int attempts = 0;  // see RollResult
    bool fellBack = false;
};

// The roll config a dial is rolled with: cfg (sanitized) with clampMax
// lowered so base * multiplier stays at or below the dial's maxValue (and
// clampMin lowered with it if needed), then narrowed to the dial's own
// multiplier bounds where it has them (wary and alerted sight range, attention
// decay time, crowd distance factor).
RollConfig SensorRollConfig(const SensorDial& dial, const RollConfig& cfg);

// The dial's value for this mission: vanilla times a multiplier drawn as
// RollMultiplier does, from DialHash(seed, scene, "sensor", Fnv1a64(key)).
SensorDecision DecideSensorDial(int32_t seed, std::string_view scene, const SensorDial& dial, const RollConfig& cfg,
                                const std::set<std::string>& off);

} // namespace fr
