#include "PatchPackage.h"

#include "RpkgRead.h"
#include "TextNoCase.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace pkg {

namespace {

namespace fs = std::filesystem;

constexpr std::array<uint8_t, 8> kXorKey {0xDC, 0x45, 0xA6, 0x9C, 0xD3, 0x72, 0x4C, 0xAB};

constexpr uint32_t kXoredFlag = 0x80000000u;
constexpr uint32_t kSizeMask = 0x3FFFFFFFu;
constexpr uint32_t kRefCountFlags = 0xC0000000u;
constexpr uint32_t kRefCountMask = 0x3FFFFFFFu;

void PutU32(std::vector<uint8_t>& p_Out, uint32_t p_Value) {
    for (int i = 0; i < 4; ++i)
        p_Out.push_back(static_cast<uint8_t>(p_Value >> (8 * i)));
}

void PutU64(std::vector<uint8_t>& p_Out, uint64_t p_Value) {
    for (int i = 0; i < 8; ++i)
        p_Out.push_back(static_cast<uint8_t>(p_Value >> (8 * i)));
}

uint32_t GetU32(const uint8_t* p_Data) {
    return static_cast<uint32_t>(p_Data[0]) | static_cast<uint32_t>(p_Data[1]) << 8 |
           static_cast<uint32_t>(p_Data[2]) << 16 | static_cast<uint32_t>(p_Data[3]) << 24;
}

uint64_t GetU64(const uint8_t* p_Data) {
    return static_cast<uint64_t>(GetU32(p_Data)) | static_cast<uint64_t>(GetU32(p_Data + 4)) << 32;
}

using fr::StartsWithNoCase;

// Reads p_Size bytes at p_At of p_File; false when that runs past its end.
bool Within(std::span<const uint8_t> p_File, uint64_t p_At, uint64_t p_Size) {
    return p_At <= p_File.size() && p_Size <= p_File.size() - p_At;
}

// An LZ4 block holding p_Data as one literal run.
std::vector<uint8_t> Lz4LiteralBlock(std::span<const uint8_t> p_Data) {
    std::vector<uint8_t> s_Out;
    const size_t s_Length = p_Data.size();
    s_Out.reserve(s_Length + s_Length / 255 + 2);

    if (s_Length < 15) {
        s_Out.push_back(static_cast<uint8_t>(s_Length << 4));
    }
    else {
        s_Out.push_back(0xF0);
        size_t s_Rest = s_Length - 15;

        while (s_Rest >= 255) {
            s_Out.push_back(255);
            s_Rest -= 255;
        }

        s_Out.push_back(static_cast<uint8_t>(s_Rest));
    }

    s_Out.insert(s_Out.end(), p_Data.begin(), p_Data.end());
    return s_Out;
}

// p_Held is not empty, each hash once, every one of them in p_Own or p_Former.
bool HoldsOnlyHashes(std::vector<uint64_t> p_Held, std::span<const uint64_t> p_Own,
                     std::span<const uint64_t> p_Former) {
    if (p_Held.empty())
        return false;

    for (const uint64_t s_Hash : p_Held) {
        if (std::find(p_Own.begin(), p_Own.end(), s_Hash) == p_Own.end() &&
            std::find(p_Former.begin(), p_Former.end(), s_Hash) == p_Former.end())
            return false;
    }

    std::sort(p_Held.begin(), p_Held.end());
    return std::adjacent_find(p_Held.begin(), p_Held.end()) == p_Held.end();
}

} // namespace

void XorRpkgData(std::span<uint8_t> p_Data) {
    for (size_t i = 0; i < p_Data.size(); ++i)
        p_Data[i] ^= kXorKey[i % kXorKey.size()];
}

