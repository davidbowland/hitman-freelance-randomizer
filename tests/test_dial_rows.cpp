#include "doctest.h"

#include "DialRows.h"
#include "Guid.h"
#include "ParamNames.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

fr::Guid G(const char* text) {
    return *fr::ParseGuid(text);
}

int32_t P(std::string_view name) {
    return *fr::ParamFromName(name);
}

const int32_t kGunShot = P("EDP_HearRangeGunShot");
const int32_t kAttention = P("EDP_AIAttentionGainMultiplier");
const int32_t kFootstepRun = P("EDP_HearRangeFootstepRun");
const int32_t kCamping = P("EDP_AICombat_FlashGrenadeThrow_TargetCampingTriggerCooldown");
const int32_t kRelocation = P("EDP_AICombat_FlashGrenadeThrow_LargeRelocationTriggerCooldown");

// A pro.repo entry of param that feeds a collection entity of the same parameter.
fr::DialEntry Entry(int32_t param, std::optional<uint8_t> tiers, double base, double multiplier,
                    const char* status = "written") {
    return {param, "pro.repo", tiers, param, base, multiplier, base * multiplier, status};
}

} // namespace

TEST_CASE("repo_entries") {
    // EDP_HearRangeGunShot: casual 35, professional 60, master 70.
    const auto casual = fr::FindRepoEntry(G("03a4296f-977f-4418-83d9-18746659ff4b"));
    REQUIRE(casual);
    CHECK(casual->tiers == fr::kCasual);
    CHECK(casual->param == kGunShot);
    CHECK(casual->feeds == kGunShot);
    CHECK(casual->value == 35);

    const auto pro = fr::FindRepoEntry(G("417cb806-d136-49e8-ba7e-cb6d98b30f01"));
    REQUIRE(pro);
    CHECK(pro->tiers == fr::kProfessional);
    CHECK(pro->value == 60);

    const auto master = fr::FindRepoEntry(G("2C068A58-7DBE-40F6-943A-1AEF5B80E3E7"));
    REQUIRE(master);
    CHECK(master->tiers == fr::kMaster);

    // EDP_PlayerHitPointsRegenerationDelay 5: in both the professional and master collections.
    CHECK(fr::FindRepoEntry(G("9eb0fdc6-6742-4c66-af4a-c8d203eec85c"))->tiers == (fr::kProfessional | fr::kMaster));

    // EDP_HearRangeCloseCombatHitFromBehind: a census entry in no collection.
    const auto none = fr::FindRepoEntry(G("03971149-3aba-40d8-ad06-d07c7ad957db"));
    REQUIRE(none);
    CHECK(none->tiers == 0);
    CHECK_FALSE(none->feeds);

    // Named for the camping cooldown, but feeds the relocation cooldown's entity.
    const auto odd = fr::FindRepoEntry(G("88b947d8-e7f7-417a-b204-599e294c7896"));
    REQUIRE(odd);
    CHECK(odd->param == kCamping);
    CHECK(odd->feeds == kRelocation);

    CHECK_FALSE(fr::FindRepoEntry(fr::Guid {}));
    CHECK_FALSE(fr::FindRepoEntry(G("00000000-0000-0000-0000-000000000001")));
}

TEST_CASE("professional_repo_values") {
    CHECK(fr::ProfessionalRepoValues(kGunShot) == std::vector<double> {60});
    CHECK(fr::ProfessionalRepoValues(kFootstepRun) == std::vector<double> {0});
    CHECK(fr::ProfessionalRepoValues(kAttention).empty());
    CHECK(fr::ProfessionalRepoValues(kCamping).size() == 2);
}

TEST_CASE("repo_tier_labels") {
    CHECK(fr::RepoTierLabel(0) == "other");
    CHECK(fr::RepoTierLabel(fr::kCasual) == "casual");
    CHECK(fr::RepoTierLabel(fr::kProfessional) == "professional");
    CHECK(fr::RepoTierLabel(fr::kMaster) == "master");
    CHECK(fr::RepoTierLabel(fr::kProfessional | fr::kMaster) == "professional+master");
    CHECK(fr::RepoTierLabel(fr::kCasual | fr::kProfessional | fr::kMaster) == "casual+professional+master");
}

