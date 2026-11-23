#include "doctest.h"

#include "support/StoryTestUtil.h"

#include "MissionStories.h"
#include "VariantComments.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

TEST_CASE("variant comments: the hint class, its pins and the vitality helper") {
    CHECK(fr::IsHintClass("ZHintEntity"));
    CHECK(fr::IsHintClass("zhintentity"));
    CHECK_FALSE(fr::IsHintClass("ZHintManagerEntity"));
    CHECK_FALSE(fr::IsHintClass("ZIntelEntity"));
    CHECK_FALSE(fr::IsHintClass(""));

    CHECK(fr::kHintStartedPin == "OnHintStarted");
    CHECK(fr::IsHintQueuePin("QueueHintWithoutContext"));
    CHECK(fr::IsHintQueuePin("QueueHint"));
    CHECK_FALSE(fr::IsHintQueuePin("UnqueueHint"));
    CHECK_FALSE(fr::IsHintQueuePin("HintCompleted"));

    CHECK(fr::IsVitalityResource(fr::kHarebellVitalityTblu));
    CHECK(fr::IsVitalityResource(fr::kHarebellVitalityTemp));
    CHECK_FALSE(fr::IsVitalityResource(fr::kOpportunityTblu));
    CHECK_FALSE(fr::IsVitalityResource(0));
}

// Sloth's ancestral_harebell, as decoded: "Helper_Harebell_Vitality"
// (2391) is fed at AddRemove by ten pins; four of them also start other logic.
// "Poll vitality" (2392) reads it by property, not by a pin, and stays.
TEST_CASE("variant comments: Sloth's vitality feeds are blocked, their other logic sent on") {
    using LK = fr::BriefingLinkKind;
    const std::vector<fr::BriefingLink> links {
        {1216, "Change", 2391, "AddRemove", LK::Connection}, // Movement cost
        {2321, "Change", 2391, "AddRemove", LK::Connection}, // Takedown cost
        {2321, "Change", 1229, "Variant", LK::Connection},   // -> Float to void
        {2695, "Change", 2391, "AddRemove", LK::Connection}, // Agility cost
        {2695, "Change", 1229, "Variant", LK::Connection},
        {1936, "Change", 2391, "AddRemove", LK::Connection}, // Shooting cost
        {1936, "Change", 1229, "Variant", LK::Connection},
        {21, "OnTrue", 2391, "AddRemove", LK::Connection},    // Graceful throwing
        {21, "OnTrue", 2789, "Trigger", LK::Connection},      // -> Damage effect
        {477, "OnTrue", 2391, "AddRemove", LK::Connection},   // Jogger eliminated or pacified
        {477, "OnTrue", 1454, "Trigger", LK::Connection},     // -> JoggerCompleted
        {765, "OnTrue", 2391, "AddRemove", LK::Connection},   // Graceful body dumping
        {1598, "OnTrue", 2391, "AddRemove", LK::Connection},  // Graceful pushing
        {1851, "OnTrue", 2391, "AddRemove", LK::Connection},  // Graceful melee
        {2311, "OnTrue", 2391, "AddRemove", LK::Connection},  // Graceful kicking
        {517, "EventOccurred", 2392, "Poll", LK::Connection}, // Intro cut start -> Poll vitality
        {1854, "OnValue", 327, "Value", LK::Connection},      // Vitality -> Reach 90
        {327, "WentBelowMin", 1478, "QueueHintWithoutContext", LK::Connection},
    };
    const fr::StoryFind find = fr::FindStoryMutes({2391}, 3300, links);

    REQUIRE(find.mutes.size() == 10);
    CHECK(find.skipped.empty());
    CHECK(fr::test::StorySwitchedOff(find, 2391));

    for (const fr::StoryMute& mute : find.mutes) {
        CHECK(mute.storyPins == std::vector<std::string> {"AddRemove"});
        CHECK(mute.entity != 517);
        CHECK(mute.entity != 1854);
        CHECK(mute.entity != 327);
    }

    CHECK(find.mutes[0].entity == 1216);
    CHECK(find.mutes[0].resend.empty());
    CHECK(find.mutes[1].entity == 2321);
    CHECK(find.mutes[1].resend == std::vector<size_t> {2});
    CHECK(find.mutes[4].entity == 21);
    CHECK(find.mutes[4].pin == "OnTrue");
    CHECK(find.mutes[4].resend == std::vector<size_t> {8});
    CHECK(find.mutes[5].entity == 477);
    CHECK(find.mutes[5].resend == std::vector<size_t> {10});
}

