#include "doctest.h"

#include "Seed.h"

#include <optional>
#include <string_view>
#include <tuple>

TEST_CASE("resolve_seed_priority") {
    using fr::SeedSource;

    auto r = fr::ResolveSeed(1, 2, 3);
    CHECK(r.source == SeedSource::Live);
    CHECK(r.seed == 1);

    r = fr::ResolveSeed(std::nullopt, 2, 3);
    CHECK(r.source == SeedSource::Generated);
    CHECK(r.seed == 2);

    r = fr::ResolveSeed(std::nullopt, std::nullopt, 3);
    CHECK(r.source == SeedSource::Persisted);
    CHECK(r.seed == 3);

    CHECK(std::string_view(fr::SeedSourceName(SeedSource::Live)) == "live");
    CHECK(std::string_view(fr::SeedSourceName(SeedSource::Generated)) == "generated");
    CHECK(std::string_view(fr::SeedSourceName(SeedSource::Persisted)) == "persisted");
    CHECK(std::string_view(fr::SeedSourceName(SeedSource::Fallback)) == "fallback (seed not yet known)");
}

TEST_CASE("resolve_seed_zero_is_unknown") {
    using fr::SeedSource;

    // A 0 from any source is skipped in favor of the next one.
    auto r = fr::ResolveSeed(0, 2, 3);
    CHECK(r.source == SeedSource::Generated);
    CHECK(r.seed == 2);

    r = fr::ResolveSeed(0, 0, 3);
    CHECK(r.source == SeedSource::Persisted);
    CHECK(r.seed == 3);

    r = fr::ResolveSeed(0, std::nullopt, 3);
    CHECK(r.source == SeedSource::Persisted);
    CHECK(r.seed == 3);

    r = fr::ResolveSeed(std::nullopt, 0, 3);
    CHECK(r.source == SeedSource::Persisted);

    // A persisted 0 is ignored.
    r = fr::ResolveSeed(5, std::nullopt, 0);
    CHECK(r.source == SeedSource::Live);
    CHECK(r.seed == 5);

    // Negative seeds are real seeds.
    r = fr::ResolveSeed(-7, 2, 3);
    CHECK(r.source == SeedSource::Live);
    CHECK(r.seed == -7);
}

TEST_CASE("resolve_seed_fallback") {
    using fr::SeedSource;

    for (const auto& [live, generated, persisted] :
         {std::tuple<std::optional<int32_t>, std::optional<int32_t>, std::optional<int32_t>> {
              std::nullopt, std::nullopt, std::nullopt},
          {0, std::nullopt, std::nullopt},
          {std::nullopt, 0, std::nullopt},
          {std::nullopt, std::nullopt, 0},
          {0, 0, 0},
          {0, std::nullopt, 0}}) {
        const auto r = fr::ResolveSeed(live, generated, persisted);
        CHECK(r.source == SeedSource::Fallback);
        CHECK(r.seed == 0);
    }
}

TEST_CASE("seed_known") {
    CHECK(fr::IsKnownSeed(std::optional<int32_t>(1)));
    CHECK(fr::IsKnownSeed(std::optional<int32_t>(-1)));
    CHECK_FALSE(fr::IsKnownSeed(std::optional<int32_t>(0)));
    CHECK_FALSE(fr::IsKnownSeed(std::nullopt));
}

TEST_CASE("should_persist_resolution") {
    using fr::SeedSource;

    CHECK(fr::ShouldPersist(fr::ResolveSeed(1, std::nullopt, std::nullopt)));
    CHECK(fr::ShouldPersist(fr::ResolveSeed(std::nullopt, 2, std::nullopt)));
    // Already persisted: nothing to write.
    CHECK_FALSE(fr::ShouldPersist(fr::ResolveSeed(std::nullopt, std::nullopt, 3)));
    // Never persist the fallback (seed 0).
    CHECK_FALSE(fr::ShouldPersist(fr::ResolveSeed(0, 0, 0)));
    CHECK_FALSE(fr::ShouldPersist({SeedSource::Live, 0}));
}

TEST_CASE("late_seed") {
    using fr::LateSeed;
    using fr::SeedSource;

    const fr::SeedResolution fallback {SeedSource::Fallback, 0};
    const fr::SeedResolution live {SeedSource::Live, 42};

    // No seed at scene start, a non-zero live seed later: found late.
    CHECK(fr::DecideLateSeed(fallback, 99) == LateSeed::FoundLate);
    CHECK(fr::DecideLateSeed(fallback, -3) == LateSeed::FoundLate);

    // Still unknown.
    CHECK(fr::DecideLateSeed(fallback, 0) == LateSeed::None);
    CHECK(fr::DecideLateSeed(fallback, std::nullopt) == LateSeed::None);

    // The live seed was used at scene start: a later difference is only logged.
    CHECK(fr::DecideLateSeed(live, 42) == LateSeed::None);
    CHECK(fr::DecideLateSeed(live, 0) == LateSeed::None);
    CHECK(fr::DecideLateSeed(live, std::nullopt) == LateSeed::None);
    CHECK(fr::DecideLateSeed(live, 43) == LateSeed::Differs);
}

