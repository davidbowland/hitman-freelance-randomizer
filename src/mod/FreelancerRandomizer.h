#pragma once

#include "BrickMemory.h"
#include "ClientReqShuffle.h"
#include "CoinSpawn.h"
#include "Config.h"
#include "Debounce.h"
#include "DialInfo.h"
#include "DialRows.h"
#include "Dials.h"
#include "EntityWalk.h"
#include "FishingSpot.h"
#include "GuardWeapons.h"
#include "InvestigationFeatures.h"
#include "LandslidePackage.h"
#include "MoneyItems.h"
#include "CrateGuns.h"
#include "CrateLamps.h"
#include "MercesPayouts.h"
#include "PatchBook.h"
#include "PrestigeShuffle.h"
#include "RandomLoadout.h"
#include "Repository.h"
#include "SafeSpotLog.h"
#include "StockSteer.h"
#include "SupplierWalk.h"
#include "Seed.h"
#include "SensorDials.h"
#include "SettingsMenu.h"
#include "SettingsTable.h"
#include "SickSpotSteer.h"
#include "TargetPoolSwap.h"
#include "TouchScroll.h"
#include "DoorLockShuffle.h"
#include "EnforcerShuffle.h"
#include "Variants.h"
#include "LegalSniperKits.h"
#include "MaceConceal.h"
#include "WeaponRarityShuffle.h"

#include <IPluginInterface.h>

#include <Glacier/SGameUpdateEvent.h>
#include <Glacier/ZResource.h>
#include <Glacier/ZScene.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ZAchievementManagerSimple;
class ZDynamicObject;
class ZEvergreenCampaignManager;
class ZTemplateEntityBlueprintFactory;
struct STemplateEntityBlueprint;

class FreelancerRandomizer : public IPluginInterface {
public:
    ~FreelancerRandomizer() override;

    void Init() override;
    void OnEngineInitialized() override;
    void OnDrawMenu() override;
    void OnDrawUI(bool p_HasFocus) override;

private:
    // One difficulty-parameter entity this mod wrote: the scene-start fallback
    // when pro.repo could not be written before the scene loaded.
    struct Applied {
        ZEntityRef entity;
        int32_t param;
        float base;
        double multiplier;
        float written;
        bool writeOk;
        std::string status;             // "written", "write failed", then the read-back result
        bool readBackStopped = false;   // a read-back faulted; this entity is not read again
        std::optional<fr::Guid> repoId; // m_RepositoryId, for the overlay's difficulty tier
    };

    // One pro.repo entry written because its "Parameter" names a parameter the
    // mode changes (every such entry, before the scene loads).
    struct RepoWrite {
        fr::Guid guid;
        int32_t param;
        RepoValueKind kind;
        double base;
        double multiplier;
        double written;
        std::string status; // "written", then the read-back result
    };

    // Play-time checks after playing starts: read back written values and/or
    // read the live seed again at 5 s and 60 s.
    struct ReadBackTimer {
        bool armed = false;
        bool readValues = false; // values were written for this mission
        bool checkSeed = false;  // a Freelancer mission whose seed was resolved
        double elapsed = 0.0;
        bool did5 = false;
        bool did60 = false;
    };

    // One parameter's writes (every difficulty's pro.repo entry, entity dials, or fallback entities).
    struct OverlayRow {
        std::string name;                 // friendly name (fr::DialDisplayName)
        std::string paramName;            // enumerator name
        const fr::DialInfo* info;         // static table entry; nullptr when the parameter has none
        int8_t dir;                       // the dial table's harder direction
        fr::FreelancerValue freelancer;   // how Freelancer's (professional) value turned out
        bool noEffect;                    // fr::ViewDialRow (a sensor row: not written or not held): drawn
                                          // disabled, marked "(likely none)", sorted last
        std::string value;                // fr::FreelancerValueText without the unit
        std::string unit;                 // fr::UnitLabel; empty for a count
        bool unitInferred;                // shown marked with a help icon
        std::string multiplier;           // "x1.2345", or "varies"
        std::string status;               // see fr::DialSummary::status
        std::vector<std::string> details; // one line per written entry, for the value cell's tooltip
        fr::DialRating rating;            // of multiplier: color, marker and "Extreme first" order
    };

