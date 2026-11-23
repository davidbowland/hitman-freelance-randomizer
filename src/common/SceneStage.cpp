#include "SceneStage.h"

const char* StageName(ESceneLoadingStage p_Stage) {
    switch (p_Stage) {
        case ESceneLoadingStage::eLoading_Start: return "eLoading_Start";
        case ESceneLoadingStage::eLoading_SceneStopped: return "eLoading_SceneStopped";
        case ESceneLoadingStage::eLoading_SceneDeleted: return "eLoading_SceneDeleted";
        case ESceneLoadingStage::eLoading_AssetsLoaded: return "eLoading_AssetsLoaded";
        case ESceneLoadingStage::eLoading_SceneAllocated: return "eLoading_SceneAllocated";
        case ESceneLoadingStage::eLoading_SceneStarted: return "eLoading_SceneStarted";
        case ESceneLoadingStage::eLoading_ScenePrecaching: return "eLoading_ScenePrecaching";
        case ESceneLoadingStage::eLoading_SceneActivated: return "eLoading_SceneActivated";
        case ESceneLoadingStage::eLoading_ScenePlaying: return "eLoading_ScenePlaying";
        case ESceneLoadingStage::eLoading_MAX: return "eLoading_MAX";
    }

    return "unknown stage";
}