TEST_CASE("late_seed_replaces_stale_seed") {
    using fr::LateSeed;
    using fr::SeedSource;

    // A persisted or generated seed was used at scene start and the live seed
    // later differs: the stored seed is stale, so the live one is found late.
    for (const SeedSource source : {SeedSource::Persisted, SeedSource::Generated}) {
        const fr::SeedResolution atStart {source, 42};
        CHECK(fr::DecideLateSeed(atStart, 7) == LateSeed::FoundLate);
        CHECK(fr::DecideLateSeed(atStart, -7) == LateSeed::FoundLate);
        CHECK(fr::DecideLateSeed(atStart, 42) == LateSeed::None);
        CHECK(fr::DecideLateSeed(atStart, 0) == LateSeed::None);
        CHECK(fr::DecideLateSeed(atStart, std::nullopt) == LateSeed::None);
    }
}

TEST_CASE("scene_seed_memory") {
    using fr::SeedSource;

    fr::SceneSeedMemory memory;
    CHECK_FALSE(memory.Find("a").has_value());

    memory.Remember("a", {SeedSource::Persisted, 42});
    auto found = memory.Find("a");
    REQUIRE(found.has_value());
    CHECK(found->source == SeedSource::Persisted);
    CHECK(found->seed == 42);
    CHECK_FALSE(memory.Find("b").has_value());

    // The first start wins: a restart does not replace it.
    memory.Remember("a", {SeedSource::Live, 7});
    CHECK(memory.Find("a")->seed == 42);

    // Loading the same scene keeps it; loading another scene forgets it.
    memory.ForgetOtherScenes("a");
    CHECK(memory.Find("a").has_value());
    memory.ForgetOtherScenes("b");
    CHECK_FALSE(memory.Find("a").has_value());

    memory.Remember("b", {SeedSource::Fallback, 0});
    CHECK(memory.Size() == 1);
    memory.Clear();
    CHECK(memory.Size() == 0);
    CHECK_FALSE(memory.Find("b").has_value());
}

// Restart: ClearScene (fully unloading) may run before the same scene's
// LoadScene. A full unload does not clear the memory; LoadScene's
// ForgetOtherScenes alone decides, so the same scene keeps its first seed.
TEST_CASE("scene_seed_memory_restart_keeps_first_seed") {
    using fr::SeedSource;

    fr::SceneSeedMemory memory;
    memory.ForgetOtherScenes("mission"); // first LoadScene
    memory.Remember("mission", {SeedSource::Fallback, 0});

    // Full unload (nothing forgotten), then LoadScene of the same mission,
    // where a seed found late would otherwise resolve differently.
    memory.ForgetOtherScenes("mission");
    memory.Remember("mission", {SeedSource::Persisted, 99});
    REQUIRE(memory.Find("mission").has_value());
    CHECK(memory.Find("mission")->source == SeedSource::Fallback);
    CHECK(memory.Find("mission")->seed == 0);

    // A different mission forgets it.
    memory.ForgetOtherScenes("other");
    CHECK_FALSE(memory.Find("mission").has_value());
}

// Game data: OnGenerate gave -1676486928, the safehouse's live seed at the
// pick 1769965632 (the seed the campaign's missions ran with), the previous
// campaign's missions -1507265268.
TEST_CASE("pick_seed_adopts_the_live_campaign_seed") {
    using fr::PickSeed;

    CHECK(fr::DecidePickSeed(1769965632, -1676486928, -1507265268) == PickSeed::Adopt);
    CHECK(fr::DecidePickSeed(1769965632, -1676486928, std::nullopt) == PickSeed::Adopt);
    CHECK(fr::DecidePickSeed(1769965632, std::nullopt, std::nullopt) == PickSeed::Adopt);

    // Not read, or 0 (unknown).
    CHECK(fr::DecidePickSeed(std::nullopt, -1676486928, -1507265268) == PickSeed::Unknown);
    CHECK(fr::DecidePickSeed(0, -1676486928, -1507265268) == PickSeed::Unknown);

    // Already stored (a pick seen again, or a late read stored it).
    CHECK(fr::DecidePickSeed(1769965632, 1769965632, -1507265268) == PickSeed::Same);

    // The entity still holds the previous campaign's mission seed.
    CHECK(fr::DecidePickSeed(-1507265268, -1676486928, -1507265268) == PickSeed::Stale);
}

TEST_CASE("seed_alias_maps_only_the_generated_seed") {
    const std::optional<fr::SeedAlias> alias = fr::SeedAlias {-1676486928, 1769965632};

    CHECK(fr::ApplySeedAlias(-1676486928, alias) == 1769965632);
    CHECK(fr::ApplySeedAlias(42, alias) == 42);
    CHECK(fr::ApplySeedAlias(0, alias) == 0);
    CHECK(fr::ApplySeedAlias(-1676486928, std::nullopt) == -1676486928);
}

// The first mission after the pick resolves the adopted seed (the generated
// one is dropped), the same seed a later session resolves from the ini.
TEST_CASE("adopted_seed_resolves_the_same_in_the_first_mission_and_later") {
    using fr::SeedSource;

    const auto firstMission = fr::ResolveSeed(std::nullopt, std::nullopt, 1769965632);
    const auto laterSession = fr::ResolveSeed(std::nullopt, std::nullopt, 1769965632);
    CHECK(firstMission.source == SeedSource::Persisted);
    CHECK(firstMission.seed == laterSession.seed);

    // Its live read at scene playing then finds nothing new.
    CHECK(fr::DecideLateSeed(firstMission, 1769965632) == fr::LateSeed::None);
}
