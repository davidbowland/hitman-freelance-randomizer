#include "doctest.h"

#include "SceneKinds.h"

#include <Glacier/Enums.h>

#include <optional>
#include <set>
#include <string>

namespace {

int32_t B(ECompiledBehaviorType type) {
    return static_cast<int32_t>(type);
}

} // namespace

TEST_CASE("IsMildMissionScene and MildMissionBrickPath") {
    CHECK(fr::IsMildMissionScene("assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.entity"));
    CHECK(fr::IsMildMissionScene("assembly:/_PRO/Scenes/Missions/TheArk/Scene_Magpie_Mild_Whist.entity"));
    CHECK_FALSE(fr::IsMildMissionScene("assembly:/_pro/scenes/missions/snug/scene_vanilla.entity"));
    CHECK_FALSE(fr::IsMildMissionScene("assembly:/_pro/scenes/missions/berlin/scene_fox_hot_meyer.entity"));
    CHECK_FALSE(fr::IsMildMissionScene("assembly:/_PRO/Scenes/Frontend/MainMenu.entity"));
    CHECK_FALSE(fr::IsMildMissionScene("assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_.entity"));
    CHECK_FALSE(fr::IsMildMissionScene("assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.brick"));
    CHECK_FALSE(fr::IsMildMissionScene(""));

    CHECK(fr::MildMissionBrickPath("assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.entity") ==
          std::optional<std::string>("assembly:/_pro/scenes/missions/mumbai/mission_mild_ludo.brick"));
    CHECK(fr::MildMissionBrickPath("assembly:/_PRO/Scenes/Missions/TheArk/Scene_Magpie_Mild_Whist.entity") ==
          std::optional<std::string>("assembly:/_pro/scenes/missions/theark/mission_mild_whist.brick"));
    CHECK_FALSE(fr::MildMissionBrickPath("assembly:/_pro/scenes/missions/snug/scene_vanilla.entity"));
    CHECK_FALSE(fr::MildMissionBrickPath("assembly:/_pro/scenes/missions/berlin/scene_fox_hot_meyer.entity"));
}

TEST_CASE("IsShowdownScene and MissionBrickPath") {
    CHECK(fr::IsShowdownScene("assembly:/_pro/scenes/missions/ancestral/scene_ancestral_hot_bridge.entity"));
    CHECK(fr::IsShowdownScene("assembly:/_pro/scenes/missions/edgy/mission_fox/scene_edgy_hot_meyer.entity"));
    CHECK_FALSE(fr::IsShowdownScene("assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.entity"));
    CHECK_FALSE(fr::IsShowdownScene("assembly:/_pro/scenes/missions/snug/scene_vanilla.entity"));
    CHECK_FALSE(fr::IsShowdownScene(""));

    CHECK(fr::MissionBrickPath("assembly:/_pro/scenes/missions/ancestral/scene_ancestral_hot_bridge.entity") ==
          std::optional<std::string>("assembly:/_pro/scenes/missions/ancestral/mission_hot_bridge.brick"));
    CHECK(fr::MissionBrickPath("assembly:/_PRO/Scenes/Missions/Paris/Scene_Peacock_Hot_Craps.entity") ==
          std::optional<std::string>("assembly:/_pro/scenes/missions/paris/mission_hot_craps.brick"));
    CHECK(fr::MissionBrickPath("assembly:/_pro/scenes/missions/mumbai/scene_mongoose_mild_ludo.entity") ==
          std::optional<std::string>("assembly:/_pro/scenes/missions/mumbai/mission_mild_ludo.brick"));
    CHECK_FALSE(fr::MissionBrickPath("assembly:/_pro/scenes/missions/snug/scene_vanilla.entity"));
}

