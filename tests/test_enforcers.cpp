#include "doctest.h"

#include "Enforcers.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

// Each actor's list count after the plan (0 for an unread count), in input order.
std::vector<size_t> ApplyEnforcerMoves(std::span<const fr::EnforcerActor> actors, const fr::EnforcerPlan& plan) {
    std::vector<size_t> out;
    out.reserve(actors.size());

    for (const fr::EnforcerActor& a : actors)
        out.push_back(a.enforced.value_or(0));

    for (const fr::EnforcerMove& move : plan.moves) {
        if (move.from < out.size() && move.to < out.size())
            std::swap(out[move.from], out[move.to]);
    }

    return out;
}

constexpr const char* kScene = "assembly:/_pro/scenes/missions/skunk/scene_skunk_mild_solitaire.entity";
constexpr int32_t kCivilian = 0;
constexpr int32_t kGuard = 1;
constexpr uint64_t kSceneBrick = 0x77;
constexpr uint64_t kMissionBrick = 0x99;

fr::Guid Outfit(uint64_t low) {
    return fr::Guid::FromHalves(0x1111'2222'3333'4444ull, low);
}

fr::EnforcerActor Actor(uint64_t handle, size_t enforced, uint64_t outfit = 1, int32_t kind = kGuard) {
    fr::EnforcerActor a;
    a.handle = handle;
    a.brickRid = kSceneBrick;
    a.entityId = handle * 7;
    a.startEnabled = true;
    a.crowd = false;
    a.kind = kind;
    a.outfit = Outfit(outfit);
    a.enforced = enforced;
    return a;
}

// A guard group of n actors (outfit 1) whose first e are enforcers with 1, 2, 3, ... professions.
std::vector<fr::EnforcerActor> Group(size_t n, size_t e, uint64_t firstHandle = 1, uint64_t outfit = 1) {
    std::vector<fr::EnforcerActor> out;

    for (size_t i = 0; i < n; ++i)
        out.push_back(Actor(firstHandle + i, i < e ? 1 + i : 0, outfit));

    return out;
}

size_t Enforcers(const std::vector<size_t>& counts) {
    return static_cast<size_t>(std::count_if(counts.begin(), counts.end(), [](size_t c) { return c > 0; }));
}

std::set<uint64_t> EnforcerHandles(const std::vector<fr::EnforcerActor>& actors, const std::vector<size_t>& counts) {
    std::set<uint64_t> out;

    for (size_t i = 0; i < actors.size(); ++i) {
        if (counts[i] > 0)
            out.insert(actors[i].handle);
    }

    return out;
}

fr::EnforcerPlan Plan(const std::vector<fr::EnforcerActor>& actors, int32_t seed = 12345,
                      const fr::EnforcerInclude& include = {}) {
    return fr::PlanEnforcerShuffle(seed, kScene, actors, kMissionBrick, 1.0, true, include);
}

} // namespace

TEST_CASE("Enforcer shuffle keeps the number of enforcers and every list") {
    for (int32_t seed : {1, 2, 3, 99, -7, 123456}) {
        for (const auto& [n, e] : std::vector<std::pair<size_t, size_t>> {{10, 3}, {6, 3}, {5, 4}, {2, 1}, {8, 0}}) {
            const auto actors = Group(n, e);
            const fr::EnforcerPlan plan = Plan(actors, seed);
            const auto after = ApplyEnforcerMoves(actors, plan);

            std::vector<size_t> before;
            for (const auto& a : actors)
                before.push_back(*a.enforced);

            CHECK(Enforcers(after) == e);
            // The same multiset of lists: lists move, never merge, split or vanish.
            auto sortedBefore = before;
            auto sortedAfter = after;
            std::sort(sortedBefore.begin(), sortedBefore.end());
            std::sort(sortedAfter.begin(), sortedAfter.end());
            CHECK(sortedBefore == sortedAfter);
            CHECK(plan.moves.size() == std::min(e, n - e));
        }
    }
}

