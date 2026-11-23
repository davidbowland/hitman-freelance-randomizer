#include "Variants.h"
#include "LandslideFull.h" // FullVariant, NeverTargetRepos

#include "Rng.h"
#include "Scene.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace fr {

namespace {

// Scene resources as the Freelancer contracts name them (Peacock
// contractdata/SNUG, Metadata.ScenePath). Each option applies to both scenes
// of its map, except the ones with a full version (LandslideFull.h: The
// Author, Landslide, The Icon, The Bad Boy, Patient Zero, Marrakesh's night,
// The Entertainer), which only the mild scene offers: the full version is
// made for the mild contract, and there is no lighting-only version for the
// showdown. A map's options are in its chance walk's order: new ones go last,
// so the chances of the old ones roll the same.
const std::array<VariantMap, 34>& Maps() {
    using Options = std::vector<const VariantOption*>;
    static const Options kBangkok {&kTheSource,     &kTheWarlord, &kTheFoodCritic,
                                   &kTheExDictator, &kTheVeteran, &kTheInfiltrator};
    static const Options kHokkaido {&kPatientZero, &kSnowFestival, &kReflection,
                                    &kTheSurgeons, &kTheFugitive,  &kTheController};
    static const Options kHokkaidoHot {&kSnowFestival, &kReflection, &kTheSurgeons, &kTheFugitive, &kTheController};
    static const Options kSapienza {&kTodEbolaNight, &kLandslide, &kLightingCopperhead, &kTheCongressman, &kTheGuru,
                                    &kThePrince,     &kTheTwin,   &kTheWildcard,        &kTheDeceivers,   &kTheEnforcer,
                                    &kTheBadBoy};
    static const Options kSapienzaHot {&kTheCongressman, &kTheGuru,      &kThePrince,  &kTheTwin,
                                       &kTheWildcard,    &kTheDeceivers, &kTheEnforcer};
    static const Options kParis {&kHolidayHoarders,  &kTheForger,      &kTheSensation, &kTheBlackHat,
                                 &kThePharmacist,    &kThePaparazzo,   &kTheChef,      &kTheBroker,
                                 &kTheIdentityThief, &kTheBlackmailer, &kTheDirector,  &kTheBanker};
    static const Options kColorado {&kTheBookkeeper, &kTheChameleon, &kTheMercenary};
    static const Options kDartmoor {&kHarbinger, &kProcurers, &kCollector, &kGardenShow, &kSloth};
    // Showdowns: not Garden Show, which closes the mansion its suspects stand in (and they spawn in a
    // basement the garden layout lacks).
    static const Options kDartmoorHot {&kHarbinger, &kProcurers, &kCollector};
    static const Options kSgail {&kAppraiser, &kStowaway, &kTheWizard};
    static const Options kHaven {&kHerbalistParty};
    static const Options kChongqing {&kTheRage, &kTheSplitter, &kGluttony};
    static const Options kBerlin {&kTheDrop, &kLiability, &kLust};
    static const Options kWhittleton {&kSerialKiller, &kABitterPill};
    static const Options kMendoza {&kHeartbreaker, &kIconoclast, &kEnvy};
    static const Options kSantaFortuna {&kRevolutionary, &kEmbraceOfTheSerpent};
    static const Options kMiami {&kTheUndying, &kASilverTongue, &kTheUndyingReturns};
    static const Options kMarrakesh {&kTheFixer, &kTheGunrunner, &kTheAngelOfDeath, &kTheExtractor, &kTheEntertainer};
    static const Options kMarrakeshHot {&kTheFixer, &kTheGunrunner, &kTheAngelOfDeath, &kTheExtractor};
    static const std::array<VariantMap, 34> s_Maps {{
        {"Bangkok mild", "assembly:/_pro/scenes/missions/Bangkok/scene_tiger_mild_sicbo.entity", kBangkok},
        {"Bangkok hot", "assembly:/_pro/scenes/missions/bangkok/scene_tiger_hot_sicbo.entity", kBangkok},
        {"Hokkaido mild", "assembly:/_PRO/Scenes/Missions/hokkaido/scene_snowcrane_mild_shogi.entity", kHokkaido},
        {"Hokkaido hot", "assembly:/_pro/scenes/missions/hokkaido/scene_snowcrane_hot_shogi.entity", kHokkaidoHot},
        {"Sapienza mild", "assembly:/_PRO/Scenes/Missions/CoastalTown/scene_costaltown_mild_bingo.entity", kSapienza},
        {"Sapienza hot", "assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_hot_bingo.entity", kSapienzaHot},
        // No Paris lighting option: tod_sunset and lighting_newlook already come with
        // Freelancer Paris (via mission_fashionshowhit_01).
        {"Paris mild", "assembly:/_pro/scenes/missions/paris/scene_peacock_mild_craps.entity", kParis},
        {"Paris hot", "assembly:/_pro/scenes/missions/paris/scene_peacock_hot_craps.entity", kParis},
        {"Haven mild", "assembly:/_pro/scenes/missions/opulent/mission_stingray/scene_opulent_mild_hilo.entity",
         kHaven},
        {"Haven hot", "assembly:/_pro/scenes/missions/opulent/mission_stingray/scene_opulent_hot_hilo.entity", kHaven},
        {"Chongqing mild", "assembly:/_pro/scenes/missions/wet/scene_wet_mild_mahjong.entity", kChongqing},
        {"Chongqing hot", "assembly:/_pro/scenes/missions/wet/scene_wet_hot_mahjong.entity", kChongqing},
        {"Colorado mild", "assembly:/_pro/scenes/missions/colorado_2/scene_bull_mild_spades.entity", kColorado},
        {"Colorado hot", "assembly:/_pro/scenes/missions/colorado_2/scene_bull_hot_spades.entity", kColorado},
        {"Dartmoor mild", "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity", kDartmoor},
        {"Dartmoor hot", "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_hot_bridge.entity", kDartmoorHot},
        {"Berlin mild", "assembly:/_pro/scenes/missions/edgy/mission_fox/scene_edgy_mild_meyer.entity", kBerlin},
        {"Berlin hot", "assembly:/_pro/scenes/missions/edgy/mission_fox/scene_edgy_hot_meyer.entity", kBerlin},
        {"Whittleton Creek mild", "assembly:/_pro/scenes/missions/skunk/scene_skunk_mild_solitaire.entity",
         kWhittleton},
        {"Whittleton Creek hot", "assembly:/_pro/scenes/missions/skunk/scene_skunk_hot_solitaire.entity", kWhittleton},
        {"Mendoza mild", "assembly:/_pro/scenes/missions/elegant/scene_elegant_mild_sapo.entity", kMendoza},
        {"Mendoza hot", "assembly:/_pro/scenes/missions/elegant/scene_elegant_hot_sapo.entity", kMendoza},
        {"Isle of Sgail mild", "assembly:/_pro/scenes/missions/theark/scene_magpie_mild_whist.entity", kSgail},
        {"Isle of Sgail hot", "assembly:/_pro/scenes/missions/theark/scene_magpie_hot_whist.entity", kSgail},
        {"Santa Fortuna mild", "assembly:/_pro/scenes/missions/colombia/scene_hippo_mild_trompo.entity", kSantaFortuna},
        {"Santa Fortuna hot", "assembly:/_pro/scenes/missions/colombia/scene_hippo_hot_trompo.entity", kSantaFortuna},
        // Marrakesh's scenes are under Marrakesh/ (capital M) in the contracts.
        {"Marrakesh mild", "assembly:/_pro/scenes/missions/Marrakesh/scene_spider_mild_mancala.entity", kMarrakesh},
        {"Marrakesh hot", "assembly:/_pro/scenes/missions/Marrakesh/scene_spider_hot_mancala.entity", kMarrakeshHot},
        {"Dubai mild",
         "assembly:/_pro/scenes/missions/golden/mission_gecko/scene_golden_mild_chess.entity",
         {&kTheAscensionist}},
        {"Dubai hot",
         "assembly:/_pro/scenes/missions/golden/mission_gecko/scene_golden_hot_chess.entity",
         {&kTheAscensionist}},
        {"Miami mild", "assembly:/_pro/scenes/missions/miami/scene_flamingo_mild_pinochle.entity", kMiami},
        {"Miami hot", "assembly:/_pro/scenes/missions/miami/scene_flamingo_hot_pinochle.entity", kMiami},
        // Mumbai (Illusions of Grandeur).
        {"Mumbai mild",
         "assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.entity",
         {&kIllusionsOfGrandeur}},
        {"Mumbai hot", "assembly:/_pro/scenes/missions/mumbai/scene_mongoose_hot_ludo.entity", {&kIllusionsOfGrandeur}},
    }};
    return s_Maps;
}

size_t OptionIndex(const VariantOption& option) {
    const auto all = AllVariantOptions();
    return static_cast<size_t>(std::find(all.begin(), all.end(), &option) - all.begin());
}

// value / 100 as "30", "16.67" (two decimals, trailing zeros dropped).
std::string Hundredths(uint32_t value) {
    std::string out = std::to_string(value / 100);
    const uint32_t frac = value % 100;
    if (frac != 0) {
        out += '.';
        out += static_cast<char>('0' + frac / 10);
        if (frac % 10 != 0)
            out += static_cast<char>('0' + frac % 10);
    }
    return out;
}

} // namespace

// rid: the brick's TEMP hash in the hash list, [path].pc_entitytype.
const VariantBrick kTodZikaNightBrick {"tod_zika_night", "assembly:/_pro/scenes/missions/bangkok/tod_zika_night.brick",
                                       0x0069324FADF99DA9ull};
const VariantBrick kMissionZikaBrick {"mission_zika", "assembly:/_pro/scenes/missions/bangkok/mission_zika.brick",
                                      0x00548596858A571Eull};
const VariantBrick kTodFluBrick {"tod_flu", "assembly:/_pro/scenes/missions/hokkaido/tod_flu.brick",
                                 0x0025DD895C349D17ull};
const VariantBrick kGfxFluBrick {"gfx_flu", "assembly:/_pro/scenes/missions/hokkaido/gfx_flu.brick",
                                 0x001B58DA1120083Bull};
const VariantBrick kTodEbolaNightBrick {
    "tod_ebola_night", "assembly:/_pro/scenes/missions/coastaltown/tod_ebola_night.brick", 0x008DAD11D7EEBE74ull};
const VariantBrick kLightingMambaBrick {
    "lighting_mamba", "assembly:/_pro/scenes/missions/coastaltown/lighting_mamba.brick", 0x00979CC61CFB841Aull};
const VariantBrick kTodSummerMorningClearBrick {
    "tod_summer_morning_clear", "assembly:/_pro/scenes/missions/coastaltown/tod_summer_morning_clear.brick",
    0x007B86AD8C886AE7ull};
const VariantBrick kLightingCopperheadBrick {"lighting_copperhead",
                                             "assembly:/_pro/scenes/missions/coastaltown/lighting_copperhead.brick",
                                             0x005FC5439416F83Full};
