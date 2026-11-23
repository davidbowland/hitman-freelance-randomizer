#include "doctest.h"

#include "Rng.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>

TEST_CASE("splitmix_known_vector") {
    uint64_t state = 0;
    CHECK(fr::SplitMix64(state) == 0xE220A8397B1DCDAFull);
}

TEST_CASE("dial_hash_deterministic") {
    const uint64_t h = fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "ai", 95);
    CHECK(h == fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "ai", 95));
    CHECK(h != fr::DialHash(43, "assembly:/_pro/scenes/x.entity", "ai", 95));
    CHECK(h != fr::DialHash(42, "assembly:/_pro/scenes/y.entity", "ai", 95));
    CHECK(h != fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "weapons", 95));
    CHECK(h != fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "ai", 96));
}

TEST_CASE("fnv1a_known_vector") {
    CHECK(fr::Fnv1a64("a") == 0xaf63dc4c8601ec8cull);
}

// The generator (tools/landslide) pins the same value for the same bytes.
TEST_CASE("fnv1a_bytes_known_vector") {
    constexpr std::array<uint8_t, 10> kBytes {'f', 'r', 'e', 'e', 'l', 'a', 'n', 'c', 'e', 'r'};
    CHECK(fr::Fnv1a64(std::span<const uint8_t>(kBytes)) == 0xEC246B4D3C3EC404ull);
    CHECK(fr::Fnv1a64(std::span<const uint8_t>(kBytes)) == fr::Fnv1a64("freelancer"));
}

TEST_CASE("fnv1a_bytes_continues") {
    constexpr std::array<uint8_t, 10> kBytes {'f', 'r', 'e', 'e', 'l', 'a', 'n', 'c', 'e', 'r'};
    const std::span<const uint8_t> s_All(kBytes);
    CHECK(fr::Fnv1a64(s_All.subspan(4), fr::Fnv1a64(s_All.first(4))) == fr::Fnv1a64(s_All));
    CHECK(fr::Fnv1a64(std::span<const uint8_t>()) == fr::kFnv1a64Basis);
}

TEST_CASE("dial_hash_golden") {
    CHECK(fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "ai", 95) == 0x93FAE0B5DE1785EFull);
    CHECK(fr::DialHash(-7, "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity", "ai", 1) ==
          0xF97F89AAE67512D5ull);
}

TEST_CASE("dial_hash_id64_golden") {
    CHECK(fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "weapons", uint64_t {0xB73D9349324F8074ull}) ==
          0x788F3545B4C12F64ull);
    CHECK(fr::DialHash(-7, "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity", "weapons",
                       uint64_t {0xFEDCBA9876543210ull}) == 0x403CE78CC2B8D04Cull);
}

TEST_CASE("dial_hash_id64_matches_int_for_small_ids") {
    CHECK(fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "ai", uint64_t {95}) ==
          fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "ai", 95));
}

TEST_CASE("dial_hash_id64_uses_high_bits_and_lowercases_scene") {
    const uint64_t h = fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "weapons", uint64_t {0x1'0000'0005ull});
    CHECK(h != fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "weapons", uint64_t {5}));
    CHECK(h == fr::DialHash(42, "ASSEMBLY:/_PRO/Scenes/X.entity", "weapons", uint64_t {0x1'0000'0005ull}));
}

TEST_CASE("dial_hash_lowercases_scene") {
    CHECK(fr::DialHash(42, "ASSEMBLY:/_PRO/Scenes/X.entity", "ai", 95) ==
          fr::DialHash(42, "assembly:/_pro/scenes/x.entity", "ai", 95));
}

TEST_CASE("normal_moments") {
    constexpr int n = 10000;
    uint64_t state = 12345;
    double sum = 0.0;
    double sumSq = 0.0;
    for (int i = 0; i < n; ++i) {
        const double z = fr::NormalFromHash(fr::SplitMix64(state));
        sum += z;
        sumSq += z * z;
    }
    const double mean = sum / n;
    const double stddev = std::sqrt(sumSq / n - mean * mean);
    CHECK(std::abs(mean) < 0.03);
    CHECK(std::abs(stddev - 1.0) < 0.03);
}
