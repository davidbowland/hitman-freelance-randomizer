#include "LoadFaultWatch.h"

#include "ModLog.h"
#include "../core/LoadFault.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace load_fault {

namespace {

namespace fs = std::filesystem;

constexpr const char* kFileName = "FreelancerRandomizer-loadfault.txt";
constexpr const char* kPrevFileName = "FreelancerRandomizer-loadfault.prev.txt";
constexpr uint32_t kMaxReports = 16;  // fault lines per load
constexpr uint32_t kStackReports = 6; // the first ones also scan their stack
constexpr size_t kStackFrames = 12;   // code addresses listed per scan
constexpr size_t kStackScanBytes = 0x3000;
constexpr size_t kMaxLines = 96; // kept for the log at Disarm
constexpr size_t kLineSize = 384;

struct Range {
    uintptr_t base = 0;
    uintptr_t end = 0;
    char name[64] = {};
};

std::atomic<bool> g_Armed {false};
std::atomic<bool> g_Busy {false};
std::atomic<uint32_t> g_Reports {0};
std::atomic<uint32_t> g_Own {0};    // in the mod's own module (its guarded reads)
std::atomic<uint32_t> g_Missed {0}; // past the cap, or while another thread was writing
std::atomic<uint64_t> g_Factories {0};
std::atomic<const char*> g_Stage {nullptr};

PVOID g_Handler = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER g_PrevFilter = nullptr;
bool g_FilterSet = false;
HANDLE g_File = INVALID_HANDLE_VALUE;
char g_Load[96] = {};
wchar_t g_Path[MAX_PATH * 2] = {};
Range g_Game; // the game's exe
Range g_Mod;  // this DLL

// Written by the handler (under g_Busy), read at Disarm after g_Armed is off.
char g_Lines[kMaxLines][kLineSize];
std::atomic<size_t> g_LineCount {0};

bool RangeOf(HMODULE p_Module, Range* p_Out) {
    if (!p_Module)
        return false;

    const auto* s_Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(p_Module);

    if (s_Dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    const auto* s_Nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(p_Module) + s_Dos->e_lfanew);

    if (s_Nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    p_Out->base = reinterpret_cast<uintptr_t>(p_Module);
    p_Out->end = p_Out->base + s_Nt->OptionalHeader.SizeOfImage;
    char s_Path[MAX_PATH] = {};
    GetModuleFileNameA(p_Module, s_Path, MAX_PATH);
    strncpy_s(p_Out->name, sizeof(p_Out->name), fr::ModuleFileName(s_Path), _TRUNCATE);
    return true;
}

bool Inside(const Range& p_Range, uintptr_t p_Address) {
    return p_Range.base && p_Address >= p_Range.base && p_Address < p_Range.end;
}

// One line to the file (with CRLF) and to the kept lines. Handler only, under g_Busy.
void Emit(const char* p_Line, size_t p_Length) {
    if (g_File != INVALID_HANDLE_VALUE) {
        DWORD s_Written = 0;
        WriteFile(g_File, p_Line, static_cast<DWORD>(p_Length), &s_Written, nullptr);
        WriteFile(g_File, "\r\n", 2, &s_Written, nullptr);
    }

    const size_t s_Index = g_LineCount.load(std::memory_order_relaxed);

    if (s_Index < kMaxLines) {
        const size_t s_Length = std::min(p_Length, kLineSize - 1);
        std::memcpy(g_Lines[s_Index], p_Line, s_Length);
        g_Lines[s_Index][s_Length] = '\0';
        g_LineCount.store(s_Index + 1, std::memory_order_release);
    }
}

void Report(EXCEPTION_POINTERS* p_Info, const char* p_Kind, bool p_Count) {
    const EXCEPTION_RECORD* s_Record = p_Info->ExceptionRecord;
    const uint32_t s_Code = s_Record->ExceptionCode;
    const auto s_Address = reinterpret_cast<uintptr_t>(s_Record->ExceptionAddress);

    fr::FaultReport s_Report;
    s_Report.kind = p_Kind;
    s_Report.number =
        p_Count ? g_Reports.fetch_add(1, std::memory_order_relaxed) + 1 : g_Reports.load(std::memory_order_relaxed);
    s_Report.code = s_Code;
    s_Report.address = s_Address;
    s_Report.thread = GetCurrentThreadId();
    s_Report.load = g_Load;
    const char* s_Stage = g_Stage.load(std::memory_order_relaxed);
    s_Report.stage = s_Stage ? s_Stage : "";
    s_Report.factories = g_Factories.load(std::memory_order_relaxed);

    // The faulting module: the game's or ours from the ranges, any other through the loader.
    static char s_ModulePath[MAX_PATH];
    HMODULE s_Module = nullptr;

    if (Inside(g_Game, s_Address)) {
        s_Report.module = g_Game.name;
        s_Report.offset = s_Address - g_Game.base;
    }
    else if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(s_Address), &s_Module) &&
             s_Module) {
        s_ModulePath[0] = '\0';
        GetModuleFileNameA(s_Module, s_ModulePath, MAX_PATH);
        s_Report.module = fr::ModuleFileName(s_ModulePath);
        s_Report.offset = s_Address - reinterpret_cast<uintptr_t>(s_Module);
    }

