#include "MissionStories.h"

#include <algorithm>
#include <iterator>

namespace fr {

bool IsOpportunityResource(uint64_t rid) {
    return rid == kOpportunityTblu || rid == kOpportunityTemp;
}

bool IsStoryInputPin(std::string_view pin) {
    return std::find(std::begin(kStoryInputPins), std::end(kStoryInputPins), pin) != std::end(kStoryInputPins);
}

StoryFind FindStoryMutes(const std::vector<int32_t>& stories, size_t count, const std::vector<BriefingLink>& links) {
    StoryFind find;
    const auto inRange = [&](int32_t index) { return index >= 0 && static_cast<size_t>(index) < count; };
    const auto isStory = [&](int32_t index) {
        return std::find(stories.begin(), stories.end(), index) != stories.end();
    };
    std::vector<int32_t> fed;

    for (const BriefingLink& link : links) {
        if (!isStory(link.to) || !inRange(link.from) || link.kind == BriefingLinkKind::OutputForwarding)
            continue;

        fed.push_back(link.to);

        if (link.kind == BriefingLinkKind::InputForwarding) {
            find.skipped.push_back({link.to, link.from, link.fromPin, link.toPin, StorySkipWhy::Forwarded});
            continue;
        }

        auto it = std::find_if(find.mutes.begin(), find.mutes.end(), [&](const StoryMute& mute) {
            return mute.entity == link.from && mute.pin == link.fromPin;
        });

        if (it == find.mutes.end()) {
            // The pin's other links (connections and output forwardings) that do not go into a story: the signal is
            // sent on along them.
            std::vector<size_t> resend;

            for (size_t i = 0; i < links.size(); ++i) {
                const BriefingLink& other = links[i];

                if (other.kind != BriefingLinkKind::InputForwarding && other.from == link.from &&
                    other.fromPin == link.fromPin && inRange(other.to) && !isStory(other.to))
                    resend.push_back(i);
            }

            if (resend.size() > kLineMaxResend) {
                find.skipped.push_back({link.to, link.from, link.fromPin, link.toPin, StorySkipWhy::TooManyLinks});
                continue;
            }

            find.mutes.push_back({link.from, link.fromPin, {}, {}, std::move(resend)});
            it = find.mutes.end() - 1;
        }

        it->stories.push_back(link.to);
        it->storyPins.push_back(link.toPin);
    }

    for (const int32_t story : stories) {
        if (inRange(story) && std::find(fed.begin(), fed.end(), story) == fed.end() &&
            std::find(find.unfed.begin(), find.unfed.end(), story) == find.unfed.end())
            find.unfed.push_back(story);
    }

    return find;
}

} // namespace fr