TEST_CASE("Enforcer shuffle moves every list to a different person when the group has room") {
    const auto actors = Group(10, 3);
    const fr::EnforcerPlan plan = Plan(actors);
    const auto after = ApplyEnforcerMoves(actors, plan);

    const std::set<uint64_t> before {1, 2, 3};
    const std::set<uint64_t> now = EnforcerHandles(actors, after);
    CHECK(now.size() == 3);

    for (uint64_t h : now)
        CHECK_FALSE(before.contains(h));

    CHECK(plan.keptEnforcers == 0);
    CHECK(plan.eligibleEnforcers == 3);
    CHECK(plan.groupsWithMoves == 1);

    // Every actor is in at most one move; each move is enforcer -> non-enforcer.
    std::set<size_t> used;
    for (const auto& m : plan.moves) {
        CHECK(used.insert(m.from).second);
        CHECK(used.insert(m.to).second);
        CHECK(*actors[m.from].enforced > 0);
        CHECK(*actors[m.to].enforced == 0);
    }
}

TEST_CASE("Enforcer shuffle keeps the extra enforcers when a group has too few others") {
    const auto actors = Group(5, 4); // 4 enforcers, 1 other
    const fr::EnforcerPlan plan = Plan(actors);
    CHECK(plan.moves.size() == 1);
    CHECK(plan.keptEnforcers == 3);
    CHECK(Enforcers(ApplyEnforcerMoves(actors, plan)) == 4);
}

TEST_CASE("Enforcer shuffle is deterministic and independent of input order and handles") {
    auto actors = Group(12, 4);
    auto more = Group(9, 2, 100, 2); // a second outfit group
    actors.insert(actors.end(), more.begin(), more.end());

    const auto first = ApplyEnforcerMoves(actors, Plan(actors));
    const auto again = ApplyEnforcerMoves(actors, Plan(actors));
    CHECK(first == again);

    // Who ends up enforcing, by entity id.
    const auto byId = [](const std::vector<fr::EnforcerActor>& list, const std::vector<size_t>& counts) {
        std::map<uint64_t, size_t> out;
        for (size_t i = 0; i < list.size(); ++i)
            out[list[i].entityId] = counts[i];
        return out;
    };

    // Reversed input and different handles (pointers differ between loads): same result by entity id.
    auto reversed = actors;
    std::reverse(reversed.begin(), reversed.end());
    for (auto& a : reversed)
        a.handle += 5000;

    CHECK(byId(actors, first) == byId(reversed, ApplyEnforcerMoves(reversed, Plan(reversed))));

    // Other seeds or scenes give other results (for at least one of a few seeds).
    bool differs = false;
    for (int32_t seed : {2, 3, 4, 5, 6}) {
        if (ApplyEnforcerMoves(actors, Plan(actors, seed)) != first)
            differs = true;
    }
    CHECK(differs);
}

TEST_CASE("Enforcer shuffle: a group's result does not depend on other groups") {
    const auto alone = Group(10, 3);
    auto withOther = alone;
    auto other = Group(7, 2, 200, 5);
    withOther.insert(withOther.begin(), other.begin(), other.end());

    const auto a = ApplyEnforcerMoves(alone, Plan(alone));
    const auto b = ApplyEnforcerMoves(withOther, Plan(withOther));
    CHECK(std::vector<size_t>(b.begin() + static_cast<long>(other.size()), b.end()) == a);
}

