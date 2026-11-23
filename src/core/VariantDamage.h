#pragma once

// Timed damage of a map variant's own escalation (generic, found at runtime).
//
// Sloth's ancestral_harebell (Dartmoor) gives 47 a
// "vitality" of 100 (helper_harebell_vitality): running costs 0.15 a meter
// (helper_harebell_movementcost), and takedowns, climbing, shooting, melee,
// pushing, throwing and dumping bodies cost more while they last
// (helper_harebell_timecost). Nothing brings it back up. Two
// ZHitmanDamageOverTime entities hurt 47 inside its level volume (a 288 x 411
// x 61 m box over the estate) while the escalation's objectives are not done:
// - "Damage" (m_fDamage 10, m_fInterval 1): switched on when vitality is
//   depleted (helper Depleted true) and off when it is not, so once it runs
//   out 47 loses 10 health every second until he dies;
// - "Damage effect" (m_fInterval 0.01, m_fDamage left at the class's own):
//   one hit each time a costly action (a takedown, climbing, shooting, a
//   graceful throw, kick, push, body dump or melee) ends while vitality is not
//   depleted; it switches itself off after the hit.
// Freelancer has no vitality meter (the escalation's HUD counter is its
// contract's objective, which Freelancer's contract does not have), so the
// player cannot see it run out. The other Seven Deadly Sins bricks appended
// (Envy, Lust, Gluttony) have no ZHitmanDamageOverTime.
//
// Every ZHitmanDamageOverTime inside the variant's own bricks is switched off
// once the scene's entities exist: m_fDamage set to 0, so whatever switches it
// on it never hurts 47, and m_fInterval set to a value no mission lasts, so it
// never ticks at all (no hurt sound or hit effect either). The vitality is
// kept full and its comments muted too (core/VariantComments.h); this stays as
// the safety net. The rest of the variant (NPCs, set dressing) is left as it
// is. Freelancer's own bricks are never searched.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fr {

// The engine class of the escalation's damage entities.
constexpr std::string_view kDamageOverTimeClass = "ZHitmanDamageOverTime";

// Whether an engine class name is ZHitmanDamageOverTime (any case).
bool IsDamageOverTimeClass(std::string_view className);

// The properties set, and to what.
constexpr std::string_view kDamageProperty = "m_fDamage";
constexpr std::string_view kIntervalProperty = "m_fInterval";
constexpr float kDamageOff = 0.0f;
constexpr float kIntervalOff = 1.0e7f; // seconds: about 115 days

// One damage entity's values before it was switched off (read raw; empty
// when not readable).
struct DamageBefore {
    std::optional<float> damage;
    std::optional<float> interval;
};

// "10 every 1 s"; "? every 0.01 s" when a value was not read.
std::string DamageBeforeText(const DamageBefore& before);

// Whether a value read back after the switch off is the one set (empty: not
// read back, taken as set when SetProperty returned true).
bool DamageReadBackOk(std::optional<float> readBack, float expected);

// The one log line's counts.
struct DamageTally {
    size_t switchedOff = 0;
    size_t failed = 0;   // a property not set, or not read back as set
    std::string details; // "10 every 1 s, ? every 0.01 s"
};

void AddDamage(DamageTally& tally, const DamageBefore& before, bool ok);

// The log line's text after "variant: <option>: ": "timed damage switched off
// (2 ZHitmanDamageOverTime; they dealt 10 every 1 s, ? every 0.01 s; set to
// 0 damage, never ticking)", with the ones that failed counted; "timed damage
// not switched off (...)" when none could be; "timed damage: none in this
// variant's bricks" when nothing was found.
std::string DamageTallyText(const DamageTally& tally);

} // namespace fr
