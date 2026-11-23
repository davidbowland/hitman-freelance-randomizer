#pragma once

// Crate guns (setting crate_guns, on by default): the
// pure part. In a Freelancer mission all 16 supply crates share one
// setpiece_evergreen_stash_items instance; opening a crate copies its seed
// into the shared seed (kStashItemsSeedId), which seeds a ZRandomSelector
// with two ZRandomSelectorChoice entries:
// - kCrateJunkBranchId, m_Weight 0.85: the tool pool. Three selectors of 22,
//   25 and 18 crate options (melee, explosives, poisons, tools, distractions;
//   no firearm) each pick one offer; the options it activates get state 1.
// - kCrateWeaponBranchId, m_Weight 0.15: the weapon pool. One selector over 30
//   options: 13 firearms (11 pistols, a sawed-off shotgun, an SMG), 16 melee
//   weapons and the merces option (Mercer_Pickup_VerySmall_RewardCrate); its
//   options get state 3. In vanilla only the merces option gets a weight there
//   (the firearms' and melee weapons' m_fA stays 0), so this branch is the
//   one-offer "merces crate".
// In vanilla most crates offer three tool-pool items (options in state 1), the
// rest the merces option alone (options in state 3, every other weight 0); no
// crate offers a gun. The ~400 other crate options (instances outside the stash items, among them
// 121 firearms with rarity weights) belong to something else: they got their
// weights at a time no crate opens, and no crate offers one of them.
//
// An option's weight (its ZRandomSelectorChoice m_Weight) is the vanilla
// m_fA / m_fB of its ZMathMultiplyDivide: m_fA a value by rarity (crate
// option firearms elsewhere: common 70, rare 20, epic 9, legendary 1; most
// tools 100), m_fB its cost (16-186).
//
// The mod (src/mod/CrateGuns.*):
// 1. Roll: for crate open n of the load, a seeded roll (DialHash of the
//    scene seed, the scene and n) hits with probability poolChance (ini
//    crate_gun_chance, default 5 %: with 5-10 crates opened per mission and
//    a gun offered in ~90% of hit crates, a gun offer every ~2.5-5 missions;
//    vanilla crates add whatever they offer on their own, which in the logged
//    crates was never a gun). A hit forces the weapon pool (the two
//    branch choices get m_Weight 0 / 1, so the game's own pick can only land
//    there); a miss leaves the crate vanilla (0.85 / 0.15, firearm weights
//    untouched).
// 2. Weights, on a hit only: in the weapon pool each firearm option's weight
//    is written (rarityValue x rarityMult / cost, scaled so the firearms hold
//    gunShare of the pool's weight against the other weights there, the
//    merces option's).
// Nothing is spawned, no m_Choices array is touched.

#include "GameIds.h" // kStashItemsTemp, kStashItemsTblu, kStashItemsSeedId, kCrateOptionTblu

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Members of the stash items: the branch selector's two choices.
inline constexpr uint64_t kCrateJunkBranchId = 0x54c4831d70bc2a57ull;   // ZRandomSelectorChoice, m_Weight 0.85
inline constexpr uint64_t kCrateWeaponBranchId = 0xb888cc688d29859aull; // ZRandomSelectorChoice, m_Weight 0.15

// The vanilla branch weights.
inline constexpr float kVanillaJunkBranchWeight = 0.85f;
inline constexpr float kVanillaWeaponBranchWeight = 0.15f;

// The option state (kOptionStateId) a pool's options get when it activates.
inline constexpr int32_t kCrateStateToolPool = 1;
inline constexpr int32_t kCrateStateWeaponPool = 3;

enum class CratePool { None, Tool, Weapon, Other };

CratePool CratePoolOfState(int32_t state);

// Rarity index 0..3 = common, rare, epic, legendary (pro.repo
// Evergreen_Rarity 1..4). Anything else (no rarity) counts as common.
size_t CrateRarityIndex(int32_t rarity);
const char* CrateRarityName(int32_t rarity);

// The vanilla m_fA of a crate option firearm by rarity: 70, 20, 9, 1.
double CrateRarityValue(int32_t rarity);

// crate_guns_* settings. The chance and share are fractions here; the ini
// holds them in percent.
struct CrateGunsConfig {
    bool on = true;
    // crate_gun_chance: the chance per opened crate that the roll hits and the
    // crate is steered to the weapon pool with weighted firearms (vanilla: no
    // firearm is ever offered in the crates logged).
    double poolChance = 0.05;
    // crate_guns_share: on a hit, the firearms' share of the weapon pool's
    // weight (vanilla 0: merces only). 1 leaves the merces option out.
    double gunShare = 0.8;
    // crate_guns_mult_common / _rare / _epic / _legendary: multipliers on the
    // vanilla rarity values (70 / 20 / 9 / 1), flattening the curve.
    std::array<double, 4> rarityMult {1.0, 2.0, 5.0, 25.0};
};