// Envy's scenario_frangipani and Lust's scenario_ambrosia, as decoded.
TEST_CASE("variant comments: Envy's and Lust's lines by their ids") {
    const std::span<const fr::SinLine> known = fr::KnownSinLines();
    REQUIRE(known.size() == 22);
    CHECK(std::count_if(known.begin(), known.end(), [](const fr::SinLine& line) { return line.sin == "Envy"; }) == 9);
    CHECK(std::count_if(known.begin(), known.end(), [](const fr::SinLine& line) { return line.sin == "Lust"; }) == 13);

    std::set<uint64_t> ids;

    for (const fr::SinLine& line : known) {
        CHECK(line.id != 0);
        ids.insert(line.id);
    }

    CHECK(ids.size() == known.size());

    const fr::SinLine* warn = fr::FindSinLine(0xB27EA5538110A67Full);
    REQUIRE(warn);
    CHECK(warn->sin == "Envy");
    CHECK(warn->name == "AOL_pdlc44007_rivalkillwarn");

    const fr::SinLine* rules = fr::FindSinLine(0xFEBA17DF6255C7F8ull);
    REQUIRE(rules);
    CHECK(rules->sin == "Lust");
    CHECK(rules->name == "pdlc42001_introrules");

    CHECK_FALSE(fr::FindSinLine(0));
    CHECK_FALSE(fr::FindSinLine(0xFF3043C1C869C7ADull)); // Envy's "Envy is Talking" flag, not a line
    CHECK_FALSE(fr::FindSinLine(0xB27EA5538110A67Full, {}));

    CHECK(fr::IsLineEndPin("Completed"));
    CHECK(fr::IsLineEndPin("OnDone"));
    CHECK_FALSE(fr::IsLineEndPin("Start"));
    CHECK_FALSE(fr::IsLineEndPin("WaitDone"));
}

// Envy's "HandlerSpeak": each line starts from its own event receiver, which
// also sets "Envy is Talking" (2090); the line's end clears it.
TEST_CASE("variant comments: Envy's lines are skipped as if they had played") {
    using LK = fr::BriefingLinkKind;
    const std::vector<fr::BriefingLink> links {
        {143, "EventReceived", 2090, "SetTrue", LK::Connection},  // 0
        {143, "EventReceived", 552, "Start", LK::Connection},     // 1: rivalkillwarn
        {552, "Completed", 2090, "SetFalse", LK::Connection},     // 2
        {1659, "EventReceived", 2090, "SetTrue", LK::Connection}, // 3
        {1659, "EventReceived", 350, "Start", LK::Connection},    // 4: 47highscore (a sweetdialog)
        {350, "OnDone", 2090, "SetFalse", LK::Connection},        // 5
        {1889, "OnFalse", 1061, "Stop", LK::Connection},          // the rival's trash talk: not a line
    };
    const fr::StoryFind find = fr::FindSinLineMutes({552, 350}, 2126, links);

    REQUIRE(find.mutes.size() == 2);
    CHECK(find.skipped.empty());
    CHECK(find.unfed.empty());

    CHECK(find.mutes[0].entity == 143);
    CHECK(find.mutes[0].pin == "EventReceived");
    CHECK(find.mutes[0].stories == std::vector<int32_t> {552});
    CHECK(find.mutes[0].storyPins == std::vector<std::string> {"Start"});
    CHECK(find.mutes[0].resend == std::vector<size_t> {0, 2}); // set, then cleared

    CHECK(find.mutes[1].entity == 1659);
    CHECK(find.mutes[1].resend == std::vector<size_t> {3, 5});
}

