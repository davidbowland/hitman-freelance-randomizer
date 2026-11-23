#include "doctest.h"

#include "ModLogCap.h"

namespace {

// A line of p_Bytes bytes in the file (its prefix and line end included).
size_t LineOf(size_t p_Bytes) {
    return p_Bytes - static_cast<size_t>(LogCap::kLineOverhead);
}

} // namespace

TEST_CASE("log cap: topic lines stop at the topic cap, after one truncation line") {
    LogCap cap(200, 400); // room for two 100-byte topic lines
    CHECK(cap.CountTopic(LineOf(100)) == LogCap::Verdict::Write);
    CHECK(cap.CountTopic(LineOf(100)) == LogCap::Verdict::Write);
    CHECK_FALSE(cap.Truncated());
    CHECK(cap.CountTopic(1) == LogCap::Verdict::Truncate);
    CHECK(cap.Truncated());
    CHECK(cap.CountTopic(1) == LogCap::Verdict::Drop);
    CHECK(cap.CountTopic(0) == LogCap::Verdict::Drop);
    CHECK(cap.CountAlways(1) == LogCap::Verdict::Write); // not stopped
}

TEST_CASE("log cap: always-written lines go on past the topic cap, up to the hard cap, then one last line") {
    LogCap cap(200, 400);
    CHECK(cap.CountAlways(LineOf(150)) == LogCap::Verdict::Write);
    CHECK_FALSE(cap.Truncated()); // counted only
    CHECK(cap.CountTopic(LineOf(100)) == LogCap::Verdict::Truncate);
    CHECK(cap.CountAlways(LineOf(100)) == LogCap::Verdict::Write); // 350 of 400
    CHECK(cap.CountAlways(LineOf(50)) == LogCap::Verdict::Write);  // 400 of 400
    CHECK(cap.CountAlways(1) == LogCap::Verdict::Truncate);
    CHECK(cap.CountAlways(1) == LogCap::Verdict::Drop); // stopped
    CHECK(cap.CountTopic(1) == LogCap::Verdict::Drop);
}

TEST_CASE("log cap: always-written lines alone also stop topic lines at the hard cap") {
    LogCap cap(100, 200);
    CHECK(cap.CountAlways(LineOf(200)) == LogCap::Verdict::Write);
    CHECK(cap.CountAlways(1) == LogCap::Verdict::Truncate);
    CHECK(cap.Truncated());
    CHECK(cap.CountTopic(1) == LogCap::Verdict::Drop);
}

TEST_CASE("log cap: 7 MiB of topic lines, 8 MiB in all per session") {
    CHECK(LogCap::kTopicBytes == 7ull * 1024 * 1024);
    CHECK(LogCap::kMaxBytes == 8ull * 1024 * 1024);
    LogCap cap;
    CHECK(cap.TopicBytes() == LogCap::kTopicBytes);
    CHECK(cap.MaxBytes() == LogCap::kMaxBytes);
    const uint64_t fit = LogCap::kTopicBytes / 100;

    for (uint64_t i = 0; i < fit; ++i)
        REQUIRE(cap.CountTopic(LineOf(100)) == LogCap::Verdict::Write);

    CHECK(cap.CountTopic(LineOf(100)) == LogCap::Verdict::Truncate);

    // The always-written lines fill the rest, up to 8 MiB.
    uint64_t written = fit * 100 + 100;

    while (written + 100 <= LogCap::kMaxBytes) {
        REQUIRE(cap.CountAlways(LineOf(100)) == LogCap::Verdict::Write);
        written += 100;
    }

    CHECK(cap.CountAlways(LineOf(100)) == LogCap::Verdict::Truncate);
    CHECK(cap.CountAlways(1) == LogCap::Verdict::Drop); // stopped
}
