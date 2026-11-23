#include "doctest.h"

#include "ClientReqs.h"
#include "Merces.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<fr::ClientReqPlan> Plans(int32_t seed, std::span<const fr::ClientReqDef> defs,
                                     std::span<const fr::ClientReqSite> sites, const fr::ClientReqOptions& options) {
    return fr::PlanClientReqCampaign(seed, defs, sites, options).plans;
}

constexpr int32_t kSeed = 540499871;
constexpr int32_t kParis = 16;
constexpr int32_t kMiami = 13;

// The first count client requirement ids (1..16, 18, 19, ...), no blacklists.
std::vector<fr::ClientReqDef> PlainDefs(int32_t count) {
    std::vector<fr::ClientReqDef> defs;
    for (const int32_t id : fr::ClientReqIds()) {
        if (static_cast<int32_t>(defs.size()) == count)
            break;
        defs.push_back({id, {}});
    }
    return defs;
}

// Every client requirement plus the prestige objectives 101..136, as the
// safehouse walk reads them (97 distinct ids).
std::vector<fr::ClientReqDef> SafehouseDefs() {
    std::vector<fr::ClientReqDef> defs = PlainDefs(1000);
    for (int32_t id = 101; id <= 136; ++id)
        defs.push_back({id, {}});
    return defs;
}

fr::ClientReqSite Site(std::string key, int32_t destination, std::vector<int32_t> picks = {38, 43, 4}) {
    return {std::move(key), destination, std::move(picks)};
}

fr::ClientReqOptions MissionMode() {
    fr::ClientReqOptions options;
    options.mode = fr::ClientReqMode::Mission;
    return options;
}

fr::ClientReqOptions CampaignMode(int32_t poolSize) {
    fr::ClientReqOptions options;
    options.mode = fr::ClientReqMode::Campaign;
    options.poolSize = poolSize;
    return options;
}

bool Has(const std::vector<int32_t>& ids, int32_t id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// The 19 locations as sites, all in Paris, three slots each.
std::vector<fr::ClientReqSite> AllSites() {
    std::vector<fr::ClientReqSite> sites;
    for (const fr::ClientReqLocation& location : fr::ClientReqLocations())
        sites.push_back(Site("Mild" + std::string(location.code), kParis));
    return sites;
}

std::vector<int32_t> NonZero(const std::vector<int32_t>& picks) {
    std::vector<int32_t> out;
    std::copy_if(picks.begin(), picks.end(), std::back_inserter(out), [](int32_t id) { return id != 0; });
    return out;
}

} // namespace

TEST_CASE("client_reqs_distinct_within_location") {
    const auto defs = PlainDefs(20);
    for (int32_t seed = 1; seed <= 300; ++seed) {
        const std::vector<fr::ClientReqSite> sites {Site("MildBridge", kParis), Site("MildWhist", kMiami)};
        const auto plans = Plans(seed, defs, sites, {});
        REQUIRE(plans.size() == 2);
        for (const auto& plan : plans) {
            CHECK_FALSE(plan.fallback);
            REQUIRE(plan.picks.size() == 3);
            const std::set<int32_t> distinct(plan.picks.begin(), plan.picks.end());
            CHECK(distinct.size() == 3);
            CHECK_FALSE(distinct.contains(0));
        }
    }
}

TEST_CASE("client_reqs_exclusions") {
    const auto defs = PlainDefs(10);
    fr::ClientReqOptions options;
    options.excluded = {3, 4, 5, 6, 7};
    for (int32_t seed = 1; seed <= 300; ++seed) {
        const std::vector<fr::ClientReqSite> sites {Site("MildBridge", kParis)};
        const auto plan = Plans(seed, defs, sites, options).at(0);
        CHECK(plan.allowed == 5);
        for (const int32_t id : plan.picks)
            CHECK_FALSE(options.excluded.contains(id));
    }
}

TEST_CASE("client_reqs_destination_blacklist") {
    // Id 2 (say, Water Trap) is impossible in Paris only; ids 1-4 exist.
    std::vector<fr::ClientReqDef> defs = PlainDefs(4);
    defs[1].destinationBlacklist = {kParis, 3};
    bool usedInMiami = false;
    for (int32_t seed = 1; seed <= 300; ++seed) {
        const std::vector<fr::ClientReqSite> sites {Site("MildBridge", kParis), Site("MildWhist", kMiami)};
        const auto plans = Plans(seed, defs, sites, {});
        CHECK(plans[0].allowed == 3);
        CHECK(std::find(plans[0].picks.begin(), plans[0].picks.end(), 2) == plans[0].picks.end());
        CHECK(plans[1].allowed == 4);
        usedInMiami |= std::find(plans[1].picks.begin(), plans[1].picks.end(), 2) != plans[1].picks.end();
    }
    CHECK(usedInMiami);
}

TEST_CASE("client_reqs_deterministic_and_order_independent") {
    std::vector<fr::ClientReqDef> defs = PlainDefs(30);
    defs[5].destinationBlacklist = {kMiami};
    const std::vector<fr::ClientReqSite> sites {Site("MildBridge", kParis), Site("MildWhist", kMiami),
                                                Site("MildTrompo", kParis, {59, 41, 0})};
    const auto first = Plans(kSeed, defs, sites, {});
    const auto again = Plans(kSeed, defs, sites, {});

    std::vector<fr::ClientReqDef> reversedDefs(defs.rbegin(), defs.rend());
    const std::vector<fr::ClientReqSite> reversedSites(sites.rbegin(), sites.rend());
    const auto reversed = Plans(kSeed, reversedDefs, reversedSites, {});

    REQUIRE(first.size() == 3);
    for (size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].picks == again[i].picks);
        CHECK(first[i].key == reversed[2 - i].key);
        CHECK(first[i].picks == reversed[2 - i].picks);
    }

    // A site's picks do not depend on which other sites are planned with it.
    const std::vector<fr::ClientReqSite> alone {sites[1]};
    CHECK(Plans(kSeed, defs, alone, {}).at(0).picks == first[1].picks);

    // Another seed changes the picks somewhere.
    bool differs = false;
    for (int32_t seed = 1; seed <= 20 && !differs; ++seed)
        differs = Plans(seed, defs, sites, {}).at(0).picks != first[0].picks;
    CHECK(differs);
}

TEST_CASE("client_reqs_keeps_zero_slots_and_count") {
    const auto defs = PlainDefs(20);
    const std::vector<fr::ClientReqSite> sites {Site("MildTrompo", kParis, {59, 0, 41}),
                                                Site("MildMeyer", kParis, {0, 0, 0})};
    const auto plans = Plans(kSeed, defs, sites, {});
    CHECK(plans[0].needed == 2);
    CHECK(plans[0].picks.size() == 3);
    CHECK(plans[0].picks[1] == 0);
    CHECK(plans[0].picks[0] != 0);
    CHECK(plans[0].picks[2] != 0);
    CHECK(plans[0].picks[0] != plans[0].picks[2]);

    CHECK(plans[1].needed == 0);
    CHECK_FALSE(plans[1].fallback);
    CHECK(plans[1].picks == std::vector<int32_t> {0, 0, 0});
}

