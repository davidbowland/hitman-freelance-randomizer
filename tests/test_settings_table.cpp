#include "doctest.h"

#include "Config.h"
#include "Footsteps.h"
#include "ParamNames.h"
#include "SensorDials.h"
#include "SettingsTable.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string ReadRepoFile(const char* p_Relative) {
    const std::filesystem::path path = std::filesystem::path(FR_REPO_ROOT) / p_Relative;
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot read " << path.string());
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool IsNameChar(char p_Char) {
    return std::isalnum(static_cast<unsigned char>(p_Char)) || p_Char == '_';
}

// Into p_Keys: each "key" (a-z, 0-9, _) passed first to a call in p_Source
// whose name contains p_Name (followed by more name characters when
// p_AnySuffix), after p_Lead (e.g. "kSettings,") when that is not empty.
// Whitespace may come after "(" and after p_Lead.
void CollectKeys(std::string_view p_Source, std::string_view p_Name, bool p_AnySuffix, std::string_view p_Lead,
                 std::set<std::string>& p_Keys) {
    for (size_t s_At = p_Source.find(p_Name); s_At != std::string_view::npos; s_At = p_Source.find(p_Name, s_At + 1)) {
        size_t i = s_At + p_Name.size();
        const auto s_SkipSpace = [&] {
            while (i < p_Source.size() && std::isspace(static_cast<unsigned char>(p_Source[i])))
                ++i;
        };

        while (p_AnySuffix && i < p_Source.size() && IsNameChar(p_Source[i]))
            ++i;

        if (i >= p_Source.size() || p_Source[i] != '(')
            continue;

        ++i;
        s_SkipSpace();

        if (!p_Lead.empty()) {
            if (p_Source.substr(i, p_Lead.size()) != p_Lead)
                continue;

            i += p_Lead.size();
            s_SkipSpace();
        }

        if (i >= p_Source.size() || p_Source[i] != '"')
            continue;

        const size_t s_Begin = ++i;

        while (i < p_Source.size() && (std::islower(static_cast<unsigned char>(p_Source[i])) ||
                                       std::isdigit(static_cast<unsigned char>(p_Source[i])) || p_Source[i] == '_'))
            ++i;

        if (i > s_Begin && i < p_Source.size() && p_Source[i] == '"')
            p_Keys.insert(std::string(p_Source.substr(s_Begin, i - s_Begin)));
    }
}

// The [settings] keys FreelancerRandomizer.cpp reads or writes by a literal
// name: Load*("key", ...) and ReadIniOnly*("key", ...) read them,
// Get/HasSetting*(kSettings, "key", ...) read them, SetSetting*(kSettings,
// "key", ...) write them. The Load* readers also write a missing key's default.
std::set<std::string> KeysTheMod(bool p_Writes) {
    const std::string source = ReadRepoFile("src/mod/FreelancerRandomizer.cpp");
    std::set<std::string> keys;

    for (const char* name : {"LoadBool", "LoadDouble", "LoadInt", "LoadPercent"})
        CollectKeys(source, name, false, "", keys);

    CollectKeys(source, "SetSetting", true, "kSettings,", keys);

    if (!p_Writes) {
        CollectKeys(source, "ReadIniOnly", true, "", keys);
        CollectKeys(source, "GetSetting", true, "kSettings,", keys);
        CollectKeys(source, "HasSetting", true, "kSettings,", keys);
    }

    return keys;
}

std::set<std::string> KeysTheModLoads() {
    return KeysTheMod(false);
}

std::set<std::string> KeysTheModWrites() {
    return KeysTheMod(true);
}

// The [settings] keys the mod reads that are not in the Settings tab: troubleshooting values set by hand in the
// ini, never written by the mod.
const std::set<std::string> kIniOnly = {"random_loadout_delay"};

// footsteps_<gait> and footsteps_<gait>_range are loaded through fmt::format.
bool IsGaitKey(std::string_view key) {
    for (const fr::FootstepGait& gait : fr::FootstepGaits()) {
        const std::string base = "footsteps_" + std::string(gait.key);

        if (key == base || key == base + "_range")
            return true;
    }

    return false;
}

} // namespace

