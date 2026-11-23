#include "doctest.h"

#include "LandslideFull.h"
#include "PatchPackage.h"
#include "support/PatchPackageTestUtil.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

uint32_t U32(const std::vector<uint8_t>& p_Data, size_t p_At) {
    return p_Data[p_At] | p_Data[p_At + 1] << 8 | p_Data[p_At + 2] << 16 |
           static_cast<uint32_t>(p_Data[p_At + 3]) << 24;
}

uint64_t U64(const std::vector<uint8_t>& p_Data, size_t p_At) {
    return U32(p_Data, p_At) | static_cast<uint64_t>(U32(p_Data, p_At + 4)) << 32;
}

void Put32(std::vector<uint8_t>& p_Out, uint32_t p_Value) {
    for (int i = 0; i < 4; ++i)
        p_Out.push_back(static_cast<uint8_t>(p_Value >> (8 * i)));
}

void Put64(std::vector<uint8_t>& p_Out, uint64_t p_Value) {
    for (int i = 0; i < 8; ++i)
        p_Out.push_back(static_cast<uint8_t>(p_Value >> (8 * i)));
}

std::vector<uint8_t> Bytes(std::string_view p_Text) {
    return std::vector<uint8_t>(p_Text.begin(), p_Text.end());
}

// A fake BIN1 resource: header with a big-endian data size, then that much
// data and a trailing segment.
std::vector<uint8_t> FakeBin1(uint32_t p_DataSize, uint32_t p_Tail) {
    std::vector<uint8_t> s_Out = {'B', 'I', 'N', '1', 0, 8, 2, 0};
    s_Out.push_back(static_cast<uint8_t>(p_DataSize >> 24));
    s_Out.push_back(static_cast<uint8_t>(p_DataSize >> 16));
    s_Out.push_back(static_cast<uint8_t>(p_DataSize >> 8));
    s_Out.push_back(static_cast<uint8_t>(p_DataSize));
    s_Out.insert(s_Out.end(), 4, 0);

    for (uint32_t i = 0; i < p_DataSize + p_Tail; ++i)
        s_Out.push_back(static_cast<uint8_t>(i * 7));

    return s_Out;
}

// Two resources the way the Landslide package will hold them: a TEMP with a
// reference table (its blueprint and a template) and a TBLU without.
std::vector<pkg::PackageResource> TwoResources() {
    pkg::PackageResource s_Temp;
    s_Temp.hash = 0x00D1C948471CA87Full;
    s_Temp.type = "TEMP";
    s_Temp.data = FakeBin1(300, 40);
    s_Temp.memSize = 300;
    s_Temp.refs = {{0x1F, 0x00213F98DB592E9Eull}, {0x1F, 0x002325F864882245ull}, {0x5F, 0x00E9F09C3B030590ull}};

    pkg::PackageResource s_Tblu;
    s_Tblu.hash = 0x00B8E74F36928376ull;
    s_Tblu.type = "TBLU";
    s_Tblu.data = FakeBin1(20, 0);
    s_Tblu.memSize = 20;

    return {s_Temp, s_Tblu};
}

} // namespace

TEST_CASE("patch package: one resource without references has the exact layout") {
    const std::vector<uint8_t> s_Rpkg = pkg::BuildPatchRpkg(0x0011D1E068124651ull, "JSON", "{\"a\":1}");

    // Header 20, index 20, info 24 (no reference table), then the payload.
    REQUIRE(s_Rpkg.size() == 64 + 1 + 7);
    CHECK(std::string_view(reinterpret_cast<const char*>(s_Rpkg.data()), 4) == "GKPR");
    CHECK(U32(s_Rpkg, 4) == 1);
    CHECK(U32(s_Rpkg, 8) == 20);
    CHECK(U32(s_Rpkg, 12) == 24);
    CHECK(U32(s_Rpkg, 16) == 0);
    CHECK(U64(s_Rpkg, 20) == 0x0011D1E068124651ull);
    CHECK(U64(s_Rpkg, 28) == 64);
    CHECK(U32(s_Rpkg, 36) == (0x80000000u | 8u));
    CHECK(std::string_view(reinterpret_cast<const char*>(&s_Rpkg[40]), 4) == "NOSJ");
    CHECK(U32(s_Rpkg, 44) == 0);
    CHECK(U32(s_Rpkg, 48) == 0);
    CHECK(U32(s_Rpkg, 52) == 7);
    CHECK(U32(s_Rpkg, 56) == pkg::kNoMemSize);
    CHECK(U32(s_Rpkg, 60) == pkg::kNoMemSize);

    pkg::PackageResource s_Same;
    s_Same.hash = 0x0011D1E068124651ull;
    s_Same.type = "JSON";
    s_Same.data = Bytes("{\"a\":1}");
    CHECK(pkg::BuildPatchPackage(std::span<const pkg::PackageResource>(&s_Same, 1)) == s_Rpkg);
}