std::optional<std::vector<uint8_t>> Lz4Decode(std::span<const uint8_t> p_Block, size_t p_Size) {
    if (p_Size > kMaxRpkgSize)
        return std::nullopt;

    std::vector<uint8_t> s_Out;
    // A block decodes to at most about 255 times its size, whatever p_Size claims.
    s_Out.reserve(std::min<uint64_t>(p_Size, uint64_t(p_Block.size()) * 255 + 16));
    size_t i = 0;

    // Reads an extended length (bytes of 255 continue it).
    const auto s_Extend = [&](size_t& p_Length) {
        uint8_t b = 255;

        while (b == 255) {
            if (i >= p_Block.size())
                return false;

            b = p_Block[i++];
            p_Length += b;
        }

        return true;
    };

    while (i < p_Block.size()) {
        const uint8_t s_Token = p_Block[i++];
        size_t s_Literals = s_Token >> 4;

        if (s_Literals == 15 && !s_Extend(s_Literals))
            return std::nullopt;

        if (s_Literals > p_Block.size() - i || s_Out.size() + s_Literals > p_Size)
            return std::nullopt;

        s_Out.insert(s_Out.end(), p_Block.begin() + static_cast<ptrdiff_t>(i),
                     p_Block.begin() + static_cast<ptrdiff_t>(i + s_Literals));
        i += s_Literals;

        if (i == p_Block.size())
            break;

        if (i + 2 > p_Block.size())
            return std::nullopt;

        const size_t s_Distance = p_Block[i] | p_Block[i + 1] << 8;
        i += 2;
        size_t s_Match = s_Token & 15;

        if (s_Match == 15 && !s_Extend(s_Match))
            return std::nullopt;

        s_Match += 4;

        if (s_Distance == 0 || s_Distance > s_Out.size() || s_Out.size() + s_Match > p_Size)
            return std::nullopt;

        const size_t s_From = s_Out.size() - s_Distance;

        for (size_t k = 0; k < s_Match; ++k)
            s_Out.push_back(s_Out[s_From + k]);
    }

    if (s_Out.size() != p_Size)
        return std::nullopt;

    return s_Out;
}

std::vector<uint8_t> BuildPatchPackage(std::span<const PackageResource> p_Resources) {
    constexpr uint32_t kHeaderSize = 20;
    const uint32_t s_IndexSize = static_cast<uint32_t>(p_Resources.size()) * 20u;

    std::vector<std::vector<uint8_t>> s_Payloads;
    s_Payloads.reserve(p_Resources.size());
    uint32_t s_InfoSize = 0;

    for (const PackageResource& s_Resource : p_Resources) {
        if (s_Resource.type.size() != 4)
            return {};

        std::vector<uint8_t> s_Payload = Lz4LiteralBlock(std::span<const uint8_t>(s_Resource.data));
        XorRpkgData(s_Payload);
        s_Payloads.push_back(std::move(s_Payload));
        s_InfoSize += 24u + (s_Resource.refs.empty() ? 0u : 4u + 9u * static_cast<uint32_t>(s_Resource.refs.size()));
    }

    std::vector<uint8_t> s_Out;

    // Header: magic, resource count, index size, info table size, deletions.
    s_Out.insert(s_Out.end(), {'G', 'K', 'P', 'R'});
    PutU32(s_Out, static_cast<uint32_t>(p_Resources.size()));
    PutU32(s_Out, s_IndexSize);
    PutU32(s_Out, s_InfoSize);
    PutU32(s_Out, 0);

    // Index: hash, data offset, stored size with the XOR flag.
    uint64_t s_Offset = kHeaderSize + s_IndexSize + s_InfoSize;

    for (size_t i = 0; i < p_Resources.size(); ++i) {
        PutU64(s_Out, p_Resources[i].hash);
        PutU64(s_Out, s_Offset);
        PutU32(s_Out, kXoredFlag | (static_cast<uint32_t>(s_Payloads[i].size()) & kSizeMask));
        s_Offset += s_Payloads[i].size();
    }

    // Info: type (reversed), reference table size, unknown, final size,
    // memory sizes, reference table.
    for (const PackageResource& s_Resource : p_Resources) {
        for (size_t i = 0; i < 4; ++i)
            s_Out.push_back(static_cast<uint8_t>(s_Resource.type[3 - i]));

        const uint32_t s_Refs = static_cast<uint32_t>(s_Resource.refs.size());
        PutU32(s_Out, s_Refs == 0 ? 0u : 4u + 9u * s_Refs);
        PutU32(s_Out, 0);
        PutU32(s_Out, static_cast<uint32_t>(s_Resource.data.size()));
        PutU32(s_Out, s_Resource.memSize);
        PutU32(s_Out, s_Resource.vmemSize);

        if (s_Refs != 0) {
            PutU32(s_Out, kRefCountFlags | s_Refs);

            for (const PackageRef& s_Ref : s_Resource.refs)
                s_Out.push_back(s_Ref.flags);

            for (const PackageRef& s_Ref : s_Resource.refs)
                PutU64(s_Out, s_Ref.hash);
        }
    }

    for (const std::vector<uint8_t>& s_Payload : s_Payloads)
        s_Out.insert(s_Out.end(), s_Payload.begin(), s_Payload.end());

    return s_Out;
}