const VariantBrick kTodSummerNightClearBrick {"tod_summer_night_clear",
                                              "assembly:/_pro/scenes/missions/coastaltown/tod_summer_night_clear.brick",
                                              0x00229AA6BC900212ull};
// Marrakesh's own night time of day (A House Built on Sand's night, 52 KB in
// chunk25; it references only location).
const VariantBrick kTodSummerNightClearMarrakeshBrick {
    "tod_summer_night_clear", "assembly:/_pro/scenes/missions/marrakesh/tod_summer_night_clear.brick",
    0x001769B67BF98D12ull};
// Landslide's set dressing, without its story (mission_mamba) and without
// dressing_mission_mamba (blockedoff_mansion, 327 location deletes). Each
// references only location.brick and has no NPCs; beach (stage, posters,
// booths, light strings) deletes 55 location entities and church 2, the
// three small ones (keys, poster boards, garbage bags) none.
const VariantBrick kDressingBeachMambaBrick {"dressing_beach_mamba",
                                             "assembly:/_pro/scenes/missions/coastaltown/dressing_beach_mamba.brick",
                                             0x000250E46F74BE36ull};
const VariantBrick kDressingChurchMambaBrick {"dressing_church_mamba",
                                              "assembly:/_pro/scenes/missions/coastaltown/dressing_church_mamba.brick",
                                              0x001C2D18E84BD5A0ull};
const VariantBrick kDressingOldtownMambaBrick {
    "dressing_oldtown_mamba", "assembly:/_pro/scenes/missions/coastaltown/dressing_oldtown_mamba.brick",
    0x0019D2EAB61099E3ull};
const VariantBrick kDressingTownhallMambaBrick {
    "dressing_townhall_mamba", "assembly:/_pro/scenes/missions/coastaltown/dressing_townhall_mamba.brick",
    0x00156F74F3BA0E84ull};
const VariantBrick kDressingTunnelsMambaBrick {
    "dressing_tunnels_mamba", "assembly:/_pro/scenes/missions/coastaltown/dressing_tunnels_mamba.brick",
    0x00C20A7EC00C1F5Bull};
const VariantBrick kChallengesZikaBrick {
    "challenges_zika", "assembly:/_pro/scenes/missions/bangkok/challenges_zika.brick", 0x00628E3E01F948D7ull};
const VariantBrick kLocationParisNoelBrick {
    "location_parisnoel", "assembly:/_pro/scenes/missions/paris/location_parisnoel.brick", 0x00B69DB24675E6ECull};
// Official dressing layers. The "_ea" bricks are the
// official variants' environment art: set dressing with no NPCs and no
// navmesh, layered over the base brick Freelancer already loads.
// Patient Zero's quarantine dressing: plastic walls, biohazard bags, field
// tables; 279 location deletes, 8 hospital doors swapped for RFID ones.
const VariantBrick kEaFluBrick {"ea_flu", "assembly:/_pro/scenes/missions/hokkaido/ea_flu.brick",
                                0x0030C60BD2E68D98ull};
// The snow festival: garden lamps, light strings, snow sculptures, food
// stalls; 2 NPCs, 7 deletes (4 in npc_mission_snowcrane), no navmesh.
const VariantBrick kScenarioMamushiBrick {
    "scenario_mamushi", "assembly:/_pro/scenes/missions/hokkaido/scenario_mamushi.brick", 0x00502BC47FC839D9ull};
// The Herbalist dressing (244 scenario_stingray deletes, 14 collision
// boxes) and its lights. The navmesh stays override_hilo's.
const VariantBrick kScenarioGreentiniEaBrick {"scenario_greentini_ea",
                                              "assembly:/_pro/scenes/missions/opulent/scenario_greentini_ea.brick",
                                              0x001252DEA472C74Full};
const VariantBrick kScenarioGreentiniLightingBrick {
    "scenario_greentini_lighting", "assembly:/_pro/scenes/missions/opulent/scenario_greentini_lighting.brick",
    0x0070BA7AF778A58Full};
// The Rage: Sully Bowden, about 15 guards, street fences; 155 scenario_rat
// deletes (1 a pool actor); brings outfits_redsnapper and its own navmesh
// (scene_rat_elusive_redsnapper.navp, the official override).
const VariantBrick kElusiveRedsnapperBrick {
    "elusive_redsnapper", "assembly:/_pro/scenes/missions/wet/elusive_redsnapper.brick", 0x0048C98715164BC3ull};
// The Drop's rave: lights, 28 NPCs (ravers, DJ, bouncer, guards), 413
// scenario_fox deletes, its own navmesh and crowd maps.
const VariantBrick kElusiveTomorrowlandBrick {
    "elusive_tomorrowland", "assembly:/_pro/scenes/missions/edgy/mission_fox/elusive_tomorrowland.brick",
    0x00C6C53821368B23ull};
// The Serial Killer: the elusive target's layer over scenario_skunk (the
// official contract loads it alone on scene_skunk, as Freelancer's scene
// loads mission_*_solitaire). Freelancer Variations pairs it with its own
// skunk/tod_sunset, which is not a game file (not in the hash list), so the
// map keeps its day lighting.
const VariantBrick kElusiveAlabamaslammerBrick {"elusive_alabamaslammer",
                                                "assembly:/_pro/scenes/missions/skunk/elusive_alabamaslammer.brick",
                                                0x001BDE20B3F81CB1ull};
// Elusive target layers, each the one brick its official
// contract loads on its scene (Peacock contractdata/<map>/ELUSIVE), layered
// over the base brick the Freelancer scene already loads. Contents not dumped
// yet; the navmesh notes are from the rpkg metadata.
// The Procurers (Dartmoor): its own navmesh, as The Drop's.
const VariantBrick kElusiveVesperBrick {
    "elusive_vesper", "assembly:/_pro/scenes/missions/ancestral/elusive_vesper.brick", 0x0033C24EEA8C6CE8ull};
// The Collector (Dartmoor).
const VariantBrick kElusiveBrambleBrick {
    "elusive_bramble", "assembly:/_pro/scenes/missions/ancestral/elusive_bramble.brick", 0x00523CD890BED552ull};
// The Liability (Berlin), under edgy/mission_fox/ like elusive_tomorrowland.
const VariantBrick kElusiveRadlerBrick {
    "elusive_radler", "assembly:/_pro/scenes/missions/edgy/mission_fox/elusive_radler.brick", 0x005FDEBBAD94D62Cull};
// The Heartbreaker (Mendoza): its own navmesh, which takes over from the
// Freelancer scene's (override_sapo's).
const VariantBrick kElusiveClericoBrick {
    "elusive_clerico", "assembly:/_pro/scenes/missions/elegant/elusive_clerico.brick", 0x00F3756BC7320F9Full};
// The Iconoclast (Mendoza): no navmesh of its own.
const VariantBrick kElusiveJockeyclubBrick {
    "elusive_jockeyclub", "assembly:/_pro/scenes/missions/elegant/elusive_jockeyclub.brick", 0x00174B7F78F90C07ull};
// The Appraiser and The Stowaway (Isle of Sgail).
const VariantBrick kElusiveGoldendoubletBrick {"elusive_goldendoublet",
                                               "assembly:/_pro/scenes/missions/theark/elusive_goldendoublet.brick",
                                               0x0071D0E4863B00AFull};
const VariantBrick kElusiveSkittlebombBrick {
    "elusive_skittlebomb", "assembly:/_pro/scenes/missions/theark/elusive_skittlebomb.brick", 0x000B6DF156AAD815ull};
// The Revolutionary (Santa Fortuna).
const VariantBrick kElusiveHighballBrick {
    "elusive_highball", "assembly:/_pro/scenes/missions/colombia/elusive_highball.brick", 0x008BF14772B57FFEull};
// The Dartmoor Garden Show: the official scene_smoothsnake is this
// one layer over scenario_bulldog, which Freelancer Dartmoor loads. 4 MB:
// themed gardens, a visitor crowd, four contestants, its own sky and its own
// navmesh (scene_smoothsnake.navp; Freelancer Dartmoor ships no navmesh
// override). Its externals (outfits_smoothsnake in chunk3patch4,
// golden/outfits_gecko in chunk0) come with it. Without mission_smoothsnake
// (the escalation's objectives).
const VariantBrick kScenarioSmoothsnakeBrick {"scenario_smoothsnake",
                                              "assembly:/_pro/scenes/missions/ancestral/scenario_smoothsnake.brick",
                                              0x00E56CA467419D97ull};
// The HITMAN 2016 elusive targets (also IOI's Elusive Target Arcade)
// and the Sarajevo Six bonus missions, each the one layer its official
// contract loads on the map's story scene (Paris _scene_paris, Sapienza
// _scene_octopus, Marrakesh _scene_spider, Bangkok _scene_tiger, Colorado
// _scene_bull, Hokkaido _scene_snowcrane), whose base Freelancer already
// loads. None has a navmesh of its own; each pulls in its own outfits brick.
// Contents not decoded: their pool actor deletes and fly-in briefings are
// unknown (the variant check reports the former).
// Paris: The Forger.
const VariantBrick kSceneTequilasunriseBrick {"_scene_tequilasunrise_01",
                                              "assembly:/_pro/scenes/missions/paris/_scene_tequilasunrise_01.brick",
                                              0x005CE2DAE930DFFEull};
// The Sensation (pulls in tod_sunset, which Freelancer Paris already loads).
const VariantBrick kSceneShandyBrick {"_scene_shandy", "assembly:/_pro/scenes/missions/paris/_scene_shandy.brick",
                                      0x00B7EDF53810976Dull};
// The Black Hat (with dressing_mission_peacock).
const VariantBrick kSceneKamikazeBrick {"_scene_kamikaze", "assembly:/_pro/scenes/missions/paris/_scene_kamikaze.brick",
                                        0x005160675062E009ull};
// The Pharmacist.
const VariantBrick kSceneKirroyalBrick {"_scene_kirroyal", "assembly:/_pro/scenes/missions/paris/_scene_kirroyal.brick",
                                        0x001E179617DCEA6Aull};
// The Paparazzo.
const VariantBrick kSceneMojitoBrick {"_scene_mojito", "assembly:/_pro/scenes/missions/paris/_scene_mojito.brick",
                                      0x0093C5F22C30E670ull};
// The Chef.
const VariantBrick kSceneSakiniBrick {"_scene_sakini", "assembly:/_pro/scenes/missions/paris/_scene_sakini.brick",
                                      0x00DE7EC945059212ull};
// The Broker.
const VariantBrick kSceneMargaritaBrick {
    "_scene_margarita_01", "assembly:/_pro/scenes/missions/paris/_scene_margarita_01.brick", 0x00E3E62F71655BFDull};
// The Identity Thief.
const VariantBrick kSceneWhiterussianBrick {"_scene_whiterussian_01",
                                            "assembly:/_pro/scenes/missions/paris/_scene_whiterussian_01.brick",
                                            0x001E4B06765CD765ull};
// The Blackmailer.
const VariantBrick kSceneLumumbaBrick {"_scene_lumumba", "assembly:/_pro/scenes/missions/paris/_scene_lumumba.brick",
                                       0x0082309476D28A47ull};