TEST_CASE("patch package: several resources with reference tables") {
    const std::vector<pkg::PackageResource> s_In = TwoResources();
    const std::vector<uint8_t> s_Rpkg = pkg::BuildPatchPackage(s_In);

    const uint32_t s_InfoSize = (24 + 4 + 9 * 3) + 24;
    CHECK(U32(s_Rpkg, 4) == 2);
    CHECK(U32(s_Rpkg, 8) == 40);
    CHECK(U32(s_Rpkg, 12) == s_InfoSize);
    CHECK(U32(s_Rpkg, 16) == 0);

    // Data offsets follow each other after the info table.
    const uint64_t s_First = U64(s_Rpkg, 28);
    const uint32_t s_FirstStored = U32(s_Rpkg, 36) & 0x3FFFFFFFu;
    CHECK(s_First == 20 + 40 + s_InfoSize);
    CHECK(U64(s_Rpkg, 48) == s_First + s_FirstStored);
    CHECK(U64(s_Rpkg, 48) + (U32(s_Rpkg, 56) & 0x3FFFFFFFu) == s_Rpkg.size());

    // The first info entry: type, ref table size, final size, memory sizes,
    // then the count with its format bits, the flags and the hashes.
    const size_t s_Info = 60;
    CHECK(std::string_view(reinterpret_cast<const char*>(&s_Rpkg[s_Info]), 4) == "PMET");
    CHECK(U32(s_Rpkg, s_Info + 4) == 4 + 9 * 3);
    CHECK(U32(s_Rpkg, s_Info + 12) == s_In[0].data.size());
    CHECK(U32(s_Rpkg, s_Info + 16) == 300);
    CHECK(U32(s_Rpkg, s_Info + 20) == pkg::kNoMemSize);
    CHECK(U32(s_Rpkg, s_Info + 24) == 0xC0000003u);
    CHECK(s_Rpkg[s_Info + 28] == 0x1F);
    CHECK(s_Rpkg[s_Info + 30] == 0x5F);
    CHECK(U64(s_Rpkg, s_Info + 31) == 0x00213F98DB592E9Eull);
    CHECK(U64(s_Rpkg, s_Info + 47) == 0x00E9F09C3B030590ull);

    const pkg::ParsedPackage s_Parsed = pkg::ParsePatchPackage(s_Rpkg);
    REQUIRE(s_Parsed.ok);
    CHECK(s_Parsed.version1);
    CHECK(s_Parsed.deletions.empty());
    REQUIRE(s_Parsed.resources.size() == 2);

    for (size_t i = 0; i < 2; ++i) {
        CHECK(s_Parsed.resources[i].hash == s_In[i].hash);
        CHECK(s_Parsed.resources[i].type == s_In[i].type);
        CHECK(s_Parsed.resources[i].data == s_In[i].data);
        CHECK(s_Parsed.resources[i].memSize == s_In[i].memSize);
        CHECK(s_Parsed.resources[i].vmemSize == s_In[i].vmemSize);
        CHECK(s_Parsed.resources[i].refs == s_In[i].refs);
    }

    const std::array<uint64_t, 2> s_Both {0x00B8E74F36928376ull, 0x00D1C948471CA87Full};
    const std::array<uint64_t, 1> s_One {0x00D1C948471CA87Full};
    const std::array<uint64_t, 2> s_Twice {0x00D1C948471CA87Full, 0x00D1C948471CA87Full};
    CHECK(pkg::HoldsExactly(s_Parsed, s_Both));
    CHECK_FALSE(pkg::HoldsExactly(s_Parsed, s_One));
    CHECK_FALSE(pkg::HoldsExactly(s_Parsed, s_Twice));
}

