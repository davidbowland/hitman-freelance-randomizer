#pragma once

// Supplier steering (setting supplier_steer, on by default): the pure part.
// The game side is src/mod/StockSteer.*, fed by the supplier walk
// (SupplierWalk.h).
//
// How the supplier and the reward crate offer items:
// - Every offer is an option helper whose ZRandomSelectorChoice weight is the
//   m_fA / m_fB of its ZMathMultiplyDivide: m_fB is the item's cost, written
//   ~0.35 s into play and then left alone; m_fA is the rarity value (70 / 20
//   / 9 / 1, 100 for items without a rarity), written through the math's pin
//   in the frame of the roll, whose output pin then sets the choice's weight.
// - Each choice also has an m_Condition (a ZValueBool_Operation_Signal) that is
//   false until the roll evaluates it; at the roll it is true only for items
//   the player does not own (the option's ZInvertedCondition members
//   b09232f7 / 9bf7d77d / 994ee783 read true) and is entitled to (c5ddfb8c
//   reads true; false on COMIC / SHAMROCK items without the DLC). Those flags
//   are set at scene playing, long before the roll, so they are what the mod
//   reads to know the eligible guns ahead of it.
// - The selectors draw only among choices whose condition holds: the reward
//   crate's 23 level-1 options each weighed 1/23, 22 were owned, and its one
//   unowned common gun (ICA 19) was awarded every campaign; a selector with
//   no eligible choice fires OnPickFailed and its slot stays empty.
// - Supplier: five gun selectors (assault rifle, sniper, pistol, SMG, shotgun;
//   m_Seed b73d9349324f8074) roll in the frame of SupplierVisited, right
//   after the m_fA writes. Within a slot a legendary-valued gun weighs 1/70 of
//   a common one of the same cost, so DLC and event variants (mostly
//   legendary-valued) almost never come up.
// - Reward crate (safehouse, m_Seed c8657a023aedee52, 148 choices): at ~6.3 s
//   of play the crate rolls: m_fA 100 on the level's tier (level 1: the 23
//   common options), m_fB the tier's size (23 / 54 / 45 / 26), every other
//   option keeps weight 0 and an unevaluated (false) condition.
//
// The steering, weights only (never a choice list or a condition, so an owned
// gun can never be offered): before the roll, each supplier gun selector's
// eligible guns get m_fB lowered so their weights m_fA / m_fB come out equal
// (the heaviest eligible gun's weight; nothing is raised), and the weight
// their math outputs at the roll is re-asserted to that value on its way to
// the choice. Uniform, not a DLC boost: within a slot every gun you do not own
// is equally likely, so a DLC variant is as likely as a common one.
//
// Gear steering (setting supplier_steer_gear, on by default, under
// supplier_steer): the ~279 supplier options in no selector (blocks A
// explosives / poisons / tools / distractions, B tools + pistols, C melee, D
// tools + pistols, E melee, F a copy of A, SupplierRules.h) have no selector the walk found, but each non-gun slot seems
// to draw from one block, and only flag-eligible items (unowned flags and
// entitled flag true, as for the guns; the choice condition read false even on
// offered items, so the flags are the usable test) are offered. So they are
// steered as one pseudo-slot: every non-firearm option with a weight math
// (IsGearSteerOption) goes into one fr::PlanSupplierSteer, which gives every
// eligible one the same weight (the heaviest eligible one's; m_fB lowered
// only). With all eligible options at one weight, whichever real selector
// draws, it is uniform among its eligible items. The loose pistols (blocks B
// and D) are left alone. There is no pick signal to restore on: the
// originals come back kGearRestoreAfterOffer s after the first offer under a
// loose option, or kGearRestoreAfterVisit s after SupplierVisited, whichever
// is first; only offers and visits after the steer count (GearRestoreClock).
//
// Reward crate steering was tried and removed (setting crate_reward_unowned):
// every eligible gun of any tier got a weight dominating the rolled tier's.
// The crate cannot draw outside its rolled tier: with the one eligible gun of
// another tier, its condition stays false, the selector fires OnPickFailed
// and the crate pays merces. Weights alone cannot steer it.

#include "GameIds.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fr {

// Option members: ZInvertedCondition flags that read true on items the player
// does not own, and the one that reads false on DLC items without the
// entitlement.
inline constexpr uint64_t kUnownedFlagIds[] = {0xb09232f71c3f2688ull, 0x9bf7d77d61a73e4full, 0x994ee7832c056d10ull};
inline constexpr uint64_t kEntitledFlagId = 0xc5ddfb8c376bf711ull;

