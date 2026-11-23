#include "doctest.h"

#include "support/StoryTestUtil.h"

#include "MissionStories.h"

#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("mission_stories_resources_and_pins") {
    CHECK(fr::IsOpportunityResource(fr::kOpportunityTblu));
    CHECK(fr::IsOpportunityResource(fr::kOpportunityTemp));
    CHECK_FALSE(fr::IsOpportunityResource(fr::kDianaSpeakTblu));
    CHECK_FALSE(fr::IsOpportunityResource(0));

    CHECK(fr::IsStoryInputPin("Unlock"));
    CHECK(fr::IsStoryInputPin("SetTracked"));
    CHECK_FALSE(fr::IsStoryInputPin("unlock"));
    CHECK_FALSE(fr::IsStoryInputPin("Trigger"));
}

// The Splitter's scenario_lambic, as decoded: under "Mission
// Story" (1959), GameEventListener (2748) starts TimerSimple (2006, 33 ms) and
// TimerSimple 2 (3757, 500 ms); their Out pins reach "Meet your brother"
// (3661, the opportunity) at Unlock and SetTracked. Step "OpportunityStepSpatial"
// (835) gets SetIconEnabled from a setpiece (2399), which stays.
TEST_CASE("mission_stories_splitter") {
    using LK = fr::BriefingLinkKind;
    const std::vector<fr::BriefingLink> links {
        {2748, "EventOccurred", 2006, "In", LK::Connection},
        {2748, "EventOccurred", 3757, "In", LK::Connection},
        {2006, "Out", 3661, "Unlock", LK::Connection},
        {3757, "Out", 3661, "SetTracked", LK::Connection},
        {2399, "OnEntering", 835, "SetIconEnabled", LK::Connection},
        {2399, "OnLeft", 835, "SetIconEnabled", LK::Connection},
    };
    const fr::StoryFind find = fr::FindStoryMutes({3661}, 3907, links);

    REQUIRE(find.mutes.size() == 2);
    CHECK(find.mutes[0].entity == 2006);
    CHECK(find.mutes[0].pin == "Out");
    CHECK(find.mutes[0].stories == std::vector<int32_t> {3661});
    CHECK(find.mutes[0].storyPins == std::vector<std::string> {"Unlock"});
    CHECK(find.mutes[0].resend.empty());
    CHECK(find.mutes[1].entity == 3757);
    CHECK(find.mutes[1].storyPins == std::vector<std::string> {"SetTracked"});
    CHECK(find.mutes[1].resend.empty());
    CHECK(find.skipped.empty());
    CHECK(find.unfed.empty());
    CHECK(fr::test::StorySwitchedOff(find, 3661));
    // The listener's EventOccurred is never blocked: only the pins into the story.
    for (const fr::StoryMute& mute : find.mutes)
        CHECK(mute.entity != 2748);
}

