#include "doctest.h"

#include "VariantExits.h"
#include "Variants.h" // the Guru's and the Deceivers' bricks

#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace {

using LK = fr::BriefingLinkKind;

// levelflow.template?/exit_pushbutton (chunk0patch4 00EA1A647E6ADCF4, 35
// sub-entities), the links around its contract exit "Exit" (16): the manual
// sequence starts at ValueBool_Poll_UseManualTrigger01 (23) PollTrue,
// forwarded to the root's (34) Exit; the contract ends at
// ValueBool_Poll_IsExitOnManualAllowed (33) PollTrue (after the sequence, the
// root's Trigger) or ValueBool_Poll_IsExitAllowed (19) PollFalse (no manual
// sequence).
std::vector<fr::BriefingLink> PushButtonLinks() {
    return {
        {19, "PollFalse", 16, "Trigger", LK::Connection},
        {33, "PollTrue", 16, "Trigger", LK::Connection},
        {8, "Out", 23, "Poll", LK::Connection},
        {8, "Out", 19, "Poll", LK::Connection},
        {8, "Out", 22, "SetFalse", LK::Connection},
        {11, "Completed", 8, "In", LK::Connection},
        {11, "Completed", 17, "Send", LK::Connection},
        {6, "OnFalse", 25, "GetValue", LK::Connection},
        {34, "Trigger", 33, "Poll", LK::InputForwarding},
        {34, "VRTrigger", 14, "In", LK::InputForwarding},
        {23, "PollTrue", 34, "Exit", LK::OutputForwarding},
        {1, "OnValue", 34, "ExitPointEnabled", LK::OutputForwarding},
        {11, "Started", 34, "Started", LK::OutputForwarding},
    };
}

// setpieces_evergreen.template?/exit_evergreen_pushbutton (chunk0patch3
// 0004EDBC6981F932, 59 sub-entities), the links around its hijacker (4): the
// modal (55) confirmed and ForceActivateExit (29) Out signal exitActivated;
// the root's (58) Trigger is forwarded to sequenceDone.
std::vector<fr::BriefingLink> EvergreenLinks() {
    return {
        {55, "confirmed", 4, "exitActivated", LK::Connection}, {55, "confirmed", 43, "SetFalse", LK::Connection},
        {55, "confirmed", 46, "In", LK::Connection},           {29, "Out", 43, "SetFalse", LK::Connection},
        {29, "Out", 4, "exitActivated", LK::Connection},       {58, "Trigger", 4, "sequenceDone", LK::InputForwarding},
        {55, "confirmed", 58, "Exit", LK::OutputForwarding},   {29, "Out", 58, "Exit", LK::OutputForwarding},
    };
}

} // namespace

TEST_CASE("variant_exits_resources") {
    CHECK(fr::IsExitHijackerResource(fr::kExitHijackerTblu));
    CHECK(fr::IsExitHijackerResource(fr::kExitHijackerTemp));
    CHECK_FALSE(fr::IsExitHijackerResource(0));
    CHECK_FALSE(fr::IsExitHijackerResource(0x00EA1A647E6ADCF4ull));

    CHECK(fr::ExitTemplateName(0x00EA1A647E6ADCF4ull) == "exit_pushbutton");
    CHECK(fr::ExitTemplateName(0x006F4770376B274Eull) == "exit_pushbutton");
    CHECK(fr::ExitTemplateName(0x0004EDBC6981F932ull) == "exit_evergreen_pushbutton");
    CHECK(fr::ExitTemplateName(0x009AB36B427ED062ull) == "exit_evergreen_usekey");
    CHECK(fr::ExitTemplateName(0).empty());
    CHECK(fr::ExitTemplateName(0x002DAF48CF0FD7E6ull).empty()); // scenario_bellini, a brick

    CHECK(fr::ExitTemplateName(0x0037FF5183103A32ull) == "exit_evergreen_usekey"); // by TEMP too
}

