#pragma once

// Pure logic of the official map variants (round V-1): which Freelancer
// scenes have variant options, the seeded pick per campaign seed and map
// (a roll walked over each option's chance setting), and what to do with the
// pick given the scene's current brick list. An option appends one or more
// official bricks.

#include "Guid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

struct FullVariant; // LandslideFull.h

// Feature name of variant picks in DialHash.
inline constexpr std::string_view kVariantFeature = "variant";

// An official brick, appended to a scene's additional bricks.
struct VariantBrick {
    std::string_view name; // "tod_zika_night"
    std::string_view path; // "assembly:/_pro/scenes/missions/bangkok/tod_zika_night.brick"
    uint64_t rid;          // the TEMP hash of [path].pc_entitytype (ZRuntimeResourceID::FromString)
};

extern const VariantBrick kTodZikaNightBrick;
extern const VariantBrick kMissionZikaBrick;
extern const VariantBrick kTodFluBrick;
extern const VariantBrick kGfxFluBrick;
extern const VariantBrick kTodEbolaNightBrick;
extern const VariantBrick kLightingMambaBrick;
extern const VariantBrick kTodSummerMorningClearBrick; // coastaltown
extern const VariantBrick kLightingCopperheadBrick;
extern const VariantBrick kTodSummerNightClearBrick;          // coastaltown
extern const VariantBrick kTodSummerNightClearMarrakeshBrick; // marrakesh
extern const VariantBrick kDressingBeachMambaBrick;
extern const VariantBrick kDressingChurchMambaBrick;
extern const VariantBrick kDressingOldtownMambaBrick;
extern const VariantBrick kDressingTownhallMambaBrick;
extern const VariantBrick kDressingTunnelsMambaBrick;
extern const VariantBrick kChallengesZikaBrick;
extern const VariantBrick kLocationParisNoelBrick;
extern const VariantBrick kEaFluBrick;
extern const VariantBrick kScenarioMamushiBrick;
extern const VariantBrick kScenarioGreentiniEaBrick;
extern const VariantBrick kScenarioGreentiniLightingBrick;
extern const VariantBrick kElusiveRedsnapperBrick;
extern const VariantBrick kElusiveTomorrowlandBrick;
extern const VariantBrick kElusiveAlabamaslammerBrick;
extern const VariantBrick kElusiveVesperBrick;
extern const VariantBrick kElusiveBrambleBrick;
extern const VariantBrick kElusiveRadlerBrick;
extern const VariantBrick kElusiveClericoBrick;
extern const VariantBrick kElusiveJockeyclubBrick;
extern const VariantBrick kElusiveGoldendoubletBrick;
extern const VariantBrick kElusiveSkittlebombBrick;
extern const VariantBrick kElusiveHighballBrick;
// The Garden Show, the HITMAN 2016 elusive targets, the Sarajevo Six and heavy layers (see Variants.cpp).
extern const VariantBrick kScenarioSmoothsnakeBrick;
extern const VariantBrick kSceneTequilasunriseBrick;
extern const VariantBrick kSceneShandyBrick;
extern const VariantBrick kSceneKamikazeBrick;
extern const VariantBrick kSceneKirroyalBrick;
extern const VariantBrick kSceneMojitoBrick;
extern const VariantBrick kSceneSakiniBrick;
extern const VariantBrick kSceneMargaritaBrick;
extern const VariantBrick kSceneWhiterussianBrick;
extern const VariantBrick kSceneLumumbaBrick;
extern const VariantBrick kSceneBlueblazerBrick;
extern const VariantBrick kMissionMintjulepBrick;
extern const VariantBrick kMissionHarveywallbangerBrick;
extern const VariantBrick kMissionSazeracBrick;
extern const VariantBrick kMissionPiscosourBrick;
extern const VariantBrick kMissionQuadruplerumandcokeBrick;
extern const VariantBrick kScenarioCorpsereviverBrick;
extern const VariantBrick kMissionMimosaBrick;
extern const VariantBrick kMissionCosmopolitanBrick;
extern const VariantBrick kMissionScrewdriverBrick;
extern const VariantBrick kMissionHottoddyBrick;
extern const VariantBrick kMissionGinfizzBrick;
extern const VariantBrick kMissionBloodymaryBrick;
extern const VariantBrick kMissionMartiniBrick;
extern const VariantBrick kMissionBrassmonkeyBrick;
extern const VariantBrick kMissionGimletBrick;
extern const VariantBrick kSceneFlirtiniBrick;
extern const VariantBrick kSceneBushwackerBrick;
extern const VariantBrick kSceneBlackrussianBrick;
extern const VariantBrick kSceneMoscowmuleBrick;
extern const VariantBrick kSceneDirtyoctopusBrick;
extern const VariantBrick kMissionSeabreezeBrick;
extern const VariantBrick kScenarioGibsonBrick;
extern const VariantBrick kMissionSambucaBrick;
extern const VariantBrick kScenarioSambuca2Brick;
// More heavy layers (see Variants.cpp).
extern const VariantBrick kScenarioFrenchmartiniBrick;
extern const VariantBrick kScenarioBaijuBrick;
extern const VariantBrick kScenarioPomadaBrick;
extern const VariantBrick kElusivePomadaSoundBrick;
extern const VariantBrick kScenarioLambicBrick;
extern const VariantBrick kSoundLambicBrick;
extern const VariantBrick kScenarioFilurBrick;
extern const VariantBrick kScenarioGreentiniBrick;
extern const VariantBrick kScenarioBelliniBrick;
extern const VariantBrick kScenarioKingcobraBrick;
extern const VariantBrick kOutfitsKingcobraBrick;
extern const VariantBrick kScenarioGartersnakeBrick;
extern const VariantBrick kScenarioAnacondaBrick;
extern const VariantBrick kScenarioCottonmouthBrick;
extern const VariantBrick kScenarioAzaleaBrick;
extern const VariantBrick kScenarioFrangipaniBrick;
extern const VariantBrick kScenarioAmbrosiaBrick;
extern const VariantBrick kAncestralHarebellBrick;
extern const VariantBrick kOutfitsSkunkBrick;

