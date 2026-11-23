#pragma once

// The full map variants' safety net (pure logic; src/mod/LandslidePackage.h has
// the engine parts): the full Landslide, the full Author and the full Icon
// (FullVariant below), whose bricks share Sapienza's package, and the full
// Marrakesh night, in Marrakesh's own package (partition 25), with the night
// level's scene (_scene_python): no option of its own (the plain
// marrakesh_night option was removed), only the base of the full Entertainer
// below. What follows says Landslide; the full Author
// works the same way on a pick of The Author (tod_ebola_night), with its own
// scene (scene_ebola), its own crash sentinel and its own checks of its own
// resources; the full Icon likewise on a pick of The Icon
// (lighting_copperhead), with The Icon's scene (_scene_copperhead, a set-only
// map). The full Bad Boy (on a pick of the_bad_boy) is a layer on the full
// Icon: the same scene and package bricks with the vanilla mission_caipirinha
// (The Bad Boy's elusive target mission) appended, its own checks and
// sentinel. The full Patient Zero (on a pick of patient_zero) loads the
// outbreak's own scene (_scene_flu: the normal hotel plus mission_flu, the
// outbreak's people) with Hokkaido's package (partition 22). The full
// Entertainer (on a pick of the_entertainer) is a layer on the full Marrakesh
// night, as the full Bad Boy is on the full Icon: the vanilla
// mission_marrakesh_maitai (The Entertainer's elusive target mission)
// appended. The full Gluttony (on a pick of gluttony, Chongqing) swaps no
// scene (appends): the contract's scene and bricks stay, and the swap appends
// the vanilla scenario_azalea (the Seven Deadly Sins set) and then the
// package's fix brick, fr_gluttony (Chongqing's package, partition 6), which
// deletes the props of the two that would stand in each other (an old truck
// under one of the set's tents, a column of the set's crates on one of
// Freelancer's safe clues; tools/landslide/gluttony_spec.py). Ten more
// options append a fix brick of the package the same way (spot fixes,
// tools/landslide/spotfix_spec.py): The Revolutionary and Embrace of the
// Serpent (Santa Fortuna, partition 18), The Reflection (Hokkaido, 22),
// Garden Show and Sloth (Dartmoor, 3), Illusions of Grandeur (Mumbai, 17), The
// Undying and The Undying Returns (Miami, 19), The Banker (Paris, 27) and A
// Bitter Pill (Whittleton Creek, 16). Each
// fix brick holds property overrides on Freelancer's override brick that move a
// territory spot the option crowds or covers, or point a territory slot whose
// spot the option seals off at a clean spot of the same kind.
//
// There is no simple (lighting-only) version: these options are offered on
// their mild map only (Variants.cpp), and a pick always loads the full
// version. On a Landslide pick, the mod loads Landslide's own scene
// (_scene_mamba, whose content is mission_mamba) and replaces the contract's
// brick list with the bricks of its own package
// (Runtime/chunk26patch290.rpkg). Before it swaps, each check below must
// pass; any failure plays the normal map instead, with one log line and an
// overlay note.
//
// The checks, in order: the gate (the DLL has its recipe, Full map variants
// is on, the player's game files built it, the full version not turned off by
// the crash sentinel, the package installed this session, Sapienza's partition
// raised to mount it, and, while the package has no Freelancer layer, a forced
// Landslide); the contract's brick list is exactly
// mission_mild_bingo; the package's own resources resolve to its file; every
// resource its reference tables name is in the resource container; and the
// fingerprints: the vanilla resources it was built against still have their
// build-time sizes (a game update re-emits a brick with another size) and are
// not replaced by another mod's package.
//
// The crash sentinel is a small file written just before a swapped load and
// removed when that load reaches play. Found at game start, the last swapped
// load never finished (crash, hang or quit while loading): the full version
// stays off (its picks play the normal map) until the player tries it again
// (Settings) or the mod's build changes.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Mods' patch packages are numbered from here (this mod's 290; another mod's
// may be 290 or 300); IOI's are 1 to a few.
inline constexpr int kFirstModPatch = 100;

