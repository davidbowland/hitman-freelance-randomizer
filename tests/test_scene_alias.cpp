#include "doctest.h"

#include "LandslideFull.h"
#include "SceneAlias.h"

namespace {

constexpr const char* kBingo = "assembly:/_PRO/Scenes/Missions/CoastalTown/scene_costaltown_mild_bingo.entity";
constexpr const char* kOwnBrick = "assembly:/_pro/scenes/missions/coastaltown/fr_mission_mild_landslide.brick";

fr::SceneAlias Landslide(std::string p_MissionBrick = kOwnBrick) {
    return {std::string(fr::kLandslideScene), kBingo, std::move(p_MissionBrick), "the full Landslide", "", false};
}

} // namespace

TEST_CASE("scene_alias_resolve") {
    const fr::SceneAlias s_Alias = Landslide();

    // The swapped scene counts as the contract's, in the contract's own spelling.
    CHECK(fr::ResolveScene(&s_Alias, fr::kLandslideScene) == kBingo);
    CHECK(fr::ResolveScene(&s_Alias, "ASSEMBLY:/_pro/scenes/missions/CoastalTown/_SCENE_MAMBA.entity") == kBingo);
    CHECK(fr::IsAliasedScene(&s_Alias, fr::kLandslideScene));
    CHECK(fr::IsAliasedScene(&s_Alias, "assembly:/_PRO/scenes/missions/coastaltown/_scene_mamba.ENTITY"));

    // Every other scene, the logical one included, is itself.
    CHECK(fr::ResolveScene(&s_Alias, kBingo) == kBingo);
    CHECK_FALSE(fr::IsAliasedScene(&s_Alias, kBingo));
    CHECK(fr::ResolveScene(&s_Alias, "assembly:/_pro/scenes/missions/snug/scene_vanilla.entity") ==
          "assembly:/_pro/scenes/missions/snug/scene_vanilla.entity");
    CHECK(fr::ResolveScene(&s_Alias, "") == "");
    CHECK_FALSE(fr::IsAliasedScene(&s_Alias, ""));

    // No alias: always the scene itself.
    CHECK(fr::ResolveScene(nullptr, fr::kLandslideScene) == fr::kLandslideScene);
    CHECK_FALSE(fr::IsAliasedScene(nullptr, fr::kLandslideScene));

    // An alias without an actual scene matches nothing (not even an empty scene).
    const fr::SceneAlias s_Empty {};
    CHECK(fr::ResolveScene(&s_Empty, "") == "");
    CHECK_FALSE(fr::IsAliasedScene(&s_Empty, ""));
}

TEST_CASE("scene_alias_mission_brick") {
    const fr::SceneAlias s_Alias = Landslide();

    // The package's mission brick stands in for mission_mild_bingo, by either name of the scene.
    CHECK(fr::AliasMissionBrickPath(&s_Alias, fr::kLandslideScene) == std::optional<std::string>(kOwnBrick));
    CHECK(fr::AliasMissionBrickPath(&s_Alias, kBingo) == std::optional<std::string>(kOwnBrick));
    CHECK(fr::AliasMildMissionBrickPath(&s_Alias, fr::kLandslideScene) == std::optional<std::string>(kOwnBrick));
    CHECK(fr::AliasMildMissionBrickPath(&s_Alias, kBingo) == std::optional<std::string>(kOwnBrick));

    // Other scenes keep their own.
    CHECK(fr::AliasMissionBrickPath(&s_Alias, "assembly:/_pro/scenes/missions/paris/scene_peacock_hot_craps.entity") ==
          std::optional<std::string>("assembly:/_pro/scenes/missions/paris/mission_hot_craps.brick"));
    CHECK_FALSE(
        fr::AliasMildMissionBrickPath(&s_Alias, "assembly:/_pro/scenes/missions/paris/scene_peacock_hot_craps.entity"));
    CHECK_FALSE(fr::AliasMissionBrickPath(&s_Alias, "assembly:/_pro/scenes/missions/snug/scene_vanilla.entity"));

    // No alias: the scene's own (mamba alone is not a Freelancer mission).
    CHECK(fr::AliasMissionBrickPath(nullptr, kBingo) ==
          std::optional<std::string>("assembly:/_pro/scenes/missions/coastaltown/mission_mild_bingo.brick"));
    CHECK_FALSE(fr::AliasMissionBrickPath(nullptr, fr::kLandslideScene));
    CHECK_FALSE(fr::AliasMildMissionBrickPath(nullptr, fr::kLandslideScene));

    // An alias without its own brick (the test package): the logical scene's brick.
    const fr::SceneAlias s_NoBrick = Landslide("");
    CHECK(fr::AliasMissionBrickPath(&s_NoBrick, fr::kLandslideScene) ==
          std::optional<std::string>("assembly:/_pro/scenes/missions/coastaltown/mission_mild_bingo.brick"));
    CHECK(fr::AliasMildMissionBrickPath(&s_NoBrick, fr::kLandslideScene) ==
          std::optional<std::string>("assembly:/_pro/scenes/missions/coastaltown/mission_mild_bingo.brick"));
}

