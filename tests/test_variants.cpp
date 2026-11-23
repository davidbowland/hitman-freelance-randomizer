#include "doctest.h"

#include "support/FullVariantTestUtil.h"
#include "support/VariantTestUtil.h"

#include "LandslideFull.h"
#include "MoverTable.h"
#include "Scene.h" // ToLower
#include "SettingsTable.h"
#include "Variants.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char* kBangkokMild = "assembly:/_pro/scenes/missions/Bangkok/scene_tiger_mild_sicbo.entity";
constexpr const char* kBangkokHot = "assembly:/_pro/scenes/missions/bangkok/scene_tiger_hot_sicbo.entity";
constexpr const char* kHokkaidoMild = "assembly:/_PRO/Scenes/Missions/hokkaido/scene_snowcrane_mild_shogi.entity";
constexpr const char* kHokkaidoHot = "assembly:/_pro/scenes/missions/hokkaido/scene_snowcrane_hot_shogi.entity";
constexpr const char* kSapienzaMild = "assembly:/_PRO/Scenes/Missions/CoastalTown/scene_costaltown_mild_bingo.entity";
constexpr const char* kSapienzaHot = "assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_hot_bingo.entity";
constexpr const char* kMarrakeshMild = "assembly:/_pro/scenes/missions/Marrakesh/scene_spider_mild_mancala.entity";
constexpr const char* kMarrakeshHot = "assembly:/_pro/scenes/missions/Marrakesh/scene_spider_hot_mancala.entity";
constexpr const char* kDubaiMild = "assembly:/_pro/scenes/missions/golden/mission_gecko/scene_golden_mild_chess.entity";
constexpr const char* kDubaiHot = "assembly:/_pro/scenes/missions/golden/mission_gecko/scene_golden_hot_chess.entity";
constexpr const char* kMiamiMild = "assembly:/_pro/scenes/missions/miami/scene_flamingo_mild_pinochle.entity";
constexpr const char* kMiamiHot = "assembly:/_pro/scenes/missions/miami/scene_flamingo_hot_pinochle.entity";
constexpr const char* kMumbaiMild = "assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.entity";
constexpr const char* kMumbaiHot = "assembly:/_pro/scenes/missions/mumbai/scene_mongoose_hot_ludo.entity";
constexpr const char* kParisMild = "assembly:/_pro/scenes/missions/paris/scene_peacock_mild_craps.entity";
constexpr const char* kParisHot = "assembly:/_pro/scenes/missions/paris/scene_peacock_hot_craps.entity";

constexpr const char* kHavenMild =
    "assembly:/_pro/scenes/missions/opulent/mission_stingray/scene_opulent_mild_hilo.entity";
constexpr const char* kHavenHot =
    "assembly:/_pro/scenes/missions/opulent/mission_stingray/scene_opulent_hot_hilo.entity";
constexpr const char* kChongqingMild = "assembly:/_pro/scenes/missions/wet/scene_wet_mild_mahjong.entity";
constexpr const char* kChongqingHot = "assembly:/_pro/scenes/missions/wet/scene_wet_hot_mahjong.entity";
constexpr const char* kColoradoMild = "assembly:/_pro/scenes/missions/colorado_2/scene_bull_mild_spades.entity";
constexpr const char* kColoradoHot = "assembly:/_pro/scenes/missions/colorado_2/scene_bull_hot_spades.entity";
constexpr const char* kDartmoorMild = "assembly:/_PRO/Scenes/Missions/Ancestral/scene_ancestral_mild_bridge.entity";
constexpr const char* kDartmoorHot = "assembly:/_pro/scenes/missions/ancestral/scene_ancestral_hot_bridge.entity";
constexpr const char* kBerlinMild = "assembly:/_pro/scenes/missions/edgy/mission_fox/scene_edgy_mild_meyer.entity";
constexpr const char* kBerlinHot = "assembly:/_pro/scenes/missions/edgy/mission_fox/scene_edgy_hot_meyer.entity";
constexpr const char* kWhittletonMild = "assembly:/_pro/scenes/missions/skunk/scene_skunk_mild_solitaire.entity";
constexpr const char* kWhittletonHot = "assembly:/_pro/scenes/missions/skunk/scene_skunk_hot_solitaire.entity";

constexpr const char* kMendozaMild = "assembly:/_pro/scenes/missions/elegant/scene_elegant_mild_sapo.entity";
constexpr const char* kMendozaHot = "assembly:/_pro/scenes/missions/elegant/scene_elegant_hot_sapo.entity";
constexpr const char* kSgailMild = "assembly:/_pro/scenes/missions/theark/scene_magpie_mild_whist.entity";
constexpr const char* kSgailHot = "assembly:/_pro/scenes/missions/theark/scene_magpie_hot_whist.entity";
constexpr const char* kSantaFortunaMild = "assembly:/_pro/scenes/missions/colombia/scene_hippo_mild_trompo.entity";
constexpr const char* kSantaFortunaHot = "assembly:/_pro/scenes/missions/colombia/scene_hippo_hot_trompo.entity";

constexpr const char* kAllScenes[] = {
    kBangkokMild,  kBangkokHot,  kHokkaidoMild, kHokkaidoHot,      kSapienzaMild,    kSapienzaHot,   kParisMild,
    kParisHot,     kHavenMild,   kHavenHot,     kChongqingMild,    kChongqingHot,    kColoradoMild,  kColoradoHot,
    kDartmoorMild, kDartmoorHot, kBerlinMild,   kBerlinHot,        kWhittletonMild,  kWhittletonHot, kMendozaMild,
    kMendozaHot,   kSgailMild,   kSgailHot,     kSantaFortunaMild, kSantaFortunaHot, kMarrakeshMild, kMarrakeshHot,
    kDubaiMild,    kDubaiHot,    kMiamiMild,    kMiamiHot,         kMumbaiMild,      kMumbaiHot};

constexpr const char* kMissionMildSicbo = "assembly:/_pro/scenes/missions/bangkok/mission_mild_sicbo.brick";

// Every chance 0, then the given ones.
fr::VariantChances Chances(std::initializer_list<std::pair<const fr::VariantOption*, int32_t>> set = {}) {
    fr::VariantChances chances {};
    for (const auto& [option, percent] : set)
        fr::SetChance(chances, *option, percent);
    return chances;
}

const fr::VariantChances kDefaults = fr::DefaultVariantChances();

} // namespace

TEST_CASE("variant_scene_matching") {
    REQUIRE(fr::FindVariantMap(kBangkokMild));
    REQUIRE(fr::FindVariantMap(kBangkokHot));
    REQUIRE(fr::FindVariantMap(kHokkaidoMild));
    REQUIRE(fr::FindVariantMap(kHokkaidoHot));
    REQUIRE(fr::FindVariantMap(kSapienzaMild));
    REQUIRE(fr::FindVariantMap(kSapienzaHot));
    CHECK(fr::FindVariantMap(kSapienzaMild)->name == "Sapienza mild");
    CHECK(fr::FindVariantMap(kSapienzaHot)->name == "Sapienza hot");
    CHECK(fr::FindVariantMap("assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_mild_bingo.entity") ==
          fr::FindVariantMap(kSapienzaMild));
    CHECK(fr::FindVariantMap(kBangkokMild)->name == "Bangkok mild");
    CHECK(fr::FindVariantMap(kBangkokHot)->name == "Bangkok hot");
    CHECK(fr::FindVariantMap(kHokkaidoMild)->name == "Hokkaido mild");
    CHECK(fr::FindVariantMap(kHokkaidoHot)->name == "Hokkaido hot");
    REQUIRE(fr::FindVariantMap(kParisMild));
    REQUIRE(fr::FindVariantMap(kParisHot));
    CHECK(fr::FindVariantMap(kParisMild)->name == "Paris mild");
    CHECK(fr::FindVariantMap(kParisHot)->name == "Paris hot");
    CHECK(fr::FindVariantMap("ASSEMBLY:/_PRO/SCENES/MISSIONS/PARIS/SCENE_PEACOCK_MILD_CRAPS.ENTITY") ==
          fr::FindVariantMap(kParisMild));

    // Haven, Chongqing, Colorado, Dartmoor and Berlin, mild and hot.
    const std::pair<const char*, const char*> named[] = {
        {kHavenMild, "Haven mild"},
        {kHavenHot, "Haven hot"},
        {kChongqingMild, "Chongqing mild"},
        {kChongqingHot, "Chongqing hot"},
        {kColoradoMild, "Colorado mild"},
        {kColoradoHot, "Colorado hot"},
        {kDartmoorMild, "Dartmoor mild"},
        {kDartmoorHot, "Dartmoor hot"},
        {kBerlinMild, "Berlin mild"},
        {kBerlinHot, "Berlin hot"},
        {kWhittletonMild, "Whittleton Creek mild"},
        {kWhittletonHot, "Whittleton Creek hot"},
        {kMendozaMild, "Mendoza mild"},
        {kMendozaHot, "Mendoza hot"},
        {kSgailMild, "Isle of Sgail mild"},
        {kSgailHot, "Isle of Sgail hot"},
        {kSantaFortunaMild, "Santa Fortuna mild"},
        {kSantaFortunaHot, "Santa Fortuna hot"},
        // Marrakesh, Dubai and Miami.
        {kMarrakeshMild, "Marrakesh mild"},
        {kMarrakeshHot, "Marrakesh hot"},
        {kDubaiMild, "Dubai mild"},
        {kDubaiHot, "Dubai hot"},
        {kMiamiMild, "Miami mild"},
        {kMiamiHot, "Miami hot"},
        // Mumbai.
        {kMumbaiMild, "Mumbai mild"},
        {kMumbaiHot, "Mumbai hot"},
    };
    for (const auto& [scene, name] : named) {
        CAPTURE(scene);
        REQUIRE(fr::FindVariantMap(scene));
        CHECK(fr::FindVariantMap(scene)->name == name);
    }
    CHECK(fr::FindVariantMap("ASSEMBLY:/_PRO/SCENES/MISSIONS/WET/SCENE_WET_HOT_MAHJONG.ENTITY") ==
          fr::FindVariantMap(kChongqingHot));
    CHECK(fr::FindVariantMap("assembly:/_pro/scenes/missions/ancestral/scene_ancestral_mild_bridge.entity") ==
          fr::FindVariantMap(kDartmoorMild));
    CHECK(fr::FindVariantMap("ASSEMBLY:/_PRO/SCENES/MISSIONS/SKUNK/SCENE_SKUNK_HOT_SOLITAIRE.ENTITY") ==
          fr::FindVariantMap(kWhittletonHot));
    CHECK(fr::FindVariantMap("ASSEMBLY:/_PRO/SCENES/MISSIONS/THEARK/SCENE_MAGPIE_MILD_WHIST.ENTITY") ==
          fr::FindVariantMap(kSgailMild));

    // Any case.
    CHECK(fr::FindVariantMap("assembly:/_pro/scenes/missions/bangkok/scene_tiger_mild_sicbo.entity") ==
          fr::FindVariantMap(kBangkokMild));
    CHECK(fr::FindVariantMap("ASSEMBLY:/_PRO/SCENES/MISSIONS/BANGKOK/SCENE_TIGER_HOT_SICBO.ENTITY") ==
          fr::FindVariantMap(kBangkokHot));
    CHECK(fr::FindVariantMap("assembly:/_pro/scenes/missions/hokkaido/scene_snowcrane_mild_shogi.entity") ==
          fr::FindVariantMap(kHokkaidoMild));
}

TEST_CASE("variant_scene_no_match") {
    CHECK_FALSE(fr::FindVariantMap(""));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/snug/scene_vanilla.entity"));
    // Miami's story and elusive target scenes (Freelancer never loads them).
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/miami/scene_flamingo.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/miami/scene_et_sambuca.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/golden/mission_gecko/scene_gibson.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/theark/scene_penicillin.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/ancestral/scene_smoothsnake.entity"));
    // The elusive target missions' own scenes (Freelancer never loads them).
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/ancestral/scene_ancestral_vesper.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/edgy/mission_fox/scene_radler.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/elegant/scene_llama_elusive_clerico.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/theark/scene_magpie.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/colombia/scene_hippo.entity"));
    // The official scenes whose layers the heavy layer options load (Freelancer never loads them).
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/mumbai/scene_kingcobra.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/mumbai/scene_mongoose.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/wet/scene_wet_lambic.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/colorado_2/scene_rabies.entity"));
    // Hawke's Bay has no Freelancer scene.
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/sheep/scene_adonis.entity"));
    // Berlin's scenes live under edgy/mission_fox/, not edgy/ or berlin/.
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/edgy/scene_edgy_mild_meyer.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/berlin/scene_fox_hot_meyer.entity"));
    // Haven's under opulent/mission_stingray/.
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/opulent/scene_opulent_mild_hilo.entity"));
    // Marrakesh's story, bonus mission (A House Built on Sand) and Sarajevo Six scenes.
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/marrakesh/_scene_spider.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/marrakesh/_scene_python.entity"));
    // Whittleton Creek's story, bonus mission and elusive target scenes.
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/skunk/scene_skunk.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/skunk/scene_gartersnake.entity"));
    // The story missions of the same locations.
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/bangkok/scene_tiger.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/hokkaido/scene_snowcrane.entity"));
    // Near misses: no prefix or suffix matching.
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/bangkok/scene_tiger_hot_sicbo.entity "));
    CHECK_FALSE(fr::FindVariantMap("_pro/scenes/missions/bangkok/scene_tiger_hot_sicbo.entity"));
    CHECK_FALSE(fr::FindVariantMap("assembly:/_pro/scenes/missions/bangkok/scene_tiger_hot_sicbo"));
}

TEST_CASE("variant_options") {
    using Options = std::vector<const fr::VariantOption*>;
    for (const char* scene : {kBangkokMild, kBangkokHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kTheSource, &fr::kTheWarlord, &fr::kTheFoodCritic,
                                                             &fr::kTheExDictator, &fr::kTheVeteran,
                                                             &fr::kTheInfiltrator});
    // The options with a full version (Patient Zero, The Author, Landslide, The Icon, The Bad Boy, The
    // Entertainer) are the mild scene's only: the showdown has no full version and gets no lighting-only stand-in.
    CHECK(fr::FindVariantMap(kHokkaidoMild)->options == Options {&fr::kPatientZero, &fr::kSnowFestival,
                                                                 &fr::kReflection, &fr::kTheSurgeons, &fr::kTheFugitive,
                                                                 &fr::kTheController});
    CHECK(fr::FindVariantMap(kHokkaidoHot)->options ==
          Options {&fr::kSnowFestival, &fr::kReflection, &fr::kTheSurgeons, &fr::kTheFugitive, &fr::kTheController});
    CHECK(fr::FindVariantMap(kSapienzaMild)->options ==
          Options {&fr::kTodEbolaNight, &fr::kLandslide, &fr::kLightingCopperhead, &fr::kTheCongressman, &fr::kTheGuru,
                   &fr::kThePrince, &fr::kTheTwin, &fr::kTheWildcard, &fr::kTheDeceivers, &fr::kTheEnforcer,
                   &fr::kTheBadBoy});
    CHECK(fr::FindVariantMap(kSapienzaHot)->options == Options {&fr::kTheCongressman, &fr::kTheGuru, &fr::kThePrince,
                                                                &fr::kTheTwin, &fr::kTheWildcard, &fr::kTheDeceivers,
                                                                &fr::kTheEnforcer});
    for (const char* scene : {kParisMild, kParisHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kHolidayHoarders, &fr::kTheForger, &fr::kTheSensation,
                                                             &fr::kTheBlackHat, &fr::kThePharmacist, &fr::kThePaparazzo,
                                                             &fr::kTheChef, &fr::kTheBroker, &fr::kTheIdentityThief,
                                                             &fr::kTheBlackmailer, &fr::kTheDirector, &fr::kTheBanker});
    for (const char* scene : {kHavenMild, kHavenHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kHerbalistParty});
    for (const char* scene : {kChongqingMild, kChongqingHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kTheRage, &fr::kTheSplitter, &fr::kGluttony});
    for (const char* scene : {kColoradoMild, kColoradoHot})
        CHECK(fr::FindVariantMap(scene)->options ==
              Options {&fr::kTheBookkeeper, &fr::kTheChameleon, &fr::kTheMercenary});
    CHECK(fr::FindVariantMap(kDartmoorMild)->options ==
          Options {&fr::kHarbinger, &fr::kProcurers, &fr::kCollector, &fr::kGardenShow, &fr::kSloth});
    // Garden Show closes the mansion the showdown's suspects stand in; Sloth
    // removes its start (47 waits 500 m up) and most of its people.
    CHECK(fr::FindVariantMap(kDartmoorHot)->options == Options {&fr::kHarbinger, &fr::kProcurers, &fr::kCollector});
    for (const char* scene : {kBerlinMild, kBerlinHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kTheDrop, &fr::kLiability, &fr::kLust});
    for (const char* scene : {kWhittletonMild, kWhittletonHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kSerialKiller, &fr::kABitterPill});
    for (const char* scene : {kMendozaMild, kMendozaHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kHeartbreaker, &fr::kIconoclast, &fr::kEnvy});
    for (const char* scene : {kSgailMild, kSgailHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kAppraiser, &fr::kStowaway, &fr::kTheWizard});
    for (const char* scene : {kSantaFortunaMild, kSantaFortunaHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kRevolutionary, &fr::kEmbraceOfTheSerpent});
    CHECK(fr::FindVariantMap(kMarrakeshMild)->options == Options {&fr::kTheFixer, &fr::kTheGunrunner,
                                                                  &fr::kTheAngelOfDeath, &fr::kTheExtractor,
                                                                  &fr::kTheEntertainer});
    CHECK(fr::FindVariantMap(kMarrakeshHot)->options ==
          Options {&fr::kTheFixer, &fr::kTheGunrunner, &fr::kTheAngelOfDeath, &fr::kTheExtractor});
    for (const char* scene : {kDubaiMild, kDubaiHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kTheAscensionist});
    for (const char* scene : {kMiamiMild, kMiamiHot})
        CHECK(fr::FindVariantMap(scene)->options ==
              Options {&fr::kTheUndying, &fr::kASilverTongue, &fr::kTheUndyingReturns});
    for (const char* scene : {kMumbaiMild, kMumbaiHot})
        CHECK(fr::FindVariantMap(scene)->options == Options {&fr::kIllusionsOfGrandeur});

    // Every option with a full version: offered by its mild map, never by the hot one (no simple version) unless
    // the full version names it too (Gluttony, which appends, on Chongqing's showdown), and a pick there always has
    // its full version. The full Marrakesh night has no option (only the full Entertainer's base): never picked.
    CHECK(fr::test::Full("marrakesh").option.empty());

    for (const fr::FullVariant* full : fr::FullVariants()) {
        CAPTURE(full->key);

        if (full == &fr::test::Full("marrakesh"))
            continue;

        const fr::VariantOption* option = fr::FindVariantOption(full->option);
        REQUIRE(option != nullptr);

        for (const char* scene : kAllScenes) {
            const fr::VariantMap& map = *fr::FindVariantMap(scene);
            const bool offered = std::find(map.options.begin(), map.options.end(), option) != map.options.end();
            CAPTURE(map.name);
            CHECK(offered == (map.name == full->map || (!full->alsoMap.empty() && map.name == full->alsoMap)));
            CHECK((fr::FullVariantOf(full->option, map.name) != nullptr) == offered);
        }
    }

    // Each map lists its options in AllVariantOptions' order.
    const auto all = fr::AllVariantOptions();
    for (const char* scene : kAllScenes) {
        size_t last = 0;
        for (const fr::VariantOption* option : fr::FindVariantMap(scene)->options) {
            const size_t at = static_cast<size_t>(std::find(all.begin(), all.end(), option) - all.begin());
            REQUIRE(at < all.size());
            CHECK(at >= last);
            last = at;
        }
    }
}