TEST_CASE("settings table: rows are well formed") {
    std::set<std::string_view> keys;

    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        CAPTURE(info.key);
        CHECK(keys.insert(info.key).second);
        CHECK(info.group < fr::SettingGroup::Count);
        CHECK(!info.label.empty());
        CHECK(!info.help.empty());
        CHECK(info.current != nullptr);
        CHECK(!kIniOnly.contains(std::string(info.key)));

        switch (info.type) {
            case fr::SettingType::Bool: CHECK(fr::ParseSettingBool(info.def).has_value()); break;
            case fr::SettingType::Int:
            case fr::SettingType::Number: {
                CHECK(info.min < info.max);
                const auto def = fr::ParseSettingNumber(info.def);
                REQUIRE(def.has_value());
                CHECK(*def >= info.min);
                CHECK(*def <= info.max);
                // The default survives the menu's normalization unchanged.
                CHECK(fr::SameSettingValue(info, fr::NormalizeSettingNumber(info, *def), info.def));
                break;
            }
            case fr::SettingType::Choice: CHECK(fr::SettingChoiceIndex(info, info.def) >= 0); break;
            case fr::SettingType::List: break;
        }

        if (!info.parent.empty()) {
            const fr::SettingInfo* parent = fr::FindSetting(info.parent);
            REQUIRE(parent != nullptr);
            CHECK(parent->type == fr::SettingType::Bool);
            // After its switch, in its switch's section (right under it in the same group).
            CHECK(fr::SettingSectionOf(parent->group) == fr::SettingSectionOf(info.group));
            CHECK(parent < &info);
        }
    }

    CHECK(fr::FindSetting("no_such_key") == nullptr);
}

TEST_CASE("settings table: groups are contiguous, in the tab's order, logs last") {
    fr::SettingGroup last = fr::SettingGroup::General;
    std::set<fr::SettingGroup> seen;
    std::set<std::string_view> names;

    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        CAPTURE(info.key);
        CHECK(info.group >= last);
        last = info.group;
        CHECK(fr::SettingGroupName(info.group) != "?");
        seen.insert(info.group);
    }

    CHECK(last == fr::SettingGroup::Logs);
    // No empty group, and no two with the same name.
    CHECK(seen.size() == static_cast<size_t>(fr::SettingGroup::Count));

    for (const fr::SettingGroup group : seen)
        CHECK(names.insert(fr::SettingGroupName(group)).second);

    // Sentence case.
    CHECK(fr::SettingGroupName(fr::SettingGroup::Logs) == "Logs (for troubleshooting)");

    // A capital first, then lower case but for "AI".
    const auto sentenceCase = [](std::string_view name) {
        CAPTURE(name);
        REQUIRE(!name.empty());
        CHECK(name.back() != ':');
        CHECK((name[0] >= 'A' && name[0] <= 'Z'));

        for (size_t at = name.find(' '); at != std::string_view::npos; at = name.find(' ', at + 1)) {
            const std::string_view word = name.substr(at + 1, name.find(' ', at + 1) - at - 1);
            CHECK((word == "AI" || word.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string_view::npos));
        }
    };

    for (const fr::SettingGroup group : seen)
        sentenceCase(fr::SettingGroupName(group));

    for (size_t i = 0; i < static_cast<size_t>(fr::SettingSection::Count); ++i)
        sentenceCase(fr::SettingSectionName(static_cast<fr::SettingSection>(i)));

    // Labels: no trailing colon, one space after an inner one ("Log: payouts").
    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        CAPTURE(info.key);
        CHECK(info.label.back() != ':');
        CHECK(info.label.find(":  ") == std::string_view::npos);

        const size_t colon = info.label.find(':');
        if (colon != std::string_view::npos)
            CHECK(info.label.substr(colon, 2) == ": ");
    }
}

TEST_CASE("settings table: every group is in a section, the sections in the tab's order") {
    // Each group in one section, the sections' groups together and in section order (so the table,
    // ordered by group, is ordered by section too).
    fr::SettingSection last = fr::SettingSection::General;
    std::set<std::string_view> names;

    for (size_t i = 0; i < static_cast<size_t>(fr::SettingGroup::Count); ++i) {
        const fr::SettingGroup group = static_cast<fr::SettingGroup>(i);
        const fr::SettingSection section = fr::SettingSectionOf(group);
        CAPTURE(fr::SettingGroupName(group));
        REQUIRE(section < fr::SettingSection::Count);
        CHECK(section >= last);
        last = section;
    }

    CHECK(last == fr::SettingSection::Advanced);

    for (size_t i = 0; i < static_cast<size_t>(fr::SettingSection::Count); ++i) {
        const fr::SettingSection section = static_cast<fr::SettingSection>(i);
        const std::vector<fr::SettingGroup> groups = fr::SettingSectionGroups(section);
        CAPTURE(fr::SettingSectionName(section));
        CHECK(fr::SettingSectionName(section) != "?");
        CHECK(names.insert(fr::SettingSectionName(section)).second);
        // No empty section; only General (Mod enabled, at the top) shows its rows with no group of its own.
        REQUIRE(!groups.empty());

        for (const fr::SettingGroup group : groups) {
            CHECK(fr::SettingSectionOf(group) == section);

            if (groups.size() > 1)
                CHECK_FALSE(fr::SettingGroupIsSection(group));
        }
    }

    CHECK(fr::SettingSectionName(fr::SettingSection::WeaponsGear) == "Weapons & gear");
    CHECK(fr::SettingSectionName(fr::SettingSection::Maps) == "Maps");
    CHECK(fr::SettingSectionName(fr::SettingSection::Safehouse) == "Safehouse");
    CHECK(fr::SettingSectionName(fr::SettingSection::Advanced) == "Advanced");
    CHECK(fr::SettingGroupIsSection(fr::SettingGroup::General));
    // Safehouse's one group folds like every other group.
    CHECK_FALSE(fr::SettingGroupIsSection(fr::SettingGroup::Safehouse));
    CHECK(!fr::SettingSectionIntro(fr::SettingSection::Advanced).empty());

    // Sub-sections stay short. Map variants splits its chances under a heading per map; the Logs are alike
    // switches, one per log topic; supply crates (with gun crates) and footsteps (with their ranges) were
    // merged on purpose (a range or gun odds mean nothing without their switch).
    for (size_t i = 0; i < static_cast<size_t>(fr::SettingGroup::Count); ++i) {
        const fr::SettingGroup group = static_cast<fr::SettingGroup>(i);

        if (group == fr::SettingGroup::Variants || group == fr::SettingGroup::Logs ||
            group == fr::SettingGroup::Crates || group == fr::SettingGroup::Footsteps)
            continue;

        size_t rows = 0;

        for (const fr::SettingInfo& info : fr::SettingsTable())
            rows += info.group == group ? 1 : 0;

        CAPTURE(fr::SettingGroupName(group));
        CHECK(rows >= 1);
        CHECK(rows <= 10);
    }
}

