#include "HeroStart.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace fr {

double StartDistance(StartPoint a, StartPoint b) {
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    const double dz = static_cast<double>(a.z) - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::span<const DeletedSpawn> KnownDeletedSpawns() {
    // snow_festival: scenario_mamushi deletes agencymarkup_snowcrane's
    // HeroSpawn_Restaurant (5901C4EB74132892) and puts HeroSpawn_Mamushi
    // (5E3D3970F93510C4, entrance aca2b971-..., default) on its spot.
    static const std::array<DeletedSpawn, 1> s_Spawns {{
        {"be6045fd-26ab-4a3b-a6ce-b1ccdf405d09", "HeroSpawn_Restaurant", {-117.0f, 34.79f, -0.47f}},
    }};
    return s_Spawns;
}

std::span<const SealedSpawn> KnownSealedSpawns() {
    // Navmesh evidence (tools/landslide/variant_audit.py): floor
    // under Freelancer's navmesh at the spawn, none within 3 m under the
    // variant's. Watched starts (variant_audit.py's "zone" and "npc" findings):
    // the spawn stands inside a trespassing or hostile area the
    // variant adds AND one of the variant's NPCs or acts is within 3 m, so
    // 47 (never disguised in Freelancer) is seen at once. A Bitter Pill's was
    // seen in play (47 started behind Galen Vholes); the others are from the
    // game files only. An off-limits start with no one beside it is fine.
    static const std::array<SealedSpawn, 8> s_Spawns {{
        {"garden_show", 0x44bc69efb5aa958dull, "Start Evergreen_AlexaBalcony",
         "the Garden Show closes the mansion (override_bridge)"},
        {"illusions_of_grandeur", 0x86566aaa4894f5bbull, "HeroSpawn_Barge",
         "Illusions of Grandeur closes the barge (scenario_mongoose)"},
        {"illusions_of_grandeur", 0xc5724fc0bd71ca79ull, "HeroSpawn_Bollywood_Dancer",
         "Illusions of Grandeur closes the Bollywood set (scenario_mongoose)"},
        {"a_bitter_pill", 0x8c10e5329a36e3f9ull, "Evergreen_Spawn_House",
         "A Bitter Pill makes the for-sale house's first floor a hostile area, with Galen Vholes and his guards by the "
         "spawn (scenario_gartersnake)"},
        {"the_undying", 0x778eadc343bab4d9ull, "Start_Kronstad_Engineer",
         "The Undying makes the Kronstad engineers' room a trespassing area, with a guard 0.7 m from the spawn "
         "(mission_sambuca)"},
        {"the_undying_returns", 0x778eadc343bab4d9ull, "Start_Kronstad_Engineer",
         "The Undying Returns makes the Kronstad engineers' room a trespassing area, with a guard 0.7 m from the spawn "
         "(scenario_sambuca2)"},
        {"embrace_of_the_serpent", 0xee87db326b6dce06ull, "HeroSpawn_Jungle",
         "Embrace of the Serpent makes the jungle a trespassing area, with its guards 1-3 m from the spawn "
         "(scenario_anaconda)"},
        {"illusions_of_grandeur", 0x033d8b8196931cdfull, "HeroSpawn_Kashmirian",
         "Illusions of Grandeur makes the chawl a trespassing area, with Kunal 0.5 m from the spawn "
         "(scenario_kingcobra)"},
    }};
    return s_Spawns;
}

const SealedSpawn* FindSealedSpawn(std::string_view option, uint64_t id, std::span<const SealedSpawn> known) {
    if (id == 0 || option.empty())
        return nullptr;

    for (const SealedSpawn& sealed : known) {
        if (sealed.id == id && sealed.option == option)
            return &sealed;
    }

    return nullptr;
}

bool IsSpawnPointRead(StartPoint point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        return false;

    return point.x != 0.0f || point.y != 0.0f || point.z != 0.0f;
}

std::span<const OffMapSpawn> KnownOffMapSpawns() {
    // dartmoor showdown, sloth: the variant's default HeroSpawn, a staging spot
    // 500 m above the map.
    static const std::array<OffMapSpawn, 1> s_Spawns {{
        {0x22f118f0776459a5ull, "dartmoor sloth's staging spawn", {-509.04f, -0.10f, 499.99f}},
    }};
    return s_Spawns;
}

namespace {

bool IsKnownOffMap(const HeroSpawnFacts& spawn, std::span<const OffMapSpawn> offMap) {
    if (spawn.id == 0)
        return false;

    for (const OffMapSpawn& known : offMap) {
        if (known.id == spawn.id)
            return true;
    }

    return false;
}

// Read, and within the absolute bounds.
bool IsWithinBounds(const HeroSpawnFacts& spawn) {
    if (!spawn.where || !IsSpawnPointRead(*spawn.where))
        return false;

    return std::fabs(spawn.where->z) <= kOffMapHeightMeters && std::fabs(spawn.where->x) <= kOffMapReachMeters &&
           std::fabs(spawn.where->y) <= kOffMapReachMeters;
}

} // namespace

SpawnPlace SpawnPlaceOf(std::span<const HeroSpawnFacts> spawns, size_t index, std::span<const OffMapSpawn> offMap) {
    if (index >= spawns.size())
        return SpawnPlace::Unknown;

    const HeroSpawnFacts& spawn = spawns[index];

    if (IsKnownOffMap(spawn, offMap))
        return SpawnPlace::OffMap;

    if (!spawn.where || !IsSpawnPointRead(*spawn.where))
        return SpawnPlace::Unknown;

    if (!IsWithinBounds(spawn))
        return SpawnPlace::OffMap;

    // Far up or down from the other spawns on the map.
    std::vector<double> heights;

    for (size_t i = 0; i < spawns.size(); ++i) {
        if (i != index && IsWithinBounds(spawns[i]) && !IsKnownOffMap(spawns[i], offMap))
            heights.push_back(spawns[i].where->z);
    }

    if (heights.size() >= kOffMapPeers) {
        std::sort(heights.begin(), heights.end());
        const size_t half = heights.size() / 2;
        const double median = heights.size() % 2 ? heights[half] : (heights[half - 1] + heights[half]) / 2.0;

        if (std::fabs(spawn.where->z - median) > kOffMapPeerHeightMeters)
            return SpawnPlace::OffMap;
    }

    return SpawnPlace::OnMap;
}

std::string_view SpawnPlaceText(SpawnPlace place) {
    switch (place) {
        case SpawnPlace::Unknown: return "position unknown";
        case SpawnPlace::OnMap: return "on the map";
        case SpawnPlace::OffMap: return "off the map";
    }
    return "unknown";
}

StartDecision DecideHeroStart(std::span<const HeroSpawnFacts> spawns, const std::optional<Guid>& requested,
                              std::span<const DeletedSpawn> deleted, std::span<const OffMapSpawn> offMap) {
    StartDecision out;

    if (!requested || requested->IsEmpty())
        return out;

    for (size_t i = 0; i < spawns.size(); ++i) {
        if (!spawns[i].entrance || *spawns[i].entrance != *requested)
            continue;

        if (spawns[i].sealed) {
            if (!out.sealedRequest) {
                out.sealedRequest = true;
                out.sealedSpawn = i;
            }
        }
        else if (out.matches++ == 0)
            out.spawn = i;
    }

    if (out.matches > 0) {
        out.action = StartAction::Matched;
        return out;
    }

    // Spawns that can be re-pointed: entrance read, not off the map.
    std::vector<bool> usable(spawns.size(), false);

    for (size_t i = 0; i < spawns.size(); ++i) {
        if (!spawns[i].entrance || spawns[i].sealed)
            continue;

        if (SpawnPlaceOf(spawns, i, offMap) == SpawnPlace::OffMap)
            ++out.offMap;
        else
            usable[i] = true;
    }

    const auto pick = [&](size_t i, RepointWhy why) {
        out.action = StartAction::Repoint;
        out.spawn = i;
        out.why = why;
        return out;
    };

    // The spawn on the deleted one's spot (the nearest within kSameSpotMeters).
    for (const DeletedSpawn& gone : deleted) {
        const auto id = ParseGuid(gone.entrance);

        if (!id || *id != *requested)
            continue;

        std::optional<size_t> best;
        double bestDistance = kSameSpotMeters;

        for (size_t i = 0; i < spawns.size(); ++i) {
            if (!usable[i] || !spawns[i].where)
                continue;

            const double distance = StartDistance(*spawns[i].where, gone.where);

            if (distance <= bestDistance) {
                best = i;
                bestDistance = distance;
            }
        }

        if (best)
            return pick(*best, RepointWhy::SameSpot);
    }

    // A default spawn: the variant's own first, then any other brick's.
    for (const bool variant : {true, false}) {
        for (size_t i = 0; i < spawns.size(); ++i) {
            if (usable[i] && spawns[i].isDefault && spawns[i].inVariantBrick == variant)
                return pick(i, variant ? RepointWhy::VariantDefault : RepointWhy::SceneDefault);
        }
    }

    // No default on the map: any readable spawn on it beats a start in the sky.
    for (const bool variant : {true, false}) {
        for (size_t i = 0; i < spawns.size(); ++i) {
            if (usable[i] && spawns[i].inVariantBrick == variant)
                return pick(i, RepointWhy::AnySpawn);
        }
    }

    out.action = out.sealedRequest ? StartAction::SealedOnly
                 : out.offMap > 0  ? StartAction::OffMapOnly
                                   : StartAction::NoSpawn;
    return out;
}

std::string_view RepointWhyText(RepointWhy why) {
    switch (why) {
        case RepointWhy::None: return "none";
        case RepointWhy::SameSpot: return "it stands where the deleted spawn stood";
        case RepointWhy::VariantDefault: return "the map variant's default spawn";
        case RepointWhy::SceneDefault: return "the scene's default spawn";
        case RepointWhy::AnySpawn: return "no default spawn; the first one found";
    }
    return "unknown";
}

SpawnPoint PickSpawnPoint(const std::optional<StartPoint>& own, std::span<const std::optional<StartPoint>> mates) {
    if (own)
        return {own, SpawnPointSource::Own, 0};

    for (size_t i = 0; i < mates.size(); ++i) {
        if (mates[i])
            return {mates[i], SpawnPointSource::TemplateMate, i};
    }

    return {};
}

std::optional<NearestStart> NearestSpawn(std::span<const std::optional<StartPoint>> points, StartPoint hero) {
    std::optional<NearestStart> out;

    for (size_t i = 0; i < points.size(); ++i) {
        if (!points[i])
            continue;

        const double distance = StartDistance(*points[i], hero);

        if (!out || distance < out->distance)
            out = NearestStart {i, distance};
    }

    return out;
}

} // namespace fr