TEST_CASE("summarize_dials_picks_professional") {
    const std::vector<fr::DialEntry> entries {
        Entry(kGunShot, fr::kMaster, 70, 2.0, "held (5 s)"),
        Entry(kGunShot, fr::kCasual, 35, 2.0, "held (5 s)"),
        Entry(kGunShot, fr::kProfessional, 60, 2.0, "changed (now 1) (5 s)"),
    };
    const auto out = fr::SummarizeDials(entries);
    REQUIRE(out.size() == 1);
    CHECK(out[0].param == kGunShot);
    CHECK(out[0].freelancer == fr::FreelancerValue::Written);
    CHECK_FALSE(fr::NoEffect(out[0]));
    CHECK(out[0].professionalEntries == 1);
    CHECK(out[0].multiplier == 2.0);
    CHECK_FALSE(out[0].multipliersDiffer);
    CHECK(out[0].before == std::optional<double>(60));
    CHECK(out[0].after == std::optional<double>(120));
    CHECK(out[0].status == "changed (now 1) (5 s)");
    REQUIRE(out[0].entries.size() == 3);
    CHECK(out[0].entries[0].base == 70);
    CHECK(out[0].entries[2].base == 60);
    CHECK(fr::FreelancerValueText(out[0], false) == "60 -> 120");
    CHECK(fr::FreelancerValueText(out[0], true) == "60 -> 120 m");
}

TEST_CASE("summarize_dials_several_professional") {
    SUBCASE("the one feeding this parameter wins") {
        // pro.repo's two professional camping-cooldown entries: one feeds the relocation cooldown.
        std::vector<fr::DialEntry> entries {Entry(kCamping, fr::kProfessional, 8, 1.5),
                                            Entry(kCamping, fr::kProfessional, 10, 1.5)};
        entries[0].feeds = kRelocation;
        const auto out = fr::SummarizeDials(entries);
        REQUIRE(out.size() == 1);
        CHECK(out[0].freelancer == fr::FreelancerValue::Written);
        CHECK(out[0].professionalEntries == 2);
        CHECK(out[0].before == std::optional<double>(10));
    }
    SUBCASE("ambiguous") {
        const std::vector<fr::DialEntry> entries {Entry(kGunShot, fr::kProfessional | fr::kMaster, 5, 1.5),
                                                  Entry(kGunShot, fr::kProfessional, 4, 1.5)};
        const auto out = fr::SummarizeDials(entries);
        REQUIRE(out.size() == 1);
        CHECK(out[0].freelancer == fr::FreelancerValue::Ambiguous);
        CHECK_FALSE(fr::NoEffect(out[0]));
        CHECK_FALSE(out[0].before);
        CHECK(fr::FreelancerValueText(out[0], false) == "2 Professional copies");
    }
}

TEST_CASE("summarize_dials_without_professional") {
    SUBCASE("missing, shared status") {
        const std::vector<fr::DialEntry> entries {
            Entry(kAttention, fr::kMaster, 0.6, 1.2),
            Entry(kAttention, fr::kCasual, 0.4, 1.2),
        };
        const auto out = fr::SummarizeDials(entries);
        REQUIRE(out.size() == 1);
        CHECK(out[0].freelancer == fr::FreelancerValue::Missing);
        CHECK(fr::NoEffect(out[0]));
        CHECK_FALSE(out[0].before);
        CHECK_FALSE(out[0].after);
        CHECK(out[0].multiplier == 1.2);
        CHECK(out[0].status == "written");
        CHECK(fr::FreelancerValueText(out[0], false) == "no professional entry");
    }
    SUBCASE("mixed status") {
        const std::vector<fr::DialEntry> entries {
            Entry(kAttention, fr::kMaster, 0.6, 1.2, "held (5 s)"),
            Entry(kAttention, 0, 0.4, 1.2, "unreadable (5 s)"),
        };
        CHECK(fr::SummarizeDials(entries)[0].status == "mixed");
    }
    SUBCASE("professional entry is 0") {
        // EDP_HearRangeFootstepRun: casual 0, professional 0, master 6; only master is written.
        const std::vector<fr::DialEntry> entries {Entry(kFootstepRun, fr::kMaster, 6, 2.0)};
        const auto out = fr::SummarizeDials(entries);
        CHECK(out[0].freelancer == fr::FreelancerValue::Zero);
        CHECK(fr::NoEffect(out[0]));
        CHECK(fr::FreelancerValueText(out[0], true) == "Professional value is 0 (unchanged)");
    }
    SUBCASE("tier unknown") {
        std::vector<fr::DialEntry> entries {Entry(kAttention, fr::kMaster, 0.6, 0.5)};
        entries.push_back({kAttention, "entity", std::nullopt, kAttention, 0.5, 0.5, 0.25, "held (5 s)"});
        const auto out = fr::SummarizeDials(entries);
        CHECK(out[0].freelancer == fr::FreelancerValue::TierUnknown);
        CHECK_FALSE(fr::NoEffect(out[0]));
        CHECK(out[0].before == std::optional<double>(0.5));
        CHECK(out[0].after == std::optional<double>(0.25));
        CHECK(fr::FreelancerValueText(out[0], false) == "x0.5 -> x0.25 (difficulty unknown)");
    }
}

