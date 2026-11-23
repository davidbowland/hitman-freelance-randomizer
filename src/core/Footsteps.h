#pragma once

// Footstep hearing. In vanilla pro.repo every professional footstep entry
// (EDP_HearRangeFootstep*: walk, slow walk, run and the three crouched gaits)
// has Value 0 and Toggle false, so guards never hear 47's steps in Freelancer
// and the AI dials, which roll the loaded Value, can never move them (0 times
// anything is 0). Death Awaits turns them on by setting Toggle true and a
// range (HR_FS_*.repository.json: walk 10, slow walk 6, run 18, crouch walk 4,
// slow crouch walk 2, crouch run 8 m, every difficulty).
//
// Here each gait has a switch and a base range (ini, defaults: Death Awaits'
// ranges; the crouched gaits off). An enabled gait's pro.repo entries (every
// difficulty, like the AI dials) get Toggle true and Value base x the dial
// roll (DecideDial on the base: same seed, scene and parameter as any dial,
// but footsteps' own narrow roll, FootstepConfig::roll: ini footsteps_sigma,
// footsteps_lean, footsteps_min, footsteps_max, defaults 0.15 / 0 / 0.7 / 1.4,
// so run stays 13 to 25 m instead of the general dial's 7 to 54 m); a
// parameter in the off list gets its base unrolled. A disabled gait, or the
// footsteps switch off, leaves the entries vanilla. Both Value and Toggle are
// restored when the scene is cleared.
//
// EDP_AIAudioPerceptionAlwaysEmitFootstepSoundEvents: one pro.repo entry
// (15879247-ac0e-48cb-a0ee-ae09d1fd2683), in no difficulty collection, Value 1
// and Toggle true in vanilla, and no difficulty entity is built from it in a
// Freelancer scene. By its name it makes 47's
// footsteps always emit sound events to the AI's audio perception, so it is
// already on; the mod makes sure of it while footsteps are on (Value 1,
// Toggle true; restored at scene clear).
//
// The vegetation variants (EDP_HearRangeFootstepVegetationLVA*) are not
// handled: they have no pro.repo entry and sit in no difficulty collection,
// and the readers return built-in values for them in vanilla Freelancer
// (measured in game: run 6, walk 3, crouch run 5, crouch walk 2, slow gaits 0),
// so vegetation footsteps are already audible and nothing here reaches them.

#include "Dials.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string_view>

namespace fr {

// One gait.
struct FootstepGait {
    int32_t param;        // EDP_HearRangeFootstep*
    std::string_view key; // ini: footsteps_<key> (switch) and footsteps_<key>_range
    bool defaultOn;
    double defaultRange; // meters (Death Awaits)
};

constexpr size_t kFootstepGaits = 6;

// Walk, slow walk, run, crouch walk, slow crouch walk, crouch run.
std::span<const FootstepGait> FootstepGaits();

// The gait's index in FootstepGaits(), or -1 (also for the vegetation variants).
int FootstepGaitIndex(int32_t param);

// Footsteps' own roll (ini footsteps_sigma, footsteps_lean, footsteps_min,
// footsteps_max): narrow and symmetric, a range near its base.
inline constexpr RollConfig kDefaultFootstepRoll {0.15, 0.0, 0.7, 1.4};

struct FootstepConfig {
    bool on = true; // ini footsteps: master switch
    std::array<bool, kFootstepGaits> enabled {};
    std::array<double, kFootstepGaits> range {};
    RollConfig roll = kDefaultFootstepRoll;
};

// Every gait at its default.
FootstepConfig DefaultFootstepConfig();

// cfg with each range that is not finite or not above 0 replaced by its
// default, and the roll's invalid fields replaced by kDefaultFootstepRoll's
// (as SanitizeRollConfig does with the general defaults).
FootstepConfig SanitizeFootstepConfig(FootstepConfig cfg);

enum class FootstepSkip {
    None,        // write
    NotFootstep, // not one of the six gaits
    SwitchOff,   // footsteps = false
    GaitOff,     // footsteps_<key> = false
};

const char* FootstepSkipName(FootstepSkip skip);

struct FootstepDecision {
    FootstepSkip skip;
    double base = 0.0;       // the configured range
    double multiplier = 1.0; // the dial roll; 1 when the parameter is in the off list
    double value = 0.0;      // base * multiplier
    bool rolled = false;     // false: in the off list (base written as is)
    int attempts = 0;
    bool fellBack = false;
};

// The roll is cfg.roll (footsteps' own), not the general dial roll.
FootstepDecision DecideFootstep(int32_t seed, std::string_view scene, int32_t param, const FootstepConfig& cfg,
                                const std::set<int32_t>& off);

// EDP_AIAudioPerceptionAlwaysEmitFootstepSoundEvents' pro.repo entry: what
// the mod writes (Value 1, Toggle true) or why nothing.
struct AlwaysEmitDecision {
    bool write = false;
    const char* why = ""; // when not written
};

// Written when footsteps are on and at least one gait is.
AlwaysEmitDecision DecideAlwaysEmit(const FootstepConfig& cfg);
inline constexpr double kAlwaysEmitValue = 1.0;

} // namespace fr
