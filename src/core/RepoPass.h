#pragma once

#include <cstddef>

namespace fr {

// True when a pass over pro.repo has visited more nodes than the map holds
// (plus one of slack): its links are broken, and the pass stops as a fault.
constexpr bool RepoPassOverran(size_t scanned, size_t entryCount) {
    return scanned > entryCount + 1;
}

} // namespace fr
