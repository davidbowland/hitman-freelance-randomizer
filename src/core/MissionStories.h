#pragma once

// Mission stories in a map variant's bricks (generic, found at runtime).
//
// Freelancer has no mission stories, but a variant brick made for its own
// contract brings its own: an instance of the game's opportunity template
// (gamecore/opportunities.template?/opportunity.entitytemplate) per mission
// story, with its stages, reveals, completions and failures (the other
// templates of opportunities.template) around it. Inside the template, the
// mission story is a ZIntelEntity (m_sId: the story's repository id,
// m_aStages, m_bIsMainOpportunity); there is no opportunity class of its own.
// Decoded from The Splitter's scenario_lambic: "Meet your
// brother" (503FA1E90A913A63, repository id 5b7cdb1e-..., the HUD's "Escort
// Angus Pritchard") is unlocked and tracked at gameplay start by the brick's
// own logic: a ZGameEventListenerEntity starts two TimerSimple, whose Out
// pins reach the instance's Unlock (33 ms) and SetTracked (500 ms) inputs.
// Its one reveal is a "Dummy Reveal" (opportunityrevealspatial), so nothing
// but those two pins ever shows it. The base maps' scenario bricks Freelancer
// loads keep their mission stories hidden (Chongqing's scenario_rat has no
// opportunity template at all); a variant's shows because its own brick
// unlocks and tracks it.
//
// Connection-driven input pins never pass through the SignalInputPin hook
// (core/DianaBriefing.h), so a mission story is switched off on the output
// side, as Diana's lines are: every pin connection of the blueprint into an
// opportunity instance (any input pin: Unlock, SetTracked, or whatever another
// brick uses) has its feeding output pin blocked, and the signal is sent on
// along that pin's other links (resend), so the rest of the variant's logic
// (its NPCs, dressing and story scripting) still runs. A story so cut off is
// never unlocked or tracked by the brick, so it is not shown. Its stages,
// completions, failures and reveal stay as they are (they belong to the story
// that never unlocks; a real reveal, which The Splitter's dummy is not, could
// still unlock it from inside the template when the player finds the story).
// A story fed through an
// input forwarding (its blueprint's own input pin, fed from the blueprint
// above) or by a pin that starts more than kLineMaxResend other links is left
// on and reported.

#include "DianaBriefing.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// The opportunity template: an instance's own blueprint factory has one of
// these resource ids (the TBLU, or the TEMP, depending on the factory).
constexpr uint64_t kOpportunityTblu = 0x00F0DE6F7BD2126Aull; // opportunities.template?/opportunity.entitytemplate
constexpr uint64_t kOpportunityTemp = 0x00881277F0AEF207ull;
bool IsOpportunityResource(uint64_t rid);

// The opportunity's input pins the decoded bricks drive (scenario_lambic):
// a blueprint with a connection into one of these is copied while the scene
// loads (BriefingGraphs.h), as one that may hold a briefing is.
constexpr std::string_view kStoryInputPins[] = {"Unlock", "SetTracked"};
bool IsStoryInputPin(std::string_view pin);

// One output pin to block.
struct StoryMute {
    int32_t entity;                     // the sub-entity whose output pin is blocked
    std::string pin;                    // "Out"
    std::vector<int32_t> stories;       // the opportunity instances it no longer reaches
    std::vector<std::string> storyPins; // their input pins it fed ("Unlock", "SetTracked"), in link order
    std::vector<size_t> resend;         // the pin's other links (indices) the signal is sent on along, in link order
};

enum class StorySkipWhy : uint8_t {
    TooManyLinks, // the feeding pin starts more than kLineMaxResend other links
    Forwarded,    // an input forwarding: the blueprint's own input pin feeds it (from the blueprint above)
};

// A feed left on.
struct StorySkip {
    int32_t story;
    int32_t feeder; // the entity whose pin feeds it (Forwarded: the forwarding's own end)
    std::string pin;
    std::string storyPin;
    StorySkipWhy why;
};

struct StoryFind {
    std::vector<StoryMute> mutes;   // one per feeding pin, in link order
    std::vector<StorySkip> skipped; // in link order
    std::vector<int32_t> unfed;     // stories nothing in this blueprint feeds (in stories order)
};

// stories are the opportunity instances among the blueprint's count
// sub-entities; links outside them are ignored.
StoryFind FindStoryMutes(const std::vector<int32_t>& stories, size_t count, const std::vector<BriefingLink>& links);

} // namespace fr