TEST_CASE("variant_exits_known_templates") {
    const fr::KnownExitTemplate* push = fr::FindKnownExitTemplate(0x00EA1A647E6ADCF4ull);
    REQUIRE(push != nullptr);
    CHECK(fr::FindKnownExitTemplate(0x006F4770376B274Eull) == push);
    CHECK(push->anchor == 0x16BF13FFE1703300ull); // its ZContractsExitEntity "Exit"
    REQUIRE(push->ends.size() == 2);
    CHECK(push->ends[0].sub == 0xF61FB1A30A97B8E3ull);
    CHECK(push->ends[0].pin == "PollTrue");
    CHECK(push->ends[1].sub == 0x7ED437F56C459471ull);
    CHECK(push->ends[1].pin == "PollFalse");
    REQUIRE(push->starts.size() == 1);
    CHECK(push->starts[0].sub == 0x2ADB2685CDC54008ull);
    CHECK(push->confirms.empty());

    const fr::KnownExitTemplate* evergreen = fr::FindKnownExitTemplate(0x0004EDBC6981F932ull);
    REQUIRE(evergreen != nullptr);
    CHECK(evergreen->anchor == 0x7AD714FA457BD132ull); // its hijacker
    CHECK(evergreen->ends.empty());
    REQUIRE(evergreen->confirms.size() == 2);
    CHECK(evergreen->confirms[0].pin == "confirmed");
    CHECK(evergreen->confirms[1].pin == "Out");

    const fr::KnownExitTemplate* useKey = fr::FindKnownExitTemplate(0x0037FF5183103A32ull);
    REQUIRE(useKey != nullptr);
    CHECK(useKey->anchor == 0x492C010209533A1Dull);
    REQUIRE(useKey->confirms.size() == 1);

    CHECK(fr::FindKnownExitTemplate(0x00D73B9102BB6EA1ull) == nullptr); // exit_usekey: not decoded
    CHECK(fr::FindKnownExitTemplate(0) == nullptr);
}

TEST_CASE("variant_exits_pushbutton_pins") {
    const fr::VariantExitPins pins = fr::FindVariantExitPins(16, 35, PushButtonLinks());

    REQUIRE(pins.ends.size() == 2);
    CHECK(pins.ends[0].entity == 19);
    CHECK(pins.ends[0].pin == "PollFalse");
    CHECK(pins.ends[0].resend.empty()); // it feeds nothing else
    CHECK(pins.ends[1].entity == 33);
    CHECK(pins.ends[1].pin == "PollTrue");
    CHECK(pins.ends[1].resend.empty());
    REQUIRE(pins.starts.size() == 1);
    CHECK(pins.starts[0].entity == 23);
    CHECK(pins.starts[0].pin == "PollTrue");
    CHECK(pins.skipped == 0);
    // The interaction, the sequence and the root's forwardings are never blocked.
    for (const fr::ExitPin& pin : pins.ends) {
        CHECK(pin.entity != 34);
        CHECK(pin.entity != 11);
    }
}

TEST_CASE("variant_exits_pin_rules") {
    SUBCASE("a feeding pin's other links are sent on") {
        const std::vector<fr::BriefingLink> links {
            {0, "Out", 1, "Trigger", LK::Connection},
            {0, "Out", 2, "In", LK::Connection},
            {0, "Out", 3, "Done", LK::OutputForwarding},
            {0, "Other", 2, "In", LK::Connection},
        };
        const fr::VariantExitPins pins = fr::FindVariantExitPins(1, 4, links);
        REQUIRE(pins.ends.size() == 1);
        CHECK(pins.ends[0].resend == std::vector<size_t> {1, 2});
        CHECK(pins.skipped == 0);
    }

    SUBCASE("a Trigger fed from the template's own input cannot be blocked") {
        const std::vector<fr::BriefingLink> links {
            {3, "Trigger", 1, "Trigger", LK::InputForwarding},
            {0, "Out", 1, "Trigger", LK::Connection},
        };
        const fr::VariantExitPins pins = fr::FindVariantExitPins(1, 4, links);
        CHECK(pins.ends.size() == 1);
        CHECK(pins.skipped == 1);
        CHECK_FALSE(pins.skipWhy.empty());
    }

    SUBCASE("a pin with too many other links is skipped") {
        std::vector<fr::BriefingLink> links {{0, "Out", 1, "Trigger", LK::Connection}};

        for (int i = 0; i < static_cast<int>(fr::kLineMaxResend) + 1; ++i)
            links.push_back({0, "Out", 2 + i, "In", LK::Connection});

        const fr::VariantExitPins pins = fr::FindVariantExitPins(1, 20, links);
        CHECK(pins.ends.empty());
        CHECK(pins.skipped == 1);
    }

    SUBCASE("one pin into Trigger twice is one block; other pins of the exit are left") {
        const std::vector<fr::BriefingLink> links {
            {0, "Out", 1, "Trigger", LK::Connection},
            {0, "Out", 1, "Trigger", LK::Connection},
            {2, "Out", 1, "SetValue", LK::Connection},
        };
        const fr::VariantExitPins pins = fr::FindVariantExitPins(1, 3, links);
        REQUIRE(pins.ends.size() == 1);
        CHECK(pins.ends[0].resend.empty());
    }

    SUBCASE("nothing feeds the Trigger: not an exit") {
        const std::vector<fr::BriefingLink> links {{2, "Out", 3, "In", LK::Connection}};
        const fr::VariantExitPins pins = fr::FindVariantExitPins(1, 4, links);
        CHECK(pins.ends.empty());
        CHECK(pins.starts.empty());
        CHECK(pins.skipped == 0);
    }

    SUBCASE("an index out of range finds nothing") {
        CHECK(fr::FindVariantExitPins(9, 4, PushButtonLinks()).ends.empty());
        CHECK(fr::FindVariantExitPins(-1, 4, PushButtonLinks()).ends.empty());
    }
}