TEST_CASE("settings table: each group holds what its name says") {
    const auto groupOf = [](std::string_view key) {
        const fr::SettingInfo* info = fr::FindSetting(key);
        REQUIRE(info != nullptr);
        return info->group;
    };
    const auto sectionOf = [&groupOf](std::string_view key) { return fr::SettingSectionOf(groupOf(key)); };
    const auto index = [](std::string_view key) {
        const auto table = fr::SettingsTable();

        for (size_t i = 0; i < table.size(); ++i) {
            if (table[i].key == key)
                return i;
        }

        return table.size();
    };

    CHECK(groupOf("enabled") == fr::SettingGroup::General);
    CHECK(sectionOf("enabled") == fr::SettingSection::General);

    // Weapons & gear.
    CHECK(groupOf("random_loadout") == fr::SettingGroup::Loadout);
    CHECK(groupOf("grant_coin") == fr::SettingGroup::FreeItems);
    CHECK(groupOf("lockpick_grant") == fr::SettingGroup::FreeItems);
    // The nail count and the slow lockpick's time follow the lockpick grant's choice, right under it.
    CHECK(index("lockpick_nails") == index("lockpick_grant") + 1);
    CHECK(index("lockpick_slow_picks") == index("lockpick_grant") + 2);
    CHECK(groupOf("supplier_steer") == fr::SettingGroup::Supplier);
    CHECK(groupOf("supplier_steer_gear") == fr::SettingGroup::Supplier);
    CHECK(fr::FindSetting("supplier_steer_gear")->parent == "supplier_steer");
    CHECK(groupOf("crate_items") == fr::SettingGroup::Crates);
    CHECK(groupOf("crate_items_unloaded") == fr::SettingGroup::Crates);
    CHECK(fr::FindSetting("crate_items_unloaded")->parent == "crate_items");
    CHECK(groupOf("crate_guns") == fr::SettingGroup::Crates);
    CHECK(groupOf("weapon_rarity") == fr::SettingGroup::Rarity);
    CHECK(groupOf("concealable_mace") == fr::SettingGroup::WeaponsMisc);
    CHECK(groupOf("legal_sniper_rifles") == fr::SettingGroup::WeaponsMisc);
    CHECK(groupOf("guard_weapons") == fr::SettingGroup::WeaponsMisc);
    CHECK(fr::FindSetting("legal_sniper_rifles")->parent.empty());
    // The holsterable mace is its own switch (not a weapon_rarity refinement), next to the sniper rifles'.
    CHECK(fr::FindSetting("concealable_mace")->parent.empty());
    CHECK(fr::FindSetting("concealable_mace")->def == "true");
    CHECK(fr::FindSetting("concealable_mace")->help.find("rarit") == std::string_view::npos);
    CHECK(index("concealable_mace") == index("legal_sniper_rifles") + 1);

    for (const std::string_view key : {"random_loadout", "grant_coin", "supplier_steer", "crate_items", "crate_guns",
                                       "weapon_rarity", "concealable_mace"}) {
        CAPTURE(key);
        CHECK(sectionOf(key) == fr::SettingSection::WeaponsGear);
    }

    // Maps: the door locks right after the map variants.
    CHECK(groupOf("variants") == fr::SettingGroup::Variants);
    CHECK(groupOf("variant_force") == fr::SettingGroup::Variants);
    CHECK(index("variant_force") == index("variants") + 1);
    // Full map variants (the packages built at game start, and the way to remove them) right under them.
    CHECK(groupOf("full_map_variants") == fr::SettingGroup::Variants);
    CHECK(index("full_map_variants") == index("variant_force") + 1);
    CHECK(fr::FindSetting("full_map_variants")->def == "true");
    CHECK(fr::FindSetting("full_map_variants")->parent.empty());
    CHECK(groupOf("door_locks") == fr::SettingGroup::Doors);
    CHECK(groupOf("door_locks_share_min") == fr::SettingGroup::Doors);
    CHECK(fr::SettingGroupName(fr::SettingGroup::Doors) == "Doors & locks");
    CHECK(groupOf("target_pool") == fr::SettingGroup::Targets);
    CHECK(groupOf("enforcers") == fr::SettingGroup::Enforcers);

    for (const std::string_view key : {"variants", "door_locks", "target_pool", "enforcers"}) {
        CAPTURE(key);
        CHECK(sectionOf(key) == fr::SettingSection::Maps);
    }

    // Guards & AI.
    CHECK(groupOf("dials") == fr::SettingGroup::Dials);
    CHECK(groupOf("sensor_dials") == fr::SettingGroup::Senses);
    CHECK(groupOf("footsteps") == fr::SettingGroup::Footsteps);
    CHECK(groupOf("footsteps_walk_range") == fr::SettingGroup::Footsteps);
    CHECK(sectionOf("footsteps_max") == fr::SettingSection::Guards);

    // Objectives & money.
    CHECK(groupOf("client_reqs_mode") == fr::SettingGroup::ClientReqs);
    CHECK(groupOf("prestige_shuffle") == fr::SettingGroup::Prestige);
    CHECK(groupOf("payouts") == fr::SettingGroup::Money);
    CHECK(groupOf("loot_amounts") == fr::SettingGroup::Loot);
    CHECK(sectionOf("loot_amount_mule_max") == fr::SettingSection::ObjectivesMoney);

    CHECK(groupOf("fishing_script") == fr::SettingGroup::Safehouse);

    // Advanced: the logs only; every row there is a log switch, and every log switch is there.
    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        CAPTURE(info.key);
        CHECK((fr::SettingSectionOf(info.group) == fr::SettingSection::Advanced) ==
              (info.group == fr::SettingGroup::Logs));
        CHECK((info.group == fr::SettingGroup::Logs) == (info.label.substr(0, 5) == "Log: "));
    }

    CHECK(fr::SettingSectionOf(fr::SettingGroup::Logs) == fr::SettingSection::Advanced);
}