TEST_CASE("variant_bricks") {
    CHECK(fr::kTodZikaNightBrick.path == "assembly:/_pro/scenes/missions/bangkok/tod_zika_night.brick");
    CHECK(fr::kMissionZikaBrick.path == "assembly:/_pro/scenes/missions/bangkok/mission_zika.brick");
    CHECK(fr::kTodFluBrick.path == "assembly:/_pro/scenes/missions/hokkaido/tod_flu.brick");
    CHECK(fr::kGfxFluBrick.path == "assembly:/_pro/scenes/missions/hokkaido/gfx_flu.brick");
    CHECK(fr::kTodEbolaNightBrick.path == "assembly:/_pro/scenes/missions/coastaltown/tod_ebola_night.brick");
    CHECK(fr::kLightingMambaBrick.path == "assembly:/_pro/scenes/missions/coastaltown/lighting_mamba.brick");
    CHECK(fr::kTodSummerMorningClearBrick.path ==
          "assembly:/_pro/scenes/missions/coastaltown/tod_summer_morning_clear.brick");
    CHECK(fr::kLightingCopperheadBrick.path == "assembly:/_pro/scenes/missions/coastaltown/lighting_copperhead.brick");
    CHECK(fr::kChallengesZikaBrick.path == "assembly:/_pro/scenes/missions/bangkok/challenges_zika.brick");
    CHECK(fr::kLocationParisNoelBrick.path == "assembly:/_pro/scenes/missions/paris/location_parisnoel.brick");
    // The TEMP hashes of [path].pc_entitytype in the hash list.
    CHECK(fr::kTodZikaNightBrick.rid == 0x0069324FADF99DA9ull);
    CHECK(fr::kMissionZikaBrick.rid == 0x00548596858A571Eull);
    CHECK(fr::kTodFluBrick.rid == 0x0025DD895C349D17ull);
    CHECK(fr::kGfxFluBrick.rid == 0x001B58DA1120083Bull);
    CHECK(fr::kTodEbolaNightBrick.rid == 0x008DAD11D7EEBE74ull);
    CHECK(fr::kLightingMambaBrick.rid == 0x00979CC61CFB841Aull);
    CHECK(fr::kLightingCopperheadBrick.rid == 0x005FC5439416F83Full);
    CHECK(fr::kTodSummerMorningClearBrick.rid == 0x007B86AD8C886AE7ull);
    CHECK(fr::kChallengesZikaBrick.rid == 0x00628E3E01F948D7ull);
    CHECK(fr::kLocationParisNoelBrick.rid == 0x00B69DB24675E6ECull);
    // The Icon's night ToD and Landslide's set dressing (coastaltown).
    CHECK(fr::kTodSummerNightClearBrick.path ==
          "assembly:/_pro/scenes/missions/coastaltown/tod_summer_night_clear.brick");
    CHECK(fr::kTodSummerNightClearBrick.rid == 0x00229AA6BC900212ull);
    CHECK(fr::kDressingBeachMambaBrick.path == "assembly:/_pro/scenes/missions/coastaltown/dressing_beach_mamba.brick");
    CHECK(fr::kDressingBeachMambaBrick.rid == 0x000250E46F74BE36ull);
    CHECK(fr::kDressingChurchMambaBrick.path ==
          "assembly:/_pro/scenes/missions/coastaltown/dressing_church_mamba.brick");
    CHECK(fr::kDressingChurchMambaBrick.rid == 0x001C2D18E84BD5A0ull);
    CHECK(fr::kDressingOldtownMambaBrick.path ==
          "assembly:/_pro/scenes/missions/coastaltown/dressing_oldtown_mamba.brick");
    CHECK(fr::kDressingOldtownMambaBrick.rid == 0x0019D2EAB61099E3ull);
    CHECK(fr::kDressingTownhallMambaBrick.path ==
          "assembly:/_pro/scenes/missions/coastaltown/dressing_townhall_mamba.brick");
    CHECK(fr::kDressingTownhallMambaBrick.rid == 0x00156F74F3BA0E84ull);
    CHECK(fr::kDressingTunnelsMambaBrick.path ==
          "assembly:/_pro/scenes/missions/coastaltown/dressing_tunnels_mamba.brick");
    CHECK(fr::kDressingTunnelsMambaBrick.rid == 0x00C20A7EC00C1F5Bull);
    CHECK(fr::VariantName(nullptr) == "none");
    CHECK(fr::VariantName(&fr::kPatientZero) == "patient_zero");

    // Dressing layers. Each rid is ZRuntimeResourceID::FromString of
    // BrickResourcePath(path) (MD5, checked offline) and the TEMP hash the
    // rpkg metadata lists.
    struct Expected {
        const fr::VariantBrick* brick;
        const char* name;
        const char* path;
        uint64_t rid;
    };
    const Expected dressing[] = {
        {&fr::kEaFluBrick, "ea_flu", "assembly:/_pro/scenes/missions/hokkaido/ea_flu.brick", 0x0030C60BD2E68D98ull},
        {&fr::kScenarioMamushiBrick, "scenario_mamushi",
         "assembly:/_pro/scenes/missions/hokkaido/scenario_mamushi.brick", 0x00502BC47FC839D9ull},
        {&fr::kScenarioGreentiniEaBrick, "scenario_greentini_ea",
         "assembly:/_pro/scenes/missions/opulent/scenario_greentini_ea.brick", 0x001252DEA472C74Full},
        {&fr::kScenarioGreentiniLightingBrick, "scenario_greentini_lighting",
         "assembly:/_pro/scenes/missions/opulent/scenario_greentini_lighting.brick", 0x0070BA7AF778A58Full},
        {&fr::kElusiveRedsnapperBrick, "elusive_redsnapper",
         "assembly:/_pro/scenes/missions/wet/elusive_redsnapper.brick", 0x0048C98715164BC3ull},
        {&fr::kElusiveTomorrowlandBrick, "elusive_tomorrowland",
         "assembly:/_pro/scenes/missions/edgy/mission_fox/elusive_tomorrowland.brick", 0x00C6C53821368B23ull},
    };
    std::set<uint64_t> rids;
    for (const Expected& e : dressing) {
        CAPTURE(e.name);
        CHECK(e.brick->name == e.name);
        CHECK(e.brick->path == e.path);
        CHECK(e.brick->rid == e.rid);
        CHECK((e.brick->rid >> 56) == 0); // a runtime id's top byte is 0
        CHECK(rids.insert(e.brick->rid).second);
        // Never a heavy story layer by mistake: the Splitter and the full Reflection.
        CHECK(std::string_view(e.path).find("scenario_lambic") == std::string_view::npos);
        CHECK(std::string_view(e.path) != "assembly:/_pro/scenes/missions/hokkaido/scenario_bellini.brick");
    }

    // Whittleton Creek: The Serial Killer's elusive target layer (a game file:
    // its TEMP hash is in the hash list).
    CHECK(fr::kElusiveAlabamaslammerBrick.name == "elusive_alabamaslammer");
    CHECK(fr::kElusiveAlabamaslammerBrick.path == "assembly:/_pro/scenes/missions/skunk/elusive_alabamaslammer.brick");
    CHECK(fr::kElusiveAlabamaslammerBrick.rid == 0x001BDE20B3F81CB1ull);
    CHECK(rids.insert(fr::kElusiveAlabamaslammerBrick.rid).second);

    // The elusive target layers. Each path is the one brick its
    // official contract loads; each rid is the MD5 of "[path].pc_entitytype"
    // (checked offline) and a TEMP in the hash list.
    const Expected elusive[] = {
        {&fr::kElusiveVesperBrick, "elusive_vesper", "assembly:/_pro/scenes/missions/ancestral/elusive_vesper.brick",
         0x0033C24EEA8C6CE8ull},
        {&fr::kElusiveBrambleBrick, "elusive_bramble", "assembly:/_pro/scenes/missions/ancestral/elusive_bramble.brick",
         0x00523CD890BED552ull},
        {&fr::kElusiveRadlerBrick, "elusive_radler",
         "assembly:/_pro/scenes/missions/edgy/mission_fox/elusive_radler.brick", 0x005FDEBBAD94D62Cull},
        {&fr::kElusiveClericoBrick, "elusive_clerico", "assembly:/_pro/scenes/missions/elegant/elusive_clerico.brick",
         0x00F3756BC7320F9Full},
        {&fr::kElusiveJockeyclubBrick, "elusive_jockeyclub",
         "assembly:/_pro/scenes/missions/elegant/elusive_jockeyclub.brick", 0x00174B7F78F90C07ull},
        {&fr::kElusiveGoldendoubletBrick, "elusive_goldendoublet",
         "assembly:/_pro/scenes/missions/theark/elusive_goldendoublet.brick", 0x0071D0E4863B00AFull},
        {&fr::kElusiveSkittlebombBrick, "elusive_skittlebomb",
         "assembly:/_pro/scenes/missions/theark/elusive_skittlebomb.brick", 0x000B6DF156AAD815ull},
        {&fr::kElusiveHighballBrick, "elusive_highball",
         "assembly:/_pro/scenes/missions/colombia/elusive_highball.brick", 0x008BF14772B57FFEull},
    };
    for (const Expected& e : elusive) {
        CAPTURE(e.name);
        CHECK(e.brick->name == e.name);
        CHECK(e.brick->path == e.path);
        CHECK(e.brick->rid == e.rid);
        CHECK((e.brick->rid >> 56) == 0);
        CHECK(rids.insert(e.brick->rid).second);
    }
}

TEST_CASE("variant_option_bricks") {
    using Bricks = std::vector<const fr::VariantBrick*>;
    CHECK(fr::kPatientZero.bricks == Bricks {&fr::kTodFluBrick, &fr::kGfxFluBrick, &fr::kEaFluBrick});
    CHECK(fr::kSnowFestival.bricks == Bricks {&fr::kScenarioMamushiBrick});
    CHECK(fr::kReflection.bricks == Bricks {&fr::kScenarioBelliniBrick});
    CHECK(fr::kHerbalistParty.bricks ==
          Bricks {&fr::kScenarioGreentiniBrick, &fr::kScenarioGreentiniEaBrick, &fr::kScenarioGreentiniLightingBrick});
    CHECK(fr::kTheRage.bricks == Bricks {&fr::kElusiveRedsnapperBrick});
    CHECK(fr::kHarbinger.bricks == Bricks {&fr::kScenarioFilurBrick});
    CHECK(fr::kTheDrop.bricks == Bricks {&fr::kElusiveTomorrowlandBrick});
    // Freelancer Variations' skunk/tod_sunset is not a game file: never appended.
    CHECK(fr::kSerialKiller.bricks == Bricks {&fr::kElusiveAlabamaslammerBrick});
    // One elusive target layer each, never with a time of day.
    CHECK(fr::kProcurers.bricks == Bricks {&fr::kElusiveVesperBrick});
    CHECK(fr::kCollector.bricks == Bricks {&fr::kElusiveBrambleBrick});
    CHECK(fr::kLiability.bricks == Bricks {&fr::kElusiveRadlerBrick});
    CHECK(fr::kHeartbreaker.bricks == Bricks {&fr::kElusiveClericoBrick});
    CHECK(fr::kIconoclast.bricks == Bricks {&fr::kElusiveJockeyclubBrick});
    CHECK(fr::kAppraiser.bricks == Bricks {&fr::kElusiveGoldendoubletBrick});
    CHECK(fr::kStowaway.bricks == Bricks {&fr::kElusiveSkittlebombBrick});
    CHECK(fr::kRevolutionary.bricks == Bricks {&fr::kElusiveHighballBrick});
    for (const fr::VariantOption* option : {&fr::kProcurers, &fr::kCollector, &fr::kLiability, &fr::kHeartbreaker,
                                            &fr::kIconoclast, &fr::kAppraiser, &fr::kStowaway, &fr::kRevolutionary}) {
        CAPTURE(option->name);
        CHECK(!option->notTargets.empty()); // its contract's targets
        CHECK(option->removed.empty());
    }
    CHECK(fr::kTodEbolaNight.bricks == Bricks {&fr::kTodEbolaNightBrick});
    CHECK(fr::kLandslide.bricks == Bricks {&fr::kLightingMambaBrick, &fr::kTodSummerMorningClearBrick,
                                           &fr::kDressingBeachMambaBrick, &fr::kDressingChurchMambaBrick,
                                           &fr::kDressingOldtownMambaBrick, &fr::kDressingTownhallMambaBrick,
                                           &fr::kDressingTunnelsMambaBrick});
    CHECK(fr::kLightingCopperhead.bricks == Bricks {&fr::kTodSummerNightClearBrick, &fr::kLightingCopperheadBrick});
    // Never Landslide's story or its blocking dressing.
    for (const fr::VariantBrick* brick : fr::kLandslide.bricks) {
        CHECK(brick->path.find("mission_mamba") == std::string_view::npos);
        CHECK(brick->path.find("blockedoff") == std::string_view::npos);
    }
    CHECK(fr::kTheSource.bricks == Bricks {&fr::kMissionZikaBrick, &fr::kTodZikaNightBrick, &fr::kChallengesZikaBrick});
    CHECK(fr::kHolidayHoarders.bricks == Bricks {&fr::kLocationParisNoelBrick});

    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        // Lighting-only options skip the check; everything with set dressing or NPCs runs it.
        const bool lightingOnly = option == &fr::kTodEbolaNight || option == &fr::kLightingCopperhead ||
                                  option == &fr::kTheBadBoy || option == &fr::kTheEntertainer;
        CAPTURE(option->name);
        CHECK(option->check == !lightingOnly);
        CHECK_FALSE(option->bricks.empty());
        CHECK(fr::FindVariantOption(option->name) == option);
    }
    CHECK(fr::AllVariantOptions().size() == fr::kVariantOptionCount);
    CHECK_FALSE(fr::FindVariantOption("none"));
    CHECK_FALSE(fr::FindVariantOption("gfx_flu"));
}

TEST_CASE("variant_chance_defaults") {
    // The shipped defaults.
    const std::vector<std::pair<const fr::VariantOption*, int32_t>> defaults {{&fr::kTheSource, 30},
                                                                              {&fr::kTheWarlord, 10},
                                                                              {&fr::kTheFoodCritic, 5},
                                                                              {&fr::kTheExDictator, 20},
                                                                              {&fr::kTheVeteran, 5},
                                                                              {&fr::kTheInfiltrator, 25},
                                                                              {&fr::kLust, 25},
                                                                              {&fr::kTheDrop, 35},
                                                                              {&fr::kLiability, 20},
                                                                              {&fr::kGluttony, 20},
                                                                              {&fr::kTheRage, 35},
                                                                              {&fr::kTheSplitter, 25},
                                                                              {&fr::kTheBookkeeper, 25},
                                                                              {&fr::kTheChameleon, 20},
                                                                              {&fr::kTheMercenary, 35},
                                                                              {&fr::kGardenShow, 30},
                                                                              {&fr::kSloth, 15},
                                                                              {&fr::kCollector, 10},
                                                                              {&fr::kHarbinger, 30},
                                                                              {&fr::kProcurers, 10},
                                                                              {&fr::kTheAscensionist, 40},
                                                                              {&fr::kHerbalistParty, 55},
                                                                              {&fr::kPatientZero, 25},
                                                                              {&fr::kSnowFestival, 30},
                                                                              {&fr::kTheController, 10},
                                                                              {&fr::kTheFugitive, 10},
                                                                              {&fr::kReflection, 5},
                                                                              {&fr::kTheSurgeons, 10},
                                                                              {&fr::kAppraiser, 25},
                                                                              {&fr::kStowaway, 15},
                                                                              {&fr::kTheWizard, 35},
                                                                              {&fr::kTheAngelOfDeath, 10},
                                                                              {&fr::kTheEntertainer, 35},
                                                                              {&fr::kTheExtractor, 10},
                                                                              {&fr::kTheFixer, 10},
                                                                              {&fr::kTheGunrunner, 25},
                                                                              {&fr::kEnvy, 20},
                                                                              {&fr::kHeartbreaker, 30},
                                                                              {&fr::kIconoclast, 25},
                                                                              {&fr::kASilverTongue, 40},
                                                                              {&fr::kTheUndying, 25},
                                                                              {&fr::kTheUndyingReturns, 5},
                                                                              {&fr::kIllusionsOfGrandeur, 40},
                                                                              {&fr::kHolidayHoarders, 15},
                                                                              {&fr::kTheBanker, 30},
                                                                              {&fr::kTheBlackHat, 5},
                                                                              {&fr::kTheBlackmailer, 5},
                                                                              {&fr::kTheBroker, 5},
                                                                              {&fr::kTheChef, 5},
                                                                              {&fr::kTheDirector, 5},
                                                                              {&fr::kTheForger, 5},
                                                                              {&fr::kTheIdentityThief, 5},
                                                                              {&fr::kThePaparazzo, 5},
                                                                              {&fr::kThePharmacist, 5},
                                                                              {&fr::kTheSensation, 5},
                                                                              {&fr::kEmbraceOfTheSerpent, 30},
                                                                              {&fr::kRevolutionary, 35},
                                                                              {&fr::kLandslide, 20},
                                                                              {&fr::kTodEbolaNight, 15},
                                                                              {&fr::kTheBadBoy, 10},
                                                                              {&fr::kTheCongressman, 5},
                                                                              {&fr::kTheDeceivers, 5},
                                                                              {&fr::kTheEnforcer, 5},
                                                                              {&fr::kTheGuru, 5},
                                                                              {&fr::kLightingCopperhead, 10},
                                                                              {&fr::kThePrince, 10},
                                                                              {&fr::kTheTwin, 5},
                                                                              {&fr::kTheWildcard, 5},
                                                                              {&fr::kABitterPill, 30},
                                                                              {&fr::kSerialKiller, 40}};
    CHECK(defaults.size() == fr::AllVariantOptions().size());
    for (const auto& entry : defaults) {
        CAPTURE(entry.first->name);
        CHECK(entry.first->defaultChance == entry.second);
    }

    // No map's defaults add up to more than 100 (none needs scaling).
    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        int32_t sum = 0;
        for (const fr::VariantOption* sibling : fr::VariantSiblings(*option))
            sum += sibling->defaultChance;
        CHECK(sum <= 100);
    }

    for (const fr::VariantOption* option : fr::AllVariantOptions())
        CHECK(fr::ChanceOf(kDefaults, *option) == option->defaultChance);

    fr::VariantChances chances = kDefaults;
    fr::SetChance(chances, fr::kLandslide, 12);
    CHECK(fr::ChanceOf(chances, fr::kLandslide) == 12);
    CHECK(fr::ChanceOf(chances, fr::kPatientZero) == 25);
    CHECK(fr::VariantChanceKey(fr::kTheSource) == "variant_chance_the_source");
}

TEST_CASE("variant_chance_settings_rows") {
    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        const std::string key = fr::VariantChanceKey(*option);
        CAPTURE(key);
        const fr::SettingInfo* info = fr::FindSetting(key);
        REQUIRE(info != nullptr);
        CHECK(info->type == fr::SettingType::Int);
        CHECK(info->min == 0);
        CHECK(info->max == 100);
        CHECK(info->unit == "%");
        CHECK(info->parent == "variants");
        CHECK(info->group == fr::FindSetting("variants")->group);
        CHECK(fr::SettingNumberOr(*info, info->def) == option->defaultChance);
        CHECK(info->label.find(": ") != std::string_view::npos); // "Bangkok: The Source"
        // Player-facing help: plain, no development notes.
        CHECK(info->help.find("Untested") == std::string_view::npos);
    }
    CHECK(fr::FindSetting("variant_chance_the_source")->label == "Bangkok: The Source");
    CHECK_FALSE(fr::FindSetting("variant_chance_tod_flu"));
    CHECK_FALSE(fr::FindSetting("variant_chance_tod_zika_night"));
    CHECK(fr::FindSetting("variant_chance_holiday_hoarders")->label == "Paris: Holiday Hoarders");
    CHECK(fr::FindSetting("variant_chance_snow_festival")->label == "Hokkaido: Snow Festival");
    CHECK(fr::FindSetting("variant_chance_reflection")->label == "Hokkaido: The Reflection");
    CHECK(fr::FindSetting("variant_chance_herbalist_party")->label == "Haven: The Herbalist");
    CHECK(fr::FindSetting("variant_chance_the_rage")->label == "Chongqing: The Rage");
    CHECK_FALSE(fr::FindSetting("variant_chance_colorado_day")); // removed
    CHECK_FALSE(fr::FindSetting("variant_chance_the_vector"));   // removed: the Sniper Assassin mode
    CHECK(fr::FindSetting("variant_chance_harbinger")->label == "Dartmoor: The Harbinger");
    CHECK(fr::FindSetting("variant_chance_the_drop")->label == "Berlin: The Drop");
    CHECK(fr::FindSetting("variant_chance_serial_killer")->label == "Whittleton Creek: The Serial Killer");
    CHECK(fr::FindSetting("variant_chance_procurers")->label == "Dartmoor: The Procurers");
    CHECK(fr::FindSetting("variant_chance_collector")->label == "Dartmoor: The Collector");
    CHECK(fr::FindSetting("variant_chance_liability")->label == "Berlin: The Liability");
    CHECK(fr::FindSetting("variant_chance_heartbreaker")->label == "Mendoza: The Heartbreaker");
    CHECK(fr::FindSetting("variant_chance_iconoclast")->label == "Mendoza: The Iconoclast");
    CHECK(fr::FindSetting("variant_chance_appraiser")->label == "Isle of Sgail: The Appraiser");
    CHECK(fr::FindSetting("variant_chance_stowaway")->label == "Isle of Sgail: The Stowaway");
    CHECK(fr::FindSetting("variant_chance_revolutionary")->label == "Santa Fortuna: The Revolutionary");
    // The two with their own navmesh say NPCs walk elsewhere.
    for (const char* key : {"variant_chance_procurers", "variant_chance_heartbreaker"})
        CHECK(fr::FindSetting(key)->help.find("where NPCs can walk") != std::string_view::npos);
    for (const char* key : {"variant_chance_collector", "variant_chance_liability", "variant_chance_iconoclast",
                            "variant_chance_appraiser", "variant_chance_stowaway", "variant_chance_revolutionary"})
        CHECK(fr::FindSetting(key)->help.find("a target may be missing") != std::string_view::npos);
    CHECK(fr::FindSetting("variant_chance_patient_zero")->help.find("quarantine") != std::string_view::npos);

    // Each label starts with its option's place.
    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        CAPTURE(option->name);
        const fr::SettingInfo* info = fr::FindSetting(fr::VariantChanceKey(*option));
        REQUIRE(info != nullptr);
        CHECK(info->label.substr(0, info->label.find(':')) == fr::VariantPlaceName(*option));
    }
}

