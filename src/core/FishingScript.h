#pragma once

// Fishing script (fishing_script, on by default): the pure part. The game
// side is src/mod/FishingSpot.*.
//
// What the safehouse does ([assembly:/_pro/scenes/missions/snug/mission_vanilla.brick],
// brick 00A9B93A825F5BDC; read from its graph):
// - "Setpiece_S03_HM_LIP_Stand_Fishing" (0947b9e4379216d7) is the fishing spot.
//   Its Reel_cast output goes to "SignalFork_Void_01" (804f238787d6fa39), whose
//   Out02 starts "TimerSimple 2" (1c89038c59c54980); the timer's Out signals
//   Pick on "RandomSelector" (01a1a1a3f750e504). Leaving the spot (OnLeaving)
//   aborts the timer, so no roll happens without a bite.
// - The selector has six "RandomSelectorChoice" entities; choice k's OnPicked
//   signals SpawnItem on "ItemSpawner k" (k = 1..6). Spawners 1, 2, 3 and 5
//   send ItemSpawned to "HeroItemAction" (aa0d8d24f8497122: the catch goes into
//   47's hands when he leaves the spot); 4 and 6 do not (their ItemReady resets
//   "CaughtSomething"). Every spawner's ItemReady lets 47 out of the spot.
// - The selector's OnPickDone sets "CaughtSomething" (33ac0b947e10d752).
// - Item keys in the brick (which spawner holds which is not in the graph):
//   ItemKey_Melee_Fish ce8aa2b84e6f2088, ItemKey__Fiberwire_ Fishing Line
//   224cecf1549aa5fb, ItemKey_Melee_RustyPirateSaber 1fe5c5f2fb352a7d (Captain
//   Jarl's sword), ItemKey_Melee_StarfishShuriken 800cfdc287f8d8e8,
//   ItemKey_Tool_Rusty_Metal_Rod_OneTimeUse 10ddebd61d00c6b4,
//   ItemKey_Melee_Driftwood fc340b87657bb4c8.
// Not known in advance: the choices' weights and conditions (an
// "owned already" condition on the saber?), which spawner holds which item,
// and whether a catch writes a CPD key.
//
// The script, weights only (never a choice list or a condition): catch n
// (1-based, counted from the start of each safehouse visit) is
// kFishingScript[n-1] while n <= 3, vanilla after. Right before the
// selector's Pick, the choices holding the scripted item keep their weight
// (1 when theirs is 0) and every other choice gets weight 0; at the
// selector's OnPickDone / OnPickFailed the game's weights are written back.
// When the scripted item cannot be drawn (OnPickFailed: its condition hides
// it, e.g. owned already), the weights are restored and the mod signals Pick
// again on the next frame, unsteered: that catch is vanilla. A catch (an
// OnPickDone while the script is on) advances the count either way.

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

// The safehouse brick (the same one as the coin's).
inline constexpr uint64_t kFishingBrickRid = 0x00A9'B93A'825F'5BDCull;

inline constexpr uint64_t kFishingSpotId = 0x0947'b9e4'3792'16d7ull;     // Setpiece_S03_HM_LIP_Stand_Fishing
inline constexpr uint64_t kFishingTimerId = 0x1c89'038c'59c5'4980ull;    // TimerSimple 2: Out -> selector Pick
inline constexpr uint64_t kFishingSelectorId = 0x01a1'a1a3'f750'e504ull; // RandomSelector
inline constexpr uint64_t kFishingCaughtId = 0x33ac'0b94'7e10'd752ull;   // CaughtSomething (ValueBool)
inline constexpr uint64_t kFishingHandsId = 0xaa0d'8d24'f849'7122ull;    // HeroItemAction

inline constexpr size_t kFishingChoices = 6;

// Choice k (0-based) and the spawner its OnPicked signals.
inline constexpr std::array<uint64_t, kFishingChoices> kFishingChoiceIds = {
    0xf176'4152'4618'88abull, // RandomSelectorChoice   -> ItemSpawner 1
    0x4713'73a8'3a0f'dc8dull, // RandomSelectorChoice 2 -> ItemSpawner 2
    0x28a4'5d8e'2462'f740ull, // RandomSelectorChoice 3 -> ItemSpawner 3
    0x162e'4acf'1549'65ddull, // RandomSelectorChoice 4 -> ItemSpawner 4
    0x6d3f'fe32'37e2'608cull, // RandomSelectorChoice 5 -> ItemSpawner 5
    0xebe3'86fd'0dad'5c42ull, // RandomSelectorChoice 6 -> ItemSpawner 6
};
inline constexpr std::array<uint64_t, kFishingChoices> kFishingSpawnerIds = {
    0x9b05'4780'5bcf'c281ull, // ItemSpawner 1
    0xaa9a'025d'7469'017bull, // ItemSpawner 2
    0xa645'9600'f215'52f7ull, // ItemSpawner 3
    0xfa1b'8e1f'2016'3cc2ull, // ItemSpawner 4
    0xb80e'c593'ecac'c1eeull, // ItemSpawner 5
    0xe922'628b'7b04'e0e0ull, // ItemSpawner 6
};

// The item keys of the brick that look like catches (for the log).
struct FishingKey {
    uint64_t id;
    const char* name;
};

