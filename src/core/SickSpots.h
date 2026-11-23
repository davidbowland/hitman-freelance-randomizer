#pragma once

// Pure part of the sick spot steer (sick_spot_kills, on by default): an NPC
// sickened by emetic poison walks to a vomit spot ("sick spot") and throws
// up there. The mod raises the priority of the spots where 47 can then kill
// the NPC, so poisoned NPCs prefer those over a bin, a bush or a corner.
//
// What the game files say (chunk0 patch 4):
// - Every vomit spot is an instance of setpiece_trap_poison_sickspot (TEMP
//   kSickSpotTemp, TBLU kSickSpotTblu) or of
//   setpiece_trap_poison_sickspot_high (kSickSpotHighTemp / kSickSpotHighTblu;
//   bins and dumpsters). Each instance is its own drama situation
//   (DramaSituation_SickSpot, a dramasituation template instance, m_nPriority
//   2000) whose role casts an actor with STATE_SICK; a
//   ZDramaPriorityModifierActorDistance (path distance / 75 m, clamped to 1)
//   rates each spot by the actor's distance. How priority and that modifier
//   combine is not known. The GetSick situation runs at 1990, so
//   a spot's priority stays above it (raising never lowers one).
// - The normal template exposes m_nPriority (an alias of its
//   DramaSituation_SickSpot's); the high one exposes no priority, so bins
//   keep the game's 2000.
// - A spot inside activeassethelper_toilet_core (kToiletCoreTemp /
//   kToiletCoreTblu; toilets, the jacuzzi, Hokkaido's water basin, the soda
//   fountain) sits on a ZContextKillGuide of type toilet drown. Push and
//   snap-and-toss kills (setpiecehelpers_contextkill_push and the like) are
//   ZContextKillGuides too; ledges, cliffs and water edges are plain named
//   spots (Santa Fortuna's "River", "Cliff", "Ledge") that may or may not have
//   one nearby.
//
// So a spot is a kill spot when it is in a toilet core, or when a
// ZContextKillGuide of a kill type lies within kSickKillGuideRadius of where
// the NPC throws up (the act's position). Both kinds get the same boost.
//
// The radius: a context kill guide only offers its kill while the NPC stands
// within its m_fNPCMaxDistance (1.25 m on the toilet core). The NPC throws up
// at the spot's act, which both templates place 0.73 m off the spot's root
// (in a toilet core the root sits on the kill guide, so the act is 0.73 m
// from it, inside its 1.25 m reach); 2 m covers a guide's reach plus that
// offset with some slack, without reaching far past it. Each spot line logs its nearest guide's distance and reach, so
// the radius can be checked against the first in-game log.

