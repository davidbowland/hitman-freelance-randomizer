#include "BrickMemory.h"

#include <algorithm>
#include <utility>

namespace fr {

void BrickMemory::Remember(uint64_t rid) {
    m_Rids.insert(rid);
}

bool BrickMemory::Contains(uint64_t rid) const {
    return m_Rids.contains(rid);
}

bool BrickMemory::Empty() const {
    return m_Rids.empty();
}

size_t BrickMemory::Size() const {
    return m_Rids.size();
}

std::vector<uint64_t> BrickMemory::LoadedRemembered(std::span<const uint64_t> loaded) const {
    std::vector<uint64_t> result;
    for (const uint64_t rid : loaded) {
        if (m_Rids.contains(rid) && std::find(result.begin(), result.end(), rid) == result.end())
            result.push_back(rid);
    }
    return result;
}

void BrickMemory::RecordFullWalk(std::string_view scene, size_t found) {
    m_FullWalkCounts.insert_or_assign(std::string(scene), found);
}

std::optional<size_t> BrickMemory::LastFullWalk(std::string_view scene) const {
    const auto it = m_FullWalkCounts.find(scene);
    if (it == m_FullWalkCounts.end())
        return std::nullopt;
    return it->second;
}

bool BrickMemory::RememberedWalkShort(std::string_view scene, size_t found) const {
    if (found == 0)
        return true;
    const auto last = LastFullWalk(scene);
    return last && found < *last;
}

WalkPlan BrickMemory::PlanWalk(std::string_view scene, bool evergreen, bool mission,
                               std::span<const uint64_t> loaded) const {
    if (!evergreen)
        return {WalkKind::None, {}};

    if (mission && !LastFullWalk(scene))
        return {WalkKind::Full, {}};

    std::vector<uint64_t> remembered = LoadedRemembered(loaded);

    if (remembered.empty())
        return {WalkKind::Full, {}};

    return {WalkKind::Remembered, std::move(remembered)};
}

void BrickMemory::RecordVisited(WalkKind kind, size_t visited) {
    if (kind == WalkKind::Full)
        m_MaxFullVisited = std::max(m_MaxFullVisited, visited);
    else if (kind == WalkKind::Remembered)
        m_MaxRememberedVisited = std::max(m_MaxRememberedVisited, visited);
}

size_t BrickMemory::VisitedEstimate(WalkKind kind) const {
    size_t largest = 0;
    size_t fallback = 0;

    if (kind == WalkKind::Full) {
        largest = m_MaxFullVisited;
        fallback = kFullWalkVisitedDefault;
    }
    else if (kind == WalkKind::Remembered) {
        largest = m_MaxRememberedVisited;
        fallback = kRememberedWalkVisitedDefault;
    }
    else {
        return 0;
    }

    const size_t withHeadroom = largest > kVisitedReserveMax ? kVisitedReserveMax : largest + largest / 8;
    return std::min(std::max(withHeadroom, fallback), kVisitedReserveMax);
}

} // namespace fr
