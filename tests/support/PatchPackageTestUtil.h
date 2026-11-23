#pragma once

// Patch package helpers only the tests use (building one-resource packages,
// checking a parsed package's contents, reading BIN1 headers).

#include "PatchPackage.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pkg {

// BuildPatchPackage of one resource of type p_Type (4 characters, e.g.
// "JSON"), no references and memory sizes kNoMemSize.
inline std::vector<uint8_t> BuildPatchRpkg(uint64_t p_Hash, std::string_view p_Type, std::string_view p_Data) {
    PackageResource s_Resource;
    s_Resource.hash = p_Hash;
    s_Resource.type = std::string(p_Type);
    s_Resource.data.assign(p_Data.begin(), p_Data.end());
    return BuildPatchPackage(std::span<const PackageResource>(&s_Resource, 1));
}

// One resource of a synthetic package (BuildTestRpkg). stored is written as
// given (XOR-ciphered first when xored); compressed: the index's stored size
// is its length (an LZ4 block of finalSize bytes), else 0 (stored as is).
struct TestRpkgResource {
    uint64_t hash = 0;
    std::string type = "TEMP";
    std::vector<uint8_t> stored;
    uint32_t finalSize = 0;
    bool compressed = false;
    bool xored = false;
    std::vector<uint64_t> refs;
};

// A resource stored uncompressed: its bytes are p_Data.
inline TestRpkgResource PlainTestResource(uint64_t p_Hash, std::string_view p_Data, bool p_Xored = false) {
    TestRpkgResource s_Out;
    s_Out.hash = p_Hash;
    s_Out.stored.assign(p_Data.begin(), p_Data.end());
    s_Out.finalSize = static_cast<uint32_t>(p_Data.size());
    s_Out.xored = p_Xored;
    return s_Out;
}

// A package laid out as the game's are: version 1 (GKPR) or 2 (2KPR), base or
// patch (with p_Deletions), then index, info and data in resource order.
inline std::vector<uint8_t> BuildTestRpkg(bool p_Version1, bool p_IsPatch, std::span<const uint64_t> p_Deletions,
                                          std::span<const TestRpkgResource> p_Resources) {
    std::vector<uint8_t> s_Out;
    const auto s_Put32 = [&](uint32_t p_Value) {
        for (int i = 0; i < 4; ++i)
            s_Out.push_back(static_cast<uint8_t>(p_Value >> (8 * i)));
    };
    const auto s_Put64 = [&](uint64_t p_Value) {
        for (int i = 0; i < 8; ++i)
            s_Out.push_back(static_cast<uint8_t>(p_Value >> (8 * i)));
    };

    uint32_t s_InfoSize = 0;

    for (const TestRpkgResource& s_Resource : p_Resources)
        s_InfoSize += 24u + (s_Resource.refs.empty() ? 0u : 4u + 9u * static_cast<uint32_t>(s_Resource.refs.size()));

    const uint32_t s_IndexSize = static_cast<uint32_t>(p_Resources.size()) * 20u;

    if (p_Version1) {
        s_Out.insert(s_Out.end(), {'G', 'K', 'P', 'R'});
    }
    else {
        s_Out.insert(s_Out.end(), {'2', 'K', 'P', 'R', 1, 0, 0, 0, 0, 0, 0, 0, 0});
    }

    s_Put32(static_cast<uint32_t>(p_Resources.size()));
    s_Put32(s_IndexSize);
    s_Put32(s_InfoSize);

    if (p_IsPatch) {
        s_Put32(static_cast<uint32_t>(p_Deletions.size()));

        for (const uint64_t s_Hash : p_Deletions)
            s_Put64(s_Hash);
    }

    uint64_t s_Offset = s_Out.size() + s_IndexSize + s_InfoSize;

    for (const TestRpkgResource& s_Resource : p_Resources) {
        s_Put64(s_Resource.hash);
        s_Put64(s_Offset);
        s_Put32((s_Resource.xored ? 0x80000000u : 0u) |
                (s_Resource.compressed ? static_cast<uint32_t>(s_Resource.stored.size()) : 0u));
        s_Offset += s_Resource.stored.size();
    }

    for (const TestRpkgResource& s_Resource : p_Resources) {
        for (size_t i = 0; i < 4; ++i)
            s_Out.push_back(static_cast<uint8_t>(s_Resource.type[3 - i]));

        const uint32_t s_Refs = static_cast<uint32_t>(s_Resource.refs.size());
        s_Put32(s_Refs == 0 ? 0u : 4u + 9u * s_Refs);
        s_Put32(0);
        s_Put32(s_Resource.finalSize);
        s_Put32(kNoMemSize);
        s_Put32(kNoMemSize);

        if (s_Refs != 0) {
            s_Put32(0xC0000000u | s_Refs);

            for (size_t i = 0; i < s_Refs; ++i)
                s_Out.push_back(kRefFlagsNormal);

            for (const uint64_t s_Ref : s_Resource.refs)
                s_Put64(s_Ref);
        }
    }

    for (const TestRpkgResource& s_Resource : p_Resources) {
        std::vector<uint8_t> s_Stored = s_Resource.stored;

        if (s_Resource.xored)
            XorRpkgData(s_Stored);

        s_Out.insert(s_Out.end(), s_Stored.begin(), s_Stored.end());
    }

    return s_Out;
}