TEST_CASE("settings table: each default is what Config{} holds") {
    const fr::Config defaults;

    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        const std::string held = info.current(defaults);
        CAPTURE(info.key);
        CAPTURE(held);
        CHECK(fr::SameSettingValue(info, held, info.def));
    }

    // A player who brings no lockpick gets Rusty Old Nails unless they choose otherwise.
    CHECK(fr::FindSetting("lockpick_grant")->def == std::string_view("nails"));
    CHECK(defaults.lockpickGrant == "nails");
    CHECK(defaults.lockpickNails == 5);
}

TEST_CASE("settings table: the Logs group says which lines no switch turns off") {
    CHECK(fr::SettingGroupIntro(fr::SettingGroup::Logs).find("always written") != std::string_view::npos);
}

TEST_CASE("settings table: an ini key the mod does not know is ignored") {
    // The mod reads only the keys it knows, by name; anything else in the ini (a key from another build, a typo)
    // is left alone, neither read nor rewritten. Nothing walks the ini's keys.
    const std::string source = ReadRepoFile("src/mod/FreelancerRandomizer.cpp");
    CHECK(source.find("retired") == std::string::npos);

    for (const std::string& key : KeysTheModLoads()) {
        CAPTURE(key);
        CHECK((fr::FindSetting(key) != nullptr || kIniOnly.contains(key)));
    }
}

TEST_CASE("settings table: the mod writes only the Settings tab's keys to the ini") {
    const std::set<std::string> written = KeysTheModWrites();
    REQUIRE(written.count("dials") == 1);

    for (const std::string& key : written) {
        CAPTURE(key);
        CHECK(fr::FindSetting(key) != nullptr);
    }

    // The ini-only keys are read, never written.
    for (const std::string& key : kIniOnly) {
        CAPTURE(key);
        CHECK(KeysTheModLoads().contains(key));
        CHECK_FALSE(written.contains(key));
    }
}