inline constexpr std::array<FishingKey, 6> kFishingKeys = {{
    {0xce8a'a2b8'4e6f'2088ull, "ItemKey_Melee_Fish"},
    {0x224c'ecf1'549a'a5fbull, "ItemKey__Fiberwire_ Fishing Line"},
    {0x1fe5'c5f2'fb35'2a7dull, "ItemKey_Melee_RustyPirateSaber"},
    {0x800c'fdc2'87f8'd8e8ull, "ItemKey_Melee_StarfishShuriken"},
    {0x10dd'ebd6'1d00'c6b4ull, "ItemKey_Tool_Rusty_Metal_Rod_OneTimeUse"},
    {0xfc34'0b87'657b'b4c8ull, "ItemKey_Melee_Driftwood"},
}};

inline constexpr std::string_view kFishRepositoryId = "4d0d6b2a-dd81-474c-a412-3bf19af8233d"; // "Fish"
inline constexpr std::string_view kFishingLineRepositoryId =
    "2147b6cd-5a42-4cd6-b366-2c5c50d97db7";                             // "(Fiberwire) Fishing Line"
inline constexpr uint64_t kPirateSaberKeyId = 0x1fe5'c5f2'fb35'2a7dull; // ItemKey_Melee_RustyPirateSaber

// What a catch is, as far as the script cares.
enum class FishCatch { Fish, FishingLine, PirateSaber, Other };

// "fish", "fishing line", "pirate saber", "other".
const char* FishCatchName(FishCatch p_Catch);

// The script: catch 1, 2, 3.
inline constexpr std::array<FishCatch, 3> kFishingScript = {FishCatch::Fish, FishCatch::FishingLine,
                                                            FishCatch::PirateSaber};

// The scripted item of catch p_Number (1-based), or empty (vanilla) outside the script.
std::optional<FishCatch> ScriptedCatch(uint32_t p_Number);

// What a spawner's item is: by repository id (fish, fishing line); the saber
// by its item key's sub-entity id or a title containing "pirate" (any case).
FishCatch ClassifyFishItem(const std::optional<Guid>& p_Repository, uint64_t p_KeyId, std::string_view p_Title);

struct FishingSteerPlan {
    bool steer = false;
    std::vector<float> weights; // per choice, when steer: the weight to hold during the roll
    size_t targets = 0;         // choices holding the wanted item
    std::string why;
};

// The weights that leave the wanted item the only weighted catch: its
// choices keep their weight (1 when it is 0 or less), every other choice
// gets 0. No steer when no choice holds it, or a weight is unreadable or not
// finite (then the roll stays vanilla).
FishingSteerPlan PlanFishingSteer(std::span<const FishCatch> p_Items, std::span<const std::optional<float>> p_Weights,
                                  FishCatch p_Want);

// Seconds a roll may stay open without the selector's answer before the
// weights are restored anyway.
inline constexpr double kFishingRollTimeout = 2.0;

struct FishingRollStart {
    bool opened = false; // a new roll (false: one is open already, nothing to do)
    bool steer = false;  // write plan.weights now
    uint32_t number = 0; // the catch number this roll decides (1-based)
    std::optional<FishCatch> wanted;
    FishingSteerPlan plan;
    std::string why;
};

struct FishingRollEnd {
    bool closed = false;  // a roll was open and is now closed
    bool restore = false; // the game's weights go back
    bool counted = false; // a catch: this visit's count is now `catches`
    bool retry = false;   // the mod signals Pick again next frame (unsteered)
    uint32_t number = 0;
    uint32_t catches = 0;
    std::optional<size_t> picked;
    std::string verdict;
};

// One safehouse visit's rolls. Times are seconds of play. With the script
// off it only records, so the log can say what vanilla did.
class FishingRolls {
public:
    // p_Catches: the catches already made this visit (0 at the visit's start).
    FishingRolls(bool p_On, uint32_t p_Catches, std::vector<FishCatch> p_Items);

    // Right before the selector's Pick. p_Weights: each choice's current weight.
    FishingRollStart Start(std::span<const std::optional<float>> p_Weights, double p_Now);

    // A choice's OnPicked (0-based).
    void Picked(size_t p_Choice);

    // The selector's OnPickDone / OnPickFailed.
    FishingRollEnd Done(double p_Now);
    FishingRollEnd Failed(double p_Now);

    // Every frame: closes a roll the selector never answered.
    FishingRollEnd Tick(double p_Now);

    // True once after a Failed asked for a vanilla re-pick; the next Start is unsteered.
    bool TakeRetry();

    uint32_t Catches() const { return m_Catches; }
    bool Open() const { return m_Open; }
    uint32_t Rolls() const { return m_Rolls; }
    uint32_t Steered() const { return m_SteeredRolls; }
    uint32_t AsPlanned() const { return m_AsPlanned; }
    uint32_t Missed() const { return m_Missed; }
    uint32_t Retries() const { return m_RetryCount; }

private:
    FishingRollEnd Close(bool p_Done, const char* p_How);

    bool m_On;
    uint32_t m_Catches;
    std::vector<FishCatch> m_Items;

    bool m_Open = false;
    bool m_Steering = false;     // the open roll is steered
    bool m_RetryPending = false; // a Failed asked for a re-pick
    bool m_NextVanilla = false;  // the next Start is the re-pick
    bool m_IsRetry = false;      // the open roll is the re-pick
    double m_OpenedAt = 0.0;
    uint32_t m_Number = 0;
    std::optional<FishCatch> m_Wanted;
    std::optional<size_t> m_Picked;
    uint32_t m_Rolls = 0;
    uint32_t m_SteeredRolls = 0;
    uint32_t m_AsPlanned = 0;
    uint32_t m_Missed = 0;
    uint32_t m_RetryCount = 0;
};

} // namespace fr
