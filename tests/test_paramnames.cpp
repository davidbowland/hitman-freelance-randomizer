#include "doctest.h"

#include "support/DialTestUtil.h"

#include "ParamNames.h"

#include <Glacier/Enums.h>

#include <string>
#include <string_view>

TEST_CASE("repo_parameter_exact_names") {
    const auto params = fr::test::AllParams();
    CHECK(params.size() == static_cast<size_t>(EDifficultyParameter::EDP_COUNT) - 1);

    for (const auto& [name, value] : params)
        CHECK(fr::ParamFromRepoName(name) == value);

    CHECK_FALSE(fr::ParamFromRepoName("").has_value());
    CHECK_FALSE(fr::ParamFromRepoName("EDP_NotAParameter").has_value());
}

TEST_CASE("repo_parameter_truncated_names_resolve") {
    // pro.repo cuts Parameter texts at 63 characters.
    constexpr struct {
        std::string_view name;
        EDifficultyParameter param;
    } kLong[] = {
        {"EDP_AICombat_ShootingAccuracyResetPeriod_Maximum_MediumCombatants",
         EDifficultyParameter::EDP_AICombat_ShootingAccuracyResetPeriod_Maximum_MediumCombatants},
        {"EDP_AICombat_Leaders_NumberOfActiveCombatantsNeededForTwoLeaders",
         EDifficultyParameter::EDP_AICombat_Leaders_NumberOfActiveCombatantsNeededForTwoLeaders},
        {"EDP_AIInvestigateCautious_Search_ClearDisturbanceRespectSoftReservationThreshold",
         EDifficultyParameter::EDP_AIInvestigateCautious_Search_ClearDisturbanceRespectSoftReservationThreshold},
    };

    for (const auto& s_Long : kLong) {
        REQUIRE(s_Long.name.size() > fr::kRepoParameterMaxChars);
        CHECK(fr::ParamFromRepoName(s_Long.name.substr(0, fr::kRepoParameterMaxChars)) ==
              static_cast<int32_t>(s_Long.param));
    }

    // Only a text of exactly 63 characters is taken as cut.
    const std::string_view s_Name = kLong[2].name;
    CHECK_FALSE(fr::ParamFromRepoName(s_Name.substr(0, fr::kRepoParameterMaxChars - 1)).has_value());
    CHECK_FALSE(fr::ParamFromRepoName(s_Name.substr(0, fr::kRepoParameterMaxChars + 1)).has_value());

    // A 63-character text no enumerator starts with.
    CHECK_FALSE(fr::ParamFromRepoName(std::string(fr::kRepoParameterMaxChars, 'X')).has_value());
}

TEST_CASE("repo_parameter_truncation_is_unambiguous") {
    // Every enumerator name longer than 63 characters is the only one starting
    // with its first 63 characters, and no shorter name equals that prefix.
    size_t s_Long = 0;

    for (const auto& [name, value] : fr::test::AllParams()) {
        if (name.size() <= fr::kRepoParameterMaxChars)
            continue;

        ++s_Long;
        const std::string_view s_Prefix = name.substr(0, fr::kRepoParameterMaxChars);
        size_t s_Starting = 0;

        for (const auto& [other, otherValue] : fr::test::AllParams())
            s_Starting += other.substr(0, s_Prefix.size()) == s_Prefix ? 1 : 0;

        CHECK_MESSAGE(s_Starting == 1, name);
    }

    CHECK(s_Long == 3);
}
