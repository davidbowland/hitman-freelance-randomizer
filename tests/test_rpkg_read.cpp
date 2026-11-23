#include "doctest.h"

#include "PatchPackage.h"
#include "RpkgRead.h"
#include "support/PatchPackageTestUtil.h"

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using pkg::BuildTestRpkg;
using pkg::PlainTestResource;
using pkg::ReadRpkgIndex;
using pkg::ReadRpkgResource;
using pkg::RpkgEntry;
using pkg::RpkgIndex;
using pkg::TestRpkgResource;

std::istringstream Stream(const std::vector<uint8_t>& p_Bytes) {
    return std::istringstream(std::string(p_Bytes.begin(), p_Bytes.end()), std::ios::binary);
}

RpkgIndex Index(const std::vector<uint8_t>& p_Bytes, bool p_IsPatch) {
    std::istringstream s_In = Stream(p_Bytes);
    return ReadRpkgIndex(s_In, p_IsPatch);
}

std::optional<std::vector<uint8_t>> Resource(const std::vector<uint8_t>& p_Bytes, const RpkgEntry& p_Entry) {
    std::istringstream s_In = Stream(p_Bytes);
    return ReadRpkgResource(s_In, p_Entry);
}

std::vector<uint8_t> Bytes(std::string_view p_Text) {
    return std::vector<uint8_t>(p_Text.begin(), p_Text.end());
}

// "abcd" then a match 4 back of 8 bytes: "abcdabcdabcd".
TestRpkgResource Lz4Resource(uint64_t p_Hash, bool p_Xored) {
    TestRpkgResource s_Out;
    s_Out.hash = p_Hash;
    s_Out.type = "TBLU";
    s_Out.stored = {0x44, 'a', 'b', 'c', 'd', 0x04, 0x00};
    s_Out.finalSize = 12;
    s_Out.compressed = true;
    s_Out.xored = p_Xored;
    s_Out.refs = {0x1111, 0x2222};
    return s_Out;
}

// A package with one LZ4 resource (XOR-ciphered, with references) and one
// stored as is.
std::vector<uint8_t> TwoResourcePackage(bool p_Version1, bool p_IsPatch, std::vector<uint64_t> p_Deletions = {}) {
    const std::vector<TestRpkgResource> s_Resources = {Lz4Resource(0xA1, true), PlainTestResource(0xB2, "plain")};
    return BuildTestRpkg(p_Version1, p_IsPatch, p_Deletions, s_Resources);
}

// The index of a package holding p_Hashes (as named), its deletions p_Deletions.
std::pair<std::string, RpkgIndex> Named(std::string p_Name, std::vector<uint64_t> p_Hashes,
                                        std::vector<uint64_t> p_Deletions = {}) {
    RpkgIndex s_Index;
    s_Index.ok = true;
    s_Index.deletions = std::move(p_Deletions);

    for (const uint64_t s_Hash : p_Hashes) {
        RpkgEntry s_Entry;
        s_Entry.hash = s_Hash;
        s_Entry.type = "TEMP";
        s_Index.entries.push_back(s_Entry);
    }

    return {std::move(p_Name), std::move(s_Index)};
}

// The package LocateNewest finds p_Hash in, of chunk 26 (no error).
std::optional<std::string> NewestPackage(const std::vector<std::pair<std::string, RpkgIndex>>& p_Packages,
                                         uint64_t p_Hash) {
    const pkg::LocateResult s_Result = pkg::LocateNewest(p_Packages, 26, p_Hash);
    CHECK(s_Result.error.empty());

    if (!s_Result.found)
        return std::nullopt;

    CHECK(s_Result.found->entry.hash == p_Hash);
    return s_Result.found->package;
}

// LocateNewest's error for p_Hash in chunk 26 (nothing found).
std::string LocateError(const std::vector<std::pair<std::string, RpkgIndex>>& p_Packages, uint64_t p_Hash) {
    const pkg::LocateResult s_Result = pkg::LocateNewest(p_Packages, 26, p_Hash);
    CHECK_FALSE(s_Result.found);
    return s_Result.error;
}