TEST_CASE("client_reqs_fallback_keeps_game_picks") {
    std::vector<fr::ClientReqDef> defs = PlainDefs(4);
    defs[0].destinationBlacklist = {kParis};
    defs[1].destinationBlacklist = {kParis};
    fr::ClientReqOptions options;
    options.excluded = {3};
    const std::vector<fr::ClientReqSite> sites {Site("MildBridge", kParis), Site("MildWhist", kMiami)};
    const auto plans = Plans(kSeed, defs, sites, options);

    CHECK(plans[0].fallback);
    CHECK(plans[0].allowed == 1);
    CHECK(plans[0].picks == plans[0].gamePicks);
    CHECK(plans[0].why == "only 1 allowed ids for 3 slots");

    // The other site still gets its own picks.
    CHECK_FALSE(plans[1].fallback);
    CHECK(plans[1].allowed == 3);
    CHECK(std::set<int32_t>(plans[1].picks.begin(), plans[1].picks.end()) == std::set<int32_t> {1, 2, 4});

    // No definitions at all: every site falls back.
    const auto none = Plans(kSeed, {}, sites, {});
    CHECK(none[0].fallback);
    CHECK(none[1].fallback);
}

TEST_CASE("client_reqs_duplicate_definitions_merge") {
    // ID59 has two entities in the safehouse; each id is picked at most once
    // and both blacklists apply.
    std::vector<fr::ClientReqDef> defs = PlainDefs(3);
    defs.push_back({59, {kParis}});
    defs.push_back({59, {kMiami}});
    defs.push_back({0, {}});
    defs.push_back({-4, {}});
    defs.push_back({124, {}}); // a prestige objective: never a client requirement
    for (int32_t seed = 1; seed <= 200; ++seed) {
        const std::vector<fr::ClientReqSite> sites {Site("MildBridge", kParis), Site("MildWhist", kMiami),
                                                    Site("MildShogi", 9)};
        const auto plans = Plans(seed, defs, sites, {});
        CHECK(plans[0].allowed == 3);
        CHECK(plans[1].allowed == 3);
        CHECK(plans[2].allowed == 4);
        for (const auto& plan : plans) {
            const auto picks = NonZero(plan.picks);
            CHECK(std::set<int32_t>(picks.begin(), picks.end()).size() == picks.size());
        }
    }
}

TEST_CASE("client_reqs_parse_ids") {
    std::vector<std::string> bad;
    CHECK(fr::ParseClientReqIds(fr::kDefaultClientReqExclude, bad) == std::set<int32_t> {3, 4, 18});
    CHECK(bad.empty());

    CHECK(fr::ParseClientReqIds(" 18 ,, 3,38 ,", bad) == std::set<int32_t> {3, 18, 38});
    CHECK(bad.empty());

    CHECK(fr::ParseClientReqIds("", bad).empty());
    CHECK(fr::ParseClientReqIds("  ", bad).empty());
    CHECK(bad.empty());

    CHECK(fr::ParseClientReqIds("3, x, 0, -2, 4.5, 7", bad) == std::set<int32_t> {3, 7});
    CHECK(bad == std::vector<std::string> {"x", "0", "-2", "4.5"});

    CHECK(fr::FormatClientReqIds({38, 3, 18}) == "3, 18, 38");
    CHECK(fr::FormatClientReqIds({}).empty());
}

TEST_CASE("client_reqs_labels_and_keys") {
    // The game's own names.
    CHECK(fr::ClientReqName(3) == "Suit Only");
    CHECK(fr::ClientReqName(18) == "Doors Stay Locked");
    CHECK(fr::ClientReqName(38) == "Only Use One Disguise");
    CHECK(fr::ClientReqName(7) == "Neck Breaker");
    CHECK(fr::ClientReqName(11) == "Electrocution Kill");
    CHECK(fr::ClientReqName(36) == "Pacify Guards Unnoticed");
    CHECK(fr::ClientReqName(52) == "Explosive Kill - Fragmentation Grenade");
    CHECK(fr::ClientReqName(17).empty());
    CHECK(fr::ClientReqName(124).empty());
    CHECK(fr::ClientReqLabel(3) == "03 Suit Only");
    CHECK(fr::ClientReqLabel(65) == "65 Eliminate Guards - Sniper");
    CHECK(fr::ClientReqLabel(124) == "124 Shoot Target into Water (prestige objective)");
    CHECK(fr::ClientReqLabel(77) == "77 (not a client requirement)");
    CHECK(fr::ClientReqDisplayName(4) == "No Bodies Found");
    CHECK(fr::ClientReqDisplayName(101) == "Silent Assassin (prestige objective)");
    CHECK(fr::ClientReqDisplayName(77) == "Unknown objective (77)");

    // Payout rows: a name, never a bare number.
    CHECK(fr::PayoutObjectiveName(3) == "Suit Only");
    CHECK(fr::PayoutObjectiveName(115) == "Silent Assassin - Suit Only (prestige)");
    CHECK(fr::PayoutObjectiveName(113) == "Axe Kill (prestige)");
    CHECK(fr::PayoutObjectiveName(17) == "Unused objective");
    CHECK(fr::PayoutObjectiveName(32) == "Unused objective");
    CHECK(fr::PayoutObjectiveName(std::nullopt) == "Unknown objective");
    for (int32_t id = -1; id <= 140; ++id) {
        const std::string name = fr::PayoutObjectiveName(id);
        CHECK(name.find('#') == std::string::npos);
        CHECK(name.find_first_of("0123456789") == std::string::npos);
    }

    // Client requirements A to Z, then prestige A to Z, then the rest.
    std::vector<std::optional<int32_t>> rows {std::nullopt, 17, 101, 3, 113, 67, 1};
    std::stable_sort(rows.begin(), rows.end(), fr::PayoutRowBefore);
    CHECK(rows == std::vector<std::optional<int32_t>> {67, 1, 3, 113, 101, std::nullopt, 17});

    CHECK(fr::ClientReqCpdKey("Bridge", 1) == "MildBridge_ClientReq01");
    CHECK(fr::ClientReqCpdKey("Solitaire", 3) == "MildSolitaire_ClientReq03");

    CHECK(fr::ClientReqLocations().size() == 19);
    const fr::ClientReqLocation* craps = fr::FindClientReqLocation(0xbbbb40d2b468e99dull);
    REQUIRE(craps);
    CHECK(craps->code == "Craps");
    CHECK_FALSE(fr::FindClientReqLocation(0x1234));

    CHECK(fr::DestinationName(16) == "Paris");
    CHECK(fr::DestinationName(0) == "AmbroseIsland");
    CHECK(fr::DestinationName(19) == "WhittletonCreek");
    CHECK(fr::DestinationName(20).empty());
    CHECK(fr::DestinationName(-1).empty());
}

TEST_CASE("client_reqs_parse_cpd_int") {
    CHECK(fr::ParseCpdInt("45.000000") == 45);
    CHECK(fr::ParseCpdInt("0.000000") == 0);
    CHECK(fr::ParseCpdInt("59") == 59);
    CHECK(fr::ParseCpdInt(" 7 ") == 7);
    CHECK(fr::ParseCpdInt("-3.000000") == -3);
    CHECK_FALSE(fr::ParseCpdInt("4.5"));
    CHECK_FALSE(fr::ParseCpdInt("true"));
    CHECK_FALSE(fr::ParseCpdInt("\"Assassination\""));
    CHECK_FALSE(fr::ParseCpdInt(""));
    CHECK_FALSE(fr::ParseCpdInt("1e12"));
}

