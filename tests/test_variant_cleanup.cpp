#include "doctest.h"

#include "support/VariantTestUtil.h"

#include "Guid.h"
#include "LandslideFull.h"
#include "VariantCleanup.h"
#include "Variants.h"

#include <set>
#include <string>
#include <vector>

namespace {

std::vector<uint64_t> RidsOf(const fr::VariantOption& option) {
    std::vector<uint64_t> rids;

    for (const fr::VariantBrick* brick : option.bricks)
        rids.push_back(brick->rid);

    return rids;
}

std::set<std::string> NamesOf(const std::vector<fr::VariantRemoval>& removals) {
    std::set<std::string> names;

    for (const fr::VariantRemoval& removal : removals)
        names.insert(std::string(removal.npc->name));

    return names;
}

} // namespace

TEST_CASE("variant cleanup: the Reflection's two cable car guards are removed, not just never targets") {
    REQUIRE(fr::kReflection.removed.size() == 2);
    // Its own target ("Slim Shady") stays in the scene: never a target, never removed.
    CHECK(fr::kReflection.notTargets == std::vector<std::string_view> {"851043ee-a2a3-48f2-a5e2-b7e13a28131c"});

    const std::set<std::string> names {std::string(fr::kReflection.removed[0].name),
                                       std::string(fr::kReflection.removed[1].name)};
    CHECK(names == std::set<std::string> {"Hidaka Uno", "Shoichi Kataoka"});

    const auto rids = RidsOf(fr::kReflection);
    const auto removals = fr::VariantRemovals(rids, false);
    REQUIRE(removals.size() == 2);
    CHECK(removals[0].option == &fr::kReflection);
    CHECK(NamesOf(removals) == names);

    // Removed means never a target too.
    const std::set<fr::Guid> excluded = fr::VariantTargetExclusions(rids);
    CHECK(excluded.contains(*fr::ParseGuid("ecbe94ac-a5b3-4d80-8c54-140696707168")));
    CHECK(excluded.contains(*fr::ParseGuid("217286a9-beb9-4a7c-8473-775ab4e9d60e")));
    CHECK(excluded.contains(*fr::ParseGuid("851043ee-a2a3-48f2-a5e2-b7e13a28131c")));
    CHECK(excluded.size() == 3);
    CHECK(fr::VariantRemovedRepos(rids, false).size() == 2);
    CHECK(!fr::VariantRemovedRepos(rids, false).contains(*fr::ParseGuid("851043ee-a2a3-48f2-a5e2-b7e13a28131c")));
}

TEST_CASE("variant cleanup: the simple Landslide's stage sitters, not the full Landslide's never-targets") {
    CHECK(fr::kLandslide.notTargets.empty());
    REQUIRE(fr::kLandslide.removed.size() == fr::kLandslideStageSitterCount);

    auto rids = RidsOf(fr::kLandslide);
    const auto simple = fr::VariantRemovals(rids, false);
    CHECK(simple.size() == fr::kLandslideStageSitterCount);
    CHECK(NamesOf(simple).contains("Greta Palerma"));

    for (const fr::VariantRemoval& removal : simple) {
        CHECK(removal.option == &fr::kLandslide);
        CHECK(removal.npc->brickRid == fr::kNpcAmbientBeachsquareDayRid);
    }

    // The full Landslide loads every brick of kLandslide (through mission_mamba) but not their day crowd; its own
    // never-target (Marco Abiatti) stays in the scene: never removed.
    rids.push_back(0x006026A87FB981F5ull); // mission_mamba
    CHECK(fr::VariantRemovals(rids, true).empty());
    CHECK(fr::VariantRemovedRepos(rids, true).empty());

    for (const std::string_view repo : fr::NeverTargetRepos(fr::LandslideFull()))
        CHECK(!fr::VariantRemovedRepos(rids, false).contains(*fr::ParseGuid(repo)));
}

TEST_CASE("variant cleanup: only options whose bricks are all loaded, each NPC once") {
    const fr::VariantOption both {"test_both", {&fr::test::kScenarioBelliniEaBrick, &fr::kScenarioMamushiBrick},
                                  0,           true,
                                  {},          {{"Both A", "0f504ad2-5557-989e-0000-000000000001"}}};
    const fr::VariantOption one {"test_one",
                                 {&fr::test::kScenarioBelliniEaBrick},
                                 0,
                                 true,
                                 {"0f504ad2-5557-989e-0000-0000000000ff"}, // never a target, but left in
                                 {{"One A", "0f504ad2-5557-989e-0000-000000000002", 0x1234},
                                  {"One B", "0f504ad2-5557-989e-0000-000000000001"}, // the same NPC as Both A
                                  {"Bad", "not-a-guid"}}};
    const fr::VariantOption* options[] = {&both, &one};

    CHECK(fr::VariantRemovals({}, false, options).empty());

    const uint64_t bellini[] = {0x1234, fr::test::kScenarioBelliniEaBrick.rid};
    const auto onlyOne = fr::VariantRemovals(bellini, false, options);
    CHECK(NamesOf(onlyOne) == std::set<std::string> {"One A", "One B"}); // malformed ids skipped
    // notTargets are never removed.
    CHECK(fr::VariantRemovedRepos(bellini, false, options).size() == 2);
    CHECK(!fr::VariantRemovedRepos(bellini, false, options)
               .contains(*fr::ParseGuid("0f504ad2-5557-989e-0000-0000000000ff")));
    CHECK(
        fr::VariantTargetExclusions(bellini, options).contains(*fr::ParseGuid("0f504ad2-5557-989e-0000-0000000000ff")));

    const uint64_t all[] = {fr::kScenarioMamushiBrick.rid, fr::test::kScenarioBelliniEaBrick.rid};
    const auto removals = fr::VariantRemovals(all, false, options);
    CHECK(NamesOf(removals) == std::set<std::string> {"Both A", "One A"}); // the first option's entry wins
    CHECK(fr::test::MalformedVariantExclusions(one) == std::vector<std::string_view> {"not-a-guid"});

    // No option's table has a malformed id; every removed NPC is named.
    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        CAPTURE(option->name);
        CHECK(fr::test::MalformedVariantExclusions(*option).empty());

        for (const fr::StrandedNpc& npc : option->removed)
            CHECK(!npc.name.empty());
    }
}

