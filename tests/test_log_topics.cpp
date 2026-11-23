#include "doctest.h"

#include "Config.h"
#include "LogTopics.h"
#include "SettingsTable.h"

#include <set>
#include <string_view>
#include <vector>

TEST_CASE("log topics: one Logs row per topic, in the same order") {
    std::vector<std::string_view> rows;

    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        if (info.group == fr::SettingGroup::Logs)
            rows.push_back(info.key);
    }

    std::vector<std::string_view> topics;

    for (const fr::LogTopic topic : fr::AllLogTopics())
        topics.push_back(fr::LogTopicKey(topic));

    CHECK(topics.size() == fr::kLogTopicCount);
    CHECK(rows == topics);
    CHECK(std::set<std::string_view>(topics.begin(), topics.end()).size() == topics.size());

    for (const fr::LogTopic topic : fr::AllLogTopics()) {
        const fr::SettingInfo* info = fr::FindSetting(fr::LogTopicKey(topic));
        REQUIRE(info != nullptr);
        CAPTURE(info->key);
        CHECK(info->type == fr::SettingType::Bool);
        // The switch Config holds for the topic is the row's.
        CHECK(fr::SettingBoolOr(*info, "") == fr::LogTopicSwitch(fr::Config {}, topic));
    }
}

TEST_CASE("log topics: every log (log_*) is on by default") {
    for (const fr::LogTopic topic : fr::AllLogTopics()) {
        const std::string_view key = fr::LogTopicKey(topic);
        CAPTURE(key);
        CHECK(key.substr(0, 4) == "log_");
        CHECK(fr::LogTopicSwitch(fr::Config {}, topic));
        CHECK(fr::FindSetting(key)->def == "true");
    }
}

TEST_CASE("log topics: ApplyLogTopics sets each topic from its switch") {
    fr::Config config;
    config.logMapVariants = false;
    config.logSafehouse = false;
    fr::ApplyLogTopics(config);

    CHECK_FALSE(fr::LogTopicOn(fr::LogTopic::MapVariants));
    CHECK_FALSE(fr::LogTopicOn(fr::LogTopic::Safehouse));
    CHECK(fr::LogTopicOn(fr::LogTopic::Loadout));
    CHECK(fr::LogTopicOn(fr::LogTopic::SupplierCrates));

    // Back to the defaults for the other tests.
    fr::ApplyLogTopics(fr::Config {});
    CHECK(fr::LogTopicOn(fr::LogTopic::MapVariants));
    CHECK(fr::LogTopicOn(fr::LogTopic::Safehouse));
    CHECK(fr::LogTopicOn(fr::LogTopic::SafeSpots));
}

TEST_CASE("log topics: every Logs row is a plain switch that writes to the log file") {
    for (const fr::LogTopic topic : fr::AllLogTopics()) {
        const fr::SettingInfo* info = fr::FindSetting(fr::LogTopicKey(topic));
        REQUIRE(info != nullptr);
        CAPTURE(info->key);
        CHECK(info->label.substr(0, 5) == "Log: ");
        CHECK(info->help.find("to the log file, for troubleshooting.") != std::string_view::npos);
    }

    // Every topic is one of its own: no two topics share a key, and none is Count.
    CHECK(fr::AllLogTopics().size() == fr::kLogTopicCount);

    for (const fr::LogTopic topic : fr::AllLogTopics())
        CHECK(topic != fr::LogTopic::Count);
}

TEST_CASE("log topics: every topic is in AllLogTopics once, in the enum's order") {
    size_t i = 0;

    for (const fr::LogTopic topic : fr::AllLogTopics())
        CHECK(static_cast<size_t>(topic) == i++);
}