// When the mod steers (s of play): after the owned flags (scene playing,
// settled by 1.2 s in missions) and the cost writes (0.35 s), well before the
// earliest supplier roll seen (19 s).
inline constexpr double kStockSteerAt = 2.0;

// The steered values are checked (and written again if the game changed
// them) this often until the roll.
inline constexpr double kStockSteerCheckEvery = 0.5;

// The originals are restored this long after a selector's last pick signal
// (the five supplier slots fired theirs within 130 ms, two per slot).
inline constexpr double kStockRestoreAfter = 1.0;

// The game's m_fA for an item: 70 / 20 / 9 / 1 by Evergreen_Rarity 1..4, 100
// without a rarity (a Coin weighs 100, as the crate tier). Note:
// with weapon_rarity on, the game's m_fA follows the vanilla rarity, not the
// shuffled one pro.repo holds (HWK 21 Covert, shuffled to legendary, got 70),
// so this prediction is then off; the output re-assert covers that.
double StockRarityValue(int32_t rarity, bool hasRarity);

// The flags of one option: how many of the unowned flags read true and false,
// and the entitled flag (empty when not read).
struct StockFlags {
    size_t unownedTrue = 0;
    size_t unownedFalse = 0;
    std::optional<bool> entitled;
};

// Eligible: every unowned flag read reads true (at least one read), and the
// entitled flag reads true. An option with unread flags is not eligible.
bool StockEligible(const StockFlags& flags);

// One option of a supplier gun selector: the predicted m_fA, its m_fB (the
// game's, before any write) and whether it is eligible.
struct SupplierSteerOption {
    double fA = 0.0;
    double fB = 0.0;
    bool eligible = false;
};

struct SupplierSteerPlan {
    double weight = 0.0;        // the eligible guns' common weight (0: none)
    size_t eligible = 0;        // eligible options with a usable fA and fB (> 0, finite)
    size_t lowered = 0;         // of them, those whose m_fB goes down
    std::vector<double> fB;     // per option: the m_fB to hold (the game's for the others)
    std::vector<double> target; // per option: the weight to re-assert at the roll, or -1
};

// Uniform weights among the eligible options by lowering m_fB only: the
// common weight is the heaviest eligible fA / fB, each eligible m_fB becomes
// fA / weight. With fewer than two usable eligible options nothing changes
// (no target either: one gun is the slot's only pick anyway).
SupplierSteerPlan PlanSupplierSteer(std::span<const SupplierSteerOption> options);

// One supplier option for the gear pseudo-slot: in it when no selector draws
// it, it is not a firearm (the loose pistols are left alone), and its weight
// math (m_fA and m_fB) was found.
struct GearCandidate {
    bool inSelector = false;
    bool firearm = false;
    bool hasWeightMath = false;
};

bool IsGearSteerOption(const GearCandidate& option);

inline constexpr double kGearRestoreAfterOffer = 1.0;
inline constexpr double kGearRestoreAfterVisit = 2.0;

// When the gear pseudo-slot is restored: the earliest of an offer under a
// loose option + kGearRestoreAfterOffer and a SupplierVisited +
// kGearRestoreAfterVisit. Offers before the steer are ignored; a visit before
// it (the supplier already rolled) makes the restore due at the steer. Each
// call says whether it moved the moment earlier.
class GearRestoreClock {
public:
    bool Steered(double at);
    bool Offer(double at) { return Earlier(at + kGearRestoreAfterOffer); }
    bool Visit(double at);
    std::optional<double> RestoreAt() const { return m_At; }
    bool Due(double now) const { return m_At && now >= *m_At; }

    void Reset() {
        m_Steered = false;
        m_VisitedEarly = false;
        m_At.reset();
    }

private:
    bool Earlier(double at);
    bool m_Steered = false;
    bool m_VisitedEarly = false;
    std::optional<double> m_At;
};

// A weight read back matches the one steered (relative 1e-4; NaN never).
bool SteeredWeightHeld(double actual, double target);

// "3 of 4 held (Krugermeier .22 1000, Bellini 0.0435 MISSED (steered 1000), +2 more)": how many
// of the steered weights hold, with each name and value (at most maxNames).
struct SteeredWeight {
    std::string name;
    double actual = 0.0;
    double target = 0.0;
};

std::string SteeredWeightsText(std::span<const SteeredWeight> weights, size_t maxNames);

} // namespace fr