ParsedPackage ParsePatchPackage(std::span<const uint8_t> p_File, bool p_IsPatch) {
    ParsedPackage s_Out;
    const auto s_Fail = [&](std::string p_Error) {
        s_Out.ok = false;
        s_Out.error = std::move(p_Error);
        s_Out.resources.clear();
        return s_Out;
    };

    if (p_File.size() < 4)
        return s_Fail("file shorter than its header");

    uint64_t p = 4;

    if (std::memcmp(p_File.data(), "2KPR", 4) == 0) {
        p += 9;
    }
    else if (std::memcmp(p_File.data(), "GKPR", 4) == 0) {
        s_Out.version1 = true;
    }
    else {
        return s_Fail("not a resource package (bad magic)");
    }

    if (!Within(p_File, p, 12))
        return s_Fail("file shorter than its header");

    const uint32_t s_Count = GetU32(&p_File[p]);
    const uint32_t s_IndexSize = GetU32(&p_File[p + 4]);
    const uint32_t s_InfoSize = GetU32(&p_File[p + 8]);
    p += 12;

    if (s_Count > (1u << 24) || s_IndexSize != s_Count * 20u)
        return s_Fail("inconsistent resource count");

    if (p_IsPatch) {
        if (!Within(p_File, p, 4))
            return s_Fail("file shorter than its header");

        const uint32_t s_Deletions = GetU32(&p_File[p]);
        p += 4;

        if (s_Deletions > (1u << 24) || !Within(p_File, p, uint64_t(s_Deletions) * 8))
            return s_Fail("file shorter than its deletion list");

        for (uint32_t i = 0; i < s_Deletions; ++i)
            s_Out.deletions.push_back(GetU64(&p_File[p + i * 8ull]));

        p += uint64_t(s_Deletions) * 8;
    }

    if (!Within(p_File, p, s_IndexSize))
        return s_Fail("file shorter than its resource index");

    const uint64_t s_Index = p;
    p += s_IndexSize;

    if (!Within(p_File, p, s_InfoSize))
        return s_Fail("file shorter than its info table");

    const uint64_t s_InfoEnd = p + s_InfoSize;

    for (uint32_t k = 0; k < s_Count; ++k) {
        const uint8_t* s_Entry = &p_File[s_Index + k * 20ull];
        const uint64_t s_DataOffset = GetU64(s_Entry + 8);
        const uint32_t s_Stored = GetU32(s_Entry + 16);

        if (p + 24 > s_InfoEnd)
            return s_Fail("info table shorter than its resources");

        PackageResource s_Resource;
        s_Resource.hash = GetU64(s_Entry);

        for (int i = 3; i >= 0; --i)
            s_Resource.type.push_back(static_cast<char>(p_File[p + i]));

        const uint32_t s_RefSize = GetU32(&p_File[p + 4]);
        const uint32_t s_Final = GetU32(&p_File[p + 12]);
        s_Resource.memSize = GetU32(&p_File[p + 16]);
        s_Resource.vmemSize = GetU32(&p_File[p + 20]);
        p += 24;

        if (s_RefSize > s_InfoEnd - p)
            return s_Fail("reference table runs past the info table");

        if (s_RefSize != 0) {
            if (s_RefSize < 4)
                return s_Fail("reference table shorter than its count");

            const uint32_t s_Refs = GetU32(&p_File[p]) & kRefCountMask;

            if (uint64_t(s_RefSize) != 4ull + 9ull * s_Refs)
                return s_Fail("reference table size does not match its count");

            for (uint32_t i = 0; i < s_Refs; ++i)
                s_Resource.refs.push_back({p_File[p + 4 + i], GetU64(&p_File[p + 4 + s_Refs + i * 8ull])});
        }

        p += s_RefSize;

        const uint32_t s_Compressed = s_Stored & kSizeMask;
        const uint64_t s_StoredSize = s_Compressed != 0 ? s_Compressed : s_Final;

        if (!Within(p_File, s_DataOffset, s_StoredSize))
            return s_Fail("resource data runs past the end of the file");

        std::vector<uint8_t> s_Bytes(p_File.begin() + static_cast<ptrdiff_t>(s_DataOffset),
                                     p_File.begin() + static_cast<ptrdiff_t>(s_DataOffset + s_StoredSize));

        if (s_Stored & kXoredFlag)
            XorRpkgData(s_Bytes);

        if (s_Compressed != 0) {
            std::optional<std::vector<uint8_t>> s_Decoded = Lz4Decode(s_Bytes, s_Final);

            if (!s_Decoded)
                return s_Fail("resource data does not decode to its final size");

            s_Resource.data = std::move(*s_Decoded);
        }
        else {
            s_Resource.data = std::move(s_Bytes);
        }

        s_Out.resources.push_back(std::move(s_Resource));
    }

    if (p != s_InfoEnd)
        return s_Fail("info table size does not match its resources");

    s_Out.ok = true;
    return s_Out;
}

