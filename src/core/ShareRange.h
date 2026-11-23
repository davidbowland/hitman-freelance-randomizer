#pragma once

// The share a shuffle draws per mission between a least and a most share:
// the door lock, enforcer and target pool shuffles' *_min / *_max percent
// settings (fractions here).

namespace fr {

struct ShareRange {
    double min = 0.0;
    double max = 1.0;
};

// Each end clamped to [0, 1] (NaN: that end of fallback); max raised to min when below it.
ShareRange ClampShareRange(double min, double max, ShareRange fallback);

// The share at u in [0, 1]: min + u * (max - min).
double ShareAt(ShareRange range, double u);

} // namespace fr
