#include "SafehouseCoin.h"

#include <string>
#include <vector>

namespace fr {

std::optional<size_t> CoinSpotOfOut(int p_OutNumber, size_t p_Spots) {
    if (p_OutNumber < 1 || static_cast<size_t>(p_OutNumber) > p_Spots)
        return std::nullopt;

    return static_cast<size_t>(p_OutNumber - 1);
}

std::optional<size_t> PickCoinSpot(uint64_t p_Entropy, const std::array<bool, kCoinSpots>& p_HasItem) {
    std::vector<size_t> s_Spots;

    for (size_t i = 0; i < kCoinSpots; ++i) {
        if (p_HasItem[i])
            s_Spots.push_back(i);
    }

    if (s_Spots.empty())
        return std::nullopt;

    // SplitMix64 finalizer, so close entropy values still spread.
    uint64_t z = p_Entropy + 0x9E37'79B9'7F4A'7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58'476D'1CE4'E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D0'49BB'1331'11EBull;
    z ^= z >> 31;
    return s_Spots[z % s_Spots.size()];
}

bool CoinPlanner::AnyCoinSpot() const {
    for (bool s_Has : m_HasItem) {
        if (s_Has)
            return true;
    }

    return false;
}

void CoinPlanner::OnListener(double p_Now) {
    if (!m_ListenerAt)
        m_ListenerAt = p_Now;
}

CoinPinVerdict CoinPlanner::OnRouteOut(int p_Out, double p_Now, std::string* p_Why) {
    const auto s_Say = [&](std::string p_Text) {
        if (p_Why)
            *p_Why = std::move(p_Text);
    };

    m_LastOutAt = p_Now;
    m_RetryAt.reset();

    if (m_Spot) {
        if (!m_On) {
            s_Say("another pick after the coin was placed (safehouse_coin off: passed)");
            return CoinPinVerdict::Pass;
        }

        ++m_Blocked;
        s_Say("the coin is placed already: BLOCKED");
        return CoinPinVerdict::Block;
    }

    const auto s_Spot = CoinSpotOfOut(p_Out);

    if (s_Spot && m_HasItem[*s_Spot]) {
        m_Spot = s_Spot;
        m_PlacedAt = p_Now;
        s_Say("spot " + std::to_string(*s_Spot + 1) + " has the coin: the game places it");
        return CoinPinVerdict::Pass;
    }

    ++m_EmptyPicks;
    const std::string s_What = s_Spot ? "spot " + std::to_string(*s_Spot + 1) + " has no item (empty nest)"
                                      : std::string("no spawner on this pin");

    if (!m_On) {
        s_Say(s_What + ": no coin this visit (safehouse_coin off)");
        return CoinPinVerdict::Pass;
    }

    if (!AnyCoinSpot()) {
        s_Say(s_What + ": no spot has an item, nothing to do");
        return CoinPinVerdict::Pass;
    }

    ++m_Blocked;
    m_PendingRetry = true;
    s_Say(s_What + ": BLOCKED; the route picks again");
    return CoinPinVerdict::Block;
}

CoinAction CoinPlanner::Tick(double p_Now, uint64_t p_Entropy) {
    if (!m_On || m_Spot || m_GaveUp || !AnyCoinSpot())
        return {};

    const auto s_Retry = [&](std::string p_Why) -> CoinAction {
        if (m_Retries >= kCoinMaxRetries) {
            const auto s_Spot = PickCoinSpot(p_Entropy, m_HasItem);
            return {CoinActionKind::SpawnDirect, s_Spot.value_or(0),
                    p_Why + "; " + std::to_string(m_Retries) + " re-picks used: the mod spawns the coin itself"};
        }

        ++m_Retries;
        m_PendingRetry = false;
        m_RetryAt = p_Now;
        return {CoinActionKind::SignalRouteIn, 0,
                p_Why + "; the route picks again (re-pick " + std::to_string(m_Retries) + ")"};
    };

    if (m_PendingRetry)
        return s_Retry("the route picked an empty spot");

    if (m_RetryAt && p_Now - *m_RetryAt >= kCoinRouteGrace) {
        m_RetryAt.reset();
        const auto s_Spot = PickCoinSpot(p_Entropy, m_HasItem);
        return {CoinActionKind::SpawnDirect, s_Spot.value_or(0),
                "the route did not answer the mod's In: the mod spawns the coin itself"};
    }

    if (m_RetryAt || m_LastOutAt)
        return {};

    if (m_ListenerAt && p_Now - *m_ListenerAt >= kCoinRouteGrace)
        return s_Retry("the listener fired but the route signaled no Out pin");

    if (!m_ListenerAt && p_Now >= kCoinRouteDeadline)
        return s_Retry("neither the listener nor the route fired in " +
                       std::to_string(static_cast<int>(kCoinRouteDeadline)) + " s of play");

    return {};
}

void CoinPlanner::OnSpawnDirect(size_t p_Spot, double p_Now, bool p_Ok) {
    if (!p_Ok) {
        m_GaveUp = true;
        return;
    }

    m_Spot = p_Spot;
    m_PlacedAt = p_Now;
    m_ByMod = true;
}

} // namespace fr
