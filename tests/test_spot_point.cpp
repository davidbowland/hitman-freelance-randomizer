#include "doctest.h"

#include "SpotPoint.h"

#include <vector>

TEST_CASE("NearestPoints: nearest first, ties by index, capped by count and distance") {
    const std::vector<fr::SpotPoint> points {
        {10, 0, 0}, // 0: 10 m
        {0, 3, 0},  // 1: 3 m
        {0, 0, 3},  // 2: 3 m (height counts)
        {90, 0, 0}, // 3: too far
        {1, 0, 0},  // 4: 1 m
    };

    CHECK(fr::NearestPoints(points, {0, 0, 0}, 10, 80.0) == std::vector<size_t> {4, 1, 2, 0});
    CHECK(fr::NearestPoints(points, {0, 0, 0}, 2, 80.0) == std::vector<size_t> {4, 1});
    CHECK(fr::NearestPoints(points, {0, 0, 0}, 10, 100.0).size() == 5);
    CHECK(fr::NearestPoints({}, {0, 0, 0}, 10, 80.0).empty());
    CHECK(fr::SpotDistance({0, 0, 0}, {3, 4, 0}) == doctest::Approx(5.0));
}
