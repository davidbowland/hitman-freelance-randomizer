#include "doctest.h"

#include "DianaBriefing.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct BriefingFixtureNode {
    int32_t index;
    uint64_t id;
    char kind; // 'L' listener, 'S' DianaSpeak, 'O' other
    int32_t event;
    const char* name;
};

struct BriefingFixtureLink {
    int32_t from;
    const char* fromPin;
    int32_t to;
    const char* toPin;
    char kind; // 'C' connection, 'O' output forwarding
};

struct BriefingFixture {
    const char* brick;
    uint64_t rid;
    int32_t count;
    std::vector<BriefingFixtureNode> nodes;
    std::vector<BriefingFixtureLink> links;
};

#include "briefing_fixtures.inc"

const BriefingFixture& Fixture(std::string_view brick) {
    for (const BriefingFixture& fixture : kBriefingFixtures) {
        if (brick == fixture.brick)
            return fixture;
    }

    FAIL("no fixture " << brick);
    return kBriefingFixtures[0];
}

struct Graph {
    std::vector<fr::BriefingNode> nodes;
    std::vector<fr::BriefingLink> links;
};

Graph GraphOf(const BriefingFixture& fixture) {
    Graph graph;
    graph.nodes.resize(static_cast<size_t>(fixture.count));

    for (const BriefingFixtureNode& node : fixture.nodes) {
        fr::BriefingNode& out = graph.nodes[static_cast<size_t>(node.index)];
        out.kind = node.kind == 'L'   ? fr::BriefingNodeKind::Listener
                   : node.kind == 'S' ? fr::BriefingNodeKind::Speaker
                                      : fr::BriefingNodeKind::Other;
        out.event = node.event;
    }

    for (const BriefingFixtureLink& link : fixture.links)
        graph.links.push_back(
            {link.from, link.fromPin, link.to, link.toPin,
             link.kind == 'O' ? fr::BriefingLinkKind::OutputForwarding : fr::BriefingLinkKind::Connection});

    return graph;
}

uint64_t IdOf(const BriefingFixture& fixture, int32_t index) {
    for (const BriefingFixtureNode& node : fixture.nodes) {
        if (node.index == index)
            return node.id;
    }

    return 0;
}

fr::BriefingFind Find(const BriefingFixture& fixture) {
    const Graph graph = GraphOf(fixture);
    return fr::FindBriefingMutes(graph.nodes, graph.links);
}

} // namespace

// The hand-decoded listeners the mod muted before the finder (one per elusive brick, read from each brick's TEMP):
// the finder must pick exactly these from the bricks' own data.
TEST_CASE("diana_briefing_known_elusive_listeners") {
    struct Known {
        const char* brick;
        uint64_t listener;
    };

    const Known known[] {
        {"elusive_redsnapper", 0x7A17ADAFC3C3303Aull},     {"elusive_tomorrowland", 0x6073D56E7DC208E7ull},
        {"elusive_alabamaslammer", 0x3E8926779953A7C1ull}, {"elusive_vesper", 0x1C954F5B98EC8D3Dull},
        {"elusive_bramble", 0x4D2CF4F684DE815Cull},        {"elusive_radler", 0xF84418A1E2B28AFCull},
        {"elusive_clerico", 0x6E4E2917F91B7D96ull},        {"elusive_jockeyclub", 0x9D3AB2D9585A87DEull},
        {"elusive_goldendoublet", 0x21637BABCB839FFDull},  {"elusive_skittlebomb", 0x8D33899E9C9E8A9Aull},
    };

    for (const Known& row : known) {
        CAPTURE(row.brick);
        const BriefingFixture& fixture = Fixture(row.brick);
        const fr::BriefingFind find = Find(fixture);
        REQUIRE(find.mutes.size() == 1);
        CHECK(find.skipped.empty());
        const fr::BriefingMute& mute = find.mutes[0];
        CHECK(IdOf(fixture, mute.entity) == row.listener);
        CHECK(mute.listener == mute.entity);
        CHECK(mute.pin == "EventOccurred");
        CHECK_FALSE(mute.speakers.empty());
        CHECK(mute.steps >= 1);
        CHECK(mute.steps <= fr::kBriefingMaxSteps);
    }

    // Through a delay timer, a poll and a forwarded output (Serial Killer): four links.
    CHECK(Find(Fixture("elusive_alabamaslammer")).mutes[0].steps == 4);
    // Vesper's and Clerico's other intro-cut listeners (a setpiece, the disguises) stay.
    CHECK(Find(Fixture("elusive_vesper")).mutes.size() == 1);
    CHECK(Find(Fixture("elusive_clerico")).mutes.size() == 1);
}

