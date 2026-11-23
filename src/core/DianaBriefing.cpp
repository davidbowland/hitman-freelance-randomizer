#include "DianaBriefing.h"

#include <algorithm>
#include <deque>
#include <initializer_list>
#include <map>
#include <set>
#include <utility>

namespace fr {

bool IsBriefingEvent(int32_t event) {
    return event == kGameEventGameplayStart || event == kGameEventIntroCutStart || event == kGameEventIntroCutEnd ||
           event == kGameEventUnread;
}

std::string GameEventName(int32_t event) {
    switch (event) {
        case kGameEventGameplayStart: return "GET_GameplayStart";
        case 1: return "GET_GameplayStop";
        case kGameEventIntroCutStart: return "GET_IntroCutStart";
        case kGameEventIntroCutEnd: return "GET_IntroCutEnd";
        case kGameEventUnread: return "event unread";
        default: return "event " + std::to_string(event);
    }
}

bool IsDianaSpeakResource(uint64_t rid) {
    return rid == kDianaSpeakTblu || rid == kDianaSpeakTemp || rid == kDianaSpeakInterruptibleTblu ||
           rid == kDianaSpeakInterruptibleTemp;
}

namespace {

// Where a path reached Diana's speech: the link into its Trigger.
struct Reach {
    int32_t feeder;
    std::string pin;
    int32_t speaker;
    int steps;
};

bool InRange(const std::vector<BriefingNode>& nodes, int32_t index) {
    return index >= 0 && static_cast<size_t>(index) < nodes.size();
}

BriefingNodeKind KindOf(const std::vector<BriefingNode>& nodes, int32_t index) {
    return InRange(nodes, index) ? nodes[static_cast<size_t>(index)].kind : BriefingNodeKind::Other;
}

// The links a signal on (from, any pin or pin) travels: connections and output forwardings.
bool Carries(const BriefingLink& link) {
    return link.kind != BriefingLinkKind::InputForwarding;
}

// Breadth-first from the first link (out of the listener) up to kBriefingMaxSteps links,
// following any output of an entity a connection reaches and only the forwarded pin of
// an output forwarding; stops at other listeners and at Diana's speech.
std::vector<Reach> Follow(const std::vector<BriefingNode>& nodes, const std::vector<std::vector<size_t>>& out,
                          const std::vector<BriefingLink>& links, size_t first) {
    struct State {
        size_t link;
        int steps;
    };

    std::vector<Reach> reaches;
    std::deque<State> queue {{first, 1}};
    std::set<std::pair<int32_t, std::string>> seen; // (entity, pin filter; "" for any)

    while (!queue.empty()) {
        const State state = queue.front();
        queue.pop_front();
        const BriefingLink& link = links[state.link];
        const BriefingNodeKind kind = KindOf(nodes, link.to);

        if (kind == BriefingNodeKind::Speaker) {
            if (link.kind == BriefingLinkKind::Connection && link.toPin == kDianaLinePin)
                reaches.push_back({link.from, link.fromPin, link.to, state.steps});
            continue;
        }

        if (kind == BriefingNodeKind::Listener || state.steps >= kBriefingMaxSteps)
            continue;

        const bool forwarded = link.kind == BriefingLinkKind::OutputForwarding;
        const std::string filter = forwarded ? link.toPin : std::string();

        if (!seen.emplace(link.to, filter).second)
            continue;

        for (const size_t next : out[static_cast<size_t>(link.to)]) {
            if (!forwarded || links[next].fromPin == filter)
                queue.push_back({next, state.steps + 1});
        }
    }

    return reaches;
}

struct SpeechOnlyWalk {
    const std::vector<BriefingNode>& nodes;
    const std::vector<std::vector<size_t>>& out;
    const std::vector<BriefingLink>& links;
    std::map<std::pair<int32_t, std::string>, bool> memo;
    std::set<std::pair<int32_t, std::string>> visiting;

    // Whether a signal along links[index], the steps-th link of a path, only ever ends in Diana's speech within
    // kBriefingMaxSteps: it goes into a DianaSpeak (any input), or into an entity that has outputs this walk
    // follows and every one of them does. A dead end, another listener, a cycle or a longer path is other logic.
    bool Link(size_t index, int steps) {
        const BriefingLink& link = links[index];
        const BriefingNodeKind kind = KindOf(nodes, link.to);

        if (kind == BriefingNodeKind::Speaker)
            return link.kind == BriefingLinkKind::Connection;

        if (kind == BriefingNodeKind::Listener || steps >= kBriefingMaxSteps)
            return false;

        const bool forwarded = link.kind == BriefingLinkKind::OutputForwarding;
        const std::pair<int32_t, std::string> key {link.to, forwarded ? link.toPin : std::string()};

        if (const auto it = memo.find(key); it != memo.end())
            return it->second;

        if (!visiting.insert(key).second)
            return false;

        bool any = false;
        bool all = true;

        for (const size_t next : out[static_cast<size_t>(link.to)]) {
            if (forwarded && links[next].fromPin != key.second)
                continue;

            any = true;

            if (!Link(next, steps + 1)) {
                all = false;
                break;
            }
        }

        visiting.erase(key);
        return memo[key] = any && all;
    }