TEST_CASE("Enforcer shuffle keeps the actor type and prefers the same outfit") {
    std::vector<fr::EnforcerActor> actors;
    actors.push_back(Actor(1, 2, 1, kGuard));    // guard enforcer in outfit 1
    actors.push_back(Actor(2, 0, 2, kGuard));    // guard, other outfit
    actors.push_back(Actor(3, 0, 1, kCivilian)); // civilian, same outfit id
    const fr::EnforcerPlan plan = Plan(actors);
    CHECK(plan.groups == 3);
    REQUIRE(plan.moves.size() == 1);
    CHECK(plan.moves[0].from == 0);
    CHECK(plan.moves[0].to == 1); // never the civilian
    CHECK(plan.moves[0].tier == fr::EnforcerTier::Brick);
    CHECK(plan.keptEnforcers == 0);

    actors.push_back(Actor(4, 0, 1, kGuard)); // same group as actor 1
    const fr::EnforcerPlan plan2 = Plan(actors);
    REQUIRE(plan2.moves.size() == 1);
    CHECK(plan2.moves[0].from == 0);
    CHECK(plan2.moves[0].to == 3);
    CHECK(plan2.moves[0].tier == fr::EnforcerTier::Group);

    // A lone civilian enforcer with only guards around stays put.
    std::vector<fr::EnforcerActor> mixed {Actor(1, 2, 1, kCivilian), Actor(2, 0, 1, kGuard), Actor(3, 0, 2, kGuard)};
    const fr::EnforcerPlan plan3 = Plan(mixed);
    CHECK(plan3.moves.empty());
    CHECK(plan3.keptEnforcers == 1);
}

TEST_CASE("Enforcer shuffle prefers the same brick, then any brick") {
    std::vector<fr::EnforcerActor> actors;
    actors.push_back(Actor(1, 2));
    auto elsewhere = Actor(2, 0);
    elsewhere.brickRid = kSceneBrick + 1; // same kind and outfit, other brick
    actors.push_back(elsewhere);
    const fr::EnforcerPlan plan = Plan(actors);
    CHECK(plan.groups == 2);
    REQUIRE(plan.moves.size() == 1);
    CHECK(plan.moves[0].to == 1);
    CHECK(plan.moves[0].tier == fr::EnforcerTier::Kind);

    // Without anyBrick the Kind tier is skipped.
    CHECK(fr::PlanEnforcerShuffle(12345, kScene, actors, kMissionBrick, 1.0, false).moves.empty());

    actors.push_back(Actor(3, 0, 7)); // same brick as actor 1, other outfit
    const fr::EnforcerPlan plan2 = Plan(actors);
    REQUIRE(plan2.moves.size() == 1);
    CHECK(plan2.moves[0].from == 0);
    CHECK(plan2.moves[0].to == 2);
    CHECK(plan2.moves[0].tier == fr::EnforcerTier::Brick);
}

TEST_CASE("Enforcer shuffle: the looser tiers take what a crowded group leaves") {
    auto actors = Group(5, 4);        // outfit 1: 4 enforcers, 1 other
    auto roomy = Group(6, 0, 100, 2); // outfit 2, same brick: 6 others
    actors.insert(actors.end(), roomy.begin(), roomy.end());

    const fr::EnforcerPlan plan = Plan(actors);
    CHECK(plan.available == 4);
    CHECK(plan.moves.size() == 4);
    CHECK(plan.movesByTier.at(fr::EnforcerTier::Group) == 1);
    CHECK(plan.movesByTier.at(fr::EnforcerTier::Brick) == 3);
    CHECK(plan.keptEnforcers == 0);

    const auto after = ApplyEnforcerMoves(actors, plan);
    CHECK(Enforcers(after) == 4);

    std::set<size_t> used;
    for (const auto& m : plan.moves) {
        CHECK(used.insert(m.from).second);
        CHECK(used.insert(m.to).second);
        CHECK(*actors[m.from].enforced > 0);
        CHECK(*actors[m.to].enforced == 0);
        CHECK(*actors[m.from].kind == *actors[m.to].kind);
    }
}