TEST_CASE("diana_briefing_other_bricks") {
    // The Revolutionary: Diana's lines there start from other events; no fly-in.
    CHECK(Find(Fixture("elusive_highball")).mutes.empty());
    // The Splitter's scenario brick: 29 listeners, 10 DianaSpeaks, none at the intro.
    CHECK(Find(Fixture("scenario_lambic")).mutes.empty());
    CHECK(Find(Fixture("scenario_filur_ea")).mutes.empty());

    // Snow Festival: the intro listener also starts "Delay Diana by one second at the beginning", so its
    // EventOccurred stays and the forwarded pin that feeds only the fly-in speech is blocked.
    const BriefingFixture& fixture = Fixture("scenario_mamushi");
    const fr::BriefingFind find = Find(fixture);
    REQUIRE(find.mutes.size() == 1);
    CHECK(find.skipped.empty());
    const fr::BriefingMute& mute = find.mutes[0];
    CHECK(IdOf(fixture, mute.listener) == 0x90C22574C1540D74ull);
    CHECK(mute.entity != mute.listener);
    CHECK(mute.pin == "FlyInStarted");
    CHECK(mute.steps == 2);
    REQUIRE(mute.speakers.size() == 1);
}

TEST_CASE("diana_briefing_rules") {
    using K = fr::BriefingNodeKind;
    using LK = fr::BriefingLinkKind;
    const auto listener = [](int32_t event) { return fr::BriefingNode {K::Listener, event}; };
    const fr::BriefingNode other {};
    const fr::BriefingNode speaker {K::Speaker};

    SUBCASE("direct") {
        const std::vector<fr::BriefingNode> nodes {listener(fr::kGameEventIntroCutStart), speaker};
        const fr::BriefingFind find = fr::FindBriefingMutes(nodes, {{0, "EventOccurred", 1, "Trigger"}});
        REQUIRE(find.mutes.size() == 1);
        CHECK(find.mutes[0].entity == 0);
        CHECK(find.mutes[0].steps == 1);
    }

    SUBCASE("only the Trigger input starts the speech") {
        const std::vector<fr::BriefingNode> nodes {listener(fr::kGameEventIntroCutStart), speaker};
        CHECK(fr::FindBriefingMutes(nodes, {{0, "EventOccurred", 1, "StopAudio"}}).mutes.empty());
    }

    SUBCASE("events") {
        for (const int32_t event : {fr::kGameEventGameplayStart, fr::kGameEventIntroCutStart, fr::kGameEventIntroCutEnd,
                                    fr::kGameEventUnread}) {
            const std::vector<fr::BriefingNode> nodes {listener(event), speaker};
            CHECK(fr::FindBriefingMutes(nodes, {{0, "EventOccurred", 1, "Trigger"}}).mutes.size() == 1);
        }

        for (const int32_t event : {1, 4, 5, 6}) {
            const std::vector<fr::BriefingNode> nodes {listener(event), speaker};
            CHECK(fr::FindBriefingMutes(nodes, {{0, "EventOccurred", 1, "Trigger"}}).mutes.empty());
        }
    }

    SUBCASE("at most kBriefingMaxSteps links") {
        // 0 -> 1 -> 2 -> ... -> n -> speaker
        for (int steps = 1; steps <= fr::kBriefingMaxSteps + 1; ++steps) {
            std::vector<fr::BriefingNode> nodes {listener(fr::kGameEventIntroCutStart)};
            std::vector<fr::BriefingLink> links;

            for (int i = 1; i < steps; ++i) {
                nodes.push_back(other);
                links.push_back({i - 1, i == 1 ? "EventOccurred" : "Out", i, "In"});
            }

            nodes.push_back(speaker);
            links.push_back({steps - 1, steps == 1 ? "EventOccurred" : "Out", steps, "Trigger"});
            CAPTURE(steps);
            CHECK(fr::FindBriefingMutes(nodes, links).mutes.size() == (steps <= fr::kBriefingMaxSteps ? 1u : 0u));
        }
    }

    SUBCASE("a listener's other logic keeps running") {
        // Listener -> timer -> speaker, and listener -> gameplay. The timer's Out feeds only the speech: blocked.
        const std::vector<fr::BriefingNode> nodes {listener(fr::kGameEventGameplayStart), other, speaker, other};
        const fr::BriefingFind find = fr::FindBriefingMutes(
            nodes, {{0, "EventOccurred", 1, "In"}, {1, "Out", 2, "Trigger"}, {0, "EventOccurred", 3, "Start"}});
        REQUIRE(find.mutes.size() == 1);
        CHECK(find.mutes[0].entity == 1);
        CHECK(find.mutes[0].pin == "Out");
        CHECK(find.mutes[0].listener == 0);
        CHECK(find.skipped.empty());
    }

    SUBCASE("a pin shared with other logic is left alone") {
        // Listener -> speaker and -> gameplay directly: nothing to block without stopping the gameplay.
        const std::vector<fr::BriefingNode> nodes {listener(fr::kGameEventIntroCutStart), speaker, other, other};
        const fr::BriefingFind direct =
            fr::FindBriefingMutes(nodes, {{0, "EventOccurred", 1, "Trigger"}, {0, "EventOccurred", 2, "Start"}});
        CHECK(direct.mutes.empty());
        REQUIRE(direct.skipped.size() == 1);
        CHECK(direct.skipped[0].feeder == 0);

        // Listener -> relay; relay Out -> speaker and -> gameplay.
        const fr::BriefingFind relay = fr::FindBriefingMutes(
            nodes, {{0, "EventOccurred", 2, "In"}, {2, "Out", 1, "Trigger"}, {2, "Out", 3, "Start"}});
        CHECK(relay.mutes.empty());
        REQUIRE(relay.skipped.size() == 1);
        CHECK(relay.skipped[0].feeder == 2);
        CHECK(relay.skipped[0].pin == "Out");
    }

    SUBCASE("forwarded outputs follow only their own pin") {
        // Listener -o-> group.Started; group.Started -> speaker; group.Other -> speaker 2 (not reached).
        const std::vector<fr::BriefingNode> nodes {listener(fr::kGameEventIntroCutStart), other, speaker, speaker};
        const fr::BriefingFind find =
            fr::FindBriefingMutes(nodes, {{0, "EventOccurred", 1, "Started", LK::OutputForwarding},
                                          {1, "Started", 2, "Trigger"},
                                          {1, "Other", 3, "Trigger"}});
        REQUIRE(find.mutes.size() == 1);
        CHECK(find.mutes[0].entity == 0);
        CHECK(find.mutes[0].speakers == std::vector<int32_t> {2});
    }

    SUBCASE("input forwardings and other listeners are not followed") {
        const std::vector<fr::BriefingNode> nodes {listener(fr::kGameEventIntroCutStart), other,
                                                   listener(fr::kGameEventGameplayStart), speaker};
        const fr::BriefingFind find = fr::FindBriefingMutes(nodes, {{0, "EventOccurred", 1, "In", LK::InputForwarding},
                                                                    {1, "In", 3, "Trigger"},
                                                                    {0, "EventOccurred", 2, "In"},
                                                                    {2, "Out", 3, "Trigger"}});
        CHECK(find.mutes.empty());
    }

    SUBCASE("two listeners into one speech: one mute each") {
        const std::vector<fr::BriefingNode> nodes {listener(fr::kGameEventIntroCutStart),
                                                   listener(fr::kGameEventIntroCutEnd), speaker};
        const fr::BriefingFind find =
            fr::FindBriefingMutes(nodes, {{0, "EventOccurred", 2, "Trigger"}, {1, "EventOccurred", 2, "Trigger"}});
        REQUIRE(find.mutes.size() == 2);
        CHECK(find.mutes[0].entity == 0);
        CHECK(find.mutes[1].entity == 1);
    }

    SUBCASE("links out of range are ignored") {
        const std::vector<fr::BriefingNode> nodes {listener(fr::kGameEventIntroCutStart)};
        CHECK(fr::FindBriefingMutes(nodes, {{0, "EventOccurred", 7, "Trigger"}, {-1, "Out", 0, "In"}}).mutes.empty());
    }
}