TEST_CASE("patch package: a type that is not 4 characters builds nothing") {
    pkg::PackageResource s_Short;
    s_Short.hash = 1;
    s_Short.type = "AB";
    s_Short.data = {1, 2, 3};
    CHECK(pkg::BuildPatchPackage(std::span<const pkg::PackageResource>(&s_Short, 1)).empty());

    s_Short.type = "TEMPS";
    CHECK(pkg::BuildPatchPackage(std::span<const pkg::PackageResource>(&s_Short, 1)).empty());
}

TEST_CASE("patch package: the parser rejects damaged packages") {
    const std::vector<uint8_t> s_Good = pkg::BuildPatchPackage(TwoResources());

    CHECK_FALSE(pkg::ParsePatchPackage({}).ok);
    CHECK_FALSE(pkg::ParsePatchPackage(Bytes("NOPE and more bytes here")).ok);

    // Cut anywhere: always an error, never a crash.
    for (size_t s_Size = 0; s_Size < s_Good.size(); s_Size += 7) {
        const std::vector<uint8_t> s_Cut(s_Good.begin(), s_Good.begin() + static_cast<ptrdiff_t>(s_Size));
        CHECK_FALSE(pkg::ParsePatchPackage(s_Cut).ok);
    }

    // A reference count that does not match the table size.
    std::vector<uint8_t> s_BadRefs = s_Good;
    s_BadRefs[60 + 24] = 4;
    CHECK(pkg::ParsePatchPackage(s_BadRefs).error == "reference table size does not match its count");

    // A wrong final size: the data no longer decodes to it.
    std::vector<uint8_t> s_BadFinal = s_Good;
    s_BadFinal[60 + 12] ^= 1;
    CHECK_FALSE(pkg::ParsePatchPackage(s_BadFinal).ok);

    // A payload byte changed decodes to other data (still a valid block).
    std::vector<uint8_t> s_Changed = s_Good;
    s_Changed.back() ^= 0xFF;
    const pkg::ParsedPackage s_Parsed = pkg::ParsePatchPackage(s_Changed);
    REQUIRE(s_Parsed.ok);
    CHECK(s_Parsed.resources[1].data != TwoResources()[1].data);
}

TEST_CASE("patch package: the parser reads a version-2 header with deletions and stored data") {
    // 2KPR, 9 header bytes, 1 resource, index 20, info 24, 1 deletion; the
    // data is neither compressed nor ciphered (stored size 0).
    std::vector<uint8_t> s_Rpkg = {'2', 'K', 'P', 'R', 1, 0, 0, 0, 0, 0, 0, 0, 0};
    Put32(s_Rpkg, 1);
    Put32(s_Rpkg, 20);
    Put32(s_Rpkg, 24);
    Put32(s_Rpkg, 1);
    Put64(s_Rpkg, 0x0033);
    const uint64_t s_Data = s_Rpkg.size() + 20 + 24;
    Put64(s_Rpkg, 0x0044);
    Put64(s_Rpkg, s_Data);
    Put32(s_Rpkg, 0);
    s_Rpkg.insert(s_Rpkg.end(), {'N', 'O', 'S', 'J'});
    Put32(s_Rpkg, 0);
    Put32(s_Rpkg, 0);
    Put32(s_Rpkg, 3);
    Put32(s_Rpkg, pkg::kNoMemSize);
    Put32(s_Rpkg, pkg::kNoMemSize);
    s_Rpkg.insert(s_Rpkg.end(), {'a', 'b', 'c'});

    const pkg::ParsedPackage s_Parsed = pkg::ParsePatchPackage(s_Rpkg);
    REQUIRE(s_Parsed.ok);
    CHECK_FALSE(s_Parsed.version1);
    CHECK(s_Parsed.deletions == std::vector<uint64_t> {0x0033});
    REQUIRE(s_Parsed.resources.size() == 1);
    CHECK(s_Parsed.resources[0].hash == 0x0044);
    CHECK(s_Parsed.resources[0].type == "JSON");
    CHECK(s_Parsed.resources[0].data == Bytes("abc"));

    // Not version 1 and has a deletion: never the mod's.
    const std::array<uint64_t, 1> s_Hash {0x0044};
    CHECK_FALSE(pkg::HoldsExactly(s_Parsed, s_Hash));
}