TEST_CASE("Enforcer shuffle moves the drawn share of all enforcers, closest tier first") {
    auto actors = Group(10, 4);         // outfit 1: 4 Group-tier moves
    auto crowded = Group(4, 4, 100, 2); // outfit 2: 4 enforcers, no others
    auto spare = Group(6, 0, 200, 3);   // outfit 3: 6 others
    actors.insert(actors.end(), crowded.begin(), crowded.end());
    actors.insert(actors.end(), spare.begin(), spare.end());
    auto target = Actor(300, 2); // an untouched enforcer still counts in the share's base
    target.role = "mule";
    actors.push_back(target);

    const auto plan = [&](double share) {
        return fr::PlanEnforcerShuffle(12345, kScene, actors, kMissionBrick, share);
    };

    CHECK(plan(1.0).available == 8);
    CHECK(plan(1.0).wanted == 9);
    CHECK(plan(1.0).moves.size() == 8);
    CHECK(plan(0.0).moves.empty());
    CHECK(plan(-1.0).moves.empty());
    CHECK(plan(2.0).moves.size() == 8);

    // 1/3 of 9 enforcers: 3 moves, all from the same-outfit tier.
    const fr::EnforcerPlan third = plan(1.0 / 3.0);
    CHECK(third.wanted == 3);
    REQUIRE(third.moves.size() == 3);
    for (const auto& m : third.moves)
        CHECK(m.tier == fr::EnforcerTier::Group);
    CHECK(third.keptEnforcers == 5);

    // 2/3: the 4 Group-tier moves and 2 of the 4 Brick-tier ones.
    const fr::EnforcerPlan twoThirds = plan(2.0 / 3.0);
    CHECK(twoThirds.wanted == 6);
    CHECK(twoThirds.movesByTier.at(fr::EnforcerTier::Group) == 4);
    CHECK(twoThirds.movesByTier.at(fr::EnforcerTier::Brick) == 2);
    CHECK(Enforcers(ApplyEnforcerMoves(actors, twoThirds)) == 9);

    // Deterministic, and the subset follows entity ids, not input order.
    CHECK(ApplyEnforcerMoves(actors, twoThirds) == ApplyEnforcerMoves(actors, plan(2.0 / 3.0)));
    auto reversed = actors;
    std::reverse(reversed.begin(), reversed.end());
    const auto byId = [](const std::vector<fr::EnforcerActor>& list, const std::vector<size_t>& counts) {
        std::map<uint64_t, size_t> out;
        for (size_t i = 0; i < list.size(); ++i)
            out[list[i].entityId] = counts[i];
        return out;
    };
    CHECK(byId(actors, ApplyEnforcerMoves(actors, twoThirds)) ==
          byId(reversed, ApplyEnforcerMoves(
                             reversed, fr::PlanEnforcerShuffle(12345, kScene, reversed, kMissionBrick, 2.0 / 3.0))));
}

TEST_CASE("DrawEnforcerShare lies in [min, max] and is seeded") {
    double lowest = 1.0;
    double highest = 0.0;

    for (int32_t seed = 0; seed < 2000; ++seed) {
        const double share = fr::DrawEnforcerShare(seed, kScene, {0.35, 1.0});
        CHECK(share >= 0.35);
        CHECK(share <= 1.0);
        lowest = std::min(lowest, share);
        highest = std::max(highest, share);
    }

    CHECK(lowest < 0.36);
    CHECK(highest > 0.97);
    CHECK(fr::DrawEnforcerShare(7, kScene, {0.33, 1.0}) == fr::DrawEnforcerShare(7, kScene, {0.33, 1.0}));
    CHECK(fr::DrawEnforcerShare(7, kScene, {1.0, 1.0}) == 1.0);
    CHECK(fr::DrawEnforcerShare(7, kScene, {5.0, 1.0}) == 1.0);
    CHECK(fr::DrawEnforcerShare(7, kScene, {-1.0, 1.0}) >= 0.0);

    // A most share: never above it; at min = max, exactly it.
    for (int32_t seed = 0; seed < 500; ++seed) {
        const double share = fr::DrawEnforcerShare(seed, kScene, {0.2, 0.5});
        CHECK(share >= 0.2);
        CHECK(share <= 0.5);
    }
    CHECK(fr::DrawEnforcerShare(7, kScene, {0.4, 0.4}) == doctest::Approx(0.4));
    // A most below the least is raised to it.
    CHECK(fr::DrawEnforcerShare(7, kScene, {0.6, 0.1}) == doctest::Approx(0.6));

    CHECK(fr::ClampEnforcerShares(5.0, 5.0).min == 1.0);
    CHECK(fr::ClampEnforcerShares(-1.0, 1.0).min == 0.0);
    CHECK(fr::ClampEnforcerShares(std::nan(""), std::nan("")).min == fr::kDefaultEnforcerShareMin);
    CHECK(fr::ClampEnforcerShares(std::nan(""), std::nan("")).max == fr::kDefaultEnforcerShareMax);
}