TEST_CASE("variant_siblings") {
    const auto names = [](const fr::VariantOption& option) {
        std::vector<std::string> out;
        for (const fr::VariantOption* sibling : fr::VariantSiblings(option))
            out.emplace_back(sibling->name);
        return out;
    };
    const std::vector<std::string> bangkok {"the_source",      "the_warlord", "the_food_critic",
                                            "the_ex_dictator", "the_veteran", "the_infiltrator"};
    CHECK(names(fr::kTheSource) == bangkok);
    CHECK(names(fr::kTheWarlord) == bangkok);
    CHECK(names(fr::kReflection) == std::vector<std::string> {"patient_zero", "snow_festival", "reflection",
                                                              "the_surgeons", "the_fugitive", "the_controller"});
    CHECK(names(fr::kTheRage) == std::vector<std::string> {"the_rage", "the_splitter", "gluttony"});
    CHECK(names(fr::kLandslide) == std::vector<std::string> {"tod_ebola_night", "landslide", "lighting_copperhead",
                                                             "the_congressman", "the_guru", "the_prince", "the_twin",
                                                             "the_wildcard", "the_deceivers", "the_enforcer",
                                                             "the_bad_boy"});
    CHECK(names(fr::kHolidayHoarders) == std::vector<std::string> {"holiday_hoarders", "the_forger", "the_sensation",
                                                                   "the_black_hat", "the_pharmacist", "the_paparazzo",
                                                                   "the_chef", "the_broker", "the_identity_thief",
                                                                   "the_blackmailer", "the_director", "the_banker"});

    CHECK(fr::VariantPlaceName(fr::kTheSource) == "Bangkok");
    CHECK(fr::VariantPlaceName(fr::kPatientZero) == "Hokkaido");
    CHECK(fr::VariantPlaceName(fr::kLightingCopperhead) == "Sapienza");
    CHECK(fr::VariantPlaceName(fr::kHolidayHoarders) == "Paris");
    CHECK(fr::VariantPlaceName(fr::kSnowFestival) == "Hokkaido");
    CHECK(fr::VariantPlaceName(fr::kReflection) == "Hokkaido");
    CHECK(fr::VariantPlaceName(fr::kHerbalistParty) == "Haven");
    CHECK(fr::VariantPlaceName(fr::kTheRage) == "Chongqing");
    CHECK(fr::VariantPlaceName(fr::kHarbinger) == "Dartmoor");
    CHECK(fr::VariantPlaceName(fr::kTheDrop) == "Berlin");
    CHECK(fr::VariantPlaceName(fr::kSerialKiller) == "Whittleton Creek"); // a two-word place
    CHECK(names(fr::kSerialKiller) == std::vector<std::string> {"serial_killer", "a_bitter_pill"});
    CHECK(names(fr::kCollector) ==
          std::vector<std::string> {"harbinger", "procurers", "collector", "garden_show", "sloth"});
    CHECK(names(fr::kLiability) == std::vector<std::string> {"the_drop", "liability", "lust"});
    CHECK(names(fr::kHeartbreaker) == std::vector<std::string> {"heartbreaker", "iconoclast", "envy"});
    CHECK(names(fr::kStowaway) == std::vector<std::string> {"appraiser", "stowaway", "the_wizard"});
    CHECK(names(fr::kTheGunrunner) == std::vector<std::string> {"the_fixer", "the_gunrunner", "the_angel_of_death",
                                                                "the_extractor", "the_entertainer"});
    CHECK(names(fr::kTheAscensionist) == std::vector<std::string> {"the_ascensionist"});
    CHECK(names(fr::kTheUndying) == std::vector<std::string> {"the_undying", "a_silver_tongue", "the_undying_returns"});
    CHECK(names(fr::kIllusionsOfGrandeur) == std::vector<std::string> {"illusions_of_grandeur"});
    CHECK(fr::VariantPlaceName(fr::kIllusionsOfGrandeur) == "Mumbai");
    CHECK(fr::VariantPlaceName(fr::kABitterPill) == "Whittleton Creek");
    CHECK(fr::VariantPlaceName(fr::kTheFixer) == "Marrakesh");
    CHECK(fr::VariantPlaceName(fr::kTheAscensionist) == "Dubai");
    CHECK(fr::VariantPlaceName(fr::kTheUndying) == "Miami");
    CHECK(fr::VariantPlaceName(fr::kGardenShow) == "Dartmoor");
    CHECK(fr::VariantPlaceName(fr::kTheDirector) == "Paris");
    CHECK(names(fr::kRevolutionary) == std::vector<std::string> {"revolutionary", "embrace_of_the_serpent"});
    CHECK(fr::VariantPlaceName(fr::kProcurers) == "Dartmoor");
    CHECK(fr::VariantPlaceName(fr::kLiability) == "Berlin");
    CHECK(fr::VariantPlaceName(fr::kIconoclast) == "Mendoza");
    CHECK(fr::VariantPlaceName(fr::kAppraiser) == "Isle of Sgail"); // a three-word place
    CHECK(fr::VariantPlaceName(fr::kRevolutionary) == "Santa Fortuna");

    // Every option is on exactly one place.
    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        CAPTURE(option->name);
        const auto siblings = fr::VariantSiblings(*option);
        CHECK(std::find(siblings.begin(), siblings.end(), option) != siblings.end());
    }
}

TEST_CASE("variant_fit_chances") {
    // Fits already: only the edited one changes. Bangkok: The Source 30, The Warlord 30, the rest 0; other
    // maps at their defaults.
    fr::VariantChances bangkok = kDefaults;
    for (const fr::VariantOption* sibling : fr::VariantSiblings(fr::kTheSource))
        fr::SetChance(bangkok, *sibling, 0);
    fr::SetChance(bangkok, fr::kTheSource, 30);
    fr::SetChance(bangkok, fr::kTheWarlord, 30);
    fr::VariantChances fit = fr::FitVariantChances(bangkok, fr::kTheSource, 50);
    CHECK(fr::ChanceOf(fit, fr::kTheSource) == 50);
    CHECK(fr::ChanceOf(fit, fr::kTheWarlord) == 30);
    fit = fr::FitVariantChances(bangkok, fr::kTheSource, 70);
    CHECK(fr::ChanceOf(fit, fr::kTheWarlord) == 30); // exactly 100: unchanged

    // Over 100: the other one takes the rest.
    fit = fr::FitVariantChances(bangkok, fr::kTheSource, 85);
    CHECK(fr::ChanceOf(fit, fr::kTheSource) == 85);
    CHECK(fr::ChanceOf(fit, fr::kTheWarlord) == 15);
    // Other maps are untouched.
    for (const fr::VariantOption* option :
         {&fr::kPatientZero, &fr::kTodEbolaNight, &fr::kLandslide, &fr::kLightingCopperhead, &fr::kHolidayHoarders})
        CHECK(fr::ChanceOf(fit, *option) == fr::ChanceOf(kDefaults, *option));

    // 100: the others go to 0.
    const fr::VariantChances sapienza =
        Chances({{&fr::kTodEbolaNight, 20}, {&fr::kLandslide, 30}, {&fr::kLightingCopperhead, 50}});
    fit = fr::FitVariantChances(sapienza, fr::kLandslide, 100);
    CHECK(fr::ChanceOf(fit, fr::kTodEbolaNight) == 0);
    CHECK(fr::ChanceOf(fit, fr::kLandslide) == 100);
    CHECK(fr::ChanceOf(fit, fr::kLightingCopperhead) == 0);

    // Proportional: 20 and 50 share 30 as 8.57 and 21.43; the last point goes
    // to the larger remainder (8.57's).
    fit = fr::FitVariantChances(sapienza, fr::kLandslide, 70);
    CHECK(fr::ChanceOf(fit, fr::kTodEbolaNight) == 9);
    CHECK(fr::ChanceOf(fit, fr::kLandslide) == 70);
    CHECK(fr::ChanceOf(fit, fr::kLightingCopperhead) == 21);

    // Ties go to the earlier option; a 0 stays 0.
    fit =
        fr::FitVariantChances(Chances({{&fr::kTodEbolaNight, 40}, {&fr::kLandslide, 40}}), fr::kLightingCopperhead, 75);
    CHECK(fr::ChanceOf(fit, fr::kTodEbolaNight) == 13);
    CHECK(fr::ChanceOf(fit, fr::kLandslide) == 12);
    CHECK(fr::ChanceOf(fit, fr::kLightingCopperhead) == 75);
    fit = fr::FitVariantChances(Chances({{&fr::kTodEbolaNight, 90}}), fr::kLightingCopperhead, 60);
    CHECK(fr::ChanceOf(fit, fr::kTodEbolaNight) == 40);
    CHECK(fr::ChanceOf(fit, fr::kLandslide) == 0);

    // Lowering never raises the others back.
    fit = fr::FitVariantChances(Chances({{&fr::kTheSource, 90}, {&fr::kTheWarlord, 10}}), fr::kTheSource, 20);
    CHECK(fr::ChanceOf(fit, fr::kTheWarlord) == 10);

    // Out of range: the edit is clamped; a negative other counts as 0 and an
    // over-100 other is lowered.
    fit = fr::FitVariantChances(Chances({{&fr::kTheWarlord, 150}}), fr::kTheSource, 130);
    CHECK(fr::ChanceOf(fit, fr::kTheSource) == 100);
    CHECK(fr::ChanceOf(fit, fr::kTheWarlord) == 0);
    fit = fr::FitVariantChances(Chances({{&fr::kPatientZero, -5}, {&fr::kSnowFestival, 0}}), fr::kSnowFestival, -20);
    CHECK(fr::ChanceOf(fit, fr::kSnowFestival) == 0);
    CHECK(fr::ChanceOf(fit, fr::kPatientZero) == -5); // fits: unchanged (the walk reads it as 0)

    // Every edit of every option leaves its map at or under 100, none
    // negative, none raised.
    const fr::VariantChances start =
        Chances({{&fr::kTheSource, 64},        {&fr::kTheWarlord, 77},    {&fr::kPatientZero, 33},
                 {&fr::kTodEbolaNight, 17},    {&fr::kLandslide, 61},     {&fr::kLightingCopperhead, 58},
                 {&fr::kHolidayHoarders, 100}, {&fr::kSnowFestival, 45},  {&fr::kReflection, 12},
                 {&fr::kHerbalistParty, 70},   {&fr::kTheRage, 100},      {&fr::kTheMercenary, 3},
                 {&fr::kHarbinger, 50},        {&fr::kTheDrop, 99},       {&fr::kSerialKiller, 100},
                 {&fr::kProcurers, 40},        {&fr::kCollector, 35},     {&fr::kLiability, 8},
                 {&fr::kHeartbreaker, 66},     {&fr::kIconoclast, 66},    {&fr::kAppraiser, 51},
                 {&fr::kStowaway, 49},         {&fr::kRevolutionary, 100}});
    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        for (int32_t percent = 0; percent <= 100; ++percent) {
            CAPTURE(option->name);
            CAPTURE(percent);
            const fr::VariantChances after = fr::FitVariantChances(start, *option, percent);
            CHECK(fr::ChanceOf(after, *option) == percent);
            int32_t sum = 0;
            for (const fr::VariantOption* sibling : fr::VariantSiblings(*option)) {
                sum += fr::ChanceOf(after, *sibling);
                CHECK(fr::ChanceOf(after, *sibling) >= 0);
                if (sibling != option)
                    CHECK(fr::ChanceOf(after, *sibling) <= fr::ChanceOf(start, *sibling));
            }
            CHECK(sum <= 100);
            CHECK(fr::FitVariantChances(start, *option, percent) == after); // deterministic
        }
    }
}

TEST_CASE("variant_walk_boundaries") {
    const fr::VariantMap& mild = *fr::FindVariantMap(kBangkokMild);
    // The Source alone at 30 %: the later options' spans are empty.
    const fr::VariantWalk walk = fr::MakeVariantWalk(mild, Chances({{&fr::kTheSource, 30}}));
    CHECK(walk.chances == std::vector<int32_t> {30, 0, 0, 0, 0, 0});
    CHECK(walk.upTo == std::vector<uint32_t> {3000, 3000, 3000, 3000, 3000, 3000});
    CHECK(walk.sum == 30);
    CHECK_FALSE(walk.scaled);
    CHECK(fr::WalkVariant(walk, 0) == &fr::kTheSource);
    CHECK(fr::WalkVariant(walk, 2999) == &fr::kTheSource);
    CHECK(fr::WalkVariant(walk, 3000) == nullptr);
    CHECK(fr::WalkVariant(walk, 9999) == nullptr);
    CHECK(fr::DescribeWalk(walk) == "the_source 30%, the_warlord 0%, the_food_critic 0%, the_ex_dictator 0%, "
                                    "the_veteran 0%, the_infiltrator 0%, none 70%");
    // A second option's span starts where the first one's ends.
    const fr::VariantWalk two = fr::MakeVariantWalk(mild, Chances({{&fr::kTheSource, 30}, {&fr::kTheWarlord, 30}}));
    CHECK(two.upTo == std::vector<uint32_t> {3000, 6000, 6000, 6000, 6000, 6000});
    CHECK(fr::WalkVariant(two, 3000) == &fr::kTheWarlord);
    CHECK(fr::WalkVariant(two, 5999) == &fr::kTheWarlord);
    CHECK(fr::WalkVariant(two, 6000) == nullptr);

    // 100 % total: never none.
    const fr::VariantWalk full = fr::MakeVariantWalk(
        *fr::FindVariantMap(kSapienzaMild),
        Chances({{&fr::kTodEbolaNight, 20}, {&fr::kLandslide, 30}, {&fr::kLightingCopperhead, 50}}));
    CHECK(full.upTo ==
          std::vector<uint32_t> {2000, 5000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000});
    CHECK_FALSE(full.scaled);
    CHECK(fr::WalkVariant(full, 9999) == &fr::kLightingCopperhead);

    // Out-of-range chances are clamped.
    const fr::VariantWalk clamped =
        fr::MakeVariantWalk(mild, Chances({{&fr::kTheSource, -5}, {&fr::kTheWarlord, 150}}));
    CHECK(clamped.chances == std::vector<int32_t> {0, 100, 0, 0, 0, 0});
    CHECK(clamped.upTo == std::vector<uint32_t> {0, 10000, 10000, 10000, 10000, 10000});
}

TEST_CASE("variant_walk_zero_percent") {
    const fr::VariantMap& hokkaido = *fr::FindVariantMap(kHokkaidoMild);
    // The options after patient_zero are 0 %: empty spans, never picked.
    const fr::VariantWalk walk = fr::MakeVariantWalk(hokkaido, Chances({{&fr::kPatientZero, 50}}));
    CHECK(walk.upTo == std::vector<uint32_t> {5000, 5000, 5000, 5000, 5000, 5000});
    for (uint32_t roll = 0; roll < fr::kVariantRollRange; ++roll) {
        const fr::VariantOption* pick = fr::WalkVariant(walk, roll);
        CHECK(pick == (roll < 5000 ? &fr::kPatientZero : nullptr));
    }
    CHECK(fr::DescribeWalk(walk) == "patient_zero 50%, snow_festival 0%, reflection 0%, the_surgeons 0%, "
                                    "the_fugitive 0%, the_controller 0%, none 50%");

    // Everything 0: always the normal map.
    for (const char* scene : kAllScenes) {
        for (int32_t seed = -50; seed <= 300; ++seed) {
            const fr::VariantPlan plan = fr::PlanVariant(*fr::FindVariantMap(scene), seed, scene, Chances(), {});
            CHECK(plan.option == nullptr);
            CHECK(plan.action == fr::VariantAction::Unchanged);
        }
    }

    // Defaults: an option at 0 % never comes from the seed.
    for (const char* scene : kAllScenes) {
        for (int32_t seed = 1; seed <= 500; ++seed) {
            const fr::VariantPlan plan = fr::PlanVariant(*fr::FindVariantMap(scene), seed, scene, kDefaults, {});
            if (plan.option)
                CHECK(plan.option->defaultChance > 0);
        }
    }
}

TEST_CASE("variant_walk_scaled_over_100") {
    const fr::VariantMap& sapienza = *fr::FindVariantMap(kSapienzaMild);
    const fr::VariantWalk walk = fr::MakeVariantWalk(
        sapienza, Chances({{&fr::kTodEbolaNight, 100}, {&fr::kLandslide, 50}, {&fr::kLightingCopperhead, 50}}));
    CHECK(walk.sum == 200);
    CHECK(walk.scaled);
    CHECK(walk.upTo ==
          std::vector<uint32_t> {5000, 7500, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000});
    CHECK(fr::WalkVariant(walk, 9999) == &fr::kLightingCopperhead);
    CHECK(fr::DescribeWalk(walk) ==
          "tod_ebola_night 50% (set 100%), landslide 25% (set 50%), lighting_copperhead 25% (set 50%), "
          "the_congressman 0% (set 0%), the_guru 0% (set 0%), the_prince 0% (set 0%), the_twin 0% (set 0%), "
          "the_wildcard 0% (set 0%), the_deceivers 0% (set 0%), the_enforcer 0% (set 0%), the_bad_boy 0% (set 0%), "
          "none 0%");

    // Uneven: cumulative rounding, the last bound is exact.
    const fr::VariantWalk thirds = fr::MakeVariantWalk(
        sapienza, Chances({{&fr::kTodEbolaNight, 70}, {&fr::kLandslide, 70}, {&fr::kLightingCopperhead, 70}}));
    CHECK(thirds.upTo ==
          std::vector<uint32_t> {3333, 6666, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000});
    CHECK(fr::WalkShareText(thirds, 0) == "33.33");
    CHECK(fr::WalkShareText(thirds, 2) == "33.34");

    // 101 % is scaled; exactly 100 % is not.
    const fr::VariantMap& bangkok = *fr::FindVariantMap(kBangkokHot);
    CHECK(fr::MakeVariantWalk(bangkok, Chances({{&fr::kTheSource, 51}, {&fr::kTheWarlord, 50}})).scaled);
    CHECK_FALSE(fr::MakeVariantWalk(bangkok, Chances({{&fr::kTheSource, 50}, {&fr::kTheWarlord, 50}})).scaled);

    // Scaled, the shares keep their ratio over many seeds.
    const fr::VariantChances two = Chances({{&fr::kTheSource, 90}, {&fr::kTheWarlord, 60}});
    int source = 0;
    int warlord = 0;
    constexpr int kSeeds = 4000;
    for (int32_t seed = 1; seed <= kSeeds; ++seed) {
        const fr::VariantPlan plan = fr::PlanVariant(bangkok, seed, kBangkokHot, two, {});
        source += plan.option == &fr::kTheSource ? 1 : 0;
        warlord += plan.option == &fr::kTheWarlord ? 1 : 0;
        CHECK(plan.option != nullptr);
    }
    CHECK(source >= kSeeds * 55 / 100);
    CHECK(source <= kSeeds * 65 / 100);
    CHECK(source + warlord == kSeeds);
}

