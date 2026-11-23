#include "Guid.h"

#include <cstdio>
#include <cstring>

namespace fr {

bool Guid::IsEmpty() const {
    return High() == 0 && Low() == 0;
}

uint64_t Guid::High() const {
    uint64_t high = 0;
    std::memcpy(&high, this, sizeof(high));
    return high;
}

uint64_t Guid::Low() const {
    uint64_t low = 0;
    std::memcpy(&low, reinterpret_cast<const unsigned char*>(this) + sizeof(low), sizeof(low));
    return low;
}

Guid Guid::FromHalves(uint64_t high, uint64_t low) {
    Guid guid;
    std::memcpy(reinterpret_cast<unsigned char*>(&guid), &high, sizeof(high));
    std::memcpy(reinterpret_cast<unsigned char*>(&guid) + sizeof(high), &low, sizeof(low));
    return guid;
}

bool operator==(const Guid& a, const Guid& b) {
    return a.High() == b.High() && a.Low() == b.Low();
}

bool operator!=(const Guid& a, const Guid& b) {
    return !(a == b);
}

bool operator<(const Guid& a, const Guid& b) {
    if (a.High() != b.High())
        return a.High() < b.High();

    return a.Low() < b.Low();
}

std::string FormatGuid(const Guid& guid) {
    char text[40] = {};
    std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  static_cast<unsigned>(guid.data1), static_cast<unsigned>(guid.data2),
                  static_cast<unsigned>(guid.data3), guid.data4[0], guid.data4[1], guid.data4[2], guid.data4[3],
                  guid.data4[4], guid.data4[5], guid.data4[6], guid.data4[7]);
    return text;
}

namespace {

int HexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Reads 2 * bytes hex digits at text[pos] into value (big-endian digits).
bool ReadHex(std::string_view text, size_t pos, size_t bytes, uint64_t& value) {
    value = 0;

    for (size_t i = 0; i < bytes * 2; ++i) {
        const int digit = HexDigit(text[pos + i]);

        if (digit < 0)
            return false;

        value = value << 4 | static_cast<uint64_t>(digit);
    }

    return true;
}

} // namespace

std::optional<Guid> ParseGuid(std::string_view text) {
    // xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        return std::nullopt;

    Guid guid;
    uint64_t value = 0;

    if (!ReadHex(text, 0, 4, value))
        return std::nullopt;
    guid.data1 = static_cast<uint32_t>(value);

    if (!ReadHex(text, 9, 2, value))
        return std::nullopt;
    guid.data2 = static_cast<uint16_t>(value);

    if (!ReadHex(text, 14, 2, value))
        return std::nullopt;
    guid.data3 = static_cast<uint16_t>(value);

    for (size_t i = 0; i < 8; ++i) {
        const size_t pos = i < 2 ? 19 + i * 2 : 24 + (i - 2) * 2;

        if (!ReadHex(text, pos, 1, value))
            return std::nullopt;
        guid.data4[i] = static_cast<uint8_t>(value);
    }

    return guid;
}

} // namespace fr
