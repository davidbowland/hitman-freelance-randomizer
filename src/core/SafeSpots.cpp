#include "SafeSpots.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace fr {

namespace {

std::string ValueText(const std::optional<int32_t>& value) {
    return value ? std::to_string(*value) : std::string("?");
}

std::string MetersText(double meters) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f m", meters);
    return text;
}

std::string BrickText(uint64_t rid) {
    char text[24];
    std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(rid));
    return text;
}

std::string PieceText(const PlacedPiece& piece, double distance) {
    return MetersText(distance) + " " + piece.what + " " + HexId(piece.id) + " at " + PointText(piece.at);
}

} // namespace

bool IsReadPoint(SpotPoint point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        return false;

    return point.x != 0.0f || point.y != 0.0f || point.z != 0.0f;
}

SafeActivity ActivityOf(const SafeSignals& signals) {
    if (!signals.routeOuts && !signals.state && !signals.count)
        return SafeActivity::Unknown;

    if ((signals.routeOuts && *signals.routeOuts > 1) || (signals.state && *signals.state > 0) ||
        (signals.count && *signals.count > 0))
        return SafeActivity::Active;

    return SafeActivity::Idle;
}

const char* ActivityName(SafeActivity activity) {
    switch (activity) {
        case SafeActivity::Active: return "picked";
        case SafeActivity::Idle: return "not picked";
        case SafeActivity::Unknown: return "unknown (values unread)";
    }

    return "unknown";
}

std::string SignalsText(const SafeSignals& signals) {
    return "route outs " + ValueText(signals.routeOuts) + ", state " + ValueText(signals.state) + ", count " +
           ValueText(signals.count);
}

std::string HexId(uint64_t id) {
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(id));
    return text;
}

std::string PointText(SpotPoint point) {
    char text[96];
    std::snprintf(text, sizeof(text), "(%.2f, %.2f, %.2f)", point.x, point.y, point.z);
    return text;
}

std::string SafeSpotText(size_t index, size_t total, const SafeSpot& spot) {
    std::string where = spot.at && IsReadPoint(*spot.at) ? "at " + PointText(*spot.at)
                                                         : "position unread (" +
                                                               (spot.at                   ? std::string("at the origin")
                                                                : spot.noPosition.empty() ? std::string("?")
                                                                                          : spot.noPosition) +
                                                               ")";
    return "spot " + std::to_string(index + 1) + "/" + std::to_string(total) + " entity " + HexId(spot.id) +
           " in brick " + BrickText(spot.brickRid) + " " + where + ": " + ActivityName(ActivityOf(spot.signals)) +
           " (" + SignalsText(spot.signals) + ")";
}

std::string SafeSummaryText(std::span<const SafeSpot> spots) {
    if (spots.empty())
        return "no safe spots";

    std::string picked;
    size_t count = 0;
    size_t unknown = 0;

    for (size_t i = 0; i < spots.size(); ++i) {
        const SafeActivity activity = ActivityOf(spots[i].signals);

        if (activity == SafeActivity::Unknown)
            ++unknown;

        if (activity != SafeActivity::Active)
            continue;

        ++count;
        picked += (picked.empty() ? "" : ", ") + std::string("spot ") + std::to_string(i + 1) + " (" +
                  HexId(spots[i].id) + ")";
    }

    std::string text = std::to_string(spots.size()) + (spots.size() == 1 ? " spot, " : " spots, ");

    if (count == 0)
        text += "none picked yet";
    else if (count == 1)
        text += "picked: " + picked;
    else
        text += std::to_string(count) + " look picked: " + picked;

    if (unknown > 0)
        text += "; " + std::to_string(unknown) + " unreadable";

    return text;
}

std::string SafeNearText(SpotPoint safe, std::string_view option, std::span<const PlacedPiece> pieces, double radius,
                         size_t max) {
    if (!IsReadPoint(safe))
        return "position unread; not compared";

    std::vector<SpotPoint> points;
    std::vector<size_t> read; // index in pieces of each point
    points.reserve(pieces.size());
    read.reserve(pieces.size());

    for (size_t i = 0; i < pieces.size(); ++i) {
        if (!IsReadPoint(pieces[i].at))
            continue;

        points.push_back(pieces[i].at);
        read.push_back(i);
    }

    const size_t unread = pieces.size() - points.size();
    const std::string unreadText =
        unread > 0 ? "; " + std::to_string(unread) + " with position unread, not compared" : std::string();
    const std::vector<size_t> near = NearestPoints(points, safe, points.size(), radius);
    const std::string from = " from " + std::string(option) + " within " + MetersText(radius);

    if (near.empty()) {
        const std::vector<size_t> nearest = NearestPoints(points, safe, 1, std::numeric_limits<double>::infinity());

        if (nearest.empty())
            return "nothing" + from + " (no placed entities read)" + unreadText;

        const PlacedPiece& piece = pieces[read[nearest.front()]];
        return "nothing" + from + " (nearest " + PieceText(piece, SpotDistance(piece.at, safe)) + ")" + unreadText;
    }

    std::string text = std::to_string(near.size()) + from + ", nearest first: ";

    for (size_t i = 0; i < near.size() && i < max; ++i) {
        const PlacedPiece& piece = pieces[read[near[i]]];
        text += (i == 0 ? "" : "; ") + PieceText(piece, SpotDistance(piece.at, safe));
    }

    if (near.size() > max)
        text += ", +" + std::to_string(near.size() - max) + " more";

    return text + unreadText;
}

} // namespace fr
