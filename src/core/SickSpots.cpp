#include "SickSpots.h"

#include <algorithm>
#include <limits>

namespace fr {

SickTemplate SickTemplateOf(uint64_t rid) {
    if (rid == 0)
        return SickTemplate::None;

    if (rid == kSickSpotTemp || rid == kSickSpotTblu)
        return SickTemplate::Normal;

    if (rid == kSickSpotHighTemp || rid == kSickSpotHighTblu)
        return SickTemplate::High;

    return SickTemplate::None;
}

bool IsToiletCore(uint64_t rid) {
    return rid != 0 && (rid == kToiletCoreTemp || rid == kToiletCoreTblu);
}

const char* SickTemplateName(SickTemplate t) {
    switch (t) {
        case SickTemplate::Normal: return "normal";
        case SickTemplate::High: return "high";
        case SickTemplate::None: break;
    }

    return "none";
}

std::string KillGuideTypeName(std::optional<int32_t> type) {
    if (!type)
        return "type unread";

    switch (*type) {
        case 0: return "toilet drown";
        case 1: return "snap and toss";
        case 2: return "push";
        case 3: return "no animation";
        case 4: return "interrupt animation";
        default: break;
    }

    return "type " + std::to_string(*type);
}

bool KillGuideCounts(std::optional<int32_t> type) {
    return !type || (*type >= 0 && *type <= 2);
}

const char* SickSpotKindName(SickSpotKind kind) {
    switch (kind) {
        case SickSpotKind::Toilet: return "toilet (drown)";
        case SickSpotKind::KillGuide: return "kill spot";
        case SickSpotKind::Plain: return "plain";
        case SickSpotKind::Unknown: break;
    }

    return "unknown (position unread)";
}

bool IsKillSpot(SickSpotKind kind) {
    return kind == SickSpotKind::Toilet || kind == SickSpotKind::KillGuide;
}

SickVerdict ClassifySickSpot(bool inToiletCore, std::optional<SpotPoint> at, std::span<const KillGuide> guides,
                             double radius) {
    SickVerdict verdict;

    if (at) {
        for (size_t i = 0; i < guides.size(); ++i) {
            if (!guides[i].at || !KillGuideCounts(guides[i].type))
                continue;

            const double d = SpotDistance(*at, *guides[i].at);

            if (!verdict.distance || d < *verdict.distance) {
                verdict.nearest = i;
                verdict.distance = d;
            }
        }
    }

    if (inToiletCore)
        verdict.kind = SickSpotKind::Toilet;
    else if (!at)
        verdict.kind = SickSpotKind::Unknown;
    else if (verdict.distance && *verdict.distance <= radius)
        verdict.kind = SickSpotKind::KillGuide;
    else
        verdict.kind = SickSpotKind::Plain;

    return verdict;
}

int32_t ClampSickBoost(int32_t boost) {
    return std::clamp(boost, int32_t {0}, kSickBoostMax);
}

int32_t BoostedSickPriority(int32_t before, int32_t boost) {
    const int64_t sum = static_cast<int64_t>(before) + ClampSickBoost(boost);
    return static_cast<int32_t>(std::min<int64_t>(sum, std::numeric_limits<int32_t>::max()));
}

int32_t SickPriorityToWrite(int32_t current, const std::optional<SickRaise>& earlier, int32_t boost) {
    const int32_t base = earlier && earlier->written == current ? earlier->original : current;
    return BoostedSickPriority(base, boost);
}

std::optional<int32_t> SickPriorityToRestore(int32_t current, const std::optional<SickRaise>& earlier) {
    if (earlier && earlier->written == current && earlier->original != current)
        return earlier->original;

    return std::nullopt;
}

SickNearest NearestSickSpots(std::span<const SickSpotPlace> spots, SpotPoint p) {
    SickNearest out;

    for (size_t i = 0; i < spots.size(); ++i) {
        if (!spots[i].at)
            continue;

        const double d = SpotDistance(*spots[i].at, p);

        if (IsKillSpot(spots[i].kind)) {
            if (!out.killDistance || d < *out.killDistance) {
                out.kill = i;
                out.killDistance = d;
            }
        }
        else if (!out.plainDistance || d < *out.plainDistance) {
            out.plain = i;
            out.plainDistance = d;
        }
    }

    return out;
}

std::string SickSummaryText(const SickTally& t, int32_t boost, bool on) {
    std::string text = std::to_string(t.spots) + " sick spots (" + std::to_string(t.normal) + " normal, " +
                       std::to_string(t.high) + " high): " + std::to_string(t.toilet) + " toilets, " +
                       std::to_string(t.killGuide) + " near a kill guide, " + std::to_string(t.plain) + " plain";

    if (t.unknown)
        text += ", " + std::to_string(t.unknown) + " unknown";

    text += "; " + std::to_string(t.guides) + " kill guides (" + std::to_string(t.guidesCounting) + " of a kill type)";

    const size_t normalKill = t.raised + t.notRaised;
    const int32_t clamped = ClampSickBoost(boost);

    if (!on)
        text += "; priority left alone (sick_spot_kills off)";
    else if (clamped == 0)
        text += "; priority left alone (sick_spot_kill_boost 0)";
    else {
        text += "; priority raised by " + std::to_string(clamped) + " on " + std::to_string(t.raised) + " of " +
                std::to_string(normalKill) + " normal kill spots";

        if (t.notRaised)
            text += " (not raised on " + std::to_string(t.notRaised) + ": see the spot lines)";
    }

    if (t.highKill)
        text += ", " + std::to_string(t.highKill) + (t.highKill == 1 ? " high kill spot" : " high kill spots") +
                " left at its priority (the high template has none to raise)";

    return text;
}

} // namespace fr
