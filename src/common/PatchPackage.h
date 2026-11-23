#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The patch packages (rpkg files) the mod writes into the game's Runtime folder
// (one per partition of the full map variants, chunk<partition>patch290.rpkg)
// and the checks around them.
// Another mod may write its own package in the same layout.
// Everything here is pure (no engine calls, no platform calls);
// PatchPackageGlue.h has the engine parts.
//
// Package layout (version 1, magic GKPR, as the mod writes it):
//   header:  "GKPR" | resource count u32 | index size u32 | info size u32 | deletion count u32 (patch packages)
//            [version 2 (2KPR) has 9 more bytes after the magic]
//   index:   per resource: hash u64 | data offset u64 | stored size u32 (bit 31: XOR-ciphered; low 30 bits:
//            compressed size, 0 = stored uncompressed)
//   info:    per resource: type[4] (reversed) | reference table size u32 | 0 u32 | final size u32 |
//            memory size u32 | video memory size u32 | reference table
//   refs:    count u32 (0xC0000000 | n) | n flag bytes | n hashes u64
//   data:    each resource's stored bytes, in index order
namespace pkg {

struct RpkgIndex; // RpkgRead.h

// The memory sizes the game's JSON resources carry.
inline constexpr uint32_t kNoMemSize = 0xFFFFFFFFu;

// The reference flags most of the game's brick references carry (0x1F: no
// language, type bits 0).
inline constexpr uint8_t kRefFlagsNormal = 0x1F;

struct PackageRef {
    uint8_t flags = kRefFlagsNormal;
    uint64_t hash = 0;

