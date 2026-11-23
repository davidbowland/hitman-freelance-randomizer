#include "doctest.h"

#include "support/FullVariantTestUtil.h"

#include "LandslideFull.h"
#include "MoverTable.h"
#include "SceneAlias.h"
#include "TargetPool.h"
#include "Variants.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr const char* kScene = "assembly:/_pro/scenes/missions/skunk/scene_skunk_mild_solitaire.entity";
constexpr int32_t kCivilian = 0;
constexpr int32_t kGuard = 1;
constexpr uint64_t kSceneBrick = 0x77;
constexpr uint64_t kMissionBrick = 0x99;

fr::Guid Repo(uint64_t low) {
    return fr::Guid::FromHalves(0xAAAA'BBBB'CCCC'DDDDull, low);
}

// Pool handles 1..n; every fourth a guard, the rest civilians.
std::vector<fr::PoolEntry> Pool(size_t n) {
    std::vector<fr::PoolEntry> pool;

    for (size_t i = 0; i < n; ++i)
        pool.push_back({1 + i, true, i % 4 == 0 ? kGuard : kCivilian, Repo(1 + i), kSceneBrick, true});

    return pool;
}

fr::TargetCandidate Candidate(uint64_t handle, int32_t kind = kCivilian) {
    fr::TargetCandidate c;
    c.handle = handle;
    c.brickRid = kSceneBrick;
    c.entityId = handle * 3;
    c.startEnabled = true;
    c.crowd = false;
    c.repo = Repo(handle);
    c.kind = kind;
    return c;
}

// Every candidate's repository id, as a scene's mover table.
std::set<fr::Guid> MoversOf(std::span<const fr::TargetCandidate> candidates) {
    std::set<fr::Guid> movers;

    for (const auto& c : candidates) {
        if (c.repo)
            movers.insert(*c.repo);
    }

    return movers;
}

// PlanTargetSwaps with every candidate a known mover (the mover rule has its own tests).
fr::TargetPoolPlan Plan(int32_t seed, std::string_view scene, std::span<const fr::PoolEntry> pool,
                        std::span<const fr::TargetCandidate> candidates, double share, uint64_t missionBrick) {
    const auto movers = MoversOf(candidates);
    return fr::PlanTargetSwaps(seed, scene, pool, candidates, share, missionBrick, &movers);
}

// fr::ApplyTargetSwaps on the handles.
std::vector<uint64_t> Applied(std::span<const fr::PoolEntry> pool, std::span<const fr::TargetCandidate> candidates,
                              const fr::TargetPoolPlan& plan) {
    std::vector<uint64_t> handles;

    for (const fr::PoolEntry& entry : pool)
        handles.push_back(entry.handle);

    return fr::ApplyTargetSwaps(std::move(handles), plan, [&](size_t i) { return candidates[i].handle; });
}

// Handles 1001..1000+n; every third a guard.
std::vector<fr::TargetCandidate> Candidates(size_t n) {
    std::vector<fr::TargetCandidate> out;

    for (size_t i = 0; i < n; ++i)
        out.push_back(Candidate(1001 + i, i % 3 == 0 ? kGuard : kCivilian));

    return out;
}

} // namespace

