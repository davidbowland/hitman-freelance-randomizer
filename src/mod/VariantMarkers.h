#pragma once

// A map variant's own map markers switched off (core/VariantMarkers.h): the
// option's bricks are walked by the Diana briefing search (Variants.cpp
// FindBriefings, once the scene's entities exist), each entity's interfaces
// read here, and the objective and opportunity markers among them get every
// m_bShowOn* property set to false, read back. Every raw engine step follows
// the guard pattern in Guard.h.

#include "../core/VariantMarkers.h" // the pure logic (this header shares its name)

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class ZEntityType;

namespace variant_markers {

// One entity as the marker search sees it.
struct Seen {
    bool tracker = false;                // one of its interfaces is a map tracker class
    std::vector<std::string> interfaces; // its interface names, when tracker
    std::optional<int32_t> layer;        // its m_eLayer, when tracker and readable
    uint32_t fault = 0;                  // reading its interfaces faulted
};

// Its interfaces read guarded; allocates only for a map tracker.
Seen Read(ZEntityType** p_Entity);

// Every fr::kMarkerShowProperties set to false through the engine (change
// handlers run) and read back. Empty when all read back false, else why not.
std::string SwitchOff(ZEntityType** p_Entity);

} // namespace variant_markers
