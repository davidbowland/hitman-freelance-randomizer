#include "doctest.h"

#include "CrateLamps.h"
#include "LandslideFull.h"
#include "Variants.h"

#include <limits>
#include <set>
#include <string>
#include <vector>

TEST_CASE("crate lamps: night time of day bricks by name") {
    CHECK(fr::IsNightTimeOfDayBrick("tod_summer_night_clear"));
    CHECK(fr::IsNightTimeOfDayBrick("tod_ebola_night"));
    CHECK(fr::IsNightTimeOfDayBrick("tod_zika_night"));
    CHECK_FALSE(fr::IsNightTimeOfDayBrick("tod_summer_morning_clear"));
    CHECK_FALSE(fr::IsNightTimeOfDayBrick("tod_flu"));
    CHECK_FALSE(fr::IsNightTimeOfDayBrick("lighting_copperhead"));
    CHECK_FALSE(fr::IsNightTimeOfDayBrick("lighting_day"));
    // Not a time of day brick, whatever its name says.
    CHECK_FALSE(fr::IsNightTimeOfDayBrick("scenario_night_market"));
}

TEST_CASE("crate lamps: the night full versions are the Icon, the Bad Boy, the Author and the Entertainer") {
    std::set<std::string> s_Night;

    for (const fr::FullVariant* s_Full : fr::FullVariants()) {
        const fr::NightVerdict s_Verdict = fr::JudgeNightLoad(s_Full->key, true);
        CHECK_FALSE(s_Verdict.why.empty());

        if (s_Verdict.night)
            s_Night.insert(std::string(s_Full->key));
    }

    CHECK(s_Night == std::set<std::string> {"icon", "badboy", "author", "entertainer"});
    // The full Marrakesh night is only the Entertainer's base, never a load's own (no option of its own).
    CHECK_FALSE(fr::JudgeNightLoad("marrakesh", true).night);
    CHECK(fr::JudgeNightLoad("entertainer", true).why == "the full Entertainer loads tod_summer_night_clear");

    const fr::NightVerdict s_BadBoy = fr::JudgeNightLoad("badboy", true);
    CHECK(s_BadBoy.night);
    CHECK(s_BadBoy.why == "the full Bad Boy loads tod_summer_night_clear");
    CHECK(fr::JudgeNightLoad("author", true).why == "the full Author loads tod_ebola_night");
    CHECK_FALSE(fr::JudgeNightLoad("landslide", true).night);   // morning
    CHECK_FALSE(fr::JudgeNightLoad("patientzero", true).night); // tod_flu
    CHECK_FALSE(fr::JudgeNightLoad("gluttony", true).night);    // no time of day
}

TEST_CASE("crate lamps: appended options are judged by their own bricks") {
    // The Source appends tod_zika_night on Bangkok.
    CHECK(fr::JudgeNightLoad("the_source", false).night);
    CHECK(fr::JudgeNightLoad("the_source", false).why == "map variant the_source loads tod_zika_night");
    // Night and The Icon's lighting, were they appended.
    CHECK(fr::JudgeNightLoad("tod_ebola_night", false).night);
    CHECK(fr::JudgeNightLoad("lighting_copperhead", false).night);
    CHECK_FALSE(fr::JudgeNightLoad("holiday_hoarders", false).night);
    CHECK_FALSE(fr::JudgeNightLoad("the_mercenary", false).night);
    CHECK_FALSE(fr::JudgeNightLoad("landslide", false).night);

    // Only the options with a night time of day brick.
    std::set<std::string> s_Night;

    for (const fr::VariantOption* s_Option : fr::AllVariantOptions()) {
        if (fr::JudgeNightLoad(s_Option->name, false).night)
            s_Night.insert(std::string(s_Option->name));
    }

    CHECK(s_Night == std::set<std::string> {"the_source", "tod_ebola_night", "lighting_copperhead", "the_bad_boy",
                                            "the_entertainer"});
}

TEST_CASE("crate lamps: no variant, an unknown key or name is day") {
    CHECK_FALSE(fr::JudgeNightLoad("", false).night);
    CHECK_FALSE(fr::JudgeNightLoad("", true).night);
    CHECK_FALSE(fr::JudgeNightLoad("nosuch", true).night);
    CHECK_FALSE(fr::JudgeNightLoad("nosuch", false).night);
    // A full version's key is not an option's name.
    CHECK_FALSE(fr::JudgeNightLoad("badboy", false).night);
}