// Chance and share clamped to [0, 1] (NaN: the default); multipliers to
// [0, 1000] (NaN or negative: the default).
CrateGunsConfig SanitizeCrateGunsConfig(CrateGunsConfig cfg);

// "gun chance 0.05, gun share 0.9, rarity x1/x2/x5/x25 (common 70, rare 40, epic 45, legendary 25)".
std::string DescribeCrateGunsConfig(const CrateGunsConfig& cfg);

// DialHash feature of the branch draw.
inline constexpr std::string_view kCrateBranchFeature = "crate_guns_branch";

struct CrateBranchDraw {
    double u = 0.0;          // uniform in [0, 1)
    bool weaponPool = false; // u < chance: the roll hit
};

// The branch of crate open openIndex (0-based, per load). Deterministic.
CrateBranchDraw DrawCrateBranch(int32_t seed, std::string_view scene, uint32_t openIndex, double poolChance);

// m_Weight of the tool and weapon branch choices: a hit forces the weapon
// pool (0 / 1), a miss is vanilla (0.85 / 0.15).
struct CrateBranchWeights {
    float junk;
    float weapon;
};

CrateBranchWeights CrateBranchWeightsFor(bool weaponPool);

// What a roll does with the crate. The weapon pool draws only among the
// choices whose condition holds, and for a firearm that means a gun you do
// not own (the supplier's rule). When the player owns all 13 firearms of the
// crates' weapon pool (the owned flags read false on each), a forced weapon
// pool offers the merces option even with the firearm weights held as written
// (90% of the pool). A hit then only swaps the crate's three items for one
// merces pickup, so it is not steered.
enum class CrateHitVerdict {
    Miss,         // the roll missed: vanilla
    Steer,        // a hit with a firearm that can be offered (or flags not read): weapon pool forced
    NoGunToOffer, // a hit, but the owned flags say every firearm of the pool is owned: vanilla
};

// firearms: the weapon pool's firearm options; flagsRead: how many of them had
// their owned and entitled flags read; eligible: how many of those read not
// owned and entitled. A firearm with unread flags may be offerable, so the
// hit is steered unless every firearm was read and none is eligible.
CrateHitVerdict JudgeCrateHit(bool hit, size_t firearms, size_t flagsRead, size_t eligible);

const char* CrateHitVerdictText(CrateHitVerdict verdict);

// What one open showed: its offers (items spawned under a crate option), how
// many of them were weapon-pool-only options (a firearm or the merces option:
// the tool pool has neither), and the options' state activations.
struct CrateOpenSeen {
    size_t offers = 0;
    size_t weaponOnlyOffers = 0;
    uint32_t toolStates = 0;   // activations with kCrateStateToolPool
    uint32_t weaponStates = 0; // activations with kCrateStateWeaponPool
};

// The pool the game took: by the offers first (a weapon-pool-only item: the
// weapon pool; any other item: the tool pool, whose melee options are the
// only non-firearm ones with a weight in vanilla), then by the states (they
// do not always tell the pools apart: a tool-pool open can show options in
// state 3), else None.
CratePool CratePoolTaken(const CrateOpenSeen& seen);

// One firearm option of the weapon pool.
struct CrateGun {
    int32_t rarity = 0; // Evergreen_Rarity 1..4 (0: none)
    double cost = 0.0;  // m_fB / the cost value; <= 0 or NaN counts as 100
};

// The firearms' weights: raw_i = CrateRarityValue x rarityMult / cost,
// scaled so the firearms' sum is gunShare / (1 - gunShare) x otherWeight
// (otherWeight: the pool's other weights, the merces option's). With
// gunShare >= 1, or otherWeight <= 0, the firearms' sum is 100 (the caller
// zeroes the other weights for gunShare >= 1). gunShare <= 0: all 0.
std::vector<double> CrateGunWeights(std::span<const CrateGun> guns, double otherWeight, const CrateGunsConfig& cfg);

// The firearms' share of the pool's weight, and each rarity's share of the
// firearms' weight (index CrateRarityIndex), for the log.
struct CrateGunShares {
    double gunShare = 0.0;
    std::array<double, 4> rarityShare {};
};

CrateGunShares CrateGunSharesOf(std::span<const CrateGun> guns, std::span<const double> weights, double otherWeight);

// The merces weight the weapon pool gives the merces option in vanilla:
// m_fA 100 / its cost (cost <= 0 or NaN: 1).
double CrateMercesWeight(double cost);

} // namespace fr
