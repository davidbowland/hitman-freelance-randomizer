#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace fr {

// One rollable difficulty parameter and its harder direction:
// +1 = larger is harder, -1 = smaller is harder, 0 = symmetric variety.
struct DialSpec {
    int32_t param;
    int8_t dir;
};

const DialSpec* FindDial(int32_t param);

struct RollConfig {
    double sigma = 0.35;   // spread in log space
    double lean = 0.5;     // harder skew, in standard deviations
    double clampMin = 0.4; // multiplier bounds (relative to base)
    double clampMax = 3.0;
};

// Returns cfg with invalid fields replaced by def's: sigma (non-finite or
// negative) and lean (non-finite) each fall back alone; the clamps fall back as
// a pair if either is non-finite or <= 0, or clampMin > clampMax. The mod
// passes the settings' defaults (fr::Config) as def.
RollConfig SanitizeRollConfig(RollConfig cfg, const RollConfig& def = {});

// Draws per roll before giving up and keeping the default (x1).
constexpr int kMaxRollAttempts = 8;

struct RollResult {
    double multiplier; // within [clampMin, clampMax], or exactly 1.0 when fellBack
    int attempts;      // draws used: 1 = first draw accepted; kMaxRollAttempts when fellBack
    bool fellBack;     // no draw was in range; the loaded value is kept
};

// Multiplier for (seed, scene, param). Draws until one lands in
// [clampMin, clampMax] (out-of-range draws are re-rolled, not clamped), up to
// kMaxRollAttempts, then falls back to 1.0.
// The scene's case does not matter.
RollResult RollMultiplier(int32_t seed, std::string_view scene, int32_t param, int8_t dir, const RollConfig& cfg);

// The same draws from a given first-draw hash, for dials keyed outside the
// EDifficultyParameter space (the sensor dials hash their own feature name).
RollResult RollFromHash(uint64_t first, int8_t dir, const RollConfig& cfg);

// Log/overlay note for a roll: empty, "re-rolled N time(s)" (N = attempts - 1; ASCII so the overlay font can always draw it), or "no valid roll, kept default".
std::string RollNote(const RollResult& roll);

enum class SkipReason { None, NotInTable, Off, ZeroBase };

struct DialDecision {
    bool apply;
    SkipReason reason;
    double multiplier;
    float value;
    int attempts = 0;      // see RollResult; 0 when skipped
    bool fellBack = false; // see RollResult
};

DialDecision DecideDial(int32_t seed, std::string_view scene, int32_t param, float base, const RollConfig& cfg,
                        const std::set<int32_t>& off);

// Overlay rating of a written multiplier (relative to the loaded value),
// measured in log space as the fraction of the way from x1 to the clamp on
// that side: ln(m) / ln(clampMax) above x1, ln(m) / ln(clampMin) below.
constexpr double kNotableReach = 0.25; // at or beyond: notable (x1.32 / x0.80 with clamps 0.4..3.0)
constexpr double kExtremeReach = 0.75; // at or beyond: extreme (x2.28 / x0.50 with clamps 0.4..3.0)

// Multiplier thresholds for a roll config's (sanitized) clamps: at or above a
// High, or at or below a Low, rates that level. A clamp of x1 or nearer makes
// every change on that side extreme.
struct DialBands {
    double notableHigh;
    double notableLow;
    double extremeHigh;
    double extremeLow;
};
DialBands BandsFor(const RollConfig& cfg);

enum class DialLevel { Normal, Notable, Extreme };
// Which way the change pushes the mission; Neutral for symmetric dials (dir 0),
// params not in the table, and exactly x1.
enum class DialSide { Neutral, Harder, Easier };

struct DialRating {
    DialLevel level;
    DialSide side;
    double reach; // ln(m) over the log of the clamp on that side, >= 0; infinity when there is no room
                  // on that side or the multiplier is non-finite or non-positive
};

// A non-finite or non-positive multiplier rates Extreme and Neutral. Exactly
// x1 is Normal whatever the clamps.
DialRating RateMultiplier(double multiplier, int8_t dir, const RollConfig& cfg);
// RateMultiplier with the table's dir for param (0 when not in the table).
DialRating RateDial(int32_t param, double multiplier, const RollConfig& cfg);
// "", "!" or "!!", then " harder" or " easier" when the side is known.
std::string DialMarker(const DialRating& rating);
// Strict ordering for "Extreme first": higher level first, then harder before
// neutral before easier, then larger reach.
bool WilderFirst(const DialRating& a, const DialRating& b);

} // namespace fr