TEST_CASE("patch package: what to do with the package file at game start") {
    const std::vector<pkg::PackageResource> s_Resources = TwoResources();
    const std::vector<uint8_t> s_Wanted = pkg::BuildPatchPackage(s_Resources);
    const std::array<uint64_t, 2> s_Own {s_Resources[0].hash, s_Resources[1].hash};

    // A stale copy of the mod's: the same hashes, other data.
    std::vector<pkg::PackageResource> s_Old = s_Resources;
    s_Old[0].data = FakeBin1(100, 0);
    const std::vector<uint8_t> s_Stale = pkg::BuildPatchPackage(s_Old);

    // Someone else's: another hash set, or not a package.
    const std::vector<uint8_t> s_Other = pkg::BuildPatchRpkg(0x1234, "TEMP", "x");
    const std::vector<uint8_t> s_Junk = Bytes("not a package");

    using A = pkg::FileAction;
    CHECK(pkg::DecidePackageFile(false, nullptr, true, s_Own, s_Wanted) == A::None);
    CHECK(pkg::DecidePackageFile(true, nullptr, true, s_Own, s_Wanted) == A::Write);
    CHECK(pkg::DecidePackageFile(true, &s_Wanted, true, s_Own, s_Wanted) == A::UpToDate);
    CHECK(pkg::DecidePackageFile(true, &s_Stale, true, s_Own, s_Wanted) == A::Write);
    CHECK(pkg::DecidePackageFile(false, &s_Wanted, true, s_Own, s_Wanted) == A::Remove);
    CHECK(pkg::DecidePackageFile(false, &s_Stale, true, s_Own, s_Wanted) == A::Remove);
    CHECK(pkg::DecidePackageFile(true, &s_Other, true, s_Own, s_Wanted) == A::NotOurs);
    CHECK(pkg::DecidePackageFile(false, &s_Other, true, s_Own, s_Wanted) == A::NotOurs);
    CHECK(pkg::DecidePackageFile(true, &s_Junk, true, s_Own, s_Wanted) == A::NotOurs);
    CHECK(pkg::DecidePackageFile(false, &s_Junk, true, s_Own, s_Wanted) == A::NotOurs);
    // Exists but unreadable (or too large): left alone either way.
    const std::vector<uint8_t> s_Empty;
    CHECK(pkg::DecidePackageFile(true, &s_Empty, false, s_Own, s_Wanted) == A::NotOurs);
    CHECK(pkg::DecidePackageFile(false, &s_Empty, false, s_Own, s_Wanted) == A::NotOurs);

    // An earlier build's package (a former hash, or only some of the hashes):
    // still the mod's, rewritten or removed.
    const std::vector<uint8_t> s_Former = pkg::BuildPatchRpkg(0x00D1C948471CA87Full, "TEMP", "x");
    const std::array<uint64_t, 1> s_FormerHashes {0x00D1C948471CA87Full};
    const std::array<uint64_t, 1> s_NewOwn {0x00AAAAAAAAAAAAAAull};
    CHECK(pkg::DecidePackageFile(true, &s_Former, true, s_NewOwn, s_Wanted) == A::NotOurs);
    CHECK(pkg::DecidePackageFile(true, &s_Former, true, s_NewOwn, s_Wanted, s_FormerHashes) == A::Write);
    CHECK(pkg::DecidePackageFile(false, &s_Former, true, s_NewOwn, s_Wanted, s_FormerHashes) == A::Remove);
    const std::vector<uint8_t> s_Part = pkg::BuildPatchPackage(std::span(s_Resources).first(1));
    CHECK(pkg::DecidePackageFile(true, &s_Part, true, s_Own, s_Wanted) == A::Write);
    // Ours plus a hash we never had: someone else's.
    std::vector<pkg::PackageResource> s_Mixed = s_Resources;
    s_Mixed.push_back(s_Resources[0]);
    s_Mixed.back().hash = 0x5555;
    const std::vector<uint8_t> s_MixedFile = pkg::BuildPatchPackage(s_Mixed);
    CHECK(pkg::DecidePackageFile(true, &s_MixedFile, true, s_Own, s_Wanted, s_FormerHashes) == A::NotOurs);
    // The same hash twice: not a package the mod writes.
    std::vector<pkg::PackageResource> s_Twice = {s_Resources[0], s_Resources[0]};
    const std::vector<uint8_t> s_TwiceFile = pkg::BuildPatchPackage(s_Twice);
    CHECK(pkg::DecidePackageFile(true, &s_TwiceFile, true, s_Own, s_Wanted) == A::NotOurs);
    // An empty package: not the mod's either.
    const std::vector<uint8_t> s_None = pkg::BuildPatchPackage({});
    CHECK(pkg::DecidePackageFile(true, &s_None, true, s_Own, s_Wanted) == A::NotOurs);

    // On with nothing wanted: never an empty write; the mod's file (stale or not) is left as it is.
    const std::vector<uint8_t> s_NoWanted;
    CHECK(pkg::DecidePackageFile(true, nullptr, true, s_Own, s_NoWanted) == A::None);
    CHECK(pkg::DecidePackageFile(true, &s_Stale, true, s_Own, s_NoWanted) == A::None);
    CHECK(pkg::DecidePackageFile(true, &s_Other, true, s_Own, s_NoWanted) == A::NotOurs);
}

