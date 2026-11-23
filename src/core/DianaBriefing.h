#pragma once

// Diana's briefing in a map variant's bricks (generic, found at runtime).
//
// A variant brick made for its own contract (an elusive target, a special
// assignment, a Sarajevo Six mission, an escalation's scenario) has Diana
// brief the player at the start, over Freelancer's own briefing. Decoded from
// the elusive bricks' TEMP/TBLU pairs, the briefing starts the same way in
// every one: a ZGameEventListenerEntity (m_eEvent GET_IntroCutStart, or the
// class default) whose EventOccurred reaches the Trigger input of a DianaSpeak
// template instance (intel.template?/dianaspeak.entitytemplate) directly or
// through a few logic entities (a delay timer, a poll, a forwarded output pin).
//
// FindBriefingMutes takes one blueprint's sub-entities (which are listeners
// and which are Diana's speech, as the mod reads them from the loaded brick)
// and its pin connections and forwardings, and says which output pin to block
// so that speech never starts, without stopping anything else:
// - the listener's EventOccurred when everything it starts ends in Diana's
//   speech (all ten elusive bricks decoded so far);
// - otherwise the pin that feeds the speech's Trigger, when everything that
//   pin starts ends in Diana's speech (Snow Festival's "Flyin" FlyInStarted:
//   the listener also starts a delay that other lines wait on);
// - otherwise nothing, reported as skipped.
// "Ends in Diana's speech" is judged on the pin graph only, within
// kBriefingMaxSteps links: a dead end, another listener or a cycle counts as
// other logic.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fr {

// ZGameEventListenerEntity::m_eEvent (EGameEventType).
constexpr int32_t kGameEventGameplayStart = 0;
constexpr int32_t kGameEventIntroCutStart = 2;
constexpr int32_t kGameEventIntroCutEnd = 3;
constexpr int32_t kGameEventUnread = -1; // the property could not be read

constexpr std::string_view kGameEventListenerClass = "ZGameEventListenerEntity";

// The events a mission's opening fly-in raises: gameplay start, intro cut
// start and end. An unread event counts too (the speech link is the evidence).
bool IsBriefingEvent(int32_t event);

// "GET_IntroCutStart", "GET_GameplayStart", "event 5", "event unread".
std::string GameEventName(int32_t event);

// Diana's speech templates: an instance's own blueprint factory has one of
// these resource ids (the TBLU, or the TEMP, depending on the factory).
constexpr uint64_t kDianaSpeakTblu = 0x00604782EE75F282ull; // intel.template?/dianaspeak.entitytemplate
constexpr uint64_t kDianaSpeakTemp = 0x00C25939D55B4D55ull;
constexpr uint64_t kDianaSpeakInterruptibleTblu = 0x0046BF1A5B9385D4ull; // ldhelper_dianaspeak_interruptible
constexpr uint64_t kDianaSpeakInterruptibleTemp = 0x003626A7D93B0D5Dull;
bool IsDianaSpeakResource(uint64_t rid);

// Links from a briefing listener to Diana's speech Trigger, at most.
constexpr int kBriefingMaxSteps = 5;

enum class BriefingNodeKind : uint8_t { Other, Listener, Speaker };

struct BriefingNode {
    BriefingNodeKind kind = BriefingNodeKind::Other;
    int32_t event = kGameEventUnread; // listeners: m_eEvent
};

enum class BriefingLinkKind : uint8_t {
    Connection,       // from's output pin -> to's input pin
    OutputForwarding, // from's output pin -> to's output pin (signaled on to)
    InputForwarding,  // from's input pin -> to's input pin (not followed)
};

// One pin connection or forwarding of the blueprint, by sub-entity index.
struct BriefingLink {
    int32_t from;
    std::string fromPin;
    int32_t to;
    std::string toPin;
    BriefingLinkKind kind = BriefingLinkKind::Connection;
};

// One output pin to block.
struct BriefingMute {
    int32_t entity;                // the sub-entity whose output pin is blocked
    std::string pin;               // "EventOccurred", "FlyInStarted"
    int32_t listener;              // the listener the briefing starts from
    std::vector<int32_t> speakers; // the DianaSpeak instances it no longer triggers
    int steps;                     // links from the listener to the farthest of them
};

// A listener that reaches Diana's speech through a pin that also drives other
// logic: left alone.
struct BriefingSkip {
    int32_t listener;
    int32_t feeder; // the entity whose pin feeds the speech
    std::string pin;
};

struct BriefingFind {
    std::vector<BriefingMute> mutes; // one per pin, in listener order
    std::vector<BriefingSkip> skipped;
};

// nodes[i] is sub-entity i of the blueprint; links outside nodes are ignored.
BriefingFind FindBriefingMutes(const std::vector<BriefingNode>& nodes, const std::vector<BriefingLink>& links);

// One connection into a DianaSpeak instance: what starts that line (the log
// names it when the line is left on, so a briefing the finder missed shows
// what starts it).
struct BriefingFeed {
    int32_t speaker;
    int32_t from;
    std::string fromPin;
    std::string toPin;
};

// Every connection into each speaker (any input pin), by speaker index, then
// in link order.
std::vector<BriefingFeed> SpeakerFeeds(const std::vector<BriefingNode>& nodes, const std::vector<BriefingLink>& links);

