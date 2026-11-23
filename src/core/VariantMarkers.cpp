#include "VariantMarkers.h"

#include <algorithm>
#include <iterator>

namespace fr {

namespace {

constexpr std::string_view kBaseClasses[] = {kMapTrackerBaseClass, "ZGenericUIMapTrackerAspect"};

template <size_t N>
bool AnyIn(std::span<const std::string> names, const std::string_view (&set)[N]) {
    return std::any_of(names.begin(), names.end(), [&](const std::string& name) {
        return std::find(std::begin(set), std::end(set), name) != std::end(set);
    });
}

} // namespace

std::string_view MapLayerName(int32_t layer) {
    switch (static_cast<MapLayer>(layer)) {
        case MapLayer::Unspecified: return "unspecified";
        case MapLayer::Staircase: return "staircase";
        case MapLayer::AreaUndiscovered: return "undiscovered area";
        case MapLayer::Text: return "text";
        case MapLayer::DroppedItemsAndDisguises: return "dropped items";
        case MapLayer::Npc: return "NPC";
        case MapLayer::NorthIndicator: return "north indicator";
        case MapLayer::SecurityCamera: return "security camera";
        case MapLayer::AgencyPickup: return "agency pickup";
        case MapLayer::Opportunity: return "opportunity";
        case MapLayer::Exit: return "exit";
        case MapLayer::Objective: return "objective";
        case MapLayer::Target: return "target";
        case MapLayer::Opponent: return "opponent";
        case MapLayer::Hero: return "hero";
    }
    return "unknown";
}

bool IsMapTrackerClass(std::string_view className) {
    return className.find("TrackerAspect") != std::string_view::npos;
}

std::string_view MarkerClassOf(std::span<const std::string> interfaces) {
    std::string_view base;

    for (const std::string& name : interfaces) {
        if (!IsMapTrackerClass(name))
            continue;

        if (std::find(std::begin(kBaseClasses), std::end(kBaseClasses), name) == std::end(kBaseClasses))
            return name;

        if (base.empty())
            base = name;
    }

    return base;
}

MarkerAction DecideMarker(std::span<const std::string> interfaces, std::optional<int32_t> layer) {
    if (AnyIn(interfaces, kCharacterMarkerClasses))
        return MarkerAction::Character;

    if (AnyIn(interfaces, kStoryMarkerClasses))
        return MarkerAction::SwitchOff;

    const bool storyLayer = layer && (*layer == static_cast<int32_t>(MapLayer::Objective) ||
                                      *layer == static_cast<int32_t>(MapLayer::Opportunity));
    return storyLayer && !MarkerClassOf(interfaces).empty() ? MarkerAction::SwitchOff : MarkerAction::Leave;
}

std::string MarkerTallyText(const MarkerTally& tally) {
    const auto list = [](const std::map<std::string, size_t>& counts) {
        std::string text;

        for (const auto& [name, count] : counts)
            text += (text.empty() ? "" : ", ") + name + " x" + std::to_string(count);

        return text;
    };

    if (tally.switchedOff == 0 && tally.failed == 0 && tally.leftByKind.empty())
        return "none";

    std::string text = std::to_string(tally.switchedOff) + " switched off";

    if (!tally.offByClass.empty())
        text += " (" + list(tally.offByClass) + ")";

    if (tally.failed > 0)
        text += ", " + std::to_string(tally.failed) + " could not be switched off (they may still show)";

    if (!tally.leftByKind.empty())
        text += "; left as they are: " + list(tally.leftByKind);

    return text;
}

} // namespace fr