// Lust's lines: the intro dream from the intro cut's OnPlay (with its
// effects), the rules from a timer, the others from their drama situation's
// proxy, whose Done their end signals.
TEST_CASE("variant comments: Lust's lines end their drama situations at once") {
    using LK = fr::BriefingLinkKind;
    const std::vector<fr::BriefingLink> links {
        {1277, "OnPlay", 1243, "SetTrue", LK::Connection},            // 0: DreamSeq_Sound
        {1277, "OnPlay", 339, "EnterSafeZoneNoAnim", LK::Connection}, // 1
        {1277, "OnPlay", 625, "TransmitTrue", LK::Connection},        // 2: VFX_DREAM
        {1277, "OnPlay", 483, "Start", LK::Connection},               // 3: introdream
        {1277, "OnPlay", 631, "SetTrue", LK::Connection},             // 4
        {1277, "OnSkip", 918, "StopSpeak", LK::Connection},           // 5: HandlerStandIn
        {1648, "OnStart", 122, "In", LK::Connection},                 // 6: proxy -> TimerSimple
        {122, "Out", 1269, "Start", LK::Connection},                  // 7: introrules
        {1269, "OnDone", 1648, "Done", LK::Connection},               // 8
        {539, "OnStart", 47, "Start", LK::Connection},                // 9: gencluesfound
        {47, "Completed", 539, "Done", LK::Connection},               // 10
        {1799, "OnStart", 1310, "Start", LK::Connection},             // 11: admirerid
        {1310, "OnDone", 1799, "Done", LK::Connection},               // 12
        {773, "OnTrue", 1799, "Done", LK::Connection},                // 13: not the line's end
    };
    const fr::StoryFind find = fr::FindSinLineMutes({483, 1269, 47, 1310}, 1910, links);

    REQUIRE(find.mutes.size() == 4);
    CHECK(find.skipped.empty());

    CHECK(find.mutes[0].entity == 1277);
    CHECK(find.mutes[0].pin == "OnPlay");
    CHECK(find.mutes[0].resend == std::vector<size_t> {0, 1, 2, 4}); // the dream's effects still run
    CHECK(find.mutes[1].entity == 122);
    CHECK(find.mutes[1].pin == "Out");
    CHECK(find.mutes[1].resend == std::vector<size_t> {8});
    CHECK(find.mutes[2].entity == 539);
    CHECK(find.mutes[2].resend == std::vector<size_t> {10});
    CHECK(find.mutes[3].entity == 1799);
    CHECK(find.mutes[3].resend == std::vector<size_t> {12});
}

TEST_CASE("variant comments: a line's end into another line, too many links, nothing starting it") {
    using LK = fr::BriefingLinkKind;
    std::vector<fr::BriefingLink> links {
        {10, "Out", 1, "Start", LK::Connection},      // 0
        {1, "Completed", 2, "Start", LK::Connection}, // 1: into another line, never resent
        {1, "Completed", 7, "Done", LK::Connection},  // 2
        {11, "Out", 2, "Start", LK::Connection},      // 3
    };

    // Line 2's feeder starts eight other links, and its end one more: over kLineMaxResend.
    for (int32_t k = 0; k < 8; ++k)
        links.push_back({11, "Out", 20 + k, "In", LK::Connection});

    links.push_back({2, "OnDone", 30, "Done", LK::Connection});

    const fr::StoryFind find = fr::FindSinLineMutes({1, 2, 3}, 100, links);

    REQUIRE(find.mutes.size() == 2);
    CHECK(find.mutes[0].entity == 10);
    CHECK(find.mutes[0].resend == std::vector<size_t> {2}); // line 1's end, but not into line 2

    // Line 1's end is a feed of line 2 too: blocked, sending on its other link and line 2's end.
    CHECK(find.mutes[1].entity == 1);
    CHECK(find.mutes[1].pin == "Completed");
    CHECK(find.mutes[1].stories == std::vector<int32_t> {2});
    CHECK(find.mutes[1].resend == std::vector<size_t> {2, 12});

    REQUIRE(find.skipped.size() == 1);
    CHECK(find.skipped[0].story == 2);
    CHECK(find.skipped[0].feeder == 11);
    CHECK(find.skipped[0].why == fr::StorySkipWhy::TooManyLinks);
    CHECK(find.unfed == std::vector<int32_t> {3});
}