void PutU32At(std::vector<uint8_t>& p_File, size_t p_At, uint32_t p_Value) {
    for (int i = 0; i < 4; ++i)
        p_File[p_At + static_cast<size_t>(i)] = static_cast<uint8_t>(p_Value >> (8 * i));
}

} // namespace

TEST_CASE("ReadRpkgIndex reads version 1 and 2 base and patch packages") {
    for (const bool s_Version1 : {true, false}) {
        for (const bool s_IsPatch : {false, true}) {
            CAPTURE(s_Version1);
            CAPTURE(s_IsPatch);
            const std::vector<uint64_t> s_Deletions =
                s_IsPatch ? std::vector<uint64_t> {0xD1, 0xD2} : std::vector<uint64_t> {};
            const std::vector<uint8_t> s_File = TwoResourcePackage(s_Version1, s_IsPatch, s_Deletions);
            const RpkgIndex s_Index = Index(s_File, s_IsPatch);

            REQUIRE_MESSAGE(s_Index.ok, s_Index.error);
            CHECK(s_Index.version1 == s_Version1);
            CHECK(s_Index.deletions == s_Deletions);
            REQUIRE(s_Index.entries.size() == 2);

            const RpkgEntry& s_Lz4 = s_Index.entries[0];
            CHECK(s_Lz4.hash == 0xA1);
            CHECK(s_Lz4.type == "TBLU");
            CHECK(s_Lz4.stored == 7);
            CHECK(s_Lz4.xored);
            CHECK(s_Lz4.finalSize == 12);

            const RpkgEntry& s_Plain = s_Index.entries[1];
            CHECK(s_Plain.hash == 0xB2);
            CHECK(s_Plain.type == "TEMP");
            CHECK(s_Plain.stored == 0);
            CHECK_FALSE(s_Plain.xored);
            CHECK(s_Plain.finalSize == 5);
            CHECK(s_Plain.offset == s_Lz4.offset + 7);
            CHECK(s_Plain.offset + 5 == s_File.size());

            CHECK(Resource(s_File, s_Lz4) == Bytes("abcdabcdabcd"));
            CHECK(Resource(s_File, s_Plain) == Bytes("plain"));
        }
    }
}

TEST_CASE("ReadRpkgIndex and ReadRpkgResource read the packages the mod builds") {
    pkg::PackageResource s_First;
    s_First.hash = 0x0011223344556677ull;
    s_First.type = "TEMP";
    s_First.data = Bytes(std::string(300, 'x') + "tail");
    s_First.refs.push_back({pkg::kRefFlagsNormal, 0x99});
    pkg::PackageResource s_Second;
    s_Second.hash = 0x42;
    s_Second.type = "JSON";
    s_Second.data = Bytes("{}");
    const std::vector<pkg::PackageResource> s_Resources = {s_First, s_Second};
    const std::vector<uint8_t> s_File = pkg::BuildPatchPackage(s_Resources);

    const RpkgIndex s_Index = Index(s_File, true);
    REQUIRE_MESSAGE(s_Index.ok, s_Index.error);
    CHECK(s_Index.version1);
    REQUIRE(s_Index.entries.size() == 2);
    CHECK(s_Index.entries[0].type == "TEMP");
    CHECK(Resource(s_File, s_Index.entries[0]) == s_First.data);
    CHECK(Resource(s_File, s_Index.entries[1]) == s_Second.data);
}

TEST_CASE("ReadRpkgIndex reads a package with no resources") {
    const std::vector<uint64_t> s_Deletions = {0x5};
    const RpkgIndex s_Index = Index(BuildTestRpkg(false, true, s_Deletions, {}), true);
    REQUIRE_MESSAGE(s_Index.ok, s_Index.error);
    CHECK(s_Index.deletions == s_Deletions);
    CHECK(s_Index.entries.empty());
}