TEST_CASE("Enforcer shuffle eligibility") {
    auto check = [](fr::EnforcerActor odd, fr::EnforcerReject expected,
                    const fr::EnforcerInclude& include = fr::EnforcerInclude::None()) {
        std::vector<fr::EnforcerActor> actors = Group(4, 1);
        odd.handle = 50;
        odd.entityId = 1; // sorts first
        actors.push_back(odd);
        const fr::EnforcerPlan plan = Plan(actors, 12345, include);
        CHECK(plan.verdicts.back() == expected);

        if (expected != fr::EnforcerReject::None) {
            for (const auto& m : plan.moves) {
                CHECK(m.from != actors.size() - 1);
                CHECK(m.to != actors.size() - 1);
            }
            CHECK(plan.rejected.at(expected) == 1);
        }
    };

    check(Actor(0, 0), fr::EnforcerReject::None);

    auto a = Actor(0, 0);
    a.startEnabled = false;
    check(a, fr::EnforcerReject::StartDisabled);

    a = Actor(0, 0);
    a.crowd = true;
    check(a, fr::EnforcerReject::Crowd);

    a = Actor(0, 0);
    a.brickRid = kMissionBrick;
    check(a, fr::EnforcerReject::MissionBrick);

    for (const char* role : {"mule", "supplier", "lookout", "assassin", "suspect", "name unreadable"}) {
        a = Actor(0, 0);
        a.role = role;
        check(a, fr::EnforcerReject::RoleActor);
    }

    a = Actor(0, 0);
    a.inTargetPool = true;
    check(a, fr::EnforcerReject::TargetPool);

    a = Actor(0, 0);
    a.sentry = true;
    check(a, fr::EnforcerReject::Sentry);

    a = Actor(0, 0);
    a.outfit = fr::Guid {};
    check(a, fr::EnforcerReject::NoOutfit);

    a = Actor(0, 0);
    a.sharedList = true;
    check(a, fr::EnforcerReject::SharedList);

    // The inclusions, each by its own switch; the defaults switch all on.
    const fr::EnforcerInclude all {};
    CHECK(all.targets);
    CHECK(all.late);
    CHECK(all.sentries);

    a = Actor(0, 0);
    a.inTargetPool = true;
    check(a, fr::EnforcerReject::None, all);
    check(a, fr::EnforcerReject::None, {true, false, false});
    check(a, fr::EnforcerReject::TargetPool, {false, true, true});

    a = Actor(0, 0);
    a.startEnabled = false;
    check(a, fr::EnforcerReject::None, all);
    check(a, fr::EnforcerReject::None, {false, true, false});
    check(a, fr::EnforcerReject::StartDisabled, {true, false, true});

    a = Actor(0, 0);
    a.sentry = true;
    check(a, fr::EnforcerReject::None, all);
    check(a, fr::EnforcerReject::None, {false, false, true});
    check(a, fr::EnforcerReject::Sentry, {true, true, false});

    // A start-disabled possible target needs both.
    a = Actor(0, 0);
    a.startEnabled = false;
    a.inTargetPool = true;
    check(a, fr::EnforcerReject::None, all);
    check(a, fr::EnforcerReject::StartDisabled, {true, false, true});
    check(a, fr::EnforcerReject::TargetPool, {false, true, true});

    // The inclusions never let in the mission brick, role actors or crowd characters.
    a = Actor(0, 0);
    a.inTargetPool = true;
    a.sentry = true;
    a.startEnabled = false;
    a.brickRid = kMissionBrick;
    check(a, fr::EnforcerReject::MissionBrick, all);
    a.brickRid = kSceneBrick;
    for (const char* role : {"mule", "supplier", "lookout", "assassin", "suspect", "name unreadable"}) {
        a.role = role;
        check(a, fr::EnforcerReject::RoleActor, all);
    }
    a.role.reset();
    a.crowd = true;
    check(a, fr::EnforcerReject::Crowd, all);

    // An actor the loaded map variant switches off (stranded) never takes part, whatever the inclusions.
    a = Actor(0, 0);
    a.removed = true;
    check(a, fr::EnforcerReject::Removed);
    check(a, fr::EnforcerReject::Removed, all);
    a.inTargetPool = true;
    a.startEnabled = false;
    check(a, fr::EnforcerReject::Removed, all);

    for (int field = 0; field < 6; ++field) {
        a = Actor(0, 0);
        switch (field) {
            case 0: a.startEnabled.reset(); break;
            case 1: a.crowd.reset(); break;
            case 2: a.kind.reset(); break;
            case 3: a.outfit.reset(); break;
            case 4: a.enforced.reset(); break;
            case 5: a.brickRid = 0; break;
        }
        check(a, fr::EnforcerReject::Unreadable);
    }
}

