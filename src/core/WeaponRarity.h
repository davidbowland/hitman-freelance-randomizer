#pragma once

// Pure logic of the weapon rarity shuffle (weapon_rarity): when a Freelancer
// campaign starts, the rarities of the firearms (pro.repo "Evergreen_Rarity",
// 1 common .. 4 legendary) are permuted among the firearms, so the number of
// weapons of each rarity stays the same. A seeded share (drawn per campaign in
// [keep min, keep max], at least one weapon) keeps its rarity; the rest pass
// theirs on in one seeded cycle. Afterwards at least one unsilenced pistol is
// Epic (the prestige objective "Unsilenced Pistol Kill - Epic"; a dart gun
// is no unsilenced pistol here, as it can't kill: IsDartGun): if none is,
// one gets Epic, by a swap with an Epic mover that is not an unsilenced pistol
// when there is one (counts kept), else by setting it (one more Epic). Every
// draw depends only on the campaign seed and the weapons' repository ids, so
// the plan is the same in the safehouse and in every mission of a campaign,
// whatever order the weapons are listed in.

#include "Guid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Evergreen_Rarity values (H3-Controls EvergreenUtils.ITEMRARITY_*).
inline constexpr int32_t kRarityCommon = 1;
inline constexpr int32_t kRarityRare = 2;
inline constexpr int32_t kRarityEpic = 3;
inline constexpr int32_t kRarityLegendary = 4;

// DialHash's scene name and feature names of the draws.
inline constexpr std::string_view kWeaponRarityKey = "campaign";
inline constexpr std::string_view kWeaponRarityShareFeature = "weapon rarity share";
inline constexpr std::string_view kWeaponRarityKeepFeature = "weapon rarity keep";
inline constexpr std::string_view kWeaponRarityCycleFeature = "weapon rarity cycle";
inline constexpr std::string_view kWeaponRarityEpicFeature = "weapon rarity epic";
inline constexpr std::string_view kWeaponRarityPartnerFeature = "weapon rarity epic partner";

// weapon_rarity_keep_min / weapon_rarity_keep_max defaults.
inline constexpr double kDefaultWeaponRarityKeepMin = 0.05;
inline constexpr double kDefaultWeaponRarityKeepMax = 0.10;

// "common", "rare", "epic", "legendary", or "rarity <n>".
std::string RarityName(int32_t rarity);

// The rarity a pro.repo Evergreen_Rarity value stands for: a whole number
// from 1 to 4; empty for anything else (0 is "none": items without a rarity).
std::optional<int32_t> RarityFromValue(double value);

// A firearm by its InventoryCategoryIcon (any case): pistol, smg,
// assaultrifle, shotgun, sniperrifle (or sniper).
bool IsFirearmIcon(std::string_view inventoryCategoryIcon);

bool IsPistolIcon(std::string_view inventoryCategoryIcon);

// A silenced weapon by its AudioHeadType / AudioTailType (any case): either
// names a silenced head or tail ("eWBC_AudioHeadTailType_Silenced", also
// "...SuperSilenced"). Weapons without either key count as unsilenced.
bool IsSilencedAudio(std::string_view audioHeadType, std::string_view audioTailType);

// A dart gun: a pistol in pro.repo that shoots darts and never kills ("(S3)
// (Dartgun) Kalmer 2 - Tranquilizer", the emetic "(Dartgun) Sick"), so it
// never meets a pistol or firearm kill. By its AudioExoticType (any case)
// naming a dart pistol: all dart guns have
// "eWBC_AudioExoticType_TranquilizerDartPistol", the Sick too, and no other
// firearm does. Only when the item has
// no AudioExoticType, by name: "dartgun" or "dart gun" (any case).
bool IsDartGun(std::string_view audioExoticType, std::string_view name);

// RarityWeapon::unsilencedPistol from the weapon's pro.repo keys: a pistol
// icon, not silenced (IsSilencedAudio) and not a dart gun (IsDartGun).
bool IsUnsilencedPistol(std::string_view inventoryCategoryIcon, std::string_view audioHeadType,
                        std::string_view audioTailType, std::string_view audioExoticType, std::string_view name);

// Keep share bounds: each clamped to [0, 1], and swapped when min > max.
struct KeepRange {
    double min = kDefaultWeaponRarityKeepMin;
    double max = kDefaultWeaponRarityKeepMax;
};

KeepRange SanitizeKeepRange(double min, double max);

// One firearm with a rarity.
struct RarityWeapon {
    Guid id;
    int32_t rarity = 0;            // 1..4
    bool unsilencedPistol = false; // a pistol, not silenced and not a dart gun
};

// How the Epic unsilenced pistol rule was met.
enum class EpicFix {
    AlreadyEpic,        // an unsilenced pistol ends Epic without help
    SwappedWithMover,   // a pistol swapped rarities with an Epic mover that is not an unsilenced pistol
    Forced,             // a pistol was set to Epic (one Epic more, its old rarity one less)
    NoUnsilencedPistol, // there is no unsilenced pistol to make Epic
};

