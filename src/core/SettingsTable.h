#pragma once

// The [settings] keys of freelancerrandomizer.ini as one table: what the
// overlay's Settings tab draws (section and group, label, help, widget, range) and what
// the host tests check the mod against (every key the mod loads is here, and
// each row's default is what fr::Config{} holds). To add a setting: load it in
// FreelancerRandomizer::LoadConfig, add a row in SettingsTable.cpp (with an
// accessor for its fr::Config field); the tests fail until both agree.

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

struct Config;

enum class SettingType : uint8_t {
    Bool,   // checkbox; written as true / false
    Int,    // slider; whole numbers
    Number, // slider; snapped to step when step > 0
    Choice, // combo of SettingInfo::choices
    List,   // comma-separated ids or names; text input
};

// The Settings tab's top-level sections, each a heading (never folded) over
// its groups (SettingSectionOf; the groups fold), in the order the tab shows
// them: the ones a player is likeliest to change first, Advanced (the logs)
// last. General (Mod enabled) is drawn at the
// top, without a heading.
enum class SettingSection : uint8_t {
    General,
    WeaponsGear,     // loadout, free items, supplier, crates, weapon rarity
    Maps,            // map variants, doors & locks, possible targets, enforcers
    Guards,          // AI difficulty, guard senses, footsteps
    ObjectivesMoney, // common objectives, prestige cards, payouts, loot money
    Safehouse,
    Advanced, // logs
    Count,
};

// The sub-sections inside the sections, in the order the Settings tab shows
// them (each section's groups together, most used first). A setting that
// refines a switch sits in its switch's section, after it (right under it in
// the same group).
enum class SettingGroup : uint8_t {
    General,
    // Weapons & gear
    Loadout,
    FreeItems, // item grants, lockpick grant
    Supplier,
    Crates,      // supply crate items and guns
    Rarity,      // weapon rarity
    WeaponsMisc, // sniper rifles, mace, guard weapons, emetic poison's sick spots
    // Maps
    Variants, // map variants (the chances drawn map by map)
    Doors,    // door locks
    Targets,  // target pool
    Enforcers,
    // Guards & AI
    Dials, // AI difficulty
    Senses,
    Footsteps, // heard footsteps, their investigation and ranges
    Backup,    // backup guards
    // Objectives & money
    ClientReqs, // common objectives
    Prestige,   // prestige cards
    Money,      // payouts
    Loot,       // loot money
    Safehouse,
    // Advanced
    Logs,
    Count,
};

// When a changed value is used. The mod re-reads the whole ini at the next
// scene load (a mission start, a restart or the safehouse) after a change made
// in the menu; only `enabled` decides whether the mod hooks anything at all.
enum class SettingApplies : uint8_t {
    NextLoad,
    Restart,
};

struct SettingInfo {
    std::string_view key;
    SettingGroup group;
    std::string_view label; // short, plain English
    std::string_view help;  // what it does, for the Settings tab's help mark
    SettingType type;
    std::string_view def; // the default as the ini holds it
    double min = 0.0;     // Int / Number: the slider's range (the mod's own clamps where it has them)
    double max = 0.0;
    double step = 0.0;        // Number: values snap to multiples of this (0: none)
    bool logScale = false;    // Number: logarithmic slider
    std::string_view choices; // Choice: values separated by '|'
    std::string_view parent;  // the switch this setting refines (drawn under it), or empty
    SettingApplies applies = SettingApplies::NextLoad;
    std::string_view unit; // shown after a number ("m", "s"), or empty
    // The value fr::Config holds for this key, as ini text (the defaults test
    // compares Config{} with def).
    std::string (*current)(const Config&) = nullptr;
};

std::span<const SettingInfo> SettingsTable();
const SettingInfo* FindSetting(std::string_view key);

std::string_view SettingGroupName(SettingGroup group);
// A sentence or two the Settings tab shows under an opened group's heading,
// or empty.
std::string_view SettingGroupIntro(SettingGroup group);
SettingSection SettingSectionOf(SettingGroup group);
std::string_view SettingSectionName(SettingSection section);
// As SettingGroupIntro, under an opened section's heading.
std::string_view SettingSectionIntro(SettingSection section);
// The section's groups, in order.
std::vector<SettingGroup> SettingSectionGroups(SettingSection section);
// The group is its section's only one, under the section's name (General,
// Safehouse): the tab shows no sub-heading for it.
bool SettingGroupIsSection(SettingGroup group);
// "next scene load" / "after restarting the game".
std::string_view SettingAppliesText(SettingApplies applies);