TEST_CASE("mission_stories_rules") {
    using LK = fr::BriefingLinkKind;

    SUBCASE("the feeding pin's other logic is sent on") {
        // 0 Out -> story 2 Unlock, 0 Out -> 1 In, 0 Out forwarded as 3's Done; 0 Other -> 1 is another pin.
        const std::vector<fr::BriefingLink> links {
            {0, "Out", 1, "In", LK::Connection},
            {0, "Out", 2, "Unlock", LK::Connection},
            {0, "Out", 3, "Done", LK::OutputForwarding},
            {0, "Other", 1, "In", LK::Connection},
        };
        const fr::StoryFind find = fr::FindStoryMutes({2}, 4, links);
        REQUIRE(find.mutes.size() == 1);
        CHECK(find.mutes[0].resend == std::vector<size_t> {0, 2});
        CHECK(fr::test::StorySwitchedOff(find, 2));
    }

    SUBCASE("one pin into two stories, and into two pins of one") {
        const std::vector<fr::BriefingLink> links {
            {0, "Out", 1, "Unlock", LK::Connection},
            {0, "Out", 2, "Unlock", LK::Connection},
            {0, "Out", 2, "SetTracked", LK::Connection},
        };
        const fr::StoryFind find = fr::FindStoryMutes({1, 2}, 3, links);
        REQUIRE(find.mutes.size() == 1);
        CHECK(find.mutes[0].stories == std::vector<int32_t> {1, 2, 2});
        CHECK(find.mutes[0].storyPins == std::vector<std::string> {"Unlock", "Unlock", "SetTracked"});
        CHECK(find.mutes[0].resend.empty());
        CHECK(fr::test::StorySwitchedOff(find, 1));
        CHECK(fr::test::StorySwitchedOff(find, 2));
    }

    SUBCASE("any input pin counts") {
        const fr::StoryFind find = fr::FindStoryMutes({1}, 2, {{0, "OnTrue", 1, "Reveal", LK::Connection}});
        REQUIRE(find.mutes.size() == 1);
        CHECK(find.mutes[0].pin == "OnTrue");
    }

    SUBCASE("a story's own outputs are not feeds") {
        // The story's output forwarded to the root, and the story starting other logic: nothing to block.
        const std::vector<fr::BriefingLink> links {
            {1, "Completed", 0, "Completed", LK::OutputForwarding},
            {1, "Unlocked", 2, "In", LK::Connection},
        };
        const fr::StoryFind find = fr::FindStoryMutes({1}, 3, links);
        CHECK(find.mutes.empty());
        CHECK(find.unfed == std::vector<int32_t> {1});
        CHECK_FALSE(fr::test::StorySwitchedOff(find, 1));
    }

    SUBCASE("output forwardings into a story are not feeds") {
        const fr::StoryFind find = fr::FindStoryMutes({1}, 2, {{0, "Out", 1, "Unlock", LK::OutputForwarding}});
        CHECK(find.mutes.empty());
        CHECK(find.unfed == std::vector<int32_t> {1});
    }

    SUBCASE("an input forwarding is left on and reported") {
        // The blueprint's own Unlock input (its root, 0) forwarded into the story; a connection feeds it too.
        const std::vector<fr::BriefingLink> links {
            {0, "Unlock", 1, "Unlock", LK::InputForwarding},
            {2, "Out", 1, "SetTracked", LK::Connection},
        };
        const fr::StoryFind find = fr::FindStoryMutes({1}, 3, links);
        REQUIRE(find.skipped.size() == 1);
        CHECK(find.skipped[0].why == fr::StorySkipWhy::Forwarded);
        CHECK(find.skipped[0].storyPin == "Unlock");
        REQUIRE(find.mutes.size() == 1);
        CHECK(find.mutes[0].entity == 2);
        CHECK(find.unfed.empty());
        CHECK_FALSE(fr::test::StorySwitchedOff(find, 1));
    }

    SUBCASE("a pin with more than kLineMaxResend other links is left on") {
        std::vector<fr::BriefingLink> links {{0, "Out", 1, "Unlock", LK::Connection}};

        for (size_t i = 0; i <= fr::kLineMaxResend; ++i)
            links.push_back({0, "Out", static_cast<int32_t>(2 + i), "In", LK::Connection});

        const fr::StoryFind find = fr::FindStoryMutes({1}, 2 + fr::kLineMaxResend + 1, links);
        CHECK(find.mutes.empty());
        REQUIRE(find.skipped.size() == 1);
        CHECK(find.skipped[0].why == fr::StorySkipWhy::TooManyLinks);
        CHECK(find.skipped[0].feeder == 0);
        CHECK_FALSE(fr::test::StorySwitchedOff(find, 1));

        links.pop_back(); // exactly kLineMaxResend others: blocked
        const fr::StoryFind fits = fr::FindStoryMutes({1}, 2 + fr::kLineMaxResend, links);
        REQUIRE(fits.mutes.size() == 1);
        CHECK(fits.mutes[0].resend.size() == fr::kLineMaxResend);
    }

    SUBCASE("unfed stories and links outside the blueprint") {
        const std::vector<fr::BriefingLink> links {
            {-1, "Out", 1, "Unlock", LK::Connection},
            {7, "Out", 1, "Unlock", LK::Connection},
        };
        const fr::StoryFind find = fr::FindStoryMutes({1, 2, 9}, 3, links);
        CHECK(find.mutes.empty());
        CHECK(find.unfed == std::vector<int32_t> {1, 2}); // 9 is outside the blueprint
    }
}