TEST_CASE("variant_exits_freelancer_confirm_pins") {
    const std::vector<fr::ExitPin> pins = fr::FindFreelancerConfirmPins(4, 59, EvergreenLinks());

    REQUIRE(pins.size() == 2);
    CHECK(pins[0].entity == 55);
    CHECK(pins[0].pin == "confirmed");
    CHECK(pins[1].entity == 29);
    CHECK(pins[1].pin == "Out");
    // The root's Trigger forwarding (sequenceDone) is not a confirmation.
    CHECK(fr::FindFreelancerConfirmPins(4, 59, {{58, "Trigger", 4, "exitActivated", LK::InputForwarding}}).empty());
}

TEST_CASE("variant_exits_known_broken") {
    // reflection's scenario_bellini deletes the sequences of override_shogi's five exits.
    CHECK_FALSE(fr::KnownBrokenExit("reflection", 0x88C6DBAC409FFE6Aull).empty()); // the cable car, d0c66e7b
    CHECK_FALSE(fr::KnownBrokenExit("reflection", 0x1464774A68528B34ull).empty()); // the north path, 08d5a5f2
    CHECK_FALSE(fr::KnownBrokenExit("reflection", 0xA64BA5D053FD8C7Eull).empty());
    CHECK_FALSE(fr::KnownBrokenExit("reflection", 0x26B240F303F218F7ull).empty());
    CHECK_FALSE(fr::KnownBrokenExit("reflection", 0x55BD6CCDAD588E93ull).empty());
    CHECK(fr::KnownBrokenExit("snow_festival", 0x88C6DBAC409FFE6Aull).empty());
    CHECK(fr::KnownBrokenExit("reflection", 0x47EB3B2D1859C514ull).empty()); // Bellini Exit, not Freelancer's
    CHECK(fr::KnownBrokenExit("reflection", 0).empty());

    // Lust, Envy and The Harbinger delete the sequences of the exits they keep usable.
    CHECK_FALSE(fr::KnownBrokenExit("lust", 0x2DDA760FAEF09E7Cull).empty()); // B2 Emergency Exit
    CHECK_FALSE(fr::KnownBrokenExit("lust", 0x6EA77A30C4C7828Aull).empty()); // Water Tower Road_Bike
    CHECK(fr::KnownBrokenExit("the_drop", 0x2DDA760FAEF09E7Cull).empty());
    CHECK(fr::KnownBrokenExit("the_vector", 0x07586868AF4A0819ull).empty());      // not an option, so not listed
    CHECK_FALSE(fr::KnownBrokenExit("envy", 0x8D0EB27E94F0C2CCull).empty());      // the grape fields
    CHECK(fr::KnownBrokenExit("envy", 0xAFE877B6BC4DABCCull).empty());            // its sequence stays
    CHECK_FALSE(fr::KnownBrokenExit("harbinger", 0x611EEFD9E524DC7Eull).empty()); // the boat
    CHECK(fr::KnownBrokenExit("garden_show", 0x611EEFD9E524DC7Eull).empty());
}

