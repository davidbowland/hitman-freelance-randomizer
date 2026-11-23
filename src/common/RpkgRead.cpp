#include "RpkgRead.h"

#include "LandslideFull.h"
#include "PatchPackage.h"
#include "TextNoCase.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace pkg {

namespace {

constexpr uint32_t kXoredFlag = 0x80000000u;
constexpr uint32_t kSizeMask = 0x3FFFFFFFu;
constexpr uint32_t kRefCountMask = 0x3FFFFFFFu;
constexpr uint32_t kMaxCount = 1u << 24;

uint32_t GetU32(const uint8_t* p_Data) {
    return static_cast<uint32_t>(p_Data[0]) | static_cast<uint32_t>(p_Data[1]) << 8 |
           static_cast<uint32_t>(p_Data[2]) << 16 | static_cast<uint32_t>(p_Data[3]) << 24;
}

uint64_t GetU64(const uint8_t* p_Data) {
    return static_cast<uint64_t>(GetU32(p_Data)) | static_cast<uint64_t>(GetU32(p_Data + 4)) << 32;
}

// The stream's size, its read position reset to the start; empty when it
// cannot seek.
std::optional<uint64_t> StreamSize(std::istream& p_In) {
    p_In.clear();
    p_In.seekg(0, std::ios::end);
    const std::streamoff s_Size = p_In.tellg();
    p_In.seekg(0, std::ios::beg);

    if (!p_In || s_Size < 0)
        return std::nullopt;

    return static_cast<uint64_t>(s_Size);
}

// Appends p_Size bytes read at p_At to p_Out; false when they are not all
// within the first p_FileSize bytes or the read fails.
bool ReadAt(std::istream& p_In, uint64_t p_FileSize, uint64_t p_At, uint64_t p_Size, std::vector<uint8_t>& p_Out) {
    if (p_At > p_FileSize || p_Size > p_FileSize - p_At)
        return false;

    const size_t s_Old = p_Out.size();
    p_Out.resize(s_Old + static_cast<size_t>(p_Size));
    p_In.clear();
    p_In.seekg(static_cast<std::streamoff>(p_At), std::ios::beg);
    p_In.read(reinterpret_cast<char*>(p_Out.data() + s_Old), static_cast<std::streamsize>(p_Size));
    return p_In && static_cast<uint64_t>(p_In.gcount()) == p_Size;
}

// The name is chunk<p_Chunk>.rpkg or chunk<p_Chunk>patch<K>.rpkg, any case.
bool OfChunk(std::string_view p_Name, int p_Chunk) {
    const std::string s_Chunk = "chunk" + std::to_string(p_Chunk);

    if (!fr::StartsWithNoCase(p_Name, s_Chunk))
        return false;

    const std::string_view s_Rest = p_Name.substr(s_Chunk.size());
    return fr::StartsWithNoCase(s_Rest, ".") || fr::StartsWithNoCase(s_Rest, "patch");
}

} // namespace

RpkgIndex ReadRpkgIndex(std::istream& p_In, bool p_IsPatch) {
    RpkgIndex s_Out;
    const auto s_Fail = [&](std::string p_Error) {
        s_Out.ok = false;
        s_Out.error = std::move(p_Error);
        s_Out.deletions.clear();
        s_Out.entries.clear();
        return s_Out;
    };

    const std::optional<uint64_t> s_FileSize = StreamSize(p_In);

    if (!s_FileSize)
        return s_Fail("cannot read the file");

    std::vector<uint8_t> s_Head;

    if (!ReadAt(p_In, *s_FileSize, 0, 4, s_Head))
        return s_Fail("file shorter than its header");

    uint64_t p = 4;

    if (std::memcmp(s_Head.data(), "2KPR", 4) == 0) {
        p += 9;
    }
    else if (std::memcmp(s_Head.data(), "GKPR", 4) == 0) {
        s_Out.version1 = true;
    }
    else {
        return s_Fail("not a resource package (bad magic)");
    }

    s_Head.clear();

    if (!ReadAt(p_In, *s_FileSize, p, 12, s_Head))
        return s_Fail("file shorter than its header");

    const uint32_t s_Count = GetU32(&s_Head[0]);
    const uint32_t s_IndexSize = GetU32(&s_Head[4]);
    const uint32_t s_InfoSize = GetU32(&s_Head[8]);
    p += 12;

    if (s_Count > kMaxCount || uint64_t(s_IndexSize) != uint64_t(s_Count) * 20 ||
        uint64_t(s_InfoSize) < uint64_t(s_Count) * 24)
        return s_Fail("inconsistent resource count");

    if (uint64_t(s_IndexSize) + s_InfoSize > kMaxRpkgSize)
        return s_Fail("resource index and info table too large");

    if (p_IsPatch) {
        s_Head.clear();

        if (!ReadAt(p_In, *s_FileSize, p, 4, s_Head))
            return s_Fail("file shorter than its header");

        const uint32_t s_Deletions = GetU32(&s_Head[0]);
        p += 4;
        s_Head.clear();

        if (s_Deletions > kMaxCount || !ReadAt(p_In, *s_FileSize, p, uint64_t(s_Deletions) * 8, s_Head))
            return s_Fail("file shorter than its deletion list");

        for (uint32_t i = 0; i < s_Deletions; ++i)
            s_Out.deletions.push_back(GetU64(&s_Head[i * size_t(8)]));

        p += uint64_t(s_Deletions) * 8;
    }

    // The index then the info table, read together.
    s_Head.clear();

    if (!ReadAt(p_In, *s_FileSize, p, uint64_t(s_IndexSize) + s_InfoSize, s_Head))
        return s_Fail("file shorter than its resource index and info table");

    const std::span<const uint8_t> s_Tables(s_Head);
    size_t q = s_IndexSize;

    for (uint32_t k = 0; k < s_Count; ++k) {
        if (s_Tables.size() - q < 24)
            return s_Fail("info table shorter than its resources");

        const uint8_t* s_Index = &s_Tables[k * size_t(20)];
        RpkgEntry s_Entry;
        s_Entry.hash = GetU64(s_Index);
        s_Entry.offset = GetU64(s_Index + 8);
        const uint32_t s_Stored = GetU32(s_Index + 16);
        s_Entry.stored = s_Stored & kSizeMask;
        s_Entry.xored = (s_Stored & kXoredFlag) != 0;

        for (int i = 3; i >= 0; --i)
            s_Entry.type.push_back(static_cast<char>(s_Tables[q + static_cast<size_t>(i)]));

        const uint32_t s_RefSize = GetU32(&s_Tables[q + 4]);
        s_Entry.finalSize = GetU32(&s_Tables[q + 12]);
        q += 24;

        if (s_RefSize > s_Tables.size() - q)
            return s_Fail("reference table runs past the info table");

        if (s_RefSize != 0) {
            if (s_RefSize < 4)
                return s_Fail("reference table shorter than its count");

            const uint32_t s_Refs = GetU32(&s_Tables[q]) & kRefCountMask;

            if (uint64_t(s_RefSize) != 4ull + 9ull * s_Refs)
                return s_Fail("reference table size does not match its count");
        }

        q += s_RefSize;
        s_Out.entries.push_back(std::move(s_Entry));
    }

    if (q != s_Tables.size())
        return s_Fail("info table size does not match its resources");

    s_Out.ok = true;
    return s_Out;
}