// The Sarajevo Six: The Director.
const VariantBrick kSceneBlueblazerBrick {
    "_scene_ts6_blueblazer", "assembly:/_pro/scenes/missions/paris/_scene_ts6_blueblazer.brick", 0x007A613C8FFFE1FAull};
// Sapienza (on the day mission01): The Congressman.
const VariantBrick kMissionMintjulepBrick {
    "mission_mintjulep", "assembly:/_pro/scenes/missions/coastaltown/mission_mintjulep.brick", 0x00283E10AED14D88ull};
// The Guru.
const VariantBrick kMissionHarveywallbangerBrick {
    "mission_harveywallbanger", "assembly:/_pro/scenes/missions/coastaltown/mission_harveywallbanger.brick",
    0x003C73877802B873ull};
// The Prince.
const VariantBrick kMissionSazeracBrick {
    "mission_sazerac", "assembly:/_pro/scenes/missions/coastaltown/mission_sazerac.brick", 0x00D10639A07CB17Bull};
// The Twin.
const VariantBrick kMissionPiscosourBrick {
    "mission_piscosour", "assembly:/_pro/scenes/missions/coastaltown/mission_piscosour.brick", 0x00250B6924886D42ull};
// The Wildcard.
const VariantBrick kMissionQuadruplerumandcokeBrick {
    "mission_quadruplerumandcoke", "assembly:/_pro/scenes/missions/coastaltown/mission_quadruplerumandcoke.brick",
    0x005FA8CC8FFFF142ull};
// The Deceivers: a 3 KB wrapper that loads The Congressman's and The Guru's
// layers together (so both also count as loaded, see LoadedVariants).
const VariantBrick kScenarioCorpsereviverBrick {
    "scenario_corpsereviver", "assembly:/_pro/scenes/missions/coastaltown/scenario_corpsereviver.brick",
    0x003683714A5AABDFull};
// The Sarajevo Six: The Enforcer.
const VariantBrick kMissionMimosaBrick {
    "mission_ts6_mimosa", "assembly:/_pro/scenes/missions/coastaltown/mission_ts6_mimosa.brick", 0x0051886D74627B62ull};
// Marrakesh (on mission_marrakesh_spider, the day market): The Fixer.
const VariantBrick kMissionCosmopolitanBrick {
    "mission_marrakesh_cosmopolitan", "assembly:/_pro/scenes/missions/marrakesh/mission_marrakesh_cosmopolitan.brick",
    0x007FE9B3470D4D35ull};
// The Gunrunner.
const VariantBrick kMissionScrewdriverBrick {
    "mission_marrakesh_screwdriver", "assembly:/_pro/scenes/missions/marrakesh/mission_marrakesh_screwdriver.brick",
    0x0071AFFF5B386C90ull};
// The Angel of Death.
const VariantBrick kMissionHottoddyBrick {"mission_marrakesh_hottoddy",
                                          "assembly:/_pro/scenes/missions/marrakesh/mission_marrakesh_hottoddy.brick",
                                          0x00EC481C87FD55BEull};
// The Sarajevo Six: The Extractor.
const VariantBrick kMissionGinfizzBrick {"mission_marrakesh_ts6_ginfizz",
                                         "assembly:/_pro/scenes/missions/marrakesh/mission_marrakesh_ts6_ginfizz.brick",
                                         0x005FB5E6FC3627D9ull};
// Bangkok (on mission_tiger): The Warlord (with some hotel dressing bricks).
const VariantBrick kMissionBloodymaryBrick {"mission_bangkok_bloodymary",
                                            "assembly:/_pro/scenes/missions/bangkok/mission_bangkok_bloodymary.brick",
                                            0x006D1E9C109EA52Bull};
// The Food Critic.
const VariantBrick kMissionMartiniBrick {"mission_bangkok_martini",
                                         "assembly:/_pro/scenes/missions/bangkok/mission_bangkok_martini.brick",
                                         0x00FED13C7A6E0F35ull};
// The Ex-Dictator.
const VariantBrick kMissionBrassmonkeyBrick {"mission_bangkok_brassmonkey",
                                             "assembly:/_pro/scenes/missions/bangkok/mission_bangkok_brassmonkey.brick",
                                             0x000527E8ABB28124ull};
// The Sarajevo Six: The Veteran.
const VariantBrick kMissionGimletBrick {
    "mission_ts6_gimlet", "assembly:/_pro/scenes/missions/bangkok/mission_ts6_gimlet.brick", 0x0080AD2B9A4587B8ull};
// Colorado (on mission_bull): The Bookkeeper.
const VariantBrick kSceneFlirtiniBrick {
    "_scene_flirtini", "assembly:/_pro/scenes/missions/colorado_2/_scene_flirtini.brick", 0x00A9AFAAA3F58E3Full};
// The Chameleon.
const VariantBrick kSceneBushwackerBrick {
    "_scene_bushwacker", "assembly:/_pro/scenes/missions/colorado_2/_scene_bushwacker.brick", 0x00C0AF9F72488793ull};
// The Sarajevo Six: The Mercenary.
const VariantBrick kSceneBlackrussianBrick {"_scene_ts6_blackrussian",
                                            "assembly:/_pro/scenes/missions/colorado_2/_scene_ts6_blackrussian.brick",
                                            0x00E7DDB770DEAB94ull};
// Hokkaido (on mission_snowcrane): The Surgeons.
const VariantBrick kSceneMoscowmuleBrick {
    "_scene_moscowmule", "assembly:/_pro/scenes/missions/hokkaido/_scene_moscowmule.brick", 0x004AB3210BCA326Dull};
// The Fugitive.
const VariantBrick kSceneDirtyoctopusBrick {
    "_scene_dirtyoctopus", "assembly:/_pro/scenes/missions/hokkaido/_scene_dirtyoctopus.brick", 0x00E77D670A41F353ull};
// The Sarajevo Six: The Controller.
const VariantBrick kMissionSeabreezeBrick {
    "mission_hokkaido_ts6_seabreeze", "assembly:/_pro/scenes/missions/hokkaido/mission_hokkaido_ts6_seabreeze.brick",
    0x004E9471916310B7ull};
// Heavy layers: full elusive target scenarios over the base
// Freelancer loads, each with its own navmesh (and crowds and story), which
// takes over from the Freelancer scene's.
// The Ascensionist (Dubai): over scenario_gecko, 615 KB, its own navmesh
// (scene_gibson.navp); in chunk0, always mounted.
const VariantBrick kScenarioGibsonBrick {"scenario_gibson",
                                         "assembly:/_pro/scenes/missions/golden/mission_gecko/scenario_gibson.brick",
                                         0x0059E08EB59485C9ull};
// The Undying (Miami): over location only, 643 KB, its own navmesh and AI
// grid (scene_et_sambuca).
const VariantBrick kMissionSambucaBrick {
    "mission_sambuca", "assembly:/_pro/scenes/missions/miami/mission_sambuca.brick", 0x00D7DD7E8B8E3723ull};
// The Undying Returns (Miami): the one brick its contract loads on
// scene_et_sambuca, a 562-byte wrapper (chunk19) whose only references are a
// spatial root and mission_sambuca: The Undying's layer with whatever the
// wrapper sets on it. Its TEMP is not decoded.
const VariantBrick kScenarioSambuca2Brick {
    "scenario_sambuca2", "assembly:/_pro/scenes/missions/miami/scenario_sambuca2.brick", 0x00BE05F31F56D254ull};
// More heavy layers: the rest of the official versions on a
// Freelancer map whose scene is the map's base plus layer bricks. Each option
// is the bricks its official scene lists beyond what the Freelancer scene
// already loads (as for The Source), never the contract's own mission brick
// (the target's objectives; IOI's Elusive Target Arcade plays every one of
// these scenes without it too) and never an escalation's level bricks. Each
// has its own navmesh, which takes over from the Freelancer scene's. All
// installed (rpkg metadata, offline: none deleted by a later patch).
// The Banker (Paris): over mission_fashionshowhit_01, 1292 KB, its own
// navmesh and AI grid (_scene_frenchmartini), the palace's crowd reworked.
const VariantBrick kScenarioFrenchmartiniBrick {"scenario_frenchmartini",
                                                "assembly:/_pro/scenes/missions/paris/scenario_frenchmartini.brick",
                                                0x00250CEFFB630F13ull};
// The Infiltrator (Bangkok): over mission_tiger, 2119 KB, its own navmesh and
// AI grid (scene_baiju), about 20 dressing and NPC bricks of its own. Its
// evening time of day (tod_summer_evening_clear) is already in Freelancer
// Bangkok. Without mission_baiju (the contract's).
const VariantBrick kScenarioBaijuBrick {"scenario_baiju", "assembly:/_pro/scenes/missions/bangkok/scenario_baiju.brick",
                                        0x00DB3D0A73A939C2ull};
// The Wizard (Isle of Sgail): over scenario_magpie, 880 KB, its own navmesh
// and AI grid (scene_pomada), plus its sound brick, as scene_pomada lists them.
const VariantBrick kScenarioPomadaBrick {
    "scenario_pomada", "assembly:/_pro/scenes/missions/theark/scenario_pomada.brick", 0x0089842EA8B5B664ull};
const VariantBrick kElusivePomadaSoundBrick {
    "elusive_pomada_sound", "assembly:/_pro/scenes/missions/theark/elusive_pomada_sound.brick", 0x00F712178D2DBF09ull};
// The Splitter (Chongqing): over scenario_rat, 1465 KB, its own navmesh
// (scene_wet_lambic), plus its sound brick (scene_wet_lambic's two).
// mission_lambic is an empty stub. Deletes about 20 Freelancer pool actors.
const VariantBrick kScenarioLambicBrick {"scenario_lambic", "assembly:/_pro/scenes/missions/wet/scenario_lambic.brick",
                                         0x00BB7C63E0B615F9ull};
const VariantBrick kSoundLambicBrick {"sound_lambic", "assembly:/_pro/scenes/missions/wet/sound_lambic.brick",
                                      0x00ACE30E41E98DDCull};
// The Harbinger in full (Dartmoor): over scenario_bulldog, 3107 KB, its own
// navmesh and AI grid (scene_filur); it loads scenario_filur_ea (the
// harbinger option's dressing) itself. Without mission_filur (the contract's).
const VariantBrick kScenarioFilurBrick {
    "scenario_filur", "assembly:/_pro/scenes/missions/ancestral/scenario_filur.brick", 0x00DC692646C00776ull};
// The Herbalist in full (Haven): over scenario_stingray, 4430 KB, its own
// navmesh and AI grid (scene_greentini), with the dressing and lights
// scene_greentini lists next to it. Without mission_greentini (the contract's).
const VariantBrick kScenarioGreentiniBrick {
    "scenario_greentini", "assembly:/_pro/scenes/missions/opulent/scenario_greentini.brick", 0x009CE6A414D90A40ull};