TEST_CASE("variant_exits_switch_off") {
    // exit_evergreen_pushbutton is switched off through ActionEnabled (its prompt) and Show Spatial (its icons).
    const fr::KnownExitTemplate* evergreen = fr::FindKnownExitTemplate(0x00E38E3006E744D0ull);
    REQUIRE(evergreen != nullptr);
    REQUIRE(evergreen->offs.size() == 2);
    CHECK(evergreen->offs[0].sub == 0xEEE30F74A0375367ull);
    CHECK(evergreen->offs[0].pin == "SetFalse");
    CHECK(evergreen->offs[1].sub == 0x2874DC3A4E18CA9Dull);
    CHECK(evergreen->offs[1].pin == "SetFalse");
    // exit_evergreen_usekey: the same pair, ActionEnabled and Show3dSpatial.
    const fr::KnownExitTemplate* useKey = fr::FindKnownExitTemplate(0x009AB36B427ED062ull);
    REQUIRE(useKey != nullptr);
    REQUIRE(useKey->offs.size() == 2);
    CHECK(useKey->offs[0].sub == 0x427C6DE31FA17225ull);
    CHECK(useKey->offs[0].pin == "SetFalse");
    CHECK(useKey->offs[1].sub == 0x58BD3ACE491635B9ull);
    CHECK(useKey->offs[1].pin == "SetFalse");
    CHECK(fr::FindKnownExitTemplate(0x00EA1A647E6ADCF4ull)->offs.empty()); // vanilla

    // Sapienza's road car exit (Exit_Evergreen_PushButton_03) under The Guru's brick, alone or through The
    // Deceivers' wrapper (which loads it with The Congressman's).
    constexpr uint64_t kRoadExit = 0xD5AA959458203ABFull;
    const uint64_t kHarveywallbanger = fr::kMissionHarveywallbangerBrick.rid;
    const uint64_t kCorpsereviver = fr::kScenarioCorpsereviverBrick.rid;
    const uint64_t kMintjulep = fr::kMissionMintjulepBrick.rid;
    const uint64_t kLocation = 0x00E9F09C3B030590ull; // coastaltown/location

    const uint64_t guru[] = {kLocation, kHarveywallbanger};
    const fr::ExitSwitchOff* off = fr::FindExitSwitchOff(guru, kRoadExit);
    REQUIRE(off != nullptr);
    CHECK(off->exit == kRoadExit);
    CHECK_FALSE(off->why.empty());

    const uint64_t deceivers[] = {kLocation, kCorpsereviver, kMintjulep, kHarveywallbanger};
    CHECK(fr::FindExitSwitchOff(deceivers, kRoadExit) == off);
    const uint64_t wrapperOnly[] = {kLocation, kCorpsereviver};
    CHECK(fr::FindExitSwitchOff(wrapperOnly, kRoadExit) == off);

    // Not without the Guru's brick (The Congressman alone keeps the car), and never another exit.
    const uint64_t congressman[] = {kLocation, kMintjulep};
    CHECK(fr::FindExitSwitchOff(congressman, kRoadExit) == nullptr);
    CHECK(fr::FindExitSwitchOff({}, kRoadExit) == nullptr);
    CHECK(fr::FindExitSwitchOff(guru, 0xC0632396F86D316Eull) == nullptr); // Exit_Evergreen_PushButton_01
    CHECK(fr::FindExitSwitchOff(guru, 0) == nullptr);
}

namespace {

// The Freelancer exits of a map's override brick that the given bricks leave on.
size_t ExitsLeft(std::span<const uint64_t> loaded, std::span<const uint64_t> exits) {
    size_t left = 0;

    for (const uint64_t exit : exits)
        left += fr::FindExitSwitchOff(loaded, exit) == nullptr ? 1 : 0;

    return left;
}

// Each map's Freelancer exits (the exit_evergreen_* instances of its override brick; the mod's exit walk, or
// tools/landslide/variant_audit.py).
constexpr uint64_t kShogi[] = {0x88C6DBAC409FFE6Aull, 0x1464774A68528B34ull, 0xA64BA5D053FD8C7Eull,
                               0x26B240F303F218F7ull, 0x55BD6CCDAD588E93ull};
constexpr uint64_t kBridge[] = {0xF1A9EF80B4754D59ull, 0x0D527460820C4056ull, 0x611EEFD9E524DC7Eull,
                                0x36D05A06ACB29565ull};
constexpr uint64_t kMeyer[] = {0x438C41798A384859ull, 0xD1C97406FAFD603Cull, 0x6EA77A30C4C7828Aull,
                               0x40C5BB702C67E371ull, 0xA19BAB1ACA3EE6B4ull, 0x8FE1D8EECF25154Aull,
                               0x91DC977AC5F3341Aull, 0x2DDA760FAEF09E7Cull};
constexpr uint64_t kSpades[] = {0x2E45AE06AF2463D1ull, 0xD7E465E2219283E9ull, 0x07586868AF4A0819ull,
                                0xA1820E850516D412ull, 0x4AF0DB2758707C42ull, 0x32580375CC23DAC9ull};
constexpr uint64_t kSapienza[] = {0xD5AA959458203ABFull, 0xC0632396F86D316Eull, 0x08DB2FE9103A73CFull,
                                  0x6721EBE4C0FDED96ull};

} // namespace