TEST_CASE("settings table: every key the mod loads is in the table, and every row is loaded") {
    const std::set<std::string> loaded = KeysTheModLoads();
    // The scan works (a few keys every version has).
    REQUIRE(loaded.count("dials") == 1);
    REQUIRE(loaded.count("client_reqs_exclude") == 1);

    for (const std::string& key : loaded) {
        CAPTURE(key);
        CHECK((fr::FindSetting(key) != nullptr || kIniOnly.contains(key)));
    }

    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        CAPTURE(info.key);
        CHECK((loaded.count(std::string(info.key)) == 1 || IsGaitKey(info.key)));
    }

    for (const fr::FootstepGait& gait : fr::FootstepGaits()) {
        const std::string base = "footsteps_" + std::string(gait.key);
        CAPTURE(base);
        REQUIRE(fr::FindSetting(base) != nullptr);
        REQUIRE(fr::FindSetting(base + "_range") != nullptr);
        CHECK(fr::SettingBoolOr(*fr::FindSetting(base), "") == gait.defaultOn);
        CHECK(fr::SettingNumberOr(*fr::FindSetting(base + "_range"), "") == doctest::Approx(gait.defaultRange));
    }
}

TEST_CASE("settings table: shares and chances are whole percents") {
    // The ini and the menu hold 33 for 33%; fr::Config keeps the fraction.
    const std::set<std::string_view> percents = {
        "target_pool_swap_min",   "target_pool_swap_max",        "enforcers_share_min",   "enforcers_share_max",
        "door_locks_share_min",   "door_locks_share_max",        "crate_gun_chance",      "crate_guns_share",
        "weapon_rarity_keep_min", "weapon_rarity_keep_max",      "payout_jackpot_chance", "crate_all_tools_chance",
        "crate_merces_chance",    "footsteps_investigate_chance"};
    const std::string source = ReadRepoFile("src/mod/FreelancerRandomizer.cpp");

    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        CAPTURE(info.key);
        const bool variantChance = info.key.substr(0, 15) == "variant_chance_";

        if (!percents.count(info.key)) {
            // Every other "%" row is a variant chance, already a whole percent.
            CHECK((info.unit != "%" || variantChance));
            continue;
        }

        CHECK(info.type == fr::SettingType::Int);
        CHECK(info.min == 0);
        CHECK(info.unit == "%");

        // Single points matter for the jackpot chance: it moves by 1 up to 50%. Every
        // other percent moves by 5 over the whole 0-100%.
        if (info.key == "payout_jackpot_chance") {
            CHECK(info.max == 50);
            CHECK(info.step == 1);
        }
        else {
            CHECK(info.max == 100);
            CHECK(info.step == 5);
        }
        // Loaded as a percent, never as the bare number.
        const std::string key = "\"" + std::string(info.key) + "\"";
        CHECK(source.find("LoadPercent(" + key) != std::string::npos);
        CHECK(source.find("LoadDouble(" + key) == std::string::npos);
        CHECK(source.find("LoadInt(" + key) == std::string::npos);
    }

    CHECK(fr::FindSetting("door_locks_share_min")->def == std::string_view("50"));
    CHECK(fr::FindSetting("crate_gun_chance")->def == std::string_view("5"));
    CHECK(fr::FindSetting("crate_all_tools_chance")->def == std::string_view("10"));
    CHECK(fr::FindSetting("crate_merces_chance")->def == std::string_view("5"));
    CHECK(fr::FindSetting("crate_guns_share")->def == std::string_view("80"));
    CHECK(fr::FindSetting("payout_jackpot_chance")->def == std::string_view("8"));
    CHECK(fr::FindSetting("door_locks_share_min")->current(fr::Config {}) == "50");
    CHECK(fr::FindSetting("weapon_rarity_keep_max")->current(fr::Config {}) == "10");
}

TEST_CASE("settings table: every slider moves in steps over a reasonable span") {
    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        if (info.type != fr::SettingType::Int && info.type != fr::SettingType::Number)
            continue;

        CAPTURE(info.key);
        CHECK(info.step > 0);
        CHECK(info.max > info.min);
        // At most 400 steps, so a swipe or the d-pad can reach a value.
        CHECK(fr::SettingStepCount(info) <= 400);
        // max sits on a step.
        CHECK(fr::SettingStepValue(info, fr::SettingStepCount(info)) == doctest::Approx(info.max));

        // The default sits on a step, so the slider can always go back to it.
        const double def = fr::SettingNumberOr(info, info.def);
        CHECK(fr::SettingStepValue(info, fr::SettingStepIndex(info, def)) == doctest::Approx(def));
    }

    const fr::SettingInfo& bonus = *fr::FindSetting("payout_jackpot_bonus");
    CHECK(bonus.max == 10000);
    CHECK(bonus.step == 50);
    CHECK(fr::SettingStepCount(bonus) == 200);
    CHECK(fr::SettingStepIndex(bonus, 5000) == 100);
    CHECK(fr::SettingStepIndex(bonus, 5024) == 100);
    CHECK(fr::SettingStepIndex(bonus, 99999) == 200);
    CHECK(fr::SettingStepIndex(bonus, -3) == 0);
    CHECK(fr::SettingStepValue(bonus, 101) == 5050);

    const fr::SettingInfo& sigma = *fr::FindSetting("dials_sigma");
    CHECK(fr::SettingStepCount(sigma) == 30);
    CHECK(fr::SettingStepValue(sigma, 7) == 0.35);
    CHECK(fr::SettingStepIndex(sigma, 0.35) == 7);

    const fr::SettingInfo& lean = *fr::FindSetting("dials_lean");
    CHECK(fr::SettingStepIndex(lean, -2) == 0);
    CHECK(fr::SettingStepValue(lean, 17) == -0.3);

    const fr::SettingInfo& safe = *fr::FindSetting("loot_amount_safe_min");
    CHECK(fr::SettingStepValue(safe, 0) == 250);
    CHECK(fr::SettingStepIndex(safe, 1500) == 5);
}