TEST_CASE("ReadRpkgIndex fails on a corrupt header or index") {
    const std::vector<uint8_t> s_Good = TwoResourcePackage(false, true, {0xD1});

    SUBCASE("empty and bad magic") {
        CHECK_FALSE(Index({}, true).ok);
        std::vector<uint8_t> s_File = s_Good;
        s_File[0] = 'X';
        const RpkgIndex s_Index = Index(s_File, true);
        CHECK_FALSE(s_Index.ok);
        CHECK_FALSE(s_Index.error.empty());
        CHECK(s_Index.entries.empty());
    }

    SUBCASE("every truncation before the end of the info table") {
        // v2 header 13 + 12, deletion count 4 + 8, index 40, info 24 + 22 + 24.
        const size_t s_HeadEnd = 13 + 12 + 4 + 8 + 40 + 70;
        REQUIRE(Index(std::vector<uint8_t>(s_Good.begin(), s_Good.begin() + s_HeadEnd), true).ok);

        for (size_t s_Size = 0; s_Size < s_HeadEnd; ++s_Size) {
            CAPTURE(s_Size);
            CHECK_FALSE(Index(std::vector<uint8_t>(s_Good.begin(), s_Good.begin() + s_Size), true).ok);
        }
    }

    SUBCASE("index size not 20 per resource") {
        std::vector<uint8_t> s_File = s_Good;
        s_File[13 + 4] = 41;
        CHECK_FALSE(Index(s_File, true).ok);
    }

    SUBCASE("a huge resource or deletion count") {
        std::vector<uint8_t> s_File = s_Good;
        s_File[13 + 3] = 0x7F; // count 0x7F000002, index size still 40
        CHECK_FALSE(Index(s_File, true).ok);

        s_File = s_Good;
        s_File[13 + 12 + 3] = 0xFF; // deletion count 0xFF000001
        CHECK_FALSE(Index(s_File, true).ok);
    }

    SUBCASE("info table size that does not match its resources") {
        std::vector<uint8_t> s_File = s_Good;
        s_File[13 + 8] = 71;
        CHECK_FALSE(Index(s_File, true).ok);
    }

    SUBCASE("info table size shorter than 24 bytes per resource, near the file size, or huge") {
        // The info size field of the v2 header; the info table starts at 77.
        constexpr size_t kInfoSize = 13 + 8;

        for (const uint32_t s_Size :
             {47u, static_cast<uint32_t>(s_Good.size() - 77), static_cast<uint32_t>(s_Good.size() - 76),
              pkg::kMaxRpkgSize, 0xFFFFFFF0u, 0xFFFFFFFFu}) {
            CAPTURE(s_Size);
            std::vector<uint8_t> s_File = s_Good;
            PutU32At(s_File, kInfoSize, s_Size);
            CHECK_FALSE(Index(s_File, true).ok);
        }
    }

    SUBCASE("reference table size near UINT32_MAX") {
        for (const uint32_t s_Size : {0xFFFFFFFFu, 0xFFFFFFF0u, 0x80000000u}) {
            CAPTURE(s_Size);
            std::vector<uint8_t> s_File = s_Good;
            PutU32At(s_File, 13 + 12 + 4 + 8 + 40 + 4, s_Size);
            CHECK_FALSE(Index(s_File, true).ok);
        }
    }

    SUBCASE("reference table size that does not match its count") {
        std::vector<uint8_t> s_File = s_Good;
        const size_t s_Info = 13 + 12 + 4 + 8 + 40;
        s_File[s_Info + 24] = 3; // count 3 where the size holds 2
        CHECK_FALSE(Index(s_File, true).ok);
    }

    SUBCASE("a base package read as a patch, or the other way") {
        const std::vector<uint8_t> s_Base = TwoResourcePackage(true, false);
        CHECK_FALSE(Index(s_Base, true).ok);
        CHECK_FALSE(Index(s_Good, false).ok);
    }
}

