#include "SettingsTable.h"

#include "ClientReqs.h"
#include "Config.h"
#include "DialInfo.h"
#include "LootAmounts.h"
#include "ParamNames.h"
#include "PrestigeCards.h"
#include "SensorDials.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <set>

namespace fr {

namespace {

std::string Text(bool value) {
    return value ? "true" : "false";
}

std::string Text(double value) {
    return FormatSettingNumber(value);
}

std::string Text(int32_t value) {
    return std::to_string(value);
}

std::string Join(const std::set<std::string>& values) {
    std::string out;

    for (const std::string& value : values)
        out += (out.empty() ? "" : ",") + value;

    return out;
}

std::string OffNames(const std::set<int32_t>& params) {
    std::string out;

    for (const int32_t param : params)
        out += (out.empty() ? "" : ",") + std::string(ParamName(param));

    return out;
}

constexpr size_t Loot(MoneyKind kind) {
    return static_cast<size_t>(kind);
}

using G = SettingGroup;
using T = SettingType;
constexpr SettingApplies kRestart = SettingApplies::Restart;

// The accessor of a Config field as ini text.
#define FR_GET(expr) +[](const Config& c) { return Text(expr); }
// The accessor of a share or chance (a fraction in Config) as the ini's percent.
#define FR_PCT(expr) +[](const Config& c) { return Text(FractionToPercent(expr)); }

// The same names as fr::VariantForceNames (test_variants checks it): the
// variants A-Z by their settings label, as the Map variants group lists them.
constexpr std::string_view kVariantForces =
    "|none|the_ex_dictator|the_food_critic|the_infiltrator|the_source|the_veteran"
    "|the_warlord|lust|the_drop|liability|gluttony|the_rage|the_splitter"
    "|the_bookkeeper|the_chameleon|the_mercenary|garden_show|sloth|collector|harbinger"
    "|procurers|the_ascensionist|herbalist_party|patient_zero"
    "|snow_festival|the_controller|the_fugitive|reflection|the_surgeons|appraiser"
    "|stowaway|the_wizard|the_angel_of_death|the_entertainer|the_extractor|the_fixer"
    "|the_gunrunner|envy"
    "|heartbreaker|iconoclast|a_silver_tongue|the_undying|the_undying_returns|illusions_of_grandeur|holiday_hoarders"
    "|the_banker|the_black_hat|the_blackmailer|the_broker|the_chef|the_director|the_forger"
    "|the_identity_thief|the_paparazzo|the_pharmacist|the_sensation|embrace_of_the_serpent|revolutionary"
    "|landslide|tod_ebola_night|the_bad_boy|the_congressman|the_deceivers|the_enforcer|the_guru|lighting_copperhead"
    "|the_prince|the_twin|the_wildcard|a_bitter_pill|serial_killer";

// clang-format off
const std::array kTable = {
    // --- General --------------------------------------------------------------------------------------------------
    SettingInfo {"enabled", G::General, "Mod enabled",
        "Turns the whole mod on or off. Off: the mod changes nothing in the game.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, kRestart, {}, FR_GET(c.enabled)},

    // --- Weapons & gear: Loadout ----------------------------------------------------------------------------------
    SettingInfo {"random_loadout", G::Loadout, "Random loadout",
        "At the start of each Freelancer mission, the mod fills the gear slots you left empty with items you own: "
        "a lockpick, what the prestige objective and common objectives need (the common objective that pays most "
        "in that mission first), then random items that fit. Gear wall "
        "items are only lent: they go back on the wall after the mission, even if 47 drops, throws or uses them, "
        "carries them out or the mission fails, so you never have to unpack them in the safehouse (only if 47 also "
        "picked up another one in the mission does it come back in your loadout). Freelancer tools are not lent: "
        "as with any tool, one 47 drops or uses up is gone, and only one he carries out comes back.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.randomLoadout)},
    SettingInfo {"random_loadout_lockpick_first", G::Loadout, "Lockpick first",
        "The loadout's first pick is your own lockpick from the gear wall: the Lockpick Mk III, or else the "
        "Collector's Lockpick. It skips this if you packed a lockpick. Off: you get a lockpick only if the random "
        "fill picks one.",
        T::Bool, "true", 0, 0, 0, false, {}, "random_loadout", {}, {}, FR_GET(c.randomLoadoutLockpickFirst)},
    SettingInfo {"random_loadout_objective_items", G::Loadout, "Items for objectives",
        "The loadout packs what the chosen prestige objective needs, then what the common objectives need (the "
        "one that pays most in that mission first), right after the lockpick. Off: it skips these, and random "
        "items fill the slots left (a no-firearms objective still keeps guns out).",
        T::Bool, "true", 0, 0, 0, false, {}, "random_loadout", {}, {}, FR_GET(c.randomLoadoutObjectiveItems)},
    SettingInfo {"random_loadout_holsterable_only", G::Loadout, "Holsterable items only",
        "The loadout never packs an item 47 can't holster (one that comes in a briefcase). Off: it may pack one.",
        T::Bool, "false", 0, 0, 0, false, {}, "random_loadout", {}, {}, FR_GET(c.randomLoadoutHolsterableOnly)},

    // --- Weapons & gear: Free items -------------------------------------------------------------------------------
    SettingInfo {"lockpick_grant", G::FreeItems, "Lockpick when you bring none",
        "If 47 starts a mission with no lockpick of his own (packed, or added by Lockpick first), he gets one for "
        "that mission only. Nothing: he gets none. Slow lockpick: the classic lockpick, which opens locks more "
        "slowly. Rusty nails: a few Rusty Old Nails, one lock each.",
        T::Choice, "nails", 0, 0, 0, false, "off|slow_lockpick|nails", {}, {}, {},
        +[](const Config& c) { return c.lockpickGrant; }},
    SettingInfo {"lockpick_nails", G::FreeItems, "Rusty nails given",
        "With Rusty nails: how many Rusty Old Nails 47 gets.",
        T::Int, "5", 1, 20, 1, false, {}, {}, {}, {}, FR_GET(c.lockpickNails)},
    SettingInfo {"lockpick_slow_picks", G::FreeItems, "Slow lockpick: time per lock",
        "With Slow lockpick: how many times as long each lock takes to open with the given lockpick. 47 picks as "
        "usual, and the lock opens a little after he finishes; until then the Unlock and Open prompts stay "
        "hidden, as during a normal pick. 1: normal speed. Any other lockpick opens at normal speed.",
        T::Int, "2", 1, 4, 1, false, {}, {}, {}, "x", FR_GET(c.lockpickSlowPicks)},
    SettingInfo {"grant_makeshift_explosive", G::FreeItems, "Free Makeshift Explosive",
        "If 47 starts a Freelancer mission without a makeshift explosive, he gets one for that mission only. It "
        "takes no gear slot.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.grantMakeshiftExplosive)},
    SettingInfo {"grant_lethal_poison", G::FreeItems, "Free Lethal Poison Vial",
        "If 47 starts a Freelancer mission without a lethal poison (vial or pills), he gets a Lethal Poison Vial "
        "for that mission only. It takes no gear slot.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.grantLethalPoison)},
    SettingInfo {"grant_coin", G::FreeItems, "Free Classic Coin",
        "If 47 starts a Freelancer mission without a coin, he gets a Classic Coin for that mission only. It takes "
        "no gear slot.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.grantCoin)},

    // --- Weapons & gear: Supplier ---------------------------------------------------------------------------------
    SettingInfo {"supplier_steer", G::Supplier, "Even odds on new guns",
        "Each of the supplier's gun slots offers only guns you don't own, each equally likely.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.supplierSteer)},
    SettingInfo {"supplier_steer_gear", G::Supplier, "Even odds on new gear",
        "Each of the supplier's other slots (tools, explosives, poisons, distractions, melee) offers only items "
        "you don't own, each equally likely.",
        T::Bool, "true", 0, 0, 0, false, {}, "supplier_steer", {}, {}, FR_GET(c.supplierSteerGear)},
    SettingInfo {"supplier_force_pick", G::Supplier, "Fill empty gun slots",
        "When one of the supplier's gun slots comes up empty even though it has a gun you don't own, the slot "
        "offers one.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.supplierForcePick)},

    // --- Weapons & gear: Supply crates ----------------------------------------------------------------------------
    SettingInfo {"crate_items", G::Crates, "New items in supply crates",
        "The mod fills each supply crate you open. Most crates offer three random items from what the crates and "
        "the supplier hold and items found around the map. Some offer a gun you don't own instead, some Freelancer "
        "tools you don't own, and some only merces, each at its own chance below. A large item, such as a staff, "
        "comes alone in its crate. Crates never offer an item you already own. Off: crates offer their usual "
        "items.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.crateItems.on)},
    SettingInfo {"crate_all_tools_chance", G::Crates, "Tool crate chance",
        "The percent chance that a crate offers up to three Freelancer tools you don't own. If fewer are left, "
        "other items fill the rest. If the gun, tool and merces chances add up to more than 100, all three are "
        "scaled down to fit.",
        T::Int, "10", 0, 100, 5, false, {}, "crate_items", {}, "%", FR_PCT(c.crateItems.toolsChance)},
    SettingInfo {"crate_merces_chance", G::Crates, "Merces crate chance",
        "The percent chance that a crate holds only merces. With Random loot money on, the amount follows its "
        "supply crate range.",
        T::Int, "5", 0, 100, 5, false, {}, "crate_items", {}, "%", FR_PCT(c.crateItems.mercesChance)},
    SettingInfo {"crate_level_items", G::Crates, "Items found around the level",
        "With New items in supply crates on: crates may also offer the items lying around the mission's map, such "
        "as keys, keycards, tools and odd objects, so each map brings its own. Guns, money, briefcases and story "
        "items (ID cards, invitations, notes) never come this way, nor anything you already own, and a large item "
        "comes alone. Off: crates keep to what the "
        "crates and the supplier hold.",
        T::Bool, "true", 0, 0, 0, false, {}, "crate_items", {}, {}, FR_GET(c.crateItems.levelItems)},
    SettingInfo {"crate_items_unloaded", G::Crates, "Items from beyond the map",
        "With New items in supply crates on: gun and tool crates may also offer guns and Freelancer tools the "
        "mission's map has not loaded, so every gun you don't own and every Freelancer tool can turn up. Other "
        "crates always keep to items found in the mission. Off: gun and tool crates keep to them too.",
        T::Bool, "true", 0, 0, 0, false, {}, "crate_items", {}, {}, FR_GET(c.crateItems.unloaded)},
    SettingInfo {"crate_even_boxes", G::Crates, "Same-size selection boxes",
        "With New items in supply crates on: the box that marks the crate item you are looking at is the same "
        "size for every item and sits on the item, instead of keeping the size of whatever the crate held there "
        "before. Off: each box keeps the size the game gives it.",
        T::Bool, "true", 0, 0, 0, false, {}, "crate_items", {}, {}, FR_GET(c.crateItems.evenBoxes)},

    // Gun crates.
    SettingInfo {"crate_guns", G::Crates, "Guns in supply crates",
        "Supply crates can offer a gun you don't own, alone in its crate. With New items in supply crates on, any "
        "gun can turn up, and once you own them all, a gun crate offers three other items instead. With it off, "
        "only the 13 guns the crates hold on their own can, and once you own them all, crates stay as usual.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.crateGuns.on)},
    SettingInfo {"crate_gun_chance", G::Crates, "Gun crate chance",
        "The percent chance that an opened crate offers a gun you don't own, alone.",
        T::Int, "5", 0, 100, 5, false, {}, "crate_guns", {}, "%", FR_PCT(c.crateGuns.poolChance)},
    SettingInfo {"crate_guns_share", G::Crates, "Gun odds in a gun crate",
        "Only with New items in supply crates off: in a gun crate, how likely the offer is a gun rather than "
        "merces, in percent. 100: always a gun.",
        T::Int, "80", 0, 100, 5, false, {}, "crate_guns", {}, "%", FR_PCT(c.crateGuns.gunShare)},
    SettingInfo {"crate_guns_mult_common", G::Crates, "Common gun odds",
        "Multiplies how often a gun crate offers a common gun (the game's weight: 70).", T::Number, "1", 0, 50, 0.5,
        true, {}, "crate_guns", {}, "x", FR_GET(c.crateGuns.rarityMult[0])},
    SettingInfo {"crate_guns_mult_rare", G::Crates, "Rare gun odds",
        "Multiplies how often a gun crate offers a rare gun (the game's weight: 20).", T::Number, "2", 0, 50, 0.5,
        true, {}, "crate_guns", {}, "x", FR_GET(c.crateGuns.rarityMult[1])},
    SettingInfo {"crate_guns_mult_epic", G::Crates, "Epic gun odds",
        "Multiplies how often a gun crate offers an epic gun (the game's weight: 9).", T::Number, "5", 0, 50, 0.5,
        true, {}, "crate_guns", {}, "x", FR_GET(c.crateGuns.rarityMult[2])},
    SettingInfo {"crate_guns_mult_legendary", G::Crates, "Legendary gun odds",
        "Multiplies how often a gun crate offers a legendary gun (the game's weight: 1).", T::Number, "25", 0, 50,
        0.5, true, {}, "crate_guns", {}, "x", FR_GET(c.crateGuns.rarityMult[3])},

    // --- Weapons & gear: Weapon rarity ----------------------------------------------------------------------------
    SettingInfo {"weapon_rarity", G::Rarity, "Shuffle weapon rarities",
        "Each campaign shuffles the firearms' rarities. Each rarity keeps its number of guns, except that one more "
        "gun can become epic so that at least one unsilenced pistol is epic. The safehouse gear wall shows the new "
        "rarities.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.weaponRarity)},
    SettingInfo {"weapon_rarity_keep_min", G::Rarity, "Smallest share unchanged",
        "The smallest percent of firearms that keep their rarity. Each campaign draws a percent between this and "
        "Largest share unchanged.",
        T::Int, "5", 0, 100, 5, false, {}, "weapon_rarity", {}, "%", FR_PCT(c.weaponRarityKeepMin)},
    SettingInfo {"weapon_rarity_keep_max", G::Rarity, "Largest share unchanged",
        "The largest percent of firearms that keep their rarity.",
        T::Int, "10", 0, 100, 5, false, {}, "weapon_rarity", {}, "%", FR_PCT(c.weaponRarityKeepMax)},
    SettingInfo {"weapon_rarity_capacity", G::Rarity, "Slot cost follows rarity",
        "A gun costs one gear slot less for each step its rarity goes up, and one more for each step down.",
        T::Bool, "true", 0, 0, 0, false, {}, "weapon_rarity", {}, {}, FR_GET(c.weaponRarityCapacity)},

    // --- Weapons & gear: Miscellaneous ----------------------------------------------------------------------------
    SettingInfo {"legal_sniper_rifles", G::WeaponsMisc, "Sniper rifles legal where guns are",
        "In a disguise that lets 47 carry a gun openly, such as most guards', a sniper rifle in his hands is legal "
        "too, not only in the few disguises that already allow one.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.legalSniperRifles)},
    SettingInfo {"concealable_mace", G::WeaponsMisc, "Holsterable mace",
        "47 can holster the mace, so it no longer comes in a briefcase.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.concealableMace)},
    SettingInfo {"guard_weapons", G::WeaponsMisc, "Random guard weapons",
        "In Freelancer missions, guards carrying a pistol, SMG, assault rifle or shotgun get a random weapon from "
        "the ones their outfit allows.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.guardWeapons)},
    SettingInfo {"sick_spot_kills", G::WeaponsMisc, "Emetic poison: sick NPCs go where you can kill them",
        "An NPC made sick by emetic poison prefers to throw up where 47 can kill them: a toilet or other water to "
        "drown them in, or a ledge or railing to push them off. Off: they pick a place as in the normal game.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.sickSpotKills)},
    SettingInfo {"sick_spot_kill_boost", G::WeaponsMisc, "Emetic poison: how strongly they prefer those places",
        "How much more those places attract a sick NPC than a bin, a bush or a corner. Higher: they walk farther "
        "to reach one. 0: no preference.",
        T::Int, "1000", 0, 5000, 100, false, {}, "sick_spot_kills", {}, {}, FR_GET(c.sickSpotKillBoost)},

    // --- Maps: Map variants ---------------------------------------------------------------------------------------
    SettingInfo {"variants", G::Variants, "Map variants",
        "Bangkok, Berlin, Chongqing, Colorado, Dartmoor, Dubai, Haven, Hokkaido, Isle of Sgail, Marrakesh, Mendoza, "
        "Miami, Mumbai, Paris, Santa Fortuna, Sapienza and Whittleton Creek missions may take on the look of one of "
        "the game's bonus missions, special assignments, escalations or elusive target missions. "
        "The chances below, map by map, set how often each one comes up; the other missions use the normal map "
        "(each map's heading shows its share). Raising one chance lowers the others for that map, so they never "
        "add up to more than 100%. The campaign picks in advance, so restarting a mission keeps the same look.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.variants)},
    SettingInfo {"variant_force", G::Variants, "Force a variant",
        "Every mission on the chosen variant's map gets that variant, whatever the chances say (a variant marked * "
        "is used only in regular missions, never in showdowns). \"Every map stays normal\" turns all variants off; \"Off: the chances decide\" "
        "uses the chances again.",
        T::Choice, "", 0, 0, 0, false, kVariantForces, "variants", {}, {},
        +[](const Config& c) { return c.variantForce; }},
    SettingInfo {"full_map_variants", G::Variants, "Full map variants",
        "Some variants, such as Sapienza: Landslide, need map files the mod builds from your game's own files when "
        "the game starts (the first time, and again after a game or mod update, it takes a few seconds). Off: the mod "
        "removes those files at the next start and those variants never come up. To uninstall the mod, turn this "
        "off and restart the game once first.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, kRestart, {}, FR_GET(c.fullMapVariants)},
    SettingInfo {"variant_chance_the_ex_dictator", G::Variants, "Bangkok: The Ex-Dictator",
        "Percent of Bangkok missions set up as in the elusive target mission The Ex-Dictator. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "20", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheExDictator))},
    SettingInfo {"variant_chance_the_food_critic", G::Variants, "Bangkok: The Food Critic",
        "Percent of Bangkok missions set up as in the elusive target mission The Food Critic. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheFoodCritic))},
    SettingInfo {"variant_chance_the_infiltrator", G::Variants, "Bangkok: The Infiltrator",
        "Percent of Bangkok missions set up as in the elusive target mission The Infiltrator, with its guests and "
        "its story. It changes who is on the map and where NPCs can walk, so a target may be missing.",
        T::Int, "25", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheInfiltrator))},
    SettingInfo {"variant_chance_the_source", G::Variants, "Bangkok: The Source",
        "Percent of Bangkok missions that load all of The Source: night, the Zika outbreak and its NPCs.",
        T::Int, "30", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheSource))},
    SettingInfo {"variant_chance_the_veteran", G::Variants, "Bangkok: The Veteran",
        "Percent of Bangkok missions set up as in The Veteran, one of the bonus missions of The Sarajevo Six. "
        "It changes who is on the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheVeteran))},
    SettingInfo {"variant_chance_the_warlord", G::Variants, "Bangkok: The Warlord",
        "Percent of Bangkok missions set up as in the elusive target mission The Warlord. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheWarlord))},
    SettingInfo {"variant_chance_lust", G::Variants, "Berlin: Lust",
        "Percent of Berlin missions set up as in Lust, the Seven Deadly Sins escalation. It changes who is on the "
        "map and where NPCs can walk, so a target may be missing.",
        T::Int, "25", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kLust))},
    SettingInfo {"variant_chance_the_drop", G::Variants, "Berlin: The Drop",
        "Percent of Berlin missions during the rave of The Drop: club lights, a DJ, ravers, a bouncer and more "
        "guards. It changes who is on the map and where NPCs can walk, so a target may be missing.",
        T::Int, "35", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheDrop))},
    SettingInfo {"variant_chance_liability", G::Variants, "Berlin: The Liability",
        "Percent of Berlin missions set up as in the elusive target mission The Liability. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "20", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kLiability))},
    SettingInfo {"variant_chance_gluttony", G::Variants, "Chongqing: Gluttony",
        "Percent of Chongqing missions set up as in Gluttony, the Seven Deadly Sins escalation. It changes who is "
        "on the map and where NPCs can walk, so a target may be missing.",
        T::Int, "20", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kGluttony))},
    SettingInfo {"variant_chance_the_rage", G::Variants, "Chongqing: The Rage",
        "Percent of Chongqing missions with the streets of The Rage: Sully Bowden, his guards, and fences and "
        "barriers around the block. It changes who is on the map, so a target may be missing.",
        T::Int, "35", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheRage))},
    SettingInfo {"variant_chance_the_splitter", G::Variants, "Chongqing: The Splitter",
        "Percent of Chongqing missions set up as in the elusive target mission The Splitter, with its training "
        "camp, its people and its story. It changes who is on the map and where NPCs can walk, so a target may be "
        "missing. Several of the usual targets may be gone.",
        T::Int, "25", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheSplitter))},
    SettingInfo {"variant_chance_the_bookkeeper", G::Variants, "Colorado: The Bookkeeper",
        "Percent of Colorado missions set up as in the elusive target mission The Bookkeeper. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "25", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheBookkeeper))},
    SettingInfo {"variant_chance_the_chameleon", G::Variants, "Colorado: The Chameleon",
        "Percent of Colorado missions set up as in the elusive target mission The Chameleon. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "20", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheChameleon))},
    SettingInfo {"variant_chance_the_mercenary", G::Variants, "Colorado: The Mercenary",
        "Percent of Colorado missions set up as in The Mercenary, one of the bonus missions of The Sarajevo "
        "Six. It changes who is on the map, so a target may be missing.",
        T::Int, "35", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheMercenary))},
    SettingInfo {"variant_chance_garden_show", G::Variants, "Dartmoor: Garden Show",
        "Percent of Dartmoor missions during the Dartmoor Garden Show: themed gardens, a crowd of visitors and "
        "four garden contestants, under the show's own sky. It changes who is on the map and where NPCs can "
        "walk, so a target may be missing. A garden may also cover a safe, a stash or the supplier. Not in "
        "showdowns: the show closes the mansion where the suspects stand.",
        T::Int, "30", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kGardenShow))},
    SettingInfo {"variant_chance_sloth", G::Variants, "Dartmoor: Sloth",
        "Percent of Dartmoor missions set up as in Sloth, the Seven Deadly Sins escalation. It changes who is on "
        "the map and where NPCs can walk, so a target may be missing. Not in showdowns: it removes the "
        "showdown's start and most of its people.",
        T::Int, "15", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kSloth))},
    SettingInfo {"variant_chance_collector", G::Variants, "Dartmoor: The Collector",
        "Percent of Dartmoor missions set up as in the elusive target mission The Collector. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kCollector))},
    SettingInfo {"variant_chance_harbinger", G::Variants, "Dartmoor: The Harbinger",
        "Percent of Dartmoor missions set up as in the elusive target mission The Harbinger: the lab and party "
        "dressing, its guests, guards and story. It changes who is on the map and where NPCs can walk, so a "
        "target may be missing. Lab doors, door sensors and an airlock may block a route, an exit or the "
        "supplier.",
        T::Int, "30", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kHarbinger))},
    SettingInfo {"variant_chance_procurers", G::Variants, "Dartmoor: The Procurers",
        "Percent of Dartmoor missions set up as in the elusive target mission The Procurers. It changes who is on "
        "the map and where NPCs can walk, so a target may be missing.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kProcurers))},
    SettingInfo {"variant_chance_the_ascensionist", G::Variants, "Dubai: The Ascensionist",
        "Percent of Dubai missions set up as in the elusive target mission The Ascensionist, with its crowd and "
        "its story. It changes who is on the map and where NPCs can walk, so a target may be missing.",
        T::Int, "40", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheAscensionist))},
    SettingInfo {"variant_chance_herbalist_party", G::Variants, "Haven: The Herbalist",
        "Percent of Haven missions set up as in the elusive target mission The Herbalist: the party dressing "
        "and lights, its guests, staff and story. It changes who is on the map and where NPCs can walk, so a "
        "target may be missing.",
        T::Int, "55", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kHerbalistParty))},
    SettingInfo {"variant_chance_patient_zero", G::Variants, "Hokkaido: Patient Zero",
        "Percent of Hokkaido missions played in Patient Zero's outbreak: the quarantined hospital with its biosuit "
        "staff, doctors and quarantine guards, and the outbreak's lighting. Nobody gets infected. Some hospital "
        "doors become keycard doors, which may block a route. Not in showdowns.",
        T::Int, "25", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kPatientZero))},
    SettingInfo {"variant_chance_snow_festival", G::Variants, "Hokkaido: Snow Festival",
        "Percent of Hokkaido missions during the snow festival: garden lamps, light strings, snow sculptures and "
        "food stalls. It adds a guard and a patient and removes a few people, so a target may be missing.",
        T::Int, "30", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kSnowFestival))},
    SettingInfo {"variant_chance_the_controller", G::Variants, "Hokkaido: The Controller",
        "Percent of Hokkaido missions set up as in The Controller, one of the bonus missions of The Sarajevo "
        "Six. It changes who is on the map, so a target may be missing.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheController))},
    SettingInfo {"variant_chance_the_fugitive", G::Variants, "Hokkaido: The Fugitive",
        "Percent of Hokkaido missions set up as in the elusive target mission The Fugitive. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheFugitive))},
    SettingInfo {"variant_chance_reflection", G::Variants, "Hokkaido: The Reflection",
        "Percent of Hokkaido missions set up as in the elusive target mission The Reflection: the diner and "
        "stage, its guests and its story. It changes who is on the map and where NPCs can walk, so a target may "
        "be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kReflection))},
    SettingInfo {"variant_chance_the_surgeons", G::Variants, "Hokkaido: The Surgeons",
        "Percent of Hokkaido missions set up as in the elusive target mission The Surgeons. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheSurgeons))},
    SettingInfo {"variant_chance_appraiser", G::Variants, "Isle of Sgail: The Appraiser",
        "Percent of Isle of Sgail missions set up as in the elusive target mission The Appraiser. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "25", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kAppraiser))},
    SettingInfo {"variant_chance_stowaway", G::Variants, "Isle of Sgail: The Stowaway",
        "Percent of Isle of Sgail missions set up as in the elusive target mission The Stowaway. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "15", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kStowaway))},
    SettingInfo {"variant_chance_the_wizard", G::Variants, "Isle of Sgail: The Wizard",
        "Percent of Isle of Sgail missions set up as in the elusive target mission The Wizard, with its crowd and "
        "its story. It changes who is on the map and where NPCs can walk, so a target may be missing.",
        T::Int, "35", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheWizard))},
    SettingInfo {"variant_chance_the_angel_of_death", G::Variants, "Marrakesh: The Angel of Death",
        "Percent of Marrakesh missions set up as in the elusive target mission The Angel of Death. It changes "
        "who is on the map, so a target may be missing.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheAngelOfDeath))},
    SettingInfo {"variant_chance_the_entertainer", G::Variants, "Marrakesh: The Entertainer",
        "Percent of Marrakesh missions played on the night level, as in the elusive target mission The "
        "Entertainer: the night bazaar and shisha café with their people, plus the target and his bodyguards. The "
        "consulate and the school are closed. Not in showdowns.",
        T::Int, "35", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheEntertainer))},
    SettingInfo {"variant_chance_the_extractor", G::Variants, "Marrakesh: The Extractor",
        "Percent of Marrakesh missions set up as in The Extractor, one of the bonus missions of The Sarajevo "
        "Six. It changes who is on the map, so a target may be missing.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheExtractor))},
    SettingInfo {"variant_chance_the_fixer", G::Variants, "Marrakesh: The Fixer",
        "Percent of Marrakesh missions set up as in the elusive target mission The Fixer. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheFixer))},
    SettingInfo {"variant_chance_the_gunrunner", G::Variants, "Marrakesh: The Gunrunner",
        "Percent of Marrakesh missions set up as in the elusive target mission The Gunrunner. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "25", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheGunrunner))},
    SettingInfo {"variant_chance_envy", G::Variants, "Mendoza: Envy",
        "Percent of Mendoza missions set up as in Envy, the Seven Deadly Sins escalation. It changes who is on "
        "the map and where NPCs can walk, so a target may be missing.",
        T::Int, "20", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kEnvy))},
    SettingInfo {"variant_chance_heartbreaker", G::Variants, "Mendoza: The Heartbreaker",
        "Percent of Mendoza missions set up as in the elusive target mission The Heartbreaker. It changes who is on "
        "the map and where NPCs can walk, so a target may be missing.",
        T::Int, "30", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kHeartbreaker))},
    SettingInfo {"variant_chance_iconoclast", G::Variants, "Mendoza: The Iconoclast",
        "Percent of Mendoza missions set up as in the elusive target mission The Iconoclast. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "25", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kIconoclast))},
    SettingInfo {"variant_chance_a_silver_tongue", G::Variants, "Miami: A Silver Tongue",
        "Percent of Miami missions set up as in the special assignment A Silver Tongue. It changes who is on the "
        "map and where NPCs can walk, so a target may be missing.",
        T::Int, "40", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kASilverTongue))},
    SettingInfo {"variant_chance_the_undying", G::Variants, "Miami: The Undying",
        "Percent of Miami missions set up as in the elusive target mission The Undying, with its crowd and its "
        "story. It changes who is on the map and where NPCs can walk, so a target may be missing.",
        T::Int, "25", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheUndying))},
    SettingInfo {"variant_chance_the_undying_returns", G::Variants, "Miami: The Undying Returns",
        "Percent of Miami missions set up as in the elusive target mission The Undying Returns, with its crowd and "
        "its story. It changes who is on the map and where NPCs can walk, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheUndyingReturns))},
    SettingInfo {"variant_chance_illusions_of_grandeur", G::Variants, "Mumbai: Illusions of Grandeur",
        "Percent of Mumbai missions set up as in the special assignment Illusions of Grandeur. It changes who is "
        "on the map and where NPCs can walk, so a target may be missing. One area is closed off.",
        T::Int, "40", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kIllusionsOfGrandeur))},
    SettingInfo {"variant_chance_holiday_hoarders", G::Variants, "Paris: Holiday Hoarders",
        "Percent of Paris missions at Christmas, as in Holiday Hoarders. This can change who is on the map, so a "
        "target may be missing.",
        T::Int, "15", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kHolidayHoarders))},
    SettingInfo {"variant_chance_the_banker", G::Variants, "Paris: The Banker",
        "Percent of Paris missions set up as in the elusive target mission The Banker, with its guests and its "
        "story. It changes who is on the map and where NPCs can walk, so a target may be missing.",
        T::Int, "30", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheBanker))},
    SettingInfo {"variant_chance_the_black_hat", G::Variants, "Paris: The Black Hat",
        "Percent of Paris missions set up as in the elusive target mission The Black Hat. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheBlackHat))},
    SettingInfo {"variant_chance_the_blackmailer", G::Variants, "Paris: The Blackmailer",
        "Percent of Paris missions set up as in the elusive target mission The Blackmailer. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheBlackmailer))},
    SettingInfo {"variant_chance_the_broker", G::Variants, "Paris: The Broker",
        "Percent of Paris missions set up as in the elusive target mission The Broker. It changes who is on the "
        "map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheBroker))},
    SettingInfo {"variant_chance_the_chef", G::Variants, "Paris: The Chef",
        "Percent of Paris missions set up as in the elusive target mission The Chef. It changes who is on the "
        "map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheChef))},
    SettingInfo {"variant_chance_the_director", G::Variants, "Paris: The Director",
        "Percent of Paris missions set up as in The Director, one of the bonus missions of The Sarajevo Six. It "
        "changes who is on the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheDirector))},
    SettingInfo {"variant_chance_the_forger", G::Variants, "Paris: The Forger",
        "Percent of Paris missions set up as in the elusive target mission The Forger. It changes who is on the "
        "map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheForger))},
    SettingInfo {"variant_chance_the_identity_thief", G::Variants, "Paris: The Identity Thief",
        "Percent of Paris missions set up as in the elusive target mission The Identity Thief. It changes who "
        "is on the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheIdentityThief))},
    SettingInfo {"variant_chance_the_paparazzo", G::Variants, "Paris: The Paparazzo",
        "Percent of Paris missions set up as in the elusive target mission The Paparazzo. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kThePaparazzo))},
    SettingInfo {"variant_chance_the_pharmacist", G::Variants, "Paris: The Pharmacist",
        "Percent of Paris missions set up as in the elusive target mission The Pharmacist. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kThePharmacist))},
    SettingInfo {"variant_chance_the_sensation", G::Variants, "Paris: The Sensation",
        "Percent of Paris missions set up as in the elusive target mission The Sensation. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheSensation))},
    SettingInfo {"variant_chance_embrace_of_the_serpent", G::Variants, "Santa Fortuna: Embrace of the Serpent",
        "Percent of Santa Fortuna missions set up as in the special assignment Embrace of the Serpent. It changes "
        "who is on the map and where NPCs can walk, so a target may be missing.",
        T::Int, "30", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kEmbraceOfTheSerpent))},
    SettingInfo {"variant_chance_revolutionary", G::Variants, "Santa Fortuna: The Revolutionary",
        "Percent of Santa Fortuna missions set up as in the elusive target mission The Revolutionary. It changes "
        "who is on the map, so a target may be missing.",
        T::Int, "35", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kRevolutionary))},
    SettingInfo {"variant_chance_landslide", G::Variants, "Sapienza: Landslide",
        "Percent of Sapienza missions played on Landslide's level: the evening concert with its stage, crowd and "
        "lamps. Villa Caruso is closed. Not in showdowns.",
        T::Int, "20", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kLandslide))},
    SettingInfo {"variant_chance_tod_ebola_night", G::Variants, "Sapienza: The Author",
        "Percent of Sapienza missions played on The Author's level: Craig Black's night-time book signing by the "
        "church, with its people. Villa Caruso is closed. Not in showdowns.",
        T::Int, "15", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTodEbolaNight))},
    SettingInfo {"variant_chance_the_bad_boy", G::Variants, "Sapienza: The Bad Boy",
        "Percent of Sapienza missions played on The Icon's movie set at night with Bartholomew Argus and his "
        "people, as in the elusive target mission The Bad Boy. Not in showdowns.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheBadBoy))},
    SettingInfo {"variant_chance_the_congressman", G::Variants, "Sapienza: The Congressman",
        "Percent of Sapienza missions set up as in the elusive target mission The Congressman. It changes who "
        "is on the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheCongressman))},
    SettingInfo {"variant_chance_the_deceivers", G::Variants, "Sapienza: The Deceivers",
        "Percent of Sapienza missions set up as in the elusive target mission The Deceivers, which brings The "
        "Congressman and The Guru together. It changes who is on the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheDeceivers))},
    SettingInfo {"variant_chance_the_enforcer", G::Variants, "Sapienza: The Enforcer",
        "Percent of Sapienza missions set up as in The Enforcer, one of the bonus missions of The Sarajevo Six. "
        "It changes who is on the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheEnforcer))},
    SettingInfo {"variant_chance_the_guru", G::Variants, "Sapienza: The Guru",
        "Percent of Sapienza missions set up as in the elusive target mission The Guru. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheGuru))},
    SettingInfo {"variant_chance_lighting_copperhead", G::Variants, "Sapienza: The Icon",
        "Percent of Sapienza missions played on The Icon's level: Dino Bosco's night-time film shoot on the movie "
        "set. Only the set is open, so most places move onto it. Not in showdowns.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%",
        FR_GET(ChanceOf(c.variantChances, kLightingCopperhead))},
    SettingInfo {"variant_chance_the_prince", G::Variants, "Sapienza: The Prince",
        "Percent of Sapienza missions set up as in the elusive target mission The Prince. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "10", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kThePrince))},
    SettingInfo {"variant_chance_the_twin", G::Variants, "Sapienza: The Twin",
        "Percent of Sapienza missions set up as in the elusive target mission The Twin. It changes who is on "
        "the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheTwin))},
    SettingInfo {"variant_chance_the_wildcard", G::Variants, "Sapienza: The Wildcard",
        "Percent of Sapienza missions set up as in the elusive target mission The Wildcard. It changes who is "
        "on the map, so a target may be missing.",
        T::Int, "5", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kTheWildcard))},
    SettingInfo {"variant_chance_a_bitter_pill", G::Variants, "Whittleton Creek: A Bitter Pill",
        "Percent of Whittleton Creek missions set up as in the special assignment A Bitter Pill. It changes who "
        "is on the map and where NPCs can walk, so a target may be missing.",
        T::Int, "30", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kABitterPill))},
    SettingInfo {"variant_chance_serial_killer", G::Variants, "Whittleton Creek: The Serial Killer",
        "Percent of Whittleton Creek missions set up as in the elusive target mission The Serial Killer, in the "
        "normal daylight. It changes who is on the map, so a target may be missing.",
        T::Int, "40", 0, 100, 5, false, {}, "variants", {}, "%", FR_GET(ChanceOf(c.variantChances, kSerialKiller))},

    // --- Maps: Doors & locks --------------------------------------------------------------------------------------
    SettingInfo {"door_locks", G::Doors, "Shuffle door locks",
        "In missions and showdowns, the mod unlocks some locked doors you could pick open and locks as many "
        "unlocked ones. It leaves keycard and keypad doors alone, and keys still open the same doors.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.doorLocks)},
    SettingInfo {"door_locks_share_min", G::Doors, "Fewest swapped",
        "Each mission swaps a random share of its locked doors, between this percent and Most swapped.",
        T::Int, "50", 0, 100, 5, false, {}, "door_locks", {}, "%", FR_PCT(c.doorLocksShareMin)},
    SettingInfo {"door_locks_share_max", G::Doors, "Most swapped",
        "The most a mission swaps, in percent of its locked doors. Below Fewest swapped, it counts as Fewest "
        "swapped. 100: up to all of them.",
        T::Int, "100", 0, 100, 5, false, {}, "door_locks", {}, "%", FR_PCT(c.doorLocksShareMax)},

    // --- Maps: Possible targets -----------------------------------------------------------------------------------
    SettingInfo {"target_pool", G::Targets, "Shuffle possible targets",
        "In missions other than showdowns, some possible targets (the NPCs who can become targets) are swapped "
        "for other NPCs who are always on the map: people who walk around, and guards. A new target may be hard "
        "or impossible to reach.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.targetPool)},
    SettingInfo {"target_pool_swap_min", G::Targets, "Fewest swapped",
        "Each mission swaps a random share of its possible targets, between this percent and Most swapped. Set "
        "both to the same percent to swap that share every time.",
        T::Int, "35", 0, 100, 5, false, {}, "target_pool", {}, "%", FR_PCT(c.targetPoolSwapMin)},
    SettingInfo {"target_pool_swap_max", G::Targets, "Most swapped",
        "The most a mission swaps, in percent of its possible targets. Below Fewest swapped, it counts as Fewest "
        "swapped. 100: up to all of them.",
        T::Int, "100", 0, 100, 5, false, {}, "target_pool", {}, "%", FR_PCT(c.targetPoolSwapMax)},
    SettingInfo {"target_pool_any_guard", G::Targets, "Any guard may become a target",
        "On every map, any guard can join the possible targets. Off: only NPCs known to walk around that map "
        "can.",
        T::Bool, "true", 0, 0, 0, false, {}, "target_pool", {}, {}, FR_GET(c.targetPoolAnyGuard)},

    // --- Maps: Enforcers ------------------------------------------------------------------------------------------
    SettingInfo {"enforcers", G::Enforcers, "Shuffle enforcers",
        "In missions and showdowns, some enforcers hand their role to another NPC of the same kind (guard to "
        "guard, civilian to civilian), so different NPCs see through your disguises. The number of enforcers stays "
        "the same.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.enforcers)},
    SettingInfo {"enforcers_share_min", G::Enforcers, "Fewest moved",
        "Each mission moves a random share of its enforcers, between this percent and Most moved.",
        T::Int, "55", 0, 100, 5, false, {}, "enforcers", {}, "%", FR_PCT(c.enforcersShareMin)},
    SettingInfo {"enforcers_share_max", G::Enforcers, "Most moved",
        "The most a mission moves, in percent of its enforcers. Below Fewest moved, it counts as Fewest moved. "
        "100: up to all of them.",
        T::Int, "90", 0, 100, 5, false, {}, "enforcers", {}, "%", FR_PCT(c.enforcersShareMax)},
    SettingInfo {"enforcers_any_brick", G::Enforcers, "Move across NPC groups",
        "An enforcer with no one to swap with in its own group of NPCs may hand its role to an NPC from another "
        "group.",
        T::Bool, "true", 0, 0, 0, false, {}, "enforcers", {}, {}, FR_GET(c.enforcersAnyBrick)},
    SettingInfo {"enforcers_include_targets", G::Enforcers, "Include possible targets",
        "Possible targets may lose or gain the enforcer role.",
        T::Bool, "true", 0, 0, 0, false, {}, "enforcers", {}, {}, FR_GET(c.enforcersIncludeTargets)},
    SettingInfo {"enforcers_include_late", G::Enforcers, "Include NPCs who arrive later",
        "NPCs who arrive after the mission starts take part, but swap only with each other.",
        T::Bool, "true", 0, 0, 0, false, {}, "enforcers", {}, {}, FR_GET(c.enforcersIncludeLate)},
    SettingInfo {"enforcers_include_sentries", G::Enforcers, "Include sentries",
        "Checkpoint guards (who frisk you or check invitations) take part.",
        T::Bool, "true", 0, 0, 0, false, {}, "enforcers", {}, {}, FR_GET(c.enforcersIncludeSentries)},

    // --- Guards & AI: AI difficulty -------------------------------------------------------------------------------
    SettingInfo {"dials", G::Dials, "Random AI difficulty",
        "Each Freelancer mission randomizes the AI's difficulty values (hearing, attention, search times, damage, "
        "and more) around the game's own, leaning toward harder. The Status tab lists this mission's values. Off: "
        "the AI keeps the game's values.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.dials)},
    SettingInfo {"dials_sigma", G::Dials, "Spread",
        "How far the random values can stray from the game's values. Higher: bigger swings. 0: no change.",
        T::Number, "0.7", 0, 1.5, 0.05, false, {}, "dials", {}, {}, FR_GET(c.roll.sigma)},
    SettingInfo {"dials_lean", G::Dials, "Lean toward harder",
        "How much the random values lean toward harder. 0: easier and harder are equally likely. Below 0: they lean "
        "toward easier.",
        T::Number, "0.5", -2, 2, 0.1, false, {}, "dials", {}, {}, FR_GET(c.roll.lean)},
    SettingInfo {"dials_min", G::Dials, "Lowest multiplier",
        "No value goes below the game's value times this. Keep it at or below the highest multiplier.",
        T::Number, "0.4", 0.05, 1, 0.05, false, {}, "dials", {}, "x", FR_GET(c.roll.clampMin)},
    SettingInfo {"dials_max", G::Dials, "Highest multiplier",
        "No value goes above the game's value times this.",
        T::Number, "3", 1, 5, 0.1, false, {}, "dials", {}, "x", FR_GET(c.roll.clampMax)},
    SettingInfo {"dials_off", G::Dials, "Values never changed",
        "Check the AI difficulty values that always keep the game's value. The Status tab shows what each one "
        "does.",
        T::List, "", 0, 0, 0, false, {}, "dials", {}, {}, +[](const Config& c) { return OffNames(c.off); }},

    // --- Guards & AI: Guard senses --------------------------------------------------------------------------------
    SettingInfo {"sensor_dials", G::Senses, "Random guard senses",
        "With Random AI difficulty on: each mission also randomizes how guards see 47 (how far they see, how fast "
        "they react, how long they keep following).",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.sensorDials)},
    SettingInfo {"sensor_dials_off", G::Senses, "Senses never changed",
        "Check the guard senses that always keep the game's value.",
        T::List, "", 0, 0, 0, false, {}, "sensor_dials", {}, {}, +[](const Config& c) { return Join(c.sensorOff); }},

    // --- Guards & AI: Footsteps -----------------------------------------------------------------------------------
    SettingInfo {"footsteps", G::Footsteps, "Guards hear footsteps",
        "With Random AI difficulty on: guards hear 47's footsteps for the movements checked below, at a random "
        "range each mission. Off: as in the normal game, guards never hear footsteps in Freelancer.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.footsteps.on)},
    SettingInfo {"footsteps_walk", G::Footsteps, "Walking heard",
        "Guards hear 47 walking.", T::Bool, "true", 0, 0, 0, false, {}, "footsteps", {}, {},
        FR_GET(c.footsteps.enabled[0])},
    SettingInfo {"footsteps_walk_slow", G::Footsteps, "Slow walking heard",
        "Guards hear 47 walking slowly.", T::Bool, "true", 0, 0, 0, false, {}, "footsteps", {}, {},
        FR_GET(c.footsteps.enabled[1])},
    SettingInfo {"footsteps_run", G::Footsteps, "Running heard",
        "Guards hear 47 running.", T::Bool, "true", 0, 0, 0, false, {}, "footsteps", {}, {},
        FR_GET(c.footsteps.enabled[2])},
    SettingInfo {"footsteps_crouch_walk", G::Footsteps, "Crouch walking heard",
        "Guards hear 47 crouch walking. Off by default: crouching is how you sneak.",
        T::Bool, "false", 0, 0, 0, false, {}, "footsteps", {}, {}, FR_GET(c.footsteps.enabled[3])},
    SettingInfo {"footsteps_crouch_walk_slow", G::Footsteps, "Slow crouch walking heard",
        "Guards hear 47 crouch walking slowly.", T::Bool, "false", 0, 0, 0, false, {}, "footsteps", {}, {},
        FR_GET(c.footsteps.enabled[4])},
    SettingInfo {"footsteps_crouch_run", G::Footsteps, "Crouch running heard",
        "Guards hear 47 crouch running.", T::Bool, "false", 0, 0, 0, false, {}, "footsteps", {}, {},
        FR_GET(c.footsteps.enabled[5])},

    // --- Footsteps: a heard footstep is investigated (Investigations.h) ---
    SettingInfo {"footsteps_investigate", G::Footsteps, "Guards investigate footsteps",
        "When a guard hears 47's footsteps, the mod can make a noise where 47 stands, the kind the game uses for "
        "noisy objects, so the guard comes over to check. At most once every 8 seconds, and once every 20 seconds "
        "per guard. What it does is written to the log file, for troubleshooting; the detail lines stop after "
        "2,000 per scene.",
        T::Bool, "true", 0, 0, 0, false, {}, "footsteps", {}, {}, FR_GET(c.footstepsInvestigate)},
    SettingInfo {"footsteps_investigate_chance", G::Footsteps, "Investigate chance",
        "How often a guard who hears 47's footsteps comes over to check, instead of only turning to look.",
        T::Int, "70", 0, 100, 5, false, {}, "footsteps_investigate", {}, "%", FR_GET(c.footstepsInvestigateChance)},

    // Footstep ranges.
    SettingInfo {"footsteps_walk_range", G::Footsteps, "Walk range",
        "How far away guards hear 47 walking, before each mission's random change.", T::Number, "10", 0.5, 30, 0.5,
        false, {}, "footsteps", {}, "m", FR_GET(c.footsteps.range[0])},
    SettingInfo {"footsteps_walk_slow_range", G::Footsteps, "Slow walk range",
        "How far away guards hear 47 walking slowly, before each mission's random change.", T::Number, "6", 0.5, 30, 0.5,
        false, {}, "footsteps", {}, "m", FR_GET(c.footsteps.range[1])},
    SettingInfo {"footsteps_run_range", G::Footsteps, "Run range",
        "How far away guards hear 47 running, before each mission's random change.", T::Number, "18", 0.5, 30, 0.5,
        false, {}, "footsteps", {}, "m", FR_GET(c.footsteps.range[2])},
    SettingInfo {"footsteps_crouch_walk_range", G::Footsteps, "Crouch walk range",
        "How far away guards hear 47 crouch walking, before each mission's random change.", T::Number, "4", 0.5, 30, 0.5,
        false, {}, "footsteps", {}, "m", FR_GET(c.footsteps.range[3])},
    SettingInfo {"footsteps_crouch_walk_slow_range", G::Footsteps, "Slow crouch walk range",
        "How far away guards hear 47 crouch walking slowly, before each mission's random change.", T::Number, "2",
        0.5, 30, 0.5, false, {}, "footsteps", {}, "m", FR_GET(c.footsteps.range[4])},
    SettingInfo {"footsteps_crouch_run_range", G::Footsteps, "Crouch run range",
        "How far away guards hear 47 crouch running, before each mission's random change.", T::Number, "8", 0.5, 30, 0.5,
        false, {}, "footsteps", {}, "m", FR_GET(c.footsteps.range[5])},
    SettingInfo {"footsteps_sigma", G::Footsteps, "Footstep spread",
        "How far each mission's footstep ranges can stray from the ranges above. Separate from the AI difficulty "
        "spread.",
        T::Number, "0.15", 0, 1, 0.05, false, {}, "footsteps", {}, {}, FR_GET(c.footsteps.roll.sigma)},
    SettingInfo {"footsteps_lean", G::Footsteps, "Footstep lean",
        "How much footstep ranges lean toward longer. 0: shorter and longer are equally likely.",
        T::Number, "0", -2, 2, 0.1, false, {}, "footsteps", {}, {}, FR_GET(c.footsteps.roll.lean)},
    SettingInfo {"footsteps_min", G::Footsteps, "Footstep lowest multiplier",
        "Ranges never drop below the ranges above times this.", T::Number, "0.7", 0.1, 1, 0.05, false, {}, "footsteps", {},
        "x", FR_GET(c.footsteps.roll.clampMin)},
    SettingInfo {"footsteps_max", G::Footsteps, "Footstep highest multiplier",
        "Ranges never rise above the ranges above times this.", T::Number, "1.4", 1, 3, 0.1, false, {}, "footsteps", {},
        "x", FR_GET(c.footsteps.roll.clampMax)},

    // --- Guards & AI: Backup guards (Investigations.h) --------------------------------------------------------------
    SettingInfo {"backup_guard", G::Backup, "Backup checks on a guard who never reported back",
        "When a guard radios in a noise (a thrown item, a coin, a radio) and goes to check, and 47 takes him down "
        "before he is done, another guard comes to check the spot a little later. No backup when the first guard "
        "finished checking. What it does is written to the log file, for troubleshooting; the detail lines stop "
        "after 2,000 per scene.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.backupGuard)},
    SettingInfo {"backup_guard_delay_min", G::Backup, "Backup arrives after at least",
        "The shortest time after the first guard's radio call before the backup sets off. If 47 takes the guard "
        "down later than that, the backup sets off right away.",
        T::Int, "20", 0, 120, 5, false, {}, "backup_guard", {}, "s", FR_GET(c.backupGuardDelayMin)},
    SettingInfo {"backup_guard_delay_max", G::Backup, "Backup arrives after at most",
        "The longest time after the first guard's radio call before the backup sets off. Each backup picks a time "
        "between the two.",
        T::Int, "45", 0, 120, 5, false, {}, "backup_guard", {}, "s", FR_GET(c.backupGuardDelayMax)},
    SettingInfo {"backup_guard_notice", G::Backup, "Show \"Backup is on the way\"",
        "A short message on screen when someone answers and comes to check: \"Backup is on the way\" for a "
        "guard, \"Someone is coming to investigate\" when a civilian gets there first. No message when nobody "
        "comes.",
        T::Bool, "false", 0, 0, 0, false, {}, "backup_guard", {}, {}, FR_GET(c.backupGuardNotice)},

    // --- Objectives & money: Common objectives --------------------------------------------------------------------
    SettingInfo {"client_reqs", G::ClientReqs, "Shuffle common objectives",
        "When you choose a campaign, its missions get random common objectives in place of the game's. As in the "
        "game, the showdown gets the objectives of the campaign's mission on the same map. The Status tab shows "
        "each campaign's objectives before you choose.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.clientReqs)},
    SettingInfo {"client_reqs_mode", G::ClientReqs, "Pick from",
        "A list per campaign: each campaign draws its own list of common objectives, and its missions pick from "
        "it. Every allowed objective: each mission picks from all the common objectives you allow.",
        T::Choice, "campaign", 0, 0, 0, false, "campaign|mission", "client_reqs", {}, {},
        +[](const Config& c) { return std::string(ClientReqModeName(c.clientReqsMode)); }},
    SettingInfo {"client_reqs_pool_size", G::ClientReqs, "List size",
        "With A list per campaign: how many common objectives each campaign's list holds.",
        T::Int, "10", 3, 30, 1, false, {}, "client_reqs", {}, {}, FR_GET(c.clientReqsPoolSize)},
    SettingInfo {"client_reqs_exclude", G::ClientReqs, "Never pick",
        "Check the common objectives the shuffle must never pick. At least three must stay unchecked; Kill Only "
        "Targets and Do Not Pacify count as one, since a campaign never gets both.",
        T::List, "3,4,18", 0, 0, 0, false, {}, "client_reqs", {}, {},
        +[](const Config& c) { return FormatClientReqIds(c.clientReqsExclude); }},

    // --- Objectives & money: Prestige objectives ------------------------------------------------------------------
    SettingInfo {"prestige_shuffle", G::Prestige, "Shuffle prestige cards",
        "The safehouse laptop offers three random prestige objective cards. The draw belongs to the campaign, so "
        "a campaign always offers the same cards. If the shuffle can't run, you get the game's own cards.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.prestigeShuffle)},
    SettingInfo {"prestige_exclude", G::Prestige, "Never pick",
        "Check the prestige objectives the laptop must never offer. Leave enough unchecked for three cards: the "
        "first card is always a Silent Assassin or Timed objective, and the laptop shows at most one Silent "
        "Assassin, one Timed and one Collateral Kill card. If the game turns down an objective you left unchecked, "
        "a card may show one you checked. The laptop never offers showdown-only objectives.",
        T::List, "115,117,118,119,120", 0, 0, 0, false, {}, "prestige_shuffle", {}, {},
        +[](const Config& c) { return FormatClientReqIds(c.prestigeExclude); }},

    // --- Objectives & money: Payouts ------------------------------------------------------------------------------
    SettingInfo {"payouts", G::Money, "Random objective payouts",
        "Each objective's payout gets its own random multiplier, the same on the board, in the mission and at the "
        "end. Now and then an objective hits a jackpot.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.payouts)},
    SettingInfo {"payout_same_base", G::Money, "One base payout per objective kind",
        "Every prestige objective starts from one base amount, and every common objective from another, lower "
        "one; then each gets its random multiplier. No objective pays more just because the game pays more for "
        "it. Off: each objective starts from the game's amount.",
        T::Bool, "true", 0, 0, 0, false, {}, "payouts", {}, {}, FR_GET(c.payoutScale.sameBase)},
    SettingInfo {"payout_sigma", G::Money, "Spread",
        "How far payouts can stray from the objective's base amount. Higher: bigger swings. 0: no random spread "
        "(Lean, the shared base and jackpots still apply).",
        T::Number, "0.5", 0, 1.5, 0.05, false, {}, "payouts", {}, {}, FR_GET(c.payoutScale.sigma)},
    SettingInfo {"payout_lean", G::Money, "Lean",
        "Shifts payouts up (above 0) or down (below 0). 0: centered on the objective's base amount.",
        T::Number, "-0.1", -1, 1, 0.05, false, {}, "payouts", {}, {}, FR_GET(c.payoutScale.lean)},
    SettingInfo {"payout_min", G::Money, "Lowest multiplier",
        "No payout goes below the objective's base amount times this. Keep it at or below the highest multiplier.",
        T::Number, "0.5", 0.05, 1, 0.05, false, {}, "payouts", {}, "x", FR_GET(c.payoutScale.min)},
    SettingInfo {"payout_max", G::Money, "Highest multiplier",
        "No payout goes above the objective's base amount times this. A jackpot adds its bonus on top, so it can "
        "pay more.",
        T::Number, "2.5", 1, 5, 0.1, false, {}, "payouts", {}, "x", FR_GET(c.payoutScale.max)},
    SettingInfo {"payout_jackpot_chance", G::Money, "Jackpot chance",
        "The percent chance that an objective hits a jackpot. Prestige objectives never do. 0: no jackpots.",
        T::Int, "8", 0, 50, 1, false, {}, "payouts", {}, "%", FR_PCT(c.payoutScale.jackpotChance)},
    SettingInfo {"payout_jackpot_bonus", G::Money, "Jackpot bonus",
        "Merces a jackpot adds to the objective's payout (in steps of 50).",
        T::Number, "5000", 0, 10000, 50, false, {}, "payouts", {}, {}, FR_GET(c.payoutScale.jackpotBonus)},

    // --- Objectives & money: Loot money ---------------------------------------------------------------------------
    SettingInfo {"loot_amounts", G::Loot, "Random loot money",
        "Each safe, supply crate merces offer, money mule and New York's bank vault holds its own random amount "
        "of merces, between the least and the most set below for its kind (by default: safes 1,500-10,000, "
        "crates 250-5,000, mules 250-3,500, the vault 4,000-25,000). Most amounts land in the lower part of the "
        "range.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.lootAmounts)},
    SettingInfo {"loot_amount_vault_min", G::Loot, "Bank vault: least",
        "The least merces New York's bank vault holds. If it is above the most, the two are swapped.",
        T::Int, "4000", 500, 100000, 500, true, {}, "loot_amounts", {}, {},
        FR_GET(c.lootBounds[Loot(MoneyKind::Vault)].min)},
    SettingInfo {"loot_amount_vault_max", G::Loot, "Bank vault: most",
        "The most merces New York's bank vault holds. If it equals the least, the vault always holds that "
        "amount.",
        T::Int, "25000", 500, 100000, 500, true, {}, "loot_amounts", {}, {},
        FR_GET(c.lootBounds[Loot(MoneyKind::Vault)].max)},
    SettingInfo {"loot_amount_safe_min", G::Loot, "Safe: least",
        "The least merces a safe holds. If it is above the most, the two are swapped.",
        T::Int, "1500", 250, 50000, 250, true, {}, "loot_amounts", {}, {},
        FR_GET(c.lootBounds[Loot(MoneyKind::Safe)].min)},
    SettingInfo {"loot_amount_safe_max", G::Loot, "Safe: most",
        "The most merces a safe holds.",
        T::Int, "10000", 250, 50000, 250, true, {}, "loot_amounts", {}, {},
        FR_GET(c.lootBounds[Loot(MoneyKind::Safe)].max)},
    SettingInfo {"loot_amount_crate_min", G::Loot, "Supply crate: least",
        "The least merces a supply crate offers. If it is above the most, the two are swapped.",
        T::Int, "250", 50, 10000, 50, true, {}, "loot_amounts", {}, {},
        FR_GET(c.lootBounds[Loot(MoneyKind::Crate)].min)},
    SettingInfo {"loot_amount_crate_max", G::Loot, "Supply crate: most",
        "The most merces a supply crate offers.",
        T::Int, "5000", 50, 10000, 50, true, {}, "loot_amounts", {}, {},
        FR_GET(c.lootBounds[Loot(MoneyKind::Crate)].max)},
    SettingInfo {"loot_amount_mule_min", G::Loot, "Money mule: least",
        "The least merces a money mule carries. If it is above the most, the two are swapped.",
        T::Int, "250", 50, 10000, 50, true, {}, "loot_amounts", {}, {},
        FR_GET(c.lootBounds[Loot(MoneyKind::Mule)].min)},
    SettingInfo {"loot_amount_mule_max", G::Loot, "Money mule: most",
        "The most merces a money mule carries.",
        T::Int, "3500", 50, 10000, 50, true, {}, "loot_amounts", {}, {},
        FR_GET(c.lootBounds[Loot(MoneyKind::Mule)].max)},

    // --- Safehouse ------------------------------------------------------------------------------------------------
    SettingInfo {"safehouse_coin", G::Safehouse, "Bird's nest coin always spawns",
        "The bird's nest coin in the safehouse always appears, in one of its two nests.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.safehouseCoin)},
    SettingInfo {"fishing_script", G::Safehouse, "Scripted fishing catches",
        "In the safehouse, your first catch is a fish, the second the fishing line, the third the pirate saber; "
        "later catches are random. The order starts over on every safehouse visit.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.fishingScript)},

    // --- Advanced: Logs (for troubleshooting) ---------------------------------------------------------------------
    // One switch per topic (LogTopics.h), in the order of the topics.
    SettingInfo {"log_missions", G::Logs, "Log: level loads and campaign seeds",
        "Writes each level load, the campaign seed and which of the mod's features run in it to the log file, for "
        "troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logMissions)},
    SettingInfo {"log_loadout", G::Logs, "Log: loadout and free items",
        "Writes the random loadout's plan, the items 47 gets and the gear wall items the mission lends him to the "
        "log file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logLoadout)},
    SettingInfo {"log_supplier_crates", G::Logs, "Log: supplier and supply crates",
        "Writes how the mod changes the supplier's odds and picks, what each supply crate offers (its items and "
        "guns) and what you take, and why any of it was left as the game made it, to the log file, for "
        "troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logSupplierCrates)},
    SettingInfo {"log_weapons", G::Logs, "Log: weapon rarities, mace and sniper rifles",
        "Writes the shuffled weapon rarities, the holsterable mace and the disguises where sniper rifles were made "
        "legal to the log file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logWeapons)},
    SettingInfo {"log_map_variants", G::Logs, "Log: map variant picks and loading",
        "Writes which map variant each mission gets, how it loads and what it changes (Diana's briefing, mission "
        "stories, closed areas) to the log file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logMapVariants)},
    SettingInfo {"log_safe_spots", G::Logs, "Log: where safes can appear",
        "Writes where each safe can appear, which spot the game picked and what a map variant placed nearby to the "
        "log file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logSafeSpots)},
    SettingInfo {"log_door_locks", G::Logs, "Log: door lock swaps",
        "Writes which doors the mod locks and unlocks, and why it left the doors alone when it did, to the log "
        "file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logDoorLocks)},
    SettingInfo {"log_targets", G::Logs, "Log: target, enforcer and guard weapon swaps",
        "Writes which NPCs become possible targets, which enforcers swap and which weapons guards get to the log "
        "file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logTargets)},
    SettingInfo {"log_ai_difficulty", G::Logs, "Log: AI difficulty rolls",
        "Writes the AI difficulty values, guard senses and footstep ranges the mod rolls for each mission, and "
        "where poisoned NPCs go to throw up, to the log file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logAiDifficulty)},
    SettingInfo {"log_common_objectives", G::Logs, "Log: common objective picks",
        "Writes each campaign's common objectives, the game's and the mod's, to the log file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logCommonObjectives)},
    SettingInfo {"log_prestige_cards", G::Logs, "Log: prestige card picks",
        "Writes the prestige cards the laptop offers and how they were drawn to the log file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logPrestigeCards)},
    SettingInfo {"log_money", G::Logs, "Log: payouts and loot amounts",
        "Writes the payout the mod sets for each objective and the merces it puts in each safe, crate, money mule "
        "and vault to the log file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logMoney)},
    SettingInfo {"log_safehouse", G::Logs, "Log: safehouse coin and fishing",
        "Writes where the bird's nest coin appears, each fishing catch and why either was left as the game made "
        "it to the log file, for troubleshooting.",
        T::Bool, "true", 0, 0, 0, false, {}, {}, {}, {}, FR_GET(c.logSafehouse)},
};
// clang-format on

#undef FR_GET
#undef FR_PCT

std::string_view Trim(std::string_view s) {
    constexpr std::string_view ws = " \t\r\n\f\v";
    const size_t first = s.find_first_not_of(ws);

    if (first == std::string_view::npos)
        return {};

    return s.substr(first, s.find_last_not_of(ws) - first + 1);
}

std::string Lower(std::string_view s) {
    std::string out(s);

    for (char& ch : out)
        ch = ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;

    return out;
}

// A list's entries, trimmed, empty ones dropped, numbers in canonical form.
std::set<std::string> ListEntries(std::string_view text) {
    std::set<std::string> out;

    while (true) {
        const size_t comma = text.find(',');
        const std::string_view entry = Trim(text.substr(0, comma));

        if (!entry.empty()) {
            const auto number = ParseSettingNumber(entry);
            out.insert(number ? FormatSettingNumber(*number) : std::string(entry));
        }

        if (comma == std::string_view::npos)
            break;

        text.remove_prefix(comma + 1);
    }

    return out;
}

} // namespace

std::span<const SettingInfo> SettingsTable() {
    return kTable;
}

const SettingInfo* FindSetting(std::string_view key) {
    for (const SettingInfo& info : kTable) {
        if (info.key == key)
            return &info;
    }

    return nullptr;
}

std::string_view SettingGroupName(SettingGroup group) {
    switch (group) {
        case SettingGroup::General: return "General";
        case SettingGroup::Loadout: return "Loadout";
        case SettingGroup::FreeItems: return "Free items";
        case SettingGroup::Supplier: return "Supplier";
        case SettingGroup::Crates: return "Supply crates";
        case SettingGroup::Rarity: return "Weapon rarity";
        case SettingGroup::WeaponsMisc: return "Miscellaneous";
        case SettingGroup::Variants: return "Map variants";
        case SettingGroup::Doors: return "Doors & locks";
        case SettingGroup::Targets: return "Possible targets";
        case SettingGroup::Enforcers: return "Enforcers";
        case SettingGroup::Dials: return "AI difficulty";
        case SettingGroup::Senses: return "Guard senses";
        case SettingGroup::Footsteps: return "Footsteps";
        case SettingGroup::Backup: return "Backup guards";
        case SettingGroup::ClientReqs: return "Common objectives";
        case SettingGroup::Prestige: return "Prestige objectives";
        case SettingGroup::Money: return "Payouts";
        case SettingGroup::Loot: return "Loot money";
        case SettingGroup::Safehouse: return "Coin and fishing";
        case SettingGroup::Logs: return "Logs (for troubleshooting)";
        case SettingGroup::Count: break;
    }

    return "?";
}

std::string_view SettingGroupIntro(SettingGroup group) {
    if (group == SettingGroup::Logs)
        return "These switches decide what goes into the log file, FreelancerRandomizer.log, next to the mod; they "
               "change nothing in the game. A few essential lines are always written: the mod loading, the settings "
               "it read, and errors. The switches are on by default and write what each feature did and why it did "
               "nothing "
               "when it didn't, enough to look into a problem. The log holds your items, loadouts, objectives and "
               "campaign seeds and file paths on your PC: check it before you share it.";

    return {};
}

SettingSection SettingSectionOf(SettingGroup group) {
    switch (group) {
        case SettingGroup::General: return SettingSection::General;
        case SettingGroup::Loadout:
        case SettingGroup::FreeItems:
        case SettingGroup::Supplier:
        case SettingGroup::Crates:
        case SettingGroup::Rarity:
        case SettingGroup::WeaponsMisc: return SettingSection::WeaponsGear;
        case SettingGroup::Variants:
        case SettingGroup::Doors:
        case SettingGroup::Targets:
        case SettingGroup::Enforcers: return SettingSection::Maps;
        case SettingGroup::Dials:
        case SettingGroup::Senses:
        case SettingGroup::Footsteps:
        case SettingGroup::Backup: return SettingSection::Guards;
        case SettingGroup::ClientReqs:
        case SettingGroup::Prestige:
        case SettingGroup::Money:
        case SettingGroup::Loot: return SettingSection::ObjectivesMoney;
        case SettingGroup::Safehouse: return SettingSection::Safehouse;
        case SettingGroup::Logs: return SettingSection::Advanced;
        case SettingGroup::Count: break;
    }

    return SettingSection::Count;
}

std::string_view SettingSectionName(SettingSection section) {
    switch (section) {
        case SettingSection::General: return "General";
        case SettingSection::WeaponsGear: return "Weapons & gear";
        case SettingSection::Maps: return "Maps";
        case SettingSection::Guards: return "Guards & AI";
        case SettingSection::ObjectivesMoney: return "Objectives & money";
        case SettingSection::Safehouse: return "Safehouse";
        case SettingSection::Advanced: return "Advanced";
        case SettingSection::Count: break;
    }

    return "?";
}

std::string_view SettingSectionIntro(SettingSection section) {
    if (section == SettingSection::Advanced)
        return "For troubleshooting: most players never need to change these.";

    return {};
}

std::vector<SettingGroup> SettingSectionGroups(SettingSection section) {
    std::vector<SettingGroup> out;

    for (size_t i = 0; i < static_cast<size_t>(SettingGroup::Count); ++i) {
        if (SettingSectionOf(static_cast<SettingGroup>(i)) == section)
            out.push_back(static_cast<SettingGroup>(i));
    }

    return out;
}

bool SettingGroupIsSection(SettingGroup group) {
    return SettingSectionGroups(SettingSectionOf(group)).size() == 1 &&
           SettingGroupName(group) == SettingSectionName(SettingSectionOf(group));
}

std::string_view SettingAppliesText(SettingApplies applies) {
    return applies == SettingApplies::Restart ? "after restarting the game"
                                              : "at the next mission start, restart or safehouse visit";
}

std::vector<std::string_view> SettingChoices(const SettingInfo& info) {
    std::vector<std::string_view> out;
    std::string_view rest = info.choices;

    if (info.type != SettingType::Choice)
        return out;

    while (true) {
        const size_t bar = rest.find('|');
        out.push_back(rest.substr(0, bar));

        if (bar == std::string_view::npos)
            break;

        rest.remove_prefix(bar + 1);
    }

    return out;
}

int SettingChoiceIndex(const SettingInfo& info, std::string_view text) {
    const std::string wanted = Lower(Trim(text));
    const std::vector<std::string_view> choices = SettingChoices(info);

    for (size_t i = 0; i < choices.size(); ++i) {
        if (Lower(choices[i]) == wanted)
            return static_cast<int>(i);
    }

    return -1;
}

std::string SettingChoiceLabel(const SettingInfo& info, std::string_view choice) {
    if (info.key == "variant_force") {
        if (choice.empty())
            return "Off: the chances decide";

        if (choice == "none")
            return "Every map stays normal";

        if (const SettingInfo* chance = FindSetting("variant_chance_" + std::string(choice)))
            return std::string(chance->label);
    }

    // Plain names for the choices; the ini keeps the ids.
    struct Named {
        std::string_view key, choice, label;
    };
    static constexpr std::array kNamed = {
        Named {"lockpick_grant", "off", "Nothing"},
        Named {"lockpick_grant", "slow_lockpick", "Slow lockpick"},
        Named {"lockpick_grant", "nails", "Rusty nails"},
        Named {"client_reqs_mode", "campaign", "A list per campaign"},
        Named {"client_reqs_mode", "mission", "Every allowed objective"},
    };

    for (const Named& named : kNamed) {
        if (named.key == info.key && named.choice == choice)
            return std::string(named.label);
    }

    return choice.empty() ? std::string("(off)") : std::string(choice);
}

std::optional<bool> ParseSettingBool(std::string_view text) {
    const std::string word = Lower(Trim(text));

    if (word == "true" || word == "1" || word == "yes" || word == "on" || word == "y")
        return true;

    if (word == "false" || word == "0" || word == "no" || word == "off" || word == "n")
        return false;

    return std::nullopt;
}

std::optional<double> ParseSettingNumber(std::string_view text) {
    text = Trim(text);

    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);

    double value = 0.0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

    if (text.empty() || error != std::errc() || end != text.data() + text.size() || !std::isfinite(value))
        return std::nullopt;

    return value;
}

std::optional<int32_t> ParseSettingInt(std::string_view text) {
    const auto value = ParseSettingNumber(text);

    if (!value)
        return std::nullopt;

    const double rounded = std::round(*value);

    if (rounded < static_cast<double>(INT32_MIN) || rounded > static_cast<double>(INT32_MAX))
        return std::nullopt;

    return static_cast<int32_t>(rounded);
}

std::string FormatSettingNumber(double value) {
    if (value == 0.0)
        return "0"; // never "-0"

    char buffer[64];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return error == std::errc() ? std::string(buffer, end) : std::to_string(value);
}

double PercentToFraction(double percent) {
    return percent / 100.0;
}

double FractionToPercent(double fraction) {
    return std::round(fraction * 100.0 * 1e6) / 1e6;
}

std::string PercentText(double fraction) {
    return FormatSettingNumber(FractionToPercent(fraction)) + "%";
}

std::string NormalizeSettingNumber(const SettingInfo& info, double value) {
    if (!std::isfinite(value))
        value = SettingNumberOr(info, info.def);

    if (info.max > info.min)
        value = std::clamp(value, info.min, info.max);

    if (info.step > 0.0) {
        value = std::round(value / info.step) * info.step;
        // Snapping can leave float noise (0.35000000000000003): round to the step's decimals.
        value = std::round(value * 1e6) / 1e6;

        if (info.max > info.min)
            value = std::clamp(value, info.min, info.max);
    }

    if (info.type == SettingType::Int)
        value = std::round(value);

    return FormatSettingNumber(value);
}

int SettingStepCount(const SettingInfo& info) {
    if (info.step <= 0.0 || info.max <= info.min)
        return 0;

    return static_cast<int>(std::floor((info.max - info.min) / info.step + 1e-6));
}

int SettingStepIndex(const SettingInfo& info, double value) {
    if (info.step <= 0.0 || !std::isfinite(value))
        return 0;

    return std::clamp(static_cast<int>(std::lround((value - info.min) / info.step)), 0, SettingStepCount(info));
}

double SettingStepValue(const SettingInfo& info, int index) {
    // Through the ini text, so the value has no float noise (0.35, not 0.35000000000000003).
    return SettingNumberOr(info, NormalizeSettingNumber(info, info.min + index * info.step));
}

double SettingNumberOr(const SettingInfo& info, std::string_view text) {
    if (const auto value = ParseSettingNumber(text))
        return *value;

    return ParseSettingNumber(info.def).value_or(0.0);
}

bool SettingBoolOr(const SettingInfo& info, std::string_view text) {
    if (const auto value = ParseSettingBool(text))
        return *value;

    return ParseSettingBool(info.def).value_or(false);
}

bool SameSettingValue(const SettingInfo& info, std::string_view a, std::string_view b) {
    switch (info.type) {
        case SettingType::Bool: return ParseSettingBool(a) == ParseSettingBool(b) && ParseSettingBool(a).has_value();
        case SettingType::Int:
        case SettingType::Number: {
            const auto x = ParseSettingNumber(a);
            const auto y = ParseSettingNumber(b);

            if (!x || !y)
                return !x && !y && Trim(a) == Trim(b);

            return std::abs(*x - *y) <= 1e-9 * std::max({1.0, std::abs(*x), std::abs(*y)});
        }
        case SettingType::Choice: return Lower(Trim(a)) == Lower(Trim(b));
        case SettingType::List: return ListEntries(a) == ListEntries(b);
    }

    return false;
}

bool IsDefaultSetting(const SettingInfo& info, std::string_view text) {
    return SameSettingValue(info, text, info.def);
}

std::vector<SettingListOption> SettingListOptions(const SettingInfo& info) {
    std::vector<SettingListOption> out;

    if (info.key == "client_reqs_exclude") {
        for (const int32_t id : ClientReqIds())
            out.push_back({id, std::string(ClientReqName(id))});
    }
    else if (info.key == "prestige_exclude") {
        for (const int32_t id : PrestigeExcludeOptions())
            out.push_back({id, PrestigeChoiceByObjective(id)->name});
    }
    else if (info.key == "dials_off") {
        for (const DialInfo& dial : DialInfoTable())
            out.push_back({dial.param, std::string(dial.name)});
    }
    else if (info.key == "sensor_dials_off") {
        const std::span<const SensorDial> dials = SensorDialTable();

        for (size_t i = 0; i < dials.size(); ++i)
            out.push_back({static_cast<int32_t>(i), std::string(dials[i].info.name)});
    }

    return out;
}

std::set<int32_t> SettingListIds(const SettingInfo& info, std::string_view text) {
    std::vector<std::string> bad;

    if (info.key == "dials_off")
        return ParseOffList(text, bad);

    if (info.key == "sensor_dials_off") {
        const std::span<const SensorDial> dials = SensorDialTable();
        std::set<int32_t> ids;

        for (const std::string& key : ParseSensorOffList(text, bad)) {
            for (size_t i = 0; i < dials.size(); ++i) {
                if (dials[i].key == key)
                    ids.insert(static_cast<int32_t>(i));
            }
        }

        return ids;
    }

    return ParseClientReqIds(text, bad);
}

std::string SettingListToggle(const SettingInfo& info, std::string_view text, int32_t id, bool on) {
    std::set<int32_t> ids = SettingListIds(info, text);

    if (on)
        ids.insert(id);
    else
        ids.erase(id);

    // The never-changed lists keep their names in the ini.
    if (info.key == "dials_off" || info.key == "sensor_dials_off") {
        const std::span<const SensorDial> dials = SensorDialTable();
        std::string out;

        for (const int32_t i : ids) {
            const std::string_view name = info.key == "dials_off" ? ParamName(i) : dials[static_cast<size_t>(i)].key;
            out += (out.empty() ? "" : ", ") + std::string(name);
        }

        return out;
    }

    return FormatClientReqIds(ids);
}

bool SettingListLeavesEnough(const SettingInfo& info, const std::set<int32_t>& ids) {
    if (info.key == "client_reqs_exclude")
        return ClientReqExcludeLeavesEnough(ids);

    if (info.key == "prestige_exclude")
        return PrestigeExcludeLeavesEnough(ids);

    return true;
}

} // namespace fr