TEST_CASE("settings values: percents and fractions") {
    CHECK(fr::PercentToFraction(33) == 0.33);
    CHECK(fr::PercentToFraction(5) == 0.05);
    CHECK(fr::PercentToFraction(0) == 0.0);
    CHECK(fr::PercentToFraction(100) == 1.0);
    CHECK(fr::PercentToFraction(12.5) == 0.125);
    // No float noise: 0.07 * 100 is 7.000000000000001.
    CHECK(fr::FractionToPercent(0.07) == 7.0);
    CHECK(fr::FractionToPercent(0.33) == 33.0);
    CHECK(fr::FractionToPercent(0.125) == 12.5);
    CHECK(fr::PercentText(0.07) == "7%");
    CHECK(fr::PercentText(0.9) == "90%");
    CHECK(fr::PercentText(1.5) == "150%");

    // Every whole percent goes to the fraction and back unchanged.
    for (int percent = 0; percent <= 100; ++percent) {
        CAPTURE(percent);
        CHECK(fr::FractionToPercent(fr::PercentToFraction(percent)) == percent);
        CHECK(fr::FormatSettingNumber(fr::FractionToPercent(fr::PercentToFraction(percent))) ==
              std::to_string(percent));
    }
}

TEST_CASE("settings table: only enabled and full_map_variants need a restart") {
    // Full map variants builds or removes the packages at game start.
    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        CAPTURE(info.key);
        CHECK((info.applies == fr::SettingApplies::Restart) ==
              (info.key == "enabled" || info.key == "full_map_variants"));
    }

    CHECK(fr::SettingAppliesText(fr::SettingApplies::Restart) == "after restarting the game");
    CHECK(fr::SettingAppliesText(fr::SettingApplies::NextLoad) ==
          "at the next mission start, restart or safehouse visit");
}

TEST_CASE("settings table: the loadout's lockpick-first switch") {
    const fr::SettingInfo* info = fr::FindSetting("random_loadout_lockpick_first");
    REQUIRE(info != nullptr);
    CHECK(info->group == fr::SettingGroup::Loadout);
    CHECK(info->type == fr::SettingType::Bool);
    CHECK(info->def == "true");
    CHECK(info->parent == "random_loadout");
    CHECK(fr::Config {}.randomLoadoutLockpickFirst);
}

TEST_CASE("settings table: the loadout's objective items switch") {
    const fr::SettingInfo* info = fr::FindSetting("random_loadout_objective_items");
    REQUIRE(info != nullptr);
    CHECK(info->group == fr::SettingGroup::Loadout);
    CHECK(info->type == fr::SettingType::Bool);
    CHECK(info->def == "true");
    CHECK(info->parent == "random_loadout");
    CHECK(fr::Config {}.randomLoadoutObjectiveItems);
}

TEST_CASE("settings table: holsterable items only replaces the briefcase switch") {
    const fr::SettingInfo* info = fr::FindSetting("random_loadout_holsterable_only");
    REQUIRE(info != nullptr);
    CHECK(info->label == "Holsterable items only");
    CHECK(info->group == fr::SettingGroup::Loadout);
    CHECK(info->type == fr::SettingType::Bool);
    CHECK(info->def == "false");
    CHECK(info->parent == "random_loadout");
    CHECK_FALSE(fr::Config {}.randomLoadoutHolsterableOnly);
    CHECK(fr::FindSetting("random_loadout_briefcase") == nullptr);
}

TEST_CASE("settings values: booleans") {
    for (const char* yes : {"true", "TRUE", " 1 ", "yes", "on", "y"})
        CHECK(fr::ParseSettingBool(yes) == true);

    for (const char* no : {"false", "0", "No", "off", "n"})
        CHECK(fr::ParseSettingBool(no) == false);

    CHECK_FALSE(fr::ParseSettingBool("maybe").has_value());
    CHECK_FALSE(fr::ParseSettingBool("").has_value());

    const fr::SettingInfo& dials = *fr::FindSetting("dials");
    CHECK(fr::SameSettingValue(dials, "true", "1"));
    CHECK_FALSE(fr::SameSettingValue(dials, "true", "false"));
    CHECK_FALSE(fr::SameSettingValue(dials, "junk", "junk")); // unparsable is never "the same value"
    CHECK(fr::SettingBoolOr(dials, "junk"));                  // the default
}