TEST_CASE("variant_roll_determinism") {
    for (const int32_t seed : {1, 42, -7, 123456789}) {
        CHECK(fr::VariantRoll(seed, kBangkokHot) == fr::VariantRoll(seed, kBangkokHot));
        CHECK(fr::VariantRoll(seed, kBangkokHot) < fr::kVariantRollRange);
        // The scene is hashed lower-cased.
        CHECK(fr::VariantRoll(seed, kBangkokMild) ==
              fr::VariantRoll(seed, "ASSEMBLY:/_PRO/SCENES/MISSIONS/BANGKOK/SCENE_TIGER_MILD_SICBO.ENTITY"));
    }

    // Same seed and settings: the same pick, and the plan uses the roll.
    const std::vector<std::string> bricks {kMissionMildSicbo};
    for (const char* scene : kAllScenes) {
        const fr::VariantMap& map = *fr::FindVariantMap(scene);
        for (int32_t seed = 1; seed <= 100; ++seed) {
            const fr::VariantPlan a = fr::PlanVariant(map, seed, scene, kDefaults, bricks);
            const fr::VariantPlan b = fr::PlanVariant(map, seed, scene, kDefaults, bricks);
            CHECK(a.roll == fr::VariantRoll(seed, scene));
            CHECK(a.roll == b.roll);
            CHECK(a.option == b.option);
            CHECK(a.seeded == fr::WalkVariant(a.walk, a.roll));
            CHECK(a.option == a.seeded);
            CHECK_FALSE(a.forced);
        }
    }
}

TEST_CASE("variant_pick_depends_on_seed_and_scene") {
    // Hokkaido's patient_zero at 20 %: about a fifth, and the two Hokkaido maps
    // (the hot one without patient_zero) do not always agree.
    constexpr int kSeeds = 4000;
    int flu = 0;
    int differ = 0;
    const fr::VariantChances hokkaido =
        Chances({{&fr::kPatientZero, 20}, {&fr::kSnowFestival, 30}, {&fr::kReflection, 20}});
    for (int32_t seed = 1; seed <= kSeeds; ++seed) {
        const fr::VariantPlan mild =
            fr::PlanVariant(*fr::FindVariantMap(kHokkaidoMild), seed, kHokkaidoMild, hokkaido, {});
        const fr::VariantPlan hot =
            fr::PlanVariant(*fr::FindVariantMap(kHokkaidoHot), seed, kHokkaidoHot, hokkaido, {});
        flu += mild.option == &fr::kPatientZero ? 1 : 0;
        differ += mild.option != hot.option ? 1 : 0;
    }
    CHECK(flu >= kSeeds * 16 / 100);
    CHECK(flu <= kSeeds * 24 / 100);
    CHECK(differ > kSeeds / 4);

    // Bangkok with The Source alone at 30 %: about 30 / 70 (The Source or the normal map).
    const fr::VariantChances bangkok = Chances({{&fr::kTheSource, 30}});
    int source = 0;
    for (int32_t seed = 1; seed <= kSeeds; ++seed) {
        const fr::VariantPlan plan =
            fr::PlanVariant(*fr::FindVariantMap(kBangkokMild), seed, kBangkokMild, bangkok, {});
        CHECK((plan.option == nullptr || plan.option == &fr::kTheSource));
        source += plan.option == &fr::kTheSource ? 1 : 0;
    }
    CHECK(source >= kSeeds * 26 / 100);
    CHECK(source <= kSeeds * 34 / 100);
}

TEST_CASE("variant_has_brick") {
    const std::vector<std::string> bricks {kMissionMildSicbo,
                                           "assembly:/_PRO/Scenes/Missions/Bangkok/TOD_ZIKA_NIGHT.brick"};
    CHECK(fr::HasBrick(bricks, kMissionMildSicbo));
    CHECK(fr::HasBrick(bricks, fr::kTodZikaNightBrick.path));
    CHECK_FALSE(fr::HasBrick(bricks, fr::kMissionZikaBrick.path));
    CHECK_FALSE(fr::HasBrick({}, fr::kTodFluBrick.path));
}

TEST_CASE("variant_plan_actions") {
    const fr::VariantMap& hot = *fr::FindVariantMap(kBangkokHot);
    const std::vector<std::string> base {kMissionMildSicbo};

    SUBCASE("none is unchanged") {
        const fr::VariantPlan plan = fr::PlanVariant(hot, 1, kBangkokHot, Chances(), base);
        CHECK(plan.action == fr::VariantAction::Unchanged);
        CHECK(plan.map == &hot);
        CHECK(plan.bricks.empty());
    }

    SUBCASE("add") {
        const fr::VariantChances source = Chances({{&fr::kTheSource, 100}});
        CHECK(fr::PlanVariant(hot, 1, kBangkokHot, source, base).action == fr::VariantAction::Add);
        CHECK(fr::PlanVariant(hot, 1, kBangkokHot, source, {}).action == fr::VariantAction::Add);
    }

    SUBCASE("already present, any case") {
        std::vector<std::string> bricks = base;
        for (const fr::VariantBrick* brick : fr::kTheSource.bricks) {
            std::string upper(brick->path);
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            bricks.push_back(upper);
        }
        CHECK(fr::PlanVariant(hot, 1, kBangkokHot, Chances({{&fr::kTheSource, 100}}), bricks).action ==
              fr::VariantAction::AlreadyPresent);
    }

    SUBCASE("one brick listed: the others still added") {
        std::vector<std::string> withZika = base;
        withZika.emplace_back(fr::kMissionZikaBrick.path);
        CHECK(fr::PlanVariant(hot, 1, kBangkokHot, Chances({{&fr::kTheSource, 100}}), withZika).action ==
              fr::VariantAction::Add);
    }
}

TEST_CASE("variant_plan_never_duplicates") {
    // Applying a plan's Adds and planning again never adds a brick twice,
    // for seeded picks and for every forced option.
    std::vector<std::string> forces {""};
    for (const fr::VariantOption* option : fr::AllVariantOptions())
        forces.emplace_back(option->name);
    fr::VariantChances all {};
    all.fill(40);

    for (const char* scene : kAllScenes) {
        const fr::VariantMap& map = *fr::FindVariantMap(scene);
        for (const std::string& force : forces) {
            for (int32_t seed = 1; seed <= 30; ++seed) {
                std::vector<std::string> bricks {kMissionMildSicbo};
                const fr::VariantPlan first = fr::PlanVariant(map, seed, scene, all, bricks, force);
                for (const fr::VariantBrickPlan& step : first.bricks) {
                    if (step.action == fr::VariantAction::Add)
                        bricks.emplace_back(step.brick->path);
                }

                const fr::VariantPlan again = fr::PlanVariant(map, seed, scene, all, bricks, force);
                CHECK(again.option == first.option);
                CHECK(again.action != fr::VariantAction::Add);
                CHECK(bricks.size() <= 1 + (first.option ? first.option->bricks.size() : 0));
            }
        }
    }
}

TEST_CASE("variant_force_parse") {
    CHECK(fr::ParseVariantForce("") == std::string());
    CHECK(fr::ParseVariantForce("   ") == std::string());
    CHECK(fr::ParseVariantForce("none") == std::string("none"));
    CHECK(fr::ParseVariantForce(" Patient_Zero ") == std::string("patient_zero"));
    CHECK_FALSE(fr::ParseVariantForce("tod_flu")); // a brick, not an option
    CHECK(fr::ParseVariantForce("THE_SOURCE") == std::string("the_source"));
    CHECK_FALSE(fr::ParseVariantForce("tod_zika_night")); // a brick of the_source
    CHECK_FALSE(fr::ParseVariantForce("tod_flu.brick"));
    CHECK_FALSE(fr::ParseVariantForce("zika"));
    CHECK_FALSE(fr::ParseVariantForce("mission_mild_zika"));
}

TEST_CASE("variant_force_overrides_chances") {
    const std::vector<std::string> base {kMissionMildSicbo};

    for (int32_t seed = 1; seed <= 50; ++seed) {
        // An option, forced: always that option, whatever its chance.
        const fr::VariantPlan pz =
            fr::PlanVariant(*fr::FindVariantMap(kHokkaidoMild), seed, kHokkaidoMild, kDefaults, base, "patient_zero");
        CHECK(pz.forced);
        CHECK_FALSE(pz.forceNotOffered);
        CHECK(pz.option == &fr::kPatientZero);
        CHECK(pz.action == fr::VariantAction::Add);
        // The seeded pick stays recorded.
        CHECK(pz.seeded == fr::WalkVariant(pz.walk, pz.roll));

        // A showdown has no Patient Zero (only its full version exists, for the mild contract): the seeded pick.
        const fr::VariantPlan pzHot =
            fr::PlanVariant(*fr::FindVariantMap(kHokkaidoHot), seed, kHokkaidoHot, kDefaults, base, "patient_zero");
        CHECK_FALSE(pzHot.forced);
        CHECK(pzHot.forceNotOffered);
        CHECK(pzHot.option == pzHot.seeded);
        CHECK(pzHot.option != &fr::kPatientZero);

        // none, forced, over a 100 % option.
        const fr::VariantPlan none = fr::PlanVariant(*fr::FindVariantMap(kBangkokHot), seed, kBangkokHot,
                                                     Chances({{&fr::kTheSource, 100}}), base, "none");
        CHECK(none.forced);
        CHECK(none.seeded == &fr::kTheSource);
        CHECK(none.option == nullptr);
        CHECK(none.action == fr::VariantAction::Unchanged);

        // Not on this map: the seeded pick, flagged.
        const fr::VariantPlan fluOnBangkok =
            fr::PlanVariant(*fr::FindVariantMap(kBangkokMild), seed, kBangkokMild, kDefaults, base, "tod_flu");
        CHECK_FALSE(fluOnBangkok.forced);
        CHECK(fluOnBangkok.forceNotOffered);
        CHECK(fluOnBangkok.option == fluOnBangkok.seeded);

        // No force: the seeded pick, not flagged.
        const fr::VariantPlan plain =
            fr::PlanVariant(*fr::FindVariantMap(kBangkokHot), seed, kBangkokHot, kDefaults, base);
        CHECK_FALSE(plain.forced);
        CHECK_FALSE(plain.forceNotOffered);
    }

    // Every option is forceable on the scenes that offer it (both of its map's, the mild one alone for an option
    // with a full version), and nowhere else.
    for (const char* scene : kAllScenes) {
        const fr::VariantMap& map = *fr::FindVariantMap(scene);
        for (const fr::VariantOption* option : fr::AllVariantOptions()) {
            const fr::VariantPlan plan = fr::PlanVariant(map, 7, scene, Chances(), base, option->name);
            const bool onMap = std::find(map.options.begin(), map.options.end(), option) != map.options.end();
            CHECK(plan.forced == onMap);
            CHECK(plan.forceNotOffered == !onMap);
            CHECK(plan.option == (onMap ? option : nullptr));
        }
    }
}

TEST_CASE("variant_multi_brick_plan") {
    const fr::VariantMap& map = *fr::FindVariantMap(kHokkaidoMild);
    const std::string mission = "assembly:/_pro/scenes/missions/hokkaido/mission_mild_shogi.brick";
    const std::vector<std::string> base {mission};

    SUBCASE("all added, in order") {
        const fr::VariantPlan plan = fr::PlanVariant(map, 1, kHokkaidoMild, kDefaults, base, "patient_zero");
        CHECK(plan.action == fr::VariantAction::Add);
        REQUIRE(plan.bricks.size() == 3);
        CHECK(plan.bricks[0].brick == &fr::kTodFluBrick);
        CHECK(plan.bricks[0].action == fr::VariantAction::Add);
        CHECK(plan.bricks[1].brick == &fr::kGfxFluBrick);
        CHECK(plan.bricks[1].action == fr::VariantAction::Add);
        CHECK(plan.bricks[2].brick == &fr::kEaFluBrick);
        CHECK(plan.bricks[2].action == fr::VariantAction::Add);
    }

    SUBCASE("one already present: the other is still added") {
        const std::vector<std::string> bricks {mission, "ASSEMBLY:/_PRO/SCENES/MISSIONS/HOKKAIDO/TOD_FLU.BRICK"};
        const fr::VariantPlan plan = fr::PlanVariant(map, 1, kHokkaidoMild, kDefaults, bricks, "patient_zero");
        CHECK(plan.action == fr::VariantAction::Add);
        REQUIRE(plan.bricks.size() == 3);
        CHECK(plan.bricks[0].action == fr::VariantAction::AlreadyPresent);
        CHECK(plan.bricks[1].action == fr::VariantAction::Add);
        CHECK(plan.bricks[2].action == fr::VariantAction::Add);
    }

    SUBCASE("all present") {
        const std::vector<std::string> bricks {mission, std::string(fr::kGfxFluBrick.path),
                                               std::string(fr::kEaFluBrick.path), std::string(fr::kTodFluBrick.path)};
        const fr::VariantPlan plan = fr::PlanVariant(map, 1, kHokkaidoMild, kDefaults, bricks, "patient_zero");
        CHECK(plan.action == fr::VariantAction::AlreadyPresent);
    }

    SUBCASE("none has no bricks") {
        const fr::VariantPlan plan = fr::PlanVariant(map, 1, kHokkaidoMild, kDefaults, base, "none");
        CHECK(plan.action == fr::VariantAction::Unchanged);
        CHECK(plan.bricks.empty());
    }
}

TEST_CASE("variant_resolve_append") {
    // The append logic itself, on Patient Zero's three bricks (the mod never appends them: Patient Zero always
    // loads its full version; the same logic serves every option without one).
    const fr::VariantMap& map = *fr::FindVariantMap(kHokkaidoMild);
    const std::vector<std::string> base {kMissionMildSicbo};
    const fr::VariantPlan plan = fr::PlanVariant(map, 1, kHokkaidoMild, kDefaults, base, "patient_zero");

    SUBCASE("all resolve: every brick, in order") {
        std::vector<std::string> checked;
        const fr::VariantAppend append = fr::ResolveVariantAppend(plan, [&](const fr::VariantBrick& b, std::string&) {
            checked.emplace_back(b.name);
            return true;
        });
        CHECK(append.failed == nullptr);
        CHECK(append.add ==
              std::vector<const fr::VariantBrick*> {&fr::kTodFluBrick, &fr::kGfxFluBrick, &fr::kEaFluBrick});
        CHECK(checked == std::vector<std::string> {"tod_flu", "gfx_flu", "ea_flu"});
    }

    SUBCASE("one fails: the whole option is skipped") {
        const fr::VariantAppend append =
            fr::ResolveVariantAppend(plan, [](const fr::VariantBrick& b, std::string& why) {
                if (&b == &fr::kGfxFluBrick) {
                    why = "resource not found in any chunk";
                    return false;
                }
                return true;
            });
        CHECK(append.add.empty());
        CHECK(append.failed == &fr::kGfxFluBrick);
        CHECK(append.why == "resource not found in any chunk");
    }

    SUBCASE("the first fails: the second is not checked") {
        int calls = 0;
        const fr::VariantAppend append = fr::ResolveVariantAppend(plan, [&](const fr::VariantBrick&, std::string& why) {
            ++calls;
            why = "no";
            return false;
        });
        CHECK(calls == 1);
        CHECK(append.failed == &fr::kTodFluBrick);
        CHECK(append.add.empty());
    }

    SUBCASE("present bricks are neither checked nor added") {
        std::vector<std::string> bricks = base;
        bricks.emplace_back(fr::kTodFluBrick.path);
        const fr::VariantPlan partial = fr::PlanVariant(map, 1, kHokkaidoMild, kDefaults, bricks, "patient_zero");
        std::vector<std::string> checked;
        const fr::VariantAppend append =
            fr::ResolveVariantAppend(partial, [&](const fr::VariantBrick& b, std::string&) {
                checked.emplace_back(b.name);
                return true;
            });
        CHECK(checked == std::vector<std::string> {"gfx_flu", "ea_flu"});
        CHECK(append.add == std::vector<const fr::VariantBrick*> {&fr::kGfxFluBrick, &fr::kEaFluBrick});
    }

    SUBCASE("not an Add plan: nothing checked") {
        int calls = 0;
        const auto count = [&](const fr::VariantBrick&, std::string&) {
            ++calls;
            return true;
        };
        CHECK(fr::ResolveVariantAppend(fr::PlanVariant(map, 1, kHokkaidoMild, kDefaults, base, "none"), count)
                  .add.empty());
        CHECK(calls == 0);
    }
}

TEST_CASE("variant_force_names") {
    for (const char* name :
         {"patient_zero", "tod_ebola_night", "landslide", "lighting_copperhead", "the_source", "holiday_hoarders"}) {
        CHECK(fr::ParseVariantForce(name) == std::string(name));
    }
    CHECK(fr::ParseVariantForce(" Patient_Zero\t") == std::string("patient_zero"));
    CHECK(fr::ParseVariantForce("LANDSLIDE") == std::string("landslide"));
    CHECK_FALSE(fr::ParseVariantForce("lighting_mamba")); // a brick of landslide
    CHECK_FALSE(fr::ParseVariantForce("lighting_mission_python"));
    CHECK_FALSE(fr::ParseVariantForce("gfx_flu")); // a brick, not an option
    CHECK(fr::ParseVariantForce(" The_Source ") == std::string("the_source"));
    CHECK(fr::ParseVariantForce("HOLIDAY_HOARDERS") == std::string("holiday_hoarders"));
    CHECK_FALSE(fr::ParseVariantForce("location_parisnoel")); // the brick of holiday_hoarders
    CHECK_FALSE(fr::ParseVariantForce("challenges_zika"));
    CHECK_FALSE(fr::ParseVariantForce("tod_sunset")); // already Freelancer Paris's own lighting
    CHECK_FALSE(fr::ParseVariantForce("source"));
    // "none", then every option's name A-Z.
    CHECK(fr::VariantForceNames().starts_with("none, a_bitter_pill, a_silver_tongue, appraiser, collector, "
                                              "embrace_of_the_serpent, envy, garden_show, gluttony, harbinger, "));
    CHECK(fr::VariantForceNames().ends_with(", the_warlord, the_wildcard, the_wizard, tod_ebola_night"));
    for (const char* name : {"snow_festival", "reflection", "herbalist_party", "the_rage", "harbinger", "the_drop",
                             "serial_killer", "procurers", "collector", "liability", "heartbreaker", "iconoclast",
                             "appraiser", "stowaway", "revolutionary"})
        CHECK(fr::ParseVariantForce(name) == std::string(name));
    CHECK(fr::ParseVariantForce(" StowAway ") == std::string("stowaway"));
    CHECK_FALSE(fr::ParseVariantForce("the_stowaway"));     // the option is "stowaway"
    CHECK_FALSE(fr::ParseVariantForce("elusive_vesper"));   // the brick of procurers
    CHECK_FALSE(fr::ParseVariantForce("elusive_highball")); // the brick of revolutionary
    CHECK_FALSE(fr::ParseVariantForce("politician"));       // Hawke's Bay: no Freelancer scene
    CHECK(fr::ParseVariantForce(" The_Rage ") == std::string("the_rage"));
    CHECK(fr::ParseVariantForce("the_splitter") == std::string("the_splitter"));               // a heavy layer
    CHECK(fr::ParseVariantForce("the_undying_returns") == std::string("the_undying_returns")); // its own wrapper
    CHECK_FALSE(fr::ParseVariantForce("greed"));                               // Seven Deadly Sins: coin mechanics only
    CHECK_FALSE(fr::ParseVariantForce("pride"));                               // Seven Deadly Sins: a 15 KB layer
    CHECK(fr::ParseVariantForce("the_bad_boy") == std::string("the_bad_boy")); // the full Icon plus his brick
    // The full Marrakesh night plus his brick.
    CHECK(fr::ParseVariantForce("the_entertainer") == std::string("the_entertainer"));
    CHECK_FALSE(fr::ParseVariantForce("marrakesh_night"));        // removed: the night level only with The Entertainer
    CHECK_FALSE(fr::ParseVariantForce("the_brothers"));           // deleted by a patch
    CHECK_FALSE(fr::ParseVariantForce("ea_flu"));                 // a brick of patient_zero
    CHECK_FALSE(fr::ParseVariantForce("lighting_day"));           // a brick of the removed the_vector
    CHECK_FALSE(fr::ParseVariantForce("colorado_day"));           // removed
    CHECK_FALSE(fr::ParseVariantForce("the_vector"));             // removed: the Sniper Assassin mode
    CHECK_FALSE(fr::ParseVariantForce("elusive_alabamaslammer")); // the brick of serial_killer
    CHECK_FALSE(fr::ParseVariantForce("whittleton_night"));       // not offered
    CHECK_FALSE(fr::ParseVariantForce("whittleton_sunset"));      // not offered

    // The settings choice offers exactly these names, plus the empty seeded pick.
    const fr::SettingInfo& force = *fr::FindSetting("variant_force");
    std::set<std::string> choices;
    for (const std::string_view choice : fr::SettingChoices(force)) {
        if (!choice.empty())
            choices.emplace(choice);
        CHECK(fr::ParseVariantForce(choice) == std::string(choice));
    }
    std::set<std::string> names {"none"};
    for (const fr::VariantOption* option : fr::AllVariantOptions())
        names.emplace(option->name);
    CHECK(choices == names);
}

