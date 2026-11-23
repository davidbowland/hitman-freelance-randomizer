#pragma once

// A world position (meters, z up), the distance between two, and the points
// nearest one.

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace fr {

struct SpotPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f; // up
};

// 3D distance in meters.
double SpotDistance(SpotPoint a, SpotPoint b);

// The indices of points within maxDistance of at, nearest first (ties by
// index), at most count.
std::vector<size_t> NearestPoints(std::span<const SpotPoint> points, SpotPoint at, size_t count, double maxDistance);

} // namespace fr