// Where an NPC was seen (world position), for the log.
struct NpcSpot {
    float x, y, z;
};

// A scene NPC an option strands (its act spot under the option's set
// dressing, or left standing on nothing by its deletes).
struct StrandedNpc {
    std::string_view name;
    std::string_view repo;                      // repository id ("xxxxxxxx-xxxx-...")
    uint64_t brickRid = 0;                      // the brick its actor is in (0: any loaded brick)
    std::optional<NpcSpot> seen = std::nullopt; // where it was seen, if known
};

// npc_ambient_beachsquare_day (TEMP hash), the harbor square's day NPCs.
inline constexpr uint64_t kNpcAmbientBeachsquareDayRid = 0x003D280D592C5059ull;

inline constexpr size_t kLandslideStageSitterCount = 10;

// The day NPCs found standing or sitting under Landslide's stage in every
// measured load (a cafe group of the harbor square): kLandslide's removed NPCs.
std::span<const StrandedNpc> LandslideStageSitters();

// A variant option: the bricks it appends, in order. Never "none".
struct VariantOption {
    std::string_view name; // "patient_zero"
    std::vector<const VariantBrick*> bricks;
    int32_t defaultChance; // variant_chance_<name>'s default, in percent
    bool check = false;    // heavy (story NPCs or a set-dressing layer): log the "variant check" after the load;
                           // false: lighting only (IsLightingOnly)
    // Repository ids ("xxxxxxxx-xxxx-...") of scene NPCs that must never be
    // targets while this option is loaded but stay in the scene: its own
    // story's targets (its official contract's, Variants.cpp); see
    // VariantTargetExclusions.
    std::vector<std::string_view> notTargets = {};
    // Scene NPCs this option strands: switched off once the scene's entities
    // exist (VariantCleanup.h), and never targets either.
    std::vector<StrandedNpc> removed = {};
};