// Landslide's own scene, which the swap loads instead of the contract's.
inline constexpr std::string_view kLandslideScene = "assembly:/_pro/scenes/missions/coastaltown/_scene_mamba.entity";
// The Author's own scene (scenario_ebola), which the full Author's swap loads.
inline constexpr std::string_view kAuthorScene = "assembly:/_pro/scenes/missions/coastaltown/scene_ebola.entity";
// The Icon's own scene (mission_copperhead: the movie set), which the full Icon's swap loads.
inline constexpr std::string_view kIconScene = "assembly:/_pro/scenes/missions/coastaltown/_scene_copperhead.entity";
// The brick list the swap expects of the mild Sapienza contract (any case); a
// hard or easy contract adds one of its difficulty bricks (Peacock
// SNUG/MILD/BINGO.json, the hash list).
inline constexpr std::string_view kLandslideContractBrick =
    "assembly:/_pro/scenes/missions/coastaltown/mission_mild_bingo.brick";
inline constexpr std::string_view kSapienzaEasyBrick =
    "assembly:/_pro/scenes/missions/coastaltown/difficulty_easy_sapienza_octopus.brick";
inline constexpr std::string_view kSapienzaHardBrick = "assembly:/_pro/scenes/missions/coastaltown/octopus_pro1.brick";
// Marrakesh's story-free night level (mission_marrakesh_python: A House Built
// on Sand's base, Contracts mode's night), which the full Marrakesh night loads.
inline constexpr std::string_view kMarrakeshScene = "assembly:/_pro/scenes/missions/marrakesh/_scene_python.entity";
// The mild Marrakesh contract's mission brick; an easy or hard contract adds
// one of kMarrakeshDifficultyBricks (Peacock SNUG/MILD/MANCALA.json).
inline constexpr std::string_view kMarrakeshContractBrick =
    "assembly:/_pro/scenes/missions/marrakesh/mission_mild_mancala.brick";
inline constexpr std::string_view kMarrakeshEasyBrick =
    "assembly:/_pro/scenes/missions/marrakesh/difficulty_easy_marrakesh_spider.brick";
inline constexpr std::string_view kMarrakeshHardBrick =
    "assembly:/_pro/scenes/missions/marrakesh/difficulty_pro1_marrakesh_spider.brick";
// Patient Zero's scene (the normal hotel's bricks plus mission_flu: the
// outbreak's people, its navmesh and its story; Contracts mode's "flu" level
// too), which the full Patient Zero loads.
inline constexpr std::string_view kPatientZeroScene = "assembly:/_pro/scenes/missions/hokkaido/_scene_flu.entity";
// The mild Hokkaido contract's mission brick; an easy or hard contract adds
// one of the two difficulty bricks (Peacock SNUG/MILD/SHOGI.json, the logs).
inline constexpr std::string_view kHokkaidoContractBrick =
    "assembly:/_pro/scenes/missions/hokkaido/mission_mild_shogi.brick";
inline constexpr std::string_view kHokkaidoEasyBrick =
    "assembly:/_pro/scenes/missions/hokkaido/difficulty_easy_hokkaido_snowcrane.brick";
inline constexpr std::string_view kHokkaidoHardBrick =
    "assembly:/_pro/scenes/missions/hokkaido/difficulty_pro1_hokkaido_snowcrane.brick";
// Mild Chongqing's scene and mission brick: the full Gluttony keeps both (it
// appends; the showdown scene, scene_wet_hot_mahjong, likewise).
inline constexpr std::string_view kChongqingMildScene =
    "assembly:/_pro/scenes/missions/wet/scene_wet_mild_mahjong.entity";
inline constexpr std::string_view kChongqingContractBrick =
    "assembly:/_pro/scenes/missions/wet/mission_mild_mahjong.brick";
// The mild scenes and mission bricks of the maps whose spot fixes append
// (tools/landslide/spotfix_spec.py): each keeps both, as the full Gluttony.
inline constexpr std::string_view kSantaFortunaMildScene =
    "assembly:/_pro/scenes/missions/colombia/scene_hippo_mild_trompo.entity";
inline constexpr std::string_view kSantaFortunaContractBrick =
    "assembly:/_pro/scenes/missions/colombia/mission_mild_trompo.brick";
inline constexpr std::string_view kHokkaidoMildScene =
    "assembly:/_pro/scenes/missions/hokkaido/scene_snowcrane_mild_shogi.entity";
inline constexpr std::string_view kDartmoorMildScene =
    "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity";
inline constexpr std::string_view kDartmoorContractBrick =
    "assembly:/_pro/scenes/missions/ancestral/mission_mild_bridge.brick";
inline constexpr std::string_view kMumbaiMildScene =
    "assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.entity";
inline constexpr std::string_view kMumbaiContractBrick =
    "assembly:/_pro/scenes/missions/mumbai/mission_mild_ludo.brick";
inline constexpr std::string_view kMiamiMildScene =
    "assembly:/_pro/scenes/missions/miami/scene_flamingo_mild_pinochle.entity";