    // What the overlay shows; written on the game thread, copied by the render
    // thread, both under m_SnapshotMutex.
    struct OverlaySnapshot {
        bool enabled = true;
        std::string scene;
        std::string type;
        bool isMission = false;
        std::string mode;          // what this map gets: dials or nothing
        std::string status;        // short progress or problem line
        std::string readBack;      // the last check of the AI values written, for Troubleshooting
        std::string appliedAt;     // "LoadScene", "restart (eLoading_AssetsLoaded)" or "scene start (fallback)"
        std::string repoSummary;   // pro.repo entries written, per parameter count
        std::string entityCheck;   // scene start check of the rebuilt entities
        std::string sensorSummary; // sensor dials written while loading (SensorDials.cpp)
        std::optional<fr::SeedResolution> seed;
        std::optional<int32_t> lateSeed; // found after scene start; used from the next mission
        size_t found = 0;
        size_t valuesWritten = 0; // pro.repo entries and entities written
        // One per parameter; shared and immutable, so the per-frame snapshot copy does not copy rows.
        std::shared_ptr<const std::vector<OverlayRow>> rows;
        fr::DialBands bands = fr::BandsFor(fr::RollConfig {}); // the rows' rating thresholds, for the legend
    };

    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, ZEvergreenCampaignManager*, ZEvergreenCampaignManager_OnGenerate,
                          ZEvergreenCampaignManager* th);
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, bool, ZEntitySceneContext_LoadScene, ZEntitySceneContext* th,
                          SSceneInitParameters& parameters);
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, void, ZEntitySceneContext_SetLoadingStage, ZEntitySceneContext* th,
                          ESceneLoadingStage stage);
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, void, ZEntitySceneContext_ClearScene, ZEntitySceneContext* th,
                          bool bFullyUnloadScene);
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, bool, SignalOutputPin, ZEntityRef entity, uint32_t pinId,
                          const ZObjectRef& data);
    // Defined in PinHooks.cpp; added by EnsureDetours when a feature needs input pins.
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, bool, SignalInputPin, ZEntityRef entity, uint32_t pinId,
                          const ZObjectRef& data);
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, void, ZEntityManager_ActivateEntity, ZEntityManager* th,
                          ZEntityRef* entity, void* a3);
    // Defined in GuardWeapons.cpp.
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, void, ZActorInventoryHandler_StartItemStreamIn,
                          ZActorInventoryHandler* th, TArray<TEntityRef<ZItemRepositoryKeyEntity>>& rInventoryKeys,
                          TEntityRef<ZItemRepositoryKeyEntity>& rWeaponKey,
                          TEntityRef<ZItemRepositoryKeyEntity>& rGrenadeKey);
    // Defined in RandomLoadout.cpp: records the create type of 47's packed gear, which the loadout's items use.
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, ZCharacterSubcontrollerInventory::SCreateItem*,
                          ZCharacterSubcontrollerInventory_CreateItem, ZCharacterSubcontrollerInventory* th,
                          ZRepositoryID& repId, const ZString& sOnlineInstanceId,
                          const TArray<ZRepositoryID>& instanceModifiersToApply,
                          ZCharacterSubcontrollerInventory::ECreateItemType createItemType);
    // Always added: hands the game's events to the features that watch them.
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, void, ZAchievementManagerSimple_OnEventSent,
                          ZAchievementManagerSimple* th, uint32_t eventIndex, const ZDynamicObject& event);
    // Defined in PinHooks.cpp: copies the blueprints Diana's briefing finder reads while a map variant's load is
    // armed (BriefingGraphs.h); the game drops them once the factory is built.
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, ZTemplateEntityBlueprintFactory*,
                          ZTemplateEntityBlueprintFactory_ZTemplateEntityBlueprintFactory,
                          ZTemplateEntityBlueprintFactory* th, STemplateEntityBlueprint* pTemplateEntityBlueprint,
                          ZResourcePending& ResourcePending);
    // Defined in LandslidePackage.cpp; added only when a full variants' package was installed.
    DECLARE_PLUGIN_DETOUR(FreelancerRandomizer, void*, IPackageManager_SPartitionInfo_SPartitionInfo,
                          IPackageManager::SPartitionInfo* th, int32_t index, ZString partitionID,
                          IPackageManager::EPartitionType type, int32_t patchLevel);

    void OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent);

    void LoadConfig();
    // A setting changed in the menu: the whole ini is read again (LoadConfig),
    // missing hooks are added, and the menu gets the values read. Called on the
    // game thread at a scene load (LoadScene, or a restart's assets loaded).
    void ReloadConfigIfChanged(const char* p_When);
    // The hooks the settings need that are not added yet (Init and after a
    // reload); hooks are never removed (their handlers check the settings).
    void EnsureDetours();
    // The ini's text of every fr::SettingsTable() key, to the Settings tab.
    void PublishSettings();
    // The Settings tab's writer (render thread).
    void WriteMenuSetting(const fr::SettingInfo& p_Info, const std::string& p_Text);
    // A Load* default that differs from the settings table's is logged (a row to fix).
    void CheckTableDefault(std::string_view p_Key, std::string_view p_Default);

    // Every call into the SDK's settings API goes through these (they hide
    // IPluginInterface's): the Settings tab writes on the render thread, the
    // hooks read and write on the game thread.
    void SetSetting(const ZString& p_Section, const ZString& p_Name, const ZString& p_Value);
    void SetSettingInt(const ZString& p_Section, const ZString& p_Name, int64 p_Value);
    void SetSettingDouble(const ZString& p_Section, const ZString& p_Name, double p_Value);
    void SetSettingBool(const ZString& p_Section, const ZString& p_Name, bool p_Value);
    ZString GetSetting(const ZString& p_Section, const ZString& p_Name, const ZString& p_Default);
    bool GetSettingBool(const ZString& p_Section, const ZString& p_Name, bool p_Default);
    bool HasSetting(const ZString& p_Section, const ZString& p_Name);

    // A Settings tab key's value; a missing key gets its default written.
    bool LoadBool(std::string_view p_Key, bool p_Default);
    double LoadDouble(std::string_view p_Key, double p_Default);
    // A share or chance: the ini holds a percent (33), the result is the fraction (0.33).
    double LoadPercent(std::string_view p_Key, double p_DefaultFraction);
    int32_t LoadInt(std::string_view p_Key, int32_t p_Default);
    // A key that is not in the Settings tab (set by hand in the ini): its value,
    // or p_Default when the ini has none. Never written.
    double ReadIniOnlyDouble(std::string_view p_Key, double p_Default);
    // An existing key's number, or p_Default when it is not one (logged).
    double SettingDouble(std::string_view p_Key, double p_Default);
    std::optional<int32_t> ReadPersistedSeed();
    void ResumeClientReqs();
    void PersistSeed(int32_t p_Seed);
    fr::PrestigeMemory ReadPrestigeMemory();
    void SavePrestigeMemory(const fr::PrestigeMemory& p_Memory);
    void LoadPayoutConfig();
    void LoadLootBounds(fr::MoneyKind p_Kind, int32_t p_Min, int32_t p_Max);
    void LoadClientReqsConfig();
    void LoadWeaponRarityConfig();
    // Game thread: the weapon pro.repo writes of a Freelancer scene (the
    // holsterable mace, then the rarities) or their restore elsewhere.
    void ApplyWeaponRepo(const char* p_When);
    void RestoreWeaponRepo(const char* p_When);
    void LoadDoorLocksConfig();
    void LoadRandomLoadoutConfig();
    void LoadSafehouseCoinConfig();
    void LoadFishingConfig();
    void LoadInvestigationsConfig(); // footsteps_investigate(_chance), backup_guard
    void LoadPrestigeShuffleConfig();
    void LoadSensesConfig(); // footsteps and sensor dials
    // The campaign seed of this scene: the mission's resolved seed, else
    // generated > persisted (the safehouse's newest, maybe unpicked,
    // campaign); empty when unknown. The prestige shuffle draws from it.
    std::optional<int32_t> SafehouseSeed();
    // The weapon rarity shuffle's campaign ([campaign] rarity_seed and
    // rarity_live): loaded at init, changed only by OnCampaignPicked and
    // OnRarityMission (fr::RarityOnPick, fr::RarityOnMission).
    void LoadRaritySeed();
    // The client reqs watch saw the campaign pick: another campaign rolls its
    // rarities, persisted and, in the safehouse, written at once.
    void OnCampaignPicked(int32_t p_Seed, const char* p_How);
    // p_Seed is the campaign's own (mission) seed, found at the pick or late
    // in a mission: persisted and used from now on instead of the generated
    // seed. What was keyed by the campaign's generated seed p_Generated (the
    // client reqs check, the prestige pick state) carries over to it.
    void AdoptCampaignSeed(int32_t p_Seed, std::optional<int32_t> p_Generated);
    // A Freelancer mission runs with campaign seed p_Seed (p_Live: read from
    // the live seed entity, else resolved at LoadScene): the picked campaign's
    // own seed is bound to it, another campaign's rolls (its pick was missed).
    void OnRarityMission(int32_t p_Seed, bool p_Live, const char* p_When);
    // Applies fr::RarityDecision's state: kept in memory and in the ini.
    void SetRarityCampaign(const fr::RarityCampaign& p_Campaign);

    void OnSceneStarted(ZEntitySceneContext* p_Context);
    WalkStats WalkScene(const TArray<SBrickAllocationInfo>& p_Bricks, std::vector<FoundParam>& p_Found);
    WalkStats TimedWalk(const TArray<SBrickAllocationInfo>& p_Bricks, std::vector<FoundParam>& p_Found,
                        const std::vector<uint64_t>* p_OnlyBricks);
    fr::SeedResolution ResolveSceneSeed(const std::optional<int32_t>& p_Live, const char* p_LiveNote);
    void CheckLateSeed(const TArray<SBrickAllocationInfo>& p_Bricks, const char* p_Label);
    void ApplyEntities(fr::Mode p_Mode, const std::vector<FoundParam>& p_Found, int32_t p_Seed);
    float BaseOf(const FoundParam& p_Found);
    void Write(const FoundParam& p_Found, float p_Base, double p_Multiplier, float p_Value);
    void ReadBack(const char* p_Label);

    // Writing pro.repo before the scene loads (every entry whose
    // "Parameter" names a parameter the mode changes), checking the rebuilt
    // entities at scene start and restoring (RepoPatch.cpp).
    void EarlyApply(const char* p_When);
    bool ApplyRepo(fr::Mode p_Mode, int32_t p_Seed, const char* p_When);
    void ApplyFootstepEntry(const RepoParamEntry& p_Entry, int32_t p_Param, int32_t p_Seed,
                            std::map<int32_t, std::string>& p_Report);
    void ApplyAlwaysEmitEntry(const RepoParamEntry& p_Entry, std::string& p_Report);
    void LogFootsteps(int32_t p_Seed, const std::map<int32_t, std::string>& p_Report, const std::string& p_AlwaysEmit);
    void CheckEntities(const std::vector<FoundParam>& p_Found);
    void RestorePatches(const TArray<SBrickAllocationInfo>* p_Bricks, const char* p_When);
    void RestoreEntityPatches(const TArray<SBrickAllocationInfo>* p_Bricks);
    void RestoreRepoPatches();

    // The last loading stage seen, named (PinHooks.cpp).
    std::string StageLabel() const;

    void ResetMapState();
    std::vector<fr::DialEntry> DialEntries() const; // this mission's writes: pro.repo entries, then entities
    void PublishRows();
    void PublishSensors(); // the sensor dials' summary and rows
    // Render thread: the Status tab's Random values section (the AI values table).
    void DrawStatusDials(const OverlaySnapshot& p_Snapshot);
    // Render thread: the backup notice for a few seconds after someone answers a backup (backup_guard_notice).
    void DrawBackupNotice();

    template <class F>
    void UpdateSnapshot(F&& p_Update) {
        std::lock_guard s_Lock(m_SnapshotMutex);
        p_Update(m_Snapshot);
    }