TEST_CASE("diana_briefing_names") {
    CHECK(fr::GameEventName(fr::kGameEventIntroCutStart) == "GET_IntroCutStart");
    CHECK(fr::GameEventName(fr::kGameEventGameplayStart) == "GET_GameplayStart");
    CHECK(fr::GameEventName(fr::kGameEventIntroCutEnd) == "GET_IntroCutEnd");
    CHECK(fr::GameEventName(fr::kGameEventUnread) == "event unread");
    CHECK(fr::GameEventName(5) == "event 5");

    CHECK(fr::IsDianaSpeakResource(0x00604782EE75F282ull));
    CHECK(fr::IsDianaSpeakResource(0x00C25939D55B4D55ull));
    CHECK(fr::IsDianaSpeakResource(0x0046BF1A5B9385D4ull));
    CHECK(fr::IsDianaSpeakResource(0x003626A7D93B0D5Dull));
    CHECK_FALSE(fr::IsDianaSpeakResource(0));
    CHECK(fr::kGameEventListenerClass == "ZGameEventListenerEntity");
}

namespace {

fr::BriefingGraph BriefingGraphOf(const BriefingFixture& fixture) {
    fr::BriefingGraph graph;
    graph.ids.assign(static_cast<size_t>(fixture.count), 0);
    graph.names.assign(static_cast<size_t>(fixture.count), "");

    for (const BriefingFixtureNode& node : fixture.nodes) {
        graph.ids[static_cast<size_t>(node.index)] = node.id;
        graph.names[static_cast<size_t>(node.index)] = node.name;
    }

    graph.links = GraphOf(fixture).links;
    return graph;
}

fr::BriefingGraph Sized(size_t subs, size_t links) {
    fr::BriefingGraph graph;
    graph.ids.assign(subs, 0);
    graph.names.assign(subs, "");
    graph.links.assign(links, {0, "Out", 0, "In"});
    return graph;
}

} // namespace