TEST_CASE("client_reqs_mode_parse") {
    CHECK(fr::kDefaultClientReqMode == fr::ClientReqMode::Campaign);
    CHECK(fr::ParseClientReqMode("campaign") == fr::ClientReqMode::Campaign);
    CHECK(fr::ParseClientReqMode(" Mission ") == fr::ClientReqMode::Mission);
    CHECK(fr::ParseClientReqMode("CAMPAIGN") == fr::ClientReqMode::Campaign);
    CHECK_FALSE(fr::ParseClientReqMode(""));
    CHECK_FALSE(fr::ParseClientReqMode("pool"));
    CHECK_FALSE(fr::ParseClientReqMode("missions"));
    CHECK(fr::ClientReqModeName(fr::ClientReqMode::Campaign) == "campaign");
    CHECK(fr::ClientReqModeName(fr::ClientReqMode::Mission) == "mission");
    CHECK(fr::ClientReqOptions {}.mode == fr::ClientReqMode::Campaign);
}

TEST_CASE("client_reqs_conflict_table") {
    CHECK(fr::ClientReqsConflict(2, 55));
    CHECK(fr::ClientReqsConflict(55, 2));
    CHECK_FALSE(fr::ClientReqsConflict(2, 3));
    CHECK_FALSE(fr::ClientReqsConflict(55, 55));
    CHECK_FALSE(fr::ClientReqConflicts().empty());
}

TEST_CASE("client_reqs_default_pool_size") {
    CHECK(fr::kDefaultClientReqPoolSize == 10);
    CHECK(fr::ClientReqOptions {}.poolSize == 10);
    const std::vector<fr::ClientReqSite> sites {Site("MildBridge", kParis)};

    // 0 or less (an ini written by an older version holds 0): the default.
    for (const int32_t size : {0, -5}) {
        const auto plan = fr::PlanClientReqCampaign(kSeed, SafehouseDefs(), sites, CampaignMode(size));
        CHECK(plan.poolSize == 10);
        CHECK(plan.pool.size() == 10);
        CHECK(plan.poolSizeWhy == "default 10");
    }
    CHECK(fr::PlanClientReqCampaign(kSeed, SafehouseDefs(), sites, CampaignMode(12)).pool.size() == 12);
}

TEST_CASE("client_reqs_pool_size_and_clamp") {
    const auto defs = PlainDefs(30);
    const std::vector<fr::ClientReqSite> sites {Site("MildBridge", kParis)};

    auto plan = fr::PlanClientReqCampaign(kSeed, defs, sites, CampaignMode(7));
    CHECK(plan.mode == fr::ClientReqMode::Campaign);
    CHECK(plan.poolSize == 7);
    CHECK(plan.pool.size() == 7);
    CHECK(std::set<int32_t>(plan.pool.begin(), plan.pool.end()).size() == 7);

    CHECK(fr::PlanClientReqCampaign(kSeed, defs, sites, CampaignMode(1)).poolSize == fr::kMinClientReqPoolSize);
    CHECK(fr::PlanClientReqCampaign(kSeed, defs, sites, CampaignMode(500)).poolSize == 30);
    CHECK(fr::PlanClientReqCampaign(kSeed, defs, sites, CampaignMode(-5)).poolSize == 10);

    // Exclusions shrink the ids the pool is drawn from, and it never holds one.
    fr::ClientReqOptions options = CampaignMode(500);
    options.excluded = {1, 2, 3, 4, 5};
    plan = fr::PlanClientReqCampaign(kSeed, defs, sites, options);
    CHECK(plan.poolSize == 25);
    for (const int32_t id : plan.pool)
        CHECK_FALSE(options.excluded.contains(id));

    // Fewer allowed ids than the minimum: the pool is what there is.
    options.excluded.clear();
    CHECK(fr::PlanClientReqCampaign(kSeed, PlainDefs(2), sites, options).poolSize == 2);

    // Mission mode draws no pool.
    plan = fr::PlanClientReqCampaign(kSeed, defs, sites, MissionMode());
    CHECK(plan.pool.empty());
    CHECK(plan.poolSize == 0);
}

TEST_CASE("client_reqs_missions_pick_from_pool") {
    const auto defs = PlainDefs(40);
    const auto sites = AllSites();
    std::set<int32_t> usedAll;
    for (int32_t seed = 1; seed <= 100; ++seed) {
        const auto plan = fr::PlanClientReqCampaign(seed, defs, sites, CampaignMode(8));
        REQUIRE(plan.pool.size() == 8);
        const std::set<int32_t> pool(plan.pool.begin(), plan.pool.end());
        std::set<int32_t> used;
        for (const auto& site : plan.plans) {
            CHECK_FALSE(site.fallback);
            CHECK(site.toppedUp == 0);
            CHECK(site.fromPool == 8);
            CHECK(std::set<int32_t>(site.picks.begin(), site.picks.end()).size() == 3);
            for (const int32_t id : site.picks) {
                CHECK(pool.contains(id));
                used.insert(id);
                usedAll.insert(id);
            }
        }
        // 19 locations from a pool of 8: the campaign uses at most 8 ids.
        CHECK(used.size() <= 8);
    }
    // Across seeds the pools differ.
    CHECK(usedAll.size() > 8);
}

TEST_CASE("client_reqs_pool_ignores_destination_then_tops_up") {
    // Pool of 3 from ids 1..10; Paris blacklists two of whatever the pool
    // holds, so a Paris site must top up one slot from outside the pool.
    const auto plain = PlainDefs(10);
    const std::vector<fr::ClientReqSite> probe {Site("MildBridge", kMiami)};
    const auto first = fr::PlanClientReqCampaign(kSeed, plain, probe, CampaignMode(3));
    REQUIRE(first.pool.size() == 3);

    std::vector<fr::ClientReqDef> defs = plain;
    defs[static_cast<size_t>(first.pool[0] - 1)].destinationBlacklist = {kParis};
    defs[static_cast<size_t>(first.pool[1] - 1)].destinationBlacklist = {kParis};
    const std::vector<fr::ClientReqSite> sites {Site("MildBridge", kParis), Site("MildWhist", kMiami)};
    const auto plan = fr::PlanClientReqCampaign(kSeed, defs, sites, CampaignMode(3));
    CHECK(plan.pool == first.pool); // destinations do not change the pool

    const auto& paris = plan.plans[0];
    CHECK_FALSE(paris.fallback);
    CHECK(paris.fromPool == 1);
    CHECK(paris.toppedUp == 2);
    CHECK(paris.allowed == 8);
    CHECK(Has(paris.picks, first.pool[2]));
    CHECK_FALSE(Has(paris.picks, first.pool[0]));
    CHECK_FALSE(Has(paris.picks, first.pool[1]));
    CHECK(std::set<int32_t>(paris.picks.begin(), paris.picks.end()).size() == 3);

    const auto& miami = plan.plans[1];
    CHECK(miami.toppedUp == 0);
    CHECK(std::set<int32_t>(miami.picks.begin(), miami.picks.end()) ==
          std::set<int32_t>(first.pool.begin(), first.pool.end()));

    // Not enough even with the top-up: the game's picks stay.
    std::vector<fr::ClientReqDef> few = PlainDefs(3);
    few[0].destinationBlacklist = {kParis};
    const std::vector<fr::ClientReqSite> parisOnly {Site("MildBridge", kParis)};
    const auto none = fr::PlanClientReqCampaign(kSeed, few, parisOnly, CampaignMode(3)).plans.at(0);
    CHECK(none.fallback);
    CHECK(none.picks == none.gamePicks);
}