    bool operator==(const PackageRef&) const = default;
};

// One resource of a package, decoded (data is the final, uncompressed bytes).
struct PackageResource {
    uint64_t hash = 0;
    std::string type; // 4 characters, e.g. "TEMP"
    std::vector<uint8_t> data;
    uint32_t memSize =
        kNoMemSize; // for TEMP/TBLU: the BIN1 header's data size (the tests read it with tests/support's BinMemSize)
    uint32_t vmemSize = kNoMemSize;
    std::vector<PackageRef> refs;
};

// The largest resource, and index and info tables, the mod reads from a
// package (the game's largest resource is about 23 MB); larger is refused
// as corrupt, never allocated.
inline constexpr uint32_t kMaxRpkgSize = 256u << 20;

// Applies (or removes: it is its own inverse) the rpkg resource XOR cipher.
void XorRpkgData(std::span<uint8_t> p_Data);

// Decodes an LZ4 block of known decoded size; empty when it is malformed,
// does not decode to exactly p_Size bytes, or p_Size is over kMaxRpkgSize.
std::optional<std::vector<uint8_t>> Lz4Decode(std::span<const uint8_t> p_Block, size_t p_Size);

// A version-1 patch package (magic GKPR, no deletions) holding p_Resources in
// order: each one's data LZ4-compressed (one literal run) and XOR-ciphered,
// with its type, sizes and reference table. Empty when a resource's type is
// not 4 characters.
std::vector<uint8_t> BuildPatchPackage(std::span<const PackageResource> p_Resources);

// A whole package, parsed and every resource decoded.
struct ParsedPackage {
    bool ok = false;
    std::string error;     // why not, when !ok
    bool version1 = false; // GKPR (mod tools); false: 2KPR (the game's own)
    std::vector<uint64_t> deletions;
    std::vector<PackageResource> resources;
};

// Parses p_File (a patch package when p_IsPatch: it has a deletion list).
// Fails on any inconsistency: sizes, offsets outside the file, a reference
// table whose size is not 4 + 9 n, data that does not decode to its final
// size.
ParsedPackage ParsePatchPackage(std::span<const uint8_t> p_File, bool p_IsPatch = true);

// The patch number of a patch package file name of chunk p_Chunk, any case
// (ChunkPatchNumber(26, "chunk26patch4.rpkg") -> 4), or empty for any other
// name.
std::optional<int> ChunkPatchNumber(int p_Chunk, std::string_view p_FileName);

// The other patch packages raising a partition's patch level from p_Original
// to p_Target would newly mount (p_Original < n < p_Target), in the order
// given. The mod refuses the raise when there is any.
std::vector<int> PatchesUnlockedByRaise(std::span<const int> p_Numbers, int p_Original, int p_Target);

// The ini text has the section (any case; "[name]" alone on a line, spaces
// around it allowed), e.g. [maxpatchlevel] in mods.ini when the SDK's
// MaxPatchLevel mod is enabled.
bool IniHasSection(std::string_view p_Ini, std::string_view p_Section);

// What to do with a mod's own package file at game start.
enum class FileAction {
    None,     // off and absent, or on with nothing wanted (empty p_Wanted): nothing to do
    Remove,   // off, and the file is the mod's: remove it
    NotOurs,  // the file is someone else's (or unreadable): leave it alone; the feature stays off
    UpToDate, // on, and the file already holds exactly the wanted bytes
    Write,    // on: write the wanted bytes (absent, or the mod's but stale)
};

// p_Parsed is a version-1 package with no deletions holding at least one
// resource, each once, every one of them in p_Own or p_Former.
bool HoldsOnly(const ParsedPackage& p_Parsed, std::span<const uint64_t> p_Own, std::span<const uint64_t> p_Former);

// The same check on a package's index alone (ReadRpkgIndex), nothing decoded.
bool HoldsOnly(const RpkgIndex& p_Index, std::span<const uint64_t> p_Own, std::span<const uint64_t> p_Former);

// p_Current: the file's bytes, or null when it does not exist (p_Readable
// false: it exists but could not be read). The file is the mod's when it
// parses (ParsePatchPackage) and HoldsOnly(p_OwnHashes, p_FormerHashes): an
// earlier build of the mod may have written fewer resources, or ones under
// hashes it no longer uses (p_FormerHashes). Never Write for an empty
// p_Wanted.
FileAction DecidePackageFile(bool p_On, const std::vector<uint8_t>* p_Current, bool p_Readable,
                             std::span<const uint64_t> p_OwnHashes, std::span<const uint8_t> p_Wanted,
                             std::span<const uint64_t> p_FormerHashes = {});

// The partition detour's decision for one partition.
struct RaiseDecision {
    bool ours = false;         // the partition the mod raises (index and name match)
    int32_t level = 0;         // the level to create it with (the original when not raised)
    std::vector<int> unlocked; // other patches a raise would mount (refused when any)
    std::string note;          // for the log: " -> 290 (...)", " (not raised: ...)", or ""
};

// p_Index/p_Id: the partition being created; p_OwnIndex/p_OwnId: the one the
// mod raises (the id compared in any case); p_Target: the mod's patch number
// (0: not installed, never raise); p_OtherPatches: the partition's other
// patch numbers in Runtime; p_MaxPatchLevel: MaxPatchLevel is enabled (it
// raises every partition itself). p_FileName names the package in the note.
RaiseDecision DecideRaise(int32_t p_Index, std::string_view p_Id, int32_t p_Level, int32_t p_OwnIndex,
                          std::string_view p_OwnId, int p_Target, std::span<const int> p_OtherPatches,
                          bool p_MaxPatchLevel, std::string_view p_FileName);

// The file's bytes; *p_Ok false when it cannot be opened or read in full (as
// many bytes as its size), or is larger than p_MaxSize (then nothing is
// returned).
std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& p_Path, bool* p_Ok, size_t p_MaxSize = size_t(1) << 30);

// Writes through a temporary file renamed over the target, so a failed write
// never leaves a cut package for the game to mount; the temporary file is
// removed on any failure.
bool WriteFileAtomically(const std::filesystem::path& p_Path, const std::vector<uint8_t>& p_Data, std::string* p_Error);

} // namespace pkg