TEST_CASE("variant_exits_switch_off_per_variant") {
    SUBCASE("the reflection: the sealed cable car exit (exit 02) only") {
        const uint64_t loaded[] = {fr::kScenarioBelliniBrick.rid};
        const fr::ExitSwitchOff* off = fr::FindExitSwitchOff(loaded, 0x88C6DBAC409FFE6Aull);
        REQUIRE(off != nullptr);
        CHECK(off->why.find("cable car") != std::string_view::npos);
        CHECK(ExitsLeft(loaded, kShogi) == 4);
        const uint64_t snowFestival[] = {fr::kScenarioMamushiBrick.rid};
        CHECK(ExitsLeft(snowFestival, kShogi) == 5);
    }

    SUBCASE("dartmoor: the garden show leaves two Freelancer exits, the harbinger and sloth three") {
        const uint64_t garden[] = {fr::kScenarioSmoothsnakeBrick.rid};
        CHECK(ExitsLeft(garden, kBridge) == 2); // the two motorcycle push buttons (and the bridge's area exit)
        CHECK(fr::FindExitSwitchOff(garden, 0x611EEFD9E524DC7Eull) != nullptr);
        const fr::ExitSwitchOff* hearse = fr::FindExitSwitchOff(garden, 0x36D05A06ACB29565ull);
        REQUIRE(hearse != nullptr);
        CHECK(hearse->why.find("hearse") != std::string_view::npos);
        CHECK(fr::FindExitSwitchOff(garden, 0xF1A9EF80B4754D59ull) == nullptr);
        CHECK(fr::FindExitSwitchOff(garden, 0x0D527460820C4056ull) == nullptr);
        const uint64_t harbinger[] = {fr::kScenarioFilurBrick.rid};
        CHECK(ExitsLeft(harbinger, kBridge) == 3);
        CHECK(fr::FindExitSwitchOff(harbinger, 0x36D05A06ACB29565ull) != nullptr);
        const uint64_t sloth[] = {fr::kAncestralHarebellBrick.rid};
        CHECK(ExitsLeft(sloth, kBridge) == 3);
        CHECK(fr::FindExitSwitchOff(sloth, 0x36D05A06ACB29565ull) != nullptr); // the hearse is gone, its key is not
    }

    SUBCASE("berlin: lust keeps its three push buttons without a key or a missing switch") {
        const uint64_t lust[] = {fr::kScenarioAmbrosiaBrick.rid};
        CHECK(ExitsLeft(lust, kMeyer) == 3);
        CHECK(fr::FindExitSwitchOff(lust, 0x2DDA760FAEF09E7Cull) == nullptr); // B2 Emergency Exit
        CHECK(fr::FindExitSwitchOff(lust, 0x6EA77A30C4C7828Aull) == nullptr); // Water Tower Road_Bike
        CHECK(fr::FindExitSwitchOff(lust, 0x8FE1D8EECF25154Aull) == nullptr); // Moon Rider
        const uint64_t drop[] = {fr::kElusiveTomorrowlandBrick.rid};
        CHECK(ExitsLeft(drop, kMeyer) == 8);
    }

    SUBCASE("colorado: no option switches an exit off (The Vector, which did, was removed)") {
        const uint64_t vector[] = {0x002BF7B0E490475Cull}; // colorado_2/scenario_rabies
        CHECK(ExitsLeft(vector, kSpades) == 6);
    }

    SUBCASE("sapienza: the guru keeps three") {
        const uint64_t guru[] = {fr::kMissionHarveywallbangerBrick.rid};
        CHECK(ExitsLeft(guru, kSapienza) == 3);
    }

    SUBCASE("every switch-off names a brick and a reason") {
        for (const fr::ExitSwitchOff& off : fr::KnownExitSwitchOffs()) {
            CHECK(off.exit != 0);
            CHECK_FALSE(off.bricks.empty());
            CHECK_FALSE(off.why.empty());
        }
    }
}

