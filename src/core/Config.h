#pragma once

#include "ClientReqs.h"
#include "CrateGunMath.h"
#include "CrateItems.h"
#include "Dials.h"
#include "Footsteps.h"
#include "LandslideFull.h"
#include "LootAmounts.h"
#include "PayoutScale.h"
#include "Variants.h"
#include "WeaponRarity.h"

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Shares and chances (target_pool_swap_min, crate_gun_chance, ...) are
// fractions here; the ini holds them in percent (fr::PercentToFraction).
struct Config {
    bool enabled = true;
    bool dials = true;
    // Random weapons from their own kits for eligible guards in Freelancer
    // missions (guard_weapons). Independent of Mode.
    bool guardWeapons = true;
    // Official map variant bricks in Freelancer missions, independent of Mode.
    bool variants = true;
    // The full map variants (core/LandslideFull.h): their packages are built
    // from the game's files at game start (full_map_variants). A pick always
    // loads the full version (the normal map when its checks fail). Off: the
    // mod removes its packages and never picks them; one its game files can't
    // build is never picked either (fr::AvailableVariantChances).
    bool fullMapVariants = true;
    // variant_chance_<option>: the percent of a map's missions that get each
    // option (fr::VariantChanceKey; the rest keep the normal map).
    VariantChances variantChances = DefaultVariantChances();
    // An option name ("none", "the_source", "patient_zero", ...;
    // ParseVariantForce) that replaces the seeded pick where it is offered,
    // whatever its chance. Empty: no override.
    std::string variantForce;
    // Random per-objective payout scale in Freelancer scenes (missions and
    // the safehouse): factor per entry from the campaign seed and entry id
    // (payout_lean, slightly down by default), amounts shown rounded to 50 and
    // at least 500, cheap normal entries scaled from 1,000, and a seeded
    // jackpot on some normal entries (PayoutScale.h).
    bool payouts = true;
    // payout_sigma 0.5 and payout_lean -0.1; also what invalid ini values fall
    // back to.
    PayoutScaleConfig payoutScale {0.5, 0.5, 2.5, -0.1};
    // log_safe_spots, log only: where each safe can
    // appear in a Freelancer mission, which one the game picked, and a map
    // variant's entities near each (SafeSpots.h).
    bool logSafeSpots = true;
    // Loot amounts: in Freelancer missions, each money item (safe, supply
    // crate merces, money mule, New York's bank vault) gets its own seeded
    // amount (LootAmounts.h). Independent of Mode.
    bool lootAmounts = true;
    // Each kind's least and most amount (loot_amount_<kind>_min / _max), by
    // MoneyKind: safe, crate, mule, vault (fr::DefaultLootAmountBounds).
    std::array<LootAmountBounds, kMoneyKinds> lootBounds {{{1500, 10000}, {250, 5000}, {250, 3500}, {4000, 25000}}};
    // Crate items (crate_items, crate_all_tools_chance, crate_merces_chance, crate_items_unloaded,
    // crate_even_boxes, crate_level_items): each supply crate's contents planned by the mod: a gun
    // crate, a tool crate, a merces crate or a regular crate of the map's
    // items (with crate_level_items also the items lying around the level),
    // large items alone (CrateItems.h).
    CrateItemsConfig crateItems;
    // Crate guns: with crate_items, whether and how often a crate holds one
    // unowned gun, alone (crate_guns, crate_gun_chance, the rarity multipliers);
    // without, a seeded roll per opened supply crate steers a hit crate to the
    // weapon pool with weighted firearms (CrateGunMath.h).
    CrateGunsConfig crateGuns;
    // Stock steering (StockSteerMath.h), weights only, never an owned gun:
    // the supplier's eligible guns equally likely within their slot. Runs the
    // supplier walk in missions (SupplierWalk.h).
    bool supplierSteer = true;
    // With supplierSteer: the supplier's non-gun slots too (one family of the
    // options in no selector, StockSteerMath.h).
    bool supplierSteerGear = true;
    // Supplier force pick (SupplierForcePick.h): a gun selector that stays
    // silent at a visit although it has an eligible gun gets its Pick
    // signaled by the mod once the other slots have rolled. Runs the
    // supplier walk too.
    bool supplierForcePick = true;
    // Target pool shuffle (target_pool): in mild missions, replace a seeded
    // share of the target pool with other always-present scene actors
    // (TargetPool.h). Independent of Mode.
    bool targetPool = true;
    double targetPoolSwapMin = 0.35; // fr::kDefaultTargetPoolSwapMin: each mission load draws its share in [min, max]
    double targetPoolSwapMax = 1.0;  // fr::kDefaultTargetPoolSwapMax
    bool targetPoolAnyGuard = true;  // fr::kDefaultTargetPoolAnyGuard: any guard may join (second tier)
    // Enforcer shuffle (enforcers): in missions and showdowns, move a drawn
    // share of the enforced-outfit lists 1:1 to other actors of the same type
    // (Enforcers.h). Independent of Mode.
    bool enforcers = true;
    double enforcersShareMin = 0.55;      // fr::kDefaultEnforcerShareMin: the share is drawn in [min, max]
    double enforcersShareMax = 0.9;       // fr::kDefaultEnforcerShareMax
    bool enforcersAnyBrick = true;        // fr::kDefaultEnforcerAnyBrick: lists may move to another brick
    bool enforcersIncludeTargets = true;  // fr::kDefaultEnforcerIncludeTargets: possible targets take part
    bool enforcersIncludeLate = true;     // fr::kDefaultEnforcerIncludeLate: start-disabled actors take part
    bool enforcersIncludeSentries = true; // fr::kDefaultEnforcerIncludeSentries: sentries take part
    // Door lock shuffle (door_locks): in missions and showdowns, a drawn
    // share of the locked lockpickable doors unlock and as many unlocked ones
    // lock (DoorLocks.h).
    bool doorLocks = true;
    double doorLocksShareMin = 0.5; // fr::kDefaultDoorLockShareMin: the share is drawn in [min, max]
    double doorLocksShareMax = 1.0; // fr::kDefaultDoorLockShareMax
    // Lockpick grant (LockpickFree.h): in missions, 47 gets a lockpick when he
    // brings none. slow_lockpick: a Tool_Lockpick; with only it, a lock takes
    // lockpickSlowPicks times as long to open. nails:
    // lockpickNails Rusty Old Nails (one pick each). Never saved.
    std::string lockpickGrant = "nails"; // lockpick_grant: off, slow_lockpick or nails (fr::ParseLockpickGrantMode)
    int32_t lockpickNails = 5;           // lockpick_nails: fr::kDefaultLockpickNails, clamped to 1-20
    int32_t lockpickSlowPicks = 2;       // lockpick_slow_picks: fr::kDefaultLockpickSlowPicks, clamped to 1-4
    // Client requirements shuffle: when a campaign is generated, each mild
    // location's optional client requirements are replaced by seeded picks
    // (ClientReqs.h). On by default; the game's picks and the planned ones are
    // logged either way.
    bool clientReqs = true;
    std::set<int32_t> clientReqsExclude {3, 4, 18}; // never picked (client_reqs_exclude, fr::kDefaultClientReqExclude)
    ClientReqMode clientReqsMode = kDefaultClientReqMode;   // client_reqs_mode: campaign pool or per mission
    int32_t clientReqsPoolSize = kDefaultClientReqPoolSize; // client_reqs_pool_size: 0 or less is the default
    // Weapon rarity shuffle: firearms' Evergreen_Rarity permuted per campaign
    // (WeaponRarity.h). On by default.
    bool weaponRarity = true;
    double weaponRarityKeepMin = kDefaultWeaponRarityKeepMin; // weapon_rarity_keep_min
    double weaponRarityKeepMax = kDefaultWeaponRarityKeepMax; // weapon_rarity_keep_max
    bool weaponRarityCapacity = true; // weapon_rarity_capacity: capacity cost follows the rarity change
    bool concealableMace =
        true; // concealable_mace: the mace is pocketable, with or without weapon_rarity (ConcealableMace.h)
    bool legalSniperRifles =
        true; // legal_sniper_rifles: a visible sniper rifle is legal where firearms are (LegalSniper.h)
    bool randomLoadout = true;                 // random_loadout: fill the free gear slots at mission start
    double randomLoadoutDelay = 1.0;           // random_loadout_delay: game seconds after the packed items
    bool randomLoadoutHolsterableOnly = false; // random_loadout_holsterable_only: never an item 47 can't holster
        // (one that comes in a briefcase); was random_loadout_briefcase, inverted
    bool randomLoadoutLockpickFirst = true; // random_loadout_lockpick_first: a lockpick is planned before anything else
    bool randomLoadoutObjectiveItems = true; // random_loadout_objective_items: plan what the objectives need
    // Item grants (ItemGrants.h): in missions, 47 gets the item when he carries
    // none of its kind; no gear slot, never saved. Independent of random_loadout.
    bool grantMakeshiftExplosive = true; // grant_makeshift_explosive: a Makeshift Explosive
    bool grantLethalPoison = true;       // grant_lethal_poison: a Lethal Poison Vial
    bool grantCoin = true;               // grant_coin: a Classic Coin
    // Safehouse coin: the bird's nest coin always spawns (SafehouseCoin.h). On by
    // default.
    bool safehouseCoin = true;
    // Fishing script: the safehouse's first three catches are a fish, the
    // fishing line and the pirate saber, weights only (FishingScript.h). On by
    // default.
    bool fishingScript = true;
    // Investigations (Investigations.h): footsteps a guard hears draw a real
    // investigation (the game's investigate-here helper at 47) with
    // footstepsInvestigateChance percent per hearing (kDefaultFootstepInvestigateChance).
    bool footstepsInvestigate = true;
    int footstepsInvestigateChance = 70;
    // Backup guard (Investigations.h): a guard taken down while he checks a
    // noise distraction he radioed in is followed by another guard, sent
    // backupGuardDelayMin..Max s after his call ended (rolled per backup);
    // backupGuardNotice shows "Backup is on the way".
    bool backupGuard = true;
    int backupGuardDelayMin = 20;
    int backupGuardDelayMax = 45;
    bool backupGuardNotice = false;
    // The log file's troubleshooting switches (LogTopics.h); each log_<topic>
    // is on by default.
    bool logMissions = true;
    bool logLoadout = true;
    bool logSupplierCrates = true;
    bool logCommonObjectives = true;
    bool logPrestigeCards = true;
    bool logMapVariants = true;
    bool logTargets = true;
    bool logDoorLocks = true;
    bool logWeapons = true;
    bool logMoney = true;
    bool logAiDifficulty = true;
    bool logSafehouse = true;
    // Prestige card shuffle: the laptop's three cards drawn from the campaign
    // seed (PrestigeCards.h). On by default; vanilla whenever anything is unexpected.
    bool prestigeShuffle = true;
    // Prestige objectives the shuffle never draws (prestige_exclude, fr::kDefaultPrestigeExclude).
    std::set<int32_t> prestigeExclude {115, 117, 118, 119, 120};
    // Footstep hearing (Footsteps.h): with dials, turns the professional
    // footstep entries on (Toggle) at a configured range per gait, rolled
    // like a dial. ini footsteps, footsteps_<gait>, footsteps_<gait>_range.
    FootstepConfig footsteps = DefaultFootstepConfig();
    // Sensor dials (SensorDials.h): with dials, roll the professional shared
    // sensor's floats and focus-cone ranges. ini sensor_dials, sensor_dials_off.
    bool sensorDials = true;
    std::set<std::string> sensorOff;
    // Sick spot steer (SickSpots.h): emetic-poisoned NPCs prefer the vomit
    // spots where 47 can kill them (toilets, push and snap-and-toss spots).
    bool sickSpotKills = true;        // sick_spot_kills
    int32_t sickSpotKillBoost = 1000; // sick_spot_kill_boost: added to those spots' priority (fr::kSickBoostDefault)
    // dials_sigma 0.7, dials_lean, dials_min, dials_max; also what invalid ini
    // values fall back to.
    RollConfig roll {0.7};
    std::set<int32_t> off; // dials_off: the AI difficulty values never changed
};