TEST_CASE("variant_the_source_plan") {
    const fr::VariantMap& mild = *fr::FindVariantMap(kBangkokMild);
    const std::vector<std::string> base {kMissionMildSicbo};

    SUBCASE("all three added, in scene_zika's order") {
        const fr::VariantPlan plan = fr::PlanVariant(mild, 1, kBangkokMild, kDefaults, base, "the_source");
        CHECK(plan.action == fr::VariantAction::Add);
        REQUIRE(plan.bricks.size() == 3);
        CHECK(plan.bricks[0].brick == &fr::kMissionZikaBrick);
        CHECK(plan.bricks[1].brick == &fr::kTodZikaNightBrick);
        CHECK(plan.bricks[2].brick == &fr::kChallengesZikaBrick);
        for (const fr::VariantBrickPlan& step : plan.bricks)
            CHECK(step.action == fr::VariantAction::Add);
    }

    SUBCASE("tod_zika_night already listed: the others still added") {
        std::vector<std::string> bricks = base;
        bricks.emplace_back("ASSEMBLY:/_PRO/SCENES/MISSIONS/BANGKOK/TOD_ZIKA_NIGHT.BRICK");
        const fr::VariantPlan plan = fr::PlanVariant(mild, 1, kBangkokMild, kDefaults, bricks, "the_source");
        CHECK(plan.action == fr::VariantAction::Add);
        REQUIRE(plan.bricks.size() == 3);
        CHECK(plan.bricks[0].action == fr::VariantAction::Add);
        CHECK(plan.bricks[1].action == fr::VariantAction::AlreadyPresent);
        CHECK(plan.bricks[2].action == fr::VariantAction::Add);
    }

    SUBCASE("all present") {
        std::vector<std::string> bricks = base;
        for (const fr::VariantBrick* brick : fr::kTheSource.bricks)
            bricks.emplace_back(brick->path);
        CHECK(fr::PlanVariant(mild, 1, kBangkokMild, kDefaults, bricks, "the_source").action ==
              fr::VariantAction::AlreadyPresent);
    }

    SUBCASE("seeded on Bangkok hot too") {
        const fr::VariantChances source = Chances({{&fr::kTheSource, 100}});
        CHECK(fr::PlanVariant(*fr::FindVariantMap(kBangkokHot), 3, kBangkokHot, source, base).option ==
              &fr::kTheSource);
        CHECK(fr::PlanVariant(mild, 3, kBangkokMild, source, base).option == &fr::kTheSource);
    }
}

TEST_CASE("variant_check_problem") {
    fr::VariantCheckActor ok {true, true, false, true, true, true};
    CHECK_FALSE(fr::VariantCheckProblem(ok, false));
    CHECK_FALSE(fr::VariantCheckProblem(ok, true));

    CHECK(fr::VariantCheckProblem({}, false) == std::string("missing (no actor)"));
    fr::VariantCheckActor unread {true, false};
    CHECK(fr::VariantCheckProblem(unread, true) == std::string("unreadable"));

    // A scene NPC turned off by the variant.
    fr::VariantCheckActor off = ok;
    off.startEnabled = false;
    off.enabled = false;
    CHECK(fr::VariantCheckProblem(off, false)->find("start disabled") == 0);

    // In play only: disabled or dead scene NPCs.
    fr::VariantCheckActor disabled = ok;
    disabled.enabled = false;
    CHECK_FALSE(fr::VariantCheckProblem(disabled, false));
    CHECK(fr::VariantCheckProblem(disabled, true) == std::string("disabled"));
    fr::VariantCheckActor deadNpc = ok;
    deadNpc.alive = false;
    CHECK_FALSE(fr::VariantCheckProblem(deadNpc, false));
    CHECK(fr::VariantCheckProblem(deadNpc, true) == std::string("dead"));

    // The mission brick's own targets are never a problem: the unpicked ones
    // stay disabled and read as dead in play.
    fr::VariantCheckActor dedicated {true, true, true, false, false, true};
    CHECK_FALSE(fr::VariantCheckProblem(dedicated, false));
    CHECK_FALSE(fr::VariantCheckProblem(dedicated, true));
    fr::VariantCheckActor unpicked = dedicated;
    unpicked.alive = false;
    CHECK_FALSE(fr::VariantCheckProblem(unpicked, false));
    CHECK_FALSE(fr::VariantCheckProblem(unpicked, true));
    fr::VariantCheckActor unreadTarget {true, false, true};
    CHECK_FALSE(fr::VariantCheckProblem(unreadTarget, true));
}

TEST_CASE("variant_check_summary") {
    const fr::VariantCheckActor ok {true, true, false, true, true, true};
    const fr::VariantCheckActor dedicated {true, true, true, false, false, true};
    fr::VariantCheckActor unpicked = dedicated;
    unpicked.alive = false;
    fr::VariantCheckActor off = ok;
    off.startEnabled = false;
    const std::vector<fr::VariantCheckActor> clean {ok, dedicated, unpicked};
    CHECK(fr::VariantCheckSummary(clean, true) ==
          "3 pool actors: 2 in the mission brick (not checked), 1 scene NPC: no problems");
    const std::vector<fr::VariantCheckActor> bad {ok, off, {}};
    CHECK(fr::VariantCheckSummary(bad, false) ==
          "3 pool actors: 0 in the mission brick (not checked), 3 scene NPCs: 2 problems");
    const std::vector<fr::VariantCheckActor> one {off};
    CHECK(fr::VariantCheckSummary(one, true) ==
          "1 pool actor: 0 in the mission brick (not checked), 1 scene NPC: 1 problem");
    CHECK(fr::VariantCheckSummary({}, false) ==
          "0 pool actors: 0 in the mission brick (not checked), 0 scene NPCs: no problems");
}

TEST_CASE("variant target exclusions: only with every brick of the option loaded") {
    // No option's table has a malformed id.
    for (const fr::VariantOption* option : fr::AllVariantOptions())
        CHECK(fr::test::MalformedVariantExclusions(*option).empty());

    const fr::VariantOption both {"test_both",
                                  {&fr::test::kScenarioBelliniEaBrick, &fr::kScenarioMamushiBrick},
                                  0,
                                  true,
                                  {"0f504ad2-5557-989e-0000-000000000001", "not-a-guid"}};
    const fr::VariantOption one {
        "test_one", {&fr::test::kScenarioBelliniEaBrick}, 0, true, {"0F504AD2-5557-989E-0000-000000000002"}};
    const fr::VariantOption plain {"test_plain", {&fr::kScenarioMamushiBrick}, 0, true};
    const fr::VariantOption* options[] = {&both, &one, &plain};

    CHECK(fr::test::MalformedVariantExclusions(both) == std::vector<std::string_view> {"not-a-guid"});

    // Vanilla: nothing.
    CHECK(fr::VariantTargetExclusions({}, options).empty());
    const uint64_t other[] = {0x1234, fr::kScenarioMamushiBrick.rid};
    CHECK(fr::VariantTargetExclusions(other, options).empty());

    // One brick of two: only the one-brick option.
    const uint64_t bellini[] = {0x1234, fr::test::kScenarioBelliniEaBrick.rid};
    const auto onlyOne = fr::VariantTargetExclusions(bellini, options);
    REQUIRE(onlyOne.size() == 1);
    CHECK(fr::FormatGuid(*onlyOne.begin()) == "0f504ad2-5557-989e-0000-000000000002");

    const uint64_t all[] = {fr::kScenarioMamushiBrick.rid, fr::test::kScenarioBelliniEaBrick.rid};
    CHECK(fr::VariantTargetExclusions(all, options).size() == 2);

    // The real table with nothing loaded.
    CHECK(fr::VariantTargetExclusions(std::span<const uint64_t> {}).empty());
}

TEST_CASE("variant target exclusions: a variant's own story targets are never Freelancer targets") {
    const auto excludedWith = [](const fr::VariantOption& option) {
        std::vector<uint64_t> rids;

        for (const fr::VariantBrick* brick : option.bricks)
            rids.push_back(brick->rid);

        return fr::TargetPoolExclusions(rids, nullptr);
    };

    // The ones the mover table lists as movers of their own variant (showdown or mild censuses): without the
    // exclusion the target pool swap could pick them.
    constexpr const char* kSgail = "assembly:/_pro/scenes/missions/theark/scene_magpie_mild_whist.entity";
    constexpr const char* kBerlin = "assembly:/_pro/scenes/missions/edgy/mission_fox/scene_edgy_mild_meyer.entity";
    constexpr const char* kChongqing = "assembly:/_pro/scenes/missions/wet/scene_wet_mild_mahjong.entity";

    struct Mover {
        const fr::VariantOption* option;
        const char* scene;
        const char* repo;
    };

    const Mover movers[] = {
        {&fr::kAppraiser, kSgail, "4c7c489d-3992-41ad-b2af-df3b3c431b53"},           // Miranda Jamison
        {&fr::kStowaway, kSgail, "0808d791-d0ba-4c71-880f-a31c88ad69d9"},            // Jimmy Chen
        {&fr::kTheRage, kChongqing, "2981aaba-0a8d-4895-9456-976fc437bee5"},         // Sully Bowden
        {&fr::kTheDrop, kBerlin, "e1258f2b-1693-44a4-99a4-90c3dea952d1"},            // Alexios Laskaridis
        {&fr::kLiability, kBerlin, "9d1c5d01-a793-4c0a-875b-3b48ad584537"},          // Terrence Chesterfield
        {&fr::kReflection, kHokkaidoMild, "851043ee-a2a3-48f2-a5e2-b7e13a28131c"},   // "Slim Shady"
        {&fr::kSnowFestival, kHokkaidoMild, "5087ab4a-586c-4967-967c-487cc2d17c92"}, // Dmitri Fedorov
        {&fr::kTheSurgeons, kHokkaidoMild, "88836cf0-852e-4244-9aa3-90174467b5b1"},  // Akane Akenawa
        {&fr::kTheFugitive, kHokkaidoMild, "2f47716a-2663-461b-bd8c-2dfdccc720c6"},  // Ji-hu (one of three)
        {&fr::kTheFugitive, kHokkaidoMild, "6c9474f0-30eb-4bac-af97-2541a9173bab"},  // Ji-hu
        {&fr::kTheFugitive, kHokkaidoMild, "84049f2c-ba47-4d02-a5a2-c31aa2f2cd4f"},  // Ji-hu
    };

    for (const Mover& mover : movers) {
        CAPTURE(mover.option->name);
        CAPTURE(mover.repo);
        const fr::Guid guid = *fr::ParseGuid(mover.repo);
        const auto rows = fr::SceneMovers(mover.scene, mover.option->name);
        REQUIRE(rows);
        CHECK(rows->contains(guid));
        CHECK(excludedWith(*mover.option).contains(guid));
        // Only with that option's bricks loaded.
        CHECK(!fr::TargetPoolExclusions(std::span<const uint64_t> {}, nullptr).contains(guid));
    }

    // The Wizard: Taylor Graves (scenario_pomada's "Pomada"), the one who flees when alarmed.
    CHECK(excludedWith(fr::kTheWizard).contains(*fr::ParseGuid("fbe2c0d4-91df-4499-8799-9a9a66073247")));
    // The Stowaway's other people stay possible targets.
    CHECK(!excludedWith(fr::kStowaway).contains(*fr::ParseGuid("1ebca75b-d68f-44ca-adf3-6c994059518d"))); // Emily Patel

    // Every option has its contract's targets listed, except the ones with no such list: lighting only, the full
    // versions (core/LandslideFull.h) and the escalations.
    const std::set<std::string_view> without {"tod_ebola_night",
                                              "lighting_copperhead",
                                              "patient_zero",
                                              "landslide",
                                              "the_bad_boy",
                                              "garden_show",
                                              "gluttony",
                                              "envy",
                                              "lust",
                                              "sloth",
                                              "the_entertainer"};

    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        CAPTURE(option->name);
        CHECK(option->notTargets.empty() == without.contains(option->name));
    }
}

TEST_CASE("target pool exclusions: the full Landslide's never-targets, not the simple version's day crowd") {
    std::vector<uint64_t> landslideBricks;

    for (const fr::VariantBrick* brick : fr::kLandslide.bricks)
        landslideBricks.push_back(brick->rid);

    std::set<fr::Guid> sitters;

    for (const fr::StrandedNpc& npc : fr::kLandslide.removed)
        sitters.insert(*fr::ParseGuid(npc.repo));

    std::set<fr::Guid> story;

    for (const std::string_view repo : fr::NeverTargetRepos(fr::LandslideFull()))
        story.insert(*fr::ParseGuid(repo));

    REQUIRE(sitters.size() == 10);
    REQUIRE(story.size() == 1);

    // The simple Landslide: its stage sitters, as before.
    CHECK(fr::TargetPoolExclusions(landslideBricks, false) == sitters);
    CHECK(fr::TargetPoolExclusions(landslideBricks, false) == fr::VariantTargetExclusions(landslideBricks));

    // The full one: mission_mamba loads all of kLandslide's bricks, but only its never-targets apply.
    landslideBricks.push_back(0x006026A87FB981F5ull); // mission_mamba
    CHECK(fr::TargetPoolExclusions(landslideBricks, true) == story);

    // Whatever the brick list (none read, or not the ones expected).
    CHECK(fr::TargetPoolExclusions({}, true) == story);

    // Another option's notTargets still apply with its bricks loaded.
    const fr::VariantOption other {
        "test_other", {&fr::test::kScenarioBelliniEaBrick}, 0, true, {"0f504ad2-5557-989e-0000-000000000003"}};
    const fr::VariantOption* options[] = {&fr::kLandslide, &other};
    const uint64_t bellini[] = {fr::test::kScenarioBelliniEaBrick.rid};
    const auto withOther = fr::TargetPoolExclusions(bellini, true, options);
    CHECK(withOther.size() == story.size() + 1);
    CHECK(withOther.contains(*fr::ParseGuid("0f504ad2-5557-989e-0000-000000000003")));

    // Vanilla: nothing.
    CHECK(fr::TargetPoolExclusions({}, false).empty());
}

TEST_CASE("target pool exclusions: each full variant's own never-targets") {
    std::set<fr::Guid> abiatti;
    std::set<fr::Guid> craig;

    for (const std::string_view repo : fr::NeverTargetRepos(fr::LandslideFull()))
        abiatti.insert(*fr::ParseGuid(repo));

    for (const std::string_view repo : fr::NeverTargetRepos(fr::test::Full("author")))
        craig.insert(*fr::ParseGuid(repo));

    REQUIRE(craig.size() == 1);
    CHECK(fr::TargetPoolExclusions({}, &fr::test::Full("author")) == craig);

    // The full Icon's: Dino Bosco and Sophia Wilde, with The Icon's lighting loaded (mission_copperhead's own).
    std::set<fr::Guid> dinoSophia;

    for (const std::string_view repo : fr::NeverTargetRepos(fr::test::Full("icon")))
        dinoSophia.insert(*fr::ParseGuid(repo));

    REQUIRE(dinoSophia.size() == 2);
    const uint64_t iconLighting[] = {fr::kTodSummerNightClearBrick.rid, fr::kLightingCopperheadBrick.rid};
    CHECK(fr::TargetPoolExclusions({}, &fr::test::Full("icon")) == dinoSophia);
    CHECK(fr::TargetPoolExclusions(iconLighting, &fr::test::Full("icon")) == dinoSophia);
    CHECK(fr::TargetPoolExclusions({}, &fr::LandslideFull()) == abiatti);
    CHECK(fr::TargetPoolExclusions({}, &fr::LandslideFull()) == fr::TargetPoolExclusions({}, true));
    CHECK(fr::TargetPoolExclusions({}, nullptr).empty());

    // Under the full Author, The Author's own lighting brick is loaded (scenario_ebola loads tod_ebola_night); a
    // loaded option's notTargets still apply, and Landslide's never-targets do not.
    const uint64_t ebolaNight[] = {fr::kTodEbolaNightBrick.rid};
    CHECK(fr::TargetPoolExclusions(ebolaNight, &fr::test::Full("author")) == craig);
    const fr::VariantOption other {
        "test_other", {&fr::test::kScenarioBelliniEaBrick}, 0, true, {"0f504ad2-5557-989e-0000-000000000003"}};
    const fr::VariantOption* options[] = {&fr::kTodEbolaNight, &other};
    const uint64_t bellini[] = {fr::test::kScenarioBelliniEaBrick.rid};
    const auto withOther = fr::TargetPoolExclusions(bellini, &fr::test::Full("author"), options);
    CHECK(withOther.size() == 2);
    CHECK(withOther.contains(*craig.begin()));
}

TEST_CASE("target pool exclusions: the main story mission's targets, with no map variant loaded") {
    constexpr const char* kSgail = "assembly:/_pro/scenes/missions/theark/scene_magpie_mild_whist.entity";
    const fr::Guid sophia = *fr::ParseGuid("67f39ab8-c25f-48c3-84be-0ec495a553ec");     // Sophia Washington
    const fr::Guid zoe = *fr::ParseGuid("7eb39f2d-1030-44d2-be82-6df608085ec0");        // Zoe Washington
    const fr::Guid bernadette = *fr::ParseGuid("8e39a621-8bbd-43b4-badc-3d6e73e43fd8"); // Bernadette Craig (pool)

    // Sophia Washington is a good mover of Sgail's own table, so the swap drew her.
    const auto movers = fr::SceneMovers(kSgail);
    REQUIRE(movers);
    CHECK(movers->contains(sophia));
    CHECK(movers->contains(zoe));

    // Now never a target there, with nothing loaded and no full variant.
    const auto excluded = fr::TargetPoolExclusions(kSgail, std::span<const uint64_t> {}, nullptr);
    CHECK(excluded.contains(sophia));
    CHECK(excluded.contains(zoe));
    CHECK(!excluded.contains(bernadette));
    const std::set<fr::Guid> twins {sophia, zoe};
    CHECK(excluded == twins);
    CHECK(excluded == fr::StoryTargetExclusions(kSgail));

    // Any case (the contracts name Marrakesh's folder Marrakesh/, the table marrakesh/).
    CHECK(fr::StoryTargetExclusions("ASSEMBLY:/_PRO/SCENES/MISSIONS/THEARK/SCENE_MAGPIE_MILD_WHIST.ENTITY") ==
          excluded);
    CHECK(fr::StoryTargetExclusions(kMarrakeshMild).contains(*fr::ParseGuid("b38b0b62-8071-4761-b2a5-2f635cd8da1b")));

    // Only at its own location: not at another one, nor with no scene.
    CHECK(!fr::StoryTargetExclusions(kMumbaiMild).contains(sophia));
    CHECK(fr::StoryTargetExclusions("").empty());
    CHECK(fr::SceneStoryTargets("assembly:/_pro/scenes/missions/snug/scene_vanilla.entity").empty());

    // The old overloads (no scene) are as before: the variants' exclusions only.
    CHECK(fr::TargetPoolExclusions({}, nullptr).empty());
}

TEST_CASE("target pool exclusions: the main story's targets as well as a map variant's or a full variant's") {
    constexpr const char* kSgail = "assembly:/_pro/scenes/missions/theark/scene_magpie_mild_whist.entity";
    const fr::Guid sophia = *fr::ParseGuid("67f39ab8-c25f-48c3-84be-0ec495a553ec");  // Sophia Washington
    const fr::Guid miranda = *fr::ParseGuid("4c7c489d-3992-41ad-b2af-df3b3c431b53"); // The Appraiser's Miranda Jamison

    // The Appraiser loaded at Sgail: its elusive target and the twins.
    std::vector<uint64_t> appraiser;

    for (const fr::VariantBrick* brick : fr::kAppraiser.bricks)
        appraiser.push_back(brick->rid);

    const auto withVariant = fr::TargetPoolExclusions(kSgail, appraiser, nullptr);
    CHECK(withVariant.contains(miranda));
    CHECK(withVariant.contains(sophia));

    std::set<fr::Guid> expected = fr::TargetPoolExclusions(appraiser, nullptr);
    expected.merge(fr::StoryTargetExclusions(kSgail));
    CHECK(withVariant == expected);

    // The full Author counts as mild Sapienza (fr::SceneOf): Craig Black, Silvio Caruso and Francesca De Santis.
    const auto author =
        fr::TargetPoolExclusions(kSapienzaMild, std::span<const uint64_t> {}, &fr::test::Full("author"));
    CHECK(author.contains(*fr::ParseGuid("0dfaea51-3c36-4722-9eff-f1e7ef139878"))); // Silvio Caruso
    CHECK(author.contains(*fr::ParseGuid("94ab740b-b30f-4086-9aea-5c9c0de28456"))); // Francesca De Santis

    for (const std::string_view repo : fr::NeverTargetRepos(fr::test::Full("author")))
        CHECK(author.contains(*fr::ParseGuid(repo)));
}