    if ((s_Code == 0xC0000005u || s_Code == 0xC0000006u) && s_Record->NumberParameters >= 2) {
        s_Report.hasAccess = true;
        s_Report.accessKind = s_Record->ExceptionInformation[0];
        s_Report.accessAddress = s_Record->ExceptionInformation[1];
    }

    static char s_Line[kLineSize];
    Emit(s_Line, fr::FormatFaultLine(s_Line, sizeof(s_Line), s_Report));

    // Likely return addresses: words on the stack inside the game's exe or this DLL. Not for a stack overflow (no
    // room to work in, and the stack is what broke).
    if (s_Code == 0xC00000FDu || s_Report.number > kStackReports || !p_Info->ContextRecord)
        return;

    const auto* s_Tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const uintptr_t s_Top = p_Info->ContextRecord->Rsp;
    const uintptr_t s_Base = reinterpret_cast<uintptr_t>(s_Tib->StackBase);

    if (s_Top == 0 || s_Top >= s_Base || (s_Top & 7) != 0)
        return;

    const uintptr_t s_End = std::min(s_Base, s_Top + kStackScanBytes);
    size_t s_Frames = 0;

    for (uintptr_t s_At = s_Top; s_At + 8 <= s_End && s_Frames < kStackFrames; s_At += 8) {
        const uintptr_t s_Value = *reinterpret_cast<const uintptr_t*>(s_At);
        const Range* s_Range = Inside(g_Game, s_Value) ? &g_Game : Inside(g_Mod, s_Value) ? &g_Mod : nullptr;

        if (!s_Range)
            continue;

        ++s_Frames;
        Emit(s_Line, fr::FormatStackLine(s_Line, sizeof(s_Line), s_Range->name, s_Value - s_Range->base));
    }
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* p_Info) {
    if (!g_Armed.load(std::memory_order_relaxed) || !p_Info || !p_Info->ExceptionRecord)
        return EXCEPTION_CONTINUE_SEARCH;

    const uint32_t s_Code = p_Info->ExceptionRecord->ExceptionCode;

    if (!fr::IsWatchedFaultCode(s_Code))
        return EXCEPTION_CONTINUE_SEARCH;

    // The mod's own guarded reads (__try) fault on purpose now and then: counted, not listed.
    if (Inside(g_Mod, reinterpret_cast<uintptr_t>(p_Info->ExceptionRecord->ExceptionAddress))) {
        g_Own.fetch_add(1, std::memory_order_relaxed);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (g_Reports.load(std::memory_order_relaxed) >= kMaxReports || g_Busy.exchange(true)) {
        g_Missed.fetch_add(1, std::memory_order_relaxed);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Disarm may have run since the check above: it closes the file once g_Busy is clear.
    if (!g_Armed.load()) {
        g_Busy.store(false, std::memory_order_release);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    Report(p_Info, "first chance (the game may still handle it)", true);
    g_Busy.store(false, std::memory_order_release);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Nobody handled it: the game ends now (unless a filter before ours recovers).
LONG WINAPI Unhandled(EXCEPTION_POINTERS* p_Info) {
    if (g_Armed.load(std::memory_order_relaxed) && p_Info && p_Info->ExceptionRecord && !g_Busy.exchange(true)) {
        // Re-checked under g_Busy, as in Handler.
        if (g_Armed.load())
            Report(p_Info, "UNHANDLED (the game ends here)", false);

        g_Busy.store(false, std::memory_order_release);
    }

    return g_PrevFilter ? g_PrevFilter(p_Info) : EXCEPTION_CONTINUE_SEARCH;
}

void CloseFile(bool p_Delete) {
    if (g_File != INVALID_HANDLE_VALUE) {
        CloseHandle(g_File);
        g_File = INVALID_HANDLE_VALUE;
    }

    if (p_Delete && g_Path[0])
        DeleteFileW(g_Path);
}

// The fault file the last session left (it died while a load was watched): into the log, then kept as .prev.
void ReportLastSession(const fs::path& p_Dir) {
    std::error_code s_Error;
    const fs::path s_Path = p_Dir / kFileName;

    if (!fs::exists(s_Path, s_Error))
        return;

    std::ifstream s_In(s_Path);
    std::string s_Line;
    size_t s_Count = 0;

    while (std::getline(s_In, s_Line) && s_Count < 200) {
        if (!s_Line.empty() && s_Line.back() == '\r')
            s_Line.pop_back();

        Log("load fault (last session): {}", s_Line);
        ++s_Count;
    }

    s_In.close();
    fs::remove(p_Dir / kPrevFileName, s_Error);
    fs::rename(s_Path, p_Dir / kPrevFileName, s_Error);
    Log("load fault (last session): the game ended while a full map variant was loading; {} lines above, from {} "
        "(kept as {})",
        s_Count, kFileName, kPrevFileName);
}

} // namespace

void Install(bool p_Watch) noexcept {
    try {
        if (g_Handler)
            return;

        const fs::path s_Dir = ModDirectory();

        if (s_Dir.empty())
            return;

        ReportLastSession(s_Dir);

        // Only a full variant's load is ever watched: without their package no handler is needed.
        if (!p_Watch)
            return;

        const std::wstring s_Path = (s_Dir / kFileName).wstring();

        if (s_Path.size() >= std::size(g_Path))
            return;

        wcsncpy_s(g_Path, std::size(g_Path), s_Path.c_str(), _TRUNCATE);
        RangeOf(GetModuleHandleA(nullptr), &g_Game);
        HMODULE s_Mod = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&Install), &s_Mod);
        RangeOf(s_Mod, &g_Mod);
        g_Handler = AddVectoredExceptionHandler(0, &Handler);
        Log("load fault watch: ready ({}); full map variant loads are watched",
            g_Handler ? "handler added" : "the handler could not be added");
    }
    catch (...) {
    }
}

void Uninstall() noexcept {
    // A watched load the mod is unloaded during: stopped as at play (the filter put back when it is still ours,
    // the fault file removed), so the next start reports no fault.
    Disarm("mod unload");

    if (g_Handler) {
        RemoveVectoredExceptionHandler(g_Handler);
        g_Handler = nullptr;
    }

    // A handler already running on another thread finishes before the file closes.
    for (int i = 0; i < 1000 && g_Busy.load(std::memory_order_acquire); ++i)
        Sleep(1);

    CloseFile(false);
}

void Arm(std::string_view p_Load) noexcept {
    try {
        if (!g_Handler)
            return;

        if (g_Armed.load(std::memory_order_relaxed))
            Disarm("another watched load began");

        const size_t s_Length = std::min(p_Load.size(), sizeof(g_Load) - 1);
        std::memcpy(g_Load, p_Load.data(), s_Length);
        g_Load[s_Length] = '\0';
        g_Reports.store(0, std::memory_order_relaxed);
        g_Own.store(0, std::memory_order_relaxed);
        g_Missed.store(0, std::memory_order_relaxed);
        g_Factories.store(0, std::memory_order_relaxed);
        g_LineCount.store(0, std::memory_order_relaxed);

        g_File =
            CreateFileW(g_Path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (g_File != INVALID_HANDLE_VALUE) {
            const std::string s_Head = fmt::format("watching {} (from the scene swap until play)\r\n", g_Load);
            DWORD s_Written = 0;
            WriteFile(g_File, s_Head.data(), static_cast<DWORD>(s_Head.size()), &s_Written, nullptr);
        }

        // Set again for each watched load: the game may have put its own filter in since.
        g_PrevFilter = SetUnhandledExceptionFilter(&Unhandled);
        g_FilterSet = true;
        g_Armed.store(true, std::memory_order_release);
        Log("load fault watch: {}: watching the load ({}{})", g_Load,
            g_File != INVALID_HANDLE_VALUE ? "faults go to " : "the fault file could not be opened: ", kFileName);
    }
    catch (...) {
    }
}

void Disarm(std::string_view p_Why) noexcept {
    try {
        if (!g_Armed.exchange(false))
            return;

        // Let a handler that is writing now finish (one that takes g_Busy later sees g_Armed off).
        for (int i = 0; i < 1000 && g_Busy.load(); ++i)
            Sleep(1);

        if (g_FilterSet) {
            const LPTOP_LEVEL_EXCEPTION_FILTER s_Now = SetUnhandledExceptionFilter(g_PrevFilter);

            // Another filter went in after ours: it stays.
            if (s_Now != &Unhandled)
                SetUnhandledExceptionFilter(s_Now);

            g_FilterSet = false;
        }

        const uint32_t s_Reports = g_Reports.load(std::memory_order_relaxed);
        Log("load fault watch: {}: {}: {} faults outside the mod while loading{}{}, {} blueprint factories built",
            g_Load, p_Why, s_Reports,
            g_Own.load(std::memory_order_relaxed)
                ? fmt::format(" ({} more in the mod's own guarded reads)", g_Own.load(std::memory_order_relaxed))
                : std::string(),
            g_Missed.load(std::memory_order_relaxed)
                ? fmt::format(" ({} not listed)", g_Missed.load(std::memory_order_relaxed))
                : std::string(),
            g_Factories.load(std::memory_order_relaxed));

        const size_t s_Lines = std::min(g_LineCount.load(std::memory_order_acquire), kMaxLines);

        for (size_t i = 0; i < s_Lines; ++i)
            Log("load fault watch: {}", g_Lines[i]);

        // In the log now; the file only matters when the game dies while a load is watched.
        CloseFile(true);
    }
    catch (...) {
    }
}

void SetStage(const char* p_Stage) noexcept {
    g_Stage.store(p_Stage, std::memory_order_relaxed);
}

void StageHandled(const char* p_Stage) noexcept {
    if (!g_Armed.load(std::memory_order_relaxed))
        return;

    try {
        Log("load fault watch: {}: the mod's {} handlers are done ({} blueprint factories built so far); the game "
            "goes on",
            g_Load, p_Stage ? p_Stage : "?", g_Factories.load(std::memory_order_relaxed));
    }
    catch (...) {
    }
}

void NoteFactoryBuilt() noexcept {
    if (g_Armed.load(std::memory_order_relaxed))
        g_Factories.fetch_add(1, std::memory_order_relaxed);
}

} // namespace load_fault