inline constexpr std::string_view kMiamiContractBrick =
    "assembly:/_pro/scenes/missions/miami/mission_mild_pinochle.brick";
inline constexpr std::string_view kParisMildScene =
    "assembly:/_pro/scenes/missions/paris/scene_peacock_mild_craps.entity";
inline constexpr std::string_view kParisContractBrick = "assembly:/_pro/scenes/missions/paris/mission_mild_craps.brick";
inline constexpr std::string_view kWhittletonMildScene =
    "assembly:/_pro/scenes/missions/skunk/scene_skunk_mild_solitaire.entity";
inline constexpr std::string_view kWhittletonContractBrick =
    "assembly:/_pro/scenes/missions/skunk/mission_mild_solitaire.brick";

// One vanilla resource the package was built against (generated table): its
// newest copy's final (decompressed) size and package at build time.
struct LandslideFingerprint {
    uint64_t rid = 0;
    uint32_t size = 0;
    std::string_view package; // "chunk26patch4.rpkg", for the log
    std::string_view name;    // "mission_mamba.brick"
};

// What the resource container holds for one resource: its newest copy.
struct ResourceSeen {
    uint32_t fault = 0; // the read's exception code, or 0
    bool found = false;
    uint32_t size = 0;   // final size
    std::string package; // the mounted package's name ("../runtime//chunk26patch4.rpkg")
};

using ResourceLookup = std::function<ResourceSeen(uint64_t)>;

// The file name of a mounted package's name ("../runtime//chunk26patch4.rpkg"
// -> "chunk26patch4.rpkg"; either slash).
std::string_view PackageFileName(std::string_view p_Package);

// A package's patch number, any folder and case: chunk26patch4.rpkg -> 4,
// chunk26.rpkg -> 0; empty for any other name.
std::optional<int> PackagePatchNumber(std::string_view p_Package);

// Why the full version is not used (None: it is).
enum class LandslideBlock : uint8_t {
    None,
    NotBuiltIn,   // the DLL has no recipe for it
    SwitchedOff,  // Full map variants is off (full_map_variants)
    NotBuildable, // the player's game files can't build it (a source changed or is missing)
    TurnedOff,    // the crash sentinel: the last swapped load did not finish
    NotInstalled, // the package file was not written or kept this session
    NotRaised,    // Sapienza's partition was not raised to mount it
    TestOnly,     // the package has no Freelancer layer and Landslide was not forced
    Contract,     // the contract's brick list is not mission_mild_bingo
    NotMounted,   // chunk26 is not mounted
    ReadFault,    // reading the resource container faulted
    OwnResource,  // one of the package's resources does not resolve to its file
    Dependency,   // a resource the package references is missing
    GameChanged,  // a fingerprint's size changed (a game update)
    OtherMod,     // a fingerprinted resource comes from another mod's package
};

struct LandslideVerdict {
    LandslideBlock block = LandslideBlock::None;
    std::string why; // for the log ("a game update changed ..."), empty when None
};

// The cheap checks, made before anything is read from the engine.
struct LandslideGate {
    bool builtIn = false;         // the DLL has its recipe
    bool switchedOff = false;     // Full map variants is off
    bool buildable = false;       // its resources and its base's were built from the game's files this session
    bool turnedOff = false;       // the crash sentinel tripped (and was not re-armed)
    bool installed = false;       // the package file is in place this session
    bool partitionSeen = false;   // the partition hook saw partition 26
    int32_t raisedTo = -1;        // the patch level partition 26 was created with
    int target = 0;               // the package's patch number (290)
    bool maxPatchLevel = false;   // the SDK's MaxPatchLevel mod raises every partition
    bool freelancerLayer = false; // the package carries targets, supplier and exits
    bool forced = false;          // Landslide was forced (variant_force), not rolled
};

// Each of p_Own resolves to a package whose file name is p_OwnFile.
LandslideVerdict CheckOwnResources(std::span<const uint64_t> p_Own, const ResourceLookup& p_Lookup,
                                   std::string_view p_OwnFile);

// Every one of p_Dependencies is in the resource container.
LandslideVerdict CheckDependencies(std::span<const uint64_t> p_Dependencies, const ResourceLookup& p_Lookup);