// The Reflection in full (Hokkaido): over mission_snowcrane, 2058 KB, its own
// navmesh and AI grid (scene_bellini); it loads scenario_bellini_ea (the
// reflection option's dressing), override_norfidreaders and its twilight
// (tod_winter_twilight_clear, already in Freelancer Hokkaido) itself.
// Without mission_bellini (the contract's).
const VariantBrick kScenarioBelliniBrick {
    "scenario_bellini", "assembly:/_pro/scenes/missions/hokkaido/scenario_bellini.brick", 0x002DAF48CF0FD7E6ull};
// HITMAN 2's special assignments. Illusions of Grandeur (Mumbai): over
// scenario_mongoose, 671 KB, its own navmesh (scene_kingcobra); it loads
// override_blockluxury (closes an area) itself; scene_kingcobra also lists
// its outfits brick.
const VariantBrick kScenarioKingcobraBrick {
    "scenario_kingcobra", "assembly:/_pro/scenes/missions/mumbai/scenario_kingcobra.brick", 0x00EFFE23BA968693ull};
const VariantBrick kOutfitsKingcobraBrick {
    "outfits_kingcobra", "assembly:/_pro/scenes/missions/mumbai/outfits_kingcobra.brick", 0x00EC26C1DF84A092ull};
// A Bitter Pill (Whittleton Creek): over scenario_skunk, 843 KB, its own
// navmesh (scene_gartersnake); it loads override_nohouseforsale itself.
const VariantBrick kScenarioGartersnakeBrick {
    "scenario_gartersnake", "assembly:/_pro/scenes/missions/skunk/scenario_gartersnake.brick", 0x00794561315ED1EDull};
// Embrace of the Serpent (Santa Fortuna): over scenario_hippo, 356 KB, its own
// navmesh (scene_anaconda).
const VariantBrick kScenarioAnacondaBrick {
    "scenario_anaconda", "assembly:/_pro/scenes/missions/colombia/scenario_anaconda.brick", 0x008AB510D55AF5A3ull};
// A Silver Tongue (Miami): over location, 565 KB, its own navmesh
// (scene_cottonmouth).
const VariantBrick kScenarioCottonmouthBrick {
    "scenario_cottonmouth", "assembly:/_pro/scenes/missions/miami/scenario_cottonmouth.brick", 0x0076DCA438306FDAull};
// The Seven Deadly Sins escalations (HITMAN 3): each escalation's shared
// scenario brick, without its level bricks and without the location's Diana
// commentary brick its scene also lists. Gluttony (Chongqing): over
// scenario_rat, 4007 KB, its own navmesh (scene_wet_azalea).
const VariantBrick kScenarioAzaleaBrick {"scenario_azalea", "assembly:/_pro/scenes/missions/wet/scenario_azalea.brick",
                                         0x00CAFC0C93568981ull};
// Envy (Mendoza): over scenario_llama, 774 KB, its own navmesh
// (scene_frangipani), which takes over from override_sapo's.
const VariantBrick kScenarioFrangipaniBrick {
    "scenario_frangipani", "assembly:/_pro/scenes/missions/elegant/scenario_frangipani.brick", 0x0000A4945EAFA2B3ull};
// Lust (Berlin): over scenario_fox, 706 KB, its own navmesh (scene_ambrosia).
const VariantBrick kScenarioAmbrosiaBrick {"scenario_ambrosia",
                                           "assembly:/_pro/scenes/missions/edgy/mission_fox/scenario_ambrosia.brick",
                                           0x00D47F2B1CBC7BEBull};
// Sloth (Dartmoor): over scenario_bulldog, 1144 KB, its own navmesh
// (scene_ancestral_harebell), plus the Whittleton Creek outfits its scene lists
// (chunk1patch4).
const VariantBrick kAncestralHarebellBrick {
    "ancestral_harebell", "assembly:/_pro/scenes/missions/ancestral/ancestral_harebell.brick", 0x002F7D8B6D31F6E0ull};
const VariantBrick kOutfitsSkunkBrick {"outfits_skunk", "assembly:/_pro/scenes/missions/skunk/outfits_skunk.brick",
                                       0x00D98058A63761D7ull};
// Removed: Wrath (Dartmoor, ancestral_hollyhock, as wrath): appended onto
// Freelancer's scene it brought only its own 23 NPCs (about 6 in sight at the
// start), and its suspects T-posed and flew through walls.
// Not added: Pride (Chongqing) and Greed (Dubai). Their scenes are built the
// same way (scene_wet_makoyana: scenario_rat, commentary_rat and
// scenario_makoyana; scene_gecko_lunaria: scenario_gecko, commentary_gecko
// and golden_lunaria; the contracts add the level bricks), but their shared
// bricks hold no set: scenario_makoyana is 15 KB (a hero spawn, a few acts
// and drama situations, its navmesh) and golden_lunaria 59 KB (the coin
// pickups and their effects, a hero spawn, a Diana line, its navmesh). The
// escalations' sets are in their level bricks (137-146 KB and 83-92 KB
// each), which no option loads.
// Removed: the Easter egg hunt (Berlin, scenario_grasssnake, as egg_hunt): 47
// started inside the warehouse it seals off, and Berlin's showdown offered it
// too, where it cannot work.
// Removed: The Vector (Colorado, scenario_rabies with or_sound_rabies and
// lighting_day, as the_vector): in game it loaded the Sniper Assassin mode (a
// locked weapon, no map, a "{0}/{1} targets" HUD), not a Freelancer mission.

// Never targets: an option's own story targets (an elusive target, a Sarajevo
// Six target, a bonus mission's or special assignment's targets) stay in the
// scene, but Freelancer's target pool never uses them (the showdown censuses put The Appraiser herself, Miranda Jamison, and The
// Stowaway's Jimmy Chen in the mover rows). The game does not mark them: it
// never loads their contract, and every census reads their contract-target
// bits false. So each option lists its official contract's targets: the
// repository ids of the contract's kill objectives (a Kill event's
// RepositoryId, or a kill objective's Context.Targets), as Peacock's copy of
// the official contracts has them (contractdata/<map>/[ELUSIVE/]<contract>.json,
// named on each option as "Never targets: <map>/.../<contract>"). The names are
// the ones a census or Diana's lines gave (scenario_pomada's "Is Penicillin
// Dead by Punch", elusive_skittlebomb's "DianaSpeak ID Jimmy Chen",
// elusive_goldendoublet's "Is Jamison Alive"); "the target" where no log has
// seen it yet. Options without such a list: lighting only, the full versions
// (their never-targets are in core/LandslideFull.h) and the escalations
// (their targets change by level).
// The Source in full: the bricks scene_zika lists beyond Freelancer Bangkok
// (mission_tiger is already there), in its order. mission_zika brings the
// rest itself (outfits_zika, npc_ambient_leftwing_cleaners and four sound and
// outfit bricks are its install references, never listed by scene_zika).
// Never targets: BANGKOK/THESOURCE.
const VariantOption kTheSource {"the_source",
                                {&kMissionZikaBrick, &kTodZikaNightBrick, &kChallengesZikaBrick},
                                30,
                                true,
                                {"010e451b-99de-4a73-a6df-520158636654",   // Oybek Nabazov
                                 "893ecd0c-9a13-42a6-b3f5-72c6059d4b16"}}; // Sister Yulduz
// The options with a full version (core/LandslideFull.h: Patient Zero, The
// Author, Landslide, The Icon, The Bad Boy, Marrakesh's night, The
// Entertainer) are offered on their mild map only, and a pick always loads
// the full version (the normal map when its checks fail): their bricks are
// never appended. They are the bricks the official scene loads, which tell
// the mover table which variant is loaded (LoadedVariants).
// Patient Zero's lighting, colors and quarantine dressing; the full version
// loads the outbreak's own scene (_scene_flu, with mission_flu's people).
const VariantOption kPatientZero {"patient_zero", {&kTodFluBrick, &kGfxFluBrick, &kEaFluBrick}, 25, true};
const VariantOption kTodEbolaNight {"tod_ebola_night", {&kTodEbolaNightBrick}, 15, false};

namespace {

// Measured in game: under Landslide's stage in at least a quarter of the
// samples, every load, at the same spots (a cafe group of the harbor square).
constexpr std::array<StrandedNpc, kLandslideStageSitterCount> kSitters {{
    {"Alice Biondi", "4adc1eee-47f2-4d71-a28a-9d46287f2e4f", kNpcAmbientBeachsquareDayRid,
     NpcSpot {-19.61f, 29.51f, 1.44f}},
    {"Greta Palerma", "d3e75ec0-08e5-4f01-a4a6-2fe1d72882aa", kNpcAmbientBeachsquareDayRid,
     NpcSpot {-21.14f, 26.10f, 1.34f}},
    {"Dimitri Napolitani", "9cff47b0-c5b5-479f-a10a-aa5fcc7b4ed2", kNpcAmbientBeachsquareDayRid,
     NpcSpot {-20.85f, 29.05f, 1.34f}},
    {"Sergio Centomo", "47e0984c-3933-443c-af0e-ce2311337a1d", kNpcAmbientBeachsquareDayRid,
     NpcSpot {-20.28f, 24.45f, 1.34f}},
    {"Umberto Megazzini", "bb15423f-bc60-4c49-ae0b-ee0a8843cbcc", kNpcAmbientBeachsquareDayRid,
     NpcSpot {-18.24f, 26.43f, 1.37f}},
    {"Giuliano Paglia", "95e93ad0-d671-41bb-905e-9e6d074ab6e9", kNpcAmbientBeachsquareDayRid,
     NpcSpot {-20.77f, 27.82f, 1.34f}},
    {"Gil Scherzer", "9ba668f0-842c-457f-a999-20d3fb414fd0", kNpcAmbientBeachsquareDayRid,
     NpcSpot {-19.54f, 23.53f, 1.34f}},
    {"Francesco Vivaldi", "a1df1adf-9fe1-4122-b95f-d6e3b367fca5", kNpcAmbientBeachsquareDayRid,
     NpcSpot {-21.45f, 25.16f, 1.34f}},
    {"Beata De Salvatori", "f0051abe-690b-40d7-8406-c9650f534557", kNpcAmbientBeachsquareDayRid,
     NpcSpot {-21.21f, 24.16f, 1.34f}},
    {"Nadia Barese", "47c20717-ab6b-4d34-97b4-bba797650379", kNpcAmbientBeachsquareDayRid,
     NpcSpot {-20.29f, 21.32f, 1.34f}},
}};

} // namespace

std::span<const StrandedNpc> LandslideStageSitters() {
    return kSitters;
}

// Landslide pairs its lamps (lighting_mamba) with this morning time of day,
// plus its set dressing (story-free; checked, for its location deletes). The
// day NPCs whose act spot is under its stage are removed (switched off, never
// targets).
const VariantOption kLandslide {"landslide",
                                {&kLightingMambaBrick, &kTodSummerMorningClearBrick, &kDressingBeachMambaBrick,
                                 &kDressingChurchMambaBrick, &kDressingOldtownMambaBrick, &kDressingTownhallMambaBrick,
                                 &kDressingTunnelsMambaBrick},
                                20,
                                true,
                                {},
                                {LandslideStageSitters().begin(), LandslideStageSitters().end()}};
