#pragma once

// Pure part of the night crate lamps (src/mod/CrateLamps.h has the engine
// part). Freelancer's supply crate (setpiece_evergreen_stash_unique, TEMP
// 00D1399ACD41FD9F) lights its inside when it opens, tuned for Freelancer's
// day maps; on a night map variant, with the eye adapted to the dark, that
// lamp blinds (an "incredibly bright light" in the middle of the crate). On a
// night load each
// crate's lamp is dimmed to a modest glow; day loads are left alone.
//
// The crate's lights, from its template (chunk0patch4) and the crate
// placements of Sapienza's override_bingo brick (chunk26patch4):
// - Light_Area_Side_04, the center light, points down into the crate; nothing
//   drives it. Template m_fDiffusePower 100, but each placement sets its own
//   through the crate root's alias m_fDiffusePower: 100 to 7000 on Sapienza.
// - Light_Env_05, in the center too: template m_fDiffusePower 1, each
//   placement's through the alias m_fDiffusePower_Environment: 25 to 2000.
// - FX_Logic_LampCore_01: the crate's "Open" turns it on (TurnOn) and off; on
//   it sends its DiffusePower (template 200, placements 25 to 1000 through the
//   alias DiffusePower) to the four side lights' DiffusePower input pins. Its
//   GlowPower (10) is linked to nothing in the crate.
// - Light_Area_Side, _01, _02 and _03: the four side lights, m_fDiffusePower
//   1 until the lamp core sends its power at the open.
// Each value is lowered to its night cap (never raised): the center and
// environment lights once the scene plays, the lamp core's DiffusePower too
// when it is a property at run time (a template alias may not be), and the
// side lights whenever they are seen above the cap after an open.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fr {

struct VariantOption;

// --- Night loads ---

// A night time of day brick, by its catalog name (VariantBrick::name): a
// "tod_" brick with "night" in its name (tod_summer_night_clear,
// tod_ebola_night, tod_zika_night).
bool IsNightTimeOfDayBrick(std::string_view p_Name);

// The option's first night time of day brick's name, or empty.
std::string_view NightTimeOfDayOf(const VariantOption& p_Option);

struct NightVerdict {
    bool night = false;
    std::string why; // for the log: "the full Bad Boy loads tod_summer_night_clear"
};

// Whether a load is at night, from the variant it applied (AppliedVariant in
// src/mod/Variants.h): p_Applied is a full version's key ("badboy") when
// p_Full, else an appended option's name ("the_source"); empty for none. A
// full version is at night when the option it is the full version of
// (FullVariant::option) has a night time of day brick: the full Icon, Bad
// Boy, Author and Entertainer; not the full Landslide
// (morning), Patient Zero (tod_flu) or Gluttony. An appended option likewise
// (The Source's tod_zika_night). Freelancer's own maps are all day.
NightVerdict JudgeNightLoad(std::string_view p_Applied, bool p_Full);

// --- The crate's lamp ---

enum class CrateLampPart : uint8_t {
    LampCore,    // FX_Logic_LampCore_01: DiffusePower, sent to the side lights on TurnOn
    Center,      // Light_Area_Side_04: m_fDiffusePower
    Environment, // Light_Env_05: m_fDiffusePower
    Side,        // the four side lights: m_fDiffusePower, set by the lamp core at an open
};

// One entity of the crate template, by its sub-entity id.
struct CrateLampEntity {
    uint64_t id;
    std::string_view name;
    CrateLampPart part;
    std::string_view property; // the float32 property capped at night
};

inline constexpr size_t kCrateLampEntityCount = 7;
inline constexpr size_t kCrateSideLightCount = 4;

// The lamp core, the center light, the environment light, then the four side lights.
std::span<const CrateLampEntity> CrateLampEntities();

// The night cap of a part's power: the side lights' is the lamp core's (it
// sends its power to them).
float CrateLampNightCap(CrateLampPart p_Part);

// The value to write at night: the cap when p_Value is above it (by more than
// a rounding error), else nothing (a lower value is never raised).
std::optional<float> CrateLampNightValue(CrateLampPart p_Part, float p_Value);

// The log's name of a part ("center light").
std::string_view CrateLampPartName(CrateLampPart p_Part);

// A set of values for the log: "100", "100 to 7000", or "none".
std::string CrateLampValueSpan(std::span<const float> p_Values);

} // namespace fr