const char* EpicFixName(EpicFix fix);

struct RarityChange {
    int32_t rarity = 0;   // the new rarity
    bool kept = false;    // in the kept share (its rarity is its own, unless the Epic rule changed it)
    bool epicFix = false; // changed by the Epic rule (the pistol, or the mover it swapped with)
};

struct RarityPlan {
    std::vector<RarityChange> changes; // one per input weapon, in input order
    double keepShare = 0.0;            // drawn in the keep range
    size_t keepCount = 0;              // weapons kept (at least 1 when there is any)
    size_t moverCount = 0;             // weapons in the cycle (0 when fewer than 2 would move)
    size_t changedCount = 0;           // weapons whose rarity differs from their own
    EpicFix epic = EpicFix::NoUnsilencedPistol;
    std::optional<size_t> epicPistol;  // input index of the pistol the Epic rule changed
    std::optional<size_t> epicPartner; // input index of the mover it swapped with
};

// The weapons' ranking key in DialHash: FNV-1a of the lower-case dashed GUID.
uint64_t WeaponRarityId(const Guid& id);

// The plan for one campaign (see the file comment). Weapons with a rarity
// outside 1..4 should not be passed; they would be moved like any other.
// Duplicate ids are not expected (pro.repo keys are unique).
RarityPlan PlanWeaponRarity(std::span<const RarityWeapon> weapons, int32_t seed, KeepRange keep);

// Rarity counts by rarity (index 1..4; 0 counts any other value).
using RarityCounts = std::array<size_t, 5>;

RarityCounts CountRarities(std::span<const int32_t> rarities);

// "common a, rare b, epic c, legendary d" (and "other e" when any).
std::string DescribeRarityCounts(const RarityCounts& counts);

// weapon_rarity_capacity: the gear capacity cost after a rarity change. Each
// rarity step up costs one slot less and each step down one more, as the
// vanilla walls trend (Ancestral and Ornamental legendaries cost 1, common
// rifles 4 or 5), kept within [1, max(5, vanilla)]. A cost of 0 or less (free
// items) and an unchanged rarity keep the vanilla cost.
double CapacityAfter(double vanillaCost, int32_t vanillaRarity, int32_t newRarity);

// Which campaign's rarities apply ([campaign] rarity_seed, rarity_live). The
// game generates a new, unpicked campaign (a new seed) at every safehouse
// visit until the player picks one, so the rarities follow only the campaign
// the player picked: they are rolled once, from the picked campaign's seed,
// when the pick is seen, and hold until the next pick. The picked campaign's
// missions run with another seed of their own (the evergreen_setup brick's
// CPD_RandomSeed, the "live" seed; e.g. pick -951706414, missions
// 216926673), which is not readable in the safehouse at
// the pick. The first live seed a mission reads after the pick is bound to the
// campaign (rarity_live) instead of rolling again; only a mission whose live
// seed is another campaign's (its pick was missed) rolls new rarities.

// At load: the persisted rarity_seed when it is a non-zero 32-bit integer;
// otherwise (blank, bad, or an ini of an earlier build) the persisted
// campaign seed when it is known and not 0. With an empty campaign seed it
// parses a seed text (rarity_live).
std::optional<int32_t> InitialRaritySeed(std::string_view raritySeedText, std::optional<int32_t> campaignSeed);

struct RarityCampaign {
    std::optional<int32_t> seed; // the seed the rarities are rolled from (rarity_seed)
    std::optional<int32_t> live; // the campaign's own seed as its missions read it (rarity_live); empty until bound
};

enum class RarityStep {
    Keep,   // nothing changes
    Bind,   // the rarities stay; the mission's live seed is recorded as this campaign's
    Reroll, // another campaign: the rarities are rolled from next.seed
};

struct RarityDecision {
    RarityStep step = RarityStep::Keep;
    RarityCampaign next; // the state afterwards (the current one for Keep)
};

// The campaign pick was seen with the picked campaign's seed (the generated
// seed): another campaign rolls from it, with no live seed bound yet. 0
// (unknown) and the rarities' own campaign keep everything.
RarityDecision RarityOnPick(const RarityCampaign& current, int32_t picked);

// A Freelancer mission runs with campaign seed `seed`. `live`: read from the
// mission's live seed entity; otherwise resolved at LoadScene from the
// generated or persisted seed. A live seed with none bound yet is bound (the
// picked campaign's missions); a live seed other than the bound one is another
// campaign whose pick was missed (rolled). A resolved seed rolls only when it
// is neither the rarities' seed nor the bound live seed and a live seed is
// bound (else the live read decides). 0 (unknown) keeps everything.
RarityDecision RarityOnMission(const RarityCampaign& current, int32_t seed, bool live);

} // namespace fr