// The Icon's night: mission_copperhead references tod_summer_night_clear and
// lighting_copperhead (in that order) and no day ToD.
const VariantOption kLightingCopperhead {
    "lighting_copperhead", {&kTodSummerNightClearBrick, &kLightingCopperheadBrick}, 10, false};
// The Bad Boy (Bartholomew Argus on The Icon's set, an elusive target
// mission): its scene is The Icon's (the same bricks); the full version is
// the full Icon with his own brick (mission_caipirinha) appended
// (core/LandslideFull.h). Added last on Sapienza, so the other options roll
// as before.
const VariantOption kTheBadBoy {"the_bad_boy", {&kTodSummerNightClearBrick, &kLightingCopperheadBrick}, 10, false};
// Holiday Hoarders is _scene_paris (all inside Freelancer Paris) plus this one
// brick; it brings noel_outfits and ow_sound_noel itself.
// Never targets: PARIS/HOLIDAYHOARDERS.
const VariantOption kHolidayHoarders {"holiday_hoarders",
                                      {&kLocationParisNoelBrick},
                                      15,
                                      true,
                                      {"21017f2e-3090-4b41-91b9-1ec4ddd358f9",   // Bagnato
                                       "a713a8a2-e203-4032-9c4a-1f8eab4c3efa"}}; // Gonif
// Dressing layers: each one official layer (two for Haven) over the base brick the
// Freelancer scene already loads; all checked.
// Never targets: HOKKAIDO/HOKKAIDOSNOWFESTIVAL.
const VariantOption kSnowFestival {
    "snow_festival", {&kScenarioMamushiBrick}, 30, true, {"5087ab4a-586c-4967-967c-487cc2d17c92"}}; // Dmitri Fedorov
// Its location deletes leave two guards by the cable car exit standing on
// nothing, out of reach: Hidaka Uno and Shoichi Kataoka,
// named by the Kill events of the two the player shot there (positions
// where they died). Both are in Hokkaido's scene
// NPC brick 009AAFAC5EA2ABC8 in every log naming them. Removed (switched off,
// never targets).
// The Reflection in full: scenario_bellini (it loads scenario_bellini_ea, the
// diner and stage, itself), with its guests and story.
// Never targets: HOKKAIDO/ELUSIVE/THEREFLECTION.
const VariantOption kReflection {
    "reflection",
    {&kScenarioBelliniBrick},
    5,
    true,
    {"851043ee-a2a3-48f2-a5e2-b7e13a28131c"}, // "Slim Shady" (its census name)
    {{"Hidaka Uno", "ecbe94ac-a5b3-4d80-8c54-140696707168", 0x009AAFAC5EA2ABC8ull, NpcSpot {-64.16f, -61.09f, -9.17f}},
     {"Shoichi Kataoka", "217286a9-beb9-4a7c-8473-775ab4e9d60e", 0x009AAFAC5EA2ABC8ull,
      NpcSpot {-63.99f, -60.41f, -9.17f}}}};
// The Herbalist in full: scenario_greentini with its dressing and lights, as
// scene_greentini lists them.
// Never targets: HAVEN/ELUSIVE/THEHERBALIST.
const VariantOption kHerbalistParty {
    "herbalist_party",
    {&kScenarioGreentiniBrick, &kScenarioGreentiniEaBrick, &kScenarioGreentiniLightingBrick},
    55,
    true,
    {"36e7a631-e72b-4dfa-a0f8-574422b919f9"}}; // the target
// Never targets: CHONGQING/ELUSIVE/THERAGE.
const VariantOption kTheRage {
    "the_rage", {&kElusiveRedsnapperBrick}, 35, true, {"2981aaba-0a8d-4895-9456-976fc437bee5"}}; // Sully Bowden
// Removed: Colorado by day alone (colorado_day, lighting_day appended; The
// Vector, also removed, loaded it too). It was first on Colorado: its 0 %
// default share moved nothing, so the other Colorado options roll as before
// at defaults.
// The Harbinger in full: scenario_filur (it loads scenario_filur_ea, the lab
// and party dressing, itself).
// Never targets: DARTMOOR/ELUSIVE/THEHARBINGER.
const VariantOption kHarbinger {
    "harbinger", {&kScenarioFilurBrick}, 30, true, {"4a9a11bf-e93d-4cdc-9852-4f3a5b9812f3"}}; // the target
// Never targets: BERLIN/ELUSIVE/THEDROP.
const VariantOption kTheDrop {
    "the_drop", {&kElusiveTomorrowlandBrick}, 35, true, {"e1258f2b-1693-44a4-99a4-90c3dea952d1"}}; // Alexios Laskaridis
// Never targets: WHITTLETON/ELUSIVE/THESERIALKILLER.
const VariantOption kSerialKiller {"serial_killer",
                                   {&kElusiveAlabamaslammerBrick},
                                   40,
                                   true,
                                   {"d21efe38-c577-49bf-95f0-e72891f004bb"}}; // Joseph Gorski
// Elusive targets: one layer each; all checked.
// Never targets: DARTMOOR/ELUSIVE/THEPROCURERS.
const VariantOption kProcurers {"procurers",
                                {&kElusiveVesperBrick},
                                10,
                                true,
                                {"df7a1d47-6e8e-4609-af56-9142347be325",   // The Chef
                                 "61350e1a-f853-4ce0-9128-743ece3daf52"}}; // The Embalmer
// Never targets: DARTMOOR/ELUSIVE/THECOLLECTOR.
const VariantOption kCollector {
    "collector", {&kElusiveBrambleBrick}, 10, true, {"6240a1a1-f5ef-43b9-8665-7829fb0c0b52"}}; // The Collector
// Never targets: BERLIN/ELUSIVE/THELIABILITY.
const VariantOption kLiability {
    "liability", {&kElusiveRadlerBrick}, 20, true, {"9d1c5d01-a793-4c0a-875b-3b48ad584537"}}; // Terrence Chesterfield
// Never targets: MENDOZA/ELUSIVE/THEHEARTBREAKER.
const VariantOption kHeartbreaker {
    "heartbreaker", {&kElusiveClericoBrick}, 30, true, {"4085a2b5-87dd-4286-a04a-5698d9812d16"}}; // The Wedding Planner
// Never targets: MENDOZA/ELUSIVE/THEICONOCLAST.
const VariantOption kIconoclast {
    "iconoclast", {&kElusiveJockeyclubBrick}, 25, true, {"16487ed1-f857-411f-a239-88884c884972"}}; // the target
// Never targets: SGAIL/ELUSIVE/THEAPPRAISER.
const VariantOption kAppraiser {
    "appraiser", {&kElusiveGoldendoubletBrick}, 25, true, {"4c7c489d-3992-41ad-b2af-df3b3c431b53"}}; // Miranda Jamison
// Never targets: SGAIL/ELUSIVE/THESTOWAWAY.
const VariantOption kStowaway {
    "stowaway", {&kElusiveSkittlebombBrick}, 15, true, {"0808d791-d0ba-4c71-880f-a31c88ad69d9"}}; // Jimmy Chen
// Never targets: SANTAFORTUNA/ELUSIVE/THEREVOLUTIONARY.
const VariantOption kRevolutionary {
    "revolutionary", {&kElusiveHighballBrick}, 35, true, {"d339202b-5282-4bb9-9be6-d3335311be1e"}}; // the target
// One official layer each; all checked.
const VariantOption kGardenShow {"garden_show", {&kScenarioSmoothsnakeBrick}, 30, true};
// Never targets: PARIS/ELUSIVE/THEFORGER.
const VariantOption kTheForger {
    "the_forger", {&kSceneTequilasunriseBrick}, 5, true, {"46b78498-b470-4aad-b72b-d90f3126a7f6"}}; // the target
// Never targets: PARIS/ELUSIVE/THESENSATION.
const VariantOption kTheSensation {
    "the_sensation", {&kSceneShandyBrick}, 5, true, {"e30acbd5-cb3b-4691-a919-d1cf0ccf7fc4"}}; // the target
// Never targets: PARIS/ELUSIVE/THEBLACKHAT.
const VariantOption kTheBlackHat {
    "the_black_hat", {&kSceneKamikazeBrick}, 5, true, {"1b21e6e6-beba-4f7e-8760-e9604a69c569"}}; // the target
// Never targets: PARIS/ELUSIVE/THEPHARMACIST.
const VariantOption kThePharmacist {
    "the_pharmacist", {&kSceneKirroyalBrick}, 5, true, {"124c40d5-b342-4288-b202-57e80148e0d7"}}; // the target
// Never targets: PARIS/ELUSIVE/THEPAPARAZZO.
const VariantOption kThePaparazzo {
    "the_paparazzo", {&kSceneMojitoBrick}, 5, true, {"fc036cf1-7c6c-4418-b821-593628b67826"}}; // the target
// Never targets: PARIS/ELUSIVE/THECHEF.
const VariantOption kTheChef {
    "the_chef", {&kSceneSakiniBrick}, 5, true, {"7635712b-ebe1-40a4-a947-452fa6820cfc"}}; // the target
// Never targets: PARIS/ELUSIVE/THEBROKER.
const VariantOption kTheBroker {
    "the_broker", {&kSceneMargaritaBrick}, 5, true, {"b1db6854-b82b-4bb4-8e73-a9c138a283d2"}}; // the target
// Never targets: PARIS/ELUSIVE/THEIDENTITYTHIEF.
const VariantOption kTheIdentityThief {
    "the_identity_thief", {&kSceneWhiterussianBrick}, 5, true, {"e44ef736-8bba-4d58-8136-c1aca6b74d71"}}; // the target
// Never targets: PARIS/ELUSIVE/THEBLACKMAILER.
const VariantOption kTheBlackmailer {
    "the_blackmailer", {&kSceneLumumbaBrick}, 5, true, {"2c11757e-905e-44e7-8115-81e6a5a72ee2"}}; // the target
// Never targets: PARIS/THEDIRECTOR.
const VariantOption kTheDirector {
    "the_director", {&kSceneBlueblazerBrick}, 5, true, {"65c6eafe-a9a1-42d9-99f8-5796507117e0"}}; // the target
// Never targets: SAPIENZA/ELUSIVE/THECONGRESSMAN.
const VariantOption kTheCongressman {
    "the_congressman", {&kMissionMintjulepBrick}, 5, true, {"3abdf4fd-4db9-4276-8a1a-6b6e119bedae"}}; // the target
// Never targets: SAPIENZA/ELUSIVE/THEGURU.
const VariantOption kTheGuru {"the_guru",
                              {&kMissionHarveywallbangerBrick},
                              5,
                              true,
                              {"d749b0f2-d1f0-47d5-bab9-10e3ff4dac8b",   // the target
                               "8c2dbfaf-d9f5-4343-9bf5-f8092f22da6b",   // pacify or kill
                               "d555c808-a97d-4094-ae90-d684dd68e936"}}; // pacify or kill
