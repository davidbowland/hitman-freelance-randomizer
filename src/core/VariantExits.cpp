#include "VariantExits.h"

#include <algorithm>
#include <array>
#include <iterator>

namespace fr {

namespace {

struct ExitTemplate {
    uint64_t temp;
    uint64_t tblu;
    std::string_view name;
};

// ZHMTools' hash list: [assembly:/_pro/design/levelflow.template?/exit_*.entitytemplate] and
// [assembly:/_pro/design/setpieces/setpieces_evergreen.template?/exit_evergreen_*.entitytemplate].
constexpr std::array<ExitTemplate, 18> kExitTemplates {{
    {0x006F4770376B274Eull, 0x00EA1A647E6ADCF4ull, "exit_pushbutton"},
    {0x00B1C1AF0FBF19FBull, 0x008A4467DE496723ull, "exit_pushbutton2d_volumebox"},
    {0x00EBE06398B0BBB0ull, 0x00D73B9102BB6EA1ull, "exit_usekey"},
    {0x008B1A8FB21AEE59ull, 0x00B55BC66BA1D8C2ull, "exit_useitem"},
    {0x001AEB1B13F74269ull, 0x007DEB941993B5F8ull, "exit_usedisguise"},
    {0x00ACED05F5115B67ull, 0x0065250CBDB68590ull, "exit_conditionaluse"},
    {0x00F950FC18B9C8EEull, 0x00C191C67AE267F1ull, "exit_area_crowd"},
    {0x00867DFCE87A918Full, 0x00E3047C98CA8235ull, "exit_area_logic_with icon"},
    {0x00887DF262A65E73ull, 0x00CDBD3C3C896E68ull, "exit_icon_for_train"},
    {0x00E38E3006E744D0ull, 0x0004EDBC6981F932ull, "exit_evergreen_pushbutton"},
    {0x0061D971F87022C9ull, 0x00DBBE6276676165ull, "exit_evergreen_pushbutton2d_volumebox"},
    {0x0037FF5183103A32ull, 0x009AB36B427ED062ull, "exit_evergreen_usekey"},
    {0x0006137045C56C7Aull, 0x006F89877A5D5C55ull, "exit_evergreen_useitem"},
    {0x00311DC250B6D7D8ull, 0x008C12E5328ADF5Dull, "exit_evergreen_usedisguise"},
    {0x009ACFA220EDD0D8ull, 0x00493E9A5B30B360ull, "exit_evergreen_conditionaluse"},
    {0x003E398EAD96D76Full, 0x00F71487E387D9DFull, "exit_evergreen_area_crowd"},
    {0x00AA2E96383F51F4ull, 0x00D310CBAF6BB9EAull, "exit_evergreen_area_logic_with icon"},
    {0x009517A70A7C990Bull, 0x00D6E763E38175C6ull, "exit_evergreen_container_body_manhole"},
}};

const ExitTemplate* FindTemplate(uint64_t rid) {
    for (const ExitTemplate& t : kExitTemplates) {
        if (rid != 0 && (t.temp == rid || t.tblu == rid))
            return &t;
    }

    return nullptr;
}

// levelflow.template?/exit_pushbutton: ValueBool_Poll_IsExitOnManualAllowed PollTrue and
// ValueBool_Poll_IsExitAllowed PollFalse each feed only the contract exit "Exit"'s Trigger;
// ValueBool_Poll_UseManualTrigger01 PollTrue is forwarded to the template's Exit.
constexpr std::array<KnownExitPin, 2> kPushButtonEnds {{
    {0xF61FB1A30A97B8E3ull, "PollTrue"},
    {0x7ED437F56C459471ull, "PollFalse"},
}};
constexpr std::array<KnownExitPin, 1> kPushButtonStarts {{{0x2ADB2685CDC54008ull, "PollTrue"}}};

// setpieces_evergreen.template?/exit_evergreen_pushbutton: Helper_Evergreen_MissionExitModal confirmed and
// ForceActivateExit Out (its ManualForceActivateExit input) signal the hijacker's exitActivated.
constexpr std::array<KnownExitPin, 2> kEvergreenPushButtonConfirms {{
    {0xBFC6D80E20A8087Dull, "confirmed"},
    {0x282C1F1F235C6A68ull, "Out"},
}};

// setpieces_evergreen.template?/exit_evergreen_usekey: its modal's confirmed.
constexpr std::array<KnownExitPin, 1> kEvergreenUseKeyConfirms {{{0xA5B53C261CE4A088ull, "confirmed"}}};

// setpieces_evergreen.template?/exit_evergreen_pushbutton: ActionEnabled (its prompt) and Show Spatial (its map
// tracker and 3D icon), both ValueBoolBasic.
constexpr std::array<KnownExitPin, 2> kEvergreenPushButtonOffs {{
    {0xEEE30F74A0375367ull, "SetFalse"}, // ActionEnabled
    {0x2874DC3A4E18CA9Dull, "SetFalse"}, // Show Spatial
}};

// setpieces_evergreen.template?/exit_evergreen_usekey: the same pair as the push
// button's. Its modal's confirmed sends ActionEnabled SetFalse; Visible (the AND that is Interaction_Interact_Key's
// visible condition list) is ActionEnabled, OuterConditions (the instance's m_aConditions) and the inventory having
// loaded; Show3dSpatial is polled before each of its four 3D icons is shown and is in "Mission Complete" and "Mission
// Not Complete 1", whose OnValue sets its two map trackers visible.
constexpr std::array<KnownExitPin, 2> kEvergreenUseKeyOffs {{
    {0x427C6DE31FA17225ull, "SetFalse"}, // ActionEnabled
    {0x58BD3ACE491635B9ull, "SetFalse"}, // Show3dSpatial
}};

constexpr std::array<KnownExitTemplate, 3> kKnownTemplates {{
    {0x006F4770376B274Eull, 0x00EA1A647E6ADCF4ull, 0x16BF13FFE1703300ull, kPushButtonEnds, kPushButtonStarts, {}},
    {0x00E38E3006E744D0ull,
     0x0004EDBC6981F932ull,
     0x7AD714FA457BD132ull,
     {},
     {},
     kEvergreenPushButtonConfirms,
     kEvergreenPushButtonOffs},
    {0x0037FF5183103A32ull,
     0x009AB36B427ED062ull,
     0x492C010209533A1Dull,
     {},
     {},
     kEvergreenUseKeyConfirms,
     kEvergreenUseKeyOffs},
}};

// override_shogi's five exits (Exit_Evergreen_PushButton, _01 to _04): each
// starts one of location's exit sequences, all of which scenario_bellini
// deletes (location's ExitPoints but LeavingMissionArea).
constexpr std::array<uint64_t, 5> kShogiExits {
    0x88C6DBAC409FFE6Aull, // Exit_Evergreen_PushButton_02, the cable car (CableCar_Exit 5c9a8fccbe0aa735), d0c66e7b
    0x1464774A68528B34ull, // Exit_Evergreen_PushButton_03, the north path (Snowscooter_Exit f8aea9fa05d151b4), 08d5a5f2
    0xA64BA5D053FD8C7Eull, // Exit_Evergreen_PushButton, the cliff top (Mountain_Exit 9c0500561d7a4c00), e5550c16
    0x26B240F303F218F7ull, // Exit_Evergreen_PushButton_01, the helipad (Helicopter_Exit a76bfdf34907f4df), 6191ec3d
    0x55BD6CCDAD588E93ull, // Exit_Evergreen_PushButton_04, the helipad (Helicopter_Exit a76bfdf34907f4df), 8884692d
};

// The other variants that delete the exit sequences Freelancer's exits start, with the exits themselves kept: a
// brick's delete takes the deleted entity's subtree in its own brick, not another brick's entities parented under it
// (Lust deletes scenario_fox's "Exit locations", the logical parent of override_meyer's eight exits, and the exit
// walk still finds all eight). From tools/landslide/variant_audit.py's exit-seq findings; the exits these variants leave unusable are switched off instead
// (kSwitchOffs), so only the usable ones are listed.
constexpr std::array<uint64_t, 3> kMeyerExits {
    0x6EA77A30C4C7828Aull, // Water Tower Road_Bike Evergreen (Exit_Road_Bike)
    0x2DDA760FAEF09E7Cull, // B2 Emergency Exit Evergreen (Exit_Emergency)
    0x8FE1D8EECF25154Aull, // Moon Rider Bike Evergreen (Exit_Road_Bike_01; only once its easter egg is unlocked)
};
constexpr std::array<uint64_t, 6> kSapoExits {
    0x9D8892E605683375ull, // Exit_Evergreen_UseKey, the boat (Exit_Boat)
    0xCA16199D211B879Dull, // Exit_Evergreen_UseKey, Yates's car (Exit_Car)
    0x3D562751B596CE29ull, // Exit_Evergreen_PushButton, the shrine (Exit_Shrine_01)
    0x8D0EB27E94F0C2CCull, // Exit_Evergreen_PushButton, the grape fields (Exit_Grape_Fields)
    0x04D5B4E3A0450A26ull, // Exit_Evergreen_UseDisguise (Exit_Cave_01; its disguise is deleted too)
    0xFB292B76838A5664ull, // Exit_Evergreen_UseDisguise_01 (Exit_Cave_01; its disguise is deleted too)
};
constexpr std::array<uint64_t, 1> kBridgeBoatExit {
    0x611EEFD9E524DC7Eull, // Exit_Evergreen_PushButton_02, the boat (Exit_Boat)
};

constexpr std::array<BrokenExitSequences, 4> kBroken {{
    {"reflection", kShogiExits,
     "scenario_bellini deletes location's exit sequences (CableCar_Exit, Snowscooter_Exit, Mountain_Exit, "
     "Helicopter_Exit) that Freelancer's Hokkaido exits start"},
    {"lust", kMeyerExits,
     "scenario_ambrosia deletes scenario_fox's Exit locations, with every exit sequence Freelancer's Berlin exits "
     "start"},
    {"envy", kSapoExits, "scenario_frangipani deletes the exit sequences Freelancer's Mendoza exits start"},
    {"harbinger", kBridgeBoatExit, "scenario_filur deletes scenario_bulldog's Exit_Boat (and the boat)"},
}};

// The Guru's mission_harveywallbanger (also loaded by The Deceivers' scenario_corpsereviver, listed too in case
// the wrapper's own bricks are not in the loaded list) deletes location's getaway car Avance_Alpha_1_A_00
// (3a579881fa46f497) and its Exit_Road; Freelancer's Exit_Evergreen_PushButton_03 still starts location's Car_Exit
// (44724f00f7782bb3), whose track drives that car.
constexpr std::array<uint64_t, 2> kGuruBricks {
    0x003C73877802B873ull, // coastaltown/mission_harveywallbanger (kMissionHarveywallbangerBrick)
    0x003683714A5AABDFull, // coastaltown/scenario_corpsereviver (kScenarioCorpsereviverBrick)
};

// The other variants' bricks (as Variants.cpp's VariantBrick rids), each with the Freelancer exits it leaves
// unusable (tools/landslide/variant_audit.py).
constexpr std::array<uint64_t, 1> kBelliniBricks {0x002DAF48CF0FD7E6ull}; // hokkaido/scenario_bellini (The Reflection)
constexpr std::array<uint64_t, 1> kSmoothsnakeBricks {
    0x00E56CA467419D97ull};                                             // ancestral/scenario_smoothsnake (Garden Show)
constexpr std::array<uint64_t, 1> kFilurBricks {0x00DC692646C00776ull}; // ancestral/scenario_filur (The Harbinger)
constexpr std::array<uint64_t, 1> kHarebellBricks {0x002F7D8B6D31F6E0ull}; // ancestral/ancestral_harebell (Sloth)
constexpr std::array<uint64_t, 1> kAmbrosiaBricks {0x00D47F2B1CBC7BEBull}; // edgy/mission_fox/scenario_ambrosia (Lust)

constexpr std::array<ExitSwitchOff, 11> kSwitchOffs {{
    {0xD5AA959458203ABFull, kGuruBricks,
     "The Guru's brick deletes the road getaway car this exit drives off in (Exit_Evergreen_PushButton_03, "
     "location's Car_Exit)"},
    // Hokkaido, override_shogi.
    {0x88C6DBAC409FFE6Aull, kBelliniBricks,
     "The Reflection closes the cable car station (the garage level) this exit is in, and deletes the gondola and "
     "its getaway (no floor near it on its navmesh; Exit_Evergreen_PushButton_02, CableCar_Exit)"},
    // Dartmoor, override_bridge: scenario_bulldog's Exit_Default (both push buttons) drives Motorcycle_Bulldog_A,
    // Exit_Boat's track drives a mansion guard, Exit_Hearse needs Hearse_A and the Hearse Key spawner.
    {0x611EEFD9E524DC7Eull, kSmoothsnakeBricks,
     "the Garden Show deletes the guard this exit's boat getaway shows (Exit_Evergreen_PushButton_02, Exit_Boat)"},
    {0x36D05A06ACB29565ull, kSmoothsnakeBricks,
     "the Garden Show deletes the hearse and its key (Exit_Evergreen_UseKey, Exit_Hearse)"},
    {0x36D05A06ACB29565ull, kHarebellBricks,
     "Sloth deletes the hearse this exit's getaway drives off in; its key still spawns (Exit_Evergreen_UseKey, "
     "Exit_Hearse)"},
    {0x36D05A06ACB29565ull, kFilurBricks,
     "The Harbinger deletes the hearse key's keyword, the hearse's condition and its getaway (Exit_Evergreen_UseKey, "
     "Exit_Hearse)"},
    // Berlin, override_meyer: scenario_ambrosia deletes scenario_fox's Exit locations, with the vehicles, the key
    // keywords and the "47 got key and ... not destroyed" conditions of the key exits.
    {0x438C41798A384859ull, kAmbrosiaBricks,
     "Lust deletes the delivery scooter, its key keyword and its condition (Scooter exit Evergreen 1)"},
    {0xA19BAB1ACA3EE6B4ull, kAmbrosiaBricks,
     "Lust deletes the delivery scooter, its key keyword and its condition (Scooter exit Evergreen)"},
    {0x40C5BB702C67E371ull, kAmbrosiaBricks,
     "Lust deletes the motorcycle, its key keyword and its condition (Motorcycle exit Evergreen)"},
    {0xD1C97406FAFD603Cull, kAmbrosiaBricks,
     "Lust deletes the motorcycle, its key keyword and its condition (Motorcycle exit Evergreen 2)"},
    {0x91DC977AC5F3341Aull, kAmbrosiaBricks,
     "Lust deletes the switch that enables this easter egg exit and its getaway (Exit UFO Evergreen, Exit_Files)"},
}};

// The links (indices) of from.fromPin that do not go into skip, as FindStoryMutes resends them.
std::vector<size_t> OtherLinks(const std::vector<BriefingLink>& links, int32_t from, const std::string& fromPin,
                               size_t count, const std::vector<size_t>& skip) {
    std::vector<size_t> out;

    for (size_t i = 0; i < links.size(); ++i) {
        const BriefingLink& link = links[i];

        if (link.kind == BriefingLinkKind::InputForwarding || link.from != from || link.fromPin != fromPin)
            continue;

        if (link.to < 0 || static_cast<size_t>(link.to) >= count)
            continue;

        if (std::find(skip.begin(), skip.end(), i) != skip.end())
            continue;

        out.push_back(i);
    }

    return out;
}

bool SamePin(const ExitPin& pin, int32_t entity, const std::string& name) {
    return pin.entity == entity && pin.pin == name;
}

} // namespace

bool IsExitHijackerResource(uint64_t rid) {
    return rid == kExitHijackerTblu || rid == kExitHijackerTemp;
}

std::string_view ExitTemplateName(uint64_t rid) {
    const ExitTemplate* t = FindTemplate(rid);
    return t ? t->name : std::string_view();
}

VariantExitPins FindVariantExitPins(int32_t contractsExit, size_t count, const std::vector<BriefingLink>& links) {
    VariantExitPins out;
    const auto inRange = [&](int32_t index) { return index >= 0 && static_cast<size_t>(index) < count; };

    if (!inRange(contractsExit))
        return out;

    // Every link into the contract exit's Trigger: blocked at its feeding pin.
    std::vector<size_t> feeds;

    for (size_t i = 0; i < links.size(); ++i) {
        const BriefingLink& link = links[i];

        if (link.to == contractsExit && link.toPin == kContractsExitEndPin &&
            link.kind != BriefingLinkKind::OutputForwarding)
            feeds.push_back(i);
    }

    for (const size_t i : feeds) {
        const BriefingLink& link = links[i];

        if (link.kind == BriefingLinkKind::InputForwarding || !inRange(link.from)) {
            if (out.skipped++ == 0)
                out.skipWhy = "fed by the template's own input (from outside it)";
            continue;
        }

        if (std::any_of(out.ends.begin(), out.ends.end(),
                        [&](const ExitPin& p) { return SamePin(p, link.from, link.fromPin); }))
            continue;

        std::vector<size_t> resend = OtherLinks(links, link.from, link.fromPin, count, feeds);

        if (resend.size() > kLineMaxResend) {
            if (out.skipped++ == 0)
                out.skipWhy = "its feeding pin starts too many other links";
            continue;
        }

        out.ends.push_back({link.from, link.fromPin, std::move(resend)});
    }

    // Every output forwarding into the template's Exit: the exit's own sequence starts.
    for (const BriefingLink& link : links) {
        if (link.kind != BriefingLinkKind::OutputForwarding || link.toPin != kExitStartPin || !inRange(link.from))
            continue;

        if (std::any_of(out.starts.begin(), out.starts.end(),
                        [&](const ExitPin& p) { return SamePin(p, link.from, link.fromPin); }))
            continue;

        out.starts.push_back({link.from, link.fromPin, {}});
    }

    return out;
}

std::vector<ExitPin> FindFreelancerConfirmPins(int32_t hijacker, size_t count, const std::vector<BriefingLink>& links) {
    std::vector<ExitPin> out;
    const auto inRange = [&](int32_t index) { return index >= 0 && static_cast<size_t>(index) < count; };

    for (const BriefingLink& link : links) {
        if (link.kind != BriefingLinkKind::Connection || link.to != hijacker || link.toPin != kHijackerActivatePin ||
            !inRange(link.from))
            continue;

        if (std::any_of(out.begin(), out.end(), [&](const ExitPin& p) { return SamePin(p, link.from, link.fromPin); }))
            continue;

        out.push_back({link.from, link.fromPin, {}});
    }

    return out;
}

const KnownExitTemplate* FindKnownExitTemplate(uint64_t rid) {
    for (const KnownExitTemplate& t : kKnownTemplates) {
        if (rid != 0 && (t.temp == rid || t.tblu == rid))
            return &t;
    }

    return nullptr;
}

std::span<const BrokenExitSequences> KnownBrokenExitSequences() {
    return kBroken;
}

std::string_view KnownBrokenExit(std::string_view option, uint64_t exitId, std::span<const BrokenExitSequences> known) {
    for (const BrokenExitSequences& broken : known) {
        if (broken.option == option &&
            std::find(broken.exits.begin(), broken.exits.end(), exitId) != broken.exits.end())
            return broken.why;
    }

    return {};
}

std::span<const ExitSwitchOff> KnownExitSwitchOffs() {
    return kSwitchOffs;
}

const ExitSwitchOff* FindExitSwitchOff(std::span<const uint64_t> loadedBrickRids, uint64_t exitId,
                                       std::span<const ExitSwitchOff> known) {
    if (exitId == 0)
        return nullptr;

    for (const ExitSwitchOff& off : known) {
        if (off.exit != exitId)
            continue;

        for (const uint64_t rid : off.bricks) {
            if (rid != 0 && std::find(loadedBrickRids.begin(), loadedBrickRids.end(), rid) != loadedBrickRids.end())
                return &off;
        }
    }

    return nullptr;
}

std::string_view VariantExitActionText(VariantExitAction action) {
    switch (action) {
        case VariantExitAction::Route: return "routed into Freelancer's exit";
        case VariantExitAction::NotAnExit:
            return "not an exit (nothing triggers it; it only tells whether exits are open)";
        case VariantExitAction::KeepNoFreelancer: return "left as it is (no Freelancer exit to route it to)";
        case VariantExitAction::KeepUnreadable: return "left as it is (its wiring could not be read or fully blocked)";
    }

    return "?";
}

VariantExitPlan PlanVariantExits(std::span<const FreelancerExitFacts> freelancer,
                                 std::span<const VariantExitFacts> variant) {
    VariantExitPlan plan;

    for (size_t i = 0; i < freelancer.size() && !plan.hasVia; ++i) {
        if (!freelancer[i].inVariantBrick && !freelancer[i].switchedOff) {
            plan.hasVia = true;
            plan.via = i;
        }
    }

    for (size_t i = 0; i < freelancer.size() && !plan.hasVia; ++i) {
        if (!freelancer[i].switchedOff) {
            plan.hasVia = true;
            plan.via = i;
        }
    }

    if (!plan.hasVia && !freelancer.empty()) {
        plan.hasVia = true;
        plan.via = 0;
    }

    for (const VariantExitFacts& exit : variant) {
        VariantExitAction action = VariantExitAction::Route;

        if (exit.graphRead && exit.ends == 0 && exit.skipped == 0)
            action = VariantExitAction::NotAnExit;
        else if (!exit.graphRead || exit.ends == 0 || exit.skipped > 0)
            action = VariantExitAction::KeepUnreadable;
        else if (!plan.hasVia)
            action = VariantExitAction::KeepNoFreelancer;

        plan.actions.push_back(action);
        plan.routed += action == VariantExitAction::Route ? 1 : 0;
        plan.kept +=
            action == VariantExitAction::KeepNoFreelancer || action == VariantExitAction::KeepUnreadable ? 1 : 0;
    }

    return plan;
}

std::string_view DoneWhyText(DoneWhy why) {
    switch (why) {
        case DoneWhy::None: return "none";
        case DoneWhy::Broken: return "this variant deletes the exit's own sequence";
        case DoneWhy::Waited: return "the level had not ended on its own";
        case DoneWhy::Ended: return "the variant exit's own end came";
        case DoneWhy::Gap: return "the variant exit ended without a start; Freelancer's flow had its time";
    }

    return "?";
}

ExitSteps OnFreelancerConfirm(ExitClock& clock, size_t exit, double now, bool knownBroken) {
    ExitSteps steps;

    if (clock.phase != ExitPhase::Idle) {
        steps.ignored = true;
        return steps;
    }

    clock.phase = ExitPhase::Started;
    clock.source = ExitSource::Freelancer;
    clock.exit = exit;
    clock.startedAt = now;
    clock.doneAt = now + (knownBroken ? kExitGapSeconds : kExitEndWaitSeconds);
    clock.doneWhy = knownBroken ? DoneWhy::Broken : DoneWhy::Waited;
    return steps;
}

ExitSteps OnVariantStart(ExitClock& clock, size_t exit, double now) {
    ExitSteps steps;

    if (clock.phase != ExitPhase::Idle) {
        steps.ignored = true;
        return steps;
    }

    clock.phase = ExitPhase::Started;
    clock.source = ExitSource::Variant;
    clock.exit = exit;
    clock.startedAt = now;
    clock.doneAt = now + kExitEndWaitSeconds;
    clock.doneWhy = DoneWhy::Waited;
    steps.activate = true;
    return steps;
}

ExitSteps OnVariantEnd(ExitClock& clock, size_t exit, double now) {
    ExitSteps steps;

    if (clock.phase == ExitPhase::Idle) {
        // No start seen (an exit without a manual sequence): Freelancer's flow first, its end a little later.
        clock.phase = ExitPhase::Started;
        clock.source = ExitSource::Variant;
        clock.exit = exit;
        clock.startedAt = now;
        clock.doneAt = now + kExitGapSeconds;
        clock.doneWhy = DoneWhy::Gap;
        steps.activate = true;
        return steps;
    }

    if (clock.phase != ExitPhase::Started || clock.source != ExitSource::Variant || clock.exit != exit) {
        steps.ignored = true;
        return steps;
    }

    if (now - clock.startedAt >= kExitGapSeconds) {
        clock.phase = ExitPhase::Done;
        clock.doneAt = -1.0;
        clock.doneWhy = DoneWhy::Ended;
        steps.done = true;
        steps.why = DoneWhy::Ended;
        return steps;
    }

    clock.doneAt = clock.startedAt + kExitGapSeconds;
    clock.doneWhy = DoneWhy::Ended;
    return steps;
}

ExitSteps OnExitTick(ExitClock& clock, double now) {
    ExitSteps steps;

    if (clock.phase != ExitPhase::Started || clock.doneAt < 0.0 || now < clock.doneAt)
        return steps;

    clock.phase = ExitPhase::Done;
    clock.doneAt = -1.0;
    steps.done = true;
    steps.why = clock.doneWhy;
    return steps;
}

void OnMissionEnd(ExitClock& clock) {
    clock.phase = ExitPhase::Ended;
    clock.doneAt = -1.0;
}

bool IsLevelEndEvent(std::string_view name) {
    return name == "ContractEnd" || name == "ExitInventory";
}

} // namespace fr