TEST_CASE("variant_exits_plan_all_switched_off") {
    // Every Freelancer exit switched off (no option does this now; the removed Wrath did): a variant exit is still
    // routed through the first one's hijacker.
    std::vector<fr::FreelancerExitFacts> freelancer;

    for (const uint64_t id : kBridge)
        freelancer.push_back({id, false, 2, false, true});

    const std::vector<fr::VariantExitFacts> variant {{0xBE738810AD3B2FE4ull, true, 2, 1, 0}};
    const fr::VariantExitPlan plan = fr::PlanVariantExits(freelancer, variant);
    CHECK(plan.hasVia);
    CHECK(plan.via == 0);
    REQUIRE(plan.actions.size() == 1);
    CHECK(plan.actions[0] == fr::VariantExitAction::Route);
    CHECK(plan.kept == 0);
}

TEST_CASE("variant_exits_plan") {
    SUBCASE("reflection: five Freelancer exits, Bellini Exit routed, the brick's exit condition is not an exit") {
        std::vector<fr::FreelancerExitFacts> freelancer;

        for (const uint64_t id : {0x88C6DBAC409FFE6Aull, 0x1464774A68528B34ull, 0xA64BA5D053FD8C7Eull})
            freelancer.push_back({id, false, 2, true});

        const std::vector<fr::VariantExitFacts> variant {
            {0x47EB3B2D1859C514ull, true, 2, 1, 0}, // Bellini Exit
            {0x041C609C94A400B6ull, true, 0, 0, 0}, // "Exit Points and UI"'s ZContractsExitEntity: a condition only
        };
        const fr::VariantExitPlan plan = fr::PlanVariantExits(freelancer, variant);

        CHECK(plan.hasVia);
        CHECK(plan.via == 0);
        REQUIRE(plan.actions.size() == 2);
        CHECK(plan.actions[0] == fr::VariantExitAction::Route);
        CHECK(plan.actions[1] == fr::VariantExitAction::NotAnExit);
        CHECK(plan.routed == 1);
        CHECK(plan.kept == 0);
    }

    SUBCASE("no Freelancer exit: the variant's exits are kept, never switched off") {
        const std::vector<fr::VariantExitFacts> variant {{1, true, 2, 1, 0}};
        const fr::VariantExitPlan plan = fr::PlanVariantExits({}, variant);

        CHECK_FALSE(plan.hasVia);
        REQUIRE(plan.actions.size() == 1);
        CHECK(plan.actions[0] == fr::VariantExitAction::KeepNoFreelancer);
        CHECK(plan.routed == 0);
        CHECK(plan.kept == 1);
    }

    SUBCASE("an exit whose wiring is unread, or only partly blockable, is kept") {
        const std::vector<fr::FreelancerExitFacts> freelancer {{1, false, 1, false}};
        const std::vector<fr::VariantExitFacts> variant {
            {2, false, 0, 0, 0}, // unread
            {3, true, 1, 0, 1},  // one feed cannot be blocked
            {4, true, 0, 0, 1},  // its only feed cannot be blocked
        };
        const fr::VariantExitPlan plan = fr::PlanVariantExits(freelancer, variant);

        REQUIRE(plan.actions.size() == 3);
        CHECK(plan.actions[0] == fr::VariantExitAction::KeepUnreadable);
        CHECK(plan.actions[1] == fr::VariantExitAction::KeepUnreadable);
        CHECK(plan.actions[2] == fr::VariantExitAction::KeepUnreadable);
        CHECK(plan.kept == 3);
    }

    SUBCASE("the Freelancer exit routed to is the first outside the variant's bricks") {
        const std::vector<fr::FreelancerExitFacts> freelancer {{1, true, 1, false}, {2, false, 1, false}};
        const fr::VariantExitPlan plan = fr::PlanVariantExits(freelancer, {});
        CHECK(plan.hasVia);
        CHECK(plan.via == 1);

        const std::vector<fr::FreelancerExitFacts> onlyVariant {{1, true, 1, false}};
        const fr::VariantExitPlan fallback = fr::PlanVariantExits(onlyVariant, {});
        CHECK(fallback.hasVia);
        CHECK(fallback.via == 0);
    }

    SUBCASE("a switched-off Freelancer exit is routed to only when there is no other") {
        const std::vector<fr::FreelancerExitFacts> freelancer {
            {1, false, 2, false, true}, {2, true, 2, false}, {3, false, 2, false}};
        CHECK(fr::PlanVariantExits(freelancer, {}).via == 2);

        const std::vector<fr::FreelancerExitFacts> inVariant {{1, false, 2, false, true}, {2, true, 2, false}};
        CHECK(fr::PlanVariantExits(inVariant, {}).via == 1);

        const std::vector<fr::FreelancerExitFacts> onlyOff {{1, false, 2, false, true}};
        const fr::VariantExitPlan plan = fr::PlanVariantExits(onlyOff, {});
        CHECK(plan.hasVia);
        CHECK(plan.via == 0);
    }
}