// Never targets: SAPIENZA/ELUSIVE/THEPRINCE.
const VariantOption kThePrince {
    "the_prince", {&kMissionSazeracBrick}, 10, true, {"89fafd79-9c9f-4a0d-a4a1-13c2ffdcf039"}}; // the target
// Never targets: SAPIENZA/ELUSIVE/THETWIN.
const VariantOption kTheTwin {"the_twin",
                              {&kMissionPiscosourBrick},
                              5,
                              true,
                              {"97b05a61-84d6-4b22-8dcc-3658986e1758",   // the target
                               "168c6da4-c583-47f4-9435-a42d9ea22546"}}; // pacify or kill
// Never targets: SAPIENZA/ELUSIVE/THEWILDCARD.
const VariantOption kTheWildcard {"the_wildcard",
                                  {&kMissionQuadruplerumandcokeBrick},
                                  5,
                                  true,
                                  {"fddd444a-e9a5-4650-96ff-029df224bb61"}}; // the target
// Never targets: SAPIENZA/ELUSIVE/THEDECIEVERS.
const VariantOption kTheDeceivers {"the_deceivers",
                                   {&kScenarioCorpsereviverBrick},
                                   5,
                                   true,
                                   {"a5b1f605-7767-4db4-a24b-a9191a10f12d",   // a target
                                    "2da5acff-0025-4814-831f-30156a4ee38f"}}; // a target
// Never targets: SAPIENZA/THEENFORCER.
const VariantOption kTheEnforcer {
    "the_enforcer", {&kMissionMimosaBrick}, 5, true, {"752053dd-2ab7-4d49-897d-97c957f83083"}}; // the target
// Never targets: MARRAKESH/ELUSIVE/THEFIXER.
const VariantOption kTheFixer {
    "the_fixer", {&kMissionCosmopolitanBrick}, 10, true, {"38c1700e-0247-4c09-8985-d98573a39f24"}}; // the target
// Never targets: MARRAKESH/ELUSIVE/THEGUNRUNNER.
const VariantOption kTheGunrunner {
    "the_gunrunner", {&kMissionScrewdriverBrick}, 25, true, {"905e7cd5-1dd9-4ce3-bec2-a0120b921308"}}; // the target
// Never targets: MARRAKESH/ELUSIVE/THEANGELOFDEATH.
const VariantOption kTheAngelOfDeath {
    "the_angel_of_death", {&kMissionHottoddyBrick}, 10, true, {"50cea951-bb99-431d-aa22-9a31afcaec59"}}; // the target
// Never targets: MARRAKESH/THEEXTRACTOR.
const VariantOption kTheExtractor {
    "the_extractor", {&kMissionGinfizzBrick}, 10, true, {"756f6462-9c1f-460e-bbf4-331f2916508a"}}; // the target
// Removed: Marrakesh at night alone (marrakesh_night, the night level without
// The Entertainer). Its full version, the full Marrakesh night, stays as the
// full Entertainer's base (core/LandslideFull.h).
// The Entertainer (an elusive target mission on the night level, _scene_python,
// A House Built on Sand's base, which loads Marrakesh's own night time of
// day): the full version is the full Marrakesh night with his own brick
// (mission_marrakesh_maitai) appended (core/LandslideFull.h). Added last on
// Marrakesh, so the other options roll as before. The Brothers, the other elusive target mission
// on the night level, cannot be added: chunk25patch1 deletes its brick
// (mission_marrakesh_negroni) and its outfits from the game.
const VariantOption kTheEntertainer {"the_entertainer", {&kTodSummerNightClearMarrakeshBrick}, 35};
// Never targets: BANGKOK/ELUSIVE/THEWARLORD.
const VariantOption kTheWarlord {
    "the_warlord", {&kMissionBloodymaryBrick}, 10, true, {"3a26a487-5f3b-440a-a55d-13a136eb1bea"}}; // the target
// Never targets: BANGKOK/ELUSIVE/THEFOODCRITIC.
const VariantOption kTheFoodCritic {
    "the_food_critic", {&kMissionMartiniBrick}, 5, true, {"c4502487-26fb-4b6c-8760-3cc63ac873ec"}}; // the target
// Never targets: BANGKOK/ELUSIVE/THEEXDICTATOR.
const VariantOption kTheExDictator {"the_ex_dictator",
                                    {&kMissionBrassmonkeyBrick},
                                    20,
                                    true,
                                    {"7387e648-ad6b-408d-a0ee-3b3943767e78",   // a target
                                     "978ad630-8d31-4416-8976-8ed1009a4dbd"}}; // a target
// Never targets: BANGKOK/THEVETERAN.
const VariantOption kTheVeteran {
    "the_veteran", {&kMissionGimletBrick}, 5, true, {"731db58c-ec2f-40e1-92a1-6c367683b7d9"}}; // the target
// Never targets: COLORADO/ELUSIVE/THEBOOKKEEPER.
const VariantOption kTheBookkeeper {
    "the_bookkeeper", {&kSceneFlirtiniBrick}, 25, true, {"229d24be-d95a-4c73-a1be-afac4a05a5e3"}}; // the target
// Never targets: COLORADO/ELUSIVE/THECHAMELEON.
const VariantOption kTheChameleon {
    "the_chameleon", {&kSceneBushwackerBrick}, 20, true, {"441209db-35ca-46e8-bb99-b9c3f8e8783c"}}; // the target
// Never targets: COLORADO/THEMERCENARY.
const VariantOption kTheMercenary {
    "the_mercenary", {&kSceneBlackrussianBrick}, 35, true, {"abb0c23f-a38a-4492-8a16-4e3c7c438b70"}}; // the target
// Never targets: HOKKAIDO/ELUSIVE/THESURGEONS.
const VariantOption kTheSurgeons {"the_surgeons",
                                  {&kSceneMoscowmuleBrick},
                                  10,
                                  true,
                                  {"f4eb50bc-1a58-4487-a568-de5ffe19b7ff",   // Pavel Frydel
                                   "88836cf0-852e-4244-9aa3-90174467b5b1"}}; // Akane Akenawa
// Never targets: HOKKAIDO/ELUSIVE/THEFUGITIVE (the target is picked at runtime among three "Ji-hu").
const VariantOption kTheFugitive {"the_fugitive",
                                  {&kSceneDirtyoctopusBrick},
                                  10,
                                  true,
                                  {"2f47716a-2663-461b-bd8c-2dfdccc720c6",   // Ji-hu
                                   "6c9474f0-30eb-4bac-af97-2541a9173bab",   // Ji-hu
                                   "84049f2c-ba47-4d02-a5a2-c31aa2f2cd4f"}}; // Ji-hu
// Never targets: HOKKAIDO/THECONTROLLER.
const VariantOption kTheController {
    "the_controller", {&kMissionSeabreezeBrick}, 10, true, {"c04167bc-f321-4e0b-be51-0b5e662d2669"}}; // Taheji Koyama
// Never targets: DUBAI/ELUSIVE/THEASCENSIONIST.
const VariantOption kTheAscensionist {
    "the_ascensionist", {&kScenarioGibsonBrick}, 40, true, {"010a84f5-db4d-47e0-8f27-41e6dc7fe21f"}}; // the target
// Never targets: MIAMI/ELUSIVE/THEUNDYING.
const VariantOption kTheUndying {
    "the_undying", {&kMissionSambucaBrick}, 25, true, {"63283fb2-a653-486d-a505-e471efbc8c54"}}; // the target
// The Undying Returns: its wrapper loads mission_sambuca, so The Undying's
// never-target is out of the pool too (both count as loaded, as for The
// Deceivers). Added last on Miami, so the other options roll as before.
// Never targets: MIAMI/ELUSIVE/THEUNDYINGRETURNS.
const VariantOption kTheUndyingReturns {
    "the_undying_returns", {&kScenarioSambuca2Brick}, 5, true, {"0eda939e-db2b-4344-9c3f-9d00eb2fb1bc"}}; // the target
// Heavy layers, each its official scene's bricks beyond Freelancer's; all
// checked. The full Harbinger and Reflection load their dressing-only options' brick
// themselves, so the log also names those; the full Herbalist lists its own
// (as its scene does), so LoadedVariants keeps only the full one.
// Never targets: PARIS/ELUSIVE/THEBANKER.
const VariantOption kTheBanker {"the_banker",
                                {&kScenarioFrenchmartiniBrick},
                                30,
                                true,
                                {"54f583de-f261-4473-956c-dc278e5162e2",   // the target
                                 "8ea03ee0-7eac-4efa-8206-af4531cc00ee"}}; // the optional target
// Never targets: BANGKOK/ELUSIVE/THEINFILTRATOR.
const VariantOption kTheInfiltrator {"the_infiltrator",
                                     {&kScenarioBaijuBrick},
                                     25,
                                     true,
                                     {"923e2fec-4af8-41ae-a52b-b8cc74084b5e",   // the gang leader
                                      "4fff144f-b279-431b-abaa-aee85ff7a2d7",   // an assassin (Neutralise Threats)
                                      "4ef75b0f-bba5-4c4d-aa20-8fe32a92ace3",   // an assassin
                                      "de98b52c-895a-4058-b6a4-730b4887f34a"}}; // an assassin
// Never targets: SGAIL/ELUSIVE/THEWIZARD.
const VariantOption kTheWizard {"the_wizard",
                                {&kScenarioPomadaBrick, &kElusivePomadaSoundBrick},
                                35,
                                true,
                                {"fbe2c0d4-91df-4499-8799-9a9a66073247"}}; // Taylor Graves
// Never targets: CHONGQING/ELUSIVE/THESPLITTER (its secondary targets are added at runtime).
const VariantOption kTheSplitter {"the_splitter",
                                  {&kScenarioLambicBrick, &kSoundLambicBrick},
                                  25,
                                  true,
                                  {"9c2190fa-0095-4fa9-9f27-394e09c96bc5"}}; // Max Valliant
// Never targets: MUMBAI/ILLUSIONSOFGRANDEUR.
const VariantOption kIllusionsOfGrandeur {"illusions_of_grandeur",
                                          {&kScenarioKingcobraBrick, &kOutfitsKingcobraBrick},
                                          40,
                                          true,
                                          {"07ccfa5d-6dd3-4d2b-9f79-75b300524857"}}; // the target
// Never targets: WHITTLETON/ABITTERPILL.
const VariantOption kABitterPill {
    "a_bitter_pill", {&kScenarioGartersnakeBrick}, 30, true, {"3cf8ef68-e6f3-466a-9299-de50e9f0a9bb"}}; // the target
// Never targets: SANTAFORTUNA/EMBRACEOFTHESERPENT.
const VariantOption kEmbraceOfTheSerpent {"embrace_of_the_serpent",
                                          {&kScenarioAnacondaBrick},
                                          30,
                                          true,
                                          {"1e8bbf5a-473e-4b00-b58a-4706f13e497b"}}; // the target
// Never targets: MIAMI/ASILVERTONGUE.
const VariantOption kASilverTongue {
    "a_silver_tongue", {&kScenarioCottonmouthBrick}, 40, true, {"466b652d-cea0-47eb-823a-2aba9f67b5ad"}}; // the target
