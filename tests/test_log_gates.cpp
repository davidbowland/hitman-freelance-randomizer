#include "doctest.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Every log line goes through a switch (LogTo with a log topic, or LogIf with a
// setting; common/ModLog.h) except the few that are always written: failures
// (LogError, or Log), the mod loading, the settings it read, the load fault
// recorder. The raw Log( ones are listed in tests/log_always.txt, one
// "<path>: <the call's first line, trimmed>" entry per call in src/.

namespace {

std::string Trim(std::string_view p_Text) {
    size_t s_Begin = 0;
    size_t s_End = p_Text.size();

    while (s_Begin < s_End && std::isspace(static_cast<unsigned char>(p_Text[s_Begin])))
        ++s_Begin;

    while (s_End > s_Begin && std::isspace(static_cast<unsigned char>(p_Text[s_End - 1])))
        --s_End;

    return std::string(p_Text.substr(s_Begin, s_End - s_Begin));
}

// A call of Log itself: "Log(" not preceded by a name character (LogTo(,
// ModLog(, fr::Log(, s_X.Log(, p->Log( are something else).
size_t RawLogCalls(std::string_view p_Line) {
    size_t s_Count = 0;

    for (size_t s_At = p_Line.find("Log("); s_At != std::string_view::npos; s_At = p_Line.find("Log(", s_At + 1)) {
        const char s_Before = s_At ? p_Line[s_At - 1] : ' ';

        if (!std::isalnum(static_cast<unsigned char>(s_Before)) && s_Before != '_' && s_Before != ':' &&
            s_Before != '.' && s_Before != '>')
            ++s_Count;
    }

    return s_Count;
}

// "<path>: <trimmed line>" -> how many raw Log( calls, over src/ (ModLog.h, where Log is defined, excepted).
std::map<std::string, int> RawLogCallsInSrc() {
    const std::filesystem::path s_Root = FR_REPO_ROOT;
    std::map<std::string, int> s_Out;

    for (const auto& s_Entry : std::filesystem::recursive_directory_iterator(s_Root / "src")) {
        const std::filesystem::path& s_Path = s_Entry.path();

        if (!s_Entry.is_regular_file() || (s_Path.extension() != ".cpp" && s_Path.extension() != ".h"))
            continue;

        const std::string s_Rel = std::filesystem::relative(s_Path, s_Root).generic_string();

        if (s_Rel == "src/common/ModLog.h")
            continue;

        std::ifstream s_In(s_Path, std::ios::binary);
        std::string s_Line;

        while (std::getline(s_In, s_Line)) {
            const std::string s_Trimmed = Trim(s_Line);

            if (s_Trimmed.rfind("//", 0) == 0)
                continue;

            if (const size_t s_Calls = RawLogCalls(s_Trimmed))
                s_Out[s_Rel + ": " + s_Trimmed] += static_cast<int>(s_Calls);
        }
    }

    return s_Out;
}

std::map<std::string, int> AllowList() {
    std::ifstream s_In(std::filesystem::path(FR_REPO_ROOT) / "tests" / "log_always.txt", std::ios::binary);
    REQUIRE_MESSAGE(s_In.good(), "cannot read tests/log_always.txt");
    std::map<std::string, int> s_Out;
    std::string s_Line;

    while (std::getline(s_In, s_Line)) {
        const std::string s_Trimmed = Trim(s_Line);

        if (!s_Trimmed.empty() && s_Trimmed[0] != '#')
            ++s_Out[s_Trimmed];
    }

    return s_Out;
}

} // namespace

TEST_CASE("log gates: RawLogCalls finds Log( calls only") {
    CHECK(RawLogCalls("Log(\"x\");") == 1);
    CHECK(RawLogCalls("if (a) Log(\"x\"); else Log(\"y\");") == 2);
    CHECK(RawLogCalls("LogTo(fr::LogTopic::Missions, \"x\");") == 0);
    CHECK(RawLogCalls("LogIf(m_On, \"x\");") == 0);
    CHECK(RawLogCalls("ModLog().info(\"x\"); LogFault(\"x\", 0); s_X.Log(1); p->Log(1); fr::Log(1);") == 0);
}

TEST_CASE("log gates: every line written whatever the switches say is in tests/log_always.txt") {
    const std::map<std::string, int> s_Found = RawLogCallsInSrc();
    const std::map<std::string, int> s_Allowed = AllowList();
    REQUIRE_FALSE(s_Found.empty());

    std::vector<std::string> s_Unlisted;
    std::vector<std::string> s_Stale;

    for (const auto& [s_Key, s_Count] : s_Found) {
        const auto s_It = s_Allowed.find(s_Key);
        const int s_Listed = s_It == s_Allowed.end() ? 0 : s_It->second;

        for (int i = s_Listed; i < s_Count; ++i)
            s_Unlisted.push_back(s_Key);
    }

    for (const auto& [s_Key, s_Count] : s_Allowed) {
        const auto s_It = s_Found.find(s_Key);
        const int s_Present = s_It == s_Found.end() ? 0 : s_It->second;

        for (int i = s_Present; i < s_Count; ++i)
            s_Stale.push_back(s_Key);
    }

    std::ostringstream s_Message;

    for (const std::string& s_Key : s_Unlisted)
        s_Message << "\n  not gated: " << s_Key;

    for (const std::string& s_Key : s_Stale)
        s_Message << "\n  listed but gone: " << s_Key;

    // A new line belongs behind a switch: LogTo(<topic>, ...) or LogIf(<setting>, ...).
    // Only an error, the mod loading or a settings line goes into tests/log_always.txt.
    CHECK_MESSAGE(s_Unlisted.empty(),
                  "raw Log( calls not in tests/log_always.txt (use LogTo or LogIf):" << s_Message.str());
    CHECK_MESSAGE(s_Stale.empty(), "tests/log_always.txt entries no longer in src/:" << s_Message.str());
}