TEST_CASE("client_reqs_conflict_campaign_mode") {
    // Every id in the pool: 02 and 55 cannot both be in it.
    std::vector<fr::ClientReqDef> defs;
    for (const int32_t id : {1, 2, 4, 5, 55})
        defs.push_back({id, {}});
    bool sawTwo = false;
    bool sawFiftyFive = false;
    for (int32_t seed = 1; seed <= 300; ++seed) {
        const auto plan = fr::PlanClientReqCampaign(seed, defs, AllSites(), CampaignMode(500));
        CHECK(plan.pool.size() == 4);
        CHECK(plan.blocked.size() == 1);
        CHECK_FALSE((Has(plan.pool, 2) && Has(plan.pool, 55)));
        sawTwo |= Has(plan.pool, 2);
        sawFiftyFive |= Has(plan.pool, 55);
        for (const auto& site : plan.plans) {
            CHECK_FALSE(plan.blocked.contains(site.picks[0]));
            CHECK_FALSE(plan.blocked.contains(site.picks[1]));
            CHECK_FALSE(plan.blocked.contains(site.picks[2]));
        }
    }
    // Either can win.
    CHECK(sawTwo);
    CHECK(sawFiftyFive);

    // Excluding one leaves the other free.
    fr::ClientReqOptions options = CampaignMode(500);
    options.excluded = {2};
    const auto plan = fr::PlanClientReqCampaign(kSeed, defs, AllSites(), options);
    CHECK(plan.blocked.empty());
    CHECK(Has(plan.pool, 55));
}

TEST_CASE("client_reqs_conflict_mission_mode") {
    // Small id set so both 02 and 55 would often be picked somewhere.
    std::vector<fr::ClientReqDef> defs;
    for (const int32_t id : {1, 2, 4, 5, 55})
        defs.push_back({id, {}});
    bool sawTwo = false;
    bool sawFiftyFive = false;
    for (int32_t seed = 1; seed <= 300; ++seed) {
        const auto plan = fr::PlanClientReqCampaign(seed, defs, AllSites(), MissionMode());
        std::set<int32_t> used;
        for (const auto& site : plan.plans) {
            CHECK_FALSE(site.fallback);
            CHECK(site.allowed == 4);
            used.insert(site.picks.begin(), site.picks.end());
        }
        CHECK_FALSE((used.contains(2) && used.contains(55)));
        sawTwo |= used.contains(2);
        sawFiftyFive |= used.contains(55);

        // The same campaign ranking decides the winner in both modes.
        CHECK(plan.blocked == fr::PlanClientReqCampaign(seed, defs, AllSites(), CampaignMode(0)).blocked);
    }
    CHECK(sawTwo);
    CHECK(sawFiftyFive);
}

TEST_CASE("client_reqs_campaign_deterministic_and_order_independent") {
    std::vector<fr::ClientReqDef> defs = PlainDefs(30);
    defs.push_back({55, {}});
    defs[5].destinationBlacklist = {kMiami};
    fr::ClientReqOptions options = CampaignMode(0);
    const std::vector<fr::ClientReqSite> sites {Site("MildBridge", kParis), Site("MildWhist", kMiami),
                                                Site("MildTrompo", kParis, {59, 41, 0})};
    const auto first = fr::PlanClientReqCampaign(kSeed, defs, sites, options);
    const auto again = fr::PlanClientReqCampaign(kSeed, defs, sites, options);
    CHECK(first.pool == again.pool);
    CHECK(first.poolSize == 10); // the default

    std::vector<fr::ClientReqDef> reversedDefs(defs.rbegin(), defs.rend());
    const std::vector<fr::ClientReqSite> reversedSites(sites.rbegin(), sites.rend());
    const auto reversed = fr::PlanClientReqCampaign(kSeed, reversedDefs, reversedSites, options);
    CHECK(first.pool == reversed.pool);
    CHECK(first.blocked == reversed.blocked);
    for (size_t i = 0; i < first.plans.size(); ++i) {
        CHECK(first.plans[i].picks == again.plans[i].picks);
        CHECK(first.plans[i].picks == reversed.plans[2 - i].picks);
    }

    // A site's picks do not depend on the other sites.
    const std::vector<fr::ClientReqSite> alone {sites[1]};
    CHECK(fr::PlanClientReqCampaign(kSeed, defs, alone, options).plans.at(0).picks == first.plans[1].picks);

    // The pool's stream is not a location's: the pool's order is not Bridge's ranking.
    const auto big = fr::PlanClientReqCampaign(kSeed, defs, sites, CampaignMode(500));
    const auto bridgeMission = fr::PlanClientReqCampaign(kSeed, defs, sites, MissionMode()).plans.at(0);
    CHECK(std::vector<int32_t>(big.pool.begin(), big.pool.begin() + 3) != NonZero(bridgeMission.picks));

    // Another seed changes the pool.
    bool differs = false;
    for (int32_t seed = 1; seed <= 20 && !differs; ++seed)
        differs = fr::PlanClientReqCampaign(seed, defs, sites, CampaignMode(8)).pool !=
                  fr::PlanClientReqCampaign(kSeed, defs, sites, CampaignMode(8)).pool;
    CHECK(differs);
}