TEST_CASE("settings values: numbers") {
    CHECK(fr::ParseSettingNumber(" 0.35 ") == doctest::Approx(0.35));
    CHECK(fr::ParseSettingNumber("+2") == doctest::Approx(2.0));
    CHECK_FALSE(fr::ParseSettingNumber("1.5x").has_value());
    CHECK_FALSE(fr::ParseSettingNumber("nan").has_value());
    CHECK_FALSE(fr::ParseSettingNumber("").has_value());
    CHECK(fr::ParseSettingInt("+5") == 5);
    CHECK(fr::ParseSettingInt("25.0") == 25);
    CHECK(fr::ParseSettingInt(" 24.6 ") == 25);
    CHECK(fr::ParseSettingInt("-3") == -3);
    CHECK_FALSE(fr::ParseSettingInt("5x").has_value());
    CHECK_FALSE(fr::ParseSettingInt("1e12").has_value());
    CHECK(fr::FormatSettingNumber(0.35) == "0.35");
    CHECK(fr::FormatSettingNumber(5000.0) == "5000");
    CHECK(fr::FormatSettingNumber(-0.0) == "0");

    const fr::SettingInfo& sigma = *fr::FindSetting("dials_sigma");
    CHECK(fr::SameSettingValue(sigma, "0.350000", "0.35"));
    CHECK_FALSE(fr::SameSettingValue(sigma, "0.36", "0.35"));
    CHECK(fr::IsDefaultSetting(sigma, "0.700000"));
    CHECK(fr::SettingNumberOr(sigma, "bad") == doctest::Approx(0.7));

    // Clamped to the row's range and snapped to its step.
    CHECK(fr::NormalizeSettingNumber(sigma, 0.3549) == "0.35");
    CHECK(fr::NormalizeSettingNumber(sigma, 9.0) == "1.5");
    CHECK(fr::NormalizeSettingNumber(*fr::FindSetting("payout_jackpot_bonus"), 5023.0) == "5000");
    CHECK(fr::NormalizeSettingNumber(*fr::FindSetting("client_reqs_pool_size"), 7.6) == "8");
    CHECK(fr::NormalizeSettingNumber(*fr::FindSetting("client_reqs_pool_size"), 0.0) == "3");
    CHECK(fr::NormalizeSettingNumber(*fr::FindSetting("lockpick_slow_picks"), 0.0) == "1");
    CHECK(fr::NormalizeSettingNumber(*fr::FindSetting("lockpick_slow_picks"), 9.0) == "4");
    CHECK(fr::FindSetting("lockpick_free_time_factor") == nullptr);
    CHECK(fr::NormalizeSettingNumber(*fr::FindSetting("dials_lean"), -0.30000000000000004) == "-0.3");
}

TEST_CASE("settings values: choices and lists") {
    const fr::SettingInfo& grant = *fr::FindSetting("lockpick_grant");
    CHECK(fr::SettingChoices(grant) == std::vector<std::string_view> {"off", "slow_lockpick", "nails"});
    CHECK(fr::SettingChoiceIndex(grant, " NAILS ") == 2);
    CHECK(fr::SettingChoiceIndex(grant, "both") == -1);
    CHECK(fr::SameSettingValue(grant, "Nails", "nails"));

    const fr::SettingInfo& force = *fr::FindSetting("variant_force");
    CHECK(fr::SettingChoices(force).front().empty()); // the seeded pick
    CHECK(fr::SettingChoiceIndex(force, "") == 0);
    CHECK(fr::IsDefaultSetting(force, ""));

    // The menu names the options; the ini keeps the ids.
    CHECK(fr::SettingChoiceLabel(force, "") == "Off: the chances decide");
    CHECK(fr::SettingChoiceLabel(force, "none") == "Every map stays normal");
    CHECK(fr::SettingChoiceLabel(force, "tod_ebola_night") == "Sapienza: The Author");
    CHECK(fr::SettingChoiceLabel(force, "holiday_hoarders") == "Paris: Holiday Hoarders");
    CHECK(fr::SettingChoiceLabel(force, "the_rage") == "Chongqing: The Rage");
    CHECK(fr::SettingChoiceLabel(force, "the_mercenary") == "Colorado: The Mercenary");
    CHECK(fr::SettingChoiceLabel(force, "serial_killer") == "Whittleton Creek: The Serial Killer");
    CHECK(fr::SettingChoiceLabel(force, "appraiser") == "Isle of Sgail: The Appraiser");
    CHECK(fr::SettingChoiceLabel(force, "revolutionary") == "Santa Fortuna: The Revolutionary");
    for (const std::string_view choice : fr::SettingChoices(force))
        CHECK(fr::SettingChoiceLabel(force, choice) != choice); // every option has a plain name
    CHECK(fr::SettingChoiceLabel(grant, "nails") == "Rusty nails");

    const fr::SettingInfo& exclude = *fr::FindSetting("client_reqs_exclude");
    CHECK(fr::IsDefaultSetting(exclude, "18, 3,4"));
    CHECK(fr::IsDefaultSetting(exclude, "03,4,,18 "));
    CHECK_FALSE(fr::IsDefaultSetting(exclude, "3,18"));
    CHECK_FALSE(fr::IsDefaultSetting(exclude, ""));

    const fr::SettingInfo& off = *fr::FindSetting("dials_off");
    CHECK(fr::IsDefaultSetting(off, " "));
    CHECK(fr::SameSettingValue(off, "EDP_A, EDP_B", "EDP_B,EDP_A"));
    CHECK_FALSE(fr::SameSettingValue(off, "EDP_A", "edp_a")); // names are case-sensitive
}

