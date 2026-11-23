#pragma once

#include <Logging.h>

#include <spdlog/logger.h>

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

// Opens <p_BaseName>.log next to the mod's DLL (truncated, after renaming the
// previous session's log to <p_BaseName>.prev.log), flushed on every line so a
// crash keeps the evidence. The logger name and the SDK-log prefix
// "[<p_BaseName>]" derive from p_BaseName. Only the first call counts.
void InitModLog(std::string_view p_BaseName);

// The folder of the mod's DLL, where its log is written (Retail/mods/); empty
// when it cannot be found.
std::filesystem::path ModDirectory();

// The mod's file logger. Before InitModLog (or if the file could not be
// opened) it has no sinks and drops messages. Write through Log, LogError,
// LogTo and LogIf, never directly.
spdlog::logger& ModLog();

// "[<base name>]", the SDK-log prefix set by InitModLog.
const std::string& ModLogPrefix();

// Whether the session's log reached its cap for topic lines (LogTo and LogIf
// lines are dropped from then on; ModLogCap.h). Any thread.
bool LogTruncated() noexcept;

namespace detail {

void WriteLog(std::string_view p_Line);
void WriteLogError(std::string_view p_Line);
void WriteLogTo(std::string_view p_Line);

} // namespace detail

// A line that is always written (the mod loading, the settings it read): the
// mod's log file (until it holds 8 MiB, ModLogCap.h) and the SDK log, at
// info level.
template <class... A>
void Log(fmt::format_string<A...> p_Format, A&&... p_Args) {
    detail::WriteLog(fmt::format(p_Format, std::forward<A>(p_Args)...));
}

// A failure, always written: the mod's log file (as "error: <line>", until it
// holds 8 MiB) and the SDK log, at error level.
template <class... A>
void LogError(fmt::format_string<A...> p_Format, A&&... p_Args) {
    detail::WriteLogError(fmt::format(p_Format, std::forward<A>(p_Args)...));
}

// A line of one log topic, written to the mod's log file only, while the
// topic's switch is on. p_Topic is the mod's topic enum (fr::LogTopic); the
// LogTopicOn(p_Topic) of its namespace says whether the switch is on. Dropped
// once the session's log reaches 7 MiB. The arguments are evaluated either
// way; only the formatting is skipped.
template <class T, class... A>
void LogTo(T p_Topic, fmt::format_string<A...> p_Format, A&&... p_Args) {
    if (LogTopicOn(p_Topic) && !LogTruncated())
        detail::WriteLogTo(fmt::format(p_Format, std::forward<A>(p_Args)...));
}

// A line like LogTo, but written only while p_On: for the lines of a feature
// that has its own setting rather than a log topic.
template <class... A>
void LogIf(bool p_On, fmt::format_string<A...> p_Format, A&&... p_Args) {
    if (p_On && !LogTruncated())
        detail::WriteLogTo(fmt::format(p_Format, std::forward<A>(p_Args)...));
}

// Runs p_Body; when it throws, logs "<p_Tag>: <p_What> failed: <why>" instead
// of letting the exception reach the engine (hooks and game callbacks).
// False when it threw.
template <class F>
bool Contained(std::string_view p_Tag, std::string_view p_What, F&& p_Body) noexcept {
    try {
        p_Body();
        return true;
    }
    catch (const std::exception& e) {
        Log("{}: {} failed: {}", p_Tag, p_What, e.what());
    }
    catch (...) {
        Log("{}: {} failed: unknown exception", p_Tag, p_What);
    }

    return false;
}

// Every other Log call in src/ is one of the lines that are always written
// (the mod loading, the settings it read, errors not yet written with
// LogError); tests/log_always.txt lists them and tests/test_log_gates.cpp
// fails when a new one is not listed.