// The game drops a blueprint once its factory is built, so the mod copies the ones that may hold a briefing
// while the scene loads: every briefing the finder mutes in the decoded bricks passes that test.
TEST_CASE("diana_briefing_copy_filter") {
    CHECK(fr::MayHoldBriefing(true, true));
    CHECK_FALSE(fr::MayHoldBriefing(true, false));
    CHECK_FALSE(fr::MayHoldBriefing(false, true));

    size_t muted = 0;

    for (const BriefingFixture& fixture : kBriefingFixtures) {
        CAPTURE(fixture.brick);

        if (Find(fixture).mutes.empty())
            continue;

        ++muted;
        bool eventOut = false;
        bool triggerIn = false;

        for (const BriefingFixtureLink& link : fixture.links) {
            eventOut = eventOut || (link.kind == 'C' && std::string_view(link.fromPin) == "EventOccurred");
            triggerIn = triggerIn || (link.kind == 'C' && std::string_view(link.toPin) == "Trigger");
        }

        CHECK(fr::MayHoldBriefing(eventOut, triggerIn));
    }

    CHECK(muted == 11); // the ten elusive fly-ins and Snow Festival's
}

TEST_CASE("diana_briefing_copied_graph") {
    // A copy found again by its factory's address gives the same mutes as the brick's own data.
    fr::BriefingGraphCache cache(8, 100'000);
    const BriefingFixture& fixture = Fixture("scenario_mamushi");
    cache.Put(0x1000, BriefingGraphOf(fixture));
    const fr::BriefingGraph* copy = cache.Get(0x1000);
    REQUIRE(copy != nullptr);
    CHECK(cache.Get(0x2000) == nullptr);

    const Graph graph = GraphOf(fixture);
    const fr::BriefingFind find = fr::FindBriefingMutes(graph.nodes, copy->links);
    REQUIRE(find.mutes.size() == 1);
    CHECK(find.mutes[0].pin == "FlyInStarted");
    CHECK(copy->names[static_cast<size_t>(find.mutes[0].listener)] == "Flyin Started");

    // Checked against what the walk read from the entities.
    const int32_t listener = find.mutes[0].listener;
    const uint64_t id = IdOf(fixture, listener);
    CHECK(fr::BriefingGraphMatches(*copy, static_cast<size_t>(fixture.count), {{listener, id}}));
    CHECK(fr::BriefingGraphMatches(*copy, static_cast<size_t>(fixture.count), {}));
    CHECK_FALSE(fr::BriefingGraphMatches(*copy, static_cast<size_t>(fixture.count) + 1, {{listener, id}}));
    CHECK_FALSE(fr::BriefingGraphMatches(*copy, static_cast<size_t>(fixture.count), {{listener, id + 1}}));
    CHECK_FALSE(fr::BriefingGraphMatches(*copy, static_cast<size_t>(fixture.count), {{fixture.count, id}}));
    CHECK_FALSE(fr::BriefingGraphMatches(*copy, static_cast<size_t>(fixture.count), {{-1, id}}));
    CHECK_FALSE(fr::BriefingGraphMatches(fr::BriefingGraph {}, 0, {}));
}

TEST_CASE("diana_briefing_copy_budgets") {
    SUBCASE("a newer copy for the same factory replaces the old one") {
        fr::BriefingGraphCache cache(4, 100);
        cache.Put(1, Sized(3, 10));
        cache.Put(1, Sized(5, 20));
        CHECK(cache.Size() == 1);
        CHECK(cache.Links() == 20);
        CHECK(cache.Get(1)->ids.size() == 5);
        CHECK(cache.Dropped() == 0);
    }

    SUBCASE("the oldest go past the count") {
        fr::BriefingGraphCache cache(2, 100);
        cache.Put(1, Sized(1, 1));
        cache.Put(2, Sized(1, 1));
        cache.Put(3, Sized(1, 1));
        CHECK(cache.Size() == 2);
        CHECK(cache.Get(1) == nullptr);
        CHECK(cache.Get(2) != nullptr);
        CHECK(cache.Get(3) != nullptr);
        CHECK(cache.Dropped() == 1);
    }

    SUBCASE("the oldest go past the link budget; the newest always stays") {
        fr::BriefingGraphCache cache(10, 100);
        cache.Put(1, Sized(1, 60));
        cache.Put(2, Sized(1, 30));
        cache.Put(3, Sized(1, 30));
        CHECK(cache.Get(1) == nullptr);
        CHECK(cache.Links() == 60);
        cache.Put(4, Sized(1, 500));
        CHECK(cache.Size() == 1);
        CHECK(cache.Get(4) != nullptr);
        CHECK(cache.Links() == 500);
        CHECK(cache.Dropped() == 3);
    }
}

TEST_CASE("diana_briefing_speaker_feeds") {
    using K = fr::BriefingNodeKind;
    const std::vector<fr::BriefingNode> nodes {
        {K::Listener, fr::kGameEventIntroCutStart}, {}, {K::Speaker}, {K::Speaker}};
    const std::vector<fr::BriefingLink> links {
        {0, "EventOccurred", 2, "Trigger"}, {1, "Out", 3, "Trigger"},
        {1, "Out", 2, "StopAudio"},         {1, "Out", 1, "In"},
        {9, "Out", 3, "Trigger"},           {1, "Started", 3, "Trigger", fr::BriefingLinkKind::OutputForwarding}};
    const std::vector<fr::BriefingFeed> feeds = fr::SpeakerFeeds(nodes, links);
    REQUIRE(feeds.size() == 3);
    CHECK(feeds[0].speaker == 2);
    CHECK(feeds[0].from == 0);
    CHECK(feeds[0].fromPin == "EventOccurred");
    CHECK(feeds[1].speaker == 2);
    CHECK(feeds[1].toPin == "StopAudio");
    CHECK(feeds[2].speaker == 3);
    CHECK(feeds[2].from == 1);

    // Snow Festival's fly-in speech is fed by the forwarded FlyInStarted the finder blocks.
    const BriefingFixture& fixture = Fixture("scenario_mamushi");
    const Graph graph = GraphOf(fixture);
    const fr::BriefingMute mute = fr::FindBriefingMutes(graph.nodes, graph.links).mutes.at(0);
    bool fed = false;

    for (const fr::BriefingFeed& feed : fr::SpeakerFeeds(graph.nodes, graph.links))
        fed = fed || (feed.speaker == mute.speakers[0] && feed.from == mute.entity && feed.fromPin == mute.pin);

    CHECK(fed);
}

TEST_CASE("diana_briefing_story_classes") {
    // Diana's other lines are muted at their Trigger input: the pin the decoded bricks' connections start a line with.
    CHECK(fr::kDianaLinePin == "Trigger");

    // The Wizard's escape runs through a ZVIPControllerEntity ("VIP Penicillin" OnEvacuationStarted).
    CHECK(fr::IsStoryClass("ZVIPControllerEntity"));
    CHECK(fr::IsStoryClass("ZOpportunityEntity"));
    CHECK(fr::IsStoryClass("ZIntelEntity"));
    CHECK(fr::IsStoryClass("ZHUDMessageEntity"));

    // Logic and actors are not story classes.
    CHECK(!fr::IsStoryClass(fr::kGameEventListenerClass));
    CHECK(!fr::IsStoryClass("ZValueBool_Operation_Signal"));
    CHECK(!fr::IsStoryClass("ZActor"));
    CHECK(!fr::IsStoryClass(""));
}

TEST_CASE("diana_briefing_line_mutes") {
    using K = fr::BriefingNodeKind;
    using LK = fr::BriefingLinkKind;
    const fr::BriefingNode other {};
    const fr::BriefingNode speaker {K::Speaker};

    SUBCASE(
        "The Pharmacist: the briefing's speech is left to the briefing mute, the other two are muted at their feeder") {
        // 0 GameEventListener (GET_IntroCutEnd) -> 1 DianaSpeak; 2 ActorDeath OnDead -> 3 DianaSpeak;
        // 4 VolumeTriggerCharacter OnTrue -> 5 DianaSpeak (a line that plays despite a blocked Trigger).
        const std::vector<fr::BriefingNode> nodes {
            {K::Listener, fr::kGameEventIntroCutEnd}, speaker, other, speaker, other, speaker};
        const std::vector<fr::BriefingLink> links {
            {0, "EventOccurred", 1, "Trigger"}, {2, "OnDead", 3, "Trigger"}, {4, "OnTrue", 5, "Trigger"}};
        const fr::BriefingFind briefing = fr::FindBriefingMutes(nodes, links);
        REQUIRE(briefing.mutes.size() == 1);
        const fr::LineFind find = fr::FindLineMutes(nodes, links, briefing.mutes[0].speakers);
        CHECK(find.skipped.empty());
        REQUIRE(find.mutes.size() == 2);
        CHECK(find.mutes[0].entity == 2);
        CHECK(find.mutes[0].pin == "OnDead");
        CHECK(find.mutes[0].speakers == std::vector<int32_t> {3});
        CHECK(find.mutes[1].entity == 4);
        CHECK(find.mutes[1].pin == "OnTrue");
        CHECK(find.mutes[1].speakers == std::vector<int32_t> {5});

        // Nothing silenced yet: the briefing's speech gets its listener's pin too.
        CHECK(fr::FindLineMutes(nodes, links, {}).mutes.size() == 3);
    }

    SUBCASE("a pin that also starts other logic: blocked, its other links sent on") {
        // The Appraiser: "JournalTaken" PollFalse -> "DianaSpeak_TargetDown" Trigger and -> "Pip camera" TriggerCam.
        const std::vector<fr::BriefingNode> nodes {other, speaker, other};
        const fr::LineFind find =
            fr::FindLineMutes(nodes, {{0, "PollFalse", 1, "Trigger"}, {0, "PollFalse", 2, "TriggerCam"}}, {});
        CHECK(find.skipped.empty());
        REQUIRE(find.mutes.size() == 1);
        CHECK(find.mutes[0].entity == 0);
        CHECK(find.mutes[0].pin == "PollFalse");
        CHECK(find.mutes[0].resend == std::vector<size_t> {1});

        // Only links out of that pin are sent on.
        const fr::LineFind both = fr::FindLineMutes(
            nodes, {{0, "PollFalse", 1, "Trigger"}, {0, "PollTrue", 1, "Trigger"}, {0, "PollFalse", 2, "TriggerCam"}},
            {});
        REQUIRE(both.mutes.size() == 2);
        CHECK(both.mutes[0].resend == std::vector<size_t> {2});
        CHECK(both.mutes[1].pin == "PollTrue");
        CHECK(both.mutes[1].resend.empty());
    }

    SUBCASE("The Sensation: the intro listener also starts other logic") {
        // 0 GameEventListener EventOccurred -> 1 DianaSpeak Trigger, -> 2 (music) Start, -o-> 3 Started;
        // 4 ActorDeath OnDead -> 5 DianaSpeak. The briefing finder leaves the listener on.
        const std::vector<fr::BriefingNode> nodes {
            {K::Listener, fr::kGameEventIntroCutStart}, speaker, other, other, other, speaker};
        const std::vector<fr::BriefingLink> links {{0, "EventOccurred", 1, "Trigger"},
                                                   {0, "EventOccurred", 2, "Start"},
                                                   {0, "EventOccurred", 3, "Started", LK::OutputForwarding},
                                                   {4, "OnDead", 5, "Trigger"}};
        const fr::BriefingFind briefing = fr::FindBriefingMutes(nodes, links);
        CHECK(briefing.mutes.empty());
        REQUIRE(briefing.skipped.size() == 1);

        const fr::LineFind find = fr::FindLineMutes(nodes, links, {});
        CHECK(find.skipped.empty());
        REQUIRE(find.mutes.size() == 2);
        CHECK(find.mutes[0].entity == 0);
        CHECK(find.mutes[0].pin == "EventOccurred");
        CHECK(find.mutes[0].speakers == std::vector<int32_t> {1});
        CHECK(find.mutes[0].resend == std::vector<size_t> {1, 2});
        CHECK(find.mutes[1].resend.empty());
    }

    SUBCASE("more than kLineMaxResend other links: left on") {
        std::vector<fr::BriefingNode> nodes {other, speaker};
        std::vector<fr::BriefingLink> links {{0, "Out", 1, "Trigger"}};

        for (size_t i = 0; i <= fr::kLineMaxResend; ++i) {
            nodes.push_back(other);
            links.push_back({0, "Out", static_cast<int32_t>(nodes.size() - 1), "In"});
        }

        const fr::LineFind find = fr::FindLineMutes(nodes, links, {});
        CHECK(find.mutes.empty());
        REQUIRE(find.skipped.size() == 1);
        CHECK(find.skipped[0].speaker == 1);
        CHECK(find.skipped[0].feeder == 0);
        CHECK(find.skipped[0].pin == "Out");

        links.pop_back();
        REQUIRE(fr::FindLineMutes(nodes, links, {}).mutes.size() == 1);
        CHECK(fr::FindLineMutes(nodes, links, {}).mutes[0].resend.size() == fr::kLineMaxResend);
    }

    SUBCASE("one pin into two lines: one mute") {
        const std::vector<fr::BriefingNode> nodes {other, speaker, speaker};
        const fr::LineFind find =
            fr::FindLineMutes(nodes, {{0, "PollTrue", 1, "Trigger"}, {0, "PollTrue", 2, "Trigger"}}, {});
        REQUIRE(find.mutes.size() == 1);
        CHECK(find.mutes[0].speakers == std::vector<int32_t> {1, 2});
    }

    SUBCASE("a line's AudioDone into the next line") {
        // Tomorrowland's "Mission Completed" after "Dj Setpiece kill".
        const std::vector<fr::BriefingNode> nodes {other, speaker, speaker};
        const fr::LineFind find =
            fr::FindLineMutes(nodes, {{0, "Out", 1, "Trigger"}, {1, "AudioDone", 2, "Trigger"}}, {});
        REQUIRE(find.mutes.size() == 2);
        CHECK(find.mutes[1].entity == 1);
        CHECK(find.mutes[1].pin == "AudioDone");
    }

    SUBCASE("only connections into Trigger count") {
        const std::vector<fr::BriefingNode> nodes {other, speaker};
        CHECK(fr::FindLineMutes(nodes, {{0, "Out", 1, "StopAudio"}}, {}).mutes.empty());
        CHECK(fr::FindLineMutes(nodes, {{0, "Out", 1, "Trigger", LK::OutputForwarding}}, {}).mutes.empty());
        CHECK(fr::FindLineMutes(nodes, {{7, "Out", 1, "Trigger"}}, {}).mutes.empty());
        CHECK(fr::FindLineMutes(nodes, {{0, "Out", 1, "Trigger"}}, {1}).mutes.empty());
    }

    // Diana's speech is played by the ZIntelEntity inside each DianaSpeak (intel.template?/dianaspeak.entitytemplate).
    CHECK(fr::IsStoryClass("ZIntelEntity"));
}

// A full variant's swapped scene: only the vanilla bricks a layer's swap
// appends (the full Bad Boy's mission_caipirinha, after fr_mission_mild_icon)
// are searched for Diana; the package's own bricks keep Freelancer's briefing.
TEST_CASE("diana_briefing_full_variant_bricks") {
    constexpr uint64_t kFrMissionMildIcon = 0x0038F3EA6201210Dull;
    constexpr uint64_t kFrOverrideIcon = 0x0011111111111111ull;
    constexpr uint64_t kMissionCaipirinha = 0x005246A08E78F16Full;
    const std::vector<uint64_t> own {kFrMissionMildIcon, kFrOverrideIcon};

    // The full Bad Boy: the Icon's swap plus mission_caipirinha.
    CHECK(fr::FullDianaBricks({kFrMissionMildIcon, kMissionCaipirinha}, own) ==
          std::vector<uint64_t> {kMissionCaipirinha});
    // The full Icon (and Landslide, The Author, the Marrakesh night, Patient Zero): the package's bricks only.
    CHECK(fr::FullDianaBricks({kFrMissionMildIcon}, own).empty());
    // Each once, in swap order; an unread id (0) is never a brick.
    CHECK(fr::FullDianaBricks({kMissionCaipirinha, 0, kFrMissionMildIcon, kMissionCaipirinha, 7}, own) ==
          std::vector<uint64_t> {kMissionCaipirinha, 7});
    // The test package (no Freelancer layer, nothing of its own): every swap brick.
    CHECK(fr::FullDianaBricks({kMissionCaipirinha}, {}) == std::vector<uint64_t> {kMissionCaipirinha});
}
