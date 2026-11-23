#pragma once

// Guarded reads of a template instance's blueprint (STemplateEntityBlueprint,
// behind ZTemplateEntityBlueprintFactory::m_pTemplateEntityBlueprint): the
// sub-entity names, the pin connections' constants and the links the
// template was authored with. Used to name objectives and to copy briefing
// graphs. Game thread.

#include "../core/DianaBriefing.h"

#include <Glacier/ZEntity.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct STemplateEntityBlueprint;

// The template blueprint of the template instance p_Root is the root of
// (its own blueprint factory), or null with *p_Why set. The factory is taken
// as a ZTemplateEntityBlueprintFactory only when its blueprint's sub-entity
// count and root index match the factory's.
const STemplateEntityBlueprint* TemplateBlueprintOf(ZEntityType** p_Root, std::string* p_Why);

// The same for a blueprint factory in hand: e.g. a nested template instance's
// (EntityWalk's LookUpSubEntity), whose entity has no factory of its own.
const STemplateEntityBlueprint* TemplateBlueprintOfFactory(ZEntityBlueprintFactoryBase* p_Factory, std::string* p_Why);

// One sub-entity of the template instance that owns p_Entity (its owning
// entity's template): its blueprint, its index, its authored name and the
// int32 constants of the pin connections from and to it.
struct OwnedSubEntity {
    const STemplateEntityBlueprint* blueprint = nullptr; // the owning template's
    ZEntityType** owner = nullptr;                       // the owning template instance's root
    int index = -1;
    std::string name;
    std::vector<int32_t> connectionConstants;
};

// p_Entity as a sub-entity of its owning entity's template, matched by its
// sub-entity id p_Id (the blueprint's entry at that index must carry the id),
// or empty with *p_Why set.
std::optional<OwnedSubEntity> ReadOwnedSubEntity(ZEntityType** p_Entity, uint64_t p_Id, std::string* p_Why);

// One pin connection or forwarding of a template blueprint, copied out
// (Diana's briefing finder). Pin names longer than the buffers are cut.
struct BlueprintLink {
    enum class Kind { Connection, InputForwarding, OutputForwarding };

    Kind kind;
    int32_t from;
    int32_t to;
    char fromPin[96];
    char toPin[96];
};

// The blueprint's pin connections, input forwardings and output forwardings,
// in that order, read guarded. Empty with *p_Why set on a fault or an
// implausible count.
std::optional<std::vector<BlueprintLink>> ReadBlueprintLinks(const STemplateEntityBlueprint* p_Blueprint,
                                                             std::string* p_Why);

// Which of the pins BriefingGraphs.h copies blueprints for the blueprint's
// connections use, read guarded. Returns 0, or the fault code. Cheap: pin
// name lengths are compared first.
uint32_t ScanBriefingPins(const STemplateEntityBlueprint* p_Blueprint, bool* p_EventOccurredOut, bool* p_TriggerIn,
                          bool* p_StoryPinIn, bool* p_ExitPinIn = nullptr, bool* p_HintPinIn = nullptr);

// The blueprint's sub-entity ids and names and its pin connections and
// forwardings, copied out (fr::BriefingGraph), read guarded. Empty with
// *p_Why set on a fault or an implausible count.
std::optional<fr::BriefingGraph> CopyBriefingGraph(const STemplateEntityBlueprint* p_Blueprint, std::string* p_Why);