TEST_CASE("variant cleanup: an actor matches by repository id, in its brick when one is named") {
    const fr::Guid repo = *fr::ParseGuid("0f504ad2-5557-989e-0000-000000000001");
    const fr::Guid other = *fr::ParseGuid("0f504ad2-5557-989e-0000-000000000002");
    const fr::StrandedNpc inBrick {"A", "0f504ad2-5557-989e-0000-000000000001", 0x77};
    const fr::StrandedNpc anyBrick {"A", "0F504AD2-5557-989E-0000-000000000001"};
    const fr::StrandedNpc bad {"A", "not-a-guid"};

    CHECK(fr::IsStrandedNpc(inBrick, repo, 0x77));
    CHECK(!fr::IsStrandedNpc(inBrick, repo, 0x78));
    CHECK(!fr::IsStrandedNpc(inBrick, other, 0x77));
    CHECK(fr::IsStrandedNpc(anyBrick, repo, 0x78));
    CHECK(!fr::IsStrandedNpc(bad, fr::Guid {}, 0x77));
}

TEST_CASE("variant cleanup: the brick walk takes the named bricks first, then the rest when needed") {
    const uint64_t loaded[] = {0x10, 0x20, 0x30, 0x40};
    const fr::StrandedNpc in20 {"A", "0f504ad2-5557-989e-0000-000000000001", 0x20};
    const fr::StrandedNpc in40 {"B", "0f504ad2-5557-989e-0000-000000000002", 0x40};
    const fr::StrandedNpc in99 {"C", "0f504ad2-5557-989e-0000-000000000003", 0x99}; // not loaded
    const fr::StrandedNpc anywhere {"D", "0f504ad2-5557-989e-0000-000000000004"};

    using Order = std::vector<size_t>;
    const fr::VariantRemoval named[] = {{&fr::kReflection, &in40}, {&fr::kReflection, &in20}};
    CHECK(fr::CleanupBrickOrder(loaded, named) == Order {1, 3});

    const fr::VariantRemoval missing[] = {{&fr::kReflection, &in99}};
    CHECK(fr::CleanupBrickOrder(loaded, missing).empty());

    const fr::VariantRemoval mixed[] = {{&fr::kReflection, &in40}, {&fr::kReflection, &anywhere}};
    CHECK(fr::CleanupBrickOrder(loaded, mixed) == Order {3, 0, 1, 2});

    CHECK(fr::CleanupBrickOrder(loaded, {}).empty());
}

TEST_CASE("variant cleanup: the target pool must be read, but a showdown has none") {
    using P = fr::CleanupPool;

    CHECK(fr::DecideCleanupPool(false, true, 2, 2) == P::Read);
    CHECK(fr::DecideCleanupPool(false, true, 2, 1) == P::Unread);  // one list unreadable: anyone could be a target
    CHECK(fr::DecideCleanupPool(false, true, 0, 0) == P::Unread);  // a mild mission without its picker
    CHECK(fr::DecideCleanupPool(false, false, 0, 0) == P::Unread); // mission brick not loaded
    // A showdown's targets are the mission brick's own (no target picker).
    CHECK(fr::DecideCleanupPool(true, true, 0, 0) == P::None);
    CHECK(fr::DecideCleanupPool(true, false, 0, 0) == P::Unread);
    CHECK(fr::DecideCleanupPool(true, true, 1, 0) == P::Unread);
    CHECK(fr::DecideCleanupPool(true, true, 1, 1) == P::Read);
}

TEST_CASE("variant cleanup: who is switched off") {
    using A = fr::CleanupAction;
    using F = fr::CleanupFacts;

    CHECK(fr::DecideCleanup(F {true, true, false, true}) == A::SwitchOff);
    CHECK(fr::DecideCleanup(F {true, true, true, true}) == A::InPool); // a possible target: kept
    CHECK(fr::DecideCleanup(F {true, true, false, false}) == A::AlreadyOff);
    CHECK(fr::DecideCleanup(F {true, false, false, true}) == A::Unreadable);
    CHECK(fr::DecideCleanup(F {false, false, false, false}) == A::NotFound);
    // The pool wins over an unreadable actor: never touched.
    CHECK(fr::DecideCleanup(F {true, false, true, true}) == A::InPool);

    for (const A a : {A::SwitchOff, A::InPool, A::AlreadyOff, A::NotFound, A::Unreadable})
        CHECK(!fr::CleanupActionText(a).empty());

    CHECK(fr::CleanupActionText(A::InPool) == "left in: in the target pool (a possible target)");
}

TEST_CASE("variant cleanup: the log label") {
    const fr::StrandedNpc seen {"Alice Biondi", "4adc1eee-47f2-4d71-a28a-9d46287f2e4f", 0x3D,
                                fr::NpcSpot {-19.61f, 29.51f, 1.44f}};
    const fr::StrandedNpc unseen {"Hidaka Uno", "ecbe94ac-a5b3-4d80-8c54-140696707168"};

    CHECK(fr::StrandedNpcWhere(seen) == " (seen at (-19.61, 29.51, 1.44))");
    CHECK(fr::StrandedNpcWhere(unseen).empty());
}