// Gluttony has a full version that appends (LandslideFull.h, GluttonyFull):
// after scenario_azalea, the package's fr_gluttony deletes the props of the
// set and of Freelancer's Chongqing that would stand in each other; a pick
// plays the normal map when its checks fail. So do nine more (their spot
// fixes, LandslideFull.h: reflection, revolutionary, embrace_of_the_serpent,
// garden_show, sloth, illusions_of_grandeur, the_undying,
// the_undying_returns, the_banker): after the option's bricks, the package's
// fr_<option> moves or re-points the territory spots the option spoils.
const VariantOption kGluttony {"gluttony", {&kScenarioAzaleaBrick}, 20, true};
const VariantOption kEnvy {"envy", {&kScenarioFrangipaniBrick}, 20, true};
const VariantOption kLust {"lust", {&kScenarioAmbrosiaBrick}, 25, true};
const VariantOption kSloth {"sloth", {&kAncestralHarebellBrick, &kOutfitsSkunkBrick}, 15, true};
// Removed: The Vector (Colorado, the_vector), last on Colorado: the other
// Colorado options roll as before; its share goes to the normal map.

std::span<const VariantOption* const> AllVariantOptions() {
    static const std::array<const VariantOption*, kVariantOptionCount> s_All {
        // Bangkok
        &kTheSource,
        &kTheWarlord,
        &kTheFoodCritic,
        &kTheExDictator,
        &kTheVeteran,
        &kTheInfiltrator,
        // Hokkaido
        &kPatientZero,
        &kSnowFestival,
        &kReflection,
        &kTheSurgeons,
        &kTheFugitive,
        &kTheController, // Sapienza
        &kTodEbolaNight,
        &kLandslide,
        &kLightingCopperhead,
        &kTheCongressman,
        &kTheGuru,
        &kThePrince,
        &kTheTwin,
        &kTheWildcard,
        &kTheDeceivers,
        &kTheEnforcer,
        // Paris
        &kHolidayHoarders,
        &kTheForger,
        &kTheSensation,
        &kTheBlackHat,
        &kThePharmacist,
        &kThePaparazzo,
        &kTheChef,
        &kTheBroker,
        &kTheIdentityThief,
        &kTheBlackmailer,
        &kTheDirector,
        &kTheBanker,
        // Haven, Chongqing, Colorado
        &kHerbalistParty,
        &kTheRage,
        &kTheSplitter,
        &kGluttony,
        &kTheBookkeeper,
        &kTheChameleon,
        &kTheMercenary,
        // Dartmoor, Berlin, Whittleton Creek, Mendoza
        &kHarbinger,
        &kProcurers,
        &kCollector,
        &kGardenShow,
        &kSloth,
        &kTheDrop,
        &kLiability,
        &kLust,
        &kSerialKiller,
        &kABitterPill,
        &kHeartbreaker,
        &kIconoclast,
        &kEnvy,
        // Isle of Sgail, Santa Fortuna
        &kAppraiser,
        &kStowaway,
        &kTheWizard,
        &kRevolutionary,
        &kEmbraceOfTheSerpent,
        // Marrakesh, Dubai, Miami, Mumbai
        &kTheFixer,
        &kTheGunrunner,
        &kTheAngelOfDeath,
        &kTheExtractor,
        &kTheAscensionist,
        &kTheUndying,
        &kASilverTongue,
        &kIllusionsOfGrandeur,
        // Sapienza (added later)
        &kTheBadBoy,
        // Marrakesh, Miami (added later)
        &kTheEntertainer,
        &kTheUndyingReturns,
    };
    return s_All;
}

const VariantOption* FindVariantOption(std::string_view name) {
    for (const VariantOption* option : AllVariantOptions()) {
        if (option->name == name)
            return option;
    }
    return nullptr;
}

const VariantBrick* FindVariantBrick(uint64_t rid) {
    for (const VariantOption* option : AllVariantOptions()) {
        for (const VariantBrick* brick : option->bricks) {
            if (brick->rid == rid)
                return brick;
        }
    }
    return nullptr;
}

bool IsLightingOnly(const VariantOption& option) {
    return !option.check;
}

bool IsLightingOnlyVariant(std::string_view name) {
    const VariantOption* option = FindVariantOption(name);
    return option && IsLightingOnly(*option);
}

std::string VariantChanceKey(const VariantOption& option) {
    return "variant_chance_" + std::string(option.name);
}

VariantChances DefaultVariantChances() {
    VariantChances chances {};
    const auto all = AllVariantOptions();
    for (size_t i = 0; i < all.size(); ++i)
        chances[i] = all[i]->defaultChance;
    return chances;
}

int32_t ChanceOf(const VariantChances& chances, const VariantOption& option) {
    const size_t i = OptionIndex(option);
    return i < chances.size() ? chances[i] : 0;
}

void SetChance(VariantChances& chances, const VariantOption& option, int32_t percent) {
    const size_t i = OptionIndex(option);
    if (i < chances.size())
        chances[i] = percent;
}

const VariantMap* FindVariantMap(std::string_view scene) {
    const std::string lower = ToLower(scene);
    for (const VariantMap& map : Maps()) {
        if (ToLower(map.scene) == lower)
            return &map;
    }
    return nullptr;
}

namespace {

// The first map offering option (its mild scene), or null.
const VariantMap* MapOf(const VariantOption& option) {
    for (const VariantMap& map : Maps()) {
        if (std::find(map.options.begin(), map.options.end(), &option) != map.options.end())
            return &map;
    }
    return nullptr;
}

} // namespace

std::span<const VariantOption* const> VariantSiblings(const VariantOption& option) {
    const VariantMap* map = MapOf(option);
    if (!map)
        return {};
    return map->options;
}

bool OfferedInShowdowns(const VariantOption& option) {
    for (const VariantMap& map : Maps()) {
        if (!map.name.ends_with(" hot"))
            continue;

        for (const VariantOption* offered : map.options) {
            if (offered == &option)
                return true;
        }
    }
    return false;
}

std::string_view VariantPlaceName(const VariantOption& option) {
    const VariantMap* map = MapOf(option);
    if (!map)
        return {};
    return map->name.substr(0, map->name.rfind(' '));
}

VariantChances FitVariantChances(const VariantChances& chances, const VariantOption& edited, int32_t percent) {
    VariantChances out = chances;
    const size_t editedIndex = OptionIndex(edited);
    if (editedIndex >= out.size())
        return out;

    percent = std::clamp(percent, int32_t {0}, int32_t {100});
    out[editedIndex] = percent;
    const int64_t budget = 100 - percent;

    // The map's other options, with their current chances (below 0 counts as 0).
    std::vector<size_t> others;
    int64_t sum = 0;
    for (const VariantOption* option : VariantSiblings(edited)) {
        const size_t i = OptionIndex(*option);
        if (i == editedIndex || i >= out.size())
            continue;
        others.push_back(i);
        sum += std::max(out[i], int32_t {0});
    }
    if (sum <= budget)
        return out;

    // Rounded down in proportion, then one point each to the largest remainders.
    std::vector<int64_t> remainders;
    int64_t given = 0;
    for (const size_t i : others) {
        const int64_t scaled = static_cast<int64_t>(std::max(out[i], int32_t {0})) * budget;
        out[i] = static_cast<int32_t>(scaled / sum);
        remainders.push_back(scaled % sum);
        given += out[i];
    }
    for (int64_t left = budget - given; left > 0; --left) {
        size_t best = 0;
        for (size_t k = 1; k < others.size(); ++k) {
            if (remainders[k] > remainders[best])
                best = k;
        }
        ++out[others[best]];
        remainders[best] = -1;
    }
    return out;
}

std::string_view VariantName(const VariantOption* option) {
    return option ? option->name : std::string_view("none");
}

std::string VariantForceNames() {
    std::vector<std::string_view> names;
    for (const VariantOption* option : AllVariantOptions())
        names.push_back(option->name);
    std::sort(names.begin(), names.end());
    std::string out = "none";
    for (const std::string_view name : names) {
        out += ", ";
        out += name;
    }
    return out;
}

uint32_t VariantRoll(int32_t seed, std::string_view scene) {
    return static_cast<uint32_t>(DialHash(seed, scene, kVariantFeature, int32_t {0}) % kVariantRollRange);
}

VariantWalk MakeVariantWalk(const VariantMap& map, const VariantChances& chances) {
    VariantWalk walk;
    walk.options = map.options;
    for (const VariantOption* option : map.options) {
        const int32_t chance = std::clamp(ChanceOf(chances, *option), int32_t {0}, int32_t {100});
        walk.chances.push_back(chance);
        walk.sum += chance;
    }
    walk.scaled = walk.sum > 100;
    // Cumulative, so rounding never leaves a gap: the last bound is exactly
    // kVariantRollRange when scaled.
    const uint64_t total = walk.scaled ? static_cast<uint64_t>(walk.sum) : 100;
    uint64_t prefix = 0;
    for (const int32_t chance : walk.chances) {
        prefix += static_cast<uint64_t>(chance);
        walk.upTo.push_back(static_cast<uint32_t>(prefix * kVariantRollRange / total));
    }
    return walk;
}

const VariantOption* WalkVariant(const VariantWalk& walk, uint32_t roll) {
    for (size_t i = 0; i < walk.options.size() && i < walk.upTo.size(); ++i) {
        if (roll < walk.upTo[i])
            return walk.options[i];
    }
    return nullptr;
}

std::string WalkShareText(const VariantWalk& walk, size_t i) {
    if (i >= walk.upTo.size())
        return "0";
    return Hundredths(walk.upTo[i] - (i == 0 ? 0 : walk.upTo[i - 1]));
}

std::string DescribeWalk(const VariantWalk& walk) {
    std::string out;
    for (size_t i = 0; i < walk.options.size(); ++i) {
        out += std::string(walk.options[i]->name) + " " + WalkShareText(walk, i) + "%";
        if (walk.scaled)
            out += " (set " + std::to_string(walk.chances[i]) + "%)";
        out += ", ";
    }
    const uint32_t last = walk.upTo.empty() ? 0 : walk.upTo.back();
    return out + "none " + Hundredths(kVariantRollRange - last) + "%";
}

bool HasBrick(std::span<const std::string> bricks, std::string_view path) {
    const std::string lower = ToLower(path);
    for (const std::string& brick : bricks) {
        if (ToLower(brick) == lower)
            return true;
    }
    return false;
}

std::optional<std::string> ParseVariantForce(std::string_view text) {
    constexpr std::string_view ws = " \t\r\n";
    const size_t first = text.find_first_not_of(ws);
    if (first == std::string_view::npos)
        return std::string();
    const std::string name = ToLower(text.substr(first, text.find_last_not_of(ws) - first + 1));
    if (name == "none" || FindVariantOption(name))
        return name;
    return std::nullopt;
}