extern const VariantOption kTheSource;     // mission_zika + tod_zika_night + challenges_zika (check)
extern const VariantOption kPatientZero;   // tod_flu + gfx_flu + ea_flu (check); always its full version
extern const VariantOption kTodEbolaNight; // tod_ebola_night
// lighting_mamba + tod_summer_morning_clear + the dressing_*_mamba set dressing (check)
extern const VariantOption kLandslide;
extern const VariantOption kLightingCopperhead; // tod_summer_night_clear + lighting_copperhead
// The Bad Boy: The Icon's bricks (as kLightingCopperhead); always its full version, the full Icon with The Bad
// Boy's own brick.
extern const VariantOption kTheBadBoy;
extern const VariantOption kHolidayHoarders; // location_parisnoel (check)
extern const VariantOption kSnowFestival;    // Hokkaido: scenario_mamushi (check)
extern const VariantOption kReflection;      // Hokkaido: scenario_bellini, in full (check)
extern const VariantOption kHerbalistParty;  // Haven: scenario_greentini + its dressing and lights (check)
extern const VariantOption kTheRage;         // Chongqing: elusive_redsnapper (check)
extern const VariantOption kHarbinger;       // Dartmoor: scenario_filur, in full (check)
extern const VariantOption kTheDrop;         // Berlin: elusive_tomorrowland (check)
extern const VariantOption kSerialKiller;    // Whittleton Creek: elusive_alabamaslammer (check)
extern const VariantOption kProcurers;       // Dartmoor: elusive_vesper (check)
extern const VariantOption kCollector;       // Dartmoor: elusive_bramble (check)
extern const VariantOption kLiability;       // Berlin: elusive_radler (check)
extern const VariantOption kHeartbreaker;    // Mendoza: elusive_clerico (check)
extern const VariantOption kIconoclast;      // Mendoza: elusive_jockeyclub (check)
extern const VariantOption kAppraiser;       // Isle of Sgail: elusive_goldendoublet (check)
extern const VariantOption kStowaway;        // Isle of Sgail: elusive_skittlebomb (check)
extern const VariantOption kRevolutionary;   // Santa Fortuna: elusive_highball (check)
// One official layer each over the base Freelancer already loads; all checked.
extern const VariantOption kGardenShow;       // Dartmoor: scenario_smoothsnake (check)
extern const VariantOption kTheForger;        // Paris
extern const VariantOption kTheSensation;     // Paris
extern const VariantOption kTheBlackHat;      // Paris
extern const VariantOption kThePharmacist;    // Paris
extern const VariantOption kThePaparazzo;     // Paris
extern const VariantOption kTheChef;          // Paris
extern const VariantOption kTheBroker;        // Paris
extern const VariantOption kTheIdentityThief; // Paris
extern const VariantOption kTheBlackmailer;   // Paris
extern const VariantOption kTheDirector;      // Paris: The Sarajevo Six
extern const VariantOption kTheCongressman;   // Sapienza
extern const VariantOption kTheGuru;          // Sapienza
extern const VariantOption kThePrince;        // Sapienza
extern const VariantOption kTheTwin;          // Sapienza
extern const VariantOption kTheWildcard;      // Sapienza
extern const VariantOption kTheDeceivers;     // Sapienza
extern const VariantOption kTheEnforcer;      // Sapienza: The Sarajevo Six
extern const VariantOption kTheFixer;         // Marrakesh
extern const VariantOption kTheGunrunner;     // Marrakesh
extern const VariantOption kTheAngelOfDeath;  // Marrakesh
extern const VariantOption kTheExtractor;     // Marrakesh: The Sarajevo Six
// The Entertainer: Marrakesh's night time of day; always its full version, the full Marrakesh night (the night
// level) with The Entertainer's own brick.
extern const VariantOption kTheEntertainer;
extern const VariantOption kTheWarlord;        // Bangkok
extern const VariantOption kTheFoodCritic;     // Bangkok
extern const VariantOption kTheExDictator;     // Bangkok
extern const VariantOption kTheVeteran;        // Bangkok: The Sarajevo Six
extern const VariantOption kTheBookkeeper;     // Colorado
extern const VariantOption kTheChameleon;      // Colorado
extern const VariantOption kTheMercenary;      // Colorado: The Sarajevo Six
extern const VariantOption kTheSurgeons;       // Hokkaido
extern const VariantOption kTheFugitive;       // Hokkaido
extern const VariantOption kTheController;     // Hokkaido: The Sarajevo Six
extern const VariantOption kTheAscensionist;   // Dubai: scenario_gibson (check, own navmesh)
extern const VariantOption kTheUndying;        // Miami: mission_sambuca (check, own navmesh)
extern const VariantOption kTheUndyingReturns; // Miami: scenario_sambuca2, a wrapper of mission_sambuca (check)
// Heavy layers, each its official scene's bricks beyond Freelancer's; all checked, each with its own
// navmesh.
extern const VariantOption kTheBanker;           // Paris: scenario_frenchmartini
extern const VariantOption kTheInfiltrator;      // Bangkok: scenario_baiju
extern const VariantOption kTheWizard;           // Isle of Sgail: scenario_pomada + its sound
extern const VariantOption kTheSplitter;         // Chongqing: scenario_lambic + its sound
extern const VariantOption kIllusionsOfGrandeur; // Mumbai: scenario_kingcobra + its outfits (special assignment)
extern const VariantOption kABitterPill;         // Whittleton Creek: scenario_gartersnake (special assignment)
extern const VariantOption kEmbraceOfTheSerpent; // Santa Fortuna: scenario_anaconda (special assignment)
extern const VariantOption kASilverTongue;       // Miami: scenario_cottonmouth (special assignment)
extern const VariantOption kGluttony;            // Chongqing: scenario_azalea (Seven Deadly Sins)
extern const VariantOption kEnvy;                // Mendoza: scenario_frangipani (Seven Deadly Sins)
extern const VariantOption kLust;                // Berlin: scenario_ambrosia (Seven Deadly Sins)
extern const VariantOption kSloth;               // Dartmoor: ancestral_harebell + outfits_skunk (Seven Deadly Sins)