TEST_CASE("TargetSwapCount rounds the share of the pool and clamps it") {
    CHECK(fr::TargetSwapCount(40, 0.25) == 10);
    CHECK(fr::TargetSwapCount(86, 0.25) == 22); // 21.5 rounds up
    CHECK(fr::TargetSwapCount(3, 0.25) == 1);   // 0.75
    CHECK(fr::TargetSwapCount(1, 0.25) == 0);
    CHECK(fr::TargetSwapCount(40, 0.0) == 0);
    CHECK(fr::TargetSwapCount(40, -1.0) == 0);
    CHECK(fr::TargetSwapCount(40, 2.0) == 40);
    CHECK(fr::TargetSwapCount(40, std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(fr::TargetSwapCount(0, 0.5) == 0);
    CHECK(fr::ClampSwapShare(0.4) == doctest::Approx(0.4));
}

TEST_CASE("PlanTargetSwaps replaces the share, keeps the size and repeats no actor") {
    const auto pool = Pool(40);
    const auto candidates = Candidates(60);
    const auto plan = Plan(1234, kScene, pool, candidates, 0.25, kMissionBrick);

    CHECK(plan.wanted == 10);
    CHECK(plan.replaceable == 40);
    CHECK(plan.eligible == 60);
    REQUIRE(plan.swaps.size() == 10);

    const auto after = Applied(pool, candidates, plan);
    CHECK(after.size() == pool.size());
    CHECK(std::set<uint64_t>(after.begin(), after.end()).size() == after.size());

    std::set<size_t> slots;

    for (size_t i = 0; i < plan.swaps.size(); ++i) {
        slots.insert(plan.swaps[i].slot);

        if (i > 0)
            CHECK(plan.swaps[i - 1].slot < plan.swaps[i].slot);
    }

    CHECK(slots.size() == 10);

    size_t changed = 0;

    for (size_t i = 0; i < after.size(); ++i)
        changed += after[i] != pool[i].handle ? 1 : 0;

    CHECK(changed == 10);
}

TEST_CASE("PlanTargetSwaps is deterministic and independent of candidate order") {
    const auto pool = Pool(40);
    auto candidates = Candidates(60);
    const auto a = Applied(pool, candidates, Plan(7, kScene, pool, candidates, 0.25, kMissionBrick));
    const auto b = Applied(pool, candidates, Plan(7, kScene, pool, candidates, 0.25, kMissionBrick));
    CHECK(a == b);

    // Another walk order (and other pointer values) gives the same actors in the same slots.
    std::vector<fr::TargetCandidate> reversed(candidates.rbegin(), candidates.rend());
    const auto c = Applied(pool, reversed, Plan(7, kScene, pool, reversed, 0.25, kMissionBrick));
    CHECK(a == c);

    // Scene case does not matter.
    std::string upper(kScene);

    for (char& ch : upper)
        ch = static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);

    const auto d = Applied(pool, candidates, Plan(7, upper, pool, candidates, 0.25, kMissionBrick));
    CHECK(a == d);

    // Another seed or scene gives another pool.
    const auto e = Applied(pool, candidates, Plan(8, kScene, pool, candidates, 0.25, kMissionBrick));
    const auto f = Applied(pool, candidates,
                           Plan(7, "assembly:/_pro/scenes/missions/colombia/scene_hippo_mild_trompo.entity", pool,
                                candidates, 0.25, kMissionBrick));
    CHECK(a != e);
    CHECK(a != f);
}

TEST_CASE("PlanTargetSwaps filters candidates by the rules") {
    const auto pool = Pool(8);
    std::vector<fr::TargetCandidate> candidates;

    candidates.push_back(Candidate(2001)); // eligible

    auto noHandle = Candidate(0);
    candidates.push_back(noHandle);

    candidates.push_back(Candidate(2001)); // duplicate of the first

    auto inPool = Candidate(3);
    inPool.repo = Repo(3);
    candidates.push_back(inPool);

    auto disabled = Candidate(2002);
    disabled.startEnabled = false;
    candidates.push_back(disabled);

    auto unreadable = Candidate(2003);
    unreadable.startEnabled.reset();
    candidates.push_back(unreadable);

    auto crowd = Candidate(2004);
    crowd.crowd = true;
    candidates.push_back(crowd);

    auto noRepo = Candidate(2005);
    noRepo.repo.reset();
    candidates.push_back(noRepo);

    auto emptyRepo = Candidate(2006);
    emptyRepo.repo = fr::Guid {};
    candidates.push_back(emptyRepo);

    auto sharesWithPool = Candidate(2007);
    sharesWithPool.repo = Repo(5); // pool entry 5's id
    candidates.push_back(sharesWithPool);

    auto sharedA = Candidate(2008);
    sharedA.repo = Repo(9999);
    auto sharedB = Candidate(2009);
    sharedB.repo = Repo(9999);
    candidates.push_back(sharedA);
    candidates.push_back(sharedB);

    auto supplier = Candidate(2010);
    supplier.role = "supplier";
    candidates.push_back(supplier);

    candidates.push_back(Candidate(2011)); // eligible

    const auto plan = Plan(1, kScene, pool, candidates, 1.0, kMissionBrick);
    using R = fr::CandidateReject;
    const std::vector<R> expected {R::None,         R::NoHandle,     R::Duplicate, R::InPool,   R::StartDisabled,
                                   R::Unreadable,   R::Crowd,        R::NoRepoId,  R::NoRepoId, R::SharedRepoId,
                                   R::SharedRepoId, R::SharedRepoId, R::RoleActor, R::None};
    CHECK(plan.verdicts == expected);
    CHECK(plan.eligible == 2);
    CHECK(plan.rejected.at(R::SharedRepoId) == 3);
    CHECK(plan.rejected.at(R::NoRepoId) == 2);

    // Share 1.0 wants all 8 but only the two eligible actors are used.
    CHECK(plan.wanted == 8);
    REQUIRE(plan.swaps.size() == 2);
    std::set<size_t> used;

    for (const auto& swap : plan.swaps)
        used.insert(swap.candidate);

    CHECK(used == std::set<size_t> {0, 13});
}

TEST_CASE("PlanTargetSwaps leaves unreadable pool entries alone") {
    auto pool = Pool(8);

    for (size_t i = 0; i < pool.size(); ++i)
        pool[i].replaceable = i % 2 == 0;

    const auto candidates = Candidates(20);
    const auto plan = Plan(5, kScene, pool, candidates, 1.0, kMissionBrick);
    CHECK(plan.replaceable == 4);
    REQUIRE(plan.swaps.size() == 4);

    for (const auto& swap : plan.swaps)
        CHECK(swap.slot % 2 == 0);
}

TEST_CASE("PlanTargetSwaps prefers like for like") {
    // 4 guards and 4 civilians in the pool; 4 guard and 4 civilian candidates.
    std::vector<fr::PoolEntry> pool;

    for (uint64_t i = 0; i < 8; ++i)
        pool.push_back({1 + i, true, i < 4 ? kGuard : kCivilian, Repo(1 + i), kSceneBrick, true});

    std::vector<fr::TargetCandidate> candidates;

    for (uint64_t i = 0; i < 8; ++i)
        candidates.push_back(Candidate(100 + i, i % 2 == 0 ? kGuard : kCivilian));

    for (const int32_t seed : {1, 2, 3, 4, 5}) {
        const auto plan = Plan(seed, kScene, pool, candidates, 1.0, kMissionBrick);
        REQUIRE(plan.swaps.size() == 8);

        for (const auto& swap : plan.swaps) {
            CHECK(swap.sameKind);
            CHECK(candidates[swap.candidate].kind == pool[swap.slot].kind);
        }
    }

    // Only civilian candidates: guard slots still get one, marked cross kind.
    std::vector<fr::TargetCandidate> civilians;

    for (uint64_t i = 0; i < 8; ++i)
        civilians.push_back(Candidate(200 + i, kCivilian));

    const auto plan = Plan(1, kScene, pool, civilians, 1.0, kMissionBrick);
    REQUIRE(plan.swaps.size() == 8);

    for (const auto& swap : plan.swaps)
        CHECK(swap.sameKind == (pool[swap.slot].kind == kCivilian));

    // Unknown kinds never count as like for like.
    auto unknownPool = pool;

    for (auto& entry : unknownPool)
        entry.kind = fr::kUnknownActorKind;

    for (const auto& swap : Plan(1, kScene, unknownPool, candidates, 1.0, kMissionBrick).swaps)
        CHECK_FALSE(swap.sameKind);
}

TEST_CASE("PlanTargetSwaps with nothing to do") {
    const auto pool = Pool(40);
    CHECK(Plan(1, kScene, pool, {}, 0.25, kMissionBrick).swaps.empty());
    CHECK(Plan(1, kScene, pool, Candidates(10), 0.0, kMissionBrick).swaps.empty());
    CHECK(Plan(1, kScene, {}, Candidates(10), 0.25, kMissionBrick).swaps.empty());

    // Fewer eligible actors than wanted: all of them, no more.
    const auto plan = Plan(1, kScene, pool, Candidates(3), 0.25, kMissionBrick);
    CHECK(plan.wanted == 10);
    CHECK(plan.capped);
    CHECK(plan.swaps.size() == 3);
}

TEST_CASE("DrawSwapShare: uniform over [0, 1], the same for a seed, spread across seeds") {
    CHECK(fr::DrawSwapShare(1234, kScene) == fr::DrawSwapShare(1234, kScene));

    double low = 1.0;
    double high = 0.0;
    double sum = 0.0;
    std::set<double> distinct;
    size_t buckets[10] = {};
    constexpr int kSeeds = 5'000;

    for (int32_t seed = 1; seed <= kSeeds; ++seed) {
        const double share = fr::DrawSwapShare(seed, kScene);
        REQUIRE(share >= 0.0);
        REQUIRE(share <= 1.0);
        low = std::min(low, share);
        high = std::max(high, share);
        sum += share;
        distinct.insert(share);
        ++buckets[std::min<size_t>(9, static_cast<size_t>(share * 10.0))];
    }

    CHECK(low < 0.01);  // near none
    CHECK(high > 0.99); // near all
    CHECK(sum / kSeeds == doctest::Approx(0.5).epsilon(0.03));
    CHECK(distinct.size() > kSeeds - 5);

    for (const size_t count : buckets) {
        CHECK(count > kSeeds / 10 * 8 / 10);
        CHECK(count < kSeeds / 10 * 12 / 10);
    }

    // The scene is part of the roll.
    CHECK(fr::DrawSwapShare(1234, kScene) !=
          fr::DrawSwapShare(1234, "assembly:/_pro/scenes/missions/skunk/scene_other_mild.entity"));
}

TEST_CASE("PlanTargetSwaps: wanted is the share of the replaceable entries") {
    auto pool = Pool(40);

    for (size_t i = 0; i < pool.size(); ++i)
        pool[i].replaceable = i % 2 == 0;

    const auto plan = Plan(9, kScene, pool, Candidates(60), 0.5, kMissionBrick);
    CHECK(plan.replaceable == 20);
    CHECK(plan.wanted == 10);
    CHECK_FALSE(plan.capped);
    CHECK(plan.swaps.size() == 10);
}

TEST_CASE("PlanTargetSwaps: eligible movers cap the swaps, no actor twice") {
    // Mendoza: 100 entries, 32 eligible movers.
    const auto pool = Pool(100);
    const auto candidates = Candidates(32);

    SUBCASE("all wanted") {
        const auto plan = Plan(3, kScene, pool, candidates, 1.0, kMissionBrick);
        CHECK(plan.wanted == 100);
        CHECK(plan.eligible == 32);
        CHECK(plan.capped);
        REQUIRE(plan.swaps.size() == 32);

        const auto after = Applied(pool, candidates, plan);
        CHECK(after.size() == 100);
        CHECK(std::set<uint64_t>(after.begin(), after.end()).size() == 100);

        std::set<size_t> used;

        for (const auto& swap : plan.swaps)
            used.insert(swap.candidate);

        CHECK(used.size() == 32);
    }

    SUBCASE("fewer wanted than eligible") {
        const auto plan = Plan(3, kScene, pool, candidates, 0.2, kMissionBrick);
        CHECK(plan.wanted == 20);
        CHECK_FALSE(plan.capped);
        CHECK(plan.swaps.size() == 20);
    }

    SUBCASE("exactly as many wanted as eligible") {
        const auto plan = Plan(3, kScene, pool, candidates, 0.32, kMissionBrick);
        CHECK(plan.wanted == 32);
        CHECK_FALSE(plan.capped);
        CHECK(plan.swaps.size() == 32);
    }

    SUBCASE("share 0 swaps nothing") {
        const auto plan = Plan(3, kScene, pool, candidates, 0.0, kMissionBrick);
        CHECK(plan.wanted == 0);
        CHECK_FALSE(plan.capped);
        CHECK(plan.swaps.empty());

        const auto after = Applied(pool, candidates, plan);

        for (size_t i = 0; i < after.size(); ++i)
            CHECK(after[i] == pool[i].handle);
    }

    SUBCASE("a drawn share") {
        const double share = fr::DrawSwapShare(77, kScene);
        const auto plan = Plan(77, kScene, pool, candidates, share, kMissionBrick);
        CHECK(plan.wanted == fr::TargetSwapCount(100, share));
        CHECK(plan.swaps.size() == std::min<size_t>(plan.wanted, 32));
        CHECK(plan.capped == (plan.wanted > 32));
    }
}

TEST_CASE("TargetSourceBricks: bricks of start-enabled pool actors, never the mission brick") {
    std::vector<fr::PoolEntry> pool {
        {1, true, kCivilian, Repo(1), kSceneBrick, true},
        {2, true, kCivilian, Repo(2), kMissionBrick, false},
        {3, true, kCivilian, Repo(3), kMissionBrick, true}, // mission brick: never a source
        {4, true, kCivilian, Repo(4), 0x55, false},         // start disabled: not a source
        {5, true, kCivilian, Repo(5), 0x44, std::nullopt},  // unread: not a source
        {6, true, kCivilian, Repo(6), 0, true},             // unknown brick: not a source
        {7, true, kCivilian, Repo(7), 0x33, true},
    };
    CHECK(fr::TargetSourceBricks(pool, kMissionBrick) == std::vector<uint64_t> {0x33, kSceneBrick});
}

TEST_CASE("PlanTargetSwaps takes candidates only from the pool actors' bricks") {
    const auto pool = Pool(8);
    auto candidates = Candidates(6);
    candidates[1].brickRid = kMissionBrick; // a mission-brick actor
    candidates[2].brickRid = 0x1234;        // the role brick
    const auto plan = Plan(1, kScene, pool, candidates, 1.0, kMissionBrick);
    CHECK(plan.sourceBricks == std::vector<uint64_t> {kSceneBrick});
    CHECK(plan.verdicts[1] == fr::CandidateReject::OtherBrick);
    CHECK(plan.verdicts[2] == fr::CandidateReject::OtherBrick);
    CHECK(plan.eligible == 4);

    for (const auto& swap : plan.swaps)
        CHECK(candidates[swap.candidate].brickRid == kSceneBrick);

    // No start-enabled pool actor outside the mission brick: nothing is swapped.
    auto dedicated = pool;

    for (auto& entry : dedicated)
        entry.brickRid = kMissionBrick;

    const auto none = Plan(1, kScene, dedicated, candidates, 1.0, kMissionBrick);
    CHECK(none.sourceBricks.empty());
    CHECK(none.eligible == 0);
    CHECK(none.swaps.empty());
}

TEST_CASE("PlanTargetSwaps breaks sort ties by repository id") {
    // Two candidates with the same brick and entity id (one template, two
    // instances): input order must not decide the outcome.
    const auto pool = Pool(4);
    auto a = Candidate(3001);
    auto b = Candidate(3002);
    a.entityId = b.entityId = 42;
    a.repo = Repo(500);
    b.repo = Repo(400);
    const std::vector<fr::TargetCandidate> ab {a, b};
    const std::vector<fr::TargetCandidate> ba {b, a};

    for (const int32_t seed : {1, 2, 3, 4, 5, 6}) {
        const auto x = Applied(pool, ab, Plan(seed, kScene, pool, ab, 0.25, kMissionBrick));
        const auto y = Applied(pool, ba, Plan(seed, kScene, pool, ba, 0.25, kMissionBrick));
        CHECK(x == y);
    }
}

TEST_CASE("PlanTargetSwaps takes only the scene's known movers") {
    const auto pool = Pool(40);
    const auto candidates = Candidates(60);

    // Every third candidate is in the table, plus ids of actors not in this load.
    std::set<fr::Guid> movers {Repo(77'777), Repo(88'888)};

    for (size_t i = 0; i < candidates.size(); i += 3)
        movers.insert(*candidates[i].repo);

    const auto plan = fr::PlanTargetSwaps(3, kScene, pool, candidates, 0.25, kMissionBrick, &movers);
    CHECK(plan.moverTable);
    CHECK(plan.eligible == 20);
    CHECK(plan.rejected.at(fr::CandidateReject::NotAMover) == 40);
    REQUIRE(plan.swaps.size() == 10);

    for (size_t i = 0; i < candidates.size(); ++i)
        CHECK((plan.verdicts[i] == fr::CandidateReject::None) == movers.contains(*candidates[i].repo));

    for (const auto& swap : plan.swaps)
        CHECK(movers.contains(*candidates[swap.candidate].repo));

    // Other rules still come first: a known mover that is a crowd character stays a crowd rejection.
    auto crowd = candidates;
    crowd[0].crowd = true;
    CHECK(fr::PlanTargetSwaps(3, kScene, pool, crowd, 0.25, kMissionBrick, &movers).verdicts[0] ==
          fr::CandidateReject::Crowd);

    // An empty table: nothing is eligible.
    const std::set<fr::Guid> none;
    const auto empty = fr::PlanTargetSwaps(3, kScene, pool, candidates, 0.25, kMissionBrick, &none);
    CHECK(empty.moverTable);
    CHECK(empty.eligible == 0);
    CHECK(empty.swaps.empty());
}

TEST_CASE("PlanTargetSwaps swaps nothing in a scene without a mover table") {
    const auto pool = Pool(40);
    const auto candidates = Candidates(60);
    const auto plan = fr::PlanTargetSwaps(3, kScene, pool, candidates, 0.25, kMissionBrick, nullptr);
    CHECK_FALSE(plan.moverTable);
    CHECK(plan.wanted == 10);
    CHECK(plan.eligible == 0);
    CHECK(plan.swaps.empty());
    CHECK(plan.rejected.at(fr::CandidateReject::NotAMover) == 60);
    fr::TargetPoolPlan unchanged;
    unchanged.length = pool.size();
    CHECK(Applied(pool, candidates, plan) == Applied(pool, candidates, unchanged));
}

TEST_CASE("PlanTargetSwaps with a mover table is deterministic and independent of candidate order") {
    const auto pool = Pool(40);
    const auto candidates = Candidates(60);
    std::set<fr::Guid> movers;

    for (size_t i = 0; i < candidates.size(); i += 2)
        movers.insert(*candidates[i].repo);

    const auto run = [&](const std::vector<fr::TargetCandidate>& c, int32_t seed) {
        return Applied(pool, c, fr::PlanTargetSwaps(seed, kScene, pool, c, 0.25, kMissionBrick, &movers));
    };

    const auto a = run(candidates, 11);
    CHECK(a == run(candidates, 11));
    CHECK(a == run({candidates.rbegin(), candidates.rend()}, 11));
    CHECK(a != run(candidates, 12));

    // Ids in the table but absent from this load change nothing.
    movers.insert(Repo(123'456));
    CHECK(a == run(candidates, 11));
}

TEST_CASE("KnownMovers reads the mover table") {
    // Whittleton Creek: Amelia Garros walked 413 m.
    const auto skunk = fr::KnownMovers(kScene);
    REQUIRE(skunk.has_value());
    CHECK(skunk->contains(*fr::ParseGuid("cafb2bda-485c-4c11-9df0-c1e6971dbbbb")));

    // Scene case does not matter.
    const auto upper = fr::KnownMovers("assembly:/_PRO/Scenes/Missions/Skunk/scene_skunk_mild_solitaire.entity");
    REQUIRE(upper.has_value());
    CHECK(*upper == *skunk);

    // Mendoza: Jorge De Luca stood still (0.2 m) and was picked as a target; he is not in the table.
    const auto sapo = fr::KnownMovers("assembly:/_pro/scenes/missions/elegant/scene_elegant_mild_sapo.entity");
    REQUIRE(sapo.has_value());
    CHECK_FALSE(sapo->empty());
    CHECK_FALSE(sapo->contains(*fr::ParseGuid("f2148e20-ae7f-4232-8110-124791063b3c")));

    // A map nobody has recorded yet, and a showdown.
    CHECK_FALSE(fr::KnownMovers("assembly:/_pro/scenes/missions/miami/scene_flamingo_mild_bridge.entity"));
    CHECK_FALSE(fr::KnownMovers("assembly:/_pro/scenes/missions/ancestral/scene_ancestral_hot_bridge.entity"));
}

// --- target_pool_any_guard: guards without a table, civilian movers from it, two tiers ---

namespace {

// Candidates' repository ids of civilians only, as a scene's mover table.
std::set<fr::Guid> CivilianMoversOf(std::span<const fr::TargetCandidate> candidates) {
    std::set<fr::Guid> movers;

    for (const auto& c : candidates) {
        if (c.repo && c.kind == kCivilian)
            movers.insert(*c.repo);
    }

    return movers;
}

fr::TargetPoolPlan PlanAnyGuard(int32_t seed, std::span<const fr::PoolEntry> pool,
                                std::span<const fr::TargetCandidate> candidates, double share, double guardShare,
                                const std::set<fr::Guid>* movers) {
    return fr::PlanTargetSwaps(seed, kScene, pool, candidates, share, kMissionBrick, movers, true, guardShare);
}

} // namespace

TEST_CASE("any guard: eligibility by kind and mover table") {
    const auto pool = Pool(8);
    std::vector<fr::TargetCandidate> c {
        Candidate(1001, kGuard),    // 0: guard, not in the table
        Candidate(1002, kCivilian), // 1: stationary civilian (not in the table)
        Candidate(1003, kGuard),    // 2: crowd guard
        Candidate(1004, kCivilian), // 3: civilian mover
        Candidate(1005, kGuard),    // 4: start-disabled guard
        Candidate(1006, kGuard),    // 5: guard with a role
        Candidate(1007, kGuard),    // 6: guard of another brick
        Candidate(1008, kGuard),    // 7: guard in the table
    };
    c[2].crowd = true;
    c[4].startEnabled = false;
    c[5].role = "supplier";
    c[6].brickRid = 0x55;
    const std::set<fr::Guid> movers {*c[3].repo, *c[7].repo};

    const auto plan = PlanAnyGuard(5, pool, c, 0.5, 0.5, &movers);
    CHECK(plan.anyGuard);
    CHECK(plan.verdicts[0] == fr::CandidateReject::None);
    CHECK(plan.tiers[0] == fr::CandidateTier::Guard);
    CHECK(plan.verdicts[1] == fr::CandidateReject::NotAMover);
    CHECK(plan.tiers[1] == fr::CandidateTier::None);
    CHECK(plan.verdicts[2] == fr::CandidateReject::Crowd);
    CHECK(plan.verdicts[3] == fr::CandidateReject::None);
    CHECK(plan.tiers[3] == fr::CandidateTier::Mover);
    CHECK(plan.verdicts[4] == fr::CandidateReject::StartDisabled);
    CHECK(plan.verdicts[5] == fr::CandidateReject::RoleActor);
    CHECK(plan.verdicts[6] == fr::CandidateReject::OtherBrick);
    CHECK(plan.tiers[7] == fr::CandidateTier::Guard); // a guard is a guard, listed or not
    CHECK(plan.eligibleGuards == 2);
    CHECK(plan.eligibleMovers == 1);
    CHECK(plan.eligible == 3);

    // Setting off: the old rule, table only (the listed guard as a mover, the other guard out).
    const auto off = fr::PlanTargetSwaps(5, kScene, pool, c, 0.5, kMissionBrick, &movers, false, 0.5);
    CHECK_FALSE(off.anyGuard);
    CHECK(off.verdicts[0] == fr::CandidateReject::NotAMover);
    CHECK(off.verdicts[1] == fr::CandidateReject::NotAMover);
    CHECK(off.tiers[3] == fr::CandidateTier::Mover);
    CHECK(off.tiers[7] == fr::CandidateTier::Mover);
    CHECK(off.eligibleGuards == 0);
    CHECK(off.eligibleMovers == 2);
}

TEST_CASE("any guard off is the old single-tier plan") {
    const auto pool = Pool(40);
    const auto candidates = Candidates(60);
    std::set<fr::Guid> movers;

    for (size_t i = 0; i < candidates.size(); i += 2)
        movers.insert(*candidates[i].repo);

    for (const int32_t seed : {1, 2, 3, 4, 5}) {
        const auto old = fr::PlanTargetSwaps(seed, kScene, pool, candidates, 0.25, kMissionBrick, &movers);
        const auto off = fr::PlanTargetSwaps(seed, kScene, pool, candidates, 0.25, kMissionBrick, &movers, false, 1.0);
        CHECK(Applied(pool, candidates, old) == Applied(pool, candidates, off));
        CHECK(off.wanted == 10);
        CHECK(off.guardWanted == 0);
        CHECK(off.guardSwapped == 0);
        CHECK(off.guardRemaining == 0);
        CHECK(off.moverSwapped == off.swaps.size());

        for (const auto& swap : off.swaps)
            CHECK(swap.tier == fr::CandidateTier::Mover);
    }

    // No table and the setting off: nothing.
    const auto none = fr::PlanTargetSwaps(3, kScene, pool, candidates, 1.0, kMissionBrick, nullptr, false, 1.0);
    CHECK(none.eligible == 0);
    CHECK(none.swaps.empty());
}

TEST_CASE("any guard: a scene without a mover table gets guards only (tier 2 only)") {
    const auto pool = Pool(40);             // 10 guard slots, 30 civilian
    const auto candidates = Candidates(60); // 20 guards, 40 civilians

    const auto plan = PlanAnyGuard(3, pool, candidates, 1.0, 1.0, nullptr);
    CHECK_FALSE(plan.moverTable);
    CHECK(plan.eligibleGuards == 20);
    CHECK(plan.eligibleMovers == 0);
    CHECK(plan.rejected.at(fr::CandidateReject::NotAMover) == 40);
    CHECK(plan.moverWanted == 30);
    CHECK(plan.moverSwapped == 0);
    CHECK(plan.guardRemaining == 40);
    CHECK(plan.guardWanted == 40);
    // No civilian mover anywhere: guards may take civilian slots too, capped by the 20 guards.
    CHECK(plan.guardSwapped == 20);
    CHECK(plan.capped);

    size_t onGuardSlots = 0;

    for (const auto& swap : plan.swaps) {
        CHECK(swap.tier == fr::CandidateTier::Guard);
        CHECK(candidates[swap.candidate].kind == kGuard);
        onGuardSlots += pool[swap.slot].kind == kGuard ? 1 : 0;
    }

    // Guard slots first.
    CHECK(onGuardSlots == 10);
}

TEST_CASE("any guard: tier 1 civilian movers on civilian slots, tier 2 guards on what is left") {
    const auto pool = Pool(40);             // 10 guard slots, 30 civilian
    const auto candidates = Candidates(60); // 20 guards, 40 civilians
    const auto movers = CivilianMoversOf(candidates);

    for (const int32_t seed : {1, 2, 3, 4, 5, 6, 7, 8}) {
        for (const double share : {0.0, 0.3, 1.0}) {
            for (const double guardShare : {0.0, 0.5, 1.0}) {
                CAPTURE(seed);
                CAPTURE(share);
                CAPTURE(guardShare);
                const auto plan = PlanAnyGuard(seed, pool, candidates, share, guardShare, &movers);
                CHECK(plan.eligibleMovers == 40);
                CHECK(plan.eligibleGuards == 20);
                CHECK(plan.moverSlots == 30);
                CHECK(plan.moverWanted == fr::TargetSwapCount(30, share));
                CHECK(plan.moverSwapped == plan.moverWanted);
                CHECK(plan.guardRemaining == 40 - plan.moverSwapped);
                CHECK(plan.guardWanted == fr::TargetSwapCount(plan.guardRemaining, guardShare));
                // Civilian movers exist: guards keep to the 10 guard slots.
                CHECK(plan.guardSwapped == std::min<size_t>(plan.guardWanted, 10));
                CHECK(plan.swaps.size() == plan.moverSwapped + plan.guardSwapped);

                std::set<size_t> slots;
                std::set<size_t> actors;

                for (const auto& swap : plan.swaps) {
                    CHECK(slots.insert(swap.slot).second); // tier 2 only uses slots tier 1 left
                    CHECK(actors.insert(swap.candidate).second);
                    CHECK(swap.sameKind); // never a guard on a civilian slot here

                    if (swap.tier == fr::CandidateTier::Mover) {
                        CHECK(pool[swap.slot].kind == kCivilian);
                        CHECK(candidates[swap.candidate].kind == kCivilian);
                    }
                    else {
                        CHECK(swap.tier == fr::CandidateTier::Guard);
                        CHECK(pool[swap.slot].kind == kGuard);
                        CHECK(candidates[swap.candidate].kind == kGuard);
                    }
                }
            }
        }
    }

    // Few civilian movers: tier 1 is capped and the civilian slots it could not fill stay untouched.
    const std::set<fr::Guid> few {*candidates[1].repo, *candidates[2].repo};
    const auto plan = PlanAnyGuard(9, pool, candidates, 1.0, 1.0, &few);
    CHECK(plan.moverWanted == 30);
    CHECK(plan.moverSwapped == 2);
    CHECK(plan.guardRemaining == 38);
    CHECK(plan.guardSwapped == 10);
    CHECK(plan.capped);

    for (const auto& swap : plan.swaps)
        CHECK((pool[swap.slot].kind == kGuard) == (swap.tier == fr::CandidateTier::Guard));
}

TEST_CASE("any guard: deterministic and independent of candidate order") {
    const auto pool = Pool(40);
    const auto candidates = Candidates(60);
    std::set<fr::Guid> movers;

    for (size_t i = 0; i < candidates.size(); i += 2) {
        if (candidates[i].kind == kCivilian)
            movers.insert(*candidates[i].repo);
    }

    const auto run = [&](const std::vector<fr::TargetCandidate>& c, int32_t seed) {
        return Applied(pool, c, PlanAnyGuard(seed, pool, c, 0.4, 0.6, &movers));
    };

    const auto a = run(candidates, 11);
    CHECK(a == run(candidates, 11));
    CHECK(a == run({candidates.rbegin(), candidates.rend()}, 11));
    CHECK(a != run(candidates, 12));
}

TEST_CASE("DrawSwapShare: the guard share is a second, independent draw") {
    CHECK(fr::DrawSwapShare(1234, kScene, fr::kTargetPoolGuardShareFeature) ==
          fr::DrawSwapShare(1234, kScene, fr::kTargetPoolGuardShareFeature));
    CHECK(fr::DrawSwapShare(1234, kScene) == fr::DrawSwapShare(1234, kScene, fr::kTargetPoolShareFeature));

    constexpr int kSeeds = 4000;
    double sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;
    int same = 0;

    for (int32_t seed = 1; seed <= kSeeds; ++seed) {
        const double a = fr::DrawSwapShare(seed, kScene);
        const double b = fr::DrawSwapShare(seed, kScene, fr::kTargetPoolGuardShareFeature);
        CHECK(b >= 0.0);
        CHECK(b <= 1.0);
        same += a == b ? 1 : 0;
        sumA += a;
        sumB += b;
        sumAA += a * a;
        sumBB += b * b;
        sumAB += a * b;
    }

    const double n = kSeeds;
    const double cov = sumAB / n - (sumA / n) * (sumB / n);
    const double varA = sumAA / n - (sumA / n) * (sumA / n);
    const double varB = sumBB / n - (sumB / n) * (sumB / n);
    const double r = cov / std::sqrt(varA * varB);
    CHECK(same == 0);
    CHECK(std::abs(r) < 0.06);
    CHECK(std::abs(sumB / n - 0.5) < 0.03);
}

TEST_CASE("Mover table tiers: good movers and plain movers") {
    constexpr const char* kSapo = "assembly:/_pro/scenes/missions/elegant/scene_elegant_mild_sapo.entity";
    const auto tiers = fr::SceneMovers(kSapo);
    REQUIRE(tiers.has_value());
    const auto good = fr::KnownMovers(kSapo);
    const auto any = fr::KnownMovers(kSapo, fr::MoverTier::Mover);
    REQUIRE(good.has_value());
    REQUIRE(any.has_value());
    CHECK(any->size() == tiers->size());
    CHECK(any->size() > good->size());

    for (const auto& repo : *good)
        CHECK(any->contains(repo));

    // Brooklyn Bush paced 12.4 m across: a census mover, not a good one.
    const auto brooklyn = *fr::ParseGuid("ba5181d6-27ce-406a-9fed-57cb9f1c3701");
    CHECK(tiers->at(brooklyn) == fr::MoverTier::Mover);
    CHECK(any->contains(brooklyn));
    CHECK_FALSE(good->contains(brooklyn));

    CHECK(std::string(fr::MoverTierName(fr::MoverTier::Good)) == "good mover");
    CHECK_FALSE(fr::SceneMovers("assembly:/_pro/scenes/missions/miami/scene_flamingo_mild_bridge.entity"));
}

TEST_CASE("excluded actors: never a candidate, and replaced when already in the pool") {
    const auto pool = Pool(40);             // 10 guard slots, 30 civilian
    const auto candidates = Candidates(60); // 20 guards, 40 civilians
    const auto movers = CivilianMoversOf(candidates);

    // Null or empty: exactly the plan without exclusions.
    const std::set<fr::Guid> none;

    for (const int32_t seed : {1, 2, 3}) {
        const auto plain = PlanAnyGuard(seed, pool, candidates, 0.3, 0.4, &movers);
        const auto empty =
            fr::PlanTargetSwaps(seed, kScene, pool, candidates, 0.3, kMissionBrick, &movers, true, 0.4, &none);
        CHECK(Applied(pool, candidates, plain) == Applied(pool, candidates, empty));
        CHECK(empty.excludedInPool == 0);
        CHECK(empty.excludedForced == 0);
    }

    // Pool entries 0 (a guard) and 1 (a civilian), and two candidates (a guard and a civilian).
    const std::set<fr::Guid> excluded {*pool[0].repo, *pool[1].repo, *candidates[0].repo, *candidates[1].repo};

    for (const int32_t seed : {1, 2, 3, 4, 5, 6, 7, 8}) {
        for (const double share : {0.0, 0.5}) {
            CAPTURE(seed);
            CAPTURE(share);
            const auto plan = fr::PlanTargetSwaps(seed, kScene, pool, candidates, share, kMissionBrick, &movers, true,
                                                  share, &excluded);
            CHECK(plan.verdicts[0] == fr::CandidateReject::Excluded);
            CHECK(plan.verdicts[1] == fr::CandidateReject::Excluded);
            CHECK(plan.rejected.at(fr::CandidateReject::Excluded) == 2);
            CHECK(plan.excludedInPool == 2);
            CHECK(plan.excludedKept == 0);

            const auto after = Applied(pool, candidates, plan);
            CHECK(after.size() == pool.size());
            CHECK(after[0] != pool[0].handle);
            CHECK(after[1] != pool[1].handle);
            CHECK(std::find(after.begin(), after.end(), candidates[0].handle) == after.end());
            CHECK(std::find(after.begin(), after.end(), candidates[1].handle) == after.end());

            const std::set<uint64_t> distinct(after.begin(), after.end());
            CHECK(distinct.size() == after.size()); // no actor twice

            std::set<size_t> slots;

            for (const auto& swap : plan.swaps)
                CHECK(slots.insert(swap.slot).second);

            if (share == 0.0) {
                CHECK(plan.wanted == 0);
                CHECK(plan.excludedForced == 2);
                CHECK(plan.swaps.size() == 2);
            }

            // Like for like first: the guard slot gets a guard, the civilian slot a civilian.
            for (const auto& swap : plan.swaps) {
                if (swap.slot <= 1)
                    CHECK(swap.sameKind);
            }
        }
    }

    // No eligible actor left: the second excluded entry stays, counted as kept.
    const std::vector<fr::TargetCandidate> one {Candidate(1001, kGuard)};
    const std::set<fr::Guid> both {*pool[0].repo, *pool[4].repo}; // two guard slots
    const auto capped = fr::PlanTargetSwaps(3, kScene, pool, one, 0.0, kMissionBrick, nullptr, true, 0.0, &both);
    CHECK(capped.excludedInPool == 2);
    CHECK(capped.excludedForced == 1);
    CHECK(capped.excludedKept == 1);

    // An unreadable (unreplaceable) excluded entry is kept.
    auto fixed = Pool(8);
    fixed[0].replaceable = false;
    const std::set<fr::Guid> first {*fixed[0].repo};
    const auto kept = fr::PlanTargetSwaps(3, kScene, fixed, candidates, 0.0, kMissionBrick, &movers, true, 0.0, &first);
    CHECK(kept.excludedInPool == 1);
    CHECK(kept.excludedKept == 1);
    CHECK(kept.swaps.empty());
}

TEST_CASE("Mover table: the full Landslide's rows, keyed by _scene_mamba as the swap loads it") {
    // The scene as the swap sets it (m_SceneResource, fr::kLandslideScene) and as the alias names it.
    const fr::SceneAlias alias {std::string(fr::kLandslideScene),
                                "assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_mild_bingo.entity",
                                "assembly:/_pro/scenes/missions/coastaltown/fr_mission_mild_landslide.brick",
                                "the full Landslide",
                                "",
                                false};
    REQUIRE(fr::IsAliasedScene(&alias, fr::kLandslideScene));

    const auto tiers = fr::SceneMovers(alias.actual);
    REQUIRE(tiers.has_value());
    const auto good = fr::KnownMovers(alias.actual);
    const auto any = fr::KnownMovers(alias.actual, fr::MoverTier::Mover);
    REQUIRE(good.has_value());
    REQUIRE(any.has_value());
    CHECK(good->size() >= 20);
    CHECK(any->size() > good->size());

    // Case does not matter.
    const auto upper = fr::SceneMovers("ASSEMBLY:/_PRO/Scenes/Missions/CoastalTown/_Scene_Mamba.entity");
    REQUIRE(upper.has_value());
    CHECK(*upper == *tiers);

    // Mild Sapienza's own rows (mission01's day crowd) are another table: none of Landslide's crowd is there.
    const auto bingo = fr::SceneMovers(fr::ResolveScene(&alias, alias.actual));
    REQUIRE(bingo.has_value());

    for (const auto& [repo, tier] : *tiers)
        CHECK_FALSE(bingo->contains(repo));

    // Its never-targets (Marco Abiatti) are never rows.
    for (const fr::LandslideNeverTarget& target : fr::LandslideFull().neverTargets) {
        CAPTURE(target.name);
        CHECK_FALSE(tiers->contains(*fr::ParseGuid(target.repo)));
    }
}

TEST_CASE("Mover table: the full Author uses mild Sapienza's rows until it has its own") {
    constexpr const char* kBingo = "assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_mild_bingo.entity";
    fr::SceneAlias alias {std::string(fr::kAuthorScene),
                          kBingo,
                          "assembly:/_pro/scenes/missions/coastaltown/fr_mission_mild_author.brick",
                          "the full Author",
                          "",
                          false};
    alias.sharesCrowd = fr::test::Full("author").sharesCrowd;
    REQUIRE(alias.sharesCrowd);

    // No census of the full Author yet: the table scene is mild Sapienza's, whose rows exist.
    const bool own = fr::SceneMovers(alias.actual).has_value();
    const std::string table = fr::MoverTableScene(&alias, alias.actual, own);
    CHECK(table == (own ? alias.actual : std::string(kBingo)));
    const auto rows = fr::SceneMovers(table);
    REQUIRE(rows.has_value());

    // Its never-target (Craig Black) is never a row of the table it uses.
    for (const fr::LandslideNeverTarget& target : fr::test::Full("author").neverTargets) {
        CAPTURE(target.name);
        CHECK_FALSE(rows->contains(*fr::ParseGuid(target.repo)));
    }
}

TEST_CASE("Mover table: the full Icon uses only its own scene's rows (the census key)") {
    constexpr const char* kBingo = "assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_mild_bingo.entity";
    fr::SceneAlias alias {std::string(fr::kIconScene),
                          kBingo,
                          "assembly:/_pro/scenes/missions/coastaltown/fr_mission_mild_icon.brick",
                          "the full Icon",
                          "",
                          false};
    alias.sharesCrowd = fr::test::Full("icon").sharesCrowd;
    REQUIRE_FALSE(alias.sharesCrowd);

    // Its crowd is the movie set's, not mild Sapienza's: the table is looked up by its own scene, with or without
    // rows recorded for it (none until a census; then the target pool lists no civilian mover).
    CHECK(fr::MoverTableScene(&alias, alias.actual, false) == alias.actual);
    CHECK(fr::MoverTableScene(&alias, alias.actual, true) == alias.actual);

    // Its never-targets (Dino Bosco, Sophia Wilde) are never rows of its scene.
    if (const auto rows = fr::SceneMovers(alias.actual)) {
        for (const fr::LandslideNeverTarget& target : fr::test::Full("icon").neverTargets) {
            CAPTURE(target.name);
            CHECK_FALSE(rows->contains(*fr::ParseGuid(target.repo)));
        }
    }
}

TEST_CASE("Mover table: the full Bad Boy's rows are its own, apart from the full Icon's") {
    // Both load _scene_copperhead (no shared crowd: its own scene is the table's): the Bad Boy's census rows are
    // tagged "badboy" and chosen only under it; under the full Icon no variant is loaded, so the scene's untagged
    // rows (the Icon's own, none recorded yet) apply.
    const uint64_t icon[] = {fr::kTodSummerNightClearBrick.rid, fr::kLightingCopperheadBrick.rid};
    const auto badboyTags = fr::MoverTableVariants(icon, &fr::test::Full("badboy"));
    const fr::MoverTableChoice badboy = fr::ChooseMoverTable(fr::kIconScene, badboyTags);
    CHECK(badboy.variant == "badboy");
    const auto iconTags = fr::MoverTableVariants(icon, &fr::test::Full("icon"));
    const fr::MoverTableChoice iconChoice = fr::ChooseMoverTable(fr::kIconScene, iconTags);
    CHECK(iconChoice.variant.empty());
    CHECK_FALSE(iconChoice.fallback);

    const auto rows = fr::SceneMovers(fr::kIconScene, "badboy");
    REQUIRE(rows);
    CHECK_FALSE(rows->empty());
    // Never targets under the full Bad Boy (The Icon's two and Argus) are never its rows.
    for (const fr::LandslideNeverTarget& target : fr::test::Full("badboy").neverTargets) {
        CAPTURE(target.name);
        CHECK_FALSE(rows->contains(*fr::ParseGuid(target.repo)));
    }
    // The Icon's own rows are a table apart: the two share the Icon's crowd, so an
    // NPC may move in both, but neither census decides the other's rows.
    const auto icons = fr::SceneMovers(fr::kIconScene);
    REQUIRE(icons);
    CHECK_FALSE(icons->empty());
    CHECK(*icons != *rows);
}

TEST_CASE("Mover table: a map variant's own rows, only while it is loaded") {
    constexpr const char* kMeyer = "assembly:/_pro/scenes/missions/edgy/mission_fox/scene_edgy_mild_meyer.entity";
    constexpr const char* kDartmoor = "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity";
    const auto normal = fr::SceneMovers(kMeyer);
    const auto drop = fr::SceneMovers(kMeyer, "the_drop");
    REQUIRE(normal.has_value());
    REQUIRE(drop.has_value());
    // The normal map's rows stay its own (19 good movers, 23 other movers).
    CHECK(normal->size() == 42);
    CHECK(*drop != *normal);
    CHECK(fr::KnownMovers(kMeyer, fr::MoverTier::Mover, "the_drop")->size() == drop->size());
    CHECK(fr::KnownMovers(kMeyer, fr::MoverTier::Good, "the_drop")->size() < drop->size());

    // Scene case does not matter; a variant with no rows of its own has no table. The Liability has its own rows.
    // Every Dartmoor variant has rows, so an unrecorded one is made up.
    CHECK(*fr::SceneMovers("ASSEMBLY:/_PRO/Scenes/Missions/Edgy/Mission_Fox/Scene_Edgy_Mild_Meyer.entity",
                           "the_drop") == *drop);
    const auto liability = fr::SceneMovers(kMeyer, "liability");
    REQUIRE(liability.has_value());
    CHECK(*liability != *drop);
    CHECK(*liability != *normal);
    CHECK_FALSE(fr::SceneMovers(kDartmoor, "unrecorded"));
    CHECK_FALSE(fr::SceneMovers("assembly:/_pro/scenes/missions/skunk/scene_skunk_mild_solitaire.entity", "the_drop"));

    // The normal map: its own rows, no fallback.
    const auto none = fr::ChooseMoverTable(kMeyer, {});
    CHECK(none.variant.empty());
    CHECK(none.loaded.empty());
    CHECK_FALSE(none.fallback);
    CHECK_FALSE(none.lightingOnly);

    // The Drop loaded: its rows.
    const std::string_view theDrop[] = {"the_drop"};
    const auto chosen = fr::ChooseMoverTable(kMeyer, theDrop);
    CHECK(chosen.variant == "the_drop");
    CHECK(chosen.loaded == "the_drop");
    CHECK_FALSE(chosen.fallback);
    CHECK_FALSE(chosen.lightingOnly);

    // A variant nobody has recorded: the normal map's rows, as a fallback.
    const std::string_view unrecorded[] = {"unrecorded"};
    const auto fallback = fr::ChooseMoverTable(kDartmoor, unrecorded);
    CHECK(fallback.variant.empty());
    CHECK(fallback.loaded == "unrecorded");
    CHECK(fallback.fallback);
    CHECK_FALSE(fallback.lightingOnly);

    // Several loaded: the first one with rows of its own.
    const std::string_view both[] = {"sloth", "the_drop"};
    const auto second = fr::ChooseMoverTable(kMeyer, both);
    CHECK(second.variant == "the_drop");
    CHECK(second.loaded == "sloth");
    CHECK_FALSE(second.fallback);

    // The Drop's rows are Berlin's only: another scene with the name loaded falls back.
    const auto elsewhere = fr::ChooseMoverTable(kScene, theDrop);
    CHECK(elsewhere.variant.empty());
    CHECK(elsewhere.fallback);
}

TEST_CASE("Mover table: a lighting-only variant uses the normal map's rows") {
    constexpr const char* kBingo = "assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_mild_bingo.entity";
    REQUIRE(fr::SceneMovers(kBingo).has_value());

    // Night and The Icon's lighting: the same crowd as the normal map, so its rows, and no fallback.
    for (const auto& [scene, name] :
         {std::pair {kBingo, "tod_ebola_night"}, std::pair {kBingo, "lighting_copperhead"}}) {
        CAPTURE(name);
        const std::string_view loaded[] = {name};
        const auto choice = fr::ChooseMoverTable(scene, loaded);
        CHECK(choice.variant.empty());
        CHECK(choice.loaded == name);
        CHECK_FALSE(choice.fallback);
        CHECK(choice.lightingOnly);
        // Never a table of its own.
        CHECK_FALSE(fr::SceneMovers(scene, name));
    }

    // A heavy variant without rows still falls back (and is not lighting only): the simple Landslide.
    const std::string_view landslide[] = {"landslide"};
    const auto heavy = fr::ChooseMoverTable(kBingo, landslide);
    CHECK(heavy.variant.empty());
    CHECK(heavy.fallback);
    CHECK_FALSE(heavy.lightingOnly);

    // A lighting-only variant next to a heavy one: the heavy one decides.
    const std::string_view mixed[] = {"tod_ebola_night", "landslide"};
    const auto both = fr::ChooseMoverTable(kBingo, mixed);
    CHECK(both.loaded == "landslide");
    CHECK(both.fallback);
    CHECK_FALSE(both.lightingOnly);
}

namespace {

// Pool handles 1..n (every fourth a guard); the civilians at odd indexes are
// listed movers, the ones at even indexes are not (stationary).
std::set<fr::Guid> PoolMoversOf(std::span<const fr::PoolEntry> pool) {
    std::set<fr::Guid> movers;

    for (size_t i = 0; i < pool.size(); ++i) {
        if (pool[i].kind == kCivilian && i % 2 == 1)
            movers.insert(*pool[i].repo);
    }

    return movers;
}

std::set<fr::Guid> Union(std::set<fr::Guid> a, const std::set<fr::Guid>& b) {
    a.insert(b.begin(), b.end());
    return a;
}

fr::StationaryRule Fill(bool trim = false, size_t minLength = fr::kMinTargetPoolLength) {
    fr::StationaryRule rule;
    rule.fill = true;
    rule.trim = trim;
    rule.minLength = minLength;
    return rule;
}

fr::TargetPoolPlan PlanStationary(int32_t seed, std::span<const fr::PoolEntry> pool,
                                  std::span<const fr::TargetCandidate> candidates, double share, double guardShare,
                                  const std::set<fr::Guid>* movers, fr::StationaryRule rule,
                                  const std::set<fr::Guid>* excluded = nullptr) {
    return fr::PlanTargetSwaps(seed, kScene, pool, candidates, share, kMissionBrick, movers, true, guardShare, excluded,
                               rule);
}

// The pool handles that are stationary under the rule.
std::set<uint64_t> StationaryHandles(std::span<const fr::PoolEntry> pool, const std::set<fr::Guid>* movers) {
    std::set<uint64_t> out;

    for (const auto& entry : pool) {
        if (fr::IsStationaryEntry(entry, kMissionBrick, movers, true))
            out.insert(entry.handle);
    }

    return out;
}

} // namespace

TEST_CASE("IsStationaryEntry: unlisted start-enabled scene civilians, only with a mover table") {
    const std::set<fr::Guid> movers {Repo(2)};
    const fr::PoolEntry civilian {1, true, kCivilian, Repo(1), kSceneBrick, true};
    CHECK(fr::IsStationaryEntry(civilian, kMissionBrick, &movers, true));
    CHECK_FALSE(fr::IsStationaryEntry(civilian, kMissionBrick, nullptr, true)); // no table: unknown

    auto listed = civilian;
    listed.repo = Repo(2);
    CHECK_FALSE(fr::IsStationaryEntry(listed, kMissionBrick, &movers, true));

    auto guard = civilian;
    guard.kind = kGuard;
    CHECK_FALSE(fr::IsStationaryEntry(guard, kMissionBrick, &movers, true));
    CHECK(fr::IsStationaryEntry(guard, kMissionBrick, &movers, false)); // the old rule: guards need the table too

    auto dedicated = civilian; // the mission brick's own target, start disabled
    dedicated.brickRid = kMissionBrick;
    dedicated.startEnabled = false;
    CHECK_FALSE(fr::IsStationaryEntry(dedicated, kMissionBrick, &movers, true));
    dedicated.startEnabled = true;
    CHECK_FALSE(fr::IsStationaryEntry(dedicated, kMissionBrick, &movers, true));

    auto unknownBrick = civilian;
    unknownBrick.brickRid = 0;
    CHECK_FALSE(fr::IsStationaryEntry(unknownBrick, kMissionBrick, &movers, true));

    auto unread = civilian;
    unread.startEnabled = std::nullopt;
    CHECK_FALSE(fr::IsStationaryEntry(unread, kMissionBrick, &movers, true));

    auto unreplaceable = civilian;
    unreplaceable.replaceable = false;
    CHECK_FALSE(fr::IsStationaryEntry(unreplaceable, kMissionBrick, &movers, true));

    auto noRepo = civilian;
    noRepo.repo = std::nullopt;
    CHECK(fr::IsStationaryEntry(noRepo, kMissionBrick, &movers, true));
}

TEST_CASE("stationary rule off: exactly the plan without it") {
    const auto pool = Pool(40);
    const auto candidates = Candidates(60);
    const auto movers = Union(CivilianMoversOf(candidates), PoolMoversOf(pool));

    for (const int32_t seed : {1, 2, 3}) {
        const auto plain = PlanAnyGuard(seed, pool, candidates, 0.3, 0.4, &movers);
        const auto off = PlanStationary(seed, pool, candidates, 0.3, 0.4, &movers, {});
        CHECK(Applied(pool, candidates, plain) == Applied(pool, candidates, off));
        CHECK(off.length == pool.size());
        CHECK(off.drops.empty());
        CHECK(off.moves.empty());
        CHECK(off.stationaryFilled == 0);
        CHECK(off.stationaryInPool == 10); // the 30 civilians, the 10 at even indexes unlisted
        CHECK(off.stationaryInPool == off.stationaryByShares + off.stationaryKept);
    }
}

TEST_CASE("stationary fill: every stationary entry is replaced, whatever the shares") {
    const auto pool = Pool(40); // 10 guards, 30 civilians: 20 listed movers, 10 stationary
    const auto candidates = Candidates(60);
    const auto movers = Union(CivilianMoversOf(candidates), PoolMoversOf(pool));
    const auto stationary = StationaryHandles(pool, &movers);
    REQUIRE(stationary.size() == 10);

    for (const int32_t seed : {1, 2, 3, 4, 5}) {
        for (const double share : {0.0, 0.2, 1.0}) {
            for (const double guardShare : {0.0, 1.0}) {
                CAPTURE(seed);
                CAPTURE(share);
                CAPTURE(guardShare);
                const auto plan = PlanStationary(seed, pool, candidates, share, guardShare, &movers, Fill());
                CHECK(plan.stationaryInPool == 10);
                CHECK(plan.stationaryKept == 0);
                CHECK(plan.stationaryByShares + plan.stationaryFilled == 10);
                CHECK(plan.drops.empty());
                CHECK(plan.length == pool.size());

                const auto after = Applied(pool, candidates, plan);
                CHECK(after.size() == pool.size());
                CHECK(std::set<uint64_t>(after.begin(), after.end()).size() == after.size()); // no actor twice

                for (const uint64_t handle : after)
                    CHECK_FALSE(stationary.contains(handle));

                // Tier 1 takes stationary slots first: with up to 10 wanted, only those.
                for (const auto& swap : plan.swaps) {
                    if (swap.tier == fr::CandidateTier::Mover && !swap.fill && plan.moverWanted <= 10)
                        CHECK(stationary.contains(pool[swap.slot].handle));

                    if (swap.fill)
                        CHECK(stationary.contains(pool[swap.slot].handle));
                }

                if (share == 0.0)
                    CHECK(plan.stationaryFilled == 10);
            }
        }
    }
}

TEST_CASE("stationary trim: no eligible actor left, the stationary entries are dropped") {
    const auto pool = Pool(40); // 10 stationary civilians
    const std::vector<fr::TargetCandidate> candidates {Candidate(1001, kCivilian), Candidate(1002, kGuard),
                                                       Candidate(1003, kCivilian)};
    const auto movers = Union(CivilianMoversOf(candidates), PoolMoversOf(pool));
    const auto stationary = StationaryHandles(pool, &movers);

    for (const int32_t seed : {1, 2, 3, 4, 5, 6}) {
        CAPTURE(seed);
        const auto plan = PlanStationary(seed, pool, candidates, 0.5, 0.5, &movers, Fill(true));
        CHECK(plan.stationaryInPool == 10);
        CHECK(plan.stationaryByShares + plan.stationaryFilled == 3); // every eligible actor went to a stationary slot
        CHECK(plan.stationaryDropped == 7);
        CHECK(plan.stationaryKept == 0);
        CHECK(plan.drops.size() == 7);
        CHECK(plan.length == 33);
        CHECK(std::is_sorted(plan.drops.begin(), plan.drops.end()));

        const auto after = Applied(pool, candidates, plan);
        REQUIRE(after.size() == 33);
        CHECK(std::set<uint64_t>(after.begin(), after.end()).size() == 33);

        for (const uint64_t handle : after)
            CHECK_FALSE(stationary.contains(handle));

        // Every entry that is not stationary is still in the pool (some moved).
        for (const auto& entry : pool) {
            if (!stationary.contains(entry.handle))
                CHECK(std::find(after.begin(), after.end(), entry.handle) != after.end());
        }

        // Moves come from the tail into dropped slots before the new end.
        for (const auto& move : plan.moves) {
            CHECK(move.from >= plan.length);
            CHECK(move.to < plan.length);
            CHECK(std::binary_search(plan.drops.begin(), plan.drops.end(), move.to));
        }

        // Entries before the new end that are not dropped stay in their slots.
        for (size_t i = 0; i < plan.length; ++i) {
            const bool moved =
                std::any_of(plan.moves.begin(), plan.moves.end(), [&](const fr::PoolMove& m) { return m.to == i; });
            const bool swapped =
                std::any_of(plan.swaps.begin(), plan.swaps.end(), [&](const fr::TargetSwap& s) { return s.slot == i; });

            if (!moved && !swapped)
                CHECK(after[i] == pool[i].handle);
        }
    }

    // Deterministic and independent of candidate order.
    const std::vector<fr::TargetCandidate> reversed(candidates.rbegin(), candidates.rend());
    const auto a = PlanStationary(9, pool, candidates, 0.5, 0.5, &movers, Fill(true));
    const auto b = PlanStationary(9, pool, reversed, 0.5, 0.5, &movers, Fill(true));
    CHECK(Applied(pool, candidates, a) == Applied(pool, reversed, b));
    CHECK(a.drops == b.drops);
    CHECK(a.moves == b.moves);
}

TEST_CASE("stationary trim: never below the minimum length") {
    auto pool = Pool(16); // 4 guards, 12 civilians, none listed: 12 stationary
    const std::set<fr::Guid> movers {Repo(9999)};
    const std::vector<fr::TargetCandidate> none;
    REQUIRE(StationaryHandles(pool, &movers).size() == 12);

    const auto plan = PlanStationary(3, pool, none, 1.0, 1.0, &movers, Fill(true, 12));
    CHECK(plan.drops.size() == 4);
    CHECK(plan.length == 12);
    CHECK(plan.stationaryDropped == 4);
    CHECK(plan.stationaryKept == 8);
    CHECK(Applied(pool, none, plan).size() == 12);

    // A pool at the minimum already: nothing dropped.
    const auto small = PlanStationary(3, std::span(pool).first(12), none, 1.0, 1.0, &movers, Fill(true, 12));
    CHECK(small.drops.empty());
    CHECK(small.length == 12);
    CHECK(small.stationaryKept == 9);

    // Without the trim the stationary entries stay.
    const auto noTrim = PlanStationary(3, pool, none, 1.0, 1.0, &movers, Fill(false));
    CHECK(noTrim.drops.empty());
    CHECK(noTrim.length == 16);
    CHECK(noTrim.stationaryKept == 12);
}

TEST_CASE("stationary trim: excluded entries left are dropped first") {
    auto pool = Pool(20); // 15 civilians, none listed, so all stationary
    const std::set<fr::Guid> movers {Repo(9999)};
    const std::set<fr::Guid> excluded {*pool[0].repo, *pool[1].repo}; // a guard and a civilian
    const std::vector<fr::TargetCandidate> none;

    const auto plan = PlanStationary(4, pool, none, 0.0, 0.0, &movers, Fill(true, 15), &excluded);
    CHECK(plan.drops.size() == 5);
    CHECK(std::binary_search(plan.drops.begin(), plan.drops.end(), size_t {0}));
    CHECK(std::binary_search(plan.drops.begin(), plan.drops.end(), size_t {1}));
    CHECK(plan.excludedInPool == 2);
    CHECK(plan.excludedKept == 0);

    const auto after = Applied(pool, none, plan);
    CHECK(std::find(after.begin(), after.end(), pool[0].handle) == after.end());
    CHECK(std::find(after.begin(), after.end(), pool[1].handle) == after.end());
}

TEST_CASE("stationary rule: the mission brick's dedicated targets and a table-less scene are left alone") {
    auto pool = Pool(30);

    for (size_t i = 0; i < 6; ++i) {
        pool[i].brickRid = kMissionBrick;
        pool[i].startEnabled = false;
    }

    const std::vector<fr::TargetCandidate> none;
    const std::set<fr::Guid> movers {Repo(9999)};
    const auto plan = PlanStationary(5, pool, none, 1.0, 1.0, &movers, Fill(true, 1));
    const auto after = Applied(pool, none, plan);

    for (size_t i = 0; i < 6; ++i)
        CHECK(std::find(after.begin(), after.end(), pool[i].handle) != after.end());

    // No mover table: civilians are unknown, nothing is filled or dropped.
    const auto candidates = Candidates(30);
    const auto noTable = PlanStationary(5, pool, candidates, 0.0, 0.0, nullptr, Fill(true));
    CHECK(noTable.stationaryInPool == 0);
    CHECK(noTable.swaps.empty());
    CHECK(noTable.drops.empty());
    CHECK(noTable.length == pool.size());
}

TEST_CASE("stationary rule: a pool like the full Bad Boy's ends with no stationary entry") {
    // 52 entries: 3 dedicated targets, 13 guards, 36 civilians of which 11 listed movers.
    std::vector<fr::PoolEntry> pool;

    for (uint64_t i = 0; i < 3; ++i)
        pool.push_back({1 + i, true, kCivilian, Repo(1 + i), kMissionBrick, false});

    for (uint64_t i = 3; i < 16; ++i)
        pool.push_back({1 + i, true, kGuard, Repo(1 + i), kSceneBrick, true});

    for (uint64_t i = 16; i < 52; ++i)
        pool.push_back({1 + i, true, kCivilian, Repo(1 + i), kSceneBrick, true});

    std::vector<fr::TargetCandidate> candidates;

    for (uint64_t i = 0; i < 7; ++i)
        candidates.push_back(Candidate(1001 + i, kCivilian));

    for (uint64_t i = 7; i < 15; ++i)
        candidates.push_back(Candidate(1001 + i, kGuard));

    std::set<fr::Guid> movers = CivilianMoversOf(candidates);

    for (uint64_t i = 16; i < 27; ++i)
        movers.insert(Repo(1 + i));

    const auto stationary = StationaryHandles(pool, &movers);
    REQUIRE(stationary.size() == 25);

    const auto plan = PlanStationary(-291838821, pool, candidates, 0.489, 0.414, &movers, Fill(true));
    CHECK(plan.stationaryInPool == 25);
    CHECK(plan.stationaryByShares + plan.stationaryFilled == 15); // all 15 eligible actors
    CHECK(plan.stationaryDropped == 10);
    CHECK(plan.stationaryKept == 0);
    CHECK(plan.length == 42);
    CHECK(plan.guardSwapped == 0); // the guards went to stationary slots, not other guards' places

    const auto after = Applied(pool, candidates, plan);
    REQUIRE(after.size() == 42);
    CHECK(std::set<uint64_t>(after.begin(), after.end()).size() == 42);

    for (const uint64_t handle : after)
        CHECK_FALSE(stationary.contains(handle));

    for (uint64_t handle = 1; handle <= 3; ++handle) // the dedicated targets stay
        CHECK(std::find(after.begin(), after.end(), handle) != after.end());
}

TEST_CASE("excluded trim: excluded entries go below the minimum length, stationary ones do not") {
    auto pool = Pool(20); // guards at 0, 4, 8, 12, 16; 15 civilians, none listed: all stationary
    const std::set<fr::Guid> movers {Repo(9999)};
    const std::vector<fr::TargetCandidate> none;
    std::set<fr::Guid> excluded; // 10 of the stationary civilians

    for (const size_t i : {1, 2, 3, 5, 6, 7, 9, 10, 11, 13})
        excluded.insert(*pool[i].repo);

    for (const int32_t seed : {1, 2, 3, 4}) {
        CAPTURE(seed);
        // Room for 8 above the minimum of 12: all 10 excluded entries go anyway, no other stationary one.
        const auto plan = PlanStationary(seed, pool, none, 0.0, 0.0, &movers, Fill(true, 12), &excluded);
        CHECK(plan.drops.size() == 10);
        CHECK(plan.length == 10);
        CHECK(plan.excludedInPool == 10);
        CHECK(plan.excludedKept == 0);
        CHECK(plan.stationaryInPool == 15);
        CHECK(plan.stationaryDropped == 10);
        CHECK(plan.stationaryKept == 5);

        const auto after = Applied(pool, none, plan);
        REQUIRE(after.size() == 10);

        for (const auto& entry : pool)
            CHECK((std::find(after.begin(), after.end(), entry.handle) == after.end()) ==
                  excluded.contains(*entry.repo));
    }

    // With the excluded minimum at the minimum length: the old rule, two excluded entries stay.
    auto strict = Fill(true, 12);
    strict.excludedMinLength = 12;
    const auto old = PlanStationary(3, pool, none, 0.0, 0.0, &movers, strict, &excluded);
    CHECK(old.drops.size() == 8);
    CHECK(old.length == 12);
    CHECK(old.excludedKept == 2);

    // Never below the excluded minimum: of 17 excluded entries, 15 go and a seeded 2 stay.
    std::set<fr::Guid> most;

    for (size_t i = 3; i < pool.size(); ++i)
        most.insert(*pool[i].repo);

    const auto floor = PlanStationary(3, pool, none, 0.0, 0.0, &movers, Fill(true, 12), &most);
    CHECK(floor.length == fr::kMinTargetPoolLengthExcluded);
    CHECK(floor.drops.size() == 15);
    CHECK(floor.excludedKept == 2);
}

TEST_CASE("excluded trim: a pool like the Garden Show's keeps none of the mansion's staff") {
    // 28 entries: 5 start-enabled scene guards (0-3, 27) and 23 dedicated targets of the mission brick (4-26), 5 of
    // them guards; 19 of the 23 are inside the closed mansion (SealedActors.inc). Eight scene guards are eligible.
    std::vector<fr::PoolEntry> pool;
    const std::set<size_t> guards {0, 1, 2, 3, 6, 9, 17, 19, 23, 27};
    const std::set<size_t> outside {0, 1, 2, 3, 5, 9, 10, 26, 27};
    std::set<fr::Guid> excluded;

    for (uint64_t i = 0; i < 28; ++i) {
        const bool scene = i < 4 || i == 27;
        pool.push_back({1 + i, true, guards.contains(i) ? kGuard : kCivilian, Repo(1 + i),
                        scene ? kSceneBrick : kMissionBrick, scene});

        if (!outside.contains(i))
            excluded.insert(Repo(1 + i));
    }

    REQUIRE(excluded.size() == 19);
    std::vector<fr::TargetCandidate> candidates;

    for (uint64_t i = 0; i < 8; ++i)
        candidates.push_back(Candidate(1001 + i, kGuard));

    const std::set<fr::Guid> movers {Repo(9999)}; // the variant's table: none of its movers eligible here

    for (const int32_t seed : {-291838821, 1, 2, 3}) {
        CAPTURE(seed);
        const auto plan = PlanStationary(seed, pool, candidates, 0.811, 0.866, &movers, Fill(true), &excluded);
        CHECK(plan.excludedInPool == 19);
        CHECK(plan.excludedForced == 8); // every eligible guard took an excluded entry
        CHECK(plan.excludedKept == 0);
        CHECK(plan.length == 17);

        const auto after = Applied(pool, candidates, plan);
        REQUIRE(after.size() == 17);
        CHECK(std::set<uint64_t>(after.begin(), after.end()).size() == 17);

        for (const auto& entry : pool) {
            const bool kept = std::find(after.begin(), after.end(), entry.handle) != after.end();
            CHECK(kept == !excluded.contains(*entry.repo));
        }

        for (const auto& candidate : candidates)
            CHECK(std::find(after.begin(), after.end(), candidate.handle) != after.end());

        // No eligible actor at all: the 19 still go, below the minimum length.
        const std::vector<fr::TargetCandidate> none;
        const auto alone = PlanStationary(seed, pool, none, 0.811, 0.866, &movers, Fill(true), &excluded);
        CHECK(alone.length == 9);
        CHECK(alone.excludedKept == 0);
    }
}

TEST_CASE("ShareAt puts the per-load roll between the least and most swap") {
    const fr::ShareRange range =
        fr::ClampShareRange(0.25, 0.75, {fr::kDefaultTargetPoolSwapMin, fr::kDefaultTargetPoolSwapMax});

    for (int32_t seed = 0; seed < 500; ++seed) {
        const double share = fr::ShareAt(range, fr::DrawSwapShare(seed, kScene));
        CHECK(share >= 0.25);
        CHECK(share <= 0.75);
    }

    // The full range [0, 1] keeps the old random share.
    const fr::ShareRange all = fr::ClampShareRange(0.0, 1.0, {});
    CHECK(fr::ShareAt(all, fr::DrawSwapShare(1234, kScene)) == fr::DrawSwapShare(1234, kScene));
    // The least and most equal: a fixed share.
    CHECK(fr::ShareAt(fr::ClampShareRange(0.3, 0.3, {}), 0.9) == doctest::Approx(0.3));
}
