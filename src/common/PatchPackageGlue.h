#pragma once

// Engine and platform parts of the patch packages (PatchPackage.h): the
// game's Runtime folder and the resource container walk that tells which
// mounted package a resource resolves to.

#include <Common.h> // Windows.h with NOMINMAX, as the SDK headers need

#include <cstdint>
#include <filesystem>

namespace pkg {

// The game's Runtime folder: next to Retail, the executable's folder.
std::filesystem::path RuntimeFolder();

// One resource's entry in the resource container, and the newer versions it
// links to (nextNewestIndex).
struct ResourceHop {
    uint32_t dataSize; // the final (decompressed) size
    char package[192]; // the mounted package's name
};

struct ResourceChainOut {
    bool container; // the resource container exists
    bool found;     // in the hash map, else by a scan of the resource table
    int hops;
    ResourceHop hop[8];
};

// Reads p_Rid's chain from the resource container, guarded (see Guard.h);
// 0, or the exception code of a fault (*p_Out then holds what was read).
DWORD SafeResourceChain(uint64_t p_Rid, ResourceChainOut* p_Out);

} // namespace pkg