TEST_CASE("variant_exits_clock_variant_exit") {
    SUBCASE("start, then its own end: exitActivated at the start, sequenceDone at the end") {
        fr::ExitClock clock;
        const fr::ExitSteps start = fr::OnVariantStart(clock, 0, 100.0);
        CHECK(start.activate);
        CHECK_FALSE(start.done);
        CHECK(clock.phase == fr::ExitPhase::Started);
        CHECK(clock.source == fr::ExitSource::Variant);

        CHECK_FALSE(fr::OnExitTick(clock, 105.0).done);

        const fr::ExitSteps end = fr::OnVariantEnd(clock, 0, 109.0);
        CHECK_FALSE(end.activate);
        CHECK(end.done);
        CHECK(end.why == fr::DoneWhy::Ended);
        CHECK(clock.phase == fr::ExitPhase::Done);
        CHECK_FALSE(fr::OnExitTick(clock, 200.0).done); // sent once
    }

    SUBCASE("its end right after its start: sequenceDone waits for the gap") {
        fr::ExitClock clock;
        fr::OnVariantStart(clock, 0, 100.0);
        const fr::ExitSteps end = fr::OnVariantEnd(clock, 0, 101.0);
        CHECK_FALSE(end.done);
        CHECK_FALSE(fr::OnExitTick(clock, 102.9).done);
        const fr::ExitSteps tick = fr::OnExitTick(clock, 100.0 + fr::kExitGapSeconds);
        CHECK(tick.done);
        CHECK(tick.why == fr::DoneWhy::Ended);
    }

    SUBCASE("an end without a start: exitActivated now, sequenceDone after the gap") {
        fr::ExitClock clock;
        const fr::ExitSteps end = fr::OnVariantEnd(clock, 2, 50.0);
        CHECK(end.activate);
        CHECK_FALSE(end.done);
        CHECK(clock.exit == 2);
        CHECK_FALSE(fr::OnExitTick(clock, 52.0).done);
        const fr::ExitSteps tick = fr::OnExitTick(clock, 53.0);
        CHECK(tick.done);
        CHECK(tick.why == fr::DoneWhy::Gap);
    }

    SUBCASE("a start whose own end never comes: sequenceDone after the wait") {
        fr::ExitClock clock;
        fr::OnVariantStart(clock, 0, 10.0);
        CHECK_FALSE(fr::OnExitTick(clock, 10.0 + fr::kExitEndWaitSeconds - 0.1).done);
        const fr::ExitSteps tick = fr::OnExitTick(clock, 10.0 + fr::kExitEndWaitSeconds);
        CHECK(tick.done);
        CHECK(tick.why == fr::DoneWhy::Waited);
    }

    SUBCASE("the level ends first: nothing more is sent") {
        fr::ExitClock clock;
        fr::OnVariantStart(clock, 0, 10.0);
        fr::OnMissionEnd(clock);
        CHECK_FALSE(fr::OnExitTick(clock, 100.0).done);
        CHECK(fr::OnVariantEnd(clock, 0, 100.0).ignored);
    }

    SUBCASE("a second exit while one is in progress is ignored") {
        fr::ExitClock clock;
        fr::OnVariantStart(clock, 0, 10.0);
        CHECK(fr::OnVariantStart(clock, 1, 11.0).ignored);
        CHECK(fr::OnVariantEnd(clock, 1, 12.0).ignored);
        CHECK(fr::OnFreelancerConfirm(clock, 0, 12.0, true).ignored);
        CHECK(clock.exit == 0);
    }
}

