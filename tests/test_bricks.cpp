#include "doctest.h"

#include "BrickMemory.h"

#include <cstdint>
#include <vector>

TEST_CASE("brick_memory_empty") {
    const fr::BrickMemory memory;
    CHECK(memory.Empty());
    const std::vector<uint64_t> loaded {1, 2, 3};
    CHECK(memory.LoadedRemembered(loaded).empty());
}

TEST_CASE("brick_memory_selects_loaded_remembered") {
    fr::BrickMemory memory;
    memory.Remember(0x00097AA87B144150);
    memory.Remember(7);
    memory.Remember(7); // remembered once
    CHECK(memory.Size() == 2);

    // Only the remembered bricks that are loaded, in load order, once each.
    const std::vector<uint64_t> loaded {5, 7, 0x00097AA87B144150, 9, 7};
    CHECK(memory.LoadedRemembered(loaded) == std::vector<uint64_t> {7, 0x00097AA87B144150});

    // None of them loaded: full walk.
    const std::vector<uint64_t> other {1, 2};
    CHECK(memory.LoadedRemembered(other).empty());
    CHECK(memory.LoadedRemembered({}).empty());
}

TEST_CASE("brick_memory_contains") {
    fr::BrickMemory memory;
    memory.Remember(42);
    CHECK(memory.Contains(42));
    CHECK_FALSE(memory.Contains(43));
}

TEST_CASE("brick_memory_full_walk_counts") {
    fr::BrickMemory memory;

    // Nothing recorded: only an empty remembered walk falls back.
    CHECK(memory.RememberedWalkShort("scene_a", 0));
    CHECK_FALSE(memory.RememberedWalkShort("scene_a", 3));
    CHECK_FALSE(memory.LastFullWalk("scene_a").has_value());

    memory.RecordFullWalk("scene_a", 226);
    CHECK(memory.LastFullWalk("scene_a") == 226u);
    CHECK(memory.RememberedWalkShort("scene_a", 225));
    CHECK(memory.RememberedWalkShort("scene_a", 0));
    CHECK_FALSE(memory.RememberedWalkShort("scene_a", 226));
    CHECK_FALSE(memory.RememberedWalkShort("scene_a", 300));

    // Per scene.
    CHECK_FALSE(memory.RememberedWalkShort("scene_b", 1));

    // The last full walk counts.
    memory.RecordFullWalk("scene_a", 10);
    CHECK_FALSE(memory.RememberedWalkShort("scene_a", 10));
}

TEST_CASE("walk_plan_only_in_evergreen_scenes") {
    fr::BrickMemory memory;
    memory.Remember(7);
    const std::vector<uint64_t> loaded {7, 8};

    // Main menu and other non-Freelancer scenes: no walk at all.
    const fr::WalkPlan plan = memory.PlanWalk("menu", false, false, loaded);
    CHECK(plan.kind == fr::WalkKind::None);
    CHECK(plan.bricks.empty());
}

TEST_CASE("walk_plan_safehouse") {
    fr::BrickMemory memory;
    const std::vector<uint64_t> loaded {7, 8};

    // Nothing remembered: full walk.
    CHECK(memory.PlanWalk("snug", true, false, loaded).kind == fr::WalkKind::Full);

    // A remembered brick loaded: only that brick, even before a full walk of this scene.
    memory.Remember(8);
    const fr::WalkPlan plan = memory.PlanWalk("snug", true, false, loaded);
    CHECK(plan.kind == fr::WalkKind::Remembered);
    CHECK(plan.bricks == std::vector<uint64_t> {8});

    // No remembered brick loaded: full walk.
    const std::vector<uint64_t> other {1, 2};
    CHECK(memory.PlanWalk("snug", true, false, other).kind == fr::WalkKind::Full);
}

TEST_CASE("walk_plan_mission_full_once_per_scene") {
    fr::BrickMemory memory;
    memory.Remember(7);
    const std::vector<uint64_t> loaded {7, 8};

    // First time in this mission scene this session: one full walk, so
    // mission-specific bricks are searched.
    CHECK(memory.PlanWalk("mission_a", true, true, loaded).kind == fr::WalkKind::Full);

    memory.RecordFullWalk("mission_a", 226);
    const fr::WalkPlan plan = memory.PlanWalk("mission_a", true, true, loaded);
    CHECK(plan.kind == fr::WalkKind::Remembered);
    CHECK(plan.bricks == std::vector<uint64_t> {7});

    // Another mission scene gets its own full walk.
    CHECK(memory.PlanWalk("mission_b", true, true, loaded).kind == fr::WalkKind::Full);

    // A full walk that found nothing still counts as done.
    memory.RecordFullWalk("mission_c", 0);
    CHECK(memory.PlanWalk("mission_c", true, true, loaded).kind == fr::WalkKind::Remembered);
}

TEST_CASE("walk_visited_estimate") {
    fr::BrickMemory memory;

    // Defaults before any walk of that kind.
    CHECK(memory.VisitedEstimate(fr::WalkKind::Full) == fr::kFullWalkVisitedDefault);
    CHECK(memory.VisitedEstimate(fr::WalkKind::Remembered) == fr::kRememberedWalkVisitedDefault);
    CHECK(memory.VisitedEstimate(fr::WalkKind::None) == 0);

    // The largest walk of that kind so far, with some headroom, never below the default.
    memory.RecordVisited(fr::WalkKind::Full, 400'000);
    CHECK(memory.VisitedEstimate(fr::WalkKind::Full) == 400'000 + 400'000 / 8);
    memory.RecordVisited(fr::WalkKind::Full, 300'000);
    CHECK(memory.VisitedEstimate(fr::WalkKind::Full) == 400'000 + 400'000 / 8);
    memory.RecordVisited(fr::WalkKind::Remembered, 10);
    CHECK(memory.VisitedEstimate(fr::WalkKind::Remembered) == fr::kRememberedWalkVisitedDefault);

    // Never more than the cap on reserved slots.
    memory.RecordVisited(fr::WalkKind::Full, 50'000'000);
    CHECK(memory.VisitedEstimate(fr::WalkKind::Full) == fr::kVisitedReserveMax);
}