TEST_CASE("patch package: a TEMP's blueprint index") {
    // BIN1 header, then STemplateEntityFactory: subType, blueprintIndexInResourceHeader.
    std::vector<uint8_t> s_Temp = FakeBin1(64, 0);
    s_Temp[16] = 2;
    s_Temp[17] = s_Temp[18] = s_Temp[19] = 0;
    s_Temp[20] = 3;
    s_Temp[21] = s_Temp[22] = s_Temp[23] = 0;
    CHECK(pkg::TempBlueprintIndex(s_Temp) == 3u);

    s_Temp[23] = 0x80; // negative: none
    CHECK_FALSE(pkg::TempBlueprintIndex(s_Temp).has_value());
    CHECK_FALSE(pkg::TempBlueprintIndex(Bytes("BIN1")).has_value());
    CHECK_FALSE(pkg::TempBlueprintIndex(Bytes("JSON{}{}{}{}{}{}{}{}{}{}")).has_value());
}

TEST_CASE("patch package: the partition raise") {
    const std::vector<int> s_Deck {1, 2, 3, 4, 300};
    const std::vector<int> s_Stray {1, 2, 3, 4, 120};

    // Partition 26 "coastaltown" at level 4: raised to 290 (patch300 stays out).
    pkg::RaiseDecision s_Raise =
        pkg::DecideRaise(26, "coastaltown", 4, 26, "coastaltown", 290, s_Deck, false, "chunk26patch290.rpkg");
    CHECK(s_Raise.ours);
    CHECK(s_Raise.level == 290);
    CHECK(s_Raise.note == " -> 290 (to mount chunk26patch290.rpkg)");

    // Any case of the name.
    CHECK(pkg::DecideRaise(26, "CoastalTown", 4, 26, "coastaltown", 290, s_Deck, false, "x").level == 290);

    // Other partitions are untouched and get no note.
    s_Raise = pkg::DecideRaise(0, "chunk0", 4, 26, "coastaltown", 290, s_Deck, false, "chunk26patch290.rpkg");
    CHECK_FALSE(s_Raise.ours);
    CHECK(s_Raise.level == 4);
    CHECK(s_Raise.note.empty());

    // The right index with another name: not raised (the table changed).
    s_Raise = pkg::DecideRaise(26, "hokkaido", 4, 26, "coastaltown", 290, s_Deck, false, "chunk26patch290.rpkg");
    CHECK_FALSE(s_Raise.ours);
    CHECK(s_Raise.level == 4);
    CHECK(s_Raise.note.find("not raised") != std::string::npos);

    // Not installed: never raised.
    CHECK(pkg::DecideRaise(26, "coastaltown", 4, 26, "coastaltown", 0, s_Deck, false, "x").level == 4);

    // MaxPatchLevel raises it itself.
    s_Raise = pkg::DecideRaise(26, "coastaltown", 4, 26, "coastaltown", 290, s_Deck, true, "chunk26patch290.rpkg");
    CHECK(s_Raise.level == 4);
    CHECK(s_Raise.note.find("MaxPatchLevel") != std::string::npos);

    // Already at or above the target.
    CHECK(pkg::DecideRaise(26, "coastaltown", 1000, 26, "coastaltown", 290, s_Deck, false, "x").level == 1000);

    // A stray patch between 4 and 290 would also mount: refused.
    s_Raise = pkg::DecideRaise(26, "coastaltown", 4, 26, "coastaltown", 290, s_Stray, false, "chunk26patch290.rpkg");
    CHECK(s_Raise.level == 4);
    CHECK(s_Raise.unlocked == std::vector<int> {120});
    CHECK(s_Raise.note.find("chunk26patch120.rpkg") != std::string::npos);
}

