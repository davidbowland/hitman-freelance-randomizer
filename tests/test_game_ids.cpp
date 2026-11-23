#include "doctest.h"

#include "EngineText.h"
#include "GameIds.h"
#include "PinIds.h"

#include <Glacier/Hash.h>

#include <cstdint>
#include <string_view>

namespace {

// A resource path's runtime id, as the SDK's ResId computes it: bytes 1-7 of
// the path's MD5, big-endian.
template <size_t N>
constexpr uint64_t RuntimeId(const char (&p_Path)[N]) {
    const auto s_Hash = Hash::MD5<N - 1>(std::string_view(p_Path, N - 1));
    const uint32_t s_High = ((s_Hash.A >> 24) & 0xFF) | ((s_Hash.A >> 8) & 0xFF00) | ((s_Hash.A << 8) & 0xFF0000);
    const uint32_t s_Low = ((s_Hash.B >> 24) & 0xFF) | ((s_Hash.B >> 8) & 0xFF00) | ((s_Hash.B << 8) & 0xFF0000) |
                           ((s_Hash.B << 24) & 0xFF000000);
    return static_cast<uint64_t>(s_High) << 32 | s_Low;
}

} // namespace

TEST_CASE("game ids: the evergreen_setup brick's runtime id is its path's hash") {
    CHECK(RuntimeId("[assembly:/_pro/scenes/bricks/evergreen_setup.brick].pc_entitytype") == fr::kEvergreenSetupBrick);
}

TEST_CASE("pin ids: the CRCs the engine uses") {
    // PinIds.h pins every id with a static_assert; these are spot checks
    // against values from the game's own data.
    CHECK(fr::kPinItemUsed == 0x8A7DCCB8u);
    CHECK(fr::kPinOnPickFailed == 0x8D645AC2u);
    CHECK(fr::kPinItemSpawned != fr::kPinSpawnerItemSpawned);
}

TEST_CASE("engine text: faults, ids, entities and scenes") {
    CHECK(FaultText(0xC0000005u) == "<fault 0xC0000005>");
    CHECK(IdText(0x00a1b2c3d4e5f607ull) == "00a1b2c3d4e5f607");
    CHECK(EntityIdText(0x2a, "ZValueBool_Basic") == "000000000000002a (ZValueBool_Basic)");
    CHECK(EntityIdText(0x2a, "") == "000000000000002a (<no class>)");
    CHECK(ShortSceneName("assembly:/_pro/scenes/missions/paris/_scene_paris.entity") == "_scene_paris");
    CHECK(ShortSceneName("plain") == "plain");
}