TEST_CASE("summarize_dials_mixed_multipliers") {
    const std::vector<fr::DialEntry> entries {
        Entry(kGunShot, fr::kCasual, 35, 1.1),
        Entry(kGunShot, fr::kProfessional, 60, 1.3),
    };
    const auto out = fr::SummarizeDials(entries);
    REQUIRE(out.size() == 1);
    CHECK(out[0].multipliersDiffer);
    CHECK(out[0].multiplier == 1.3);

    const std::vector<fr::DialEntry> noPro {Entry(kAttention, fr::kCasual, 35, 1.1), Entry(kAttention, 0, 60, 1.3)};
    CHECK(fr::SummarizeDials(noPro)[0].multiplier == 1.1);
}

TEST_CASE("summarize_dials_order_and_entities") {
    std::vector<fr::DialEntry> entries {
        Entry(kGunShot, fr::kCasual, 35, 2.0),
        Entry(kAttention, fr::kMaster, 0.6, 0.5),
        Entry(kGunShot, fr::kProfessional, 60, 2.0),
    };
    entries.push_back({kAttention, "entity", fr::kProfessional, kAttention, 0.5, 0.5, 0.25, "held (5 s)"});

    const auto out = fr::SummarizeDials(entries);
    REQUIRE(out.size() == 2);
    CHECK(out[0].param == kGunShot);
    CHECK(out[0].entries.size() == 2);
    CHECK(out[1].param == kAttention);
    REQUIRE(out[1].entries.size() == 2);
    CHECK(out[1].freelancer == fr::FreelancerValue::Written);
    CHECK(out[1].before == std::optional<double>(0.5));
    CHECK(out[1].after == std::optional<double>(0.25));
    CHECK(out[1].status == "held (5 s)");

    CHECK(fr::SummarizeDials(std::vector<fr::DialEntry> {}).empty());
}