TEST_CASE("Enforcer shuffle never touches a rejected enforcer and counts it as kept") {
    auto actors = Group(6, 1);
    auto target = Actor(40, 3);
    target.role = "lookout";
    actors.push_back(target);
    auto dup = actors.front(); // the same entity twice
    actors.push_back(dup);
    auto none = Actor(0, 2);
    actors.push_back(none);

    const fr::EnforcerPlan plan = Plan(actors);
    CHECK(plan.verdicts[6] == fr::EnforcerReject::RoleActor);
    CHECK(plan.verdicts[7] == fr::EnforcerReject::Duplicate);
    CHECK(plan.verdicts[8] == fr::EnforcerReject::NoHandle);
    CHECK(plan.untouchedEnforcers == 2); // the target and the no-entity one (the duplicate is counted once)
    CHECK(plan.eligible == 6);

    const auto after = ApplyEnforcerMoves(actors, plan);
    CHECK(after[6] == 3);
    CHECK(plan.moves.size() == 1);
}

TEST_CASE("Enforcer shuffle never moves a list to or from a removed actor") {
    // The Reflection: lists moved onto the two stranded cable car guards, who are then switched off.
    auto actors = Group(4, 2);
    auto stranded = Actor(30, 0);
    stranded.removed = true;
    actors.push_back(stranded);
    auto strandedEnforcer = Actor(31, 3);
    strandedEnforcer.removed = true;
    actors.push_back(strandedEnforcer);

    for (const int32_t seed : {1, 2, 3, 12345, 99999}) {
        const fr::EnforcerPlan plan = Plan(actors, seed);
        CHECK(plan.verdicts[4] == fr::EnforcerReject::Removed);
        CHECK(plan.verdicts[5] == fr::EnforcerReject::Removed);

        for (const auto& m : plan.moves) {
            CHECK(m.from < 4);
            CHECK(m.to < 4);
        }

        const auto after = ApplyEnforcerMoves(actors, plan);
        CHECK(after[4] == 0);
        CHECK(after[5] == 3); // kept where it is (the actor goes, its list with it)
        CHECK(plan.untouchedEnforcers == 1);
    }
}

TEST_CASE("EnforcerTierName names every tier") {
    CHECK(std::string(fr::EnforcerTierName(fr::EnforcerTier::Group)) == "same outfit");
    CHECK(std::string(fr::EnforcerTierName(fr::EnforcerTier::Brick)) == "same brick, other outfit");
    CHECK(std::string(fr::EnforcerTierName(fr::EnforcerTier::Kind)) == "other brick");
}

TEST_CASE("EnforcerRejectName names every reason") {
    CHECK(std::string(fr::EnforcerRejectName(fr::EnforcerReject::None)) == "eligible");
    CHECK(std::string(fr::EnforcerRejectName(fr::EnforcerReject::TargetPool)) == "possible target");
    CHECK(std::string(fr::EnforcerRejectName(fr::EnforcerReject::SharedList)) == "shared list storage");
    CHECK(std::string(fr::EnforcerRejectName(fr::EnforcerReject::Removed)) == "switched off by the map variant");
}