// --- Blueprints copied while the scene loads ---
//
// The game does not keep a template's blueprint (its TBLU: sub-entity names
// and pin connections) once it has built the blueprint factory from it:
// ZTemplateEntityBlueprintFactory::m_pTemplateEntityBlueprint reads null by
// scene start. The mod copies what the finder needs while
// the factory is built, keyed by the factory's address.

// What the finder needs from one blueprint.
struct BriefingGraph {
    std::vector<uint64_t> ids;      // sub-entity ids, by index
    std::vector<std::string> names; // authored names, by index
    std::vector<BriefingLink> links;
};

// A blueprint is copied only when it may hold a briefing: a connection out of
// a listener's EventOccurred and one into a DianaSpeak's Trigger.
bool MayHoldBriefing(bool eventOccurredOut, bool triggerIn);

// The copies, oldest dropped first once either budget is exceeded (a newer
// copy for the same key replaces the old one). A key may be reused by another
// factory later: check the copy against the entities before using it
// (BriefingGraphMatches). Not thread-safe.
class BriefingGraphCache {
public:
    BriefingGraphCache(size_t maxGraphs, size_t maxLinks);

    void Put(uintptr_t key, BriefingGraph graph);
    const BriefingGraph* Get(uintptr_t key) const;

    size_t Size() const { return m_Order.size(); }
    size_t Links() const { return m_Links; }
    size_t Dropped() const { return m_Dropped; }

private:
    struct Entry {
        uintptr_t key;
        BriefingGraph graph;
    };

    size_t m_MaxGraphs;
    size_t m_MaxLinks;
    size_t m_Links = 0;
    size_t m_Dropped = 0;
    std::vector<Entry> m_Order; // oldest first
};

// --- The rest of the variant's story ---
//
// Diana's other lines in a variant's bricks belong to its own contract (its
// target identified, escaping, eliminated; its mission stories), never to
// Freelancer's: each DianaSpeak's input pin kDianaLinePin is blocked. The
// mission stories are switched off too (core/MissionStories.h). The story's
// other parts (the target's escape and its "target escaping" message) are not
// decoded yet: the search logs the entities of these engine classes in the
// variant's bricks, so a playtest names what to switch off next.

// A speaker's (DianaSpeak's) input pin that plays its line.
constexpr std::string_view kDianaLinePin = "Trigger";
// A briefing listener's output pin, fired when its event occurs.
constexpr std::string_view kEventOccurredPin = "EventOccurred";

// Connection-driven input pins never pass through the SignalInputPin hook
// (a line fed by a connection plays even with its Trigger blocked there): a
// line is muted on the output side instead, at
// the pin that feeds its Trigger. The pin is blocked; whatever else it starts
// is kept by sending the signal on along each of its other links (resend)
// that does not end only in Diana's speech (the briefing's rule,
// kBriefingMaxSteps). Of the 60 Trigger feeds in the eleven decoded elusive
// bricks (briefings included), 59 need no resend; The Appraiser's
// "JournalTaken" PollFalse also starts "Pip camera" (one resend).
constexpr size_t kLineMaxResend = 8;

struct LineMute {
    int32_t entity;                // the sub-entity whose output pin is blocked
    std::string pin;               // "OnTrue", "Out", "Activated"
    std::vector<int32_t> speakers; // the DianaSpeak instances it no longer triggers
    std::vector<size_t> resend;    // the links (indices) the signal is sent on along, in link order
};

// A line whose feeding pin starts more than kLineMaxResend other links: left on.
struct LineSkip {
    int32_t speaker;
    int32_t feeder;
    std::string pin;
};

struct LineFind {
    std::vector<LineMute> mutes; // one per pin, in speaker order
    std::vector<LineSkip> skipped;
};

// Every speaker not in silenced (the briefing's, muted already): each
// connection into its Trigger gives a feeding pin to block or a skip.
LineFind FindLineMutes(const std::vector<BriefingNode>& nodes, const std::vector<BriefingLink>& links,
                       const std::vector<int32_t>& silenced);

// A class name that may run a variant's story: "Opportunit", "VIP",
// "Evacuat", "Escap", "Objective", "Contract", "HUD", "Hint" or "Intel" in it
// (ZVIPControllerEntity, ...).
bool IsStoryClass(std::string_view className);

// Whether a copied graph is the blueprint of the factory it is looked up for:
// the factory's sub-entity count, and the ids the walk read from its entities
// (index, id) all match.
bool BriefingGraphMatches(const BriefingGraph& graph, size_t subCount,
                          const std::vector<std::pair<int32_t, uint64_t>>& seenIds);

// --- A full variant's swapped scene ---
//
// A full variant loads its own scene with the package's bricks: the Freelancer
// layer (our copies of the contract's mission and override bricks), whose
// Freelancer briefing must play. The scene's own bricks are not searched for
// Diana: Freelancer's own lines load among them (evergreen_setup, which every
// Freelancer scene loads, holds DianaSpeaks). A layer variant's swap also
// appends vanilla bricks of the variant's own contract after the package's
// (the full Bad Boy's mission_caipirinha, the full Entertainer's
// mission_marrakesh_maitai): an elusive target's mission brick, with Diana's
// briefing (a GameEventListener's EventOccurred into a DianaSpeak's Trigger)
// and its other lines, which would play over Freelancer's. These bricks get the appended variant's search: briefing,
// other lines and mission stories.
//
// The swap's bricks (resource ids, in swap order) that are not the package's
// own: in swap order, each once.
std::vector<uint64_t> FullDianaBricks(const std::vector<uint64_t>& swap, const std::vector<uint64_t>& own);

} // namespace fr
