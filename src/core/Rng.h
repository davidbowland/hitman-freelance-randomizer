#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace fr {

// SplitMix64: advances state and returns the next output.
uint64_t SplitMix64(uint64_t& state);

// FNV-1a 64-bit offset basis: the hash of no bytes.
inline constexpr uint64_t kFnv1a64Basis = 0xCBF29CE484222325ull;

// FNV-1a 64-bit hash of the bytes of s.
uint64_t Fnv1a64(std::string_view s);

// FNV-1a 64-bit hash of p_Bytes, continuing from p_Hash (a hash of earlier
// bytes), so hashing in pieces equals hashing the whole.
uint64_t Fnv1a64(std::span<const uint8_t> p_Bytes, uint64_t p_Hash = kFnv1a64Basis);

// Deterministic hash of one roll. The feature name and parameter are part of
// the hash so adding features never shifts existing rolls. The scene resource
// is hashed ASCII lower-cased, so rolls do not depend on its case.
uint64_t DialHash(int32_t seed, std::string_view scene, std::string_view feature, int32_t param);

// The same hash with a 64-bit id (e.g. an entity id) in place of the
// parameter; for ids below 2^32 it equals the int32_t form with that value.
uint64_t DialHash(int32_t seed, std::string_view scene, std::string_view feature, uint64_t id);

// Standard normal sample derived deterministically from h (Box-Muller).
double NormalFromHash(uint64_t h);

// Uniform sample in [0, 1) derived deterministically from h.
double UniformFromHash(uint64_t h);

} // namespace fr