TEST_CASE("ReadRpkgResource fails on out-of-range or corrupt data") {
    const std::vector<uint8_t> s_File = TwoResourcePackage(true, false);
    const RpkgIndex s_Index = Index(s_File, false);
    REQUIRE(s_Index.ok);
    const RpkgEntry s_Lz4 = s_Index.entries[0];
    const RpkgEntry s_Plain = s_Index.entries[1];

    SUBCASE("offset past the end") {
        RpkgEntry s_Entry = s_Plain;
        s_Entry.offset = s_File.size() + 1;
        CHECK_FALSE(Resource(s_File, s_Entry));
        s_Entry.offset = ~0ull;
        CHECK_FALSE(Resource(s_File, s_Entry));
    }

    SUBCASE("data running past the end") {
        RpkgEntry s_Entry = s_Plain;
        s_Entry.finalSize = 6;
        CHECK_FALSE(Resource(s_File, s_Entry));
        s_Entry = s_Lz4;
        s_Entry.stored = 0x3FFFFFFF;
        CHECK_FALSE(Resource(s_File, s_Entry));
    }

    SUBCASE("LZ4 data that does not decode to the final size") {
        RpkgEntry s_Entry = s_Lz4;
        s_Entry.finalSize = 11;
        CHECK_FALSE(Resource(s_File, s_Entry));
        s_Entry.finalSize = 13;
        CHECK_FALSE(Resource(s_File, s_Entry));
        s_Entry.finalSize = 0xFFFFFFFFu;
        CHECK_FALSE(Resource(s_File, s_Entry));
    }

    SUBCASE("stored or final size over the limit") {
        RpkgEntry s_Entry = s_Plain;
        s_Entry.finalSize = pkg::kMaxRpkgSize + 1;
        CHECK_FALSE(Resource(s_File, s_Entry));
        s_Entry = s_Lz4;
        s_Entry.finalSize = pkg::kMaxRpkgSize + 1;
        CHECK_FALSE(Resource(s_File, s_Entry));
        s_Entry.stored = pkg::kMaxRpkgSize + 1;
        CHECK_FALSE(Resource(s_File, s_Entry));
        const std::vector<uint8_t> s_Block = {0x10, 'a'};
        CHECK(pkg::Lz4Decode(s_Block, 1) == Bytes("a"));
        CHECK_FALSE(pkg::Lz4Decode(s_Block, size_t(pkg::kMaxRpkgSize) + 1));
    }

    SUBCASE("LZ4 data with a match before the start") {
        TestRpkgResource s_Bad = Lz4Resource(0xA1, false);
        s_Bad.stored[5] = 0x05; // 5 back after 4 bytes
        const std::vector<TestRpkgResource> s_Resources = {s_Bad};
        const std::vector<uint8_t> s_BadFile = BuildTestRpkg(true, false, {}, s_Resources);
        const RpkgIndex s_BadIndex = Index(s_BadFile, false);
        REQUIRE(s_BadIndex.ok);
        CHECK_FALSE(Resource(s_BadFile, s_BadIndex.entries[0]));
    }

    SUBCASE("XOR flag that does not match the data") {
        RpkgEntry s_Entry = s_Lz4;
        s_Entry.xored = false;
        CHECK_FALSE(Resource(s_File, s_Entry));
    }

    SUBCASE("the stream reads fine after a failed read") {
        std::istringstream s_In = Stream(s_File);
        RpkgEntry s_Entry = s_Plain;
        s_Entry.offset = s_File.size() + 10;
        CHECK_FALSE(ReadRpkgResource(s_In, s_Entry));
        CHECK(ReadRpkgResource(s_In, s_Plain) == Bytes("plain"));
        CHECK(ReadRpkgIndex(s_In, false).ok);
    }
}