private:
    fr::Config m_Config;
    std::optional<int32_t> m_GeneratedSeed;   // OnGenerate's seed, until the campaign's own seed is adopted
    std::optional<fr::SeedAlias> m_SeedAlias; // the generated seed -> the adopted campaign seed (this session)
    fr::RarityCampaign m_Rarity;              // the picked campaign: the rarities' seed and its missions' live seed
    bool m_FrameUpdateRegistered = false;

    // Per map. m_Scene/m_Type/m_IsMission come from LoadScene and survive
    // ClearScene (which the engine may run for the old scene after LoadScene).
    std::string m_Scene;
    std::string m_Type;
    bool m_IsMission = false;
    bool m_LoadPending = false;        // between LoadScene and scene start
    bool m_ClearedWithoutLoad = false; // a scene was cleared and no LoadScene followed yet (a restart, so far)
    bool m_SceneStartedHandled = false;
    std::vector<Applied> m_Applied;                // entity writes: the scene-start fallback
    std::vector<RepoWrite> m_RepoWrites;           // pro.repo entries written for this mission
    bool m_RepoApplied = false;                    // pro.repo was written for this load (before the scene loaded)
    std::string m_AppliedAt;                       // when: "LoadScene", "restart (eLoading_AssetsLoaded)", ...
    fr::Mode m_Mode = fr::Mode::NotMission;        // what this map gets
    std::map<int32_t, std::string> m_EntityValues; // per parameter, the entity values seen at scene start
    ReadBackTimer m_ReadBack;
    std::optional<fr::SeedResolution>
        m_SceneSeed;                   // resolved for this mission (before the scene loads, or at scene start)
    std::optional<int32_t> m_LateSeed; // last seed found late and persisted for this map

    // Session memory: bricks where difficulty parameters were found, and how
    // many parameters each scene's last full walk found.
    fr::BrickMemory m_ParamBricks;
    bool m_LoggedNoWalk = false;         // a non-evergreen scene skipped its walk (logged once per session)
    std::optional<double> m_DialsWalkMs; // this scene start's dials walks, ms (logged with the merces search)

    // The seed each scene was first started with this session; forgotten when
    // another scene loads (kept on a full unload: a restart may fully unload
    // before its LoadScene).
    fr::SceneSeedMemory m_FirstSeeds;

    // Values this mod changed and has not restored: entities keyed by
    // (pointer, 0), pro.repo entries by (GUID high, GUID low). Restored on
    // ClearScene of the patched mission, before a new scene is patched, at a
    // non-mission scene start and on mod unload; a
    // value still holding what was written is rolled from its original (no stacking).
    fr::PatchBook m_EntityPatches;
    fr::PatchBook m_RepoPatches;
    // Footsteps: pro.repo Toggles turned on (1) from their original (0 or 1),
    // keyed like m_RepoPatches; restored with them.
    fr::PatchBook m_RepoToggles;
    std::string m_PatchedScene;

    ProRepository m_Repo;

    GuardWeapons m_GuardWeapons {m_Repo};        // guard weapon swap (guard_weapons)
    LandslidePackage m_LandslidePackage;         // the full variants' packages and checks
    Variants m_Variants;                         // official map variants (variants)
    MercesPayouts m_Merces;                      // payout table: random scale (payouts)
    SafeSpotLog m_SafeSpots;                     // where each safe can appear and which one did (log_safe_spots)
    CrateGuns m_CrateGuns {m_Repo};              // crate guns: seeded gun crates (crate_guns)
    CrateLamps m_CrateLamps;                     // supply crate lamps dimmed on night map variants
    MoneyItems m_MoneyItems;                     // loot amounts: each money item's own seeded amount (loot_amounts)
    TargetPoolSwap m_TargetPool;                 // target pool shuffle (target_pool)
    EnforcerShuffle m_Enforcers;                 // enforcer shuffle (enforcers)
    DoorLockShuffle m_DoorLocks;                 // door lock shuffle (door_locks)
    ClientReqShuffle m_ClientReqs;               // client requirements shuffle (client_reqs)
    WeaponRarityShuffle m_WeaponRarity {m_Repo}; // firearm rarity shuffle (weapon_rarity)
    MaceConceal m_Mace {m_Repo};                 // holsterable mace (concealable_mace)
    LegalSniperKits m_LegalSniper {m_Repo}; // legal sniper rifles in outfits that allow firearms (legal_sniper_rifles)
    RandomLoadout m_Loadout {m_Repo};       // random loadout (random_loadout) and its CPD / inventory reads
    CoinSpawn m_Coin;                       // safehouse coin (safehouse_coin)
    FishingSpot m_Fishing {m_Repo};         // safehouse fishing script (fishing_script)
    InvestigationFeatures
        m_Investigations; // heard footsteps investigated, backup guards (footsteps_investigate, backup_guard)
    uint32_t m_BackupNoticeSeen =
        0; // render thread: the last backup notice shown (InvestigationFeatures::NoticeSerial)
    double m_BackupNoticeUntil = 0.0;     // render thread: ImGui time the notice goes away
    std::string_view m_BackupNoticeText;  // render thread: its text (a static string of fr::BackupNoticeText)
    PrestigeShuffle m_Prestige;           // prestige card shuffle (prestige_shuffle)
    SupplierWalk m_SupplierWalk {m_Repo}; // the supplier's options and selectors, for the steering
    StockSteer m_StockSteer;              // supplier steering and force pick (supplier_steer, supplier_force_pick)
    SensorDials m_Sensors;                // guard senses: the professional shared sensor's dials (sensor_dials)
    SickSpotSteer m_SickSpots;            // poisoned NPCs prefer vomit spots where 47 can kill them (sick_spot_kills)

    // Read on any thread by the pin hook.
    std::atomic<int> m_Stage {-1};          // last ESceneLoadingStage seen, -1 before any
    std::atomic<int64_t> m_LoadSceneNs {0}; // steady clock at the last LoadScene

    bool m_SawLoadingStage = false;
    int m_LoadScenesWithoutStage = 0;
    bool m_WarnedStageNotReached = false;

    std::mutex m_SnapshotMutex;
    OverlaySnapshot m_Snapshot;

    bool m_ShowWindow = false;  // render thread only
    bool m_SettingsTab = false; // render thread only: the Settings tab is shown, not Status
    bool m_ExtremeFirst = true; // render thread only: sort the dial rows wildest first
    TouchScroll m_Window;       // render thread only: the overlay window (drag to scroll)
    // Render thread only: a Deck tap reaches ImGui as two clicks.
    fr::Debounce m_MenuButtonDebounce {fr::kTapDebounceSeconds};
    fr::Debounce m_ExtremeFirstDebounce {fr::kTapDebounceSeconds};
    fr::Debounce m_RearmDebounce {fr::kTapDebounceSeconds}; // Settings: "Try Landslide again" (a full variant's rearm)

    // Held around every SDK settings call, and across a whole reload.
    std::recursive_mutex m_SettingsMutex;
    SettingsMenu m_SettingsMenu;
    bool m_MercesConfigured = false; // m_Merces.Configure ran once: a reload configures it again, even off
    bool m_HookedInputPin = false;   // EnsureDetours (game thread)
    bool m_HookedActivate = false;
};

DECLARE_ZHM_PLUGIN(FreelancerRandomizer)