inline constexpr size_t kVariantOptionCount = 70;

// Every option, grouped by map (Bangkok, Hokkaido, Sapienza, Paris, Haven,
// Chongqing, Colorado, Dartmoor, Berlin, Whittleton Creek, Mendoza, Isle of
// Sgail, Santa Fortuna, Marrakesh, Dubai, Miami, Mumbai), each map's in its chance
// walk's order. The settings tab shows them A-Z by label instead.
std::span<const VariantOption* const> AllVariantOptions();

// The option named name (exact, lower-case), or null.
const VariantOption* FindVariantOption(std::string_view name);

// A brick of any option (AllVariantOptions) by its runtime resource id, or null.
const VariantBrick* FindVariantBrick(uint64_t rid);

// The option only changes the lighting or time of day (its bricks are ToD and
// lighting ones: no NPCs, no story brick, no set dressing), so its crowd is
// the normal map's: the mover table uses the normal map's rows for it. Every
// option that is not check: tod_ebola_night, lighting_copperhead. Their
// censuses list the same actors in the same bricks as the other Sapienza ones.
bool IsLightingOnly(const VariantOption& option);

// IsLightingOnly of the option named name (FindVariantOption); false for an
// unknown name.
bool IsLightingOnlyVariant(std::string_view name);

// The option's chance setting: "variant_chance_<name>".
std::string VariantChanceKey(const VariantOption& option);

// Each option's chance in percent, indexed like AllVariantOptions().
using VariantChances = std::array<int32_t, kVariantOptionCount>;

// Every option's defaultChance.
VariantChances DefaultVariantChances();

// The option's chance in chances (0 for an option not in AllVariantOptions).
int32_t ChanceOf(const VariantChances& chances, const VariantOption& option);
void SetChance(VariantChances& chances, const VariantOption& option, int32_t percent);

// A Freelancer scene with official variants. options never lists "none".
struct VariantMap {
    std::string_view name;                     // "Bangkok mild"
    std::string_view scene;                    // the scene resource, as the contract names it
    std::vector<const VariantOption*> options; // in AllVariantOptions' order
};

// The scene's variant map (scene resource compared ASCII case-insensitively),
// or null.
const VariantMap* FindVariantMap(std::string_view scene);

// The options that share a map with option (option included), in
// AllVariantOptions' order: the mild scene's (the hot scene offers the same
// ones, less those with a full version). Empty for an option not in
// AllVariantOptions.
std::span<const VariantOption* const> VariantSiblings(const VariantOption& option);

// Whether option can come up in a showdown: its map's hot scene offers it.
bool OfferedInShowdowns(const VariantOption& option);

// The place option is offered on ("Sapienza", "Whittleton Creek": its map's
// name without the last word, mild or hot), or empty.
std::string_view VariantPlaceName(const VariantOption& option);

// The settings menu's edit: chances with edited set to percent (clamped to
// 0..100) and, when the chances of edited's map then add up to more than
// 100, its other options lowered so they add up to exactly 100 - percent:
// each in proportion to its current chance (rounded down, the rest going one
// point each to the largest remainders, ties to the earlier option). An
// other option is never raised, never below 0 and unchanged while the map
// fits. Options on other maps are unchanged.
VariantChances FitVariantChances(const VariantChances& chances, const VariantOption& edited, int32_t percent);