TEST_CASE("corrupting any byte of a package never reads out of range") {
    for (const bool s_Version1 : {true, false}) {
        const std::vector<uint8_t> s_Good = TwoResourcePackage(s_Version1, true, {0xD1});

        for (size_t s_At = 0; s_At < s_Good.size(); ++s_At) {
            for (const uint8_t s_Value : {uint8_t(0x00), uint8_t(0x7F), uint8_t(0x80), uint8_t(0xFF)}) {
                std::vector<uint8_t> s_File = s_Good;
                s_File[s_At] = s_Value;
                const RpkgIndex s_Index = Index(s_File, true);

                if (!s_Index.ok)
                    continue;

                for (const RpkgEntry& s_Entry : s_Index.entries) {
                    const std::optional<std::vector<uint8_t>> s_Data = Resource(s_File, s_Entry);

                    if (s_Data)
                        CHECK(s_Data->size() == s_Entry.finalSize);
                }
            }
        }
    }
}

TEST_CASE("LocateNewest finds the copy the game mounts") {
    const std::vector<std::pair<std::string, RpkgIndex>> s_Packages = {
        // Given out of order: the patch number orders them.
        Named("chunk26patch2.rpkg", {0xA, 0xE}, {0xB}),
        Named("chunk26patch100.rpkg", {0xA, 0xC, 0xF}),
        Named("chunk26.rpkg", {0xA, 0xB, 0xC, 0xD}),
        Named("CHUNK26PATCH1.RPKG", {0xA, 0xE}),
        Named("chunk26patch3.rpkg", {0xE}, {0xC, 0xE}),
        Named("chunk26patch290.rpkg", {0xD}),
        Named("chunk26patch10.rpkg", {0xB}),
    };

    CHECK(NewestPackage(s_Packages, 0xA) == "chunk26patch2.rpkg");
    // Deleted in patch 2, added again in patch 10 (after patch 3 by number).
    CHECK(NewestPackage(s_Packages, 0xB) == "chunk26patch10.rpkg");
    // Deleted in patch 3; patch 100 is a mod's.
    CHECK_FALSE(NewestPackage(s_Packages, 0xC));
    // Only the base: patch 290 is a mod's.
    CHECK(NewestPackage(s_Packages, 0xD) == "chunk26.rpkg");
    // A patch's deletions apply before its own entries.
    CHECK(NewestPackage(s_Packages, 0xE) == "chunk26patch3.rpkg");
    CHECK_FALSE(NewestPackage(s_Packages, 0xF));
    CHECK_FALSE(NewestPackage(s_Packages, 0x77));
}

TEST_CASE("LocateNewest returns the entry from the package's index") {
    std::vector<std::pair<std::string, RpkgIndex>> s_Packages = {Named("chunk6.rpkg", {0x1, 0x2})};
    s_Packages[0].second.entries[1].offset = 1234;
    s_Packages[0].second.entries[1].finalSize = 56;
    const pkg::LocateResult s_Result = pkg::LocateNewest(s_Packages, 6, 0x2);
    CHECK(s_Result.error.empty());
    REQUIRE(s_Result.found);
    CHECK(s_Result.found->package == "chunk6.rpkg");
    CHECK(s_Result.found->entry.offset == 1234);
    CHECK(s_Result.found->entry.finalSize == 56);
}

TEST_CASE("LocateNewest leaves out other chunks and names that are not packages") {
    std::vector<std::pair<std::string, RpkgIndex>> s_Packages = {
        Named("chunk26.rpkg", {0xA}),
        Named("chunk2.rpkg", {0xB}),
        Named("chunk2patch1.rpkg", {0xA}, {0xA}),
        Named("chunk260patch1.rpkg", {0xA}, {0xA}),
        Named("chunk26patch1.rpkg.bak", {0xA}, {0xA}),
        Named("notes.txt", {0xA}),
    };
    // Unreadable, but not chunk 26's or not a package.
    for (size_t i = 1; i < s_Packages.size(); ++i)
        s_Packages[i].second.ok = i == 1;

    CHECK(NewestPackage(s_Packages, 0xA) == "chunk26.rpkg");
    CHECK_FALSE(NewestPackage(s_Packages, 0xB));
}

