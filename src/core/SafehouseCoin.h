#pragma once

// Pure part of the safehouse coin (safehouse_coin).
//
// What the safehouse does ([assembly:/_pro/scenes/missions/snug/mission_vanilla.brick],
// brick
// 00A9B93A825F5BDC): a GameEventListener (189ded76e343ed09, m_eEvent 3) fires
// EventOccurred into the In pin of a ZSignalRandomRouteEntity_void
// (49cecc4d19f875d3; m_nMaxOutPinNum 3, no shuffle, no seed), whose Out1 /
// Out2 / Out3 go to debug_SpawnItem of three Setpiece_Evergreen_CosmeticItem
// spawners (f649cc866fe1a303, b2950a237d56072c, 97a6a691f1535234); each
// spawner's itemSpawned makes its bird's nest visible (birdsnest_a
// c0ecde942e7f8ebe, birdsnest_a_01 37e71f3565430c9b, birdsnest_a_02
// 6c5fe35c072a20be).
//
// Measured in game: the
// spawners' ZRepositoryID property (id a1e67258) is the coin
// (dda002e9-02b1-4208-82a5-cf059f3c79cf) on spots 2 and 3 and empty on spot
// 1. So Out1 is the vanilla "no coin" outcome: the spawner has no item, never
// signals itemSpawned, and its nest stays hidden. Out2: the coin on the stump
// by the helicopter.
//
// The fix (CoinPlanner): a route output to a spot without an item is
// blocked and the route's In is signaled again on the next frame, so the
// game's own route picks again among the same pins until it picks a coin
// spot (the choice among the coin spots stays the game's random pick). After
// kCoinMaxRetries empty picks, or when a re-signaled In brings no output, the
// mod signals debug_SpawnItem on a random coin spot itself. The listener
// firing with no route output, or nothing firing in kCoinRouteDeadline of
// play, starts the same retries. Once a coin spot is picked, every later
// route output is blocked, so exactly one coin spawns.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fr {

// The safehouse brick ([assembly:/_pro/scenes/missions/snug/mission_vanilla.brick]).
inline constexpr uint64_t kCoinBrickRid = 0x00A9'B93A'825F'5BDCull;

inline constexpr uint64_t kCoinListenerId = 0x189d'ed76'e343'ed09ull; // GameEventListener -> route In
inline constexpr uint64_t kCoinRouteId = 0x49ce'cc4d'19f8'75d3ull;    // SignalRandomRoute_Void

// How many spots the route's connected Out pins reach.
inline constexpr size_t kCoinSpots = 3;

// Spot i: the spawner on Out(i+1) and the nest its itemSpawned makes visible.
inline constexpr std::array<uint64_t, kCoinSpots> kCoinSpawnerIds = {
    0xf649'cc86'6fe1'a303ull, // Setpiece_Evergreen_CosmeticItem (Out1): no item in vanilla
    0xb295'0a23'7d56'072cull, // Setpiece_Evergreen_CosmeticItem_04 (Out2): coin
    0x97a6'a691'f153'5234ull, // Setpiece_Evergreen_CosmeticItem_05 (Out3): coin
};
inline constexpr std::array<uint64_t, kCoinSpots> kCoinNestIds = {
    0xc0ec'de94'2e7f'8ebeull, // birdsnest_a
    0x37e7'1f35'6543'0c9bull, // birdsnest_a_01
    0x6c5f'e35c'072a'20beull, // birdsnest_a_02
};

// The spawners' ZRepositoryID property (its name is not in the type data).
inline constexpr uint32_t kCoinItemPropertyId = 0xa1e6'7258u;

// The coin's repository id (what spots 2 and 3 hold in vanilla).
inline constexpr std::string_view kCoinRepositoryId = "dda002e9-02b1-4208-82a5-cf059f3c79cf";

// The vanilla item spots (measured in game), used when the spawners'
// items cannot be read.
inline constexpr std::array<bool, kCoinSpots> kCoinVanillaItemSpots = {false, true, true};