VariantPlan PlanVariant(const VariantMap& map, int32_t seed, std::string_view scene, const VariantChances& chances,
                        std::span<const std::string> bricks, std::string_view force) {
    VariantPlan plan;
    plan.map = &map;
    plan.walk = MakeVariantWalk(map, chances);
    plan.roll = VariantRoll(seed, scene);
    plan.seeded = WalkVariant(plan.walk, plan.roll);
    plan.option = plan.seeded;

    if (!force.empty()) {
        if (force == "none") {
            plan.forced = true;
            plan.option = nullptr;
        }
        else {
            for (const VariantOption* option : map.options) {
                if (option->name == force) {
                    plan.forced = true;
                    plan.option = option;
                    break;
                }
            }
        }
        plan.forceNotOffered = !plan.forced;
    }

    if (!plan.option) {
        plan.action = VariantAction::Unchanged;
        return plan;
    }

    bool anyAdd = false;
    for (const VariantBrick* brick : plan.option->bricks) {
        VariantBrickPlan step {brick, VariantAction::Add};
        if (HasBrick(bricks, brick->path))
            step.action = VariantAction::AlreadyPresent;
        anyAdd = anyAdd || step.action == VariantAction::Add;
        plan.bricks.push_back(step);
    }

    plan.action = anyAdd ? VariantAction::Add : VariantAction::AlreadyPresent;
    return plan;
}

bool VariantUnavailable(const VariantOption& option, const VariantMap& map, const FullVariantAvailable& available) {
    const FullVariant* full = FullVariantOf(option.name, map.name);
    return full && !available(*full);
}

VariantChances AvailableVariantChances(const VariantMap& map, const VariantChances& chances,
                                       const FullVariantAvailable& available) {
    VariantChances out = chances;
    for (const VariantOption* option : map.options) {
        if (VariantUnavailable(*option, map, available))
            SetChance(out, *option, 0);
    }
    return out;
}

VariantAppend ResolveVariantAppend(const VariantPlan& plan, const VariantBrickCheck& check) {
    VariantAppend result;
    if (plan.action != VariantAction::Add)
        return result;

    for (const VariantBrickPlan& step : plan.bricks) {
        if (step.action != VariantAction::Add)
            continue;
        std::string why;
        if (!check(*step.brick, why)) {
            result.add.clear();
            result.failed = step.brick;
            result.why = std::move(why);
            return result;
        }
        result.add.push_back(step.brick);
    }
    return result;
}

namespace {

constexpr SealedActor kSealedActors[] = {
#include "SealedActors.inc"
};

constexpr StoryTarget kStoryTargets[] = {
#include "StoryTargets.inc"
};

bool AllBricksLoaded(const VariantOption& option, std::span<const uint64_t> loadedBrickRids) {
    return !option.bricks.empty() &&
           std::all_of(option.bricks.begin(), option.bricks.end(), [&](const VariantBrick* brick) {
               return std::find(loadedBrickRids.begin(), loadedBrickRids.end(), brick->rid) != loadedBrickRids.end();
           });
}

} // namespace

std::span<const SealedActor> SealedActors() {
    return kSealedActors;
}

std::vector<SealedActor> LoadedSealedActors(std::span<const uint64_t> loadedBrickRids,
                                            std::span<const VariantOption* const> options,
                                            std::span<const SealedActor> rows) {
    std::set<std::string_view> loaded;

    for (const VariantOption* option : options) {
        if (option && AllBricksLoaded(*option, loadedBrickRids))
            loaded.insert(option->name);
    }

    std::vector<SealedActor> out;

    for (const SealedActor& row : rows) {
        if (loaded.contains(row.option))
            out.push_back(row);
    }

    return out;
}

std::set<Guid> VariantTargetExclusions(std::span<const uint64_t> loadedBrickRids,
                                       std::span<const VariantOption* const> options) {
    std::set<Guid> out;

    for (const SealedActor& row : LoadedSealedActors(loadedBrickRids, options)) {
        if (const auto guid = ParseGuid(row.repo); guid && !guid->IsEmpty())
            out.insert(*guid);
    }

    for (const VariantOption* option : options) {
        if (!option || (option->notTargets.empty() && option->removed.empty()) ||
            !AllBricksLoaded(*option, loadedBrickRids))
            continue;

        for (const std::string_view text : option->notTargets) {
            if (const auto guid = ParseGuid(text); guid && !guid->IsEmpty())
                out.insert(*guid);
        }

        for (const StrandedNpc& npc : option->removed) {
            if (const auto guid = ParseGuid(npc.repo); guid && !guid->IsEmpty())
                out.insert(*guid);
        }
    }

    return out;
}

std::set<Guid> TargetPoolExclusions(std::span<const uint64_t> loadedBrickRids, bool fullLandslide,
                                    std::span<const VariantOption* const> options) {
    return TargetPoolExclusions(loadedBrickRids, fullLandslide ? &LandslideFull() : nullptr, options);
}

std::set<Guid> TargetPoolExclusions(std::span<const uint64_t> loadedBrickRids, const FullVariant* full,
                                    std::span<const VariantOption* const> options) {
    if (!full)
        return VariantTargetExclusions(loadedBrickRids, options);

    std::vector<const VariantOption*> others;

    for (const VariantOption* option : options) {
        if (!option || option->name != full->option)
            others.push_back(option);
    }

    std::set<Guid> out = VariantTargetExclusions(loadedBrickRids, others);

    for (const std::string_view text : NeverTargetRepos(*full)) {
        if (const auto guid = ParseGuid(text); guid && !guid->IsEmpty())
            out.insert(*guid);
    }

    return out;
}

std::span<const StoryTarget> StoryTargets() {
    return kStoryTargets;
}

std::vector<StoryTarget> SceneStoryTargets(std::string_view scene, std::span<const StoryTarget> rows) {
    const std::string lower = ToLower(scene);
    std::vector<StoryTarget> out;

    for (const StoryTarget& row : rows) {
        if (ToLower(row.scene) == lower)
            out.push_back(row);
    }

    return out;
}

std::set<Guid> StoryTargetExclusions(std::string_view scene, std::span<const StoryTarget> rows) {
    std::set<Guid> out;

    for (const StoryTarget& row : SceneStoryTargets(scene, rows)) {
        if (const auto guid = ParseGuid(row.repo); guid && !guid->IsEmpty())
            out.insert(*guid);
    }

    return out;
}

std::set<Guid> TargetPoolExclusions(std::string_view scene, std::span<const uint64_t> loadedBrickRids,
                                    const FullVariant* full, std::span<const VariantOption* const> options) {
    std::set<Guid> out = TargetPoolExclusions(loadedBrickRids, full, options);
    out.merge(StoryTargetExclusions(scene));
    return out;
}

std::vector<std::string_view> LoadedVariants(std::span<const uint64_t> loadedBrickRids, bool fullLandslide,
                                             std::span<const VariantOption* const> options) {
    return LoadedVariants(loadedBrickRids, fullLandslide ? &LandslideFull() : nullptr, options);
}

std::vector<std::string_view> LoadedVariants(std::span<const uint64_t> loadedBrickRids, const FullVariant* full,
                                             std::span<const VariantOption* const> options) {
    // Under a full variant with its own crowd, its option is not a variant of
    // its scene, nor is any other full variant's on the same scene (the full
    // Icon and the full Bad Boy, a layer on it).
    const auto isOwn = [&](const VariantOption* option) {
        if (!full || full->sharesCrowd)
            return false;

        const auto all = FullVariants();
        return option->name == full->option || std::any_of(all.begin(), all.end(), [&](const FullVariant* other) {
                   return other->option == option->name && ToLower(other->scene) == ToLower(full->scene);
               });
    };
    const auto isLoaded = [&](const VariantBrick* brick) {
        return std::find(loadedBrickRids.begin(), loadedBrickRids.end(), brick->rid) != loadedBrickRids.end();
    };
    std::vector<const VariantOption*> loaded;

    for (const VariantOption* option : options) {
        // An option with the same bricks as one before it (the_bad_boy's are
        // lighting_copperhead's) counts once, as the first.
        const bool repeat = option && std::any_of(loaded.begin(), loaded.end(), [&](const VariantOption* earlier) {
                                return earlier->bricks == option->bricks;
                            });

        if (option && !option->bricks.empty() && !isOwn(option) && !repeat &&
            std::all_of(option->bricks.begin(), option->bricks.end(), isLoaded))
            loaded.push_back(option);
    }

    // Left out: an option whose bricks all belong to another loaded option with more of them.
    const auto within = [](const VariantOption* inner, const VariantOption* outer) {
        return inner != outer && inner->bricks.size() < outer->bricks.size() &&
               std::all_of(inner->bricks.begin(), inner->bricks.end(), [&](const VariantBrick* brick) {
                   return std::find(outer->bricks.begin(), outer->bricks.end(), brick) != outer->bricks.end();
               });
    };
    std::vector<const VariantOption*> kept;

    for (const VariantOption* option : loaded) {
        if (std::none_of(loaded.begin(), loaded.end(),
                         [&](const VariantOption* other) { return within(option, other); }))
            kept.push_back(option);
    }

    std::stable_sort(kept.begin(), kept.end(), [](const VariantOption* a, const VariantOption* b) {
        return a->bricks.size() > b->bricks.size();
    });
    std::vector<std::string_view> out;

    for (const VariantOption* option : kept)
        out.push_back(option->name);

    return out;
}

std::vector<std::string_view> MoverTableVariants(std::span<const uint64_t> loadedBrickRids, const FullVariant* full,
                                                 std::span<const VariantOption* const> options) {
    std::vector<std::string_view> out = LoadedVariants(loadedBrickRids, full, options);

    if (full && !full->layerOf.empty())
        out.insert(out.begin(), full->key);

    return out;
}

std::optional<std::string> VariantCheckProblem(const VariantCheckActor& actor, bool playing) {
    // The mission's own targets: the unpicked ones stay disabled (and may
    // read as dead), as they should.
    if (actor.dedicated)
        return std::nullopt;
    if (!actor.present)
        return std::string("missing (no actor)");
    if (!actor.readable)
        return std::string("unreadable");
    if (playing && !actor.alive)
        return std::string("dead");
    if (!actor.startEnabled)
        return std::string("start disabled (a scene NPC the variant may have removed)");
    if (playing && !actor.enabled)
        return std::string("disabled");
    return std::nullopt;
}

std::string VariantCheckSummary(std::span<const VariantCheckActor> actors, bool playing) {
    size_t dedicated = 0;
    size_t problems = 0;
    for (const VariantCheckActor& actor : actors) {
        dedicated += actor.dedicated ? 1 : 0;
        problems += VariantCheckProblem(actor, playing) ? 1 : 0;
    }
    const size_t scene = actors.size() - dedicated;
    std::string out = std::to_string(actors.size()) + " pool actor" + (actors.size() == 1 ? "" : "s") + ": " +
                      std::to_string(dedicated) + " in the mission brick (not checked), " + std::to_string(scene) +
                      (scene == 1 ? " scene NPC: " : " scene NPCs: ");
    out +=
        problems == 0 ? std::string("no problems") : std::to_string(problems) + " problem" + (problems == 1 ? "" : "s");
    return out;
}

} // namespace fr