#include "SpotPoint.h" // fr::SpotPoint, fr::SpotDistance

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace fr {

constexpr uint64_t kSickSpotTemp = 0x0098C7BD51E1C742ull;
constexpr uint64_t kSickSpotTblu = 0x00CD208F7E4A992Full;
constexpr uint64_t kSickSpotHighTemp = 0x0044692C463B261Eull;
constexpr uint64_t kSickSpotHighTblu = 0x00DC3CECBB213D88ull;
constexpr uint64_t kToiletCoreTemp = 0x00E59DEEA28D48E8ull;
constexpr uint64_t kToiletCoreTblu = 0x004973FB1EE4481Eull;

// Sub-entity ids inside the two sick spot templates (the same in every instance).
constexpr uint64_t kSickSituationId = 0xd71c20d8bcb74b8bull;     // DramaSituation_SickSpot
constexpr uint64_t kSickActId = 0x3705766271c8c769ull;           // Act_MR_Stand_Sick_Throw_Up01
constexpr uint64_t kSickRoleId = 0x05c1d94e5803ed0eull;          // Role_SickSpot
constexpr uint64_t kSickHighSituationId = 0x90f44c2944c8c2b3ull; // DramaSituation_SickSpot
constexpr uint64_t kSickHighActId = 0xbd79debbc4b76089ull;       // Act_MR_Stand_Sick_Throw_Up_High
constexpr uint64_t kSickHighRoleId = 0x3bdc657ea6ce91daull;      // Role_SickSpot

constexpr int32_t kSickBoostDefault = 1000; // sick_spot_kill_boost
constexpr int32_t kSickBoostMax = 5000;
constexpr double kSickKillGuideRadius = 2.0; // m, 3D, from the act to a ZContextKillGuide

constexpr size_t kSickMaxSpots = 256;    // spots handled per load
constexpr size_t kSickMaxGuides = 4096;  // kill guides compared per load
constexpr size_t kSickMaxPinLines = 300; // pin lines per load

enum class SickTemplate : uint8_t {
    None,
    Normal, // setpiece_trap_poison_sickspot
    High,   // setpiece_trap_poison_sickspot_high (bins, dumpsters)
};

// By a blueprint factory's runtime resource id (TEMP or TBLU).
SickTemplate SickTemplateOf(uint64_t rid);
bool IsToiletCore(uint64_t rid);

// "normal", "high".
const char* SickTemplateName(SickTemplate t);

// ZContextKillGuide::m_eType (ZContextKillGuide_EContextKillType):
// "toilet drown", "snap and toss", "push", "no animation", "interrupt
// animation", "type 7", "type unread".
std::string KillGuideTypeName(std::optional<int32_t> type);

// The guide kills by drowning, push or snap and toss (types 0 to 2), or its
// type is unread (it is still a ZContextKillGuide).
bool KillGuideCounts(std::optional<int32_t> type);

struct KillGuide {
    uint64_t id = 0;
    std::optional<SpotPoint> at;
    std::optional<int32_t> type;
    std::optional<float> npcMaxDistance; // m_fNPCMaxDistance
};

enum class SickSpotKind : uint8_t {
    Toilet,    // inside a toilet core (drown)
    KillGuide, // a counting kill guide within the radius
    Plain,
    Unknown, // not in a toilet core and its position is unread
};

// "toilet (drown)", "kill spot", "plain", "unknown (position unread)".
const char* SickSpotKindName(SickSpotKind kind);

bool IsKillSpot(SickSpotKind kind);

struct SickVerdict {
    SickSpotKind kind = SickSpotKind::Unknown;
    std::optional<size_t> nearest;  // the nearest counting guide with a position
    std::optional<double> distance; // its distance
};

// A spot: in a toilet core, a toilet; else by the nearest counting guide
// (ties: the lower index) within radius (inclusive).
SickVerdict ClassifySickSpot(bool inToiletCore, std::optional<SpotPoint> at, std::span<const KillGuide> guides,
                             double radius = kSickKillGuideRadius);

// The boost as the mod uses it: clamped to 0..kSickBoostMax.
int32_t ClampSickBoost(int32_t boost);

// before + the clamped boost, saturating at INT32_MAX; never below before.
int32_t BoostedSickPriority(int32_t before, int32_t boost);

// A priority the mod raised earlier in this session: the value it read, and the one it wrote.
struct SickRaise {
    int32_t original;
    int32_t written;
};

// The priority to write over current: boosted from the recorded original when
// current still holds what was written (a restart that kept the entity), so
// the boost never stacks; else boosted from current.
int32_t SickPriorityToWrite(int32_t current, const std::optional<SickRaise>& earlier, int32_t boost);

// With the boost off: the recorded original to write back when current still
// holds what the mod wrote (a restart that kept the entity); else nothing.
std::optional<int32_t> SickPriorityToRestore(int32_t current, const std::optional<SickRaise>& earlier);

// One spot as the pin lines compare it with an actor.
struct SickSpotPlace {
    std::optional<SpotPoint> at;
    SickSpotKind kind = SickSpotKind::Unknown;
};

// The nearest spot to p of each class: a kill spot (toilet or kill guide),
// and a plain or unknown one. Spots without a position are skipped.
struct SickNearest {
    std::optional<size_t> kill;
    std::optional<double> killDistance;
    std::optional<size_t> plain;
    std::optional<double> plainDistance;
};

SickNearest NearestSickSpots(std::span<const SickSpotPlace> spots, SpotPoint p);

// What one load did, for the summary line.
struct SickTally {
    size_t spots = 0;
    size_t normal = 0;
    size_t high = 0;
    size_t toilet = 0;
    size_t killGuide = 0;
    size_t plain = 0;
    size_t unknown = 0;
    size_t guides = 0;         // ZContextKillGuides found
    size_t guidesCounting = 0; // of a kill type (or unread type)
    size_t raised = 0;         // normal kill spots whose priority read back raised
    size_t notRaised = 0;      // normal kill spots whose write did not take
    size_t highKill = 0;       // high-template kill spots (no priority to raise)
};

// "41 sick spots (33 normal, 8 high): 6 toilets, 3 near a kill guide, 30 plain, 2 unknown; 57 kill guides (40 of a
// kill type); priority raised by 1000 on 9 of 9 normal kill spots" (then " (not raised on 2: see the spot lines)"
// and ", 1 high kill spot left at its priority (...)" when so); off or boost 0: "priority left alone (...)".
std::string SickSummaryText(const SickTally& tally, int32_t boost, bool on);

} // namespace fr
