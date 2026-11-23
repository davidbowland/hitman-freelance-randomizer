#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// What a scene-start entity walk covers.
enum class WalkKind {
    None,       // no walk (not a Freelancer scene)
    Remembered, // only the remembered bricks that are loaded
    Full,       // every loaded brick
};

struct WalkPlan {
    WalkKind kind;
    std::vector<uint64_t> bricks; // for Remembered: the bricks to walk, in load order
};

// Slots reserved in the walk's visited set before any walk of that kind this
// session (a safehouse walk passes 200,000 entities in 13 bricks),
// and the most ever reserved.
constexpr size_t kFullWalkVisitedDefault = 262'144;
constexpr size_t kRememberedWalkVisitedDefault = 8'192;
constexpr size_t kVisitedReserveMax = 1'048'576;

// Runtime resource IDs of bricks where difficulty parameters were found this
// session, so later scenes can walk only those bricks.
class BrickMemory {
public:
    void Remember(uint64_t rid);
    bool Contains(uint64_t rid) const;
    bool Empty() const;
    size_t Size() const;

    // The remembered IDs among the loaded ones, in load order, each once.
    // Empty when no remembered brick is loaded (walk everything).
    std::vector<uint64_t> LoadedRemembered(std::span<const uint64_t> loaded) const;

    // How many parameters the last walk of every loaded brick found in a scene.
    void RecordFullWalk(std::string_view scene, size_t found);
    std::optional<size_t> LastFullWalk(std::string_view scene) const;

    // True when a walk of only the remembered bricks found too little: nothing,
    // or fewer parameters than the scene's last full walk. Walk everything then.
    bool RememberedWalkShort(std::string_view scene, size_t found) const;

    // The walk at a scene's start. Only Freelancer scenes (type "evergreen")
    // are walked. A mission scene gets one full walk per session (so bricks
    // only that mission loads are searched once); after that, and in the
    // safehouse, only the remembered bricks when any is loaded, else everything.
    WalkPlan PlanWalk(std::string_view scene, bool evergreen, bool mission, std::span<const uint64_t> loaded) const;

    // Entities a walk of that kind visited; the visited-set size to reserve
    // for the next one (the largest so far plus 1/8, at least the default, at
    // most kVisitedReserveMax; 0 for None).
    void RecordVisited(WalkKind kind, size_t visited);
    size_t VisitedEstimate(WalkKind kind) const;

private:
    std::set<uint64_t> m_Rids;
    std::map<std::string, size_t, std::less<>> m_FullWalkCounts;
    size_t m_MaxFullVisited = 0;
    size_t m_MaxRememberedVisited = 0;
};

} // namespace fr