TEST_CASE("variant_exits_clock_freelancer_exit") {
    SUBCASE("a sequence the variant deletes: sequenceDone after the gap") {
        fr::ExitClock clock;
        const fr::ExitSteps confirm = fr::OnFreelancerConfirm(clock, 3, 20.0, true);
        CHECK_FALSE(confirm.activate); // Freelancer's own exit sends exitActivated
        CHECK_FALSE(confirm.done);
        CHECK(clock.source == fr::ExitSource::Freelancer);
        CHECK(clock.exit == 3);
        CHECK_FALSE(fr::OnExitTick(clock, 22.0).done);
        const fr::ExitSteps tick = fr::OnExitTick(clock, 23.0);
        CHECK(tick.done);
        CHECK(tick.why == fr::DoneWhy::Broken);
    }

    SUBCASE("a working exit: the level ends on its own, nothing is sent") {
        fr::ExitClock clock;
        fr::OnFreelancerConfirm(clock, 0, 20.0, false);
        CHECK_FALSE(fr::OnExitTick(clock, 28.0).done);
        fr::OnMissionEnd(clock);
        CHECK_FALSE(fr::OnExitTick(clock, 60.0).done);
    }

    SUBCASE("a working exit whose level does not end: sequenceDone after the wait") {
        fr::ExitClock clock;
        fr::OnFreelancerConfirm(clock, 0, 20.0, false);
        CHECK_FALSE(fr::OnExitTick(clock, 34.9).done);
        const fr::ExitSteps tick = fr::OnExitTick(clock, 35.0);
        CHECK(tick.done);
        CHECK(tick.why == fr::DoneWhy::Waited);
    }

    SUBCASE("a variant exit's end during a Freelancer exit is ignored (still blocked by the caller)") {
        fr::ExitClock clock;
        fr::OnFreelancerConfirm(clock, 0, 20.0, true);
        const fr::ExitSteps end = fr::OnVariantEnd(clock, 0, 21.0);
        CHECK(end.ignored);
        CHECK_FALSE(end.activate);
        CHECK_FALSE(end.done);
    }
}

TEST_CASE("variant_exits_texts_and_events") {
    CHECK(fr::IsLevelEndEvent("ContractEnd"));
    CHECK(fr::IsLevelEndEvent("ExitInventory"));
    CHECK_FALSE(fr::IsLevelEndEvent("exit_gate"));
    CHECK_FALSE(fr::IsLevelEndEvent("EvergreenExitTriggered"));

    for (const fr::VariantExitAction action :
         {fr::VariantExitAction::Route, fr::VariantExitAction::NotAnExit, fr::VariantExitAction::KeepNoFreelancer,
          fr::VariantExitAction::KeepUnreadable})
        CHECK_FALSE(fr::VariantExitActionText(action).empty());

    for (const fr::DoneWhy why : {fr::DoneWhy::Broken, fr::DoneWhy::Waited, fr::DoneWhy::Ended, fr::DoneWhy::Gap})
        CHECK_FALSE(fr::DoneWhyText(why).empty());

    // The gap is shorter than the wait, and the wait longer than the longest exit seen (10 s).
    CHECK(fr::kExitGapSeconds < fr::kExitEndWaitSeconds);
    CHECK(fr::kExitEndWaitSeconds > 10.0);
}

TEST_CASE("variant exits: every switch-off's brick is a variant option's brick") {
    // VariantExits.cpp types the bricks' rids again; a rid that drifted from
    // Variants.cpp's would silently stop matching.
    std::set<uint64_t> s_OptionBricks;

    for (const fr::VariantOption* s_Option : fr::AllVariantOptions()) {
        for (const fr::VariantBrick* s_Brick : s_Option->bricks)
            s_OptionBricks.insert(s_Brick->rid);
    }

    REQUIRE_FALSE(fr::KnownExitSwitchOffs().empty());

    for (const fr::ExitSwitchOff& s_Off : fr::KnownExitSwitchOffs()) {
        for (const uint64_t s_Rid : s_Off.bricks) {
            INFO(s_Off.why);
            CHECK(s_OptionBricks.contains(s_Rid));
        }
    }
}