// Each fingerprint's newest copy has its build-time size and comes from the
// game's own packages (chunkN.rpkg, chunkNpatchK with K < 100: the mods'
// patches are numbered 100 and up) or p_OwnFile. Another IOI package with the
// same size passes (a patch that did not change it). *p_Summary (when given)
// gets one entry per fingerprint for the log: "mission_mamba.brick 3437671
// chunk26patch4.rpkg", with the build-time package when it differs.
LandslideVerdict CheckFingerprints(std::span<const LandslideFingerprint> p_Fingerprints, const ResourceLookup& p_Lookup,
                                   std::string_view p_OwnFile, std::string* p_Summary = nullptr);

// The build the sentinel names: the mod's version and a hash of the package
// it writes ("0.1.0+1a2b3c4d"), so a new package gets a fresh try.
std::string LandslideBuildId(std::string_view p_Version, std::span<const uint8_t> p_Package);

// The sentinel file's text: one "key value" line each for the build, the time
// it was written and the scene being loaded.
std::string LandslideSentinelText(std::string_view p_Build, std::string_view p_Time, std::string_view p_Scene);

// The value of the sentinel's "build" line, or empty.
std::string LandslideSentinelBuild(std::string_view p_Text);

enum class SentinelAction : uint8_t {
    None,    // no sentinel: the full version may load
    TurnOff, // the last swapped load of this build did not finish: off until re-armed
    Discard, // another build's sentinel: removed, the full version may load
};

// At game start: p_Text is the sentinel's content (null: no sentinel;
// p_Readable false: it exists but could not be read, which turns off too).
SentinelAction JudgeSentinel(const std::string* p_Text, bool p_Readable, std::string_view p_Build);

// --- Never targets under the full Landslide ---

// One of Landslide's own characters that the target pool swap never swaps in
// (or keeps in the pool) under the full Landslide: Marco Abiatti, Landslide's
// own target, whose story keeps running there. The rest of its cast (Abiatti's
// entourage, the band, the stage crew and its guards) may be targets: their
// "fail" states in mission_mamba (Lawyer_Failed, Priest_Failed, ...) only close
// Landslide's own opportunities, never the mission. Measured in game in the
// full Landslide, and from mission_mamba's entity tree.
struct LandslideNeverTarget {
    std::string_view name;
    std::string_view repo; // repository id
    uint64_t entityId;     // its sub-entity id in mission_mamba
};

// --- The full map variants ---

// A map variant the mod can load as its official scene, with its own copies
// of Freelancer's bricks (one package for all of them, chunk26patch290).
struct FullVariant {
    std::string_view key;      // "landslide": the package's table, the log prefix, the crash sentinel's file
    std::string_view display;  // "Landslide": the overlay and Settings
    std::string_view option;   // the VariantOption (Variants.h) it is the full version of: "landslide"; empty for
                               // the full Marrakesh night, only a base (the full Entertainer's), never picked
    std::string_view map;      // the VariantMap it applies to: "Sapienza mild"
    std::string_view scene;    // the scene its swap loads
    std::string_view aLoad;    // "a Landslide load", for the log
    std::string_view fullName; // "the full Landslide": log lines and the scene alias
    std::string_view offNote;  // Settings, when the crash sentinel turned it off
    std::span<const LandslideNeverTarget> neverTargets; // never targets under it (fr::TargetPoolExclusions)
    // Its scene keeps the contract scene's crowd bricks (The Author keeps mild
    // Sapienza's day crowd), so the contract scene's mover table rows apply
    // until it has rows of its own (fr::MoverTableScene).
    bool sharesCrowd = false;
    // Its package's partition: the map's chunk, whose patch level the mod
    // raises to mount Runtime/chunk<partition>patch290.rpkg (26 "coastaltown":
    // Sapienza; 25 "marrakesh"; 22 "hokkaido").
    int partition = 26;
    std::string_view partitionId = "coastaltown";
    std::string_view location = "Sapienza"; // the map, in messages
    // The contract's brick list: its mission brick, plus at most one of the
    // droppable bricks (a difficulty's, which belongs to the day level), which
    // the swap leaves out (CheckFullContract).
    std::string_view contractBrick = kLandslideContractBrick;
    std::span<const std::string_view> droppable = {};
    // A layer on another full variant (its key: the full Bad Boy on "icon",
    // the full Entertainer on "marrakesh"):
    // the same scene and package bricks with vanilla bricks appended. The
    // scene alias names the layer's own key (SceneAlias::fullKey), since the
    // two share a scene. Empty for the others.
    std::string_view layerOf = {};
    // Why the swap leaves a droppable brick out, for the log (DroppedBrickNote).
    std::string_view droppedWhy = "it belongs to the day level";
    // It swaps no scene (the full Gluttony): the contract's scene and bricks
    // stay (no contract check, nothing dropped), and the option's bricks are
    // appended as for any variant, then the package's own (its table's swap
    // bricks that are not the option's). `scene` names the mild scene, for its
    // tables only; FullVariantOfScene never answers with it.
    bool appends = false;
    // A second map it applies to (an appending variant: the full Gluttony on
    // Chongqing's showdown too). Empty for the others.
    std::string_view alsoMap = {};
};