TEST_CASE("summarize_dials_uncollected_entity") {
    const int32_t kRadius = P("EDP_AIInvestigateCautious_Disturbance_Radius");
    const int32_t kFollow = P("EDP_AIDisguise_FollowWarningsDelay");

    SUBCASE("one uncollected entity: its value, not (no effect?)") {
        // pro.repo's only entry (other tier, 12) is written, then the uncollected entity (15).
        std::vector<fr::DialEntry> entries {Entry(kRadius, 0, 12, 1.5)};
        entries.push_back({kRadius, "uncollected entity", std::nullopt, kRadius, 15, 1.5, 22.5, "held (5 s)", true});
        const auto out = fr::SummarizeDials(entries);
        REQUIRE(out.size() == 1);
        CHECK(out[0].freelancer == fr::FreelancerValue::Uncollected);
        CHECK_FALSE(fr::NoEffect(out[0]));
        CHECK(out[0].uncollectedEntries == 1);
        CHECK(out[0].before == std::optional<double>(15));
        CHECK(out[0].after == std::optional<double>(22.5));
        CHECK(out[0].status == "held (5 s)");
        CHECK(fr::FreelancerValueText(out[0], false) == "15 -> 22.5 (level copy, may have no effect)");
        CHECK(fr::FreelancerValueText(out[0], true) == "15 -> 22.5 m (level copy, may have no effect)");
    }
    SUBCASE("several uncollected entities: every value") {
        std::vector<fr::DialEntry> entries;
        entries.push_back({kFollow, "uncollected entity", std::nullopt, kFollow, 7, 0.5, 3.5, "written", true});
        entries.push_back({kFollow, "uncollected entity", std::nullopt, kFollow, 6, 0.5, 3, "written", true});
        const auto out = fr::SummarizeDials(entries);
        CHECK(out[0].freelancer == fr::FreelancerValue::Uncollected);
        CHECK(out[0].uncollectedEntries == 2);
        CHECK(out[0].before == std::optional<double>(7));
        CHECK(fr::FreelancerValueText(out[0], false) == "7 -> 3.5, 6 -> 3 (2 level copies, may have no effect)");
        CHECK(fr::FreelancerValueText(out[0], true) == "7 -> 3.5, 6 -> 3 s (2 level copies, may have no effect)");
    }
    SUBCASE("a professional entry still wins") {
        std::vector<fr::DialEntry> entries {Entry(kGunShot, fr::kProfessional, 60, 2.0)};
        entries.push_back({kGunShot, "uncollected entity", std::nullopt, kGunShot, 50, 2.0, 100, "written", true});
        const auto out = fr::SummarizeDials(entries);
        CHECK(out[0].freelancer == fr::FreelancerValue::Written);
        CHECK(out[0].before == std::optional<double>(60));
    }
    SUBCASE("an uncollected entity is not 'tier unknown'") {
        std::vector<fr::DialEntry> entries;
        entries.push_back({kFollow, "uncollected entity", std::nullopt, kFollow, 6, 2.0, 12, "written", true});
        CHECK(fr::SummarizeDials(entries)[0].freelancer == fr::FreelancerValue::Uncollected);
    }
}

TEST_CASE("view_dial_row: rows a held sensor dial covers") {
    // EDP_SightInvestigationMinAttention: casual and master entries only.
    const int32_t kInvestigate = P("EDP_SightInvestigationMinAttention");
    // EDP_AIInvestigateCautious_Disturbance_Radius: one entry in no collection.
    const int32_t kRadius = P("EDP_AIInvestigateCautious_Disturbance_Radius");
    const std::vector<fr::DialEntry> entries {
        Entry(kInvestigate, fr::kMaster, 0.5, 0.5),
        Entry(kInvestigate, fr::kCasual, 0.7, 0.5),
        Entry(kRadius, 0, 12, 0.4),
        Entry(kGunShot, fr::kProfessional, 60, 2.0),
    };
    const auto out = fr::SummarizeDials(entries);
    REQUIRE(out.size() == 3);

    SUBCASE("no sensor dial held: no professional entry is marked") {
        CHECK(fr::ViewDialRow(out[0], {}).shown);
        CHECK(fr::ViewDialRow(out[0], {}).noEffect);
        CHECK(fr::ViewDialRow(out[1], {}).noEffect);
        CHECK(fr::ViewDialRow(out[2], {}).shown);
        CHECK_FALSE(fr::ViewDialRow(out[2], {}).noEffect);
    }
    SUBCASE("the sight threshold's sensor dial held: its AI row gives way, the rest stay") {
        const std::vector<int32_t> covered {kInvestigate};
        CHECK_FALSE(fr::ViewDialRow(out[0], covered).shown);
        CHECK_FALSE(fr::ViewDialRow(out[0], covered).noEffect);
        // Nothing stands in for the investigation radius: still no effect.
        CHECK(fr::ViewDialRow(out[1], covered).shown);
        CHECK(fr::ViewDialRow(out[1], covered).noEffect);
        // A written professional entry is shown whatever is covered.
        const std::vector<int32_t> gunshot {kGunShot};
        CHECK(fr::ViewDialRow(out[2], gunshot).shown);
        CHECK_FALSE(fr::ViewDialRow(out[2], gunshot).noEffect);
    }
}