// "none" for null, else the option's name.
std::string_view VariantName(const VariantOption* option);

// "none, appraiser, collector, ...": every name variant_force accepts, "none"
// first, then A-Z.
std::string VariantForceNames();

// The roll walks [0, kVariantRollRange): 10000 steps of 0.01 %.
inline constexpr uint32_t kVariantRollRange = 10000;

// The seeded roll: DialHash(seed, scene, "variant", 0) % kVariantRollRange
// (scene hashed lower-cased).
uint32_t VariantRoll(int32_t seed, std::string_view scene);

// A map's chances as the roll walks them.
struct VariantWalk {
    std::vector<const VariantOption*> options; // the map's options, in order
    std::vector<int32_t> chances;              // each one's chance, in percent, clamped to 0..100
    std::vector<uint32_t> upTo;                // cumulative bounds: options[i] when upTo[i-1] <= roll < upTo[i]
    int32_t sum = 0;                           // of chances
    bool scaled = false;                       // sum > 100: bounds scaled so they end at kVariantRollRange
};

// The walk of map's options with chances. upTo[i] is the chances of options
// 0..i in 0.01 % steps (times 100), or, when they sum above 100, that times
// 100 / sum (rounded down), so the last bound is kVariantRollRange.
VariantWalk MakeVariantWalk(const VariantMap& map, const VariantChances& chances);

// The option roll lands on, or null ("none": past the last bound).
const VariantOption* WalkVariant(const VariantWalk& walk, uint32_t roll);

// An option's share of the walk in percent ("30", "16.67").
std::string WalkShareText(const VariantWalk& walk, size_t i);

// "the_source 30%, the_warlord 0%, none 70%" (with the scaled shares
// when scaled).
std::string DescribeWalk(const VariantWalk& walk);

// True when bricks lists path (ASCII case-insensitive).
bool HasBrick(std::span<const std::string> bricks, std::string_view path);

enum class VariantAction {
    Unchanged,      // the pick is "none"
    Add,            // append at least one of the option's bricks
    AlreadyPresent, // every brick of the option is already listed
};

// The variant_force setting: the trimmed, lower-cased option name (any case;
// see VariantForceNames), empty for an empty or blank value, or nothing for
// an unknown name.
std::optional<std::string> ParseVariantForce(std::string_view text);

// One brick of the picked option and what to do with it.
struct VariantBrickPlan {
    const VariantBrick* brick = nullptr;
    VariantAction action = VariantAction::Add; // Add or AlreadyPresent
};

struct VariantPlan {
    const VariantMap* map = nullptr;
    VariantWalk walk;                      // the map's chances
    uint32_t roll = 0;                     // VariantRoll
    const VariantOption* seeded = nullptr; // the walk's pick (null: none)
    const VariantOption* option = nullptr; // the applied option (seeded, or force's); null for "none"
    std::vector<VariantBrickPlan> bricks;  // the option's bricks, in order
    VariantAction action = VariantAction::Unchanged;
    bool forced = false;          // the option is force's, not the seeded one
    bool forceNotOffered = false; // force was set but this map has no such option (seeded pick used)
};

// The plan for a scene with variant map: the seeded pick (the roll walked
// over chances), and what to do with each of its bricks given the scene's
// current additional bricks (never Add for a listed one). A non-empty force
// (ParseVariantForce's result) that is "none" or one of the map's options
// replaces the seeded pick, whatever its chance; one this map does not have
// is ignored.
VariantPlan PlanVariant(const VariantMap& map, int32_t seed, std::string_view scene, const VariantChances& chances,
                        std::span<const std::string> bricks, std::string_view force = {});

// --- Full variants out of rotation (LandslideFull.h) ---

struct FullVariant;

// Whether a full variant can be picked this session: the DLL has its recipe,
// Full map variants is on, the game's files built it and its package is
// installed (LandslidePackage::Available).
using FullVariantAvailable = std::function<bool(const FullVariant&)>;

// The option plays only as its full version on map (FullVariantOf), and
// available says that version can't be picked.
bool VariantUnavailable(const VariantOption& option, const VariantMap& map, const FullVariantAvailable& available);