const FullVariant& LandslideFull();

// How many full variants there are (FullVariants().size()).
inline constexpr size_t kFullVariantCount = 18;

// Every full variant, in the package's order.
std::span<const FullVariant* const> FullVariants();

// The full version of a picked option (VariantOption::name) on a map
// (VariantMap::name: its map or alsoMap), or null.
const FullVariant* FullVariantOf(std::string_view p_Option, std::string_view p_Map);

// By key ("author"), exactly; null for none.
const FullVariant* FullVariantOfKey(std::string_view p_Key);

// By the scene its swap loads (any case); null for none. Layers (layerOf)
// share their base's scene and are left out: the base answers. Appending
// variants swap in no scene and are left out too.
const FullVariant* FullVariantOfScene(std::string_view p_Scene);

// The full variant of a swapped load: the one the alias's fullKey names when
// its scene is p_Scene (any case) and it swaps one in (not appends), else
// FullVariantOfScene(p_Scene).
const FullVariant* FullVariantOfLoad(std::string_view p_FullKey, std::string_view p_Scene);

// Why a full variant can't be picked this session (out of rotation, grayed
// out in Settings), or Available.
enum class FullAvailability : uint8_t {
    Available,
    SwitchedOff,  // Full map variants is off
    NotBuildable, // no recipe in this DLL, or the game's files don't match it (the game's version)
    NotInstalled, // its package wasn't installed: another mod's file or a file error (the log says which)
};

struct FullAvailabilityInputs {
    bool builtIn = false;     // the DLL has its recipe
    bool switchedOff = false; // Full map variants is off
    bool buildable = false;   // Install built it or found it in the cache
    bool readFailed = false;  // not buildable only because a game file could not be read
    bool installed = false;   // its partition's package is in place
};

FullAvailability JudgeFullAvailability(const FullAvailabilityInputs& p_In);

using FullAvailabilityOf = std::function<FullAvailability(const FullVariant&)>;

// The first unavailable full version of option p_Option (VariantOption::name),
// in FullVariants() order; Available when none is.
FullAvailability FullOptionAvailability(std::string_view p_Option, const FullAvailabilityOf& p_Of);

// The cheap checks (LandslideGate) for p_Variant: its sentinel, its forced
// option and its partition in the reasons.
LandslideVerdict CheckFullGate(const FullVariant& p_Variant, const LandslideGate& p_Gate);

// The contract check for p_Variant: the brick
// list is its contractBrick (any case) plus at most one of its droppable
// bricks, which the swap leaves out (dropped: as the contract names them).
struct ContractVerdict {
    LandslideVerdict verdict;
    std::vector<std::string> dropped;
};
ContractVerdict CheckFullContract(const FullVariant& p_Variant, std::span<const std::string> p_Bricks);

// The log line for a brick the swap leaves out ("marrakesh: full version:
// ..."'s tail): "the contract's hard-difficulty brick
// (difficulty_pro1_marrakesh_spider) is left out: it belongs to the day level"
// (p_Why: the variant's droppedWhy).
std::string DroppedBrickNote(std::string_view p_Brick, std::string_view p_Why = "it belongs to the day level");

// The log prefix of p_Variant's package lines: "landslide package" for
// Sapienza's (partition 26), "<partition id> package" for the others
// ("marrakesh package").
std::string PackageLogName(int p_Partition, std::string_view p_PartitionId);

// A partition's package file name: "chunk26patch290.rpkg".
std::string FullPackageFileName(int p_Partition, int p_Patch);

// The overlay text for p_Variant: its display name when it is used (None),
// else "normal map (...)" saying why not ("normal map (Landslide could not be
// loaded)"): there is no simple version to fall back on.
std::string FullOverlay(const FullVariant& p_Variant, LandslideBlock p_Block);

// The crash sentinel's file name, next to the DLL:
// "freelancerrandomizer-<key>-loading.txt".
std::string FullSentinelFileName(const FullVariant& p_Variant);

// Its never-targets' repository ids.
std::vector<std::string_view> NeverTargetRepos(const FullVariant& p_Variant);

// Settings' note for a full variant the crash sentinel turned off.
std::string_view FullTurnedOffNote(const FullVariant& p_Variant);

} // namespace fr