// What a scene gets.
enum class Mode {
    NotMission, // not a Freelancer mission: nothing changed
    DialsOff,   // Freelancer mission, dials = false
    Dials,      // roll AI dials
};

// Writes happen only in Freelancer missions, and only with dials.
Mode ChooseMode(const Config& cfg, bool isMission);

const char* ModeName(Mode mode);

// The mode changes values (dials).
bool ModeWrites(Mode mode);

// The three parameters the scene start check lists: EDP_AIAttentionGainMultiplier,
// EDP_HearRangeFootstepRun and EDP_HearRangeGunShot.
std::array<int32_t, 3> CheckedParams(Mode mode);

// The multiplier the mode applies to one value of param loaded as base, or
// empty when the mode leaves it alone: dials as DecideDial decides (not in the
// table, off, or base 0: empty), nothing in the modes that change nothing. The
// same for every value of a parameter (base only decides a dial's zero skip).
std::optional<double> ModeFactor(Mode mode, const Config& cfg, int32_t seed, std::string_view scene, int32_t param,
                                 double base);

// Parses a comma-separated list of EDifficultyParameter names. Entries are
// whitespace-trimmed; empty entries are ignored; unknown names are appended to
// unknownOut and skipped.
std::set<int32_t> ParseOffList(std::string_view csv, std::vector<std::string>& unknownOut);

} // namespace fr