TEST_CASE("story targets: the table is well formed") {
    const auto rows = fr::StoryTargets();
    REQUIRE(rows.size() == 37);

    std::set<fr::Guid> repos;
    std::set<std::string> scenes;

    for (const fr::StoryTarget& row : rows) {
        CAPTURE(row.name);
        CAPTURE(row.repo);
        const auto guid = fr::ParseGuid(row.repo);
        REQUIRE(guid);
        CHECK(!guid->IsEmpty());
        CHECK(repos.insert(*guid).second); // no id twice
        CHECK(!row.name.empty());
        // A Freelancer mild scene, in lower case like the mover table's keys.
        const std::string scene(row.scene);
        CHECK(scene.rfind("assembly:/_pro/scenes/missions/", 0) == 0);
        CHECK(scene.find("_mild_") != std::string::npos);
        CHECK(scene == fr::ToLower(scene));
        scenes.insert(scene);
        // Its own scene's exclusions have it.
        CHECK(fr::StoryTargetExclusions(row.scene).contains(*guid));
    }

    // Every Freelancer location but Berlin (none of its story targets is in its Freelancer level).
    CHECK(scenes.size() == 18);
    CHECK(fr::StoryTargetExclusions("assembly:/_pro/scenes/missions/edgy/mission_fox/scene_edgy_mild_meyer.entity")
              .empty());
}

TEST_CASE("loaded variants: the full Icon's own lighting is not a variant of its scene") {
    // mission_copperhead loads The Icon's lighting, but the full Icon stands in for that option: no variant rows
    // (those would be the normal map's, as for the simple Icon); its own scene's rows apply.
    const uint64_t icon[] = {fr::kTodSummerNightClearBrick.rid, fr::kLightingCopperheadBrick.rid};
    CHECK(fr::LoadedVariants(icon, &fr::test::Full("icon")).empty());
    // The Bad Boy's simple version appends the same bricks: one variant, the first listed.
    CHECK(fr::LoadedVariants(icon, nullptr) == std::vector<std::string_view> {"lighting_copperhead"});
    // The full Bad Boy loads the full Icon's scene: neither option is a variant of it.
    CHECK(fr::LoadedVariants(icon, &fr::test::Full("badboy")).empty());
}

TEST_CASE("mover table variants: a layer full variant is tagged with its key, its base is not") {
    // The full Bad Boy is the full Icon's scene plus Argus's people: its rows are its own ("badboy"), the scene's
    // untagged rows the full Icon's. The same for the full Entertainer on the full Marrakesh night.
    const uint64_t icon[] = {fr::kTodSummerNightClearBrick.rid, fr::kLightingCopperheadBrick.rid};
    CHECK(fr::MoverTableVariants(icon, &fr::test::Full("badboy")) == std::vector<std::string_view> {"badboy"});
    CHECK(fr::MoverTableVariants(icon, &fr::test::Full("icon")).empty());
    const uint64_t night[] = {fr::kTodSummerNightClearMarrakeshBrick.rid};
    CHECK(fr::MoverTableVariants(night, &fr::test::Full("entertainer")) ==
          std::vector<std::string_view> {"entertainer"});
    CHECK(fr::MoverTableVariants(night, &fr::test::Full("marrakesh")).empty());
    // Not a full variant: exactly LoadedVariants.
    CHECK(fr::MoverTableVariants(icon, nullptr) == fr::LoadedVariants(icon, nullptr));

    // Only layers are tagged, and no option is named like a layer's key, so the tag is never a lighting-only option.
    for (const fr::FullVariant* full : fr::FullVariants()) {
        CAPTURE(full->key);
        CHECK((fr::MoverTableVariants({}, full) == std::vector<std::string_view> {full->key}) ==
              !full->layerOf.empty());

        if (!full->layerOf.empty()) {
            CHECK(fr::FindVariantOption(full->key) == nullptr);
            CHECK_FALSE(fr::IsLightingOnlyVariant(full->key));
        }
    }
}

TEST_CASE("exclusions under the full Bad Boy: The Icon's two and Bartholomew Argus") {
    const uint64_t icon[] = {fr::kTodSummerNightClearBrick.rid, fr::kLightingCopperheadBrick.rid};
    const auto never = fr::TargetPoolExclusions(icon, &fr::test::Full("badboy"));
    CHECK(never.size() == 3);
    CHECK(never.contains(*fr::ParseGuid("ab3fe438-acd0-49a4-a0f4-690c86a10324")));
    CHECK(never.contains(*fr::ParseGuid("9c81b545-a234-46c5-9296-54f9f2a0bb6d")));
    CHECK(fr::TargetPoolExclusions(icon, &fr::test::Full("icon")).size() == 2);
}

TEST_CASE("the_bad_boy: The Icon's night lighting, its full version the full Icon plus his brick") {
    CHECK(fr::kTheBadBoy.name == "the_bad_boy");
    CHECK(fr::kTheBadBoy.bricks == fr::kLightingCopperhead.bricks);
    CHECK(fr::kTheBadBoy.defaultChance == 10);
    CHECK_FALSE(fr::kTheBadBoy.check);
    CHECK(fr::VariantPlaceName(fr::kTheBadBoy) == "Sapienza");
    // Last on Sapienza, so the other options roll as before.
    CHECK(fr::FindVariantMap(kSapienzaMild)->options.back() == &fr::kTheBadBoy);
}

TEST_CASE("the_entertainer: the night's brick, its full version the full Marrakesh night plus his brick") {
    CHECK(fr::kTheEntertainer.name == "the_entertainer");
    CHECK(fr::kTheEntertainer.bricks == std::vector<const fr::VariantBrick*> {&fr::kTodSummerNightClearMarrakeshBrick});
    CHECK(fr::kTheEntertainer.defaultChance == 35);
    CHECK_FALSE(fr::kTheEntertainer.check);
    CHECK(fr::kTheEntertainer.notTargets.empty()); // his are the full version's
    CHECK(fr::VariantPlaceName(fr::kTheEntertainer) == "Marrakesh");
    // Last on mild Marrakesh, so the other options roll as before; not on the showdown (no full version there).
    CHECK(fr::FindVariantMap(kMarrakeshMild)->options.back() == &fr::kTheEntertainer);
    const auto& hot = fr::FindVariantMap(kMarrakeshHot)->options;
    CHECK(std::find(hot.begin(), hot.end(), &fr::kTheEntertainer) == hot.end());
    CHECK(fr::FullVariantOf("the_entertainer", "Marrakesh mild") == &fr::test::Full("entertainer"));
    CHECK_FALSE(fr::FullVariantOf("the_entertainer", "Marrakesh hot"));
    CHECK(fr::FindSetting("variant_chance_the_entertainer")->label == "Marrakesh: The Entertainer");
    CHECK(fr::FindSetting("variant_chance_the_entertainer")->help.find("Not in showdowns") != std::string_view::npos);

    // The night level loads the_entertainer's brick: under the full Entertainer (or its base, the full Marrakesh
    // night) it is no variant of it; with no full version the night brick alone names the_entertainer.
    const uint64_t night[] = {fr::kTodSummerNightClearMarrakeshBrick.rid};
    CHECK(fr::LoadedVariants(night, &fr::test::Full("entertainer")).empty());
    CHECK(fr::LoadedVariants(night, &fr::test::Full("marrakesh")).empty());
    CHECK(fr::LoadedVariants(night, nullptr) == std::vector<std::string_view> {"the_entertainer"});

    // Never targets: the night's two and The Entertainer's target (his contract's one Kill); not under the full
    // Marrakesh night alone (its base).
    const auto never = fr::TargetPoolExclusions(night, &fr::test::Full("entertainer"));
    CHECK(never.size() == 3);
    CHECK(never.contains(*fr::ParseGuid("e63259c4-1a82-4299-9633-6ac54991e1e9")));
    CHECK(never.contains(*fr::ParseGuid("23415c1d-6ee8-41a8-9975-523f3eede581"))); // Kong Tuo-Kwang
    CHECK(never.contains(*fr::ParseGuid("4d0953f0-7f19-4130-abae-610a8f5dddca"))); // Matthieu Mendola
    CHECK_FALSE(fr::TargetPoolExclusions(night, &fr::test::Full("marrakesh"))
                    .contains(*fr::ParseGuid("e63259c4-1a82-4299-9633-6ac54991e1e9")));
}

TEST_CASE("the_undying_returns: its own wrapper of The Undying's layer") {
    CHECK(fr::kTheUndyingReturns.name == "the_undying_returns");
    CHECK(fr::kTheUndyingReturns.bricks == std::vector<const fr::VariantBrick*> {&fr::kScenarioSambuca2Brick});
    CHECK(fr::kScenarioSambuca2Brick.name == "scenario_sambuca2");
    CHECK(fr::kScenarioSambuca2Brick.path == "assembly:/_pro/scenes/missions/miami/scenario_sambuca2.brick");
    // MD5 of "[path].pc_entitytype", checked offline against the rpkg metadata (chunk19.rpkg, not deleted).
    CHECK(fr::kScenarioSambuca2Brick.rid == 0x00BE05F31F56D254ull);
    CHECK(fr::kTheUndyingReturns.check);
    CHECK(fr::kTheUndyingReturns.defaultChance == 5);
    CHECK(fr::kTheUndyingReturns.removed.empty());
    CHECK(fr::VariantPlaceName(fr::kTheUndyingReturns) == "Miami");
    // Last on Miami (both scenes), so the other options roll as before.
    CHECK(fr::FindVariantMap(kMiamiMild)->options.back() == &fr::kTheUndyingReturns);
    CHECK(fr::FindVariantMap(kMiamiHot)->options.back() == &fr::kTheUndyingReturns);
    CHECK(fr::AllVariantOptions().back() == &fr::kTheUndyingReturns);
    CHECK(fr::FindSetting("variant_chance_the_undying_returns")->label == "Miami: The Undying Returns");
    CHECK(fr::FindSetting("variant_chance_the_undying_returns")->help.find("where NPCs can walk") !=
          std::string_view::npos);

    // Never targets: its contract's one Kill (MIAMI/ELUSIVE/THEUNDYINGRETURNS). The wrapper loads mission_sambuca,
    // so The Undying's target is out of the pool too, and both options count as loaded (as for The Deceivers'
    // wrapper). Plus the two NPCs both options seal off (SealedActors.inc: Moses Lee, Sierra Knox).
    const uint64_t returns[] = {fr::kScenarioSambuca2Brick.rid, fr::kMissionSambucaBrick.rid};
    const auto never = fr::TargetPoolExclusions(returns, nullptr);
    CHECK(never.size() == 4);
    CHECK(never.contains(*fr::ParseGuid("0771273c-2ea6-4d2b-a636-409925f626c9")));
    CHECK(never.contains(*fr::ParseGuid("c0ab162c-1502-40d5-801f-c5471289d6b7")));
    CHECK(never.contains(*fr::ParseGuid("0eda939e-db2b-4344-9c3f-9d00eb2fb1bc")));
    CHECK(never.contains(*fr::ParseGuid("63283fb2-a653-486d-a505-e471efbc8c54")));
    CHECK(fr::LoadedVariants(returns, false) == std::vector<std::string_view> {"the_undying", "the_undying_returns"});
    // The Undying alone: not The Undying Returns' target.
    const uint64_t undying[] = {fr::kMissionSambucaBrick.rid};
    CHECK_FALSE(
        fr::TargetPoolExclusions(undying, nullptr).contains(*fr::ParseGuid("0eda939e-db2b-4344-9c3f-9d00eb2fb1bc")));
}

TEST_CASE("loaded variants and exclusions under the full Patient Zero") {
    // The outbreak's scene loads patient_zero's three bricks itself (tod_flu and gfx_flu from _scene_flu, ea_flu
    // from mission_flu). The outbreak keeps the hotel's people (sharesCrowd), so, as for the full Author, the option
    // still counts as loaded: until _scene_flu has rows of its own, the target pool uses mild Hokkaido's
    // patient_zero rows (the hotel's movers under the quarantine dressing). Owen Cage and Klaus Liebleid are never
    // targets.
    const uint64_t flu[] = {fr::kTodFluBrick.rid, fr::kGfxFluBrick.rid, fr::kEaFluBrick.rid};
    CHECK(fr::test::Full("patientzero").sharesCrowd);
    CHECK(fr::LoadedVariants(flu, &fr::test::Full("patientzero")) == std::vector<std::string_view> {"patient_zero"});
    CHECK(fr::LoadedVariants(flu, nullptr) == std::vector<std::string_view> {"patient_zero"});

    std::set<fr::Guid> cageLiebleid;

    for (const std::string_view repo : fr::NeverTargetRepos(fr::test::Full("patientzero")))
        cageLiebleid.insert(*fr::ParseGuid(repo));

    REQUIRE(cageLiebleid.size() == 2);
    CHECK(fr::TargetPoolExclusions({}, &fr::test::Full("patientzero")) == cageLiebleid);
    CHECK(fr::TargetPoolExclusions(flu, &fr::test::Full("patientzero")) == cageLiebleid);
    // The simple Patient Zero (no mission_flu: none of the outbreak's people) excludes nobody.
    CHECK(fr::TargetPoolExclusions(flu, nullptr).empty());
    // Its full version is the outbreak's own scene, on mild Hokkaido only.
    CHECK(fr::FullVariantOf(fr::kPatientZero.name, "Hokkaido mild") == &fr::test::Full("patientzero"));
    CHECK_FALSE(fr::FullVariantOf(fr::kPatientZero.name, "Hokkaido hot"));
}

TEST_CASE("loaded variants and exclusions under the full Marrakesh night") {
    // The full Entertainer's base (no option of its own): the night level loads Marrakesh's own
    // tod_summer_night_clear (the_entertainer's brick), which under it is no variant: no variant rows; Kong Tuo-Kwang
    // and Matthieu Mendola are never targets.
    const uint64_t night[] = {fr::kTodSummerNightClearMarrakeshBrick.rid};
    CHECK(fr::LoadedVariants(night, &fr::test::Full("marrakesh")).empty());
    CHECK(fr::LoadedVariants(night, nullptr) == std::vector<std::string_view> {"the_entertainer"});

    std::set<fr::Guid> kongMendola;

    for (const std::string_view repo : fr::NeverTargetRepos(fr::test::Full("marrakesh")))
        kongMendola.insert(*fr::ParseGuid(repo));

    REQUIRE(kongMendola.size() == 2);
    CHECK(fr::TargetPoolExclusions({}, &fr::test::Full("marrakesh")) == kongMendola);
    CHECK(fr::TargetPoolExclusions(night, &fr::test::Full("marrakesh")) == kongMendola);
    // The night brick with no full version (lighting only) excludes nobody.
    CHECK(fr::TargetPoolExclusions(night, nullptr).empty());
}

TEST_CASE("loaded variants: options whose bricks are all loaded, most specific first") {
    using Names = std::vector<std::string_view>;

    // The normal map.
    CHECK(fr::LoadedVariants({}, false).empty());
    const uint64_t unrelated[] = {0x1234};
    CHECK(fr::LoadedVariants(unrelated, false).empty());

    const uint64_t drop[] = {0x1234, fr::kElusiveTomorrowlandBrick.rid};
    CHECK(fr::LoadedVariants(drop, false) == Names {"the_drop"});

    const uint64_t source[] = {fr::kMissionZikaBrick.rid, fr::kTodZikaNightBrick.rid, fr::kChallengesZikaBrick.rid};
    CHECK(fr::LoadedVariants(source, false) == Names {"the_source"});
    // Only some of an option's bricks (The Source's night): no option.
    const uint64_t night[] = {fr::kTodZikaNightBrick.rid};
    CHECK(fr::LoadedVariants(night, false).empty());

    // Landslide: the simple version is the variant; the full one's scene is its own (mission_mamba loads the bricks).
    std::vector<uint64_t> landslide;

    for (const fr::VariantBrick* brick : fr::kLandslide.bricks)
        landslide.push_back(brick->rid);

    CHECK(fr::LoadedVariants(landslide, false) == Names {"landslide"});
    CHECK(fr::LoadedVariants(landslide, true).empty());

    // Two unrelated options: the one with more bricks first.
    const fr::VariantOption one {"test_one", {&fr::test::kScenarioBelliniEaBrick}, 0};
    const fr::VariantOption two {"test_two", {&fr::kScenarioMamushiBrick, &fr::test::kScenarioFilurEaBrick}, 0};
    const fr::VariantOption* options[] = {&one, &two};
    const uint64_t all[] = {fr::test::kScenarioBelliniEaBrick.rid, fr::kScenarioMamushiBrick.rid,
                            fr::test::kScenarioFilurEaBrick.rid};
    CHECK(fr::LoadedVariants(all, false, options) == Names {"test_two", "test_one"});
}

TEST_CASE("lighting-only options: no NPCs, story or set dressing, so the normal map's crowd") {
    // Every option that is not checked only changes the lighting or time of day: ToD and lighting bricks.
    std::vector<std::string_view> lightingOnly;

    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        CAPTURE(option->name);
        CHECK(fr::IsLightingOnly(*option) == !option->check);
        CHECK(fr::IsLightingOnlyVariant(option->name) == fr::IsLightingOnly(*option));

        if (fr::IsLightingOnly(*option))
            lightingOnly.push_back(option->name);
    }

    CHECK(lightingOnly ==
          std::vector<std::string_view> {"tod_ebola_night", "lighting_copperhead", "the_bad_boy", "the_entertainer"});

    // Their bricks are times of day and lighting only.

    CHECK(fr::kTodEbolaNight.bricks == std::vector<const fr::VariantBrick*> {&fr::kTodEbolaNightBrick});
    CHECK(fr::kLightingCopperhead.bricks ==
          std::vector<const fr::VariantBrick*> {&fr::kTodSummerNightClearBrick, &fr::kLightingCopperheadBrick});
    // The Entertainer: Marrakesh's own tod_summer_night_clear (not Sapienza's).
    CHECK(fr::kTheEntertainer.bricks == std::vector<const fr::VariantBrick*> {&fr::kTodSummerNightClearMarrakeshBrick});
    CHECK(fr::kTodSummerNightClearMarrakeshBrick.path ==
          "assembly:/_pro/scenes/missions/marrakesh/tod_summer_night_clear.brick");
    CHECK(fr::kTodSummerNightClearMarrakeshBrick.rid == 0x001769B67BF98D12ull);

    // Heavy ones and unknown names are not.
    CHECK_FALSE(fr::IsLightingOnlyVariant("the_source"));
    CHECK_FALSE(fr::IsLightingOnlyVariant("patient_zero"));
    CHECK_FALSE(fr::IsLightingOnlyVariant("landslide"));
    CHECK_FALSE(fr::IsLightingOnlyVariant(""));
    CHECK_FALSE(fr::IsLightingOnlyVariant("no_such_option"));
}

// --- The H1/H2 elusive targets, the Sarajevo Six, the Garden Show and three heavy layers ---

