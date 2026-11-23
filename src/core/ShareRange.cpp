#include "ShareRange.h"

#include <algorithm>
#include <cmath>

namespace fr {

ShareRange ClampShareRange(double min, double max, ShareRange fallback) {
    ShareRange r;
    r.min = std::clamp(std::isnan(min) ? fallback.min : min, 0.0, 1.0);
    r.max = std::max(std::clamp(std::isnan(max) ? fallback.max : max, 0.0, 1.0), r.min);
    return r;
}

double ShareAt(ShareRange range, double u) {
    return range.min + u * (range.max - range.min);
}

} // namespace fr
