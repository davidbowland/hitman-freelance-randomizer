#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace fr {

// Fallback: no source held a non-zero seed; rolls use seed 0.
enum class SeedSource { Live, Generated, Persisted, Fallback };

struct SeedResolution {
    SeedSource source;
    int32_t seed; // 0 when source is Fallback
};

// A seed value that is set and not 0. The game's seed entity reads 0 before
// the campaign seed is known, so 0 always means "unknown".
bool IsKnownSeed(std::optional<int32_t> seed);

// Picks the first known (non-zero) seed of live > generated > persisted, or
// Fallback with seed 0 when none is known.
SeedResolution ResolveSeed(std::optional<int32_t> live, std::optional<int32_t> generated,
                           std::optional<int32_t> persisted);

// True when the resolved seed should be written to the settings file: a known
// seed from the live or generated source (persisted is already stored, and the
// fallback 0 is never stored).
bool ShouldPersist(const SeedResolution& resolution);

// What a later read of the live seed means for a scene whose seed was resolved
// at scene start.
enum class LateSeed {
    None,      // nothing new
    FoundLate, // the seed used at scene start was not the live one (persisted, generated or the
               // fallback 0) and the live seed is now a different known value: it replaces it
    Differs,   // the live seed was used at scene start and is now a different known value
};

LateSeed DecideLateSeed(const SeedResolution& atStart, std::optional<int32_t> live);

const char* SeedSourceName(SeedSource source);

// What the live seed entity read in the safehouse at the campaign pick means.
// ZEvergreenCampaignManager's OnGenerate seed is not the seed the picked
// campaign's missions run with; the safehouse's CPD_RandomSeed already holds the latter at the
// pick.
enum class PickSeed {
    Unknown, // not read, or 0
    Same,    // already the persisted seed: nothing to do
    Stale,   // still the previous campaign's mission seed: not adopted (the late read at the mission corrects it)
    Adopt,   // the picked campaign's own seed: persisted, so its first mission already runs with it
};

// live: the safehouse's live seed at the pick; persisted: [campaign] seed
// (the generated seed until now); previousCampaign: the mission seed of the
// campaign played before this pick, when known.
PickSeed DecidePickSeed(std::optional<int32_t> live, std::optional<int32_t> persisted,
                        std::optional<int32_t> previousCampaign);

// The generated seed a campaign was known by (client reqs, prestige cards)
// until its own seed was found: what is stored under `from` belongs to `to`.
struct SeedAlias {
    int32_t from;
    int32_t to;
};

// `to` for the alias's `from`, otherwise the seed itself.
int32_t ApplySeedAlias(int32_t seed, const std::optional<SeedAlias>& alias);

// The seed each scene was first started with this session, so a restart of the
// same scene rolls the same values. Entries are forgotten when another scene
// loads (not on a full unload, which a restart may do before its LoadScene).
class SceneSeedMemory {
public:
    std::optional<SeedResolution> Find(std::string_view scene) const;
    // Keeps the first resolution remembered for a scene.
    void Remember(const std::string& scene, const SeedResolution& resolution);
    void ForgetOtherScenes(std::string_view scene);
    void Clear();
    size_t Size() const;

private:
    std::map<std::string, SeedResolution, std::less<>> m_Seeds;
};

} // namespace fr