namespace {

struct ClassicRow {
    const fr::VariantOption* option;
    const char* name;                                                    // the option's (variant_force) name
    const char* label;                                                   // its settings row's label
    std::vector<std::pair<const fr::VariantBrick*, const char*>> bricks; // brick, path under missions/
    std::vector<uint64_t> rids;
};

// Each path is the brick the official contract or scene loads over the base
// Freelancer already loads; each rid is the MD5 of "[path].pc_entitytype"
// (checked offline against the rpkg metadata: every one is installed, none
// deleted by a patch).
const std::vector<ClassicRow>& ClassicOptions() {
    static const std::vector<ClassicRow> s_Table {
        {&fr::kGardenShow,
         "garden_show",
         "Dartmoor: Garden Show",
         {{&fr::kScenarioSmoothsnakeBrick, "ancestral/scenario_smoothsnake.brick"}},
         {0x00E56CA467419D97ull}},
        {&fr::kTheForger,
         "the_forger",
         "Paris: The Forger",
         {{&fr::kSceneTequilasunriseBrick, "paris/_scene_tequilasunrise_01.brick"}},
         {0x005CE2DAE930DFFEull}},
        {&fr::kTheSensation,
         "the_sensation",
         "Paris: The Sensation",
         {{&fr::kSceneShandyBrick, "paris/_scene_shandy.brick"}},
         {0x00B7EDF53810976Dull}},
        {&fr::kTheBlackHat,
         "the_black_hat",
         "Paris: The Black Hat",
         {{&fr::kSceneKamikazeBrick, "paris/_scene_kamikaze.brick"}},
         {0x005160675062E009ull}},
        {&fr::kThePharmacist,
         "the_pharmacist",
         "Paris: The Pharmacist",
         {{&fr::kSceneKirroyalBrick, "paris/_scene_kirroyal.brick"}},
         {0x001E179617DCEA6Aull}},
        {&fr::kThePaparazzo,
         "the_paparazzo",
         "Paris: The Paparazzo",
         {{&fr::kSceneMojitoBrick, "paris/_scene_mojito.brick"}},
         {0x0093C5F22C30E670ull}},
        {&fr::kTheChef,
         "the_chef",
         "Paris: The Chef",
         {{&fr::kSceneSakiniBrick, "paris/_scene_sakini.brick"}},
         {0x00DE7EC945059212ull}},
        {&fr::kTheBroker,
         "the_broker",
         "Paris: The Broker",
         {{&fr::kSceneMargaritaBrick, "paris/_scene_margarita_01.brick"}},
         {0x00E3E62F71655BFDull}},
        {&fr::kTheIdentityThief,
         "the_identity_thief",
         "Paris: The Identity Thief",
         {{&fr::kSceneWhiterussianBrick, "paris/_scene_whiterussian_01.brick"}},
         {0x001E4B06765CD765ull}},
        {&fr::kTheBlackmailer,
         "the_blackmailer",
         "Paris: The Blackmailer",
         {{&fr::kSceneLumumbaBrick, "paris/_scene_lumumba.brick"}},
         {0x0082309476D28A47ull}},
        {&fr::kTheDirector,
         "the_director",
         "Paris: The Director",
         {{&fr::kSceneBlueblazerBrick, "paris/_scene_ts6_blueblazer.brick"}},
         {0x007A613C8FFFE1FAull}},
        {&fr::kTheCongressman,
         "the_congressman",
         "Sapienza: The Congressman",
         {{&fr::kMissionMintjulepBrick, "coastaltown/mission_mintjulep.brick"}},
         {0x00283E10AED14D88ull}},
        {&fr::kTheGuru,
         "the_guru",
         "Sapienza: The Guru",
         {{&fr::kMissionHarveywallbangerBrick, "coastaltown/mission_harveywallbanger.brick"}},
         {0x003C73877802B873ull}},
        {&fr::kThePrince,
         "the_prince",
         "Sapienza: The Prince",
         {{&fr::kMissionSazeracBrick, "coastaltown/mission_sazerac.brick"}},
         {0x00D10639A07CB17Bull}},
        {&fr::kTheTwin,
         "the_twin",
         "Sapienza: The Twin",
         {{&fr::kMissionPiscosourBrick, "coastaltown/mission_piscosour.brick"}},
         {0x00250B6924886D42ull}},
        {&fr::kTheWildcard,
         "the_wildcard",
         "Sapienza: The Wildcard",
         {{&fr::kMissionQuadruplerumandcokeBrick, "coastaltown/mission_quadruplerumandcoke.brick"}},
         {0x005FA8CC8FFFF142ull}},
        {&fr::kTheDeceivers,
         "the_deceivers",
         "Sapienza: The Deceivers",
         {{&fr::kScenarioCorpsereviverBrick, "coastaltown/scenario_corpsereviver.brick"}},
         {0x003683714A5AABDFull}},
        {&fr::kTheEnforcer,
         "the_enforcer",
         "Sapienza: The Enforcer",
         {{&fr::kMissionMimosaBrick, "coastaltown/mission_ts6_mimosa.brick"}},
         {0x0051886D74627B62ull}},
        {&fr::kTheFixer,
         "the_fixer",
         "Marrakesh: The Fixer",
         {{&fr::kMissionCosmopolitanBrick, "marrakesh/mission_marrakesh_cosmopolitan.brick"}},
         {0x007FE9B3470D4D35ull}},
        {&fr::kTheGunrunner,
         "the_gunrunner",
         "Marrakesh: The Gunrunner",
         {{&fr::kMissionScrewdriverBrick, "marrakesh/mission_marrakesh_screwdriver.brick"}},
         {0x0071AFFF5B386C90ull}},
        {&fr::kTheAngelOfDeath,
         "the_angel_of_death",
         "Marrakesh: The Angel of Death",
         {{&fr::kMissionHottoddyBrick, "marrakesh/mission_marrakesh_hottoddy.brick"}},
         {0x00EC481C87FD55BEull}},
        {&fr::kTheExtractor,
         "the_extractor",
         "Marrakesh: The Extractor",
         {{&fr::kMissionGinfizzBrick, "marrakesh/mission_marrakesh_ts6_ginfizz.brick"}},
         {0x005FB5E6FC3627D9ull}},
        {&fr::kTheWarlord,
         "the_warlord",
         "Bangkok: The Warlord",
         {{&fr::kMissionBloodymaryBrick, "bangkok/mission_bangkok_bloodymary.brick"}},
         {0x006D1E9C109EA52Bull}},
        {&fr::kTheFoodCritic,
         "the_food_critic",
         "Bangkok: The Food Critic",
         {{&fr::kMissionMartiniBrick, "bangkok/mission_bangkok_martini.brick"}},
         {0x00FED13C7A6E0F35ull}},
        {&fr::kTheExDictator,
         "the_ex_dictator",
         "Bangkok: The Ex-Dictator",
         {{&fr::kMissionBrassmonkeyBrick, "bangkok/mission_bangkok_brassmonkey.brick"}},
         {0x000527E8ABB28124ull}},
        {&fr::kTheVeteran,
         "the_veteran",
         "Bangkok: The Veteran",
         {{&fr::kMissionGimletBrick, "bangkok/mission_ts6_gimlet.brick"}},
         {0x0080AD2B9A4587B8ull}},
        {&fr::kTheBookkeeper,
         "the_bookkeeper",
         "Colorado: The Bookkeeper",
         {{&fr::kSceneFlirtiniBrick, "colorado_2/_scene_flirtini.brick"}},
         {0x00A9AFAAA3F58E3Full}},
        {&fr::kTheChameleon,
         "the_chameleon",
         "Colorado: The Chameleon",
         {{&fr::kSceneBushwackerBrick, "colorado_2/_scene_bushwacker.brick"}},
         {0x00C0AF9F72488793ull}},
        {&fr::kTheMercenary,
         "the_mercenary",
         "Colorado: The Mercenary",
         {{&fr::kSceneBlackrussianBrick, "colorado_2/_scene_ts6_blackrussian.brick"}},
         {0x00E7DDB770DEAB94ull}},
        {&fr::kTheSurgeons,
         "the_surgeons",
         "Hokkaido: The Surgeons",
         {{&fr::kSceneMoscowmuleBrick, "hokkaido/_scene_moscowmule.brick"}},
         {0x004AB3210BCA326Dull}},
        {&fr::kTheFugitive,
         "the_fugitive",
         "Hokkaido: The Fugitive",
         {{&fr::kSceneDirtyoctopusBrick, "hokkaido/_scene_dirtyoctopus.brick"}},
         {0x00E77D670A41F353ull}},
        {&fr::kTheController,
         "the_controller",
         "Hokkaido: The Controller",
         {{&fr::kMissionSeabreezeBrick, "hokkaido/mission_hokkaido_ts6_seabreeze.brick"}},
         {0x004E9471916310B7ull}},
        {&fr::kTheAscensionist,
         "the_ascensionist",
         "Dubai: The Ascensionist",
         {{&fr::kScenarioGibsonBrick, "golden/mission_gecko/scenario_gibson.brick"}},
         {0x0059E08EB59485C9ull}},
        {&fr::kTheUndying,
         "the_undying",
         "Miami: The Undying",
         {{&fr::kMissionSambucaBrick, "miami/mission_sambuca.brick"}},
         {0x00D7DD7E8B8E3723ull}},
    };
    return s_Table;
}

// "Dartmoor: The Collector" < "Dartmoor: the drop" (A-Z, any case).
bool LessNoCase(std::string_view a, std::string_view b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

} // namespace

TEST_CASE("variant classic elusive targets and heavy layers: bricks, options, chances and settings rows") {
    CHECK(ClassicOptions().size() == 34);
    std::set<uint64_t> rids;

    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        for (const fr::VariantBrick* brick : option->bricks)
            rids.insert(brick->rid);
    }

    size_t newBricks = 0;

    for (const ClassicRow& row : ClassicOptions()) {
        CAPTURE(row.name);
        const fr::VariantOption& option = *row.option;
        CHECK(option.name == row.name);
        CHECK(fr::FindVariantOption(row.name) == &option);
        CHECK(fr::ParseVariantForce(row.name) == std::string(row.name));
        // Story NPCs and set dressing: checked.
        CHECK(option.check);
        CHECK(fr::ChanceOf(kDefaults, option) == option.defaultChance);
        CHECK(option.removed.empty());
        CHECK_FALSE(fr::IsLightingOnly(option));
        REQUIRE(option.bricks.size() == row.bricks.size());

        for (size_t i = 0; i < row.bricks.size(); ++i) {
            const fr::VariantBrick& brick = *row.bricks[i].first;
            CAPTURE(brick.name);
            CHECK(option.bricks[i] == &brick);
            CHECK(brick.path == "assembly:/_pro/scenes/missions/" + std::string(row.bricks[i].second));
            CHECK(brick.rid == row.rids[i]);
            CHECK((brick.rid >> 56) == 0);
            // The brick's name is its file name.
            CHECK(brick.path.ends_with("/" + std::string(brick.name) + ".brick"));
            ++newBricks;
        }

        // Never an escalation's objectives (mission_smoothsnake) or a base swap's mission.
        for (const fr::VariantBrick* brick : option.bricks) {
            CHECK(brick->path.find("mission_smoothsnake") == std::string_view::npos);
            CHECK(brick->path.find("tod_night_garden_show") == std::string_view::npos);
            CHECK(brick->path.find("mission_marrakesh_python") == std::string_view::npos);
            CHECK(brick->path.find("mission_caipirinha") == std::string_view::npos);
        }

        const fr::SettingInfo* info = fr::FindSetting(fr::VariantChanceKey(option));
        REQUIRE(info != nullptr);
        CHECK(info->label == row.label);
        CHECK(info->def == std::to_string(option.defaultChance));
        CHECK(info->help.find("a target may be missing") != std::string_view::npos);
        CHECK(info->help.find("untested") == std::string_view::npos);
        CHECK(fr::SettingChoiceLabel(*fr::FindSetting("variant_force"), row.name) == row.label);
    }

    // 34 bricks, every rid unique across all options.
    CHECK(newBricks == 34);
    // Every brick (by its table entry) has its own rid.
    std::set<const fr::VariantBrick*> bricks;
    for (const fr::VariantOption* option : fr::AllVariantOptions())
        bricks.insert(option->bricks.begin(), option->bricks.end());
    CHECK(rids.size() == bricks.size());

    // The two with their own navmesh say NPCs walk elsewhere, and so does the Garden Show.
    for (const char* key :
         {"variant_chance_garden_show", "variant_chance_the_ascensionist", "variant_chance_the_undying"})
        CHECK(fr::FindSetting(key)->help.find("where NPCs can walk") != std::string_view::npos);
}

// --- The heavy layers left (elusive targets, full versions, special assignments, Seven Deadly Sins) ---

TEST_CASE("variant heavy layers: bricks, options, chances and settings rows") {
    struct Row {
        const fr::VariantOption* option;
        const char* name;  // the option's (variant_force) name
        const char* label; // its settings row's label
        const char* place;
        std::vector<std::pair<const fr::VariantBrick*, const char*>> bricks; // brick, path under missions/
        std::vector<uint64_t> rids;
    };
    // Each option is the bricks its official scene lists beyond what the Freelancer scene loads; each rid is the
    // MD5 of "[path].pc_entitytype", checked offline against the rpkg metadata (installed, none deleted by a patch).
    const std::vector<Row> rows {
        {&fr::kTheBanker,
         "the_banker",
         "Paris: The Banker",
         "Paris",
         {{&fr::kScenarioFrenchmartiniBrick, "paris/scenario_frenchmartini.brick"}},
         {0x00250CEFFB630F13ull}},
        {&fr::kTheInfiltrator,
         "the_infiltrator",
         "Bangkok: The Infiltrator",
         "Bangkok",
         {{&fr::kScenarioBaijuBrick, "bangkok/scenario_baiju.brick"}},
         {0x00DB3D0A73A939C2ull}},
        {&fr::kTheWizard,
         "the_wizard",
         "Isle of Sgail: The Wizard",
         "Isle of Sgail",
         {{&fr::kScenarioPomadaBrick, "theark/scenario_pomada.brick"},
          {&fr::kElusivePomadaSoundBrick, "theark/elusive_pomada_sound.brick"}},
         {0x0089842EA8B5B664ull, 0x00F712178D2DBF09ull}},
        {&fr::kTheSplitter,
         "the_splitter",
         "Chongqing: The Splitter",
         "Chongqing",
         {{&fr::kScenarioLambicBrick, "wet/scenario_lambic.brick"}, {&fr::kSoundLambicBrick, "wet/sound_lambic.brick"}},
         {0x00BB7C63E0B615F9ull, 0x00ACE30E41E98DDCull}},
        {&fr::kIllusionsOfGrandeur,
         "illusions_of_grandeur",
         "Mumbai: Illusions of Grandeur",
         "Mumbai",
         {{&fr::kScenarioKingcobraBrick, "mumbai/scenario_kingcobra.brick"},
          {&fr::kOutfitsKingcobraBrick, "mumbai/outfits_kingcobra.brick"}},
         {0x00EFFE23BA968693ull, 0x00EC26C1DF84A092ull}},
        {&fr::kABitterPill,
         "a_bitter_pill",
         "Whittleton Creek: A Bitter Pill",
         "Whittleton Creek",
         {{&fr::kScenarioGartersnakeBrick, "skunk/scenario_gartersnake.brick"}},
         {0x00794561315ED1EDull}},
        {&fr::kEmbraceOfTheSerpent,
         "embrace_of_the_serpent",
         "Santa Fortuna: Embrace of the Serpent",
         "Santa Fortuna",
         {{&fr::kScenarioAnacondaBrick, "colombia/scenario_anaconda.brick"}},
         {0x008AB510D55AF5A3ull}},
        {&fr::kASilverTongue,
         "a_silver_tongue",
         "Miami: A Silver Tongue",
         "Miami",
         {{&fr::kScenarioCottonmouthBrick, "miami/scenario_cottonmouth.brick"}},
         {0x0076DCA438306FDAull}},
        {&fr::kGluttony,
         "gluttony",
         "Chongqing: Gluttony",
         "Chongqing",
         {{&fr::kScenarioAzaleaBrick, "wet/scenario_azalea.brick"}},
         {0x00CAFC0C93568981ull}},
        {&fr::kEnvy,
         "envy",
         "Mendoza: Envy",
         "Mendoza",
         {{&fr::kScenarioFrangipaniBrick, "elegant/scenario_frangipani.brick"}},
         {0x0000A4945EAFA2B3ull}},
        {&fr::kLust,
         "lust",
         "Berlin: Lust",
         "Berlin",
         {{&fr::kScenarioAmbrosiaBrick, "edgy/mission_fox/scenario_ambrosia.brick"}},
         {0x00D47F2B1CBC7BEBull}},
        {&fr::kSloth,
         "sloth",
         "Dartmoor: Sloth",
         "Dartmoor",
         {{&fr::kAncestralHarebellBrick, "ancestral/ancestral_harebell.brick"},
          {&fr::kOutfitsSkunkBrick, "skunk/outfits_skunk.brick"}},
         {0x002F7D8B6D31F6E0ull, 0x00D98058A63761D7ull}},
    };
    CHECK(rows.size() == 12);

    for (const Row& row : rows) {
        CAPTURE(row.name);
        const fr::VariantOption& option = *row.option;
        CHECK(option.name == row.name);
        CHECK(fr::FindVariantOption(row.name) == &option);
        CHECK(fr::ParseVariantForce(row.name) == std::string(row.name));
        CHECK(fr::VariantPlaceName(option) == row.place);
        CHECK(option.check);
        CHECK(fr::ChanceOf(kDefaults, option) == option.defaultChance);
        CHECK(option.removed.empty());
        CHECK_FALSE(fr::IsLightingOnly(option));
        REQUIRE(option.bricks.size() == row.bricks.size());

        for (size_t i = 0; i < row.bricks.size(); ++i) {
            const fr::VariantBrick& brick = *row.bricks[i].first;
            CAPTURE(brick.name);
            CHECK(option.bricks[i] == &brick);
            CHECK(brick.path == "assembly:/_pro/scenes/missions/" + std::string(row.bricks[i].second));
            CHECK(brick.rid == row.rids[i]);
            CHECK((brick.rid >> 56) == 0);
            CHECK(brick.path.ends_with("/" + std::string(brick.name) + ".brick"));
        }

        // Never a contract's mission brick, an escalation's level brick, a location commentary brick or a base
        // swap's mission.
        for (const fr::VariantBrick* brick : option.bricks) {
            const std::string_view path = brick->path;
            for (const char* never :
                 {"mission_baiju", "mission_filur", "mission_greentini", "mission_bellini", "mission_lambic",
                  "mission_kingcobra", "mission_gartersnake", "mission_anaconda", "mission_cottonmouth",
                  "mission_rabies", "_level", "commentary_", "scenario_sambuca2", "mission_marrakesh_python",
                  "mission_caipirinha", "golden_lunaria", "scenario_makoyana"})
                CHECK(path.find(never) == std::string_view::npos);
        }

        // Both scenes of its map offer it (the mild one only, when it is never in showdowns).
        size_t maps = 0;
        for (const char* scene : kAllScenes) {
            const auto& options = fr::FindVariantMap(scene)->options;
            maps += std::count(options.begin(), options.end(), &option);
        }
        CHECK(maps == (fr::OfferedInShowdowns(option) ? 2u : 1u));

        const fr::SettingInfo* info = fr::FindSetting(fr::VariantChanceKey(option));
        REQUIRE(info != nullptr);
        CHECK(info->label == row.label);
        CHECK(info->def == std::to_string(option.defaultChance));
        // Each has its own navmesh.
        CHECK(info->help.find("where NPCs can walk, so a target may be missing") != std::string_view::npos);
        CHECK(info->help.find("untested") == std::string_view::npos);
        CHECK(fr::SettingChoiceLabel(*fr::FindSetting("variant_force"), row.name) == row.label);
    }

    // The Herbalist lists its dressing bricks (as its scene does): one option named.
    const std::vector<uint64_t> herbalist {0x009CE6A414D90A40ull, 0x001252DEA472C74Full, 0x0070BA7AF778A58Full};
    CHECK(fr::LoadedVariants(herbalist, false) == std::vector<std::string_view> {"herbalist_party"});
    // The Harbinger loads its dressing brick itself (a brick of its own brick): one option named.
    const std::vector<uint64_t> harbinger {0x00DC692646C00776ull, 0x007F3515FFDFA46Eull};
    CHECK(fr::LoadedVariants(harbinger, false) == std::vector<std::string_view> {"harbinger"});
}

