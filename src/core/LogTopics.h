#pragma once

// The log file's switches: each line the mod writes about a feature belongs
// to one topic, and the topic's setting (SettingGroup::Logs, "Advanced:
// Logs") turns its lines on or off. src/mod writes such a line with
// LogTo(fr::LogTopic::..., ...) (common/ModLog.h); the essential lines (the
// mod loading, the settings it read, errors, the load fault recorder) use Log
// and are always written; tests/log_always.txt lists them.
//
// Each switch (log_*, "Log: ...", on by default) is for troubleshooting: one
// per feature a player can name (a settings group or two), holding the
// feature's decisions and every reason it did nothing or failed, enough to
// tell from a player's log what went wrong. Two features log under their own
// setting rather than a topic (LogIf): footsteps_investigate and
// backup_guard, whose lines are written whenever they run.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace fr {

struct Config;

enum class LogTopic : uint8_t {
    // In the Settings tab's section order.
    Missions,         // log_missions: level loads, campaign seeds, which features run
    Loadout,          // log_loadout: random loadout, free items, lockpick when you bring none
    SupplierCrates,   // log_supplier_crates: supplier steering and force pick, supply crate items and guns
    Weapons,          // log_weapons: weapon rarities, the holsterable mace, legal sniper rifles
    MapVariants,      // log_map_variants: variant picks, loading, Diana's briefing
    SafeSpots,        // log_safe_spots: where safes can appear
    DoorLocks,        // log_door_locks: the door lock swap (its plan, why nothing moves)
    Targets,          // log_targets: target pool, enforcer and guard weapon swaps
    AiDifficulty,     // log_ai_difficulty: the rolled AI difficulty values, guard senses, footsteps, sick spots
    CommonObjectives, // log_common_objectives: the common objective shuffle
    PrestigeCards,    // log_prestige_cards: the prestige card shuffle
    Money,            // log_money: objective payouts and loot amounts as the mod sets them, payout read faults
    Safehouse,        // log_safehouse: the bird's nest coin and the fishing script
    Count,
};

constexpr size_t kLogTopicCount = static_cast<size_t>(LogTopic::Count);

// The [settings] key of a topic's switch.
std::string_view LogTopicKey(LogTopic p_Topic);
// Every topic, in the order the Logs group lists them.
std::span<const LogTopic> AllLogTopics();

// Whether the topic's lines are written. Every topic is on until
// ApplyLogTopics runs. Thread-safe (the hooks run on several threads).
bool LogTopicOn(LogTopic p_Topic);
void SetLogTopicOn(LogTopic p_Topic, bool p_On);
// Each topic from its Config switch (after the settings are read).
void ApplyLogTopics(const Config& p_Config);
// The switch's value in p_Config.
bool LogTopicSwitch(const Config& p_Config, LogTopic p_Topic);

} // namespace fr
