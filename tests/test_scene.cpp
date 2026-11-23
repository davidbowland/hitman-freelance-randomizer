#include "doctest.h"

#include "Scene.h"

TEST_CASE("freelancer_detection") {
    constexpr const char* mission = "assembly:/_PRO/Scenes/Missions/Ancestral/scene_ancestral_mild_bridge.entity";
    CHECK(fr::IsFreelancerMission("evergreen", mission));
    CHECK_FALSE(fr::IsFreelancerMission("mission", mission));
}

TEST_CASE("brick_resource_path") {
    CHECK(fr::BrickResourcePath("assembly:/_pro/scenes/missions/snug/mission_vanilla.brick") ==
          "[assembly:/_pro/scenes/missions/snug/mission_vanilla.brick].pc_entitytype");
    CHECK(fr::BrickResourcePath("assembly:/_PRO/Scenes/Bricks/Evergreen_Setup.brick") ==
          "[assembly:/_pro/scenes/bricks/evergreen_setup.brick].pc_entitytype");
    CHECK(fr::BrickResourcePath("assembly:/_pro/scenes/missions/paris/scene_peacock_hot_craps.entity") ==
          "[assembly:/_pro/scenes/missions/paris/scene_peacock_hot_craps.entity].pc_entitytype");
    CHECK(fr::BrickResourcePath("[assembly:/_pro/scenes/bricks/X.brick].pc_entitytype") ==
          "[assembly:/_pro/scenes/bricks/x.brick].pc_entitytype");
    CHECK(fr::BrickResourcePath("") == "[].pc_entitytype");
}

TEST_CASE("scene_resource_path") {
    // A scene's TEMP is its .pc_entitytemplate (004B6755A8F753FA, 00FD4FC867F311FE; MD5 checked offline), not
    // the .pc_entitytype a brick's is.
    CHECK(fr::SceneResourcePath("assembly:/_PRO/Scenes/Missions/CoastalTown/scene_costaltown_mild_bingo.entity") ==
          "[assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_mild_bingo.entity].pc_entitytemplate");
    CHECK(fr::SceneResourcePath("assembly:/_pro/scenes/missions/coastaltown/_scene_mamba.entity") ==
          "[assembly:/_pro/scenes/missions/coastaltown/_scene_mamba.entity].pc_entitytemplate");
    CHECK(fr::SceneResourcePath("[assembly:/_pro/scenes/X.entity].pc_entitytemplate") ==
          "[assembly:/_pro/scenes/x.entity].pc_entitytemplate");
}

TEST_CASE("safehouse_detection_case") {
    CHECK_FALSE(fr::IsFreelancerMission("evergreen", "assembly:/_pro/scenes/missions/snug/scene_vanilla.entity"));
    CHECK_FALSE(fr::IsFreelancerMission("evergreen", "assembly:/_PRO/Scenes/Missions/SNUG/scene_vanilla.entity"));
}

TEST_CASE("freelancer_scene_includes_safehouse") {
    CHECK(fr::IsFreelancerScene("evergreen"));
    CHECK_FALSE(fr::IsFreelancerScene(""));
    CHECK_FALSE(fr::IsFreelancerScene("mission"));
}