    // Every link carrying (entity, pin) only ever ends in Diana's speech.
    bool Pin(int32_t entity, const std::string& pin, int steps) {
        bool any = false;

        for (const size_t i : out[static_cast<size_t>(entity)]) {
            if (links[i].fromPin != pin)
                continue;

            any = true;

            if (!Link(i, steps))
                return false;
        }

        return any;
    }
};

void AddMute(BriefingFind& find, int32_t entity, const std::string& pin, int32_t listener,
             const std::vector<Reach>& reaches) {
    auto it = std::find_if(find.mutes.begin(), find.mutes.end(),
                           [&](const BriefingMute& mute) { return mute.entity == entity && mute.pin == pin; });

    if (it == find.mutes.end()) {
        find.mutes.push_back({entity, pin, listener, {}, 0});
        it = find.mutes.end() - 1;
    }

    for (const Reach& reach : reaches) {
        if (std::find(it->speakers.begin(), it->speakers.end(), reach.speaker) == it->speakers.end())
            it->speakers.push_back(reach.speaker);

        it->steps = std::max(it->steps, reach.steps);
    }
}

} // namespace

BriefingFind FindBriefingMutes(const std::vector<BriefingNode>& nodes, const std::vector<BriefingLink>& links) {
    BriefingFind find;
    std::vector<std::vector<size_t>> out(nodes.size());

    for (size_t i = 0; i < links.size(); ++i) {
        if (Carries(links[i]) && InRange(nodes, links[i].from) && InRange(nodes, links[i].to))
            out[static_cast<size_t>(links[i].from)].push_back(i);
    }

    SpeechOnlyWalk speechOnly {nodes, out, links, {}, {}};

    for (size_t l = 0; l < nodes.size(); ++l) {
        if (nodes[l].kind != BriefingNodeKind::Listener || !IsBriefingEvent(nodes[l].event))
            continue;

        const int32_t listener = static_cast<int32_t>(l);
        std::vector<Reach> all;

        for (const size_t first : out[l]) {
            if (links[first].fromPin != kEventOccurredPin)
                continue;

            const std::vector<Reach> reaches = Follow(nodes, out, links, first);
            all.insert(all.end(), reaches.begin(), reaches.end());
        }

        if (all.empty())
            continue;

        // Everything the listener starts is Diana's speech: its EventOccurred is blocked.
        if (speechOnly.Pin(listener, std::string(kEventOccurredPin), 1)) {
            AddMute(find, listener, std::string(kEventOccurredPin), listener, all);
            continue;
        }

        // The listener also starts other logic: block the pins that feed only Diana's speech.
        std::vector<std::pair<int32_t, std::string>> feeders;

        for (const Reach& reach : all) {
            const std::pair<int32_t, std::string> feeder {reach.feeder, reach.pin};

            if (std::find(feeders.begin(), feeders.end(), feeder) == feeders.end())
                feeders.push_back(feeder);
        }

        for (const auto& [entity, pin] : feeders) {
            std::vector<Reach> through;

            for (const Reach& reach : all) {
                if (reach.feeder == entity && reach.pin == pin)
                    through.push_back(reach);
            }

            if (entity != listener && speechOnly.Pin(entity, pin, through.front().steps))
                AddMute(find, entity, pin, listener, through);
            else
                find.skipped.push_back({listener, entity, pin});
        }
    }

    return find;
}

LineFind FindLineMutes(const std::vector<BriefingNode>& nodes, const std::vector<BriefingLink>& links,
                       const std::vector<int32_t>& silenced) {
    LineFind find;
    std::vector<std::vector<size_t>> out(nodes.size());

    for (size_t i = 0; i < links.size(); ++i) {
        if (Carries(links[i]) && InRange(nodes, links[i].from) && InRange(nodes, links[i].to))
            out[static_cast<size_t>(links[i].from)].push_back(i);
    }

    SpeechOnlyWalk speechOnly {nodes, out, links, {}, {}};

    for (size_t s = 0; s < nodes.size(); ++s) {
        const int32_t speaker = static_cast<int32_t>(s);

        if (nodes[s].kind != BriefingNodeKind::Speaker ||
            std::find(silenced.begin(), silenced.end(), speaker) != silenced.end())
            continue;

        for (const BriefingLink& link : links) {
            if (link.kind != BriefingLinkKind::Connection || link.to != speaker || link.toPin != kDianaLinePin ||
                !InRange(nodes, link.from))
                continue;

            auto it = std::find_if(find.mutes.begin(), find.mutes.end(), [&](const LineMute& mute) {
                return mute.entity == link.from && mute.pin == link.fromPin;
            });

            if (it == find.mutes.end()) {
                // The pin's other links that start more than Diana's speech: the signal is sent on along them.
                std::vector<size_t> resend;

                for (const size_t i : out[static_cast<size_t>(link.from)]) {
                    if (links[i].fromPin == link.fromPin && !speechOnly.Link(i, 1))
                        resend.push_back(i);
                }

                if (resend.size() > kLineMaxResend) {
                    find.skipped.push_back({speaker, link.from, link.fromPin});
                    continue;
                }

                find.mutes.push_back({link.from, link.fromPin, {}, std::move(resend)});
                it = find.mutes.end() - 1;
            }

            if (std::find(it->speakers.begin(), it->speakers.end(), speaker) == it->speakers.end())
                it->speakers.push_back(speaker);
        }
    }

    return find;
}

std::vector<BriefingFeed> SpeakerFeeds(const std::vector<BriefingNode>& nodes, const std::vector<BriefingLink>& links) {
    std::vector<BriefingFeed> feeds;

    for (size_t s = 0; s < nodes.size(); ++s) {
        if (nodes[s].kind != BriefingNodeKind::Speaker)
            continue;

        for (const BriefingLink& link : links) {
            if (link.kind == BriefingLinkKind::Connection && link.to == static_cast<int32_t>(s) &&
                InRange(nodes, link.from))
                feeds.push_back({link.to, link.from, link.fromPin, link.toPin});
        }
    }

    return feeds;
}

namespace {

bool HasAny(std::string_view name, std::initializer_list<std::string_view> parts) {
    return std::any_of(parts.begin(), parts.end(),
                       [&](std::string_view part) { return name.find(part) != std::string_view::npos; });
}

} // namespace

bool IsStoryClass(std::string_view className) {
    return HasAny(className,
                  {"Opportunit", "VIP", "Evacuat", "Escap", "Objective", "Contract", "HUD", "Hint", "Intel"});
}

bool MayHoldBriefing(bool eventOccurredOut, bool triggerIn) {
    return eventOccurredOut && triggerIn;
}

BriefingGraphCache::BriefingGraphCache(size_t maxGraphs, size_t maxLinks)
    : m_MaxGraphs(maxGraphs), m_MaxLinks(maxLinks) {}

void BriefingGraphCache::Put(uintptr_t key, BriefingGraph graph) {
    for (auto it = m_Order.begin(); it != m_Order.end(); ++it) {
        if (it->key == key) {
            m_Links -= it->graph.links.size();
            m_Order.erase(it);
            break;
        }
    }

    m_Links += graph.links.size();
    m_Order.push_back({key, std::move(graph)});

    // The newest copy always stays, even alone over the link budget.
    while (m_Order.size() > 1 && (m_Order.size() > m_MaxGraphs || m_Links > m_MaxLinks)) {
        m_Links -= m_Order.front().graph.links.size();
        m_Order.erase(m_Order.begin());
        ++m_Dropped;
    }
}

const BriefingGraph* BriefingGraphCache::Get(uintptr_t key) const {
    for (const Entry& entry : m_Order) {
        if (entry.key == key)
            return &entry.graph;
    }

    return nullptr;
}

bool BriefingGraphMatches(const BriefingGraph& graph, size_t subCount,
                          const std::vector<std::pair<int32_t, uint64_t>>& seenIds) {
    if (graph.ids.size() != subCount || subCount == 0)
        return false;

    for (const auto& [index, id] : seenIds) {
        if (index < 0 || static_cast<size_t>(index) >= graph.ids.size() || graph.ids[static_cast<size_t>(index)] != id)
            return false;
    }

    return true;
}

std::vector<uint64_t> FullDianaBricks(const std::vector<uint64_t>& swap, const std::vector<uint64_t>& own) {
    std::vector<uint64_t> out;

    for (const uint64_t rid : swap) {
        if (rid != 0 && std::find(own.begin(), own.end(), rid) == own.end() &&
            std::find(out.begin(), out.end(), rid) == out.end())
            out.push_back(rid);
    }

    return out;
}

} // namespace fr