TEST_CASE("patch package: chunk patch numbers, package names, BIN1 memory size") {
    CHECK(pkg::ChunkPatchNumber(26, "chunk26patch4.rpkg") == 4);
    CHECK(pkg::ChunkPatchNumber(26, "CHUNK26PATCH290.RPKG") == 290);
    CHECK_FALSE(pkg::ChunkPatchNumber(26, "chunk2patch4.rpkg").has_value());
    CHECK_FALSE(pkg::ChunkPatchNumber(2, "chunk26patch4.rpkg").has_value());
    CHECK_FALSE(pkg::ChunkPatchNumber(26, "chunk26.rpkg").has_value());
    CHECK_FALSE(pkg::ChunkPatchNumber(26, "chunk26patch290.rpkg.tmp").has_value());
    CHECK(pkg::ChunkPatchNumber(0, "chunk0patch2.rpkg") == 2);

    // The game's mission_mild_bingo TEMP: BIN1 data size 0xFD9C = 64924, its
    // rpkg memory size.
    std::vector<uint8_t> s_Bin = FakeBin1(64924, 15376);
    CHECK(pkg::BinMemSize(s_Bin) == 64924u);
    s_Bin.resize(100);
    CHECK_FALSE(pkg::BinMemSize(s_Bin).has_value());
    CHECK_FALSE(pkg::BinMemSize(Bytes("JSON{}{}{}{}{}{}{}")).has_value());
}

TEST_CASE("patch package: atomic write and bounded read") {
    namespace fs = std::filesystem;
    const fs::path s_Dir = fs::temp_directory_path() / "fr_patch_package_test";
    fs::remove_all(s_Dir);
    fs::create_directories(s_Dir);
    const fs::path s_File = s_Dir / "chunk26patch290.rpkg";
    const std::vector<uint8_t> s_Data = pkg::BuildPatchPackage(TwoResources());

    std::string s_Error;
    REQUIRE(pkg::WriteFileAtomically(s_File, s_Data, &s_Error));
    CHECK_FALSE(fs::exists(s_Dir / "chunk26patch290.rpkg.tmp"));

    bool s_Ok = false;
    CHECK(pkg::ReadFileBytes(s_File, &s_Ok) == s_Data);
    CHECK(s_Ok);
    CHECK(pkg::ReadFileBytes(s_File, &s_Ok, s_Data.size() - 1).empty());
    CHECK_FALSE(s_Ok);
    pkg::ReadFileBytes(s_Dir / "missing.rpkg", &s_Ok);
    CHECK_FALSE(s_Ok);

    // A write into a folder that does not exist fails cleanly.
    CHECK_FALSE(pkg::WriteFileAtomically(s_Dir / "no" / "x.rpkg", s_Data, &s_Error));
    CHECK_FALSE(s_Error.empty());

    fs::remove_all(s_Dir);
}