TEST_CASE("LocateNewest fails when the copy the game mounts cannot be told") {
    SUBCASE("an unreadable IOI patch of the chunk") {
        std::vector<std::pair<std::string, RpkgIndex>> s_Packages = {
            Named("chunk26.rpkg", {0xA}),
            Named("chunk26patch1.rpkg", {}),
        };
        s_Packages[1].second.ok = false;
        s_Packages[1].second.error = "bad magic";
        CHECK(LocateError(s_Packages, 0xA) == "chunk26patch1.rpkg cannot be read: bad magic");
    }

    SUBCASE("an unreadable base package") {
        std::vector<std::pair<std::string, RpkgIndex>> s_Packages = {Named("chunk26.rpkg", {0xA})};
        s_Packages[0].second.ok = false;
        CHECK_FALSE(LocateError(s_Packages, 0xA).empty());
    }

    SUBCASE("an unreadable mod's patch is left out") {
        std::vector<std::pair<std::string, RpkgIndex>> s_Packages = {
            Named("chunk26.rpkg", {0xA}),
            Named("chunk26patch290.rpkg", {}),
        };
        s_Packages[1].second.ok = false;
        CHECK(NewestPackage(s_Packages, 0xA) == "chunk26.rpkg");
    }

    SUBCASE("no base package") {
        CHECK(LocateError({Named("chunk26patch1.rpkg", {0xA})}, 0xA) == "no chunk26.rpkg");
        CHECK(LocateError({}, 0xA) == "no chunk26.rpkg");
    }

    SUBCASE("two packages with the same patch number, in any order") {
        CHECK(LocateError(
                  {Named("chunk26.rpkg", {}), Named("chunk26patch1.rpkg", {0xA}), Named("chunk26patch01.rpkg", {0xA})},
                  0xA) == "chunk26patch01.rpkg and chunk26patch1.rpkg have the same patch number");
        CHECK(LocateError(
                  {Named("chunk26patch01.rpkg", {0xA}), Named("chunk26.rpkg", {}), Named("chunk26patch1.rpkg", {0xA})},
                  0xA) == "chunk26patch01.rpkg and chunk26patch1.rpkg have the same patch number");
    }
}

TEST_CASE("HoldsOnly over a package index") {
    const std::vector<uint64_t> s_Own = {0xA1, 0xB2};
    const std::vector<uint64_t> s_Former = {0xC3};
    const std::vector<uint64_t> s_None;
    const auto s_Holds = [&](const std::vector<uint8_t>& p_File, bool p_IsPatch = true) {
        return pkg::HoldsOnly(Index(p_File, p_IsPatch), s_Own, s_Former);
    };

    CHECK(s_Holds(TwoResourcePackage(true, true)));
    CHECK(s_Holds(pkg::BuildPatchRpkg(0xC3, "JSON", "{}")));
    CHECK_FALSE(pkg::HoldsOnly(Index(pkg::BuildPatchRpkg(0xC3, "JSON", "{}"), true), s_Own, s_None));
    // Version 2, deletions, a foreign or repeated hash, nothing held, unreadable.
    CHECK_FALSE(s_Holds(TwoResourcePackage(false, true)));
    CHECK_FALSE(s_Holds(TwoResourcePackage(true, true, {0xA1})));
    CHECK_FALSE(s_Holds(pkg::BuildPatchRpkg(0xD4, "JSON", "{}")));
    const std::vector<TestRpkgResource> s_Twice = {PlainTestResource(0xA1, "x"), PlainTestResource(0xA1, "y")};
    CHECK_FALSE(s_Holds(BuildTestRpkg(true, true, {}, s_Twice)));
    CHECK_FALSE(s_Holds(BuildTestRpkg(true, true, {}, {})));
    CHECK_FALSE(s_Holds({}));
}
