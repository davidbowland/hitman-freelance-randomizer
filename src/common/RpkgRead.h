#pragma once

#include <cstdint>
#include <istream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Reading the game's own packages (rpkg files in Runtime): a package's index
// without its data, one resource's bytes, and which package holds the copy of
// a resource the game mounts. Pure (streams only); the layout is in
// PatchPackage.h, and tools/rpkg/rpkg.py reads it the same way.
namespace pkg {

// One resource of a package's index.
struct RpkgEntry {
    uint64_t hash = 0;
    uint64_t offset = 0; // of its stored bytes in the file
    uint32_t stored = 0; // LZ4 block size; 0: stored uncompressed (finalSize bytes)
    bool xored = false;  // its stored bytes are XOR-ciphered
    std::string type;    // 4 characters, e.g. "TEMP"
    uint32_t finalSize = 0;
};

struct RpkgIndex {
    bool ok = false;
    std::string error;     // why not, when !ok
    bool version1 = false; // GKPR (mod tools); false: 2KPR (the game's own)
    std::vector<uint64_t> deletions;
    std::vector<RpkgEntry> entries;
};

// p_In in both: a stream opened with std::ios::binary (text mode would
// translate line ends). Each call seeks where it reads and clears the stream's
// state first, so one stream serves many calls.

// The header, deletion list (p_IsPatch: a chunk<N>patch<K>.rpkg), index and
// info table of the package p_In reads, from its start. Fails on any
// inconsistency, a table running past the end of the file, or the index and
// info tables together over kMaxRpkgSize; resource data is not read
// (ReadRpkgResource checks it).
RpkgIndex ReadRpkgIndex(std::istream& p_In, bool p_IsPatch);

// The resource's final bytes (deciphered, decompressed); empty when its data
// runs past the end of the file, its stored or final size is over
// kMaxRpkgSize, or it does not decode to exactly finalSize bytes.
std::optional<std::vector<uint8_t>> ReadRpkgResource(std::istream& p_In, const RpkgEntry& p_Entry);

struct Located {
    std::string package; // its file name, e.g. "chunk26patch4.rpkg"
    RpkgEntry entry;
};

struct LocateResult {
    std::optional<Located> found; // empty: no copy remains, or error
    std::string error;            // not empty: the copy cannot be told (found is empty)
};

// The copy of p_Hash the game mounts from chunk p_Chunk's packages, given as
// (file name, index) in any order: chunk<C>.rpkg, then each IOI patch
// chunk<C>patch<K>.rpkg by ascending K (mods' patches, K >= fr::kFirstModPatch,
// are left out), each patch's deletions applied before its entries. Other
// chunks' packages and names of other forms are left out. An error when
// chunk<C>.rpkg is missing, an IOI package of the chunk has an index that is
// not ok, or two packages have the same patch number (chunk26patch1.rpkg and
// chunk26patch01.rpkg: which one the game mounts is not known).
LocateResult LocateNewest(std::span<const std::pair<std::string, RpkgIndex>> p_Packages, int p_Chunk, uint64_t p_Hash);

} // namespace pkg