// p_Parsed is a version-1 package with no deletions holding exactly the
// resources p_Hashes (any order, each once).
inline bool HoldsExactly(const ParsedPackage& p_Parsed, std::span<const uint64_t> p_Hashes) {
    if (!p_Parsed.ok || !p_Parsed.version1 || !p_Parsed.deletions.empty() ||
        p_Parsed.resources.size() != p_Hashes.size())
        return false;

    std::vector<uint64_t> s_Held;

    for (const PackageResource& s_Resource : p_Parsed.resources)
        s_Held.push_back(s_Resource.hash);

    std::vector<uint64_t> s_Wanted(p_Hashes.begin(), p_Hashes.end());
    std::sort(s_Held.begin(), s_Held.end());
    std::sort(s_Wanted.begin(), s_Wanted.end());
    return s_Held == s_Wanted && std::adjacent_find(s_Held.begin(), s_Held.end()) == s_Held.end();
}

// The u32 at p_At, big- or little-endian (p_Data holds at least p_At + 4
// bytes). Read through a subspan: GCC 13's -Warray-bounds misreads indexing
// past a short buffer on paths the callers' size checks rule out.
inline uint32_t U32Be(std::span<const uint8_t> p_Data, size_t p_At) {
    uint32_t s_Value = 0;
    for (const uint8_t s_Byte : p_Data.subspan(p_At, 4))
        s_Value = s_Value << 8 | s_Byte;
    return s_Value;
}

inline uint32_t U32Le(std::span<const uint8_t> p_Data, size_t p_At) {
    const std::span<const uint8_t> s_Bytes = p_Data.subspan(p_At, 4);
    uint32_t s_Value = 0;
    for (auto s_It = s_Bytes.rbegin(); s_It != s_Bytes.rend(); ++s_It)
        s_Value = s_Value << 8 | *s_It;
    return s_Value;
}

// The memory size of a ResourceLib binary (TEMP, TBLU, ...): its BIN1
// header's data size (big-endian u32 at offset 8), which is the rpkg info
// table's memory size for the game's own TEMP and TBLU resources. Empty when
// p_Data is not a BIN1 file whose data fits in it.
inline std::optional<uint32_t> BinMemSize(std::span<const uint8_t> p_Data) {
    if (p_Data.size() < 16 || std::memcmp(p_Data.data(), "BIN1", 4) != 0)
        return std::nullopt;

    const uint32_t s_Size = U32Be(p_Data, 8);

    if (s_Size > p_Data.size() - 16)
        return std::nullopt;

    return s_Size;
}

// A TEMP's blueprint: the index into its reference table that its
// STemplateEntityFactory names (blueprintIndexInResourceHeader, the
// little-endian int32 at offset 20, after subType at the start of the BIN1
// data). Empty when p_Data is too short, not BIN1, or the index is negative.
inline std::optional<uint32_t> TempBlueprintIndex(std::span<const uint8_t> p_Data) {
    if (!BinMemSize(p_Data) || p_Data.size() < 24)
        return std::nullopt;

    const uint32_t s_Index = U32Le(p_Data, 20);

    if (s_Index & 0x80000000u)
        return std::nullopt;

    return s_Index;
}

} // namespace pkg
