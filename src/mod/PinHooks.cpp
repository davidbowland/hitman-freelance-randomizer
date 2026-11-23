// The pin, blueprint factory and activation detours: each hands the call to
// the features that watch it (each checks its own switch first).

#include "FreelancerRandomizer.h"

#include "BriefingGraphs.h"
#include "Guard.h"
#include "LoadFaultWatch.h"
#include "ModLog.h"
#include "SceneStage.h"

#include <Functions.h>
#include <Globals.h>

#include <Glacier/EntityFactory.h>
#include <Glacier/Enums.h>
#include <Glacier/Hash.h>
#include <Glacier/SExternalReferences.h>
#include <Glacier/ZResource.h>

#include <exception>

// Hot path: every output pin signal in the game. With nothing armed, a few
// relaxed loads, with nothing allocated.
DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, bool, SignalOutputPin, ZEntityRef entity, uint32_t pinId,
                     const ZObjectRef& data) {
    (void)p_Hook; // declared by DEFINE_PLUGIN_DETOUR
    // A blocked signal returns early: the engine skips the pin and its connections.
    if (m_Coin.PinsOn() && m_Coin.OnOutputPin(entity, pinId))
        return HookResult<bool>(HookAction::Return(), false);

    // lockpick_grant: a pick with only the given lockpick has its LockpickCompleted held back (given later).
    if (m_DoorLocks.LockpickPinsOn() && m_DoorLocks.OnOutputPin(entity, pinId))
        return HookResult<bool>(HookAction::Return(), false);

    // A map variant's elusive target brick: Diana's fly-in briefing would talk over Freelancer's own, and her
    // other lines belong to the variant's own story.
    if (m_Variants.OutputPinsOn() && m_Variants.OnOutputPin(entity, pinId, data))
        return HookResult<bool>(HookAction::Return(), false);

    // A map variant's own exit: its contract end is replaced by Freelancer's exit flow (VariantExits.h).
    if (m_Variants.ExitPinsOn() && m_Variants.OnExitPin(entity, pinId, data))
        return HookResult<bool>(HookAction::Return(), false);

    if (m_ClientReqs.PinsOn())
        m_ClientReqs.OnOutputPin(entity, pinId);

    // Before the engine passes the signal on: the fishing selector's weights are set before its Pick.
    if (m_Fishing.PinsOn())
        m_Fishing.OnOutputPin(entity, pinId);

    if (m_Prestige.PinsOn())
        m_Prestige.OnOutputPin(entity, pinId, data);

    if (m_Merces.PinsOn())
        m_Merces.OnOutputPin(entity, data);

    m_Loadout.OnOutputPin(entity, pinId, data);

    // Before the engine passes the signal on: a firearm's weight is rewritten on its way to its choice.
    if (m_CrateGuns.Armed())
        m_CrateGuns.OnOutputPin(entity, pinId, data);

    // Before the engine passes the signal on: an eligible gun's weight is held on its way to its choice.
    if (m_StockSteer.Armed())
        m_StockSteer.OnOutputPin(entity, pinId, data);

    if (m_SickSpots.PinsOn())
        m_SickSpots.OnOutputPin(entity, pinId, data);

    if (m_Investigations.PinsOn())
        m_Investigations.OnPin(entity, pinId, data, false);

    return HookResult<bool>(HookAction::Continue());
}

// Every template blueprint factory the game builds: while a map variant's load
// is armed, the blueprints that may hold Diana's briefing are copied (the game
// drops the blueprint once the factory is built). Otherwise one relaxed load.
DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, ZTemplateEntityBlueprintFactory*,
                     ZTemplateEntityBlueprintFactory_ZTemplateEntityBlueprintFactory,
                     ZTemplateEntityBlueprintFactory* th, STemplateEntityBlueprint* pTemplateEntityBlueprint,
                     ZResourcePending& /*ResourcePending*/
) {
    (void)p_Hook; // declared by DEFINE_PLUGIN_DETOUR
    load_fault::NoteFactoryBuilt();
    briefing_graphs::OnBlueprintBuilt(th, pTemplateEntityBlueprint);
    return HookResult<ZTemplateEntityBlueprintFactory*>(HookAction::Continue());
}

// Added by EnsureDetours when a feature needs input pins. Hot: one relaxed
// load each when none is armed; when armed, a pointer lookup per pin.
DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, bool, SignalInputPin, ZEntityRef entity, uint32_t pinId,
                     const ZObjectRef& data) {
    (void)p_Hook; // declared by DEFINE_PLUGIN_DETOUR
    // lockpick_grant: the given items' push or remove on the mission's "Inventory items" is skipped (never saved).
    if (m_Loadout.GrantPinsOn() && m_Loadout.OnInputPin(entity, pinId, data))
        return HookResult<bool>(HookAction::Return(), false);

    // A map variant's Diana lines: its own story (its target escaping or eliminated), never Freelancer's.
    if (m_Variants.LinePinsOn() && m_Variants.OnInputPin(entity, pinId))
        return HookResult<bool>(HookAction::Return(), false);

    if (m_MoneyItems.Armed())
        m_MoneyItems.OnInputPin(entity, pinId, data);

    if (m_Fishing.PinsOn())
        m_Fishing.OnInputPin(entity, pinId);

    if (m_Prestige.PinsOn())
        m_Prestige.OnInputPin(entity, pinId, data);

    if (m_Investigations.PinsOn())
        m_Investigations.OnPin(entity, pinId, data, true);

    return HookResult<bool>(HookAction::Continue());
}

// Added with loot_amounts, crate_guns or the supplier walk, before the engine
// activates the entity: money items get their amount written (loot amounts)
// first; crate guns and the supplier walk hand offers spawned under options
// to their steering.
DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, void, ZEntityManager_ActivateEntity, ZEntityManager* /*th*/,
                     ZEntityRef* entity, void* /*a3*/) {
    (void)p_Hook; // declared by DEFINE_PLUGIN_DETOUR
    if (m_MoneyItems.Armed())
        m_MoneyItems.OnActivateEntity(entity);

    if (m_CrateGuns.Armed())
        m_CrateGuns.OnActivateEntity(entity);

    if (m_SupplierWalk.OffersOn())
        m_SupplierWalk.OnActivateEntity(entity);

    return HookResult<void>(HookAction::Continue());
}

std::string FreelancerRandomizer::StageLabel() const {
    const int s_Stage = m_Stage.load(std::memory_order_relaxed);

    if (s_Stage < 0)
        return "no loading stage yet";

    return fmt::format("{} ({})", StageName(static_cast<ESceneLoadingStage>(s_Stage)), s_Stage);
}
