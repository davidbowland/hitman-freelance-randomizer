#pragma once

// The log file's size cap per session (ModLog.h): topic lines (LogTo, LogIf)
// stop at 7 MiB, after one truncation line; always-written lines (Log,
// LogError: few, and the evidence that matters) go on until the file holds
// 8 MiB, then one last line and nothing more. Any thread.

#include <atomic>
#include <cstddef>
#include <cstdint>

class LogCap {
public:
    static constexpr uint64_t kTopicBytes = 7ull << 20;
    static constexpr uint64_t kMaxBytes = 8ull << 20;
    // Each line's "[YYYY-MM-DD HH:MM:SS.mmm] " prefix (26 characters) and its line end (spdlog writes CRLF on
    // Windows).
#ifdef _WIN32
    static constexpr uint64_t kLineOverhead = 28;
#else
    static constexpr uint64_t kLineOverhead = 27;
#endif

    enum class Verdict {
        Write,    // under the cap: write the line
        Truncate, // this line crossed the cap: write the cap's line instead
        Drop,     // past the cap
    };

    explicit constexpr LogCap(uint64_t p_TopicBytes = kTopicBytes, uint64_t p_MaxBytes = kMaxBytes)
        : m_Topic(p_TopicBytes), m_Max(p_MaxBytes) {}

    // An always-written line of p_Size characters: written while the file stays within the hard cap.
    Verdict CountAlways(size_t p_Size) {
        if (m_Stopped.load(std::memory_order_relaxed))
            return Verdict::Drop;

        const uint64_t s_Size = p_Size + kLineOverhead;

        if (m_Bytes.fetch_add(s_Size, std::memory_order_relaxed) + s_Size <= m_Max)
            return Verdict::Write;

        m_Truncated.store(true, std::memory_order_relaxed);
        return m_Stopped.exchange(true) ? Verdict::Drop : Verdict::Truncate;
    }

    // A topic line of p_Size characters: written up to the topic cap.
    Verdict CountTopic(size_t p_Size) {
        if (m_Truncated.load(std::memory_order_relaxed))
            return Verdict::Drop;

        const uint64_t s_Size = p_Size + kLineOverhead;

        if (m_Bytes.fetch_add(s_Size, std::memory_order_relaxed) + s_Size <= m_Topic)
            return Verdict::Write;

        return m_Truncated.exchange(true) ? Verdict::Drop : Verdict::Truncate;
    }

    // Topic lines are dropped (a cap was reached).
    bool Truncated() const { return m_Truncated.load(std::memory_order_relaxed); }

    uint64_t TopicBytes() const { return m_Topic; }
    uint64_t MaxBytes() const { return m_Max; }

private:
    uint64_t m_Topic;
    uint64_t m_Max;
    std::atomic<uint64_t> m_Bytes = 0;
    std::atomic<bool> m_Truncated = false;
    std::atomic<bool> m_Stopped = false;
};
