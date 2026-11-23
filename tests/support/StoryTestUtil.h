#pragma once

// Whether every feed of a story in a blueprint is blocked (at least one, none
// skipped): FindStoryMutes switches it off.

#include "MissionStories.h"

#include <algorithm>
#include <cstdint>

namespace fr::test {

inline bool StorySwitchedOff(const StoryFind& find, int32_t story) {
    const bool skipped = std::any_of(find.skipped.begin(), find.skipped.end(),
                                     [&](const StorySkip& skip) { return skip.story == story; });
    const bool muted = std::any_of(find.mutes.begin(), find.mutes.end(), [&](const StoryMute& mute) {
        return std::find(mute.stories.begin(), mute.stories.end(), story) != mute.stories.end();
    });
    return muted && !skipped;
}

} // namespace fr::test