TEST_CASE("settings values: never-pick checklists") {
    const fr::SettingInfo& reqs = *fr::FindSetting("client_reqs_exclude");
    const fr::SettingInfo& prestige = *fr::FindSetting("prestige_exclude");
    CHECK(prestige.group == fr::SettingGroup::Prestige);
    CHECK(prestige.parent == "prestige_shuffle");
    CHECK(prestige.def == "115,117,118,119,120");
    CHECK(fr::Config {}.prestigeExclude == std::set<int32_t> {115, 117, 118, 119, 120});

    // Every option has a plain name; the defaults are among them.
    for (const fr::SettingInfo* info : {&reqs, &prestige}) {
        CAPTURE(info->key);
        const std::vector<fr::SettingListOption> options = fr::SettingListOptions(*info);
        REQUIRE(!options.empty());
        std::set<int32_t> listed;

        for (const fr::SettingListOption& option : options) {
            CHECK(!option.name.empty());
            CHECK(listed.insert(option.id).second);
        }

        for (const int32_t id : fr::SettingListIds(*info, info->def))
            CHECK(listed.contains(id));

        CHECK(fr::SettingListLeavesEnough(*info, fr::SettingListIds(*info, info->def)));
        CHECK_FALSE(fr::SettingListLeavesEnough(*info, listed));
    }

    CHECK(fr::SettingListOptions(*fr::FindSetting("dials")).empty());
    CHECK(fr::SettingListOptions(reqs).front().name == "Hide Target Bodies");

    // A tick adds the id, an untick removes it; the ini keeps comma-separated ids.
    CHECK(fr::SettingListToggle(reqs, "3,4,18", 7, true) == "3, 4, 7, 18");
    CHECK(fr::SettingListToggle(reqs, "3, 4, 18", 4, false) == "3, 18");
    CHECK(fr::SettingListToggle(prestige, "", 115, true) == "115");
    CHECK(fr::SettingListToggle(prestige, "115", 115, false).empty());
    CHECK(fr::IsDefaultSetting(reqs, fr::SettingListToggle(reqs, "3,18", 4, true)));
}

TEST_CASE("settings: the never-changed lists are checklists that keep names in the ini") {
    const fr::SettingInfo& off = *fr::FindSetting("dials_off");
    const fr::SettingInfo& senses = *fr::FindSetting("sensor_dials_off");

    for (const fr::SettingInfo* info : {&off, &senses}) {
        CAPTURE(info->key);
        const std::vector<fr::SettingListOption> options = fr::SettingListOptions(*info);
        REQUIRE(!options.empty());
        std::set<int32_t> ids;

        for (const fr::SettingListOption& option : options) {
            CHECK(!option.name.empty());
            CHECK(ids.insert(option.id).second);
            // Each box round-trips through the ini text.
            const std::string text = fr::SettingListToggle(*info, "", option.id, true);
            CHECK(fr::SettingListIds(*info, text) == std::set<int32_t> {option.id});
        }

        // Nothing checked by default; any number may be checked.
        CHECK(fr::SettingListIds(*info, info->def).empty());
        CHECK(fr::SettingListLeavesEnough(*info, ids));
    }

    // The ini keeps the names the mod reads (fr::ParseOffList, fr::ParseSensorOffList).
    const int32_t hp = *fr::ParamFromName("EDP_PlayerHitPointsMax");
    CHECK(fr::SettingListToggle(off, "", hp, true) == "EDP_PlayerHitPointsMax");
    CHECK(fr::SettingListIds(off, "EDP_PlayerHitPointsMax, nonsense") == std::set<int32_t> {hp});
    const std::set<int32_t> cone = fr::SettingListIds(senses, "cone_combat");
    REQUIRE(cone.size() == 1);
    CHECK(fr::SettingListToggle(senses, "cone_combat", *cone.begin(), false).empty());
}