TEST_CASE("Enforcer shuffle: possible targets and sentries move lists both ways") {
    // Guard group: 2 enforcers that are a possible target and a sentry, 2 non-enforcers that are too.
    std::vector<fr::EnforcerActor> actors {Actor(1, 2), Actor(2, 1), Actor(3, 0), Actor(4, 0)};
    actors[0].inTargetPool = true;
    actors[1].sentry = true;
    actors[2].sentry = true;
    actors[3].inTargetPool = true;

    const fr::EnforcerPlan plan = Plan(actors);
    CHECK(plan.eligible == 4);
    CHECK(plan.moves.size() == 2);
    CHECK(Enforcers(ApplyEnforcerMoves(actors, plan)) == 2);
    CHECK(EnforcerHandles(actors, ApplyEnforcerMoves(actors, plan)) == std::set<uint64_t> {3, 4});

    const auto& targets = plan.included.at(fr::EnforcerInclusion::Target);
    CHECK(targets.actors == 2);
    CHECK(targets.enforcers == 1);
    CHECK(targets.movedFrom == 1);
    CHECK(targets.movedTo == 1);
    const auto& sentries = plan.included.at(fr::EnforcerInclusion::Sentry);
    CHECK(sentries.actors == 2);
    CHECK(sentries.enforcers == 1);
    CHECK(sentries.movedFrom == 1);
    CHECK(sentries.movedTo == 1);
    CHECK_FALSE(plan.included.contains(fr::EnforcerInclusion::Late));

    // Switched off, they keep their lists and nothing moves.
    const fr::EnforcerPlan none = Plan(actors, 12345, fr::EnforcerInclude::None());
    CHECK(none.moves.empty());
    CHECK(none.untouchedEnforcers == 2);
    CHECK(none.included.empty());
}

TEST_CASE("Enforcer shuffle: start-disabled actors only trade with start-disabled actors") {
    // Same brick, kind and outfit: a start-disabled enforcer, a start-enabled other, then a start-disabled other.
    std::vector<fr::EnforcerActor> actors {Actor(1, 3), Actor(2, 0)};
    actors[0].startEnabled = false;

    const fr::EnforcerPlan alone = Plan(actors);
    CHECK(alone.eligible == 2);
    CHECK(alone.groups == 2); // start state splits the group
    CHECK(alone.moves.empty());
    CHECK(alone.keptEnforcers == 1);
    CHECK(alone.included.at(fr::EnforcerInclusion::Late).enforcers == 1);

    auto late = Actor(3, 0, 9); // other outfit, start disabled: the Brick tier
    late.startEnabled = false;
    actors.push_back(late);
    const fr::EnforcerPlan plan = Plan(actors);
    REQUIRE(plan.moves.size() == 1);
    CHECK(plan.moves[0].from == 0);
    CHECK(plan.moves[0].to == 2);
    CHECK(plan.moves[0].tier == fr::EnforcerTier::Brick);
    CHECK(plan.included.at(fr::EnforcerInclusion::Late).movedFrom == 1);
    CHECK(plan.included.at(fr::EnforcerInclusion::Late).movedTo == 1);

    // Across bricks too (anyBrick): still only between start-disabled actors.
    std::vector<fr::EnforcerActor> spread {Actor(1, 2), Actor(2, 0), Actor(3, 0)};
    spread[0].startEnabled = false;
    spread[1].brickRid = kSceneBrick + 1;
    spread[2].brickRid = kSceneBrick + 2;
    spread[2].startEnabled = false;
    const fr::EnforcerPlan wide = Plan(spread);
    REQUIRE(wide.moves.size() == 1);
    CHECK(wide.moves[0].to == 2);
    CHECK(wide.moves[0].tier == fr::EnforcerTier::Kind);

    // In a random mix every move keeps the start state, and the counts hold per start state.
    for (int32_t seed : {1, 2, 3, 4, 5}) {
        std::vector<fr::EnforcerActor> mix;
        for (uint64_t h = 1; h <= 40; ++h) {
            auto a = Actor(h, h % 3 == 0 ? h % 5 + 1 : 0, h % 4, h % 2 ? kGuard : kCivilian);
            a.startEnabled = h % 7 != 0 && h % 5 != 0;
            a.inTargetPool = h % 6 == 0;
            a.sentry = h % 11 == 0;
            mix.push_back(a);
        }
        const fr::EnforcerPlan p = Plan(mix, seed);
        CHECK_FALSE(p.moves.empty());
        for (const auto& m : p.moves) {
            CHECK(*mix[m.from].startEnabled == *mix[m.to].startEnabled);
            CHECK(*mix[m.from].kind == *mix[m.to].kind);
        }
        const auto after = ApplyEnforcerMoves(mix, p);
        size_t lateBefore = 0;
        size_t lateAfter = 0;
        for (size_t i = 0; i < mix.size(); ++i) {
            if (!*mix[i].startEnabled) {
                lateBefore += *mix[i].enforced > 0;
                lateAfter += after[i] > 0;
            }
        }
        CHECK(lateBefore == lateAfter);
        CHECK(Enforcers(after) == static_cast<size_t>(std::count_if(mix.begin(), mix.end(),
                                                                    [](const auto& a) { return *a.enforced > 0; })));
        CHECK(ApplyEnforcerMoves(mix, Plan(mix, seed)) == after);
    }
}