// chances with every option of map that is VariantUnavailable at 0, so the
// roll treats it as off: its share goes to the normal map, or to the other
// options when the rest still add up to more than 100.
VariantChances AvailableVariantChances(const VariantMap& map, const VariantChances& chances,
                                       const FullVariantAvailable& available);

// Checks that a brick's resource can load: true, or false with the reason.
using VariantBrickCheck = std::function<bool(const VariantBrick& brick, std::string& why)>;

struct VariantAppend {
    std::vector<const VariantBrick*> add; // the bricks to append, in order; empty when skipped
    const VariantBrick* failed = nullptr; // the first brick that did not resolve, or null
    std::string why;                      // its reason
};

// The bricks of an Add plan to append: every brick planned Add, each passing
// check (in order, stopping at the first failure). When one fails, the whole
// option is skipped: add is empty, and failed and why say which and why.
VariantAppend ResolveVariantAppend(const VariantPlan& plan, const VariantBrickCheck& check);

// --- Target exclusions (VariantOption::notTargets and removed, SealedActors) ---

// An NPC a map variant shuts in (src/core/SealedActors.inc, generated by
// tools/landslide/sealed_actors.py from the game's bricks and navmeshes): its
// start or an act of its route is where Freelancer's navmesh has floor and the
// variant's has none on its main island (the Garden Show closes Dartmoor's
// mansion, and its staff of Freelancer's own target pool stay inside). Never a
// target while the variant is loaded: 47 could not reach it.
struct SealedActor {
    std::string_view option; // VariantOption::name
    std::string_view repo;   // repository id ("xxxxxxxx-xxxx-...")
    std::string_view name;   // its actor name, for the log
};

// Every row of the generated table.
std::span<const SealedActor> SealedActors();

// The rows (of rows, by default the generated table) of every option in
// options whose bricks are all loaded, in rows' order.
std::vector<SealedActor> LoadedSealedActors(std::span<const uint64_t> loadedBrickRids,
                                            std::span<const VariantOption* const> options = AllVariantOptions(),
                                            std::span<const SealedActor> rows = SealedActors());

// The repository ids that must never be targets in a scene whose loaded
// bricks (runtime resource ids, as VariantBrick::rid) are loadedBrickRids:
// the notTargets and removed NPCs of every option in options whose bricks are
// all loaded, and the NPCs it seals in (LoadedSealedActors; malformed ids are
// skipped; a test checks the table has none). Empty for a scene without such an option (vanilla, or an option
// listing none).
std::set<Guid> VariantTargetExclusions(std::span<const uint64_t> loadedBrickRids,
                                       std::span<const VariantOption* const> options = AllVariantOptions());

// The repository ids the target pool swap must never use. Under the full
// Landslide (fullLandslide: the scene loaded is the swapped _scene_mamba),
// its never-targets (FullVariant::neverTargets, LandslideFull.h: Marco
// Abiatti), whatever bricks are listed, plus VariantTargetExclusions of every option but
// kLandslide: mission_mamba loads all of kLandslide's bricks, but its
// notTargets are the simple version's day crowd, which the full version does
// not load. Otherwise exactly VariantTargetExclusions (the simple Landslide:
// its stage sitters, which it removes).
std::set<Guid> TargetPoolExclusions(std::span<const uint64_t> loadedBrickRids, bool fullLandslide,
                                    std::span<const VariantOption* const> options = AllVariantOptions());

// The same under any full variant (LandslideFull.h; null: none, exactly
// VariantTargetExclusions): its never-targets (the full Author's: Craig
// Black) plus VariantTargetExclusions of every option but the one it is the
// full version of. TargetPoolExclusions(rids, true) is this with the full
// Landslide.
std::set<Guid> TargetPoolExclusions(std::span<const uint64_t> loadedBrickRids, const FullVariant* full,
                                    std::span<const VariantOption* const> options = AllVariantOptions());

// --- Main story targets (StoryTargets.inc) ---

// A target of the main story mission at a Freelancer location who is an NPC
// of its Freelancer level (the Washington twins at Sgail, Alexa Carlisle at
// Dartmoor, ...): never a Freelancer target there, shuffle on or off, map
// variant or not. The game's own pools never hold one; the target pool swap
// could draw one from the mover table (Sophia Washington at Sgail).
struct StoryTarget {
    std::string_view scene; // the mild scene ("assembly:/_pro/scenes/missions/...", any case)
    std::string_view repo;  // repository id ("xxxxxxxx-xxxx-...")
    std::string_view name;  // its actor name, for the log
};

