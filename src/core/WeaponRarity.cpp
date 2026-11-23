#include "WeaponRarity.h"

#include "Rng.h"
#include "Scene.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <utility>

namespace fr {

std::string RarityName(int32_t rarity) {
    switch (rarity) {
        case kRarityCommon: return "common";
        case kRarityRare: return "rare";
        case kRarityEpic: return "epic";
        case kRarityLegendary: return "legendary";
        default: break;
    }

    char s_Text[32];
    std::snprintf(s_Text, sizeof(s_Text), "rarity %d", static_cast<int>(rarity));
    return s_Text;
}

std::optional<int32_t> RarityFromValue(double value) {
    if (!std::isfinite(value) || value != std::floor(value) || value < kRarityCommon || value > kRarityLegendary)
        return std::nullopt;

    return static_cast<int32_t>(value);
}

bool IsFirearmIcon(std::string_view inventoryCategoryIcon) {
    const std::string s_Icon = ToLower(inventoryCategoryIcon);
    return s_Icon == "pistol" || s_Icon == "smg" || s_Icon == "assaultrifle" || s_Icon == "shotgun" ||
           s_Icon == "sniperrifle" || s_Icon == "sniper";
}

bool IsPistolIcon(std::string_view inventoryCategoryIcon) {
    return ToLower(inventoryCategoryIcon) == "pistol";
}

bool IsSilencedAudio(std::string_view audioHeadType, std::string_view audioTailType) {
    return ToLower(audioHeadType).find("silenced") != std::string::npos ||
           ToLower(audioTailType).find("silenced") != std::string::npos;
}

bool IsDartGun(std::string_view audioExoticType, std::string_view name) {
    if (!audioExoticType.empty())
        return ToLower(audioExoticType).find("dart") != std::string::npos;

    const std::string s_Name = ToLower(name);
    return s_Name.find("dartgun") != std::string::npos || s_Name.find("dart gun") != std::string::npos;
}

bool IsUnsilencedPistol(std::string_view inventoryCategoryIcon, std::string_view audioHeadType,
                        std::string_view audioTailType, std::string_view audioExoticType, std::string_view name) {
    return IsPistolIcon(inventoryCategoryIcon) && !IsSilencedAudio(audioHeadType, audioTailType) &&
           !IsDartGun(audioExoticType, name);
}

KeepRange SanitizeKeepRange(double min, double max) {
    const auto s_Clamp = [](double p_Value, double p_Default) {
        if (!std::isfinite(p_Value))
            return p_Default;
        return std::clamp(p_Value, 0.0, 1.0);
    };

    KeepRange s_Range {s_Clamp(min, kDefaultWeaponRarityKeepMin), s_Clamp(max, kDefaultWeaponRarityKeepMax)};

    if (s_Range.min > s_Range.max)
        std::swap(s_Range.min, s_Range.max);

    return s_Range;
}

const char* EpicFixName(EpicFix fix) {
    switch (fix) {
        case EpicFix::AlreadyEpic: return "an unsilenced pistol is already epic";
        case EpicFix::SwappedWithMover: return "an unsilenced pistol swapped rarities with an epic mover";
        case EpicFix::Forced: return "an unsilenced pistol was set to epic (one epic more)";
        case EpicFix::NoUnsilencedPistol: return "no unsilenced pistol";
    }

    return "?";
}

uint64_t WeaponRarityId(const Guid& id) {
    return Fnv1a64(FormatGuid(id));
}

namespace {

uint64_t RankHash(int32_t seed, std::string_view feature, const Guid& id) {
    return DialHash(seed, kWeaponRarityKey, feature, WeaponRarityId(id));
}

// Indices of weapons ordered by the feature's hash, ties by id (never seen).
std::vector<size_t> RankedBy(std::span<const RarityWeapon> weapons, const std::vector<size_t>& indices, int32_t seed,
                             std::string_view feature) {
    std::vector<std::pair<uint64_t, size_t>> s_Keys;
    s_Keys.reserve(indices.size());

    for (const size_t s_Index : indices)
        s_Keys.push_back({RankHash(seed, feature, weapons[s_Index].id), s_Index});

    std::sort(s_Keys.begin(), s_Keys.end(), [&](const auto& p_A, const auto& p_B) {
        if (p_A.first != p_B.first)
            return p_A.first < p_B.first;
        return weapons[p_A.second].id < weapons[p_B.second].id;
    });

    std::vector<size_t> s_Out;
    s_Out.reserve(s_Keys.size());

    for (const auto& s_Key : s_Keys)
        s_Out.push_back(s_Key.second);

    return s_Out;
}

} // namespace

RarityPlan PlanWeaponRarity(std::span<const RarityWeapon> weapons, int32_t seed, KeepRange keep) {
    keep = SanitizeKeepRange(keep.min, keep.max);
    RarityPlan s_Plan;
    const size_t s_Count = weapons.size();
    s_Plan.changes.resize(s_Count);

    for (size_t i = 0; i < s_Count; ++i)
        s_Plan.changes[i] = {weapons[i].rarity, true, false};

    s_Plan.keepShare = keep.min + (keep.max - keep.min) *
                                      UniformFromHash(DialHash(seed, kWeaponRarityKey, kWeaponRarityShareFeature, 0));

    std::vector<size_t> s_All(s_Count);

    for (size_t i = 0; i < s_Count; ++i)
        s_All[i] = i;

    if (s_Count > 0) {
        size_t s_Keep = static_cast<size_t>(std::llround(s_Plan.keepShare * static_cast<double>(s_Count)));
        s_Keep = std::clamp<size_t>(s_Keep, 1, s_Count);

        // A swap needs two weapons that move.
        if (s_Count - s_Keep < 2) {
            s_Plan.keepCount = s_Count;
        }
        else {
            const std::vector<size_t> s_ByKeep = RankedBy(weapons, s_All, seed, kWeaponRarityKeepFeature);
            const std::vector<size_t> s_Movers(s_ByKeep.begin() + static_cast<ptrdiff_t>(s_Keep), s_ByKeep.end());
            // One cycle in a seeded order: each mover takes the next one's rarity.
            const std::vector<size_t> s_Cycle = RankedBy(weapons, s_Movers, seed, kWeaponRarityCycleFeature);

            for (size_t k = 0; k < s_Cycle.size(); ++k) {
                RarityChange& s_Change = s_Plan.changes[s_Cycle[k]];
                s_Change.rarity = weapons[s_Cycle[(k + 1) % s_Cycle.size()]].rarity;
                s_Change.kept = false;
            }

            s_Plan.keepCount = s_Keep;
            s_Plan.moverCount = s_Cycle.size();
        }
    }

    // The Epic unsilenced pistol rule.
    std::vector<size_t> s_Pistols;
    bool s_HasEpicPistol = false;

    for (size_t i = 0; i < s_Count; ++i) {
        if (!weapons[i].unsilencedPistol)
            continue;

        s_Pistols.push_back(i);
        s_HasEpicPistol = s_HasEpicPistol || s_Plan.changes[i].rarity == kRarityEpic;
    }

    if (s_HasEpicPistol) {
        s_Plan.epic = EpicFix::AlreadyEpic;
    }
    else if (!s_Pistols.empty()) {
        // A pistol that moves first: changing it leaves the kept share alone.
        std::vector<size_t> s_MovingPistols;

        for (const size_t s_Index : s_Pistols) {
            if (!s_Plan.changes[s_Index].kept)
                s_MovingPistols.push_back(s_Index);
        }

        const size_t s_Pistol =
            RankedBy(weapons, s_MovingPistols.empty() ? s_Pistols : s_MovingPistols, seed, kWeaponRarityEpicFeature)
                .front();

        std::vector<size_t> s_Partners;

        for (size_t i = 0; i < s_Count; ++i) {
            if (!weapons[i].unsilencedPistol && !s_Plan.changes[i].kept && s_Plan.changes[i].rarity == kRarityEpic)
                s_Partners.push_back(i);
        }

        RarityChange& s_PistolChange = s_Plan.changes[s_Pistol];
        s_Plan.epicPistol = s_Pistol;

        if (!s_Partners.empty()) {
            const size_t s_Partner = RankedBy(weapons, s_Partners, seed, kWeaponRarityPartnerFeature).front();
            RarityChange& s_PartnerChange = s_Plan.changes[s_Partner];
            s_PartnerChange.rarity = s_PistolChange.rarity;
            s_PartnerChange.epicFix = true;
            s_Plan.epicPartner = s_Partner;
            s_Plan.epic = EpicFix::SwappedWithMover;
        }
        else {
            s_Plan.epic = EpicFix::Forced;
        }

        if (s_PistolChange.kept) {
            s_PistolChange.kept = false;
            --s_Plan.keepCount;
        }

        s_PistolChange.rarity = kRarityEpic;
        s_PistolChange.epicFix = true;
    }

    for (size_t i = 0; i < s_Count; ++i) {
        if (s_Plan.changes[i].rarity != weapons[i].rarity)
            ++s_Plan.changedCount;
    }

    return s_Plan;
}

RarityCounts CountRarities(std::span<const int32_t> rarities) {
    RarityCounts s_Counts {};

    for (const int32_t s_Rarity : rarities) {
        if (s_Rarity >= kRarityCommon && s_Rarity <= kRarityLegendary)
            ++s_Counts[static_cast<size_t>(s_Rarity)];
        else
            ++s_Counts[0];
    }

    return s_Counts;
}

std::string DescribeRarityCounts(const RarityCounts& counts) {
    char s_Text[160];
    std::snprintf(s_Text, sizeof(s_Text), "common %zu, rare %zu, epic %zu, legendary %zu", counts[1], counts[2],
                  counts[3], counts[4]);
    std::string s_Out = s_Text;

    if (counts[0] > 0) {
        std::snprintf(s_Text, sizeof(s_Text), ", other %zu", counts[0]);
        s_Out += s_Text;
    }

    return s_Out;
}

double CapacityAfter(double vanillaCost, int32_t vanillaRarity, int32_t newRarity) {
    if (!(vanillaCost > 0.0) || vanillaRarity == newRarity)
        return vanillaCost;

    const double s_Cost = vanillaCost - static_cast<double>(newRarity - vanillaRarity);
    return std::clamp(s_Cost, 1.0, std::max(5.0, vanillaCost));
}

std::optional<int32_t> InitialRaritySeed(std::string_view raritySeedText, std::optional<int32_t> campaignSeed) {
    const auto s_First = raritySeedText.find_first_not_of(" \t\r\n");
    const auto s_Last = raritySeedText.find_last_not_of(" \t\r\n");
    const std::string_view s_Text =
        s_First == std::string_view::npos ? std::string_view() : raritySeedText.substr(s_First, s_Last - s_First + 1);
    int32_t s_Seed = 0;
    const auto [s_End, s_Error] = std::from_chars(s_Text.data(), s_Text.data() + s_Text.size(), s_Seed);

    if (!s_Text.empty() && s_Error == std::errc() && s_End == s_Text.data() + s_Text.size() && s_Seed != 0)
        return s_Seed;

    if (campaignSeed && *campaignSeed != 0)
        return campaignSeed;

    return std::nullopt;
}

RarityDecision RarityOnPick(const RarityCampaign& current, int32_t picked) {
    if (picked == 0 || current.seed == picked)
        return {RarityStep::Keep, current};

    return {RarityStep::Reroll, {picked, std::nullopt}};
}

RarityDecision RarityOnMission(const RarityCampaign& current, int32_t seed, bool live) {
    if (seed == 0 || current.live == seed)
        return {RarityStep::Keep, current};

    if (!current.seed)
        return {RarityStep::Reroll, {seed, live ? std::optional(seed) : std::nullopt}};

    if (live) {
        if (!current.live || current.seed == seed)
            return {RarityStep::Bind, {current.seed, seed}};

        return {RarityStep::Reroll, {seed, seed}};
    }

    if (current.seed == seed || !current.live)
        return {RarityStep::Keep, current};

    return {RarityStep::Reroll, {seed, std::nullopt}};
}

} // namespace fr