TEST_CASE("Enforcer shuffle: start-enabled groups pair as before the start state key") {
    // Adding start-disabled actors leaves the start-enabled actors' pairing alone.
    const auto base = Group(10, 3);
    auto withLate = base;
    for (uint64_t h = 500; h < 506; ++h) {
        auto a = Actor(h, h < 502 ? 1 : 0);
        a.startEnabled = false;
        withLate.push_back(a);
    }
    const auto a = ApplyEnforcerMoves(base, Plan(base));
    const auto b = ApplyEnforcerMoves(withLate, Plan(withLate));
    CHECK(std::vector<size_t>(b.begin(), b.begin() + static_cast<long>(base.size())) == a);
}

TEST_CASE("EnforcerInclusionName names every inclusion") {
    CHECK(std::string(fr::EnforcerInclusionName(fr::EnforcerInclusion::Target)) == "possible targets");
    CHECK(std::string(fr::EnforcerInclusionName(fr::EnforcerInclusion::Late)) == "start disabled");
    CHECK(std::string(fr::EnforcerInclusionName(fr::EnforcerInclusion::Sentry)) == "sentries");
}

TEST_CASE("Enforcer shuffle counts mission brick actors whatever the include settings") {
    // A showdown-like scene: the suspects are start disabled in the mission brick, the guards elsewhere.
    std::vector<fr::EnforcerActor> actors = Group(6, 2);
    for (uint64_t h = 100; h < 104; ++h) {
        auto a = Actor(h, h == 100 ? 2 : 0);
        a.brickRid = kMissionBrick;
        a.startEnabled = false;
        actors.push_back(a);
    }

    fr::EnforcerInclude noLate;
    noLate.late = false;
    const fr::EnforcerPlan plan = Plan(actors, 12345, noLate);
    // They are rejected as start disabled, never as mission brick...
    CHECK_FALSE(plan.rejected.contains(fr::EnforcerReject::MissionBrick));
    CHECK(plan.rejected.at(fr::EnforcerReject::StartDisabled) == 4);
    // ...but the backstop still sees them.
    CHECK(plan.missionBrickActors == 4);

    // Included, they are rejected as mission brick and counted the same.
    const fr::EnforcerPlan included = Plan(actors);
    CHECK(included.rejected.at(fr::EnforcerReject::MissionBrick) == 4);
    CHECK(included.missionBrickActors == 4);

    // No handle and duplicates don't count; no mission brick rid: nothing does.
    auto none = actors;
    none.push_back(actors[6]);
    none.back().handle = 0;
    none.push_back(actors[7]);
    CHECK(Plan(none).missionBrickActors == 4);
    CHECK(fr::PlanEnforcerShuffle(12345, kScene, actors, 0, 1.0, true, noLate).missionBrickActors == 0);
    CHECK(Plan(Group(6, 2)).missionBrickActors == 0);
}
