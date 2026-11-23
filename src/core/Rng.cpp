#include "Rng.h"

#include <cmath>
#include <numbers>

namespace fr {

uint64_t SplitMix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

namespace {

uint64_t Fnv1a64Impl(std::string_view s, bool lower) {
    uint64_t h = kFnv1a64Basis;
    for (char c : s) {
        if (lower && c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        h ^= static_cast<uint8_t>(c);
        h *= 0x100000001B3ull;
    }
    return h;
}

} // namespace

uint64_t Fnv1a64(std::string_view s) {
    return Fnv1a64Impl(s, false);
}

uint64_t Fnv1a64(std::span<const uint8_t> p_Bytes, uint64_t p_Hash) {
    for (uint8_t b : p_Bytes) {
        p_Hash ^= b;
        p_Hash *= 0x100000001B3ull;
    }
    return p_Hash;
}

uint64_t DialHash(int32_t seed, std::string_view scene, std::string_view feature, int32_t param) {
    return DialHash(seed, scene, feature, static_cast<uint64_t>(static_cast<uint32_t>(param)));
}

uint64_t DialHash(int32_t seed, std::string_view scene, std::string_view feature, uint64_t id) {
    uint64_t st = static_cast<uint64_t>(static_cast<uint32_t>(seed));
    const uint64_t h = SplitMix64(st) ^ Fnv1a64Impl(scene, true) ^ Fnv1a64(feature) ^ id;
    st = h;
    return SplitMix64(st);
}

double NormalFromHash(uint64_t h) {
    uint64_t st = h;
    // u1 in (0, 1] so the log is finite; u2 in [0, 1).
    const double u1 = static_cast<double>((SplitMix64(st) >> 11) + 1) * 0x1.0p-53;
    const double u2 = static_cast<double>(SplitMix64(st) >> 11) * 0x1.0p-53;
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2);
}

double UniformFromHash(uint64_t h) {
    uint64_t st = h;
    return static_cast<double>(SplitMix64(st) >> 11) * 0x1.0p-53;
}

} // namespace fr