TEST_CASE("variant options: unique names, one settings row each, chances per map at most 100") {
    std::set<std::string_view> names;
    std::set<std::string_view> labels;

    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        CAPTURE(option->name);
        CHECK(names.insert(option->name).second);
        CHECK(option->name != "none");
        CHECK(option->name.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") == std::string_view::npos);
        const fr::SettingInfo* info = fr::FindSetting(fr::VariantChanceKey(*option));
        REQUIRE(info != nullptr);
        CHECK(labels.insert(info->label).second);

        int32_t sum = 0;
        for (const fr::VariantOption* sibling : fr::VariantSiblings(*option))
            sum += sibling->defaultChance;
        CHECK(sum <= 100);
    }

    // Every chance row is an option's.
    size_t rows = 0;
    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        if (info.key.starts_with("variant_chance_")) {
            ++rows;
            CHECK(fr::FindVariantOption(info.key.substr(std::string_view("variant_chance_").size())));
        }
    }
    CHECK(rows == fr::AllVariantOptions().size());
    CHECK(fr::AllVariantOptions().size() == 70);

    // Every map's mild and hot scenes offer the same options, less the ones with a full version on the hot one
    // and Dartmoor's Garden Show and Sloth (the show closes the mansion a showdown's suspects stand in; Sloth
    // removes the showdown's start).
    for (size_t i = 0; i + 1 < std::size(kAllScenes); i += 2) {
        CAPTURE(kAllScenes[i]);
        const fr::VariantMap& mild = *fr::FindVariantMap(kAllScenes[i]);
        const fr::VariantMap& hot = *fr::FindVariantMap(kAllScenes[i + 1]);
        std::vector<const fr::VariantOption*> withoutFull;

        // A full version offered on the hot scene too (Gluttony) stays.
        for (const fr::VariantOption* option : mild.options) {
            const bool mildOnlyFull =
                fr::FullVariantOf(option->name, mild.name) && !fr::FullVariantOf(option->name, hot.name);
            if (!mildOnlyFull && option != &fr::kGardenShow && option != &fr::kSloth)
                withoutFull.push_back(option);
        }

        CHECK(hot.options == withoutFull);
    }
}

TEST_CASE("variant lists are shown A-Z") {
    // The settings tab's Map variants group: the switch first, Force a variant and Full map variants under it,
    // then the chances A-Z by label (so each map's rows stay together, under the map's heading).
    std::vector<const fr::SettingInfo*> group;
    for (const fr::SettingInfo& info : fr::SettingsTable()) {
        if (info.group == fr::SettingGroup::Variants)
            group.push_back(&info);
    }
    REQUIRE(group.size() == fr::AllVariantOptions().size() + 3);
    CHECK(group[0]->key == "variants");
    CHECK(group[1]->key == "variant_force");
    CHECK(group[2]->key == "full_map_variants");
    for (size_t i = 4; i < group.size(); ++i) {
        CAPTURE(group[i]->label);
        CHECK(LessNoCase(group[i - 1]->label, group[i]->label));
    }

    // Each place's rows are together (one heading per map, with the normal map's share).
    std::set<std::string_view> placesDone;
    std::string_view place;
    for (size_t i = 3; i < group.size(); ++i) {
        const std::string_view label = group[i]->label;
        const std::string_view here = label.substr(0, label.find(':'));
        if (here != place) {
            CAPTURE(here);
            CHECK(placesDone.insert(here).second);
            place = here;
        }
    }
    CHECK(placesDone.size() == 17);

    // Force a variant: the two plain choices first, then the variants A-Z by their shown name.
    const fr::SettingInfo& force = *fr::FindSetting("variant_force");
    const std::vector<std::string_view> choices = fr::SettingChoices(force);
    REQUIRE(choices.size() == fr::AllVariantOptions().size() + 2);
    CHECK(choices[0].empty());
    CHECK(choices[1] == "none");
    for (size_t i = 3; i < choices.size(); ++i) {
        CAPTURE(choices[i]);
        CHECK(LessNoCase(fr::SettingChoiceLabel(force, choices[i - 1]), fr::SettingChoiceLabel(force, choices[i])));
    }

    // The switch's help names the maps A-Z.
    CHECK(fr::FindSetting("variants")
              ->help.starts_with("Bangkok, Berlin, Chongqing, Colorado, Dartmoor, Dubai, Haven, Hokkaido, Isle of "
                                 "Sgail, Marrakesh, Mendoza, "
                                 "Miami, Mumbai, Paris, Santa Fortuna, Sapienza and Whittleton Creek missions"));

    // The log's list of variant_force names: none, then A-Z.
    const std::string text = fr::VariantForceNames();
    std::vector<std::string> listed;
    for (size_t at = 0; at <= text.size();) {
        const size_t comma = std::min(text.find(", ", at), text.size());
        listed.push_back(text.substr(at, comma - at));
        at = comma + 2;
    }
    REQUIRE(listed.size() == fr::AllVariantOptions().size() + 1);
    CHECK(listed.front() == "none");
    CHECK(std::is_sorted(listed.begin() + 1, listed.end()));
}

TEST_CASE("variants left out of showdowns say so in their help") {
    CHECK_FALSE(fr::OfferedInShowdowns(fr::kGardenShow));
    CHECK_FALSE(fr::OfferedInShowdowns(fr::kSloth));
    CHECK_FALSE(fr::OfferedInShowdowns(fr::kLandslide));
    CHECK_FALSE(fr::OfferedInShowdowns(fr::kTheEntertainer));
    CHECK(fr::OfferedInShowdowns(fr::kHarbinger));
    CHECK(fr::OfferedInShowdowns(fr::kTheSource));

    // The settings menu marks the rows; the help must agree with the showdown lists.
    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        CAPTURE(option->name);
        const fr::SettingInfo* info = fr::FindSetting("variant_chance_" + std::string(option->name));
        REQUIRE(info);
        CHECK((info->help.find("Not in showdowns") != std::string_view::npos) == !fr::OfferedInShowdowns(*option));
    }
}

// --- NPCs a map variant shuts in (SealedActors.inc, tools/landslide/sealed_actors.py) ---

namespace {

std::vector<uint64_t> RidsOf(const fr::VariantOption& option) {
    std::vector<uint64_t> rids;

    for (const fr::VariantBrick* brick : option.bricks)
        rids.push_back(brick->rid);

    return rids;
}

} // namespace

TEST_CASE("sealed actors: every row names a variant and a well-formed id, once") {
    REQUIRE_FALSE(fr::SealedActors().empty());
    std::set<std::string_view> options;

    for (const fr::VariantOption* option : fr::AllVariantOptions())
        options.insert(option->name);

    std::set<std::pair<std::string_view, std::string_view>> seen;

    for (const fr::SealedActor& row : fr::SealedActors()) {
        CAPTURE(row.option);
        CAPTURE(row.repo);
        CHECK(options.contains(row.option));
        const auto guid = fr::ParseGuid(row.repo);
        REQUIRE(guid);
        CHECK_FALSE(guid->IsEmpty());
        CHECK_FALSE(row.name.empty());
        CHECK(seen.insert({row.option, row.repo}).second);
    }
}

TEST_CASE("sealed actors: the Garden Show's closed mansion staff are never targets") {
    // Freelancer's own Dartmoor pool: mission_mild_bridge's dedicated targets. Ross Holt, a valet upstairs in the
    // mansion, was picked under the Garden Show and 47 could not reach him; 19 of the pool's 23 are inside.
    const char* inside[] = {
        "fc19387a-7003-4e5e-88d1-aeccccc71ebc", // Ross Holt
        "6a6fae90-517b-4ec8-a1e1-c5d2cd707b11", // Amy Hepburn
        "0c06226f-e448-4c73-acbd-64707b20d58a", // Bernard Keen (one act inside)
        "157d4a9e-3f79-4df1-80b2-fcb9332a3ab0", // Beth Treloar
        "c1b83af0-c42c-40ab-b70f-c5ab78951afa", // Cale Younger
        "a67429b7-85d7-42ee-9406-a53c59b5af1a", // Dalton Sharrow
        "7b399765-91f7-4f16-9c93-d4854f968b8d", // Dennis Courtney
        "50416e7a-28a1-4808-8784-609e114eff8d", // Edmond Ashworth
        "90d8ebe4-f637-4df4-89dc-e7273995f708", // Enola Morriss
        "da772dde-353a-4a1d-8f97-07cb68e9ab49", // Forrest Hogarth
        "c7c0c9f4-bc9b-4b2f-b44c-064ae98bc231", // Hugh Paget
        "77971db1-ed49-43b0-b494-7de3ddd529ba", // Joseph Leonardson
        "1c048e50-da69-42c3-8e3d-514633ca0391", // Joshua Hall
        "5a595b3b-31ae-49ac-906c-1d3e84d43b0a", // Kenway Matthews
        "91691db9-ee40-45c0-a27e-09490841b218", // Maddox Mathers (one act inside)
        "4101da0d-2d6f-44e8-a996-564e47c2cf7b", // Maggie Loman
        "38d4c8d9-14de-4453-972b-a2618e674811", // Olin Aylmer (one act inside)
        "ab7f785e-0384-47a7-99c8-aec699adc87a", // Warren Christianson
        "d3ae1a94-b184-45d5-9b7b-789fdea43934", // Zelene Eason
    };
    // Outside: the pool's other four, and the two scene guards picked with Ross Holt.
    const char* outside[] = {
        "c730e56d-25b1-42fa-86a8-2d2957a40666", // Mercy Stanton
        "cd8bcc26-a1ed-4834-bb16-22f7be8e45f4", // Nicholas Robert
        "0c8c82b5-340f-47ec-910f-3e3a8881b74d", // Claude Whitaker
        "d7aded96-ebad-48f5-b56d-27d7d5a2638f", // Dee Underhill
        "972e2f49-6faf-45e7-ba2b-f56af52cd19c", // Olly Marshall
        "f9ddce50-b7e7-4411-a3eb-c2f908dc0abb", // Cillian Russell
    };

    const auto garden = fr::VariantTargetExclusions(RidsOf(fr::kGardenShow));
    const auto collector = fr::VariantTargetExclusions(RidsOf(fr::kCollector));

    for (const char* repo : inside) {
        CAPTURE(repo);
        const fr::Guid guid = *fr::ParseGuid(repo);
        CHECK(garden.contains(guid));
        CHECK(fr::TargetPoolExclusions(RidsOf(fr::kGardenShow), nullptr).contains(guid));
        // Only with the Garden Show loaded: the normal map and another variant keep them.
        CHECK_FALSE(fr::VariantTargetExclusions(std::span<const uint64_t> {}).contains(guid));
        CHECK_FALSE(collector.contains(guid));
    }

    for (const char* repo : outside) {
        CAPTURE(repo);
        CHECK_FALSE(garden.contains(*fr::ParseGuid(repo)));
    }

    // Named for the log.
    const auto rows = fr::LoadedSealedActors(RidsOf(fr::kGardenShow));
    CHECK(std::any_of(rows.begin(), rows.end(), [](const fr::SealedActor& row) {
        return row.option == "garden_show" && row.name == "Ross Holt";
    }));
    CHECK(
        std::all_of(rows.begin(), rows.end(), [](const fr::SealedActor& row) { return row.option == "garden_show"; }));
}

TEST_CASE("sealed actors: Illusions of Grandeur's closed rooms of Mumbai's pool") {
    const auto excluded = fr::VariantTargetExclusions(RidsOf(fr::kIllusionsOfGrandeur));
    CHECK(excluded.contains(*fr::ParseGuid("1800c225-e271-4f82-8c91-797f29ecba22"))); // Arya Kapudia
    CHECK(excluded.contains(*fr::ParseGuid("47aebfd1-1e42-462f-85d6-8674c87db2b4"))); // Karam Poddar
    CHECK_FALSE(fr::VariantTargetExclusions(std::span<const uint64_t> {})
                    .contains(*fr::ParseGuid("1800c225-e271-4f82-8c91-797f29ecba22")));
}

TEST_CASE("sealed actors: only the rows of options whose bricks are all loaded") {
    const fr::VariantOption both {
        "test_both", {&fr::test::kScenarioBelliniEaBrick, &fr::kScenarioMamushiBrick}, 0, true};
    const fr::VariantOption one {"test_one", {&fr::test::kScenarioBelliniEaBrick}, 0, true};
    const fr::VariantOption* options[] = {&both, &one};
    const fr::SealedActor rows[] = {
        {"test_both", "0f504ad2-5557-989e-0000-000000000011", "Both"},
        {"test_one", "0f504ad2-5557-989e-0000-000000000012", "One"},
        {"test_other", "0f504ad2-5557-989e-0000-000000000013", "Other"}, // no such option: never loaded
    };

    CHECK(fr::LoadedSealedActors({}, options, rows).empty());

    const uint64_t bellini[] = {fr::test::kScenarioBelliniEaBrick.rid};
    const auto onlyOne = fr::LoadedSealedActors(bellini, options, rows);
    REQUIRE(onlyOne.size() == 1);
    CHECK(onlyOne[0].name == "One");

    const uint64_t all[] = {fr::kScenarioMamushiBrick.rid, fr::test::kScenarioBelliniEaBrick.rid};
    const auto two = fr::LoadedSealedActors(all, options, rows);
    REQUIRE(two.size() == 2);
    CHECK(two[0].name == "Both");
    CHECK(two[1].name == "One");

    // The generated table under options with no rows of their own: nothing, whatever is loaded.
    CHECK(fr::LoadedSealedActors(all, options).empty());
    CHECK(fr::VariantTargetExclusions(all, options).empty());
}

// --- Full variants out of rotation (Full map variants off, not built from the game's files, or not installed) ---

namespace {

const fr::FullVariantAvailable kNoPackage = [](const fr::FullVariant&) { return false; };
const fr::FullVariantAvailable kWithPackage = [](const fr::FullVariant&) { return true; };

// The Settings tab's view of p_Option: a full version of it can't be picked.
bool OptionUnavailable(const fr::VariantOption& p_Option, const fr::FullVariantAvailable& p_Available) {
    return fr::FullOptionAvailability(p_Option.name, [&](const fr::FullVariant& p_Full) {
               return p_Available(p_Full) ? fr::FullAvailability::Available : fr::FullAvailability::NotInstalled;
           }) != fr::FullAvailability::Available;
}

} // namespace

TEST_CASE("variant_package: an option is unavailable exactly when it has a full version that is") {
    std::set<std::string_view> full;

    for (const fr::FullVariant* variant : fr::FullVariants()) {
        if (!variant->option.empty())
            full.insert(variant->option);
    }

    CHECK(full.size() == 17); // 18 full variants; the full Marrakesh night is only a base

    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        CAPTURE(option->name);
        CHECK(OptionUnavailable(*option, kNoPackage) == full.contains(option->name));
        CHECK_FALSE(OptionUnavailable(*option, kWithPackage));
    }

    // Every map that offers such an option plays its full version there, so the Settings tab (per option) and the
    // roll (per map) agree.
    for (const char* scene : kAllScenes) {
        const fr::VariantMap& map = *fr::FindVariantMap(scene);

        for (const fr::VariantOption* option : map.options) {
            CAPTURE(map.name);
            CAPTURE(option->name);
            CHECK(fr::VariantUnavailable(*option, map, kNoPackage) == full.contains(option->name));
        }
    }
}

TEST_CASE("variant_package: with no full variant available, their options' chances are 0 and the rest unchanged") {
    const fr::VariantChances defaults = fr::DefaultVariantChances();

    for (const char* scene : kAllScenes) {
        const fr::VariantMap& map = *fr::FindVariantMap(scene);
        const fr::VariantChances available = fr::AvailableVariantChances(map, defaults, kNoPackage);
        CHECK(fr::AvailableVariantChances(map, defaults, kWithPackage) == defaults);

        for (const fr::VariantOption* option : fr::AllVariantOptions()) {
            CAPTURE(map.name);
            CAPTURE(option->name);
            const bool offered = std::find(map.options.begin(), map.options.end(), option) != map.options.end();
            const bool gone = offered && fr::VariantUnavailable(*option, map, kNoPackage);
            CHECK(fr::ChanceOf(available, *option) == (gone ? 0 : fr::ChanceOf(defaults, *option)));
        }
    }
}

TEST_CASE("variant_package: with no full variant available, no mission ever picks one of their options") {
    for (const char* scene : kAllScenes) {
        const fr::VariantMap& map = *fr::FindVariantMap(scene);
        // Every option at 100: the roll always lands on an option, scaled over the ones left.
        fr::VariantChances all {};
        all.fill(100);

        for (const fr::VariantChances& chances : {fr::DefaultVariantChances(), all}) {
            const fr::VariantChances available = fr::AvailableVariantChances(map, chances, kNoPackage);

            for (int32_t seed = -1000; seed < 1000; ++seed) {
                const fr::VariantPlan plan = fr::PlanVariant(map, seed, scene, available, {});
                CAPTURE(map.name);
                CAPTURE(seed);

                if (plan.option)
                    CHECK_FALSE(fr::VariantUnavailable(*plan.option, map, kNoPackage));
            }
        }
    }
}

TEST_CASE("variant_package: the share of an unavailable option goes where it would if that option were off") {
    const fr::VariantMap& sapienza = *fr::FindVariantMap(kSapienzaMild);
    fr::VariantChances chances {};

    // The four full Sapienza options at 25 each, The Congressman at 50, The Guru at 50: 200% in all.
    for (const fr::VariantOption* option :
         {&fr::kLandslide, &fr::kTodEbolaNight, &fr::kLightingCopperhead, &fr::kTheBadBoy})
        fr::SetChance(chances, *option, 25);

    fr::SetChance(chances, fr::kTheCongressman, 50);
    fr::SetChance(chances, fr::kTheGuru, 50);

    fr::VariantChances off = chances;

    for (const fr::VariantOption* option :
         {&fr::kLandslide, &fr::kTodEbolaNight, &fr::kLightingCopperhead, &fr::kTheBadBoy})
        fr::SetChance(off, *option, 0);

    const fr::VariantChances available = fr::AvailableVariantChances(sapienza, chances, kNoPackage);
    CHECK(available == off);

    const fr::VariantWalk walk = fr::MakeVariantWalk(sapienza, available);
    CHECK(walk.sum == 100);
    CHECK_FALSE(walk.scaled);
    CHECK(walk.upTo.back() == fr::kVariantRollRange); // all of it to The Congressman and The Guru

    // Over 100 without them too: scaled over the ones left.
    fr::SetChance(chances, fr::kTheGuru, 100);
    const fr::VariantWalk scaled =
        fr::MakeVariantWalk(sapienza, fr::AvailableVariantChances(sapienza, chances, kNoPackage));
    CHECK(scaled.sum == 150);
    CHECK(scaled.scaled);
    CHECK(scaled.upTo.back() == fr::kVariantRollRange);
}

TEST_CASE("variant_package: an unbuildable variant and its layer are never rolled; their share goes to the rest") {
    // The full Icon's game files changed: it and the full Bad Boy (a layer on it) can't be built.
    const fr::FullVariantAvailable s_IconGone = [](const fr::FullVariant& p_Full) {
        return p_Full.key != "icon" && p_Full.layerOf != "icon";
    };
    const fr::VariantMap& sapienza = *fr::FindVariantMap(kSapienzaMild);
    fr::VariantChances chances {};
    for (const fr::VariantOption* option :
         {&fr::kLandslide, &fr::kTodEbolaNight, &fr::kLightingCopperhead, &fr::kTheBadBoy})
        fr::SetChance(chances, *option, 50);

    const fr::VariantChances available = fr::AvailableVariantChances(sapienza, chances, s_IconGone);
    CHECK(fr::ChanceOf(available, fr::kLightingCopperhead) == 0);
    CHECK(fr::ChanceOf(available, fr::kTheBadBoy) == 0);
    CHECK(fr::ChanceOf(available, fr::kLandslide) == 50);
    CHECK(fr::ChanceOf(available, fr::kTodEbolaNight) == 50);
    CHECK(OptionUnavailable(fr::kLightingCopperhead, s_IconGone));
    CHECK(OptionUnavailable(fr::kTheBadBoy, s_IconGone));
    CHECK_FALSE(OptionUnavailable(fr::kLandslide, s_IconGone));

    std::map<std::string_view, int> picks;
    for (int32_t seed = -2000; seed < 2000; ++seed) {
        const fr::VariantPlan plan = fr::PlanVariant(sapienza, seed, kSapienzaMild, available, {});
        REQUIRE(plan.option != nullptr); // 100% between the two left
        ++picks[plan.option->name];
    }
    CHECK(picks.size() == 2);
    CHECK(picks[fr::kLandslide.name] > 1500);
    CHECK(picks[fr::kTodEbolaNight.name] > 1500);
}

TEST_CASE("FindVariantBrick: any option's brick by its resource id") {
    for (const fr::VariantOption* option : fr::AllVariantOptions()) {
        for (const fr::VariantBrick* brick : option->bricks)
            CHECK(fr::FindVariantBrick(brick->rid) == brick);
    }

    CHECK(fr::FindVariantBrick(fr::kDressingBeachMambaBrick.rid) == &fr::kDressingBeachMambaBrick);
    CHECK(fr::FindVariantBrick(0) == nullptr);
    CHECK(fr::FindVariantBrick(0x1234) == nullptr);
}
