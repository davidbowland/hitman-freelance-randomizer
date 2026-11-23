#pragma once

// The template blueprints Diana's briefing finder, the mission story search
// and the variant exit fix read (core/DianaBriefing.h, core/MissionStories.h,
// core/VariantExits.h), copied in the blueprint factory constructor hook while
// a map variant's load is armed: by scene start the game no longer holds them.
// Kept across restarts (a restart builds no new factories); the oldest copies
// are dropped past the budgets. Also the helpers those features share to read
// a graph and to send a blocked pin's signal on.

#include "../core/DianaBriefing.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

class ZEntityBlueprintFactoryBase;
class ZEntityType;
class ZObjectRef;
struct STemplateEntityBlueprint;

namespace briefing_graphs {

// Game thread: copy the blueprints built from now on (a map variant's load
// is armed), or stop.
void SetCapture(bool p_On) noexcept;

// Game thread: drops every copy (a load that arms no map variant).
void Clear();

// ZTemplateEntityBlueprintFactory constructor hook, any thread: one relaxed
// load when capture is off; otherwise a guarded pin scan (ScanBriefingPins,
// BlueprintRead.h), and a copy of a blueprint that may hold one of the pins
// those features look for.
void OnBlueprintBuilt(const void* p_Factory, const STemplateEntityBlueprint* p_Blueprint) noexcept;

// The copy made for the factory at this address, if any (check it with
// fr::BriefingGraphMatches before using it).
std::optional<fr::BriefingGraph> Captured(const void* p_Factory);

// The graph of p_Factory's blueprint: still resident, else the copy made while
// the scene loaded, checked against p_Seen (sub-entity index, id) as the walk
// read them. Empty with *p_Why set.
std::optional<fr::BriefingGraph> GraphOf(ZEntityBlueprintFactoryBase* p_Factory,
                                         const std::vector<std::pair<int32_t, uint64_t>>& p_Seen, std::string* p_Why);

// Sub-entity p_Index's authored name, else its id in hex, else "#<index>".
std::string SubName(const fr::BriefingGraph& p_Graph, int32_t p_Index);

struct Stats {
    size_t seen = 0;    // blueprints built while capture was on (session)
    size_t copied = 0;  // copies made (session)
    size_t faults = 0;  // reads that faulted (session)
    size_t kept = 0;    // copies held now
    size_t links = 0;   // their pin connections and forwardings
    size_t dropped = 0; // copies dropped for the budgets (session)
};

Stats GetStats();

// One link a blocked pin's signal is sent on along (its other logic).
struct Resend {
    ZEntityType** entity = nullptr;
    uint32_t pin = 0;    // the target's pin id
    bool output = false; // an output forwarding: the target's output pin is signaled
};

struct ResendResult {
    size_t sent = 0;
    size_t faults = 0;
};

// Any thread (a pin hook): signals p_Data along each link, guarded, except a
// link back into the blocked pin itself (p_From's output p_FromPin).
ResendResult SendOn(std::span<const Resend> p_Links, ZEntityType** p_From, uint32_t p_FromPin,
                    const ZObjectRef& p_Data) noexcept;

} // namespace briefing_graphs
