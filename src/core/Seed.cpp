#include "Seed.h"

namespace fr {

bool IsKnownSeed(std::optional<int32_t> seed) {
    return seed && *seed != 0;
}

SeedResolution ResolveSeed(std::optional<int32_t> live, std::optional<int32_t> generated,
                           std::optional<int32_t> persisted) {
    if (IsKnownSeed(live))
        return {SeedSource::Live, *live};
    if (IsKnownSeed(generated))
        return {SeedSource::Generated, *generated};
    if (IsKnownSeed(persisted))
        return {SeedSource::Persisted, *persisted};
    return {SeedSource::Fallback, 0};
}

bool ShouldPersist(const SeedResolution& resolution) {
    return (resolution.source == SeedSource::Live || resolution.source == SeedSource::Generated) &&
           resolution.seed != 0;
}

LateSeed DecideLateSeed(const SeedResolution& atStart, std::optional<int32_t> live) {
    if (!IsKnownSeed(live))
        return LateSeed::None;
    if (*live == atStart.seed)
        return LateSeed::None;
    return atStart.source == SeedSource::Live ? LateSeed::Differs : LateSeed::FoundLate;
}

const char* SeedSourceName(SeedSource source) {
    switch (source) {
        case SeedSource::Live: return "live";
        case SeedSource::Generated: return "generated";
        case SeedSource::Persisted: return "persisted";
        case SeedSource::Fallback: return "fallback (seed not yet known)";
    }
    return "unknown";
}

PickSeed DecidePickSeed(std::optional<int32_t> live, std::optional<int32_t> persisted,
                        std::optional<int32_t> previousCampaign) {
    if (!IsKnownSeed(live))
        return PickSeed::Unknown;
    if (persisted == live)
        return PickSeed::Same;
    if (previousCampaign == live)
        return PickSeed::Stale;
    return PickSeed::Adopt;
}

int32_t ApplySeedAlias(int32_t seed, const std::optional<SeedAlias>& alias) {
    return alias && alias->from == seed ? alias->to : seed;
}

std::optional<SeedResolution> SceneSeedMemory::Find(std::string_view scene) const {
    const auto it = m_Seeds.find(scene);
    if (it == m_Seeds.end())
        return std::nullopt;
    return it->second;
}

void SceneSeedMemory::Remember(const std::string& scene, const SeedResolution& resolution) {
    m_Seeds.try_emplace(scene, resolution);
}

void SceneSeedMemory::ForgetOtherScenes(std::string_view scene) {
    std::erase_if(m_Seeds, [&](const auto& entry) { return entry.first != scene; });
}

void SceneSeedMemory::Clear() {
    m_Seeds.clear();
}

size_t SceneSeedMemory::Size() const {
    return m_Seeds.size();
}

} // namespace fr