TEST_CASE("client_reqs_pick_watch_waits_then_applies_once_stable") {
    using Step = fr::ClientReqWatchStep;
    fr::ClientReqPickWatch watch(true, 500);
    const std::vector<int32_t> none(6, 0);
    const std::vector<int32_t> picks {21, 0, 45, 0, 0, 0};
    const std::vector<int32_t> more {21, 0, 45, 65, 0, 21};

    // Minutes at the selection screen: still waiting, never gives up.
    for (int64_t t = 0; t < 600'000; t += 1'000)
        CHECK(watch.OnRead(none, t) == Step::Waiting);
    CHECK(watch.sawEmpty());

    // Picks appear; they must hold for the settle time.
    CHECK(watch.OnRead(picks, 600'000) == Step::Settling);
    CHECK(watch.candidateSinceMs() == 600'000);
    CHECK(watch.OnRead(picks, 600'499) == Step::Settling);
    // A slot written later restarts the settle time.
    CHECK(watch.OnRead(more, 600'499) == Step::Settling);
    CHECK(watch.OnRead(more, 600'800) == Step::Settling);
    CHECK(watch.OnRead(more, 600'999) == Step::Apply);
}

TEST_CASE("client_reqs_pick_watch_first_read") {
    using Step = fr::ClientReqWatchStep;
    const std::vector<int32_t> none(3, 0);
    const std::vector<int32_t> picks {38, 43, 4};

    // Generated in this visit: picks at the first read are this campaign's.
    fr::ClientReqPickWatch fresh(true, 500);
    CHECK(fresh.OnRead(picks, 0) == Step::Settling);
    CHECK(fresh.OnRead(picks, 500) == Step::Apply);

    // Carried over from an earlier visit or session: picks there from the
    // start were made without the mod and are left alone.
    fr::ClientReqPickWatch carried(false, 500);
    CHECK(carried.OnRead(picks, 0) == Step::AlreadyThere);
    CHECK_FALSE(carried.sawEmpty());

    // ... but picks appearing after an empty read are the player's choice now.
    fr::ClientReqPickWatch later(false, 500);
    CHECK(later.OnRead(none, 0) == Step::Waiting);
    CHECK(later.OnRead(picks, 1'000) == Step::Settling);
    CHECK(later.OnRead(picks, 1'500) == Step::Apply);

    // Picks cleared again before they settled: back to waiting, a new settle time.
    fr::ClientReqPickWatch flicker(true, 500);
    CHECK(flicker.OnRead(picks, 0) == Step::Settling);
    CHECK(flicker.OnRead(none, 400) == Step::Waiting);
    CHECK(flicker.OnRead(picks, 600) == Step::Settling);
    CHECK(flicker.OnRead(picks, 1'000) == Step::Settling);
    CHECK(flicker.OnRead(picks, 1'100) == Step::Apply);
    CHECK(flicker.reads() == 5);
}

TEST_CASE("client_reqs_resumable_seed") {
    CHECK(fr::ResumableClientReqSeed("308605890", 308605890) == 308605890);
    CHECK(fr::ResumableClientReqSeed(" -2124433314 ", -2124433314) == -2124433314);
    CHECK_FALSE(fr::ResumableClientReqSeed("", 308605890));
    CHECK_FALSE(fr::ResumableClientReqSeed("0", 0));
    CHECK_FALSE(fr::ResumableClientReqSeed("308605890", std::nullopt));
    CHECK_FALSE(fr::ResumableClientReqSeed("308605890", 12345)); // another campaign generated since
    CHECK_FALSE(fr::ResumableClientReqSeed("30860589x", 308605890));
    CHECK_FALSE(fr::ResumableClientReqSeed("99999999999", 308605890));
}

TEST_CASE("client_reqs_only_client_requirement_ids") {
    // The 61 BonusReq ids; the prestige objectives (101..136) share the
    // definition class and are never client requirements.
    CHECK(fr::ClientReqIds().size() == 61);
    for (const int32_t id : fr::ClientReqIds()) {
        CHECK(id > 0);
        CHECK(id < 100);
        CHECK(fr::IsClientReqId(id));
        CHECK_FALSE(fr::ClientReqName(id).empty());
        CHECK(fr::ClientReqLabel(id).find("not a client") == std::string::npos);
    }
    CHECK(std::is_sorted(fr::ClientReqIds().begin(), fr::ClientReqIds().end()));
    for (const int32_t id : {0, -1, 17, 32, 46, 73, 99, 100, 101, 124, 136, 200})
        CHECK_FALSE(fr::IsClientReqId(id));
    for (int32_t id = 101; id <= 136; ++id) {
        CHECK(fr::PrestigeObjectiveName(id) != nullptr);
        CHECK(fr::ClientReqDisplayName(id).find("(unknown)") == std::string::npos);
    }

    // Tonight's walk: 97 distinct ids, 36 of them prestige. None is ever
    // pooled or picked, in either mode, and they do not count as allowed.
    const auto defs = SafehouseDefs();
    for (int32_t seed = 1; seed <= 200; ++seed) {
        for (const auto& options : {CampaignMode(500), MissionMode()}) {
            const auto plan = fr::PlanClientReqCampaign(seed, defs, AllSites(), options);
            CHECK(plan.campaignAllowed + plan.blocked.size() == 61);
            for (const int32_t id : plan.pool)
                CHECK(fr::IsClientReqId(id));
            for (const auto& site : plan.plans)
                for (const int32_t id : site.picks)
                    CHECK(fr::IsClientReqId(id));
        }
    }
}

TEST_CASE("client_reqs_default_exclusions") {
    std::vector<std::string> bad;
    const std::set<int32_t> excluded = fr::ParseClientReqIds(fr::kDefaultClientReqExclude, bad);
    CHECK(excluded == std::set<int32_t> {3, 4, 18});
    CHECK(fr::ClientReqName(4) == "No Bodies Found");

    // With the default exclusions nothing excluded is ever planned.
    fr::ClientReqOptions options = CampaignMode(8);
    options.excluded = excluded;
    for (int32_t seed = 1; seed <= 200; ++seed) {
        const auto plan = fr::PlanClientReqCampaign(seed, SafehouseDefs(), AllSites(), options);
        for (const int32_t id : plan.pool)
            CHECK_FALSE(excluded.contains(id));
        for (const auto& site : plan.plans)
            for (const int32_t id : site.picks)
                CHECK_FALSE(excluded.contains(id));
    }
}

TEST_CASE("client_reqs_campaign_types") {
    // The eight crime sectors by CrimeType; 1..3 as seen in the CpdSets.
    CHECK(fr::CampaignTypeNameForIndex(0) == "Arms Trafficking");
    CHECK(fr::CampaignTypeNameForIndex(1) == "Assassination");
    CHECK(fr::CampaignTypeNameForIndex(2) == "Organ Trafficking");
    CHECK(fr::CampaignTypeNameForIndex(3) == "Big Pharma");
    CHECK(fr::CampaignTypeNameForIndex(4) == "Eco Crime");
    CHECK(fr::CampaignTypeNameForIndex(5) == "Psy Ops");
    CHECK(fr::CampaignTypeNameForIndex(6) == "Espionage");
    CHECK(fr::CampaignTypeNameForIndex(7) == "Sick Games");
    CHECK(fr::CampaignTypeNameForIndex(8).empty());
    CHECK(fr::CampaignTypeNameForIndex(-1).empty());

    CHECK(fr::CanonicalCampaignTypeName(" big PHARMA ") == "Big Pharma");
    CHECK(fr::CanonicalCampaignTypeName("Espionage") == "Espionage");
    // Case, spaces and punctuation do not matter: the table's spelling.
    CHECK(fr::CanonicalCampaignTypeName("PSY-OPS") == "Psy Ops");
    CHECK(fr::CanonicalCampaignTypeName("PsyOps") == "Psy Ops");
    CHECK(fr::CanonicalCampaignTypeName("Eco-Crime") == "Eco Crime");
    CHECK(fr::CanonicalCampaignTypeName("Smuggling") == "Smuggling"); // not a table name: kept as is
    CHECK(fr::CanonicalCampaignTypeName("CPD_CrimeSectorName").empty());
    CHECK(fr::CanonicalCampaignTypeName("[assets:/x.png]").empty());
    CHECK(fr::CanonicalCampaignTypeName("  ").empty());
    CHECK(fr::IsKnownCampaignTypeName("sick games"));
    CHECK_FALSE(fr::IsKnownCampaignTypeName("Smuggling"));
    CHECK_FALSE(fr::IsKnownCampaignTypeName(""));

    CHECK(fr::ClientReqCampaignKey(std::nullopt) == "campaign");
    CHECK(fr::ClientReqCampaignKey(std::string("organ trafficking")) == "campaign:Organ Trafficking");
    CHECK(fr::ClientReqCampaignKey(std::string("Psy-Ops")) == "campaign:Psy Ops");
    CHECK(fr::ClientReqCampaignKey(std::string("CPD_CrimeSectorName")) == "campaign");
}

TEST_CASE("client_reqs_resolve_campaign_type") {
    // Game data: the helper's strings before and after the pick, index 1 -> 2.
    const std::vector<std::string> before {"CPD_CrimeSectorName", "Assassination", "CPD_CrimeSectorName"};
    const std::vector<std::string> after {"CPD_CrimeSectorName", "Assassination", "CPD_CrimeSectorName",
                                          "Organ Trafficking"};
    auto choice = fr::ResolveClientReqCampaignType(before, after, 2);
    REQUIRE(choice.name);
    CHECK(*choice.name == "Organ Trafficking");

    // The same type as the last campaign: nothing new, one name.
    choice = fr::ResolveClientReqCampaignType(before, before, 1);
    CHECK(choice.name == std::optional<std::string>("Assassination"));

    // A later visit (no before): the one name there is.
    const std::vector<std::string> later {"CPD_CrimeSectorName", "Organ Trafficking"};
    CHECK(fr::ResolveClientReqCampaignType({}, later, std::nullopt).name ==
          std::optional<std::string>("Organ Trafficking"));

    // Ambiguous strings: the index decides.
    const std::vector<std::string> two {"Assassination", "Big Pharma"};
    CHECK(fr::ResolveClientReqCampaignType({}, two, 3).name == std::optional<std::string>("Big Pharma"));

    // Nothing readable and an index not in the table: unknown.
    choice = fr::ResolveClientReqCampaignType({}, std::vector<std::string> {"CPD_CrimeSectorName"}, 9);
    CHECK_FALSE(choice.name);
    CHECK_FALSE(choice.how.empty());
    CHECK_FALSE(fr::ResolveClientReqCampaignType({}, two, std::nullopt).name);

    // Nothing readable but a table index: the index's name.
    choice = fr::ResolveClientReqCampaignType({}, std::vector<std::string> {"CPD_CrimeSectorName"}, 7);
    CHECK(choice.name == std::optional<std::string>("Sick Games"));

    // A table name new since the walk wins over a disagreeing index (logged).
    const std::vector<std::string> fresh {"CPD_CrimeSectorName", "Assassination", "Espionage"};
    choice = fr::ResolveClientReqCampaignType(before, fresh, 5);
    CHECK(choice.name == std::optional<std::string>("Espionage"));
    CHECK(choice.how.find("Psy Ops") != std::string::npos);

    // Another spelling of a table name: the table's spelling.
    const std::vector<std::string> spelled {"CPD_CrimeSectorName", "Assassination", "PSY-OPS"};
    CHECK(fr::ResolveClientReqCampaignType(before, spelled, std::nullopt).name ==
          std::optional<std::string>("Psy Ops"));

    // A name not in the table gives way to the index's table name (the pool
    // previewed for that book) ...
    const std::vector<std::string> odd {"CPD_CrimeSectorName", "Assassination", "Syndicate Of Eco Crimes"};
    choice = fr::ResolveClientReqCampaignType(before, odd, 4);
    CHECK(choice.name == std::optional<std::string>("Eco Crime"));
    CHECK(choice.how.find("Syndicate Of Eco Crimes") != std::string::npos);
    // ... and without a table index is still a type (its own pool, not previewed).
    CHECK(fr::ResolveClientReqCampaignType(before, odd, std::nullopt).name ==
          std::optional<std::string>("Syndicate Of Eco Crimes"));
    CHECK(fr::ResolveClientReqCampaignType(before, odd, 12).name ==
          std::optional<std::string>("Syndicate Of Eco Crimes"));
}

TEST_CASE("client_reqs_preview_matches_applied") {
    const auto defs = SafehouseDefs();
    fr::ClientReqOptions options = CampaignMode(0);
    options.excluded = {3, 4, 18};
    const auto sites = AllSites();
    const std::vector<std::string> before {"CPD_CrimeSectorName", "Assassination"};
    for (int32_t seed = 1; seed <= 50; ++seed) {
        const auto previews = fr::PreviewClientReqCampaignTypes(seed, defs, options);
        // No books read: every crime sector, alphabetically (the shelf is shuffled).
        const std::vector<std::string> shelf {"Arms Trafficking", "Assassination",     "Big Pharma", "Eco Crime",
                                              "Espionage",        "Organ Trafficking", "Psy Ops",    "Sick Games"};
        REQUIRE(previews.size() == shelf.size());
        for (size_t i = 0; i < previews.size(); ++i) {
            CHECK(previews[i].name == shelf[i]);
            CHECK(previews[i].label == previews[i].name);
            CHECK_FALSE(previews[i].slot);
        }
        std::set<std::vector<int32_t>> distinct;
        for (const auto& preview : previews) {
            CHECK(preview.plan.plans.empty());
            CHECK(preview.plan.pool.size() == 10);

            // The pick as the game reports it (any case, among the helper's
            // other strings) resolves to the previewed name and its pool.
            std::string reported = preview.name;
            std::transform(reported.begin(), reported.end(), reported.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::vector<std::string> after = before;
            after.push_back(" " + reported);
            const auto choice = fr::ResolveClientReqCampaignType(before, after, std::nullopt);
            if (preview.name == "Assassination")
                CHECK(choice.name == std::optional<std::string>("Assassination")); // same type again
            else
                CHECK(choice.name == std::optional<std::string>(preview.name));

            const auto applied = fr::PlanClientReqTypeCampaign(seed, defs, sites, options, choice.name);
            CHECK(applied.pool == preview.plan.pool);
            CHECK(applied.blocked == preview.plan.blocked);
            for (const auto& plan : applied.plans) {
                CHECK(plan.toppedUp == 0);
                for (const int32_t id : NonZero(plan.picks))
                    CHECK(Has(preview.plan.pool, id));
            }
            distinct.insert(preview.plan.pool);
        }
        // Each book gets its own pool.
        CHECK(distinct.size() == previews.size());
    }
}

TEST_CASE("client_reqs_read_book") {
    using Prop = fr::ClientReqBookProperty;

    // A text naming the sector and an index.
    std::vector<Prop> props {{"m_sText", std::nullopt, std::string("ESPIONAGE"), std::nullopt},
                             {"m_nIndex", 3, std::nullopt, std::nullopt}};
    auto book = fr::ReadClientReqBook(props);
    CHECK(book.slot == std::optional<int32_t>(3));
    CHECK(book.type == std::optional<std::string>("Espionage"));
    CHECK(book.how.find("m_nIndex") != std::string::npos);

    // A reference to an accessor beats an index; a crime sector int is a type, not a slot.
    props = {{"m_nCrimeSectorIndex", 5, std::nullopt, std::nullopt},
             {"m_Index", 6, std::nullopt, std::nullopt},
             {"m_rCampaign", std::nullopt, std::nullopt, 2}};
    book = fr::ReadClientReqBook(props);
    CHECK(book.slot == std::optional<int32_t>(2));
    CHECK(book.type == std::optional<std::string>("Psy Ops"));

    // A leader's index, an index out of range and a text that names no sector tell nothing.
    props = {{"m_nLeaderCodenameIndex", 2, std::nullopt, std::nullopt},
             {"m_Index", 8, std::nullopt, std::nullopt},
             {"m_Index2", -1, std::nullopt, std::nullopt},
             {"m_sTitle", std::nullopt, std::string("The Ninth Circle"), std::nullopt},
             {"m_nCrimeType", 11, std::nullopt, std::nullopt}};
    book = fr::ReadClientReqBook(props);
    CHECK_FALSE(book.slot);
    CHECK_FALSE(book.type);
    CHECK_FALSE(book.how.empty());

    // The accessor itself: a slot, no type.
    book = fr::ReadClientReqBook(std::vector<Prop> {{"m_Index", 0, std::nullopt, std::nullopt}});
    CHECK(book.slot == std::optional<int32_t>(0));
    CHECK_FALSE(book.type);
    CHECK(fr::ReadClientReqBook({}).how.find("not read") != std::string::npos);
}

TEST_CASE("client_reqs_preview_order") {
    const auto book = [](std::optional<int32_t> slot, std::optional<std::string> type) {
        return fr::ClientReqBook {slot, std::move(type), {}};
    };
    const auto types = [](const fr::ClientReqPreviewOrder& order) {
        std::vector<std::string> out;
        for (const auto& entry : order.entries)
            out.push_back(entry.type);
        return out;
    };
    std::vector<std::string> all;
    for (int32_t index = 0; index < 8; ++index)
        all.emplace_back(fr::CampaignTypeNameForIndex(index));
    std::sort(all.begin(), all.end());

    // Nothing read: all eight, alphabetically.
    auto order = fr::OrderClientReqPreview({});
    CHECK_FALSE(order.byBooks);
    CHECK(types(order) == all);
    CHECK_FALSE(order.why.empty());

    // Every book read: the books, in slot order (read in any order), and only them.
    std::vector<fr::ClientReqBook> books {book(2, "Sick Games"), book(0, "Big Pharma"), book(1, "Espionage"),
                                          book(std::nullopt, "Assassination")};
    order = fr::OrderClientReqPreview(books);
    CHECK(order.byBooks);
    CHECK(types(order) == std::vector<std::string> {"Big Pharma", "Espionage", "Sick Games"});
    CHECK(order.entries[0].slot == std::optional<int32_t>(0));
    CHECK(order.entries[2].slot == std::optional<int32_t>(2));
    CHECK(order.entries[1].label == "Espionage");

    // Two books of one sector: both listed (one pool).
    order = fr::OrderClientReqPreview(std::vector<fr::ClientReqBook> {book(1, "Espionage"), book(0, "Espionage")});
    CHECK(order.byBooks);
    CHECK(types(order) == std::vector<std::string> {"Espionage", "Espionage"});

    // A book without a type: the read ones first, then every other sector alphabetically.
    books = {book(1, "Sick Games"), book(0, std::nullopt), book(3, "Arms Trafficking")};
    order = fr::OrderClientReqPreview(books);
    CHECK_FALSE(order.byBooks);
    std::vector<std::string> expected {"Sick Games", "Arms Trafficking"}; // slots 1, 3
    for (const auto& name : all)
        if (name != "Arms Trafficking" && name != "Sick Games")
            expected.push_back(name);
    CHECK(types(order) == expected);
    CHECK_FALSE(order.entries[2].slot);
    CHECK(order.why.find("without a crime sector") != std::string::npos);

    // One slot read with two sectors: not trusted either.
    order = fr::OrderClientReqPreview(std::vector<fr::ClientReqBook> {book(0, "Eco Crime"), book(0, "Psy Ops")});
    CHECK_FALSE(order.byBooks);
    CHECK(types(order).size() == all.size());
    CHECK(types(order).front() == "Eco Crime");
}

TEST_CASE("client_reqs_book_preview_matches_applied") {
    const auto defs = SafehouseDefs();
    const auto sites = AllSites();
    const fr::ClientReqOptions options = CampaignMode(0);
    const std::vector<fr::ClientReqBook> books {{4, "Psy Ops", {}},
                                                {0, "Eco Crime", {}},
                                                {1, "Arms Trafficking", {}},
                                                {2, "Espionage", {}},
                                                {3, "Sick Games", {}}};
    for (int32_t seed = 1; seed <= 20; ++seed) {
        const auto previews = fr::PreviewClientReqCampaignTypes(seed, defs, options, books);
        REQUIRE(previews.size() == books.size());
        for (size_t i = 0; i < previews.size(); ++i) {
            CHECK(previews[i].slot == std::optional<int32_t>(static_cast<int32_t>(i)));
            // The same type as the full list previews: one pool per type, whatever the list.
            const auto alone = fr::PlanClientReqTypeCampaign(seed, defs, {}, options, previews[i].name);
            CHECK(previews[i].plan.pool == alone.pool);
            // The game's string in another spelling, or only the index: the same pool.
            const std::vector<std::string> before {"CPD_CrimeSectorName", "Assassination"};
            std::vector<std::string> after = before;
            std::string shouted;
            for (const char c : previews[i].name)
                shouted += c == ' ' ? '-' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            after.push_back(shouted);
            const auto byString = fr::ResolveClientReqCampaignType(before, after, std::nullopt);
            int32_t index = -1;
            for (int32_t type = 0; type < 8; ++type)
                if (fr::CampaignTypeNameForIndex(type) == previews[i].name)
                    index = type;
            const auto byIndex = fr::ResolveClientReqCampaignType(before, before, index);
            const auto byOdd =
                fr::ResolveClientReqCampaignType(before, std::vector<std::string> {"Assassination", "Xyz"}, index);
            for (const auto& choice : {byString, byOdd}) {
                REQUIRE(choice.name);
                CHECK(*choice.name == previews[i].name);
                CHECK(fr::PlanClientReqTypeCampaign(seed, defs, sites, options, choice.name).pool ==
                      previews[i].plan.pool);
            }
            // The same type as before and an index that disagrees: the string wins (Assassination).
            CHECK(byIndex.name == std::optional<std::string>("Assassination"));
        }
    }
}

TEST_CASE("client_reqs_unknown_type_uses_seed_only_pool") {
    const auto defs = PlainDefs(40);
    const auto sites = AllSites();
    const auto options = CampaignMode(10);
    for (int32_t seed = 1; seed <= 20; ++seed) {
        const auto unknown = fr::PlanClientReqTypeCampaign(seed, defs, sites, options, std::nullopt);
        CHECK(unknown.pool == fr::PlanClientReqCampaign(seed, defs, sites, options).pool);
    }
}

TEST_CASE("client_reqs_repair_detection") {
    const std::set<int32_t> excluded {3, 4, 18};
    CHECK_FALSE(fr::InvalidClientReq(0, excluded));
    CHECK_FALSE(fr::InvalidClientReq(41, excluded));
    CHECK(fr::InvalidClientReq(124, excluded));
    CHECK(fr::InvalidClientReq(4, excluded));
    CHECK(fr::InvalidClientReq(17, excluded));
    CHECK(fr::InvalidClientReq(-3, excluded));

    // Game data, a campaign as saved: Solitaire holds 124 (prestige).
    const std::vector<fr::ClientReqSite> sites {Site("MildChess", 15, {42, 37, 36}), Site("MildMeyer", 2, {42, 7, 59}),
                                                Site("MildSolitaire", 19, {41, 124, 20}),
                                                Site("MildBridge", 17, {4, 0, 38})};
    CHECK(fr::ClientReqSitesToRepair(sites, excluded) == std::vector<size_t> {2, 3});
    CHECK(fr::ClientReqSitesToRepair(sites, {}) == std::vector<size_t> {2});

    // The repair's plan for a site is the one the pick would have made for
    // it: a site's picks do not depend on the other sites.
    const auto defs = SafehouseDefs();
    fr::ClientReqOptions options = CampaignMode(0);
    options.excluded = excluded;
    const std::optional<std::string> type("Organ Trafficking");
    const std::vector<fr::ClientReqSite> repair {sites[2]};
    const auto repaired = fr::PlanClientReqTypeCampaign(-1927756609, defs, repair, options, type).plans.at(0);
    const auto whole = fr::PlanClientReqTypeCampaign(-1927756609, defs, sites, options, type).plans.at(2);
    CHECK(repaired.picks == whole.picks);
    CHECK_FALSE(repaired.fallback);
    const std::vector<fr::ClientReqSite> after {Site("MildSolitaire", 19, repaired.picks)};
    CHECK(fr::ClientReqSitesToRepair(after, excluded).empty());
}

TEST_CASE("client_reqs_check_due") {
    CHECK(fr::ClientReqCheckDue(true, -1927756609, false, ""));
    CHECK(fr::ClientReqCheckDue(true, -1927756609, false, "308605890"));
    CHECK_FALSE(fr::ClientReqCheckDue(true, -1927756609, false, " -1927756609 "));
    CHECK_FALSE(fr::ClientReqCheckDue(false, -1927756609, false, ""));
    CHECK_FALSE(fr::ClientReqCheckDue(true, std::nullopt, false, ""));
    CHECK_FALSE(fr::ClientReqCheckDue(true, 0, false, ""));
    CHECK_FALSE(fr::ClientReqCheckDue(true, -1927756609, true, "")); // a pick is awaited
}

TEST_CASE("client_reqs_mirror_slots") {
    // Game data, Solitaire's board values: the array
    // starts Req 08, Req 01, Req 02, so the slot comes from the id, not the
    // position.
    CHECK(fr::ClientReqMirrorSlot(0x6ad2bbe82fa1f0d4ull) == 0); // Solitaire Req 08
    CHECK(fr::ClientReqMirrorSlot(0xf1bbafb870a105c5ull) == 1); // Solitaire Req 01
    CHECK(fr::ClientReqMirrorSlot(0x844f8e0ee23abdc1ull) == 2); // Solitaire Req 02
    CHECK(fr::ClientReqMirrorSlot(0x1fb100ef5e82cc31ull) == 3); // Solitaire Req 03
    CHECK(fr::ClientReqMirrorSlot(0xceecb5f41d8235daull) == 1); // Chess Req 01
    CHECK(fr::ClientReqMirrorSlot(0x7eb63a78bfb339adull) == 2); // Chess Req 02
    CHECK(fr::ClientReqMirrorSlot(0xf065c24da4d2e747ull) == 3); // Chess Req 03
    CHECK(fr::ClientReqMirrorSlot(0x51524543b60c5426ull) == 0); // Solitaire Req 04
    CHECK(fr::ClientReqMirrorSlot(0) == 0);

    CHECK(fr::KnownClientReqCpdIntId(1) == 0x7ff3346576b348b3ull);
    CHECK(fr::KnownClientReqCpdIntId(2) == 0xf439b561579924b1ull);
    CHECK(fr::KnownClientReqCpdIntId(3) == 0);
    CHECK(fr::KnownClientReqCpdIntId(0) == 0);
    CHECK(fr::KnownClientReqCpdIntId(4) == 0);
}

TEST_CASE("client_reqs_slot_outliers") {
    using Ids = std::array<uint64_t, 3>;
    const Ids good {1, 2, 3};
    CHECK(fr::ClientReqSlotOutliers({good, good, good}).empty());
    CHECK(fr::ClientReqSlotOutliers({good, good, Ids {1, 9, 3}, good}) == std::set<size_t> {2});
    CHECK(fr::ClientReqSlotOutliers({good, Ids {1, 0, 3}, good}) == std::set<size_t> {1}); // no CPD int
    // A tie: nothing can be trusted for that slot.
    CHECK(fr::ClientReqSlotOutliers({good, Ids {1, 9, 3}}) == std::set<size_t> {0, 1});
    CHECK(fr::ClientReqSlotOutliers({}).empty());
}

TEST_CASE("client_reqs_settle_default") {
    // The game writes every location within ~10 ms; the picks must hold for
    // seconds, not half a second.
    CHECK(fr::kClientReqSettleMs >= 2'000);
    using Step = fr::ClientReqWatchStep;
    fr::ClientReqPickWatch watch;
    const std::vector<int32_t> burst {0, 10, 36, 0, 0, 0};
    const std::vector<int32_t> whole {10, 36, 20, 42, 37, 36};
    CHECK(watch.OnRead(burst, 0) == Step::Settling);
    CHECK(watch.OnRead(burst, 1'000) == Step::Settling);
    CHECK(watch.OnRead(whole, 1'500) == Step::Settling); // more arrive: the settle time restarts
    CHECK(watch.OnRead(whole, 1'500 + fr::kClientReqSettleMs - 1) == Step::Settling);
    CHECK(watch.OnRead(whole, 1'500 + fr::kClientReqSettleMs) == Step::Apply);
}

TEST_CASE("client_reqs_write_guard") {
    using Step = fr::ClientReqGuardStep;
    const std::vector<int32_t> written {41, 24, 63, 0, 0, 0};
    const std::vector<int32_t> moved {41, 24, 20, 0, 0, 0};
    const std::vector<int32_t> more {41, 24, 20, 42, 7, 59};
    fr::ClientReqWriteGuard guard(written, 2, 1'000);

    CHECK(guard.OnRead(written, 0) == Step::Holding);
    CHECK(guard.OnRead(written, 60'000) == Step::Holding);

    // The game writes again: settle, then rewrite.
    CHECK(guard.OnRead(moved, 61'000) == Step::Settling);
    CHECK(guard.OnRead(more, 61'500) == Step::Settling); // still changing: the settle time restarts
    CHECK(guard.OnRead(more, 62'499) == Step::Settling);
    CHECK(guard.OnRead(more, 62'500) == Step::Rewrite);
    const std::vector<int32_t> rewritten {41, 24, 63, 30, 12, 9};
    guard.Rewritten(rewritten);
    CHECK(guard.rewrites() == 1);
    CHECK(guard.expected() == rewritten);
    CHECK(guard.OnRead(rewritten, 63'000) == Step::Holding);

    // A change that goes back before it settles: holding, nothing rewritten.
    CHECK(guard.OnRead(more, 64'000) == Step::Settling);
    CHECK(guard.OnRead(rewritten, 64'500) == Step::Holding);
    CHECK(guard.OnRead(more, 65'000) == Step::Settling);
    CHECK(guard.OnRead(more, 65'999) == Step::Settling);

    // All slots cleared settles like any change.
    const std::vector<int32_t> cleared(6, 0);
    CHECK(guard.OnRead(cleared, 70'000) == Step::Settling);
    CHECK(guard.OnRead(cleared, 71'000) == Step::Rewrite);
    guard.Rewritten(cleared);
    CHECK(guard.rewrites() == 2);

    // After maxRewrites, a change is left alone.
    CHECK(guard.OnRead(cleared, 72'000) == Step::Holding);
    CHECK(guard.OnRead(more, 73'000) == Step::GaveUp);
    CHECK(guard.OnRead(more, 90'000) == Step::GaveUp);
}

TEST_CASE("client reqs: the never-pick guard keeps three to pick from") {
    CHECK(fr::ClientReqExcludeLeavesEnough({3, 4, 18}));
    CHECK(fr::ClientReqExcludeLeavesEnough({}));

    const auto ids = fr::ClientReqIds();
    std::set<int32_t> all(ids.begin(), ids.end());
    CHECK_FALSE(fr::ClientReqExcludeLeavesEnough(all));

    // Three left: enough.
    std::set<int32_t> three(all);
    three.erase(3);
    three.erase(4);
    three.erase(18);
    CHECK(fr::ClientReqExcludeLeavesEnough(three));

    // 02 Targets Only and 55 No Pacify never share a campaign: with a third, two count.
    std::set<int32_t> conflict(all);
    conflict.erase(2);
    conflict.erase(55);
    conflict.erase(3);
    CHECK_FALSE(fr::ClientReqExcludeLeavesEnough(conflict));
    conflict.erase(4);
    CHECK(fr::ClientReqExcludeLeavesEnough(conflict));
}
