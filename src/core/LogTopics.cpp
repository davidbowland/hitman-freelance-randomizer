#include "LogTopics.h"

#include "Config.h"

#include <array>
#include <atomic>

namespace fr {

namespace {

// Off flags, so every topic starts on (zero-initialized).
std::array<std::atomic<bool>, kLogTopicCount> g_Off {};

// The Logs group's order (the enum's order).
constexpr std::array<LogTopic, kLogTopicCount> kAll = {
    LogTopic::Missions,     LogTopic::Loadout,          LogTopic::SupplierCrates, LogTopic::Weapons,
    LogTopic::MapVariants,  LogTopic::SafeSpots,        LogTopic::DoorLocks,      LogTopic::Targets,
    LogTopic::AiDifficulty, LogTopic::CommonObjectives, LogTopic::PrestigeCards,  LogTopic::Money,
    LogTopic::Safehouse,
};

size_t Index(LogTopic p_Topic) {
    return static_cast<size_t>(p_Topic) < kLogTopicCount ? static_cast<size_t>(p_Topic) : 0;
}

} // namespace

std::string_view LogTopicKey(LogTopic p_Topic) {
    switch (p_Topic) {
        case LogTopic::Missions: return "log_missions";
        case LogTopic::Loadout: return "log_loadout";
        case LogTopic::SupplierCrates: return "log_supplier_crates";
        case LogTopic::Weapons: return "log_weapons";
        case LogTopic::MapVariants: return "log_map_variants";
        case LogTopic::SafeSpots: return "log_safe_spots";
        case LogTopic::DoorLocks: return "log_door_locks";
        case LogTopic::Targets: return "log_targets";
        case LogTopic::AiDifficulty: return "log_ai_difficulty";
        case LogTopic::CommonObjectives: return "log_common_objectives";
        case LogTopic::PrestigeCards: return "log_prestige_cards";
        case LogTopic::Money: return "log_money";
        case LogTopic::Safehouse: return "log_safehouse";
        case LogTopic::Count: break;
    }

    return {};
}

std::span<const LogTopic> AllLogTopics() {
    return kAll;
}

bool LogTopicOn(LogTopic p_Topic) {
    return !g_Off[Index(p_Topic)].load(std::memory_order_relaxed);
}

void SetLogTopicOn(LogTopic p_Topic, bool p_On) {
    g_Off[Index(p_Topic)].store(!p_On, std::memory_order_relaxed);
}

bool LogTopicSwitch(const Config& p_Config, LogTopic p_Topic) {
    switch (p_Topic) {
        case LogTopic::Missions: return p_Config.logMissions;
        case LogTopic::Loadout: return p_Config.logLoadout;
        case LogTopic::SupplierCrates: return p_Config.logSupplierCrates;
        case LogTopic::Weapons: return p_Config.logWeapons;
        case LogTopic::MapVariants: return p_Config.logMapVariants;
        case LogTopic::SafeSpots: return p_Config.logSafeSpots;
        case LogTopic::DoorLocks: return p_Config.logDoorLocks;
        case LogTopic::Targets: return p_Config.logTargets;
        case LogTopic::AiDifficulty: return p_Config.logAiDifficulty;
        case LogTopic::CommonObjectives: return p_Config.logCommonObjectives;
        case LogTopic::PrestigeCards: return p_Config.logPrestigeCards;
        case LogTopic::Money: return p_Config.logMoney;
        case LogTopic::Safehouse: return p_Config.logSafehouse;
        case LogTopic::Count: break;
    }

    return true;
}

void ApplyLogTopics(const Config& p_Config) {
    for (const LogTopic s_Topic : kAll)
        SetLogTopicOn(s_Topic, LogTopicSwitch(p_Config, s_Topic));
}

} // namespace fr