std::optional<std::vector<uint8_t>> ReadRpkgResource(std::istream& p_In, const RpkgEntry& p_Entry) {
    const std::optional<uint64_t> s_FileSize = StreamSize(p_In);

    if (!s_FileSize)
        return std::nullopt;

    if (p_Entry.stored > kMaxRpkgSize || p_Entry.finalSize > kMaxRpkgSize)
        return std::nullopt;

    std::vector<uint8_t> s_Bytes;

    if (!ReadAt(p_In, *s_FileSize, p_Entry.offset, p_Entry.stored != 0 ? p_Entry.stored : p_Entry.finalSize, s_Bytes))
        return std::nullopt;

    if (p_Entry.xored)
        XorRpkgData(s_Bytes);

    if (p_Entry.stored == 0)
        return s_Bytes;

    return Lz4Decode(s_Bytes, p_Entry.finalSize);
}

LocateResult LocateNewest(std::span<const std::pair<std::string, RpkgIndex>> p_Packages, int p_Chunk, uint64_t p_Hash) {
    LocateResult s_Out;
    std::vector<std::pair<int, const std::pair<std::string, RpkgIndex>*>> s_Order;

    for (const std::pair<std::string, RpkgIndex>& s_Package : p_Packages) {
        const std::optional<int> s_Patch = fr::PackagePatchNumber(s_Package.first);

        if (!s_Patch || *s_Patch >= fr::kFirstModPatch || !OfChunk(s_Package.first, p_Chunk))
            continue;

        if (!s_Package.second.ok) {
            s_Out.error = s_Package.first + " cannot be read: " + s_Package.second.error;
            return s_Out;
        }

        s_Order.emplace_back(*s_Patch, &s_Package);
    }

    std::sort(s_Order.begin(), s_Order.end(), [](const auto& p_A, const auto& p_B) {
        return p_A.first != p_B.first ? p_A.first < p_B.first : p_A.second->first < p_B.second->first;
    });

    if (s_Order.empty() || s_Order.front().first != 0) {
        s_Out.error = "no chunk" + std::to_string(p_Chunk) + ".rpkg";
        return s_Out;
    }

    for (size_t i = 1; i < s_Order.size(); ++i) {
        if (s_Order[i].first == s_Order[i - 1].first) {
            s_Out.error =
                s_Order[i - 1].second->first + " and " + s_Order[i].second->first + " have the same patch number";
            return s_Out;
        }
    }

    for (const auto& [s_Patch, s_Package] : s_Order) {
        const RpkgIndex& s_Index = s_Package->second;

        if (std::find(s_Index.deletions.begin(), s_Index.deletions.end(), p_Hash) != s_Index.deletions.end())
            s_Out.found.reset();

        for (const RpkgEntry& s_Entry : s_Index.entries) {
            if (s_Entry.hash == p_Hash)
                s_Out.found = Located {s_Package->first, s_Entry};
        }
    }

    return s_Out;
}

} // namespace pkg