// The values a Choice offers, in order.
std::vector<std::string_view> SettingChoices(const SettingInfo& info);
// The index of text among the choices (case-insensitive, trimmed), or -1.
int SettingChoiceIndex(const SettingInfo& info, std::string_view text);
// What the menu shows for a choice (the ini keeps the choice itself): plain
// names for variant_force's option ids (the label of their variant_chance_
// row), "(off)" for an empty choice, else the choice as it is.
std::string SettingChoiceLabel(const SettingInfo& info, std::string_view choice);

// The ini's boolean words (true, 1, yes, on, y / false, 0, no, off, n; any case).
std::optional<bool> ParseSettingBool(std::string_view text);
// A whole finite number (whitespace trimmed); empty otherwise.
std::optional<double> ParseSettingNumber(std::string_view text);
// ParseSettingNumber rounded to the nearest whole number, as the settings
// window rounds it ("+5", "25.0" and "24.6" are 5, 25 and 25); empty outside
// the 32-bit range.
std::optional<int32_t> ParseSettingInt(std::string_view text);
// Shortest text of a number ("0.35", "5000", "-0.5").
std::string FormatSettingNumber(double value);

// Share and chance settings (door_locks_share_min, crate_gun_chance, ...): the
// ini and the menu hold whole percents (33), fr::Config the fraction (0.33).
double PercentToFraction(double percent);
// The percent of a fraction without float noise (0.07: 7, not 7.000000000000001).
double FractionToPercent(double fraction);
// "33%" for 0.33, for log lines.
std::string PercentText(double fraction);

// The text the menu writes for a number: clamped to [min, max], snapped to
// step, rounded for an Int.
std::string NormalizeSettingNumber(const SettingInfo& info, double value);
// A slider moves over a row's steps: the last step's index (min is step 0,
// max the last), the step nearest a value (clamped), and a step's value.
int SettingStepCount(const SettingInfo& info);
int SettingStepIndex(const SettingInfo& info, double value);
double SettingStepValue(const SettingInfo& info, int index);
// The number a row's text holds, or its default when it holds none.
double SettingNumberOr(const SettingInfo& info, std::string_view text);
bool SettingBoolOr(const SettingInfo& info, std::string_view text);

// Two ini texts mean the same value for this setting: booleans and numbers
// by value, choices ignoring case, lists as sets of their entries (numbers
// by value, so "03" is "3"; order, spaces and empty entries ignored).
bool SameSettingValue(const SettingInfo& info, std::string_view a, std::string_view b);
bool IsDefaultSetting(const SettingInfo& info, std::string_view text);

// A List of ids the menu draws as checkboxes (a checked box: the id is in
// the list): client_reqs_exclude (every common objective), prestige_exclude
// (fr::PrestigeExcludeOptions), dials_off (every AI difficulty value,
// fr::DialInfoTable, by its EDifficultyParameter) and sensor_dials_off (every
// guard sense, by its fr::SensorDialTable index), each with its plain name.
// Empty for any other setting (a text field).
struct SettingListOption {
    int32_t id;
    std::string name;
};
std::vector<SettingListOption> SettingListOptions(const SettingInfo& info);

// A List's ids (fr::ParseClientReqIds; dials_off: fr::ParseOffList; sensor_dials_off:
// the table indexes of fr::ParseSensorOffList's keys; entries that are not one are dropped).
std::set<int32_t> SettingListIds(const SettingInfo& info, std::string_view text);
// The List text with id added (on) or removed: "3, 4, 18", or for the
// never-changed lists their names ("EDP_PlayerHitPointsMax, ...", "cone_combat, ...").
std::string SettingListToggle(const SettingInfo& info, std::string_view text, int32_t id, bool on);
// Whether these ids still leave the shuffle enough to pick from
// (fr::ClientReqExcludeLeavesEnough, fr::PrestigeExcludeLeavesEnough); true
// for any other setting.
bool SettingListLeavesEnough(const SettingInfo& info, const std::set<int32_t>& ids);

} // namespace fr