TEST_CASE("scene_alias_label") {
    const fr::SceneAlias s_Alias = Landslide();

    // For log lines: the scene as loaded, and what the mod counts it as.
    CHECK(fr::SceneLabel(&s_Alias, fr::kLandslideScene) ==
          std::string(fr::kLandslideScene) + " (the full Landslide, counted as " + kBingo + ")");
    CHECK(fr::SceneLabel(&s_Alias, kBingo) == kBingo);
    CHECK(fr::SceneLabel(nullptr, fr::kLandslideScene) == fr::kLandslideScene);

    // Without a name.
    fr::SceneAlias s_Unnamed = s_Alias;
    s_Unnamed.name.clear();
    CHECK(fr::SceneLabel(&s_Unnamed, fr::kLandslideScene) ==
          std::string(fr::kLandslideScene) + " (counted as " + kBingo + ")");
}

TEST_CASE("scene_alias_mover_table_scene") {
    constexpr const char* kEbola = "assembly:/_pro/scenes/missions/coastaltown/scene_ebola.entity";
    const fr::SceneAlias s_Landslide = Landslide();
    fr::SceneAlias s_Author {
        kEbola, kBingo, "assembly:/_pro/scenes/missions/coastaltown/fr_mission_mild_author.brick", "the full Author",
        "",     false};
    s_Author.sharesCrowd = true;

    // Landslide's crowd is its own: its scene's rows, recorded or not.
    CHECK(fr::MoverTableScene(&s_Landslide, fr::kLandslideScene, true) == fr::kLandslideScene);
    CHECK(fr::MoverTableScene(&s_Landslide, fr::kLandslideScene, false) == fr::kLandslideScene);

    // The Author keeps mild Sapienza's day crowd: its own rows once recorded, mild Sapienza's until then.
    CHECK(fr::MoverTableScene(&s_Author, kEbola, true) == kEbola);
    CHECK(fr::MoverTableScene(&s_Author, kEbola, false) == kBingo);
    CHECK(fr::MoverTableScene(&s_Author, "ASSEMBLY:/_PRO/Scenes/Missions/CoastalTown/SCENE_EBOLA.entity", false) ==
          kBingo);

    // Any other scene (or none aliased): itself.
    CHECK(fr::MoverTableScene(&s_Author, kBingo, false) == kBingo);
    CHECK(fr::MoverTableScene(nullptr, kEbola, false) == kEbola);
    CHECK(fr::MoverTableScene(nullptr, kBingo, true) == kBingo);
}

TEST_CASE("scene_alias_marrakesh_night") {
    // The full Marrakesh night: Marrakesh's night level counted as mild Marrakesh, with the package's copy of
    // mission_mild_mancala as its mission brick and its own crowd (its own mover rows only).
    constexpr const char* kMancala = "assembly:/_pro/scenes/missions/Marrakesh/scene_spider_mild_mancala.entity";
    constexpr const char* kBrick = "assembly:/_pro/scenes/missions/marrakesh/fr_mission_mild_marrakesh_night.brick";
    const fr::SceneAlias s_Night {std::string(fr::kMarrakeshScene), kMancala, kBrick,
                                  "the full Marrakesh night",       "",       false};

    CHECK(fr::IsAliasedScene(&s_Night, "assembly:/_pro/scenes/missions/Marrakesh/_scene_python.entity"));
    CHECK(fr::ResolveScene(&s_Night, fr::kMarrakeshScene) == kMancala);
    CHECK(fr::AliasMildMissionBrickPath(&s_Night, fr::kMarrakeshScene) == std::optional<std::string>(kBrick));
    CHECK(fr::AliasMildMissionBrickPath(&s_Night, kMancala) == std::optional<std::string>(kBrick));
    CHECK(fr::MoverTableScene(&s_Night, fr::kMarrakeshScene, false) == fr::kMarrakeshScene);
    // Another night scene (the Hellebore escalation's) is not the alias.
    CHECK_FALSE(
        fr::IsAliasedScene(&s_Night, "assembly:/_pro/scenes/missions/marrakesh/_scene_python_hellebore.entity"));
}
