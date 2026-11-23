#include "doctest.h"

#include "LoadFault.h"

#include <cstring>
#include <string>

TEST_CASE("load fault: the watched codes") {
    CHECK(fr::IsWatchedFaultCode(0xC0000005u));
    CHECK(fr::IsWatchedFaultCode(0xC00000FDu));
    CHECK(fr::IsWatchedFaultCode(0xC0000409u));
    CHECK(fr::IsWatchedFaultCode(0xE06D7363u));
    CHECK_FALSE(fr::IsWatchedFaultCode(0x80000003u)); // breakpoint
    CHECK_FALSE(fr::IsWatchedFaultCode(0x80000001u)); // guard page
    CHECK_FALSE(fr::IsWatchedFaultCode(0x406D1388u)); // thread naming
    CHECK(fr::FaultCodeName(0xC0000005u) == "access violation");
    CHECK(fr::FaultCodeName(0x12345678u).empty());
}

TEST_CASE("load fault: a report line") {
    fr::FaultReport r;
    r.kind = "first chance (the game may still handle it)";
    r.number = 2;
    r.code = 0xC0000005u;
    r.address = 0x7FF612345678ull;
    r.module = "HITMAN3.exe";
    r.offset = 0x2345678;
    r.hasAccess = true;
    r.accessKind = 1;
    r.accessAddress = 0x10;
    r.thread = 4321;
    r.load = "badboy (full version)";
    r.stage = "eLoading_AssetsLoaded";
    r.factories = 812;
    char buf[512];
    const size_t n = fr::FormatFaultLine(buf, sizeof(buf), r);
    CHECK(n == std::strlen(buf));
    CHECK(std::string(buf) ==
          "load fault #2: first chance (the game may still handle it): access violation (C0000005) at "
          "HITMAN3.exe+0x2345678 (0x7FF612345678), writing 0x0000000000000010, thread 4321; loading badboy (full "
          "version), last stage eLoading_AssetsLoaded, 812 blueprint factories built since the load began");
}

TEST_CASE("load fault: no module, no access, no stage; truncation") {
    fr::FaultReport r;
    r.kind = "unhandled";
    r.number = 1;
    r.code = 0xC00000FDu;
    r.address = 0x1000;
    r.load = "icon (full version)";
    char buf[512];
    fr::FormatFaultLine(buf, sizeof(buf), r);
    CHECK(std::string(buf) ==
          "load fault #1: unhandled: stack overflow (C00000FD) at 0x1000 (in no module), thread 0; loading icon (full "
          "version), last stage none yet, 0 blueprint factories built since the load began");

    char small[16];
    const size_t n = fr::FormatFaultLine(small, sizeof(small), r);
    CHECK(n == 15);
    CHECK(std::string(small) == "load fault #1: ");
    CHECK(fr::FormatFaultLine(small, 0, r) == 0);
}

TEST_CASE("load fault: stack lines and module names") {
    char buf[64];
    fr::FormatStackLine(buf, sizeof(buf), "HITMAN3.exe", 0xABC);
    CHECK(std::string(buf) == "  stack: HITMAN3.exe+0xABC");
    CHECK(std::string(fr::ModuleFileName("Z:\\games\\Retail\\HITMAN3.exe")) == "HITMAN3.exe");
    CHECK(std::string(fr::ModuleFileName("/a/b.dll")) == "b.dll");
    CHECK(std::string(fr::ModuleFileName("plain.dll")) == "plain.dll");
    CHECK(std::string(fr::ModuleFileName(nullptr)).empty());
}
