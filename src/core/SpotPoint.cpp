#include "SpotPoint.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fr {

double SpotDistance(SpotPoint a, SpotPoint b) {
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    const double dz = static_cast<double>(a.z) - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::vector<size_t> NearestPoints(std::span<const SpotPoint> points, SpotPoint at, size_t count, double maxDistance) {
    std::vector<std::pair<double, size_t>> near;

    for (size_t i = 0; i < points.size(); ++i) {
        const double d = SpotDistance(points[i], at);

        if (d <= maxDistance)
            near.emplace_back(d, i);
    }

    std::sort(near.begin(), near.end());
    std::vector<size_t> out;

    for (size_t i = 0; i < near.size() && i < count; ++i)
        out.push_back(near[i].second);

    return out;
}

} // namespace fr