TEST_CASE("variant comments: the log line's tally") {
    CHECK(fr::CommentTallyText({}) == "commentary: none in this variant's bricks");

    fr::CommentTally sloth;
    sloth.hints = 12;
    sloth.hintsMuted = 12;
    sloth.vitality = 1;
    sloth.feedsBlocked = 10;
    CHECK(fr::CommentTallyText(sloth) == "vitality comments muted (12 ZHintEntity, blocked at OnHintStarted; vitality "
                                         "kept full: 10 pins into helper_harebell_vitality blocked, their other logic "
                                         "sent on)");

    fr::CommentTally partly = sloth;
    partly.hintsMuted = 11;
    partly.feedsBlocked = 9;
    partly.feedsLeft = 1;
    CHECK(fr::CommentTallyText(partly) == "vitality comments muted (11 of 12 ZHintEntity, blocked at OnHintStarted; 1 "
                                          "could not be: they may still talk; vitality kept full: 9 pins into "
                                          "helper_harebell_vitality blocked, their other logic sent on, 1 left on: it "
                                          "may still drop)");

    fr::CommentTally unread;
    unread.hints = 12;
    unread.hintsMuted = 12;
    unread.vitality = 1;
    unread.vitalityWhy = "its blueprint was not read: no copy";
    CHECK(fr::CommentTallyText(unread) == "vitality comments muted (12 ZHintEntity, blocked at OnHintStarted; vitality "
                                          "left running (its blueprint was not read: no copy): its thresholds may "
                                          "still raise the music's tension)");

    fr::CommentTally stuck = unread;
    stuck.vitalityWhy.clear();
    stuck.feedsLeft = 2;
    CHECK(fr::CommentTallyText(stuck) == "vitality comments muted (12 ZHintEntity, blocked at OnHintStarted; vitality "
                                         "left running: 2 pins into helper_harebell_vitality could not be blocked)");

    fr::CommentTally none;
    none.hints = 3;
    CHECK(fr::CommentTallyText(none) == "vitality comments not muted (0 of 3 ZHintEntity, blocked at OnHintStarted; 3 "
                                        "could not be: they may still talk)");

    fr::CommentTally helperOnly;
    helperOnly.vitality = 1;
    helperOnly.feedsBlocked = 1;
    CHECK(fr::CommentTallyText(helperOnly) == "vitality comments: no ZHintEntity in this variant's bricks; vitality "
                                              "kept full: 1 pin into helper_harebell_vitality blocked, their other "
                                              "logic sent on");

    fr::CommentTally envy;
    envy.lines = 9;
    envy.linesMuted = 9;
    envy.lineFeeds = 9;
    CHECK(fr::CommentTallyText(envy) == "the sin's lines muted (9, each skipped as if it had played: 9 pins that start "
                                        "them blocked)");

    fr::CommentTally lust = envy;
    lust.lines = 13;
    lust.linesMuted = 12;
    lust.lineFeeds = 12;
    CHECK(fr::CommentTallyText(lust) == "the sin's lines muted (12 of 13, each skipped as if it had played: 12 pins "
                                        "that start them blocked; 1 could not be: they may still talk)");

    fr::CommentTally unreadLines;
    unreadLines.lines = 9;
    unreadLines.linesWhy = "its blueprint was not read: no copy";
    CHECK(fr::CommentTallyText(unreadLines) == "the sin's lines not muted (9 found; its blueprint was not read: no "
                                               "copy): they may still talk");

    fr::CommentTally both = sloth;
    both.lines = 1;
    both.linesMuted = 1;
    both.lineFeeds = 1;
    CHECK(fr::CommentTallyText(both) == "vitality comments muted (12 ZHintEntity, blocked at OnHintStarted; vitality "
                                        "kept full: 10 pins into helper_harebell_vitality blocked, their other logic "
                                        "sent on); the sin's lines muted (1, each skipped as if it had played: 1 pin "
                                        "that starts them blocked)");
}