// The route's class (its type's first interface).
inline constexpr const char* kCoinRouteClass = "ZSignalRandomRouteEntity_void";

// The most Out pins the mod watches on the route (Out1..OutN).
inline constexpr int kCoinMaxWatchedOut = 16;

// Seconds of play without the listener and without a route output before
// the mod starts the retries itself.
inline constexpr double kCoinRouteDeadline = 60.0;

// Seconds after the listener fired (or the mod signaled In) for the route to
// signal an Out pin.
inline constexpr double kCoinRouteGrace = 2.0;

// Empty picks re-signaled before the mod spawns the coin itself (all empty:
// (1/3)^8 with one empty spot of three).
inline constexpr int kCoinMaxRetries = 8;

// Seconds after a coin spot was picked (or spawned) for itemSpawned to be seen
// before it is logged as missing (log only).
inline constexpr double kCoinSpawnGrace = 10.0;

// The spot (0-based) a route Out pin number (1-based, as in "Out2") feeds, or
// empty when nothing is connected to it.
std::optional<size_t> CoinSpotOfOut(int p_OutNumber, size_t p_Spots = kCoinSpots);

// A uniform pick among the spots with an item, from some entropy; empty
// when no spot has one.
std::optional<size_t> PickCoinSpot(uint64_t p_Entropy, const std::array<bool, kCoinSpots>& p_HasItem);

enum class CoinPinVerdict { Pass, Block };

enum class CoinActionKind {
    None,
    SignalRouteIn, // let the route pick again
    SpawnDirect,   // signal debug_SpawnItem on spot
};

struct CoinAction {
    CoinActionKind kind = CoinActionKind::None;
    size_t spot = 0; // SpawnDirect
    std::string why;
};

// The visit's state machine. Times are seconds of play (0 for anything before
// the scene plays). With safehouse_coin off it only records (never blocks,
// never acts) so the log can say what vanilla did.
class CoinPlanner {
public:
    CoinPlanner(bool p_On, const std::array<bool, kCoinSpots>& p_HasItem) : m_On(p_On), m_HasItem(p_HasItem) {}

    void OnListener(double p_Now);

    // A route Out pin (1-based). Block: the pin must not reach its spawner.
    CoinPinVerdict OnRouteOut(int p_Out, double p_Now, std::string* p_Why = nullptr);

    // Every frame while playing; p_Entropy picks the spot of a direct spawn.
    CoinAction Tick(double p_Now, uint64_t p_Entropy);

    // The mod's direct spawn was signaled (or not, p_Ok false: given up).
    void OnSpawnDirect(size_t p_Spot, double p_Now, bool p_Ok);

    bool Placed() const { return m_Spot.has_value(); }
    std::optional<size_t> Spot() const { return m_Spot; }
    std::optional<double> PlacedAt() const { return m_PlacedAt; }
    bool ByMod() const { return m_ByMod; }
    bool GaveUp() const { return m_GaveUp; }
    int EmptyPicks() const { return m_EmptyPicks; }
    int Retries() const { return m_Retries; }
    int Blocked() const { return m_Blocked; }
    std::optional<double> ListenerAt() const { return m_ListenerAt; }
    bool AnyCoinSpot() const;

private:
    bool m_On;
    std::array<bool, kCoinSpots> m_HasItem;
    std::optional<double> m_ListenerAt;
    std::optional<double> m_LastOutAt;
    std::optional<double> m_RetryAt; // when the mod last signaled In
    bool m_PendingRetry = false;     // an empty pick waits for the next frame's In
    std::optional<size_t> m_Spot;    // the coin spot picked (game) or spawned (mod)
    std::optional<double> m_PlacedAt;
    bool m_ByMod = false;
    bool m_GaveUp = false;
    int m_EmptyPicks = 0;
    int m_Retries = 0;
    int m_Blocked = 0;
};

} // namespace fr
