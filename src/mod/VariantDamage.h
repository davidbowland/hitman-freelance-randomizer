#pragma once

// A map variant's own timed damage switched off (core/VariantDamage.h): the
// option's bricks are walked by the Diana briefing search (Variants.cpp
// FindBriefings, once the scene's entities exist), and each
// ZHitmanDamageOverTime among them gets m_fDamage set to 0 and m_fInterval
// to fr::kIntervalOff, read back. Every raw engine step follows the guard
// pattern in Guard.h.

#include "../core/VariantDamage.h" // the pure logic (this header shares its name)

#include <string>

class ZEntityType;

namespace variant_damage {

// Its m_fDamage and m_fInterval as they were (read raw, guarded).
fr::DamageBefore Read(ZEntityType** p_Entity);

// m_fDamage set to fr::kDamageOff and m_fInterval to fr::kIntervalOff
// through the engine (change handlers run), read back. Empty when both read
// back as set, else why not.
std::string SwitchOff(ZEntityType** p_Entity);

} // namespace variant_damage
