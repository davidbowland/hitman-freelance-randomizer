#pragma once
// Also guarded by a macro: mod headers reach this file through "../core/"
// paths too, and #pragma once (file identity) has failed to match the two
// paths under parallel builds on the Docker bind mount.
#ifndef FR_CORE_GUID_H
#define FR_CORE_GUID_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fr {

// A repository GUID with the engine's ZGuid layout (data1, data2, data3,
// data4[8]; 16 bytes), so it can be copied byte for byte out of engine memory.
struct Guid {
    uint32_t data1 = 0;
    uint16_t data2 = 0;
    uint16_t data3 = 0;
    uint8_t data4[8] = {};

    bool IsEmpty() const;
    // The two 64-bit halves (engine m_nHigh/m_nLow on little-endian), for keys.
    uint64_t High() const;
    uint64_t Low() const;

    // The GUID whose High() and Low() are the given halves.
    static Guid FromHalves(uint64_t high, uint64_t low);
};

static_assert(sizeof(Guid) == 16, "Guid must match the engine's 16-byte ZGuid layout");

bool operator==(const Guid& a, const Guid& b);
bool operator!=(const Guid& a, const Guid& b);
// Byte order of (High, Low); any strict order works for sorting and lookup.
bool operator<(const Guid& a, const Guid& b);

// Lower-case "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx", the form the game's
// *.repository.json files use (and the SDK's ZGuid::ToString up to case).
std::string FormatGuid(const Guid& guid);

// Parses the dashed form (either case); empty when malformed.
std::optional<Guid> ParseGuid(std::string_view text);

} // namespace fr

#endif // FR_CORE_GUID_H
