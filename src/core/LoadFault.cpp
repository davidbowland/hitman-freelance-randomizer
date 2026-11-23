#include "LoadFault.h"

#include <cinttypes>
#include <cstdio>

namespace fr {

namespace {

struct CodeName {
    uint32_t code;
    const char* name;
};

constexpr CodeName kCodes[] = {
    {0xC0000005u, "access violation"},
    {0xC000001Du, "illegal instruction"},
    {0xC00000FDu, "stack overflow"},
    {0xC0000374u, "heap corruption"},
    {0xC0000409u, "fast fail (stack buffer overrun)"},
    {0xC0000094u, "integer divide by zero"},
    {0xC0000096u, "privileged instruction"},
    {0xC0000006u, "in-page error"},
    {0xC000008Cu, "array bounds exceeded"},
    {0xE06D7363u, "C++ exception"},
};

const char* AccessVerb(uint64_t p_Kind) {
    switch (p_Kind) {
        case 0: return "reading";
        case 1: return "writing";
        case 8: return "executing";
        default: return "accessing";
    }
}

size_t Clamp(int p_Written, size_t p_Size) {
    if (p_Written < 0 || p_Size == 0)
        return 0;

    return static_cast<size_t>(p_Written) < p_Size ? static_cast<size_t>(p_Written) : p_Size - 1;
}

} // namespace

bool IsWatchedFaultCode(uint32_t p_Code) {
    return !FaultCodeName(p_Code).empty();
}

std::string_view FaultCodeName(uint32_t p_Code) {
    for (const CodeName& s_Code : kCodes)
        if (s_Code.code == p_Code)
            return s_Code.name;

    return {};
}

size_t FormatFaultLine(char* p_Out, size_t p_Size, const FaultReport& p_Report) {
    if (!p_Out || p_Size == 0)
        return 0;

    const std::string_view s_Name = FaultCodeName(p_Report.code);
    char s_Where[160];

    if (p_Report.module && *p_Report.module)
        std::snprintf(s_Where, sizeof(s_Where), "%s+0x%" PRIX64 " (0x%" PRIX64 ")", p_Report.module, p_Report.offset,
                      p_Report.address);
    else
        std::snprintf(s_Where, sizeof(s_Where), "0x%" PRIX64 " (in no module)", p_Report.address);

    char s_Access[64] = "";

    if (p_Report.hasAccess)
        std::snprintf(s_Access, sizeof(s_Access), ", %s 0x%016" PRIX64, AccessVerb(p_Report.accessKind),
                      p_Report.accessAddress);

    const int s_Written =
        std::snprintf(p_Out, p_Size,
                      "load fault #%u: %s: %.*s (%08X) at %s%s, thread %u; loading %s, last stage %s, %" PRIu64
                      " blueprint factories built since the load began",
                      p_Report.number, p_Report.kind ? p_Report.kind : "", static_cast<int>(s_Name.size()),
                      s_Name.empty() ? "" : s_Name.data(), p_Report.code, s_Where, s_Access, p_Report.thread,
                      p_Report.load ? p_Report.load : "",
                      p_Report.stage && *p_Report.stage ? p_Report.stage : "none yet", p_Report.factories);

    return Clamp(s_Written, p_Size);
}

size_t FormatStackLine(char* p_Out, size_t p_Size, const char* p_Module, uint64_t p_Offset) {
    if (!p_Out || p_Size == 0)
        return 0;

    return Clamp(std::snprintf(p_Out, p_Size, "  stack: %s+0x%" PRIX64, p_Module ? p_Module : "?", p_Offset), p_Size);
}

const char* ModuleFileName(const char* p_Path) {
    if (!p_Path)
        return "";

    const char* s_Name = p_Path;

    for (const char* s_At = p_Path; *s_At; ++s_At)
        if (*s_At == '\\' || *s_At == '/')
            s_Name = s_At + 1;

    return s_Name;
}

} // namespace fr
