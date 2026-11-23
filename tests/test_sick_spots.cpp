#include "doctest.h"

#include "Config.h"
#include "SettingsTable.h"
#include "SickSpots.h"

#include <limits>
#include <string>
#include <vector>

namespace {

fr::KillGuide Guide(float x, float y, float z, std::optional<int32_t> type) {
    fr::KillGuide g;
    g.at = fr::SpotPoint {x, y, z};
    g.type = type;
    return g;
}

} // namespace

TEST_CASE("sick spots: the two templates and the toilet core by TEMP or TBLU") {
    CHECK(fr::SickTemplateOf(0x0098C7BD51E1C742ull) == fr::SickTemplate::Normal);
    CHECK(fr::SickTemplateOf(0x00CD208F7E4A992Full) == fr::SickTemplate::Normal);
    CHECK(fr::SickTemplateOf(0x0044692C463B261Eull) == fr::SickTemplate::High);
    CHECK(fr::SickTemplateOf(0x00DC3CECBB213D88ull) == fr::SickTemplate::High);
    CHECK(fr::SickTemplateOf(0) == fr::SickTemplate::None);
    CHECK(fr::SickTemplateOf(0x00E59DEEA28D48E8ull) == fr::SickTemplate::None);

    CHECK(fr::IsToiletCore(0x00E59DEEA28D48E8ull));
    CHECK(fr::IsToiletCore(0x004973FB1EE4481Eull));
    CHECK_FALSE(fr::IsToiletCore(0));
    CHECK_FALSE(fr::IsToiletCore(0x0098C7BD51E1C742ull));

    CHECK(std::string(fr::SickTemplateName(fr::SickTemplate::Normal)) == "normal");
    CHECK(std::string(fr::SickTemplateName(fr::SickTemplate::High)) == "high");
}

TEST_CASE("sick spots: drown, push and snap-and-toss guides count; the rest do not") {
    CHECK(fr::KillGuideCounts(0));
    CHECK(fr::KillGuideCounts(1));
    CHECK(fr::KillGuideCounts(2));
    CHECK_FALSE(fr::KillGuideCounts(3));
    CHECK_FALSE(fr::KillGuideCounts(4));
    CHECK_FALSE(fr::KillGuideCounts(5));
    CHECK_FALSE(fr::KillGuideCounts(-1));
    // Still a ZContextKillGuide: an unread type counts.
    CHECK(fr::KillGuideCounts(std::nullopt));

    CHECK(fr::KillGuideTypeName(0) == "toilet drown");
    CHECK(fr::KillGuideTypeName(1) == "snap and toss");
    CHECK(fr::KillGuideTypeName(2) == "push");
    CHECK(fr::KillGuideTypeName(4) == "interrupt animation");
    CHECK(fr::KillGuideTypeName(9) == "type 9");
    CHECK(fr::KillGuideTypeName(std::nullopt) == "type unread");
}

TEST_CASE("sick spots: a toilet core spot is a toilet, else the nearest counting guide decides") {
    const std::vector<fr::KillGuide> guides {
        Guide(10.0f, 0.0f, 0.0f, 2), // push, 10 m from the origin spot
        Guide(1.5f, 0.0f, 0.0f, 3),  // no animation: never counts, though nearest
        Guide(0.0f, 1.9f, 0.0f, 1),  // snap and toss, 1.9 m
        Guide(0.0f, 0.0f, 0.5f, std::nullopt),
    };
    const fr::SpotPoint at {0.0f, 0.0f, 3.0f};

    // In a toilet core: a toilet, whatever is near (even with no position).
    CHECK(fr::ClassifySickSpot(true, std::nullopt, guides).kind == fr::SickSpotKind::Toilet);
    CHECK(fr::ClassifySickSpot(true, at, {}).kind == fr::SickSpotKind::Toilet);

    // No position, not in a toilet core: unknown.
    CHECK(fr::ClassifySickSpot(false, std::nullopt, guides).kind == fr::SickSpotKind::Unknown);

    // The snap-and-toss guide at 1.9 m is within 2 m; the nearer no-animation one is skipped.
    const fr::SickVerdict near = fr::ClassifySickSpot(false, fr::SpotPoint {0.0f, 0.0f, 0.0f}, guides);
    CHECK(near.kind == fr::SickSpotKind::KillGuide);
    REQUIRE(near.nearest.has_value());
    CHECK(*near.nearest == 3); // the unread-type guide at 0.5 m
    CHECK(*near.distance == doctest::Approx(0.5));

    // Only guides beyond the radius: plain, the nearest still reported.
    const std::vector<fr::KillGuide> far {Guide(10.0f, 0.0f, 0.0f, 2), Guide(0.0f, 2.5f, 0.0f, 0)};
    const fr::SickVerdict plain = fr::ClassifySickSpot(false, fr::SpotPoint {0.0f, 0.0f, 0.0f}, far);
    CHECK(plain.kind == fr::SickSpotKind::Plain);
    REQUIRE(plain.nearest.has_value());
    CHECK(*plain.nearest == 1);
    CHECK(*plain.distance == doctest::Approx(2.5));

    // The radius is inclusive.
    CHECK(fr::ClassifySickSpot(false, fr::SpotPoint {0.0f, 0.0f, 0.0f}, far, 2.5).kind == fr::SickSpotKind::KillGuide);

    // No guides at all: plain, nothing nearest. A guide without a position is skipped.
    fr::KillGuide unplaced;
    unplaced.type = 0;
    const std::vector<fr::KillGuide> none {unplaced};
    const fr::SickVerdict lone = fr::ClassifySickSpot(false, fr::SpotPoint {1.0f, 1.0f, 1.0f}, none);
    CHECK(lone.kind == fr::SickSpotKind::Plain);
    CHECK_FALSE(lone.nearest.has_value());

    // Ties: the lower index.
    const std::vector<fr::KillGuide> tie {Guide(1.0f, 0.0f, 0.0f, 0), Guide(-1.0f, 0.0f, 0.0f, 2)};
    CHECK(*fr::ClassifySickSpot(false, fr::SpotPoint {0.0f, 0.0f, 0.0f}, tie).nearest == 0);

    // The default radius.
    CHECK(fr::kSickKillGuideRadius == 2.0);
}