bool HoldsOnly(const ParsedPackage& p_Parsed, std::span<const uint64_t> p_Own, std::span<const uint64_t> p_Former) {
    if (!p_Parsed.ok || !p_Parsed.version1 || !p_Parsed.deletions.empty())
        return false;

    std::vector<uint64_t> s_Held;

    for (const PackageResource& s_Resource : p_Parsed.resources)
        s_Held.push_back(s_Resource.hash);

    return HoldsOnlyHashes(std::move(s_Held), p_Own, p_Former);
}

bool HoldsOnly(const RpkgIndex& p_Index, std::span<const uint64_t> p_Own, std::span<const uint64_t> p_Former) {
    if (!p_Index.ok || !p_Index.version1 || !p_Index.deletions.empty())
        return false;

    std::vector<uint64_t> s_Held;

    for (const RpkgEntry& s_Entry : p_Index.entries)
        s_Held.push_back(s_Entry.hash);

    return HoldsOnlyHashes(std::move(s_Held), p_Own, p_Former);
}

std::optional<int> ChunkPatchNumber(int p_Chunk, std::string_view p_FileName) {
    const std::string s_Prefix = "chunk" + std::to_string(p_Chunk) + "patch";
    constexpr std::string_view kSuffix = ".rpkg";

    if (p_Chunk < 0 || !StartsWithNoCase(p_FileName, s_Prefix) ||
        p_FileName.size() <= s_Prefix.size() + kSuffix.size() ||
        !StartsWithNoCase(p_FileName.substr(p_FileName.size() - kSuffix.size()), kSuffix))
        return std::nullopt;

    const std::string_view s_Digits =
        p_FileName.substr(s_Prefix.size(), p_FileName.size() - s_Prefix.size() - kSuffix.size());

    if (s_Digits.size() > 6)
        return std::nullopt;

    int s_Number = 0;

    for (const char c : s_Digits) {
        if (c < '0' || c > '9')
            return std::nullopt;

        s_Number = s_Number * 10 + (c - '0');
    }

    return s_Number;
}

std::vector<int> PatchesUnlockedByRaise(std::span<const int> p_Numbers, int p_Original, int p_Target) {
    std::vector<int> s_Out;

    for (const int s_Number : p_Numbers) {
        if (s_Number > p_Original && s_Number < p_Target)
            s_Out.push_back(s_Number);
    }

    return s_Out;
}

bool IniHasSection(std::string_view p_Ini, std::string_view p_Section) {
    size_t s_Start = 0;

    while (s_Start <= p_Ini.size()) {
        const size_t s_End = std::min(p_Ini.find('\n', s_Start), p_Ini.size());
        std::string_view s_Line = p_Ini.substr(s_Start, s_End - s_Start);

        while (!s_Line.empty() && std::isspace(static_cast<unsigned char>(s_Line.front())))
            s_Line.remove_prefix(1);

        while (!s_Line.empty() && std::isspace(static_cast<unsigned char>(s_Line.back())))
            s_Line.remove_suffix(1);

        if (s_Line.size() == p_Section.size() + 2 && s_Line.front() == '[' && s_Line.back() == ']' &&
            StartsWithNoCase(s_Line.substr(1, p_Section.size()), p_Section))
            return true;

        s_Start = s_End + 1;
    }

    return false;
}

FileAction DecidePackageFile(bool p_On, const std::vector<uint8_t>* p_Current, bool p_Readable,
                             std::span<const uint64_t> p_OwnHashes, std::span<const uint8_t> p_Wanted,
                             std::span<const uint64_t> p_FormerHashes) {
    if (!p_Readable)
        return FileAction::NotOurs;

    if (p_Current && !HoldsOnly(ParsePatchPackage(*p_Current), p_OwnHashes, p_FormerHashes))
        return FileAction::NotOurs;

    if (!p_On)
        return p_Current ? FileAction::Remove : FileAction::None;

    // On with nothing to write: never write an empty package; leave any file as it is.
    if (p_Wanted.empty())
        return FileAction::None;

    if (p_Current && std::equal(p_Current->begin(), p_Current->end(), p_Wanted.begin(), p_Wanted.end()))
        return FileAction::UpToDate;

    return FileAction::Write;
}

