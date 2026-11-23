#pragma once

// The load fault watch's pure parts (src/mod/LoadFaultWatch.h has the engine
// parts): which exception codes it reports and how a report line reads. The
// lines are formatted into a caller's buffer without allocating, since they
// are written from an exception handler, where the heap may be broken.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fr {

// The exception codes that end the game when nobody handles them: access
// violation, illegal instruction, stack overflow, heap corruption, fast fail
// (stack buffer overrun), integer divide by zero, privileged instruction, in-page
// error, array bounds exceeded, and a C++ throw (0xE06D7363). Breakpoints,
// debugger notifications, guard pages and the like are not reported.
bool IsWatchedFaultCode(uint32_t p_Code);

// Its usual name ("access violation", "C++ exception", ...), or "" for none.
std::string_view FaultCodeName(uint32_t p_Code);

// One exception seen while a load was watched.
struct FaultReport {
    const char* kind = ""; // "first chance (the game may still handle it)" or "unhandled"
    uint32_t number = 0;   // 1, 2, ... in this load
    uint32_t code = 0;
    uint64_t address = 0;    // the faulting instruction
    const char* module = ""; // its module's file name, or "" when in none
    uint64_t offset = 0;     // address - the module's base
    bool hasAccess = false;  // an access violation's or in-page error's read/write and target
    uint64_t accessKind = 0; // 0 read, 1 write, 8 execute
    uint64_t accessAddress = 0;
    uint32_t thread = 0;
    const char* load = "";  // what was loading ("badboy (full version)")
    const char* stage = ""; // the last loading stage the game set
    uint64_t factories = 0; // template blueprint factories built since the watch was armed
};

// "load fault #1: first chance ...: access violation (C0000005) at HITMAN3.exe+0x1234 (0x7FF6...), writing
// 0x0000000000000010, thread 1234; loading badboy (full version), last stage eLoading_AssetsLoaded, 812 blueprint
// factories built since the load began". Writes at most p_Size - 1 characters and a terminating NUL (truncated when
// too long); returns the number written (without the NUL).
size_t FormatFaultLine(char* p_Out, size_t p_Size, const FaultReport& p_Report);

// "  stack: HITMAN3.exe+0x1234": one code address found on the faulting thread's stack (a likely return address).
size_t FormatStackLine(char* p_Out, size_t p_Size, const char* p_Module, uint64_t p_Offset);

// A module path's file name (after the last '\\' or '/').
const char* ModuleFileName(const char* p_Path);

} // namespace fr