TEST_CASE("sick spots: toilets and push spots are kill spots alike") {
    CHECK(fr::IsKillSpot(fr::SickSpotKind::Toilet));
    CHECK(fr::IsKillSpot(fr::SickSpotKind::KillGuide));
    CHECK_FALSE(fr::IsKillSpot(fr::SickSpotKind::Plain));
    CHECK_FALSE(fr::IsKillSpot(fr::SickSpotKind::Unknown));

    CHECK(std::string(fr::SickSpotKindName(fr::SickSpotKind::Toilet)) == "toilet (drown)");
    CHECK(std::string(fr::SickSpotKindName(fr::SickSpotKind::KillGuide)) == "kill spot");
    CHECK(std::string(fr::SickSpotKindName(fr::SickSpotKind::Plain)) == "plain");
}

TEST_CASE("sick spots: the boost is clamped and only ever raises") {
    CHECK(fr::ClampSickBoost(-5) == 0);
    CHECK(fr::ClampSickBoost(1000) == 1000);
    CHECK(fr::ClampSickBoost(999'999) == fr::kSickBoostMax);

    CHECK(fr::BoostedSickPriority(2000, 1000) == 3000);
    CHECK(fr::BoostedSickPriority(2000, 0) == 2000);
    CHECK(fr::BoostedSickPriority(2000, -300) == 2000);
    CHECK(fr::BoostedSickPriority(2000, 999'999) == 2000 + fr::kSickBoostMax);
    CHECK(fr::BoostedSickPriority(std::numeric_limits<int32_t>::max() - 10, 1000) ==
          std::numeric_limits<int32_t>::max());

    // A raised spot (game data: m_nPriority 2000) stays above GetSick's own situation (1990).
    CHECK(fr::BoostedSickPriority(2000, fr::kSickBoostDefault) > 1990);
}

TEST_CASE("sick spots: a restart that keeps a raised priority does not raise it again") {
    // First raise: nothing recorded.
    CHECK(fr::SickPriorityToWrite(2000, std::nullopt, 1000) == 3000);
    // The same entity still holds what was written: boosted from the original again, not stacked.
    CHECK(fr::SickPriorityToWrite(3000, fr::SickRaise {2000, 3000}, 1000) == 3000);
    // A changed boost applies to the original.
    CHECK(fr::SickPriorityToWrite(3000, fr::SickRaise {2000, 3000}, 500) == 2500);
    // The value changed since (the game, or a new entity at the same address): boosted from it.
    CHECK(fr::SickPriorityToWrite(2000, fr::SickRaise {2000, 3000}, 1000) == 3000);
    CHECK(fr::SickPriorityToWrite(1500, fr::SickRaise {2000, 3000}, 1000) == 2500);
}

TEST_CASE("sick spots: turning the boost off puts a kept raise back on a restart") {
    CHECK(fr::SickPriorityToRestore(3000, fr::SickRaise {2000, 3000}) == std::optional<int32_t>(2000));
    // Nothing recorded, or the value changed since: left alone.
    CHECK_FALSE(fr::SickPriorityToRestore(2000, std::nullopt));
    CHECK_FALSE(fr::SickPriorityToRestore(2500, fr::SickRaise {2000, 3000}));
}

TEST_CASE("sick spots: nearest kill spot and nearest plain spot to an actor") {
    const std::vector<fr::SickSpotPlace> spots {
        {fr::SpotPoint {10.0f, 0.0f, 0.0f}, fr::SickSpotKind::Toilet},
        {fr::SpotPoint {2.0f, 0.0f, 0.0f}, fr::SickSpotKind::Plain},
        {std::nullopt, fr::SickSpotKind::KillGuide}, // no position: skipped
        {fr::SpotPoint {0.0f, 6.0f, 0.0f}, fr::SickSpotKind::KillGuide},
        {fr::SpotPoint {0.0f, 0.0f, 1.0f}, fr::SickSpotKind::Unknown}, // counts with the plain ones
    };

    const fr::SickNearest near = fr::NearestSickSpots(spots, fr::SpotPoint {0.0f, 0.0f, 0.0f});
    REQUIRE(near.kill.has_value());
    CHECK(*near.kill == 3);
    CHECK(*near.killDistance == doctest::Approx(6.0));
    REQUIRE(near.plain.has_value());
    CHECK(*near.plain == 4);
    CHECK(*near.plainDistance == doctest::Approx(1.0));

    const fr::SickNearest empty = fr::NearestSickSpots({}, fr::SpotPoint {});
    CHECK_FALSE(empty.kill.has_value());
    CHECK_FALSE(empty.plain.has_value());
}

TEST_CASE("sick spots: the summary line") {
    fr::SickTally t;
    t.spots = 41;
    t.normal = 33;
    t.high = 8;
    t.toilet = 6;
    t.killGuide = 3;
    t.plain = 30;
    t.unknown = 2;
    t.guides = 57;
    t.guidesCounting = 40;
    t.raised = 9;

    CHECK(fr::SickSummaryText(t, 1000, true) ==
          "41 sick spots (33 normal, 8 high): 6 toilets, 3 near a kill guide, 30 plain, 2 unknown; 57 kill guides "
          "(40 of a kill type); priority raised by 1000 on 9 of 9 normal kill spots");

    t.notRaised = 2;
    t.highKill = 1;
    t.unknown = 0;
    CHECK(fr::SickSummaryText(t, 1000, true) ==
          "41 sick spots (33 normal, 8 high): 6 toilets, 3 near a kill guide, 30 plain; 57 kill guides (40 of a kill "
          "type); priority raised by 1000 on 9 of 11 normal kill spots (not raised on 2: see the spot lines), 1 high "
          "kill spot left at its priority (the high template has none to raise)");

    t.notRaised = 0;
    t.highKill = 0;
    CHECK(fr::SickSummaryText(t, 1000, false).find("priority left alone (sick_spot_kills off)") != std::string::npos);
    CHECK(fr::SickSummaryText(t, 0, true).find("priority left alone (sick_spot_kill_boost 0)") != std::string::npos);
}

TEST_CASE("sick spots: the settings") {
    const fr::SettingInfo* on = fr::FindSetting("sick_spot_kills");
    const fr::SettingInfo* boost = fr::FindSetting("sick_spot_kill_boost");
    REQUIRE(on != nullptr);
    REQUIRE(boost != nullptr);

    // Shipped on.
    CHECK(on->type == fr::SettingType::Bool);
    CHECK(on->def == "true");
    CHECK(fr::Config {}.sickSpotKills);

    CHECK(boost->type == fr::SettingType::Int);
    CHECK(boost->parent == "sick_spot_kills");
    CHECK(boost->def == std::to_string(fr::kSickBoostDefault));
    CHECK(fr::Config {}.sickSpotKillBoost == fr::kSickBoostDefault);
    CHECK(boost->min == 0);
    CHECK(boost->max == fr::kSickBoostMax);

    // Guards & AI, their own group.
    CHECK(on->group == fr::SettingGroup::WeaponsMisc);
    CHECK(boost->group == fr::SettingGroup::WeaponsMisc);
    CHECK(fr::SettingSectionOf(fr::SettingGroup::WeaponsMisc) == fr::SettingSection::WeaponsGear);
    CHECK(fr::SettingGroupName(fr::SettingGroup::WeaponsMisc) == "Miscellaneous");

    // Plain words for players: no engine jargon.
    for (const fr::SettingInfo* info : {on, boost}) {
        CAPTURE(info->key);

        for (const char* word : {"priority", "drama", "spot", "template", "guide", "ContextKill"}) {
            CAPTURE(word);
            CHECK(info->help.find(word) == std::string_view::npos);
            CHECK(info->label.find(word) == std::string_view::npos);
        }
    }
}