TEST_CASE("crate lamps: the template's entities") {
    const auto s_Entities = fr::CrateLampEntities();
    REQUIRE(s_Entities.size() == fr::kCrateLampEntityCount);
    CHECK(s_Entities[0].id == 0x834100cee5e680edull);
    CHECK(s_Entities[0].part == fr::CrateLampPart::LampCore);
    CHECK(s_Entities[0].property == "DiffusePower");
    CHECK(s_Entities[1].id == 0xb667d37a9d333f1full);
    CHECK(s_Entities[1].part == fr::CrateLampPart::Center);
    CHECK(s_Entities[2].id == 0x623b6598fcbc59d9ull);
    CHECK(s_Entities[2].part == fr::CrateLampPart::Environment);

    size_t s_Sides = 0;
    std::set<uint64_t> s_Ids;

    for (const fr::CrateLampEntity& s_Entity : s_Entities) {
        s_Ids.insert(s_Entity.id);
        s_Sides += s_Entity.part == fr::CrateLampPart::Side ? 1 : 0;

        if (s_Entity.part != fr::CrateLampPart::LampCore)
            CHECK(s_Entity.property == "m_fDiffusePower");
    }

    CHECK(s_Sides == fr::kCrateSideLightCount);
    CHECK(s_Ids.size() == s_Entities.size());
}

TEST_CASE("crate lamps: values are lowered to the cap, never raised") {
    using P = fr::CrateLampPart;
    // The side lights get what the lamp core sends.
    CHECK(fr::CrateLampNightCap(P::Side) == fr::CrateLampNightCap(P::LampCore));

    CHECK(fr::CrateLampNightValue(P::LampCore, 200.0f) == fr::CrateLampNightCap(P::LampCore));
    CHECK(fr::CrateLampNightValue(P::Center, 7000.0f) == fr::CrateLampNightCap(P::Center));
    CHECK(fr::CrateLampNightValue(P::Environment, 2000.0f) == fr::CrateLampNightCap(P::Environment));
    CHECK(fr::CrateLampNightValue(P::Side, 1000.0f) == fr::CrateLampNightCap(P::Side));

    // At or below the cap (a side light before its crate opens: 1): left alone.
    CHECK_FALSE(fr::CrateLampNightValue(P::Side, 1.0f).has_value());
    CHECK_FALSE(fr::CrateLampNightValue(P::Side, fr::CrateLampNightCap(P::Side)).has_value());
    CHECK_FALSE(fr::CrateLampNightValue(P::Side, fr::CrateLampNightCap(P::Side) + 0.001f).has_value());
    CHECK_FALSE(fr::CrateLampNightValue(P::Environment, 1.0f).has_value());
    CHECK_FALSE(fr::CrateLampNightValue(P::Center, 0.0f).has_value());
    CHECK_FALSE(fr::CrateLampNightValue(P::Center, std::numeric_limits<float>::quiet_NaN()).has_value());

    // A modest glow, well under the day placements' smallest powers (25).
    for (const P s_Part : {P::LampCore, P::Center, P::Environment, P::Side}) {
        CHECK(fr::CrateLampNightCap(s_Part) > 0.0f);
        CHECK(fr::CrateLampNightCap(s_Part) < 25.0f);
    }
}

TEST_CASE("crate lamps: log text") {
    CHECK(fr::CrateLampPartName(fr::CrateLampPart::Center) == "center light");
    CHECK(fr::CrateLampValueSpan(std::vector<float> {}) == "none");
    CHECK(fr::CrateLampValueSpan(std::vector<float> {100.0f}) == "100");
    CHECK(fr::CrateLampValueSpan(std::vector<float> {600.0f, 100.0f, 7000.0f}) == "100 to 7000");
    CHECK(fr::CrateLampValueSpan(std::vector<float> {8.0f, 8.0f}) == "8");
    CHECK(fr::CrateLampValueSpan(std::vector<float> {0.5f, 15.0f}) == "0.5 to 15");
}