TEST_CASE("KnownTargetPoolHelper") {
    // The full Landslide's, the full Author's and the full Icon's copies of mission_mild_bingo keep bingo's helper.
    CHECK(fr::KnownTargetPoolHelper(0x00F48FAD6DC84140) == fr::KnownTargetPoolHelper(0x00D15B6A8159D00D));
    CHECK(fr::KnownTargetPoolHelper(0x009877248AF5209E) == fr::KnownTargetPoolHelper(0x00D15B6A8159D00D));
    CHECK(fr::KnownTargetPoolHelper(0x0038F3EA6201210D) == fr::KnownTargetPoolHelper(0x00D15B6A8159D00D));
    // The full Marrakesh night's copy of mission_mild_mancala keeps mancala's.
    CHECK(fr::KnownTargetPoolHelper(0x00E30A5A924F6813) == std::optional<uint64_t>(0x6dd94565adf91563));
    CHECK(fr::KnownTargetPoolHelper(0x00E30A5A924F6813) == fr::KnownTargetPoolHelper(0x009613F820E477D4));
    // The full Patient Zero's copy of mission_mild_shogi (fr_mission_mild_patientzero.brick): shogi's helper.
    CHECK(fr::KnownTargetPoolHelper(0x00D2065FDB6203F1) == std::optional<uint64_t>(0x1cd639bf97c91b8f));
    CHECK(fr::KnownTargetPoolHelper(0x00D2065FDB6203F1) == fr::KnownTargetPoolHelper(0x00D6C83B2487913C));
    // Mumbai's mission_mild_ludo.brick (its pc_entitytype hash) and Ambrose's.
    CHECK(fr::KnownTargetPoolHelper(0x0085023077F99752) == std::optional<uint64_t>(0x38db0abc6eea8297));
    CHECK(fr::KnownTargetPoolHelper(0x00EC6F39AB4C993D) == std::optional<uint64_t>(0x2a4c5ae131266493));
    CHECK(fr::KnownTargetPoolHelper(0x0050299D79861FA5) == std::optional<uint64_t>(0xb4ead3d8d9214bd5));
    CHECK_FALSE(fr::KnownTargetPoolHelper(0));
    CHECK_FALSE(fr::KnownTargetPoolHelper(0x00ABCDEF01234567));
}

TEST_CASE("BehaviorName matches the SDK's ECompiledBehaviorType") {
    CHECK(fr::BehaviorName(B(ECompiledBehaviorType::BT_ConditionScope)) == "BT_ConditionScope");
    CHECK(fr::BehaviorName(B(ECompiledBehaviorType::BT_Dummy)) == "BT_Dummy");
    CHECK(fr::BehaviorName(B(ECompiledBehaviorType::BT_PlayAct)) == "BT_PlayAct");
    CHECK(fr::BehaviorName(B(ECompiledBehaviorType::BT_MoveToLocation)) == "BT_MoveToLocation");
    CHECK(fr::BehaviorName(B(ECompiledBehaviorType::BT_AmbientWalk)) == "BT_AmbientWalk");
    CHECK(fr::BehaviorName(B(ECompiledBehaviorType::BT_Act)) == "BT_Act");
    CHECK(fr::BehaviorName(B(ECompiledBehaviorType::BT_Patrol)) == "BT_Patrol");
    CHECK(fr::BehaviorName(B(ECompiledBehaviorType::BT_MoveToPosition)) == "BT_MoveToPosition");
    CHECK(fr::BehaviorName(B(ECompiledBehaviorType::BT_AgitatedPatrol)) == "BT_AgitatedPatrol");
    CHECK(fr::BehaviorName(B(ECompiledBehaviorType::BT_TestFlashbangGrenadeThrow)) == "BT_TestFlashbangGrenadeThrow");
    CHECK(B(ECompiledBehaviorType::BT_BEHAVIORS_END) == fr::kBehaviorTypeEnd);
    CHECK(fr::BehaviorName(fr::kBehaviorTypeEnd) == "BT_#141");
    CHECK(fr::BehaviorName(-1) == "BT_#-1");

    for (int32_t i = 0; i < fr::kBehaviorTypeEnd; ++i)
        CHECK(fr::BehaviorName(i).starts_with("BT_"));

    std::set<std::string> names;

    for (int32_t i = 0; i < fr::kBehaviorTypeEnd; ++i)
        names.insert(fr::BehaviorName(i));

    CHECK(names.size() == static_cast<size_t>(fr::kBehaviorTypeEnd));
}

TEST_CASE("RoleFromActorName") {
    CHECK(fr::RoleFromActorName("Lookout 1") == std::optional<std::string_view>("lookout"));
    CHECK(fr::RoleFromActorName("The ASSASSIN") == std::optional<std::string_view>("assassin"));
    CHECK(fr::RoleFromActorName("Money Mule") == std::optional<std::string_view>("mule"));
    CHECK(fr::RoleFromActorName("Ali Supplier") == std::optional<std::string_view>("supplier"));
    CHECK(fr::RoleFromActorName("Suspect_04") == std::optional<std::string_view>("suspect"));
    CHECK_FALSE(fr::RoleFromActorName("Mark Chase"));
    CHECK_FALSE(fr::RoleFromActorName(""));
}
