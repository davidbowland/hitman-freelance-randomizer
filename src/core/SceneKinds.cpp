#include "SceneKinds.h"

#include "Scene.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace fr {

namespace {

constexpr std::string_view kSceneMarker = "_mild_";
constexpr std::string_view kHotSceneMarker = "_hot_";

// ECompiledBehaviorType 0 to 140 (the SDK's Glacier/Enums.h; 4 is BT_Dummy,
// also named BT_BEHAVIORS_BEGIN).
constexpr std::array<std::string_view, kBehaviorTypeEnd> kBehaviorNames {
    "BT_ConditionScope",
    "BT_Random",
    "BT_Match",
    "BT_Sequence",
    "BT_Dummy",
    "BT_Dummy2",
    "BT_Error",
    "BT_Wait",
    "BT_WaitForStanding",
    "BT_WaitBasedOnDistanceToTarget",
    "BT_WaitForItemHandled",
    "BT_AbandonOrder",
    "BT_CompleteOrder",
    "BT_PlayAct",
    "BT_ConfiguredAct",
    "BT_PlayReaction",
    "BT_SimpleReaction",
    "BT_SituationAct",
    "BT_SituationApproach",
    "BT_SituationGetHelp",
    "BT_SituationFace",
    "BT_SituationConversation",
    "BT_Holster",
    "BT_SpeakWait",
    "BT_SpeakWaitWithFallbackIfAlone",
    "BT_ConfiguredSpeak",
    "BT_ConditionedConfiguredSpeak",
    "BT_ConditionedConfiguredAct",
    "BT_SpeakCustomOrDefaultDistractionAckSoundDef",
    "BT_SpeakCustomOrDefaultDistractionInvestigationSoundDef",
    "BT_SpeakCustomOrDefaultDistractionStndSoundDef",
    "BT_Pickup",
    "BT_Drop",
    "BT_PlayConversation",
    "BT_PlayAnimation",
    "BT_MoveToLocation",
    "BT_MoveToTargetKnownPosition",
    "BT_MoveToTargetActualPosition",
    "BT_MoveToInteraction",
    "BT_MoveToNPC",
    "BT_FollowTargetKnownPosition",
    "BT_FollowTargetActualPosition",
    "BT_PickUpItem",
    "BT_GrabItem",
    "BT_PutDownItem",
    "BT_Search",
    "BT_LimitedSearch",
    "BT_MoveTo",
    "BT_Reposition",
    "BT_SituationMoveTo",
    "BT_FormationMove",
    "BT_SituationJumpTo",
    "BT_AmbientWalk",
    "BT_AmbientStand",
    "BT_CrowdAmbientStand",
    "BT_AmbientItemUse",
    "BT_AmbientLook",
    "BT_Act",
    "BT_Patrol",
    "BT_MoveToPosition",
    "BT_AlertedStand",
    "BT_AlertedDebug",
    "BT_AttentionToPerson",
    "BT_StunnedByFlashGrenade",
    "BT_CuriousIdle",
    "BT_InvestigateWeapon",
    "BT_DeliverWeapon",
    "BT_RecoverUnconscious",
    "BT_GetOutfit",
    "BT_RadioCall",
    "BT_EscortOut",
    "BT_StashItem",
    "BT_CautiousSearchPosition",
    "BT_LockdownWarning",
    "BT_WakeUpUnconscious",
    "BT_DeadBodyInvestigate",
    "BT_GuardDeadBody",
    "BT_DragDeadBody",
    "BT_CuriousBystander",
    "BT_DeadBodyBystander",
    "BT_StandOffArrest",
    "BT_StandOffReposition",
    "BT_StandAndAim",
    "BT_CloseCombat",
    "BT_MoveToCloseCombat",
    "BT_MoveAwayFromCloseCombat",
    "BT_CoverFightSeasonTwo",
    "BT_ShootFromPosition",
    "BT_StandAndShoot",
    "BT_CheckLastPosition",
    "BT_ProtoSearchIdle",
    "BT_ProtoApproachSearchArea",
    "BT_ProtoSearchPosition",
    "BT_ShootTarget",
    "BT_TriggerAlarm",
    "BT_MoveInCover",
    "BT_MoveToCover",
    "BT_HomeAttackOrigin",
    "BT_Shoot",
    "BT_Aim",
    "BT_MoveToRandomNeighbourNode",
    "BT_MoveToRandomNeighbourNodeAiming",
    "BT_MoveToAndPlayCombatPositionAct",
    "BT_MoveToAimingAndPlayCombatPositionAct",
    "BT_PlayJumpyReaction",
    "BT_JumpyInvestigation",
    "BT_AgitatedPatrol",
    "BT_AgitatedGuard",
    "BT_HeroEscort",
    "BT_Escort",
    "BT_ControlledFormationMove",
    "BT_EscortSearch",
    "BT_LeadEscort",
    "BT_LeadEscort2",
    "BT_AimReaction",
    "BT_FollowHitman",
    "BT_RideTheLightning",
    "BT_Scared",
    "BT_Flee",
    "BT_AgitatedBystander",
    "BT_SentryFrisk",
    "BT_SentryIdle",
    "BT_SentryWarning",
    "BT_SentryCheckItem",
    "BT_VIPScared",
    "BT_VIPSafeRoomTrespasser",
    "BT_DefendVIP",
    "BT_CautiousVIP",
    "BT_CautiousGuardVIP",
    "BT_InfectedConfused",
    "BT_EnterInfected",
    "BT_CureInfected",
    "BT_SickActInfected",
    "BT_Smart",
    "BT_Controlled",
    "BT_SpeakTest",
    "BT_Conversation",
    "BT_RunToHelp",
    "BT_WaitForDialog",
    "BT_WaitForConfiguredAct",
    "BT_TestFlashbangGrenadeThrow",
};

// Brick runtime resource id (the MFT patch's tempHash) -> the
// Helper_Evergreen_Spawn_Targets entity its patch edits.
struct KnownHelper {
    uint64_t brickRid;
    uint64_t helperId;
};

constexpr std::array<KnownHelper, 24> kKnownHelpers {{
    {0x008CDE1D57EDF6C9, 0xb903fe02330c7e8a}, // Dubai, chess
    {0x00915F68E5D78A5B, 0x123d26152394a9df}, // Haven, hilo
    {0x00D166E0B6B33168, 0x72e74299a1dc66fe}, // New York, blackjack
    {0x00590B4B89C00E50, 0x8b3aa1ba18464731}, // Isle of Sgail, whist
    {0x00B4DA544A107E74, 0x7b246b50cd81d75c}, // Whittleton Creek, solitaire
    {0x0085023077F99752, 0x38db0abc6eea8297}, // Mumbai, ludo
    {0x009A7356A1593833, 0x2ca408a9694d11d3}, // Santa Fortuna, trompo
    {0x004D4229C4B64540, 0x6c2968716337aea1}, // Miami, pinochle
    {0x00D6C83B2487913C, 0x1cd639bf97c91b8f}, // Hokkaido, shogi
    {0x0055FB7FD3AA2DA1, 0x7bc8436345790d03}, // Colorado, spades
    {0x0085DAB0452AE7E7, 0x061efd4c27bb4894}, // Bangkok, sicbo
    {0x009613F820E477D4, 0x6dd94565adf91563}, // Marrakesh, mancala
    {0x00D15B6A8159D00D, 0x98ca7baed06a1211}, // Sapienza, bingo
    {0x00C257440C6A6F3A, 0x3be7fdda23725722}, // Paris, craps
    {0x00EC6F39AB4C993D, 0x2a4c5ae131266493}, // Ambrose Island, poker
    {0x0050299D79861FA5, 0xb4ead3d8d9214bd5}, // Dartmoor, bridge
    {0x003DD08876288CD8, 0xa58fe041f1863e60}, // Berlin, meyer
    {0x00B52F185158BD26, 0x00c594397ea7af0b}, // Mendoza, sapo
    {0x00EC3CEC3F43C631, 0x0e9653f81c8ea1dd}, // Chongqing, mahjong
    // The full Landslide's mission brick (fr_mission_mild_landslide.brick, the
    // mod's copy of mission_mild_bingo; core/SceneAlias.h): the same helper.
    {0x00F48FAD6DC84140, 0x98ca7baed06a1211},
    // The full Author's (fr_mission_mild_author.brick), likewise.
    {0x009877248AF5209E, 0x98ca7baed06a1211},
    // The full Icon's (fr_mission_mild_icon.brick), likewise.
    {0x0038F3EA6201210D, 0x98ca7baed06a1211},
    // The full Marrakesh night's (fr_mission_mild_marrakesh_night.brick, the
    // mod's copy of mission_mild_mancala): mancala's helper.
    {0x00E30A5A924F6813, 0x6dd94565adf91563},
    // The full Patient Zero's (fr_mission_mild_patientzero.brick, the mod's
    // copy of mission_mild_shogi): shogi's helper.
    {0x00D2065FDB6203F1, 0x1cd639bf97c91b8f},
}};

constexpr std::array<std::string_view, 5> kRoles {"lookout", "assassin", "mule", "supplier", "suspect"};

// The file name part of a resource path.
std::string_view FileName(std::string_view path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// The <game> of scene_*_mild_<game>.entity (in a lower-case path), or empty.
std::optional<std::string> GameAfter(const std::string& lowerScene, std::string_view sceneMarker) {
    const std::string_view file = FileName(lowerScene);
    constexpr std::string_view prefix = "scene_";
    constexpr std::string_view suffix = ".entity";

    if (!file.starts_with(prefix) || !file.ends_with(suffix))
        return std::nullopt;

    const std::string_view stem = file.substr(0, file.size() - suffix.size());
    const size_t marker = stem.rfind(sceneMarker);

    if (marker == std::string_view::npos || marker < prefix.size())
        return std::nullopt;

    const std::string_view game = stem.substr(marker + sceneMarker.size());

    if (game.empty() || game.find('/') != std::string_view::npos)
        return std::nullopt;

    return std::string(game);
}

std::optional<std::string> MildGame(const std::string& lowerScene) {
    return GameAfter(lowerScene, kSceneMarker);
}

} // namespace

bool IsMildMissionScene(std::string_view scene) {
    return MildGame(ToLower(scene)).has_value();
}

std::optional<std::string> MildMissionBrickPath(std::string_view scene) {
    const std::string lower = ToLower(scene);
    const auto game = MildGame(lower);

    if (!game)
        return std::nullopt;

    const size_t slash = lower.find_last_of('/');
    const std::string folder = slash == std::string::npos ? std::string() : lower.substr(0, slash + 1);
    return folder + "mission_mild_" + *game + ".brick";
}

bool IsShowdownScene(std::string_view scene) {
    return GameAfter(ToLower(scene), kHotSceneMarker).has_value();
}

std::optional<std::string> MissionBrickPath(std::string_view scene) {
    if (auto mild = MildMissionBrickPath(scene))
        return mild;

    const std::string lower = ToLower(scene);
    const auto game = GameAfter(lower, kHotSceneMarker);

    if (!game)
        return std::nullopt;

    const size_t slash = lower.find_last_of('/');
    const std::string folder = slash == std::string::npos ? std::string() : lower.substr(0, slash + 1);
    return folder + "mission_hot_" + *game + ".brick";
}

std::optional<uint64_t> KnownTargetPoolHelper(uint64_t brickRid) {
    for (const KnownHelper& known : kKnownHelpers) {
        if (known.brickRid == brickRid)
            return known.helperId;
    }

    return std::nullopt;
}

std::string BehaviorName(int32_t type) {
    if (type >= 0 && type < kBehaviorTypeEnd)
        return std::string(kBehaviorNames[static_cast<size_t>(type)]);

    char text[24];
    std::snprintf(text, sizeof(text), "BT_#%d", type);
    return text;
}

std::optional<std::string_view> RoleFromActorName(std::string_view name) {
    const std::string lower = ToLower(name);

    for (const std::string_view role : kRoles) {
        if (lower.find(role) != std::string::npos)
            return role;
    }

    return std::nullopt;
}

} // namespace fr
