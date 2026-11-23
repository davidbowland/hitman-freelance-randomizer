#pragma once

// What each dial does, in plain English, and its unit. IOI documents none of
// these parameters: every entry says whether it rests on an external source
// (Documented), on what the parameter's name says (Named; units only), or on
// the name and its values per difficulty (Inferred).

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace fr {

enum class DialUnit {
    Meters,
    Seconds,
    Multiplier, // shown as x1.5
    Attention,  // a fraction of full attention, 0 to 1
    Count,
    PerSecond,
    Percent,
    Degrees,
    HitPoints,
};

enum class Basis {
    Documented, // an external source says so (DialInfo::source)
    Named,      // units only: the name says what it measures (...Range, ...Distance, ...Radius: meters;
                // ...Time, ...Delay, ...Cooldown, ...Duration, ...Period: seconds; ...Multiplier: x;
                // ...PerSecond: per second), in the engine's units
    Inferred,   // a reading of the name and the values per difficulty; may be wrong
};

struct DialInfo {
    int32_t param;
    std::string_view name; // short and friendly, e.g. "Gunshot hearing range"
    DialUnit unit;
    Basis unitBasis;
    std::string_view description; // one line: what it does in game
    Basis basis;                  // of the description: Documented or Inferred
    std::string_view source;      // where a Documented unit or description comes from; empty otherwise
};

// One entry per dial (FindDial), in the dial table's order.
std::span<const DialInfo> DialInfoTable();
const DialInfo* FindDialInfo(int32_t param);

// "m", "s", "x", "attention", "", "/s", "%", "deg", "HP".
std::string_view UnitLabel(DialUnit unit);

// The friendly name, or the enumerator name (ParamLabel) for a parameter with no info.
std::string DialDisplayName(int32_t param);

// A value at 4 significant digits ("60", "120.1", "0.7264"), never in exponent form below 1e6.
std::string FormatDialNumber(double value);

// "60 -> 120.1", or "x1.1 -> x1.43" for a multiplier: the numbers without the unit label.
std::string FormatDialValues(double before, double after, DialUnit unit);

// "60 -> 120.1 m", "x1.1 -> x1.43", "0.3 -> 0.45 attention", "2 -> 3" (Count);
// " (?)" follows an Inferred unit. ASCII only.
std::string FormatDialChange(double before, double after, DialUnit unit, Basis unitBasis);

// "larger", "smaller" or "neither" for a dial direction (+1, -1, 0).
std::string_view HarderWhen(int8_t dir);

// "documented", "from its name" or "inferred".
std::string_view BasisLabel(Basis basis);

} // namespace fr