RaiseDecision DecideRaise(int32_t p_Index, std::string_view p_Id, int32_t p_Level, int32_t p_OwnIndex,
                          std::string_view p_OwnId, int p_Target, std::span<const int> p_OtherPatches,
                          bool p_MaxPatchLevel, std::string_view p_FileName) {
    RaiseDecision s_Out;
    s_Out.level = p_Level;

    if (p_Index != p_OwnIndex)
        return s_Out;

    if (p_Id.size() != p_OwnId.size() || !StartsWithNoCase(p_Id, p_OwnId)) {
        s_Out.note = " (not raised: the partition's name is not \"" + std::string(p_OwnId) + "\")";
        return s_Out;
    }

    s_Out.ours = true;

    if (p_Target <= 0)
        return s_Out;

    if (p_Level >= p_Target) {
        s_Out.note = " (already reaches " + std::string(p_FileName) + ")";
        return s_Out;
    }

    if (p_MaxPatchLevel) {
        s_Out.note = " (not raised: MaxPatchLevel is enabled and raises it)";
        return s_Out;
    }

    s_Out.unlocked = PatchesUnlockedByRaise(p_OtherPatches, p_Level, p_Target);

    if (!s_Out.unlocked.empty()) {
        std::string s_Names;
        const std::string s_Prefix(p_FileName.substr(0, p_FileName.find("patch") + 5));

        for (const int s_Number : s_Out.unlocked)
            s_Names += (s_Names.empty() ? "" : ", ") + s_Prefix + std::to_string(s_Number) + ".rpkg";

        s_Out.note = " (not raised to " + std::to_string(p_Target) + ": that would also mount " + s_Names +
                     ", which the game's own level leaves out; " + std::string(p_FileName) + " is not mounted)";
        return s_Out;
    }

    s_Out.level = p_Target;
    s_Out.note = " -> " + std::to_string(p_Target) + " (to mount " + std::string(p_FileName) + ")";
    return s_Out;
}

std::vector<uint8_t> ReadFileBytes(const fs::path& p_Path, bool* p_Ok, size_t p_MaxSize) {
    std::ifstream s_In(p_Path, std::ios::binary);
    *p_Ok = static_cast<bool>(s_In);

    if (!*p_Ok)
        return {};

    std::vector<uint8_t> s_Out;
    std::array<char, 65536> s_Buffer {};

    while (s_In) {
        s_In.read(s_Buffer.data(), static_cast<std::streamsize>(s_Buffer.size()));
        const size_t s_Got = static_cast<size_t>(s_In.gcount());

        if (s_Out.size() + s_Got > p_MaxSize) {
            *p_Ok = false;
            return {};
        }

        s_Out.insert(s_Out.end(), s_Buffer.begin(), s_Buffer.begin() + static_cast<ptrdiff_t>(s_Got));
    }

    // A read error ends the loop early, as the file's end does.
    std::error_code s_Error;
    const uintmax_t s_Size = fs::file_size(p_Path, s_Error);

    if (s_Error || s_Size != s_Out.size()) {
        *p_Ok = false;
        return {};
    }

    return s_Out;
}

bool WriteFileAtomically(const fs::path& p_Path, const std::vector<uint8_t>& p_Data, std::string* p_Error) {
    fs::path s_Temp = p_Path;
    s_Temp += ".tmp";
    std::error_code s_Error;
    bool s_Written = false;
    {
        std::ofstream s_Out(s_Temp, std::ios::binary | std::ios::trunc);

        if (s_Out) {
            s_Out.write(reinterpret_cast<const char*>(p_Data.data()), static_cast<std::streamsize>(p_Data.size()));
            s_Out.close();
            s_Written = static_cast<bool>(s_Out);
        }
    }

    if (!s_Written) {
        *p_Error = "cannot write " + s_Temp.string();
        fs::remove(s_Temp, s_Error);
        return false;
    }

    fs::rename(s_Temp, p_Path, s_Error);

    if (s_Error) {
        *p_Error = "cannot rename " + s_Temp.string() + " to " + p_Path.filename().string() + ": " + s_Error.message();
        fs::remove(s_Temp, s_Error);
        return false;
    }

    return true;
}

} // namespace pkg