// Every row of the table.
std::span<const StoryTarget> StoryTargets();

// The rows (of rows, by default the table) of scene (compared in any case:
// the mild scene as fr::SceneOf gives it, a full variant counting as its
// contract's), in rows' order. Empty for any other scene.
std::vector<StoryTarget> SceneStoryTargets(std::string_view scene, std::span<const StoryTarget> rows = StoryTargets());

// Their repository ids (malformed ones skipped).
std::set<Guid> StoryTargetExclusions(std::string_view scene, std::span<const StoryTarget> rows = StoryTargets());

// What the target pool swap rules out in scene (fr::SceneOf: the mild scene):
// TargetPoolExclusions(loadedBrickRids, full, options) (a loaded map
// variant's own targets, the NPCs it strands or shuts in, a full variant's
// never-targets) plus StoryTargetExclusions(scene), always.
std::set<Guid> TargetPoolExclusions(std::string_view scene, std::span<const uint64_t> loadedBrickRids,
                                    const FullVariant* full,
                                    std::span<const VariantOption* const> options = AllVariantOptions());

// The names of the options whose bricks are all loaded (runtime resource
// ids, as VariantBrick::rid), most specific first (more bricks first, then
// in options' order): the map variants the scene has, for the mover table's
// variant rows (MoverTable.h). An option whose bricks all belong to another
// loaded option is left out (the_bad_boy's are lighting_copperhead's: once). Under the
// full Landslide (fullLandslide), kLandslide is left out: mission_mamba loads
// all its bricks, but the scene loaded is its own (_scene_mamba's rows).
std::vector<std::string_view> LoadedVariants(std::span<const uint64_t> loadedBrickRids, bool fullLandslide,
                                             std::span<const VariantOption* const> options = AllVariantOptions());

// The same under any full variant (null: none): its option is left out unless
// its scene shares the contract scene's crowd (the full Author: The Author's
// lighting, tod_ebola_night, is loaded and lighting only, so the normal rows
// apply as for the simple Author). LoadedVariants(rids, true) is this with the
// full Landslide.
std::vector<std::string_view> LoadedVariants(std::span<const uint64_t> loadedBrickRids, const FullVariant* full,
                                             std::span<const VariantOption* const> options = AllVariantOptions());

// The mover table's variant tags for a load (ChooseMoverTable, MoverTable.h),
// most specific first: under a full variant that is a layer on another
// (FullVariant::layerOf: the full Bad Boy on the full Icon, the full
// Entertainer on the full Marrakesh night), its key ("badboy",
// "entertainer"), then LoadedVariants. The layer's scene is its base's with
// the layer's people added, so its censuses are filed under its key, apart
// from the base's own rows (the scene's untagged rows stay the base's); with
// no rows of its own it falls back to the base's. No option is named like a
// full variant's key, so the tag is never taken for a lighting-only option.
std::vector<std::string_view> MoverTableVariants(std::span<const uint64_t> loadedBrickRids, const FullVariant* full,
                                                 std::span<const VariantOption* const> options = AllVariantOptions());

// --- Variant check (heavy options: VariantOption::check) ---

// Seconds of play before the second check (enabled and alive bits).
constexpr double kVariantCheckPlaySeconds = 10.0;

// One target-pool actor (or the supplier) as the check reads it.
struct VariantCheckActor {
    bool present = false;   // the element has an entity with a ZActor
    bool readable = false;  // its bits were read
    bool dedicated = false; // in the mission brick: a Freelancer target, start disabled until picked
    bool startEnabled = false;
    bool enabled = false;
    bool alive = false;
};

// What is wrong with the actor, or nothing. Mission-brick actors (dedicated)
// are never a problem: the targets the mission did not pick stay disabled,
// and may read as dead. Scene NPCs (the ones a variant can change): at load
// (playing false) missing, unreadable or start disabled; in play also not
// enabled or dead.
std::optional<std::string> VariantCheckProblem(const VariantCheckActor& actor, bool playing);

// "24 pool actors: 16 in the mission brick (not checked), 8 scene NPCs: 2 problems".
std::string VariantCheckSummary(std::span<const VariantCheckActor> actors, bool playing);

} // namespace fr
