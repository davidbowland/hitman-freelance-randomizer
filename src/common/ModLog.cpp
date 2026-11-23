#include "ModLog.h"

#include "ModLogCap.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <system_error>

namespace {

// An error line's marker in the file, after the time.
constexpr std::string_view kErrorMarker = "error: ";

LogCap g_Cap;

std::shared_ptr<spdlog::logger>& LoggerSlot() {
    static std::shared_ptr<spdlog::logger> s_Logger = std::make_shared<spdlog::logger>("ModLog");
    return s_Logger;
}

std::string& PrefixSlot() {
    static std::string s_Prefix;
    return s_Prefix;
}

// Folder of the module (DLL) that contains this function.
std::filesystem::path ModuleDirectory() {
    HMODULE s_Module = nullptr;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ModuleDirectory), &s_Module)) {
        return {};
    }

    wchar_t s_Path[MAX_PATH * 4] = {};
    const DWORD s_Length = GetModuleFileNameW(s_Module, s_Path, static_cast<DWORD>(std::size(s_Path)));

    if (s_Length == 0 || s_Length >= std::size(s_Path))
        return {};

    return std::filesystem::path(s_Path).parent_path();
}

// Renames an existing log to p_Previous, replacing an older one there. False when the old log is still at
// p_Path, so opening p_Path would wipe it.
bool KeepPreviousLog(const std::filesystem::path& p_Path, const std::filesystem::path& p_Previous) {
    std::error_code s_Error;

    if (!std::filesystem::exists(p_Path, s_Error))
        return !s_Error;

    std::filesystem::remove(p_Previous, s_Error);
    std::filesystem::rename(p_Path, p_Previous, s_Error);

    if (s_Error) {
        Logger::Error("{} could not keep the previous log: {}", ModLogPrefix(), s_Error.message());
        return false;
    }

    return true;
}

void OpenModLog(std::string_view p_BaseName) {
    try {
        const std::string s_BaseName(p_BaseName);
        PrefixSlot() = "[" + s_BaseName + "]";

        const auto s_Directory = ModuleDirectory();

        if (s_Directory.empty()) {
            Logger::Error("{} could not locate the mod folder; file log disabled", ModLogPrefix());
            return;
        }

        auto s_Path = s_Directory / (s_BaseName + ".log");

        // The old log could not be moved aside: it stays as it is, and this session writes a fresh file next to it
        // (the size cap counts from an empty file).
        if (!KeepPreviousLog(s_Path, s_Directory / (s_BaseName + ".prev.log"))) {
            s_Path = s_Directory / (s_BaseName + ".new.log");
            Logger::Error("{} writing this session's log to {}", ModLogPrefix(), s_Path.string());
        }

        auto s_Sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(s_Path.string(), true);
        auto s_Logger = std::make_shared<spdlog::logger>(s_BaseName, std::move(s_Sink));
        s_Logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
        s_Logger->set_level(spdlog::level::trace);
        // Every line is flushed: the log's main use is the evidence before a crash.
        s_Logger->flush_on(spdlog::level::trace);
        LoggerSlot() = std::move(s_Logger);
    }
    catch (const std::exception& e) {
        Logger::Error("{} could not open the log file: {}", ModLogPrefix(), e.what());
    }
}

} // namespace

void InitModLog(std::string_view p_BaseName) {
    static std::once_flag s_Once;
    std::call_once(s_Once, OpenModLog, p_BaseName);
}

std::filesystem::path ModDirectory() {
    try {
        return ModuleDirectory();
    }
    catch (...) {
        return {};
    }
}

spdlog::logger& ModLog() {
    return *LoggerSlot();
}

const std::string& ModLogPrefix() {
    return PrefixSlot();
}

bool LogTruncated() noexcept {
    return g_Cap.Truncated();
}

namespace detail {

// The file part of an always-written line (p_Marker before it), up to the hard cap.
static void WriteAlways(std::string_view p_Marker, std::string_view p_Line, spdlog::level::level_enum p_Level) {
    switch (g_Cap.CountAlways(p_Marker.size() + p_Line.size())) {
        case LogCap::Verdict::Write: ModLog().log(p_Level, "{}{}", p_Marker, p_Line); break;
        case LogCap::Verdict::Truncate:
            ModLog().info("log stopped at {} MiB: nothing more is written to this file this session",
                          g_Cap.MaxBytes() >> 20);
            break;
        case LogCap::Verdict::Drop: break;
    }
}

void WriteLog(std::string_view p_Line) {
    WriteAlways({}, p_Line, spdlog::level::info);
    Logger::Info("{} {}", ModLogPrefix(), p_Line);
}

void WriteLogError(std::string_view p_Line) {
    WriteAlways(kErrorMarker, p_Line, spdlog::level::err);
    Logger::Error("{} {}", ModLogPrefix(), p_Line);
}

void WriteLogTo(std::string_view p_Line) {
    switch (g_Cap.CountTopic(p_Line.size())) {
        case LogCap::Verdict::Write: ModLog().info("{}", p_Line); break;
        case LogCap::Verdict::Truncate:
            ModLog().info("log truncated at {} MiB: topic lines dropped from here; errors still written",
                          g_Cap.TopicBytes() >> 20);
            break;
        case LogCap::Verdict::Drop: break;
    }
}

} // namespace detail
