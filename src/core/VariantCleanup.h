#pragma once

// Pure part of the variant cleanup: the scene NPCs a loaded map variant
// strands (VariantOption::removed: an act spot under its set dressing, or left
// standing on nothing by its deletes) are switched off once the scene's
// entities exist (m_bStartEnabled false, the way the game keeps an actor out
// of a scene), unless they are possible targets. They are never targets
// either (VariantTargetExclusions), so the target pool swap has taken them
// out of Freelancer's pool before. NPCs that are only never targets
// (VariantOption::notTargets, the full Landslide's never-targets)
// stay in the scene.

#include "Guid.h"
#include "Variants.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// One NPC to switch off and the option that strands it.
struct VariantRemoval {
    const VariantOption* option = nullptr;
    const StrandedNpc* npc = nullptr;
};

// The removed NPCs of every option in options whose bricks are all loaded
// (runtime resource ids, as VariantBrick::rid), in options' and each table's
// order; an NPC listed twice (by repository id) only once, malformed ids
// skipped. Under the full Landslide (fullLandslide), kLandslide is left out:
// mission_mamba loads all its bricks, but not the simple version's day crowd.
std::vector<VariantRemoval> VariantRemovals(std::span<const uint64_t> loadedBrickRids, bool fullLandslide,
                                            std::span<const VariantOption* const> options = AllVariantOptions());

// Their repository ids (the enforcer shuffle leaves them alone).
std::set<Guid> VariantRemovedRepos(std::span<const uint64_t> loadedBrickRids, bool fullLandslide,
                                   std::span<const VariantOption* const> options = AllVariantOptions());

// Whether an actor with repository id repo, found in brick brickRid, is npc
// (its brick, when it names one, must match).
bool IsStrandedNpc(const StrandedNpc& npc, const Guid& repo, uint64_t brickRid);

// The loaded bricks to walk for removals (indices into loadedBrickRids): the
// ones a removal names, in load order, then, when a removal names no brick,
// every other one. Empty when no removal's brick is loaded.
std::vector<size_t> CleanupBrickOrder(std::span<const uint64_t> loadedBrickRids,
                                      std::span<const VariantRemoval> removals);

// Freelancer's target pool, as the cleanup sees it.
enum class CleanupPool {
    Read,   // every target picker of the mission brick read
    None,   // a showdown: no target picker (its targets are the mission brick's own)
    Unread, // not found or not fully read: anyone could be a target, so nobody is switched off
};

CleanupPool DecideCleanupPool(bool showdown, bool missionBrickLoaded, size_t pickers, size_t pickersRead);

// What one listed NPC shows at scene allocation (after the target pool swap).
struct CleanupFacts {
    bool found;        // its actor is in the loaded bricks
    bool readable;     // its bits read
    bool inPool;       // in Freelancer's target pool (a possible target)
    bool startEnabled; // m_bStartEnabled
};

enum class CleanupAction {
    SwitchOff,  // m_bStartEnabled to false: the actor never appears
    InPool,     // left in: a possible target
    AlreadyOff, // start disabled already
    NotFound,
    Unreadable,
};

CleanupAction DecideCleanup(const CleanupFacts& facts);

std::string_view CleanupActionText(CleanupAction action);

// " (seen at (-19.61, 29.51, 1.44))" for an NPC with a seen position, else empty.
std::string StrandedNpcWhere(const StrandedNpc& npc);

} // namespace fr
