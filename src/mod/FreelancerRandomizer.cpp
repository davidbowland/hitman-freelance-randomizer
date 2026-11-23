#include "FreelancerRandomizer.h"

#include "DialRows.h"
#include "Dials.h"
#include "Guard.h"
#include "OverlayStyle.h"
#include "LoadFaultWatch.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "ParamNames.h"
#include "Scene.h"
#include "SceneOf.h"
#include "SceneStage.h"
#include "TextFit.h"
#include "WrappedText.h"

#include <Hooks.h>
#include <IconsMaterialDesign.h>
#include <ModSDKVersion.h>

#include <Glacier/ZEvergreenCampaignManager.h>
#include <Glacier/ZGameLoopManager.h>

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <exception>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

namespace {

constexpr const char* kVersion = FR_VERSION;
constexpr std::string_view kSettings = "settings";
constexpr std::string_view kCampaign = "campaign";
constexpr std::string_view kMerces = "merces";
// The SDK names a mod's settings file after its mods.ini section, which
// mINI reads lowercased (ModLoader::LoadMod, ModSettings::Reload).
constexpr const char* kSettingsFile = "Retail/mods/freelancerrandomizer.ini";
constexpr double kSlowWalkMs = 250.0; // a walk slower than this is logged as a warning
constexpr const char* kStageNotReached =
    "The mod was enabled after the game started, so it can't change missions yet. Restart the game to use it.";

using fr::ParamLabel;

// Dial row colors (see DialRowColor); normal rows keep the default text color.
// Amber (kNotableColor): notable, or extreme with no direction.
constexpr ImVec4 kExtremeHarderColor(1.00f, 0.35f, 0.35f, 1.00f); // red
constexpr ImVec4 kExtremeEasierColor(0.35f, 0.85f, 1.00f, 1.00f); // cyan
// Widest Watch cell; the column is fixed to it.
constexpr const char* kWidestWatch = ICON_MD_WARNING " !! harder";
// The Watch cell of a row whose roll most likely changes nothing (fr::ViewDialRow,
// or a sensor dial not written or not held).
constexpr const char* kNoEffect = "(likely none)";
// The icon marks the column's cells as tappable.
constexpr const char* kDialHeader = "AI value " ICON_MD_INFO_OUTLINE;

// The text color for a dial row, or nullptr for the default.
const ImVec4* DialRowColor(const fr::DialRating& p_Rating) {
    if (p_Rating.level == fr::DialLevel::Normal)
        return nullptr;

    if (p_Rating.level == fr::DialLevel::Extreme) {
        if (p_Rating.side == fr::DialSide::Harder)
            return &kExtremeHarderColor;

        if (p_Rating.side == fr::DialSide::Easier)
            return &kExtremeEasierColor;
    }

    return &kNotableColor;
}

// The Watch header's tooltip: what the markers and colors mean, with this config's thresholds.
void DrawDialLegend(const fr::DialBands& p_Bands) {
    ImGui::BeginTooltip();
    ImGui::Text("!! Extreme: x%.2f or more, or x%.2f or less", p_Bands.extremeHigh, p_Bands.extremeLow);
    ImGui::Text("!  Notable: x%.2f or more, or x%.2f or less", p_Bands.notableHigh, p_Bands.notableLow);
    ImGui::TextColored(kExtremeHarderColor, ICON_MD_WARNING " Red: extreme, harder for you");
    ImGui::TextColored(kExtremeEasierColor, ICON_MD_WARNING " Cyan: extreme, easier for you");
    ImGui::TextColored(kNotableColor, "Amber: notable, or extreme with no clear direction");
    ImGui::TextUnformatted("Notable: a quarter of the way to the lowest or highest multiplier.");
    ImGui::TextUnformatted("Extreme: three quarters of the way.");
    ImGui::TextUnformatted("(likely none): this change probably has no effect in Freelancer.");
    ImGui::EndTooltip();
}

// The Dial cell's tooltip: what the parameter does, its unit and how sure that is.
template <class Row>
void DrawDialAbout(const Row& p_Row) {
    ImGui::BeginTooltip();
    // Wrapped at spaces only (WrappedText), never inside a word.
    const float s_Width = WrapWidthTo(ImGui::GetFontSize() * kTooltipWrapEms);
    WrappedText(p_Row.name, s_Width);
    ImGui::Separator();

    if (const fr::DialInfo* s_Info = p_Row.info) {
        const std::string_view s_Unit = fr::UnitLabel(s_Info->unit);
        const std::string_view s_UnitBasis = fr::BasisLabel(s_Info->unitBasis);
        WrappedText(s_Info->description, s_Width);
        const std::string_view s_UnitName = s_Unit.empty() ? std::string_view("count") : s_Unit;
        WrappedText(fmt::format("Unit: {} ({})", s_UnitName, s_UnitBasis), s_Width);

        if (s_Info->basis == fr::Basis::Documented) {
            WrappedText(fmt::format("Source: {}", s_Info->source), s_Width);
        }
        else {
            WrappedText("What this value does is worked out from its name and its values at each difficulty.", s_Width);

            if (!s_Info->source.empty())
                WrappedText(fmt::format("Unit source: {}", s_Info->source), s_Width);
        }
    }

    WrappedText(fmt::format("Harder when: {}", fr::HarderWhen(p_Row.dir)), s_Width);

    switch (p_Row.freelancer) {
        case fr::FreelancerValue::Written: break;
        case fr::FreelancerValue::Ambiguous:
            WrappedText("This value has several Professional-difficulty copies and it isn't clear which one "
                        "Freelancer uses; tap the value to see each.",
                        s_Width);
            break;
        case fr::FreelancerValue::Uncollected:
            WrappedText("This value has no Professional-difficulty setting, so the mod changed the copy in the "
                        "level instead. In tests the game did not use that copy, so this likely has no effect in "
                        "Freelancer.",
                        s_Width);
            break;
        case fr::FreelancerValue::Zero:
            WrappedText("Its Professional-difficulty value (the one Freelancer uses) is 0, and multiplying 0 "
                        "changes nothing, so this has no effect in Freelancer.",
                        s_Width);
            break;
        case fr::FreelancerValue::TierUnknown:
            WrappedText("The changed value's difficulty is unknown, so it may not be the one Freelancer uses.",
                        s_Width);
            break;
        case fr::FreelancerValue::Missing:
            WrappedText("Freelancer uses Professional-difficulty values, this one has none, and the mod found no "
                        "other copy to change, so the game most likely keeps its built-in value and this has no "
                        "effect.",
                        s_Width);
            break;
    }

    WrappedTextDisabled(p_Row.paramName, s_Width);
    ImGui::EndTooltip();
}

// The Freelancer value cell's tooltip: every written entry.
void DrawDialDetails(const std::vector<std::string>& p_Details) {
    ImGui::BeginTooltip();
    const float s_Width = WrapWidthTo(ImGui::GetFontSize() * kTooltipWrapEms);
    WrappedText("For troubleshooting: every copy the mod changed (difficulty: copy, game value -> new value, "
                "multiplier, check):",
                s_Width);

    for (const std::string& s_Line : p_Details)
        WrappedText(s_Line, s_Width);

    ImGui::EndTooltip();
}

// Successful and failed writes, in that order.
template <class A>
std::pair<size_t, size_t> CountWrites(const std::vector<A>& p_Applied) {
    size_t s_Written = 0;

    for (const auto& s_Applied : p_Applied)
        s_Written += s_Applied.writeOk ? 1 : 0;

    return {s_Written, p_Applied.size() - s_Written};
}

// The text without surrounding ASCII whitespace.
std::string_view Trim(std::string_view p_Text) {
    constexpr std::string_view s_Space = " \t\r\n\f\v";
    const size_t s_First = p_Text.find_first_not_of(s_Space);

    if (s_First == std::string_view::npos)
        return {};

    return p_Text.substr(s_First, p_Text.find_last_not_of(s_Space) - s_First + 1);
}

template <class T>
std::string OptionalText(const std::optional<T>& p_Value) {
    return p_Value ? fmt::format("{}", *p_Value) : std::string("none");
}

// One tab of the overlay's tab strip, colored like an ImGui tab; true on the
// frame it is tapped. Not BeginTabBar: a tab hit-tests against last frame's
// hover, so a touchscreen tap on another tab misses. A plain button fires on
// the press (selecting twice is harmless, so the Deck's doubled click needs no
// debounce).
bool OverlayTab(const char* p_Label, bool p_Selected) {
    const ImVec4* s_Colors = ImGui::GetStyle().Colors;
    ImGui::PushStyleColor(ImGuiCol_Button, s_Colors[p_Selected ? ImGuiCol_TabSelected : ImGuiCol_Tab]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, s_Colors[ImGuiCol_TabHovered]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, s_Colors[ImGuiCol_TabSelected]);
    ImGui::Button(p_Label);
    ImGui::PopStyleColor(3);

    return ImGui::IsItemActivated();
}

// A section heading of the Status tab, as the Settings tab's: never folded, a divider with room above it.
void StatusHeading(const char* p_Name) {
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::SeparatorText(p_Name);
}

// A plain note in place of a section's values (none yet).
void StatusNone(const char* p_Text) {
    WrappedTextAvail(p_Text, &ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
}

// The map variant options that can't come up this session, and why (a full version of theirs can't be picked).
SettingsMenu::UnavailableOptions UnavailableVariantOptions(const LandslidePackage& p_Package) {
    SettingsMenu::UnavailableOptions s_Out;

    for (const fr::VariantOption* s_Option : fr::AllVariantOptions()) {
        const fr::FullAvailability s_Why = fr::FullOptionAvailability(
            s_Option->name, [&](const fr::FullVariant& p_Full) { return p_Package.Availability(p_Full); });

        if (s_Why != fr::FullAvailability::Available)
            s_Out.emplace(s_Option->name, s_Why);
    }

    return s_Out;
}

} // namespace

FreelancerRandomizer::~FreelancerRandomizer() {
    load_fault::Uninstall();

    if (m_FrameUpdateRegistered) {
        const ZMemberDelegate<FreelancerRandomizer, void(const SGameUpdateEvent&)> s_Delegate(
            this, &FreelancerRandomizer::OnFrameUpdate);
        Globals::GameLoopManager->UnregisterFrameUpdate(s_Delegate, 1, EUpdateMode::eUpdatePlayMode);
    }

    // Unloaded at runtime (from the SDK's mod list): put back every value
    // still patched (the weapon rarity shuffle's pro.repo values first).
    m_Mace.Restore("mod unload");
    m_WeaponRarity.Restore("mod unload");
    m_LegalSniper.Restore("mod unload");
    // The laptop's card weights, forced until the safehouse scene clears: unloaded during a safehouse visit,
    // the cards would stay forced until the game restarts.
    m_Prestige.Restore("mod unload");

    // The sensor dials (their entity is still alive while a scene is loaded).
    m_Sensors.Restore("mod unload");
    // The supplier's steered weights.
    m_StockSteer.Restore("mod unload");

    // Every other value still patched. Restoring pro.repo needs no bricks; entity restores need
    // the current scene's bricks to find the entities again.
    try {
        if (!m_EntityPatches.Empty() || !m_RepoPatches.Empty()) {
            ZEntitySceneContext* s_Context = CurrentSceneContext();
            RestorePatches(s_Context ? &s_Context->m_aLoadedBricks : nullptr, "mod unload");
            Log("mod unload: {} entity values and {} pro.repo values still patched after the restore",
                m_EntityPatches.Size(), m_RepoPatches.Size());
        }
    }
    catch (const std::exception& e) {
        LogError("mod unload: restore failed: {}", e.what());
    }
    catch (...) {
        LogError("mod unload: restore failed: unknown exception");
    }
}

void FreelancerRandomizer::Init() {
    InitModLog("FreelancerRandomizer");

    m_SettingsMenu.SetWriter(
        [this](const fr::SettingInfo& p_Info, const std::string& p_Text) { WriteMenuSetting(p_Info, p_Text); });

    try {
        m_Config.enabled = LoadBool("enabled", true);

        if (!m_Config.enabled) {
            // Full map variants off still removes the mod's packages: the way to uninstall them, with or without
            // the rest of the mod on.
            m_Config.fullMapVariants = LoadBool("full_map_variants", true);

            if (!m_Config.fullMapVariants) {
                m_LandslidePackage.Install(kVersion, false);
                m_SettingsMenu.SetUnavailable(UnavailableVariantOptions(m_LandslidePackage));
            }

            // The Settings tab still shows (and saves) every setting, for the next game start.
            PublishSettings();
            Log("FreelancerRandomizer {} loaded (SDK {}): disabled; no hooks added (to enable, set enabled = true "
                "under [settings] in {} and restart the game)",
                kVersion, ZHMMODSDK_VER, kSettingsFile);
            UpdateSnapshot([](OverlaySnapshot& p_Snapshot) { p_Snapshot.enabled = false; });
            return;
        }

        Log("FreelancerRandomizer {} loaded (SDK {})", kVersion, ZHMMODSDK_VER);
        LoadConfig();
        // Before anything loads: a fault file the last session left goes into this log. Only a full variant's
        // load is watched, so with Full map variants off no exception handler is added.
        load_fault::Install(LandslidePackage::BuiltIn() && m_Config.fullMapVariants);
        PublishSettings();
        ResumeClientReqs();

        // The full variants' packages: built from the game's files and written (or, with Full map variants off,
        // removed) before the game creates its partitions; the partition detour only when one is installed. What
        // can't come up is fixed for the session from here on.
        m_Variants.SetLandslidePackage(&m_LandslidePackage);
        m_Variants.SetRepository(&m_Repo);
        m_LandslidePackage.Install(kVersion, m_Config.fullMapVariants);
        m_SettingsMenu.SetUnavailable(UnavailableVariantOptions(m_LandslidePackage));

        if (m_LandslidePackage.Installed()) {
            Hooks::IPackageManager_SPartitionInfo_SPartitionInfo->AddDetour(
                this, &FreelancerRandomizer::IPackageManager_SPartitionInfo_SPartitionInfo);
            Log("detour added (the full map variants' packages): IPackageManager_SPartitionInfo_SPartitionInfo");
        }

        Hooks::ZEvergreenCampaignManager_OnGenerate->AddDetour(
            this, &FreelancerRandomizer::ZEvergreenCampaignManager_OnGenerate);
        Hooks::ZEntitySceneContext_LoadScene->AddDetour(this, &FreelancerRandomizer::ZEntitySceneContext_LoadScene);
        Hooks::ZEntitySceneContext_SetLoadingStage->AddDetour(
            this, &FreelancerRandomizer::ZEntitySceneContext_SetLoadingStage);
        Hooks::ZEntitySceneContext_ClearScene->AddDetour(this, &FreelancerRandomizer::ZEntitySceneContext_ClearScene);
        Hooks::SignalOutputPin->AddDetour(this, &FreelancerRandomizer::SignalOutputPin);
        Hooks::ZTemplateEntityBlueprintFactory_ZTemplateEntityBlueprintFactory->AddDetour(
            this, &FreelancerRandomizer::ZTemplateEntityBlueprintFactory_ZTemplateEntityBlueprintFactory);
        Hooks::ZActorInventoryHandler_StartItemStreamIn->AddDetour(
            this, &FreelancerRandomizer::ZActorInventoryHandler_StartItemStreamIn);
        Hooks::ZCharacterSubcontrollerInventory_CreateItem->AddDetour(
            this, &FreelancerRandomizer::ZCharacterSubcontrollerInventory_CreateItem);

        m_Loadout.LoadState();
        // The plan's client requirements go highest payout first: what the payout table shows at scene start.
        m_Loadout.SetClientReqPayouts([this] { return m_Merces.ClientReqPayouts(); });
        Hooks::ZAchievementManagerSimple_OnEventSent->AddDetour(
            this, &FreelancerRandomizer::ZAchievementManagerSimple_OnEventSent);

        EnsureDetours();

        Log("detours added: ZEvergreenCampaignManager_OnGenerate, ZEntitySceneContext_LoadScene, "
            "ZEntitySceneContext_SetLoadingStage, ZEntitySceneContext_ClearScene, SignalOutputPin, "
            "ZTemplateEntityBlueprintFactory_ZTemplateEntityBlueprintFactory (map variants: Diana's briefing), "
            "ZActorInventoryHandler_StartItemStreamIn, ZCharacterSubcontrollerInventory_CreateItem (the random "
            "loadout), "
            "ZAchievementManagerSimple_OnEventSent");
        Log("note: this mod must be enabled in mods.ini before the game starts; enabled later from the in-game "
            "mod list, its loading-stage hook never runs");
        UpdateSnapshot([](OverlaySnapshot& p_Snapshot) { p_Snapshot.status = "Waiting for a level to load."; });
    }
    catch (const std::exception& e) {
        LogError("Init failed: {}", e.what());
    }
    catch (...) {
        LogError("Init failed: unknown exception");
    }
}

void FreelancerRandomizer::OnEngineInitialized() {
    if (!m_Config.enabled)
        return;

    const ZMemberDelegate<FreelancerRandomizer, void(const SGameUpdateEvent&)> s_Delegate(
        this, &FreelancerRandomizer::OnFrameUpdate);
    Globals::GameLoopManager->RegisterFrameUpdate(s_Delegate, 1, EUpdateMode::eUpdatePlayMode);
    m_FrameUpdateRegistered = true;
}

// The SDK keeps plugin settings in Retail/mods/freelancerrandomizer.ini.
// Absent keys are written back with their defaults so the player can find them.
void FreelancerRandomizer::LoadConfig() {
    m_Config.dials = LoadBool("dials", true);
    m_Config.guardWeapons = LoadBool("guard_weapons", true);
    m_Config.variants = LoadBool("variants", true);
    m_Config.fullMapVariants = LoadBool("full_map_variants", true);

    // One chance per option (literal keys: the settings table test reads them
    // from this file).
    const std::pair<const fr::VariantOption*, int32_t> s_Chances[] = {
        {&fr::kTheSource, LoadInt("variant_chance_the_source", fr::kTheSource.defaultChance)},
        {&fr::kPatientZero, LoadInt("variant_chance_patient_zero", fr::kPatientZero.defaultChance)},
        {&fr::kSnowFestival, LoadInt("variant_chance_snow_festival", fr::kSnowFestival.defaultChance)},
        {&fr::kReflection, LoadInt("variant_chance_reflection", fr::kReflection.defaultChance)},
        {&fr::kTodEbolaNight, LoadInt("variant_chance_tod_ebola_night", fr::kTodEbolaNight.defaultChance)},
        {&fr::kLandslide, LoadInt("variant_chance_landslide", fr::kLandslide.defaultChance)},
        {&fr::kLightingCopperhead,
         LoadInt("variant_chance_lighting_copperhead", fr::kLightingCopperhead.defaultChance)},
        {&fr::kHolidayHoarders, LoadInt("variant_chance_holiday_hoarders", fr::kHolidayHoarders.defaultChance)},
        {&fr::kHerbalistParty, LoadInt("variant_chance_herbalist_party", fr::kHerbalistParty.defaultChance)},
        {&fr::kTheRage, LoadInt("variant_chance_the_rage", fr::kTheRage.defaultChance)},
        {&fr::kHarbinger, LoadInt("variant_chance_harbinger", fr::kHarbinger.defaultChance)},
        {&fr::kProcurers, LoadInt("variant_chance_procurers", fr::kProcurers.defaultChance)},
        {&fr::kCollector, LoadInt("variant_chance_collector", fr::kCollector.defaultChance)},
        {&fr::kTheDrop, LoadInt("variant_chance_the_drop", fr::kTheDrop.defaultChance)},
        {&fr::kLiability, LoadInt("variant_chance_liability", fr::kLiability.defaultChance)},
        {&fr::kSerialKiller, LoadInt("variant_chance_serial_killer", fr::kSerialKiller.defaultChance)},
        {&fr::kHeartbreaker, LoadInt("variant_chance_heartbreaker", fr::kHeartbreaker.defaultChance)},
        {&fr::kIconoclast, LoadInt("variant_chance_iconoclast", fr::kIconoclast.defaultChance)},
        {&fr::kAppraiser, LoadInt("variant_chance_appraiser", fr::kAppraiser.defaultChance)},
        {&fr::kStowaway, LoadInt("variant_chance_stowaway", fr::kStowaway.defaultChance)},
        {&fr::kRevolutionary, LoadInt("variant_chance_revolutionary", fr::kRevolutionary.defaultChance)},
        {&fr::kGardenShow, LoadInt("variant_chance_garden_show", fr::kGardenShow.defaultChance)},
        {&fr::kTheForger, LoadInt("variant_chance_the_forger", fr::kTheForger.defaultChance)},
        {&fr::kTheSensation, LoadInt("variant_chance_the_sensation", fr::kTheSensation.defaultChance)},
        {&fr::kTheBlackHat, LoadInt("variant_chance_the_black_hat", fr::kTheBlackHat.defaultChance)},
        {&fr::kThePharmacist, LoadInt("variant_chance_the_pharmacist", fr::kThePharmacist.defaultChance)},
        {&fr::kThePaparazzo, LoadInt("variant_chance_the_paparazzo", fr::kThePaparazzo.defaultChance)},
        {&fr::kTheChef, LoadInt("variant_chance_the_chef", fr::kTheChef.defaultChance)},
        {&fr::kTheBroker, LoadInt("variant_chance_the_broker", fr::kTheBroker.defaultChance)},
        {&fr::kTheIdentityThief, LoadInt("variant_chance_the_identity_thief", fr::kTheIdentityThief.defaultChance)},
        {&fr::kTheBlackmailer, LoadInt("variant_chance_the_blackmailer", fr::kTheBlackmailer.defaultChance)},
        {&fr::kTheDirector, LoadInt("variant_chance_the_director", fr::kTheDirector.defaultChance)},
        {&fr::kTheCongressman, LoadInt("variant_chance_the_congressman", fr::kTheCongressman.defaultChance)},
        {&fr::kTheGuru, LoadInt("variant_chance_the_guru", fr::kTheGuru.defaultChance)},
        {&fr::kThePrince, LoadInt("variant_chance_the_prince", fr::kThePrince.defaultChance)},
        {&fr::kTheTwin, LoadInt("variant_chance_the_twin", fr::kTheTwin.defaultChance)},
        {&fr::kTheWildcard, LoadInt("variant_chance_the_wildcard", fr::kTheWildcard.defaultChance)},
        {&fr::kTheDeceivers, LoadInt("variant_chance_the_deceivers", fr::kTheDeceivers.defaultChance)},
        {&fr::kTheEnforcer, LoadInt("variant_chance_the_enforcer", fr::kTheEnforcer.defaultChance)},
        {&fr::kTheBadBoy, LoadInt("variant_chance_the_bad_boy", fr::kTheBadBoy.defaultChance)},
        {&fr::kTheFixer, LoadInt("variant_chance_the_fixer", fr::kTheFixer.defaultChance)},
        {&fr::kTheGunrunner, LoadInt("variant_chance_the_gunrunner", fr::kTheGunrunner.defaultChance)},
        {&fr::kTheAngelOfDeath, LoadInt("variant_chance_the_angel_of_death", fr::kTheAngelOfDeath.defaultChance)},
        {&fr::kTheExtractor, LoadInt("variant_chance_the_extractor", fr::kTheExtractor.defaultChance)},
        {&fr::kTheEntertainer, LoadInt("variant_chance_the_entertainer", fr::kTheEntertainer.defaultChance)},
        {&fr::kTheWarlord, LoadInt("variant_chance_the_warlord", fr::kTheWarlord.defaultChance)},
        {&fr::kTheFoodCritic, LoadInt("variant_chance_the_food_critic", fr::kTheFoodCritic.defaultChance)},
        {&fr::kTheExDictator, LoadInt("variant_chance_the_ex_dictator", fr::kTheExDictator.defaultChance)},
        {&fr::kTheVeteran, LoadInt("variant_chance_the_veteran", fr::kTheVeteran.defaultChance)},
        {&fr::kTheBookkeeper, LoadInt("variant_chance_the_bookkeeper", fr::kTheBookkeeper.defaultChance)},
        {&fr::kTheChameleon, LoadInt("variant_chance_the_chameleon", fr::kTheChameleon.defaultChance)},
        {&fr::kTheMercenary, LoadInt("variant_chance_the_mercenary", fr::kTheMercenary.defaultChance)},
        {&fr::kTheSurgeons, LoadInt("variant_chance_the_surgeons", fr::kTheSurgeons.defaultChance)},
        {&fr::kTheFugitive, LoadInt("variant_chance_the_fugitive", fr::kTheFugitive.defaultChance)},
        {&fr::kTheController, LoadInt("variant_chance_the_controller", fr::kTheController.defaultChance)},
        {&fr::kTheAscensionist, LoadInt("variant_chance_the_ascensionist", fr::kTheAscensionist.defaultChance)},
        {&fr::kTheUndying, LoadInt("variant_chance_the_undying", fr::kTheUndying.defaultChance)},
        {&fr::kTheUndyingReturns, LoadInt("variant_chance_the_undying_returns", fr::kTheUndyingReturns.defaultChance)},
        {&fr::kTheBanker, LoadInt("variant_chance_the_banker", fr::kTheBanker.defaultChance)},
        {&fr::kTheInfiltrator, LoadInt("variant_chance_the_infiltrator", fr::kTheInfiltrator.defaultChance)},
        {&fr::kTheWizard, LoadInt("variant_chance_the_wizard", fr::kTheWizard.defaultChance)},
        {&fr::kTheSplitter, LoadInt("variant_chance_the_splitter", fr::kTheSplitter.defaultChance)},
        {&fr::kIllusionsOfGrandeur,
         LoadInt("variant_chance_illusions_of_grandeur", fr::kIllusionsOfGrandeur.defaultChance)},
        {&fr::kABitterPill, LoadInt("variant_chance_a_bitter_pill", fr::kABitterPill.defaultChance)},
        {&fr::kEmbraceOfTheSerpent,
         LoadInt("variant_chance_embrace_of_the_serpent", fr::kEmbraceOfTheSerpent.defaultChance)},
        {&fr::kASilverTongue, LoadInt("variant_chance_a_silver_tongue", fr::kASilverTongue.defaultChance)},
        {&fr::kGluttony, LoadInt("variant_chance_gluttony", fr::kGluttony.defaultChance)},
        {&fr::kEnvy, LoadInt("variant_chance_envy", fr::kEnvy.defaultChance)},
        {&fr::kLust, LoadInt("variant_chance_lust", fr::kLust.defaultChance)},
        {&fr::kSloth, LoadInt("variant_chance_sloth", fr::kSloth.defaultChance)},
    };
    static_assert(std::size(s_Chances) == fr::kVariantOptionCount);
    std::string s_ChanceText;

    for (const auto& [s_Option, s_Percent] : s_Chances) {
        const int32_t s_Clamped = std::clamp(s_Percent, int32_t {0}, int32_t {100});

        if (s_Clamped != s_Percent)
            Log("settings: {} = {} is outside 0..100; using {}", fr::VariantChanceKey(*s_Option), s_Percent, s_Clamped);

        fr::SetChance(m_Config.variantChances, *s_Option, s_Clamped);
        s_ChanceText += fmt::format("{}{} {}%", s_ChanceText.empty() ? "" : ", ", s_Option->name, s_Clamped);
    }

    m_Config.logSafeSpots = LoadBool("log_safe_spots", true);
    m_SafeSpots.Configure(m_Config.logSafeSpots);
    Log("config: log_safe_spots {}", m_Config.logSafeSpots);
    m_Config.lootAmounts = LoadBool("loot_amounts", true);
    LoadLootBounds(fr::MoneyKind::Vault, LoadInt("loot_amount_vault_min", 4000),
                   LoadInt("loot_amount_vault_max", 25000));
    LoadLootBounds(fr::MoneyKind::Safe, LoadInt("loot_amount_safe_min", 1500), LoadInt("loot_amount_safe_max", 10000));
    LoadLootBounds(fr::MoneyKind::Crate, LoadInt("loot_amount_crate_min", 250), LoadInt("loot_amount_crate_max", 5000));
    LoadLootBounds(fr::MoneyKind::Mule, LoadInt("loot_amount_mule_min", 250), LoadInt("loot_amount_mule_max", 3500));
    m_MoneyItems.Configure(m_Config.lootAmounts, m_Config.lootBounds);
    Log("config: loot_amounts {} (vault {}-{}, safe {}-{}, crate {}-{}, mule {}-{})", m_Config.lootAmounts,
        m_Config.lootBounds[static_cast<size_t>(fr::MoneyKind::Vault)].min,
        m_Config.lootBounds[static_cast<size_t>(fr::MoneyKind::Vault)].max,
        m_Config.lootBounds[static_cast<size_t>(fr::MoneyKind::Safe)].min,
        m_Config.lootBounds[static_cast<size_t>(fr::MoneyKind::Safe)].max,
        m_Config.lootBounds[static_cast<size_t>(fr::MoneyKind::Crate)].min,
        m_Config.lootBounds[static_cast<size_t>(fr::MoneyKind::Crate)].max,
        m_Config.lootBounds[static_cast<size_t>(fr::MoneyKind::Mule)].min,
        m_Config.lootBounds[static_cast<size_t>(fr::MoneyKind::Mule)].max);
    m_Config.crateGuns.on = LoadBool("crate_guns", true);
    m_Config.crateGuns.poolChance = LoadPercent("crate_gun_chance", fr::CrateGunsConfig {}.poolChance);
    m_Config.crateGuns.gunShare = LoadPercent("crate_guns_share", fr::CrateGunsConfig {}.gunShare);
    m_Config.crateGuns.rarityMult[0] = LoadDouble("crate_guns_mult_common", fr::CrateGunsConfig {}.rarityMult[0]);
    m_Config.crateGuns.rarityMult[1] = LoadDouble("crate_guns_mult_rare", fr::CrateGunsConfig {}.rarityMult[1]);
    m_Config.crateGuns.rarityMult[2] = LoadDouble("crate_guns_mult_epic", fr::CrateGunsConfig {}.rarityMult[2]);
    m_Config.crateGuns.rarityMult[3] = LoadDouble("crate_guns_mult_legendary", fr::CrateGunsConfig {}.rarityMult[3]);
    m_Config.crateItems.on = LoadBool("crate_items", true);
    m_Config.crateItems.toolsChance = LoadPercent("crate_all_tools_chance", fr::CrateItemsConfig {}.toolsChance);
    m_Config.crateItems.mercesChance = LoadPercent("crate_merces_chance", fr::CrateItemsConfig {}.mercesChance);
    m_Config.crateItems.unloaded = LoadBool("crate_items_unloaded", fr::CrateItemsConfig {}.unloaded);
    m_Config.crateItems.evenBoxes = LoadBool("crate_even_boxes", fr::CrateItemsConfig {}.evenBoxes);
    m_Config.crateItems.levelItems = LoadBool("crate_level_items", fr::CrateItemsConfig {}.levelItems);
    m_CrateGuns.Configure(m_Config.crateItems, m_Config.crateGuns, [this] { return m_Loadout.OwnedItemIds(); });
    Log("config: crate_items {} (unloaded {}, even boxes {}, level items {}), crate_guns {}", m_Config.crateItems.on,
        m_Config.crateItems.unloaded, m_Config.crateItems.evenBoxes, m_Config.crateItems.levelItems,
        m_Config.crateGuns.on);
    m_Config.supplierSteer = LoadBool("supplier_steer", true);
    m_Config.supplierSteerGear = LoadBool("supplier_steer_gear", true);
    m_Config.supplierForcePick = LoadBool("supplier_force_pick", true);
    m_StockSteer.Configure(m_Config.supplierSteer, m_Config.supplierSteerGear, m_Config.supplierForcePick);
    // The steering and the force pick work from the supplier walk: either setting runs it in missions.
    m_SupplierWalk.SetSteer(&m_StockSteer);
    m_SupplierWalk.Configure(m_Config.supplierSteer || m_Config.supplierForcePick);
    Log("config: supplier_steer {}, supplier_steer_gear {}, supplier_force_pick {}", m_Config.supplierSteer,
        m_Config.supplierSteerGear, m_Config.supplierForcePick);
    m_Config.targetPool = LoadBool("target_pool", true);
    m_Config.targetPoolSwapMin = LoadPercent("target_pool_swap_min", fr::kDefaultTargetPoolSwapMin);
    m_Config.targetPoolSwapMax = LoadPercent("target_pool_swap_max", fr::kDefaultTargetPoolSwapMax);
    m_Config.targetPoolAnyGuard = LoadBool("target_pool_any_guard", fr::kDefaultTargetPoolAnyGuard);
    m_TargetPool.Configure(m_Config.targetPool, m_Config.targetPoolSwapMin, m_Config.targetPoolSwapMax,
                           m_Config.targetPoolAnyGuard);
    Log("config: target_pool {}, target_pool_swap_min {}, target_pool_swap_max {}, target_pool_any_guard {}",
        m_Config.targetPool, fr::PercentText(m_Config.targetPoolSwapMin), fr::PercentText(m_Config.targetPoolSwapMax),
        m_Config.targetPoolAnyGuard);
    m_Config.enforcers = LoadBool("enforcers", true);
    m_Config.enforcersShareMin = LoadPercent("enforcers_share_min", fr::kDefaultEnforcerShareMin);
    m_Config.enforcersShareMax = LoadPercent("enforcers_share_max", fr::kDefaultEnforcerShareMax);
    m_Config.enforcersAnyBrick = LoadBool("enforcers_any_brick", fr::kDefaultEnforcerAnyBrick);
    m_Config.enforcersIncludeTargets = LoadBool("enforcers_include_targets", fr::kDefaultEnforcerIncludeTargets);
    m_Config.enforcersIncludeLate = LoadBool("enforcers_include_late", fr::kDefaultEnforcerIncludeLate);
    m_Config.enforcersIncludeSentries = LoadBool("enforcers_include_sentries", fr::kDefaultEnforcerIncludeSentries);
    m_Enforcers.Configure(
        m_Config.enforcers, m_Config.enforcersShareMin, m_Config.enforcersShareMax, m_Config.enforcersAnyBrick,
        {m_Config.enforcersIncludeTargets, m_Config.enforcersIncludeLate, m_Config.enforcersIncludeSentries});
    Log("config: enforcers {}, enforcers_share_min {}, enforcers_share_max {}, enforcers_any_brick {}, "
        "enforcers_include_targets {}, enforcers_include_late {}, enforcers_include_sentries {}",
        m_Config.enforcers, fr::PercentText(m_Config.enforcersShareMin), fr::PercentText(m_Config.enforcersShareMax),
        m_Config.enforcersAnyBrick, m_Config.enforcersIncludeTargets, m_Config.enforcersIncludeLate,
        m_Config.enforcersIncludeSentries);
    LoadDoorLocksConfig();

    LoadClientReqsConfig();
    LoadWeaponRarityConfig();
    LoadRandomLoadoutConfig();
    LoadSafehouseCoinConfig();
    LoadFishingConfig();
    LoadInvestigationsConfig();
    LoadPrestigeShuffleConfig();
    LoadPayoutConfig();
    LoadSensesConfig();

    const fr::RollConfig s_Defaults = fr::Config {}.roll;
    const fr::RollConfig s_Loaded {
        LoadDouble("dials_sigma", s_Defaults.sigma),
        LoadDouble("dials_lean", s_Defaults.lean),
        LoadDouble("dials_min", s_Defaults.clampMin),
        LoadDouble("dials_max", s_Defaults.clampMax),
    };
    m_Config.roll = fr::SanitizeRollConfig(s_Loaded, s_Defaults);

    // LoadDouble already replaced NaN, so plain comparison detects every reset.
    if (s_Loaded.sigma != m_Config.roll.sigma || s_Loaded.lean != m_Config.roll.lean ||
        s_Loaded.clampMin != m_Config.roll.clampMin || s_Loaded.clampMax != m_Config.roll.clampMax) {
        Log("settings: invalid AI difficulty roll values reset to defaults (loaded dials_sigma {} dials_lean {} "
            "dials_min {} dials_max {})",
            s_Loaded.sigma, s_Loaded.lean, s_Loaded.clampMin, s_Loaded.clampMax);
    }

    if (!HasSetting(kSettings, "dials_off"))
        SetSetting(kSettings, "dials_off", "");

    const ZString s_Off = GetSetting(kSettings, "dials_off", "");
    std::vector<std::string> s_Unknown;
    m_Config.off = fr::ParseOffList(s_Off.ToStringView(), s_Unknown);

    for (const auto& s_Name : s_Unknown)
        Log("settings: unknown parameter \"{}\" in dials_off; ignored", s_Name);

    std::string s_OffNames;

    for (const int32_t s_Param : m_Config.off)
        s_OffNames += fmt::format("{}{}", s_OffNames.empty() ? "" : ", ", fr::ParamName(s_Param));

    if (!HasSetting(kSettings, "variant_force"))
        SetSetting(kSettings, "variant_force", "");

    const ZString s_Force = GetSetting(kSettings, "variant_force", "");
    const auto s_ForceName = fr::ParseVariantForce(s_Force.ToStringView());
    m_Config.variantForce = s_ForceName.value_or("");

    if (!s_ForceName) {
        Log("settings: variant_force = \"{}\" is not one of {}; ignored", s_Force.ToStringView(),
            fr::VariantForceNames());
    }

    Log("config: enabled true, dials {}, guard_weapons {}, variants {}, full_map_variants {}, variant chances [{}], "
        "variant_force \"{}\", dials_sigma {}, dials_lean {}, dials_min {}, dials_max {}, dials_off [{}]",
        m_Config.dials, m_Config.guardWeapons, m_Config.variants, m_Config.fullMapVariants, s_ChanceText,
        m_Config.variantForce, m_Config.roll.sigma, m_Config.roll.lean, m_Config.roll.clampMin, m_Config.roll.clampMax,
        s_OffNames);

    // The log file's switches (log_safe_spots is read above with its feature).
    m_Config.logMissions = LoadBool("log_missions", true);
    m_Config.logLoadout = LoadBool("log_loadout", true);
    m_Config.logSupplierCrates = LoadBool("log_supplier_crates", true);
    m_Config.logCommonObjectives = LoadBool("log_common_objectives", true);
    m_Config.logPrestigeCards = LoadBool("log_prestige_cards", true);
    m_Config.logMapVariants = LoadBool("log_map_variants", true);
    m_Config.logTargets = LoadBool("log_targets", true);
    m_Config.logDoorLocks = LoadBool("log_door_locks", true);
    m_Config.logWeapons = LoadBool("log_weapons", true);
    m_Config.logMoney = LoadBool("log_money", true);
    m_Config.logAiDifficulty = LoadBool("log_ai_difficulty", true);
    m_Config.logSafehouse = LoadBool("log_safehouse", true);
    fr::ApplyLogTopics(m_Config);
    std::string s_LogsOff;

    for (const fr::LogTopic s_Topic : fr::AllLogTopics()) {
        if (!fr::LogTopicOn(s_Topic))
            s_LogsOff += fmt::format("{}{}", s_LogsOff.empty() ? "" : ", ", fr::LogTopicKey(s_Topic));
    }

    Log("config: log switches off: [{}]", s_LogsOff);
}

void FreelancerRandomizer::ReloadConfigIfChanged(const char* p_When) {
    if (!m_SettingsMenu.TakeChanged())
        return;

    try {
        // Held across the reload: a menu write waits, so it is either read
        // now or marks the next load.
        std::lock_guard s_Lock(m_SettingsMutex);
        Log("settings: changed in the overlay; reading {} again ({})", kSettingsFile, p_When);
        LoadConfig();
        EnsureDetours();
        PublishSettings();
    }
    catch (const std::exception& e) {
        LogError("settings: reload failed ({}): {}", p_When, e.what());
    }
    catch (...) {
        LogError("settings: reload failed ({}): unknown exception", p_When);
    }
}

void FreelancerRandomizer::EnsureDetours() {
    // loot_amounts writes each money item's amount at its input pins and as it activates; the
    // prestige shuffle draws at Picker1's Pick input and counts its selectors'
    // picks; the fishing script steers at the fishing selector's Pick input.
    // lockpick_grant and the item grants keep the given items out of the mission's saved inventory list.
    // The map variants mute Diana's lines of their own story at each line's Trigger input.
    const bool s_InputPin = m_Config.lootAmounts || m_Fishing.Wanted() || m_Prestige.On() ||
                            m_Config.lockpickGrant != "off" || m_Config.grantMakeshiftExplosive ||
                            m_Config.grantLethalPoison || m_Config.grantCoin || m_Config.variants;
    // crate_guns and the supplier steering watch offers: the activations under options.
    const bool s_Activate = m_Config.lootAmounts || m_CrateGuns.On() || m_SupplierWalk.On();

    if (s_InputPin && !m_HookedInputPin) {
        Hooks::SignalInputPin->AddDetour(this, &FreelancerRandomizer::SignalInputPin);
        m_HookedInputPin = true;
        Log("detour added for loot_amounts / fishing_script / prestige_shuffle / lockpick_grant / grant_* / variants: "
            "SignalInputPin");
    }

    if (s_Activate && !m_HookedActivate) {
        Hooks::ZEntityManager_ActivateEntity->AddDetour(this, &FreelancerRandomizer::ZEntityManager_ActivateEntity);
        m_HookedActivate = true;
        Log("detour added for loot_amounts / crate_guns / supplier_steer / supplier_force_pick: "
            "ZEntityManager_ActivateEntity");
    }

    // footsteps_investigate / backup_guard: the helper's input pins.
    if (m_Investigations.Wanted() && !m_HookedInputPin) {
        Hooks::SignalInputPin->AddDetour(this, &FreelancerRandomizer::SignalInputPin);
        m_HookedInputPin = true;
        Log("detour added for footsteps_investigate / backup_guard: SignalInputPin");
    }
}

void FreelancerRandomizer::PublishSettings() {
    std::lock_guard s_Lock(m_SettingsMutex);
    std::map<std::string, std::string> s_Values;

    for (const fr::SettingInfo& s_Info : fr::SettingsTable()) {
        s_Values[std::string(s_Info.key)] = HasSetting(kSettings, s_Info.key)
                                                ? std::string(GetSetting(kSettings, s_Info.key, "").ToStringView())
                                                : std::string(s_Info.def);
    }

    m_SettingsMenu.Loaded(std::move(s_Values));
}

void FreelancerRandomizer::WriteMenuSetting(const fr::SettingInfo& p_Info, const std::string& p_Text) {
    try {
        std::lock_guard s_Lock(m_SettingsMutex);
        SetSetting(kSettings, p_Info.key, p_Text);
        Log("settings: {} = \"{}\" (set in the overlay; used {})", p_Info.key, p_Text,
            fr::SettingAppliesText(m_Config.enabled ? p_Info.applies : fr::SettingApplies::Restart));
    }
    catch (const std::exception& e) {
        LogError("settings: writing {} failed: {}", p_Info.key, e.what());
    }
    catch (...) {
        LogError("settings: writing {} failed: unknown exception", p_Info.key);
    }
}

void FreelancerRandomizer::CheckTableDefault(std::string_view p_Key, std::string_view p_Default) {
    const fr::SettingInfo* s_Info = fr::FindSetting(p_Key);

    if (s_Info && !fr::SameSettingValue(*s_Info, p_Default, s_Info->def))
        Log("settings: {}: the loader's default {} is not the settings table's {} (fix src/core/SettingsTable.cpp)",
            p_Key, p_Default, s_Info->def);
}

void FreelancerRandomizer::SetSetting(const ZString& p_Section, const ZString& p_Name, const ZString& p_Value) {
    std::lock_guard s_Lock(m_SettingsMutex);
    IPluginInterface::SetSetting(p_Section, p_Name, p_Value);
}

void FreelancerRandomizer::SetSettingInt(const ZString& p_Section, const ZString& p_Name, int64 p_Value) {
    std::lock_guard s_Lock(m_SettingsMutex);
    IPluginInterface::SetSettingInt(p_Section, p_Name, p_Value);
}

void FreelancerRandomizer::SetSettingDouble(const ZString& p_Section, const ZString& p_Name, double p_Value) {
    std::lock_guard s_Lock(m_SettingsMutex);
    IPluginInterface::SetSettingDouble(p_Section, p_Name, p_Value);
}

void FreelancerRandomizer::SetSettingBool(const ZString& p_Section, const ZString& p_Name, bool p_Value) {
    std::lock_guard s_Lock(m_SettingsMutex);
    IPluginInterface::SetSettingBool(p_Section, p_Name, p_Value);
}

ZString FreelancerRandomizer::GetSetting(const ZString& p_Section, const ZString& p_Name, const ZString& p_Default) {
    std::lock_guard s_Lock(m_SettingsMutex);
    return IPluginInterface::GetSetting(p_Section, p_Name, p_Default);
}

bool FreelancerRandomizer::GetSettingBool(const ZString& p_Section, const ZString& p_Name, bool p_Default) {
    std::lock_guard s_Lock(m_SettingsMutex);
    return IPluginInterface::GetSettingBool(p_Section, p_Name, p_Default);
}

bool FreelancerRandomizer::HasSetting(const ZString& p_Section, const ZString& p_Name) {
    std::lock_guard s_Lock(m_SettingsMutex);
    return IPluginInterface::HasSetting(p_Section, p_Name);
}

// One kind's loot_amount_<kind>_min / _max into m_Config.lootBounds: rounded
// to the amount step, clamped to the settable range and swapped when
// min > max (logged).
void FreelancerRandomizer::LoadLootBounds(fr::MoneyKind p_Kind, int32_t p_Min, int32_t p_Max) {
    const fr::LootBoundsFix s_Fix = fr::SanitizeLootAmountBounds(p_Min, p_Max);
    m_Config.lootBounds[static_cast<size_t>(p_Kind)] = s_Fix.bounds;

    if (!s_Fix.rounded && !s_Fix.clamped && !s_Fix.swapped)
        return;

    std::string s_Why;
    const auto s_Add = [&s_Why](const std::string& p_Part) { s_Why += (s_Why.empty() ? "" : "; ") + p_Part; };

    if (s_Fix.rounded)
        s_Add(fmt::format("each a multiple of {}", fr::kLootAmountStep));

    if (s_Fix.clamped)
        s_Add(fmt::format("each in [{}, {}]", fr::kMinLootAmount, fr::kMaxLootAmount));

    if (s_Fix.swapped)
        s_Add("the least was above the most: swapped");

    Log("settings: loot_amount_{0}_min {1} / loot_amount_{0}_max {2} used as {3} / {4} ({5})",
        fr::LootAmountKey(p_Kind), p_Min, p_Max, s_Fix.bounds.min, s_Fix.bounds.max, s_Why);
}

// client_reqs (the client requirements shuffle) and its settings.
void FreelancerRandomizer::LoadClientReqsConfig() {
    m_Config.clientReqs = LoadBool("client_reqs", true);

    if (!HasSetting(kSettings, "client_reqs_exclude"))
        SetSetting(kSettings, "client_reqs_exclude", std::string(fr::kDefaultClientReqExclude));

    const ZString s_Exclude = GetSetting(kSettings, "client_reqs_exclude", "");
    std::vector<std::string> s_Bad;
    m_Config.clientReqsExclude = fr::ParseClientReqIds(s_Exclude.ToStringView(), s_Bad);

    for (const auto& s_Entry : s_Bad)
        Log("settings: client_reqs_exclude entry \"{}\" is not a requirement id; ignored", s_Entry);

    if (!fr::ClientReqExcludeLeavesEnough(m_Config.clientReqsExclude)) {
        Log("settings: client_reqs_exclude \"{}\" leaves fewer than {} common objectives to pick from; using the "
            "default \"{}\"",
            s_Exclude.ToStringView(), fr::kMinClientReqPoolSize, fr::kDefaultClientReqExclude);
        std::vector<std::string> s_None;
        m_Config.clientReqsExclude = fr::ParseClientReqIds(fr::kDefaultClientReqExclude, s_None);
    }

    if (!HasSetting(kSettings, "client_reqs_mode"))
        SetSetting(kSettings, "client_reqs_mode", std::string(fr::ClientReqModeName(fr::kDefaultClientReqMode)));

    const ZString s_Mode = GetSetting(kSettings, "client_reqs_mode", "");
    const std::optional<fr::ClientReqMode> s_ParsedMode = fr::ParseClientReqMode(s_Mode.ToStringView());
    m_Config.clientReqsMode = s_ParsedMode.value_or(fr::kDefaultClientReqMode);

    if (!s_ParsedMode)
        Log("settings: client_reqs_mode = \"{}\" is not campaign or mission; using {}", s_Mode.ToStringView(),
            fr::ClientReqModeName(fr::kDefaultClientReqMode));

    m_Config.clientReqsPoolSize = LoadInt("client_reqs_pool_size", fr::kDefaultClientReqPoolSize);

    if (m_Config.clientReqsPoolSize <= 0) {
        Log("settings: client_reqs_pool_size = {}: using the default {}", m_Config.clientReqsPoolSize,
            fr::kDefaultClientReqPoolSize);
        m_Config.clientReqsPoolSize = fr::kDefaultClientReqPoolSize;
    }

    m_ClientReqs.Configure(m_Config.clientReqs, m_Config.clientReqsExclude, m_Config.clientReqsMode,
                           m_Config.clientReqsPoolSize);
}

// weapon_rarity (the firearm rarity shuffle) and its settings.
void FreelancerRandomizer::LoadWeaponRarityConfig() {
    m_Config.weaponRarity = LoadBool("weapon_rarity", true);
    const double s_Min = LoadPercent("weapon_rarity_keep_min", fr::kDefaultWeaponRarityKeepMin);
    const double s_Max = LoadPercent("weapon_rarity_keep_max", fr::kDefaultWeaponRarityKeepMax);
    const fr::KeepRange s_Keep = fr::SanitizeKeepRange(s_Min, s_Max);

    if (s_Keep.min != s_Min || s_Keep.max != s_Max)
        Log("settings: weapon_rarity_keep_min {} / weapon_rarity_keep_max {} adjusted to {} / {} (each 0% to 100%, "
            "min <= max)",
            fr::PercentText(s_Min), fr::PercentText(s_Max), fr::PercentText(s_Keep.min), fr::PercentText(s_Keep.max));

    m_Config.weaponRarityKeepMin = s_Keep.min;
    m_Config.weaponRarityKeepMax = s_Keep.max;
    m_Config.weaponRarityCapacity = LoadBool("weapon_rarity_capacity", true);
    m_Config.concealableMace = LoadBool("concealable_mace", true);
    m_WeaponRarity.Configure(m_Config.weaponRarity, s_Keep, m_Config.weaponRarityCapacity);
    Log("config: weapon_rarity {}, weapon_rarity_keep_min {}, weapon_rarity_keep_max {}, weapon_rarity_capacity {}, "
        "concealable_mace {}",
        m_Config.weaponRarity, fr::PercentText(s_Keep.min), fr::PercentText(s_Keep.max), m_Config.weaponRarityCapacity,
        m_Config.concealableMace);

    m_Config.legalSniperRifles = LoadBool("legal_sniper_rifles", true);
    m_LegalSniper.Configure(m_Config.legalSniperRifles);
    Log("config: legal_sniper_rifles {}", m_Config.legalSniperRifles);
}

// The holsterable mace (concealable_mace, independent of weapon_rarity) is
// written first, so the rarity shuffle and the random loadout plan that
// follow read its ItemSize.
void FreelancerRandomizer::ApplyWeaponRepo(const char* p_When) {
    if (m_Config.concealableMace)
        m_Mace.Apply(p_When);
    else
        m_Mace.Restore(p_When);

    m_WeaponRarity.Apply(m_Rarity.seed, p_When);
}

void FreelancerRandomizer::RestoreWeaponRepo(const char* p_When) {
    m_Mace.Restore(p_When);
    m_WeaponRarity.Restore(p_When);
}

// random_loadout (the random loadout) and its settings.
void FreelancerRandomizer::LoadRandomLoadoutConfig() {
    m_Config.randomLoadout = LoadBool("random_loadout", true);
    // Not in the Settings tab: a troubleshooting value set by hand in the ini.
    double s_Delay = ReadIniOnlyDouble("random_loadout_delay", 1.0);

    if (!(s_Delay >= 0.0 && s_Delay <= 60.0)) {
        Log("settings: random_loadout_delay = {} is outside 0..60 seconds; using 1", s_Delay);
        s_Delay = 1.0;
    }

    m_Config.randomLoadoutDelay = s_Delay;
    m_Config.randomLoadoutHolsterableOnly = LoadBool("random_loadout_holsterable_only", false);
    m_Config.randomLoadoutLockpickFirst = LoadBool("random_loadout_lockpick_first", true);
    m_Config.randomLoadoutObjectiveItems = LoadBool("random_loadout_objective_items", true);
    m_Loadout.Configure(m_Config.randomLoadout, s_Delay, !m_Config.randomLoadoutHolsterableOnly,
                        m_Config.randomLoadoutLockpickFirst, m_Config.randomLoadoutObjectiveItems);
    Log("config: random_loadout {}, random_loadout_delay {}, random_loadout_holsterable_only {}, "
        "random_loadout_lockpick_first {}, random_loadout_objective_items {}",
        m_Config.randomLoadout, s_Delay, m_Config.randomLoadoutHolsterableOnly, m_Config.randomLoadoutLockpickFirst,
        m_Config.randomLoadoutObjectiveItems);

    // The item grants (independent of random_loadout).
    m_Config.grantMakeshiftExplosive = LoadBool("grant_makeshift_explosive", true);
    m_Config.grantLethalPoison = LoadBool("grant_lethal_poison", true);
    m_Config.grantCoin = LoadBool("grant_coin", true);
    m_Loadout.ConfigureItemGrants({m_Config.grantMakeshiftExplosive, m_Config.grantLethalPoison, m_Config.grantCoin});
    Log("config: grant_makeshift_explosive {}, grant_lethal_poison {}, grant_coin {}", m_Config.grantMakeshiftExplosive,
        m_Config.grantLethalPoison, m_Config.grantCoin);
}

// safehouse_coin (the bird's nest coin always spawns).
void FreelancerRandomizer::LoadSafehouseCoinConfig() {
    m_Config.safehouseCoin = LoadBool("safehouse_coin", true);
    m_Coin.Configure(m_Config.safehouseCoin);
    Log("config: safehouse_coin {}", m_Config.safehouseCoin);
}

// fishing_script (each safehouse visit's first three catches: fish, fishing
// line, pirate saber).
void FreelancerRandomizer::LoadFishingConfig() {
    m_Config.fishingScript = LoadBool("fishing_script", true);
    m_Fishing.Configure(m_Config.fishingScript);
    Log("config: fishing_script {}", m_Config.fishingScript);
}

// footsteps_investigate (a heard footstep may draw a real investigation, with
// footsteps_investigate_chance per hearing) and backup_guard; InvestigationFeatures.h.
void FreelancerRandomizer::LoadInvestigationsConfig() {
    m_Config.footstepsInvestigate = LoadBool("footsteps_investigate", true);
    const int32_t s_Chance = static_cast<int32_t>(std::lround(fr::FractionToPercent(
        LoadPercent("footsteps_investigate_chance", fr::kDefaultFootstepInvestigateChance / 100.0))));
    m_Config.footstepsInvestigateChance = fr::ClampFootstepInvestigateChance(s_Chance);

    if (m_Config.footstepsInvestigateChance != s_Chance)
        Log("settings: footsteps_investigate_chance {} is outside 0-100; using {}", s_Chance,
            m_Config.footstepsInvestigateChance);

    // backup_guard: a guard taken down mid-investigation of a noise brings another (Investigations.h).
    m_Config.backupGuard = LoadBool("backup_guard", true);
    const int32_t s_DelayMin = LoadInt("backup_guard_delay_min", fr::kDefaultBackupDelayMin);
    const int32_t s_DelayMax = LoadInt("backup_guard_delay_max", fr::kDefaultBackupDelayMax);
    const fr::BackupDelays s_Delays = fr::ClampBackupDelays(s_DelayMin, s_DelayMax);
    m_Config.backupGuardDelayMin = s_Delays.min;
    m_Config.backupGuardDelayMax = s_Delays.max;

    if (s_Delays.min != s_DelayMin || s_Delays.max != s_DelayMax)
        Log("settings: backup_guard_delay_min {} / backup_guard_delay_max {} are outside 0-{} or max is below min; "
            "using {} / {}",
            s_DelayMin, s_DelayMax, fr::kBackupDelayLimit, s_Delays.min, s_Delays.max);

    m_Config.backupGuardNotice = LoadBool("backup_guard_notice", false);
    m_Investigations.Configure(m_Config.footstepsInvestigate, m_Config.footstepsInvestigateChance, m_Config.backupGuard,
                               s_Delays, m_Config.backupGuardNotice);
    Log("config: footsteps_investigate {}, footsteps_investigate_chance {}%, backup_guard {} ({}-{} s after the call, "
        "notice {})",
        m_Config.footstepsInvestigate, m_Config.footstepsInvestigateChance, m_Config.backupGuard, s_Delays.min,
        s_Delays.max, m_Config.backupGuardNotice);
}

// prestige_shuffle (the laptop's prestige cards drawn from the campaign seed)
// and its persisted pick state ([campaign] prestige_shuffle).
void FreelancerRandomizer::LoadPrestigeShuffleConfig() {
    m_Config.prestigeShuffle = LoadBool("prestige_shuffle", true);

    if (!HasSetting(kSettings, "prestige_exclude"))
        SetSetting(kSettings, "prestige_exclude", std::string(fr::kDefaultPrestigeExclude));

    const ZString s_Exclude = GetSetting(kSettings, "prestige_exclude", "");
    std::vector<std::string> s_Bad;
    std::set<int32_t> s_Ids = fr::ParseClientReqIds(s_Exclude.ToStringView(), s_Bad);

    for (const auto& s_Entry : s_Bad)
        Log("settings: prestige_exclude entry \"{}\" is not an objective id; ignored", s_Entry);

    const auto s_Options = fr::PrestigeExcludeOptions();

    for (auto s_It = s_Ids.begin(); s_It != s_Ids.end();) {
        if (std::find(s_Options.begin(), s_Options.end(), *s_It) != s_Options.end()) {
            ++s_It;
            continue;
        }

        Log("settings: prestige_exclude entry {} is not a prestige objective the shuffle can draw; ignored",
            fr::PrestigeChoiceText(*s_It));
        s_It = s_Ids.erase(s_It);
    }

    // Kept as is: the draw fills a card the list can't with the fewest objectives from outside it.
    if (!fr::PrestigeExcludeLeavesEnough(s_Ids))
        Log("settings: prestige_exclude \"{}\" leaves too few objectives for three cards (card A takes only a "
            "Silent Assassin or Timed one; at most one per category); kept: a card it can't fill gets one from "
            "outside the list",
            s_Exclude.ToStringView());

    m_Config.prestigeExclude = s_Ids;
    std::optional<fr::PrestigePickState> s_State;

    if (HasSetting(kCampaign, "prestige_shuffle")) {
        const ZString s_Raw = GetSetting(kCampaign, "prestige_shuffle", "");
        s_State = fr::ParsePrestigePickState(s_Raw.ToStringView());

        if (!s_State && !Trim(s_Raw.ToStringView()).empty())
            Log("prestige shuffle: persisted state \"{}\" is not \"<seed> <counter> <taken 0|1>\"; starting over",
                s_Raw.ToStringView());
    }

    m_Prestige.Configure(
        m_Config.prestigeShuffle, m_Config.prestigeExclude, s_State,
        // The safehouse's seed: generated this session > persisted (the live seed entity reads 0 there).
        [this]() { return SafehouseSeed(); },
        [this](const std::string& p_Text) { SetSetting(kCampaign, "prestige_shuffle", p_Text); });
    std::string s_ExcludeText;

    for (const int32_t s_Id : m_Config.prestigeExclude)
        s_ExcludeText += fmt::format("{}{}", s_ExcludeText.empty() ? "" : ", ", fr::PrestigeChoiceText(s_Id));

    Log("config: prestige_shuffle {} (pick state {}), prestige_exclude [{}]", m_Config.prestigeShuffle,
        s_State ? fr::FormatPrestigePickState(*s_State) : std::string("none"), s_ExcludeText);
}

// door_locks (the door lock shuffle) and its settings.
void FreelancerRandomizer::LoadDoorLocksConfig() {
    m_Config.doorLocks = LoadBool("door_locks", true);
    m_Config.doorLocksShareMin = LoadPercent("door_locks_share_min", fr::kDefaultDoorLockShareMin);
    m_Config.doorLocksShareMax = LoadPercent("door_locks_share_max", fr::kDefaultDoorLockShareMax);
    m_DoorLocks.Configure(m_Config.doorLocks, m_Config.doorLocksShareMin, m_Config.doorLocksShareMax);
    Log("config: door_locks {}, door_locks_share_min {}, door_locks_share_max {}", m_Config.doorLocks,
        fr::PercentText(m_Config.doorLocksShareMin), fr::PercentText(m_Config.doorLocksShareMax));

    if (!HasSetting(kSettings, "lockpick_grant"))
        SetSetting(kSettings, "lockpick_grant", std::string("nails"));

    const ZString s_GrantText = GetSetting(kSettings, "lockpick_grant", "");
    const std::optional<fr::LockpickGrantMode> s_Grant = fr::ParseLockpickGrantMode(s_GrantText.ToStringView());

    if (!s_Grant)
        Log("settings: lockpick_grant = \"{}\" is not off, slow_lockpick or nails; using nails",
            s_GrantText.ToStringView());

    const fr::LockpickGrantMode s_Mode = s_Grant.value_or(fr::LockpickGrantMode::Nails);
    m_Config.lockpickGrant = fr::LockpickGrantModeName(s_Mode);
    const int32_t s_Nails = LoadInt("lockpick_nails", fr::kDefaultLockpickNails);
    m_Config.lockpickNails = fr::ClampLockpickNails(s_Nails);

    if (m_Config.lockpickNails != s_Nails)
        Log("settings: lockpick_nails {} is outside {}-{}; using {}", s_Nails, fr::kMinLockpickNails,
            fr::kMaxLockpickNails, m_Config.lockpickNails);

    // The slow lockpick is always the classic lockpick (fr::kSlowLockpickId).
    const int32_t s_Picks = LoadInt("lockpick_slow_picks", fr::kDefaultLockpickSlowPicks);
    m_Config.lockpickSlowPicks = fr::ClampLockpickSlowPicks(s_Picks);

    if (m_Config.lockpickSlowPicks != s_Picks)
        Log("settings: lockpick_slow_picks {} is outside {}-{}; using {}", s_Picks, fr::kMinLockpickSlowPicks,
            fr::kMaxLockpickSlowPicks, m_Config.lockpickSlowPicks);

    m_Loadout.ConfigureLockpickGrant(s_Mode, m_Config.lockpickNails);
    m_DoorLocks.ConfigureLockpickFree(
        s_Mode, m_Config.lockpickSlowPicks, [this] { m_Loadout.PrepareWorldInventory(); },
        [this](std::string* p_Why) { return m_Loadout.HeroLockpicks(p_Why); });
    Log("config: lockpick_grant {}, lockpick_nails {}, lockpick_slow_picks {}", m_Config.lockpickGrant,
        m_Config.lockpickNails, m_Config.lockpickSlowPicks);
}

std::optional<int32_t> FreelancerRandomizer::SafehouseSeed() {
    const fr::SeedResolution s_Seed =
        m_IsMission && m_SceneSeed ? *m_SceneSeed : fr::ResolveSeed(std::nullopt, m_GeneratedSeed, ReadPersistedSeed());

    if (s_Seed.source == fr::SeedSource::Fallback)
        return std::nullopt;

    return s_Seed.seed;
}

// payouts (random per-objective scale) and its settings.
void FreelancerRandomizer::LoadPayoutConfig() {
    m_Config.payouts = LoadBool("payouts", true);
    const fr::PayoutScaleConfig s_Defaults = fr::Config {}.payoutScale;
    const fr::PayoutScaleConfig s_Loaded {
        LoadDouble("payout_sigma", s_Defaults.sigma),
        LoadDouble("payout_min", s_Defaults.min),
        LoadDouble("payout_max", s_Defaults.max),
        LoadDouble("payout_lean", s_Defaults.lean),
        LoadPercent("payout_jackpot_chance", s_Defaults.jackpotChance),
        LoadDouble("payout_jackpot_bonus", s_Defaults.jackpotBonus),
        LoadBool("payout_same_base", s_Defaults.sameBase),
    };
    m_Config.payoutScale = fr::SanitizePayoutScaleConfig(s_Loaded, s_Defaults);
    const fr::PayoutScaleConfig& s_Safe = m_Config.payoutScale;

    // LoadDouble already replaced NaN, so plain comparison detects every reset.
    if (s_Loaded.sigma != s_Safe.sigma || s_Loaded.min != s_Safe.min || s_Loaded.max != s_Safe.max ||
        s_Loaded.lean != s_Safe.lean || s_Loaded.jackpotChance != s_Safe.jackpotChance ||
        s_Loaded.jackpotBonus != s_Safe.jackpotBonus) {
        Log("settings: invalid payout values reset to defaults (loaded payout_sigma {} payout_min {} payout_max {} "
            "payout_lean {} payout_jackpot_chance {} payout_jackpot_bonus {}; using {} {} {} {} {} {})",
            s_Loaded.sigma, s_Loaded.min, s_Loaded.max, s_Loaded.lean, fr::PercentText(s_Loaded.jackpotChance),
            s_Loaded.jackpotBonus, s_Safe.sigma, s_Safe.min, s_Safe.max, s_Safe.lean,
            fr::PercentText(s_Safe.jackpotChance), s_Safe.jackpotBonus);
    }

    const fr::PayoutMode s_Mode = fr::ChoosePayoutMode(m_Config.payouts);

    Log("config: payouts {}, payout_sigma {}, payout_lean {}, payout_min {}, payout_max {}, payout_jackpot_chance {}, "
        "payout_jackpot_bonus {}, payout_same_base {}: payout table {}",
        m_Config.payouts, s_Safe.sigma, s_Safe.lean, s_Safe.min, s_Safe.max, fr::PercentText(s_Safe.jackpotChance),
        s_Safe.jackpotBonus, s_Safe.sameBase, fr::PayoutModeName(s_Mode));

    // Once configured, a reload configures it again even when it is now off.
    if (s_Mode != fr::PayoutMode::Off || m_MercesConfigured) {
        m_MercesConfigured = true;
        m_Merces.Configure(
            s_Mode, m_Config.payoutScale, ReadPrestigeMemory(),
            [this](const fr::PrestigeMemory& p_Memory) { SavePrestigeMemory(p_Memory); },
            // In a mission, the seed the dials settled for the scene (resolved
            // before it loaded; a restart reuses it). Elsewhere (the safehouse)
            // generated > persisted: the live seed entity reads 0 at scene start.
            // MercesPayouts keeps the result per scene; nothing is persisted here.
            [this]() {
                if (m_IsMission && m_SceneSeed) {
                    LogTo(fr::LogTopic::Money, "merces scale: using the dials' seed for this mission");
                    return *m_SceneSeed;
                }

                const auto s_Persisted = ReadPersistedSeed();
                LogTo(fr::LogTopic::Money,
                      "merces scale: seed sources: live not read (0 at scene start), generated {}, persisted {}",
                      OptionalText(m_GeneratedSeed), OptionalText(s_Persisted));
                return fr::ResolveSeed(std::nullopt, m_GeneratedSeed, s_Persisted);
            });
    }
}

bool FreelancerRandomizer::LoadBool(std::string_view p_Key, bool p_Default) {
    CheckTableDefault(p_Key, p_Default ? "true" : "false");

    if (!HasSetting(kSettings, p_Key)) {
        SetSettingBool(kSettings, p_Key, p_Default);
        return p_Default;
    }

    // The SDK returns the default for unparsable values; two different
    // defaults disagree exactly when the stored value is unparsable.
    const bool s_IfTrue = GetSettingBool(kSettings, p_Key, true);

    if (s_IfTrue != GetSettingBool(kSettings, p_Key, false)) {
        Log("settings: {} = \"{}\" is not a boolean; using {}", p_Key, GetSetting(kSettings, p_Key, "").ToStringView(),
            p_Default);
        return p_Default;
    }

    return s_IfTrue;
}

// Footsteps (Footsteps.h) and sensor dials (SensorDials.h); both apply only with dials.
void FreelancerRandomizer::LoadSensesConfig() {
    const fr::FootstepConfig s_Defaults = fr::DefaultFootstepConfig();
    fr::FootstepConfig s_Steps = s_Defaults;
    s_Steps.on = LoadBool("footsteps", s_Defaults.on);
    std::string s_Gaits;

    for (size_t i = 0; i < fr::kFootstepGaits; ++i) {
        const fr::FootstepGait& s_Gait = fr::FootstepGaits()[i];
        s_Steps.enabled[i] = LoadBool(fmt::format("footsteps_{}", s_Gait.key), s_Defaults.enabled[i]);
        s_Steps.range[i] = LoadDouble(fmt::format("footsteps_{}_range", s_Gait.key), s_Defaults.range[i]);
    }

    s_Steps.roll = {
        LoadDouble("footsteps_sigma", s_Defaults.roll.sigma), LoadDouble("footsteps_lean", s_Defaults.roll.lean),
        LoadDouble("footsteps_min", s_Defaults.roll.clampMin), LoadDouble("footsteps_max", s_Defaults.roll.clampMax)};
    m_Config.footsteps = fr::SanitizeFootstepConfig(s_Steps);
    const fr::RollConfig& s_Roll = m_Config.footsteps.roll;

    if (s_Roll.sigma != s_Steps.roll.sigma || s_Roll.lean != s_Steps.roll.lean ||
        s_Roll.clampMin != s_Steps.roll.clampMin || s_Roll.clampMax != s_Steps.roll.clampMax) {
        Log("settings: invalid footstep roll values reset to defaults (loaded footsteps_sigma {} footsteps_lean {} "
            "footsteps_min {} footsteps_max {}; using {} {} {} {})",
            s_Steps.roll.sigma, s_Steps.roll.lean, s_Steps.roll.clampMin, s_Steps.roll.clampMax, s_Roll.sigma,
            s_Roll.lean, s_Roll.clampMin, s_Roll.clampMax);
    }

    for (size_t i = 0; i < fr::kFootstepGaits; ++i) {
        const std::string_view s_Key = fr::FootstepGaits()[i].key;

        if (m_Config.footsteps.range[i] != s_Steps.range[i]) {
            Log("settings: footsteps_{}_range = {} is not a range above 0; using {}", s_Key, s_Steps.range[i],
                m_Config.footsteps.range[i]);
        }

        s_Gaits += fmt::format("{}{} {} ({} m)", s_Gaits.empty() ? "" : ", ", s_Key,
                               m_Config.footsteps.enabled[i] ? "on" : "off", m_Config.footsteps.range[i]);
    }

    Log("config: footsteps {}: {}; roll sigma {} lean {} min {} max {}", m_Config.footsteps.on, s_Gaits, s_Roll.sigma,
        s_Roll.lean, s_Roll.clampMin, s_Roll.clampMax);

    m_Config.sensorDials = LoadBool("sensor_dials", true);

    if (!HasSetting(kSettings, "sensor_dials_off"))
        SetSetting(kSettings, "sensor_dials_off", "");

    const ZString s_Off = GetSetting(kSettings, "sensor_dials_off", "");
    std::vector<std::string> s_Unknown;
    m_Config.sensorOff = fr::ParseSensorOffList(s_Off.ToStringView(), s_Unknown);

    for (const auto& s_Key : s_Unknown)
        Log("settings: unknown sensor dial \"{}\" in sensor_dials_off; ignored", s_Key);

    std::string s_OffKeys;

    for (const std::string& s_Key : m_Config.sensorOff)
        s_OffKeys += fmt::format("{}{}", s_OffKeys.empty() ? "" : ", ", s_Key);

    Log("config: sensor_dials {}, sensor_dials_off [{}]", m_Config.sensorDials, s_OffKeys);

    m_Config.sickSpotKills = LoadBool("sick_spot_kills", true);
    const int32_t s_Boost = LoadInt("sick_spot_kill_boost", fr::kSickBoostDefault);
    m_Config.sickSpotKillBoost = fr::ClampSickBoost(s_Boost);

    if (m_Config.sickSpotKillBoost != s_Boost)
        Log("settings: sick_spot_kill_boost = {} is outside 0..{}; using {}", s_Boost, fr::kSickBoostMax,
            m_Config.sickSpotKillBoost);

    m_SickSpots.Configure(m_Config.sickSpotKills, m_Config.sickSpotKillBoost);
    Log("config: sick_spot_kills {}, sick_spot_kill_boost {}", m_Config.sickSpotKills, m_Config.sickSpotKillBoost);
}

double FreelancerRandomizer::LoadDouble(std::string_view p_Key, double p_Default) {
    CheckTableDefault(p_Key, fr::FormatSettingNumber(p_Default));

    if (!HasSetting(kSettings, p_Key)) {
        SetSettingDouble(kSettings, p_Key, p_Default);
        return p_Default;
    }

    return SettingDouble(p_Key, p_Default);
}

double FreelancerRandomizer::ReadIniOnlyDouble(std::string_view p_Key, double p_Default) {
    return HasSetting(kSettings, p_Key) ? SettingDouble(p_Key, p_Default) : p_Default;
}

double FreelancerRandomizer::SettingDouble(std::string_view p_Key, double p_Default) {
    // Read as the settings window reads it; NaN and infinities are rejected so callers never see them.
    const ZString s_Raw = GetSetting(kSettings, p_Key, "");
    const std::optional<double> s_Value = fr::ParseSettingNumber(s_Raw.ToStringView());

    if (!s_Value) {
        Log("settings: {} = \"{}\" is not a number; using {}", p_Key, s_Raw.ToStringView(), p_Default);
        return p_Default;
    }

    return *s_Value;
}

double FreelancerRandomizer::LoadPercent(std::string_view p_Key, double p_DefaultFraction) {
    return fr::PercentToFraction(LoadDouble(p_Key, fr::FractionToPercent(p_DefaultFraction)));
}

int32_t FreelancerRandomizer::LoadInt(std::string_view p_Key, int32_t p_Default) {
    CheckTableDefault(p_Key, fmt::format("{}", p_Default));

    if (!HasSetting(kSettings, p_Key)) {
        SetSettingInt(kSettings, p_Key, p_Default);
        return p_Default;
    }

    // Read as the settings window reads it ("+5" is 5, "25.0" is 25).
    const ZString s_Raw = GetSetting(kSettings, p_Key, "");
    const std::optional<int32_t> s_Value = fr::ParseSettingInt(s_Raw.ToStringView());

    if (!s_Value) {
        Log("settings: {} = \"{}\" is not a 32-bit integer; using {}", p_Key, s_Raw.ToStringView(), p_Default);
        return p_Default;
    }

    return *s_Value;
}

// The persisted seed only when the key exists and holds a 32-bit integer
// (never the SDK's default value standing in for a missing or bad entry).
std::optional<int32_t> FreelancerRandomizer::ReadPersistedSeed() {
    if (!HasSetting(kCampaign, "seed"))
        return std::nullopt;

    const ZString s_Raw = GetSetting(kCampaign, "seed", "");
    const std::string_view s_Text = Trim(s_Raw.ToStringView());
    int32_t s_Seed = 0;
    const auto [s_End, s_Error] = std::from_chars(s_Text.data(), s_Text.data() + s_Text.size(), s_Seed);

    if (s_Error != std::errc() || s_End != s_Text.data() + s_Text.size()) {
        Log("persisted seed \"{}\" is not a 32-bit integer; ignored", s_Text);
        return std::nullopt;
    }

    return s_Seed;
}

// [campaign] rarity_seed: the campaign the player picked; rarity_live: the
// seed its missions run with, once one was seen. An ini of an earlier build
// starts from the persisted campaign seed (written back) and binds the live
// seed at the next mission.
void FreelancerRandomizer::LoadRaritySeed() {
    const auto s_Read = [this](std::string_view p_Key) {
        return HasSetting(kCampaign, p_Key) ? std::string(GetSetting(kCampaign, p_Key, "").ToStringView())
                                            : std::string();
    };
    const std::string s_Raw = s_Read("rarity_seed");
    m_Rarity.seed = fr::InitialRaritySeed(s_Raw, ReadPersistedSeed());
    m_Rarity.live = fr::InitialRaritySeed(s_Read("rarity_live"), std::nullopt);

    if (m_Rarity.seed && Trim(s_Raw) != fmt::format("{}", *m_Rarity.seed)) {
        LogTo(fr::LogTopic::Weapons,
              "weapon rarity: no picked campaign persisted (rarity_seed \"{}\"); starting from the persisted campaign "
              "seed {}",
              s_Raw, *m_Rarity.seed);
        SetSettingInt(kCampaign, "rarity_seed", *m_Rarity.seed);
    }

    LogTo(fr::LogTopic::Weapons,
          "weapon rarity: the rarities follow the picked campaign, seed {}, whose missions run with seed {} (they "
          "change only when another campaign is picked)",
          OptionalText(m_Rarity.seed), m_Rarity.live ? fmt::format("{}", *m_Rarity.live) : "not seen yet");
}

void FreelancerRandomizer::SetRarityCampaign(const fr::RarityCampaign& p_Campaign) {
    m_Rarity = p_Campaign;
    SetSetting(kCampaign, "rarity_seed", m_Rarity.seed ? fmt::format("{}", *m_Rarity.seed) : std::string());
    SetSetting(kCampaign, "rarity_live", m_Rarity.live ? fmt::format("{}", *m_Rarity.live) : std::string());
}

void FreelancerRandomizer::OnCampaignPicked(int32_t p_Seed, const char* p_How) {
    // The safehouse's live seed entity (CPD_RandomSeed) holds the picked
    // campaign's own seed at the pick, which its missions run with; the
    // generated seed (p_Seed) is not it.
    std::optional<int32_t> s_Live;

    if (const ZEntitySceneContext* s_Context = CurrentSceneContext())
        s_Live = ReadLiveSeed(s_Context->m_aLoadedBricks);

    LogTo(fr::LogTopic::Weapons, "weapon rarity: live seed entity at the pick: {} (bound: {})", OptionalText(s_Live),
          OptionalText(m_Rarity.live));

    const fr::RarityDecision s_Decision = fr::RarityOnPick(m_Rarity, p_Seed);
    // A rerolled campaign's bound seed is the previous campaign's: a live
    // seed equal to it was not updated yet.
    const std::optional<int32_t> s_Previous = s_Decision.step == fr::RarityStep::Reroll ? m_Rarity.live : std::nullopt;

    if (s_Decision.step != fr::RarityStep::Reroll) {
        LogTo(fr::LogTopic::Weapons, "weapon rarity: campaign seed {} picked ({}): {}", p_Seed, p_How,
              p_Seed == 0 ? "unknown seed; the rarities stay" : "already the rarities' campaign; nothing changes");
    }
    else {
        LogTo(fr::LogTopic::Weapons,
              "weapon rarity: campaign seed {} picked ({}): the rarities change from campaign {} to it, once, until "
              "the next pick; its missions' own seed is bound now when the live seed holds it, else at the first one",
              p_Seed, p_How, OptionalText(m_Rarity.seed));
        SetRarityCampaign(s_Decision.next);

        // In the safehouse the gear wall reads pro.repo, so the new rarities
        // show at once.
        if (fr::IsFreelancerScene(m_Type) && !m_IsMission)
            ApplyWeaponRepo("campaign picked");
    }

    const std::optional<int32_t> s_Persisted = ReadPersistedSeed();

    switch (fr::DecidePickSeed(s_Live, s_Persisted, s_Previous)) {
        case fr::PickSeed::Unknown:
            LogTo(fr::LogTopic::Missions,
                  "campaign pick: live seed {} unknown; missions use seed {} until the live seed is read in one",
                  OptionalText(s_Live), OptionalText(m_GeneratedSeed ? m_GeneratedSeed : s_Persisted));
            return;
        case fr::PickSeed::Same:
            LogTo(fr::LogTopic::Missions, "campaign pick: live seed {} is already the persisted campaign seed",
                  *s_Live);
            break;
        case fr::PickSeed::Stale:
            LogTo(fr::LogTopic::Missions,
                  "campaign pick: live seed {} is still the previous campaign's mission seed; not used (the first "
                  "mission reads it again)",
                  *s_Live);
            return;
        case fr::PickSeed::Adopt:
            LogTo(fr::LogTopic::Missions,
                  "campaign pick: live seed {} is the picked campaign's own seed (generated {}, persisted {}): its "
                  "missions run with it from the first",
                  *s_Live, OptionalText(m_GeneratedSeed), OptionalText(s_Persisted));
            break;
    }

    AdoptCampaignSeed(*s_Live, p_Seed);

    // Its missions' seed is known now: bound to the picked campaign, so the
    // first mission's LoadScene keeps the rarities.
    if (!m_Rarity.live)
        OnRarityMission(*s_Live, true, "campaign pick");
}

void FreelancerRandomizer::AdoptCampaignSeed(int32_t p_Seed, std::optional<int32_t> p_Generated) {
    if (ReadPersistedSeed() != p_Seed) {
        PersistSeed(p_Seed);
        LogTo(fr::LogTopic::Missions, "persisted campaign seed {}", p_Seed);
    }

    // The generated seed no longer outranks it (fr::ResolveSeed: live >
    // generated > persisted).
    if (m_GeneratedSeed && *m_GeneratedSeed != p_Seed) {
        LogTo(fr::LogTopic::Missions, "generated seed {} replaced by campaign seed {} for the rest of this session",
              *m_GeneratedSeed, p_Seed);
        m_GeneratedSeed.reset();
    }

    if (!fr::IsKnownSeed(p_Generated) || *p_Generated == p_Seed)
        return;

    // What was stored under the generated seed is the same campaign's.
    const int32_t s_Generated = *p_Generated;
    m_SeedAlias = fr::SeedAlias {s_Generated, p_Seed};

    if (HasSetting(kCampaign, "client_reqs_checked") &&
        Trim(GetSetting(kCampaign, "client_reqs_checked", "").ToStringView()) == fmt::format("{}", s_Generated)) {
        SetSettingInt(kCampaign, "client_reqs_checked", p_Seed);
        LogTo(fr::LogTopic::CommonObjectives, "client reqs: client_reqs_checked {} now reads {} (the same campaign)",
              s_Generated, p_Seed);
    }

    m_Prestige.RekeyCampaign(s_Generated, p_Seed);
}

void FreelancerRandomizer::OnRarityMission(int32_t p_Seed, bool p_Live, const char* p_When) {
    const fr::RarityDecision s_Decision = fr::RarityOnMission(m_Rarity, p_Seed, p_Live);

    switch (s_Decision.step) {
        case fr::RarityStep::Keep: return;
        case fr::RarityStep::Bind:
            LogTo(fr::LogTopic::Weapons,
                  "weapon rarity: {}: live campaign seed {} is the picked campaign's (rarities from seed {}): bound; "
                  "the rarities stay",
                  p_When, p_Seed, OptionalText(s_Decision.next.seed));
            SetRarityCampaign(s_Decision.next);
            return;
        case fr::RarityStep::Reroll:
            LogTo(fr::LogTopic::Weapons,
                  "weapon rarity: {}: campaign seed {} ({}) is not the rarities' campaign (seed {}, missions {}): its "
                  "pick was not seen; the rarities change to it{}",
                  p_When, p_Seed, p_Live ? "live" : "resolved", OptionalText(m_Rarity.seed),
                  OptionalText(m_Rarity.live), p_Live ? " from the next scene load" : "");
            SetRarityCampaign(s_Decision.next);
            return;
    }
}

// A generated campaign whose picks were not seen yet ([campaign]
// client_reqs_pending) is watched again in this session while it is still the
// persisted campaign.
void FreelancerRandomizer::ResumeClientReqs() {
    LoadRaritySeed();
    m_ClientReqs.SetOnPicked([this](int32_t p_Seed) { OnCampaignPicked(p_Seed, "the campaign pick was seen"); });
    m_ClientReqs.SetPersist([this](int32_t p_Seed) {
        SetSetting(kCampaign, "client_reqs_pending", p_Seed ? fmt::format("{}", p_Seed) : std::string());
    });
    m_ClientReqs.SetCampaign(ReadPersistedSeed(),
                             HasSetting(kCampaign, "client_reqs_checked")
                                 ? std::string(GetSetting(kCampaign, "client_reqs_checked", "").ToStringView())
                                 : std::string(),
                             // The watch keys the campaign by its generated seed; once the
                             // campaign's own seed is adopted, that is what the next session reads.
                             [this](int32_t p_Seed) {
                                 const int32_t s_Seed = fr::ApplySeedAlias(p_Seed, m_SeedAlias);
                                 SetSetting(kCampaign, "client_reqs_checked",
                                            s_Seed ? fmt::format("{}", s_Seed) : std::string());
                             });

    if (!HasSetting(kCampaign, "client_reqs_pending"))
        return;

    const ZString s_Raw = GetSetting(kCampaign, "client_reqs_pending", "");
    const std::optional<int32_t> s_Persisted = ReadPersistedSeed();

    if (const std::optional<int32_t> s_Seed = fr::ResumableClientReqSeed(s_Raw.ToStringView(), s_Persisted))
        m_ClientReqs.Resume(*s_Seed);
    else if (!Trim(s_Raw.ToStringView()).empty())
        Log("client reqs: persisted client_reqs_pending \"{}\" is not the persisted campaign seed ({}); dropped",
            s_Raw.ToStringView(), OptionalText(s_Persisted));

    if (!m_ClientReqs.Waiting())
        SetSetting(kCampaign, "client_reqs_pending", std::string());
}

// 0 means "unknown" and is never stored.
void FreelancerRandomizer::PersistSeed(int32_t p_Seed) {
    if (p_Seed == 0) {
        LogTo(fr::LogTopic::Missions, "seed 0 means unknown; not persisted");
        return;
    }

    SetSettingInt(kCampaign, "seed", p_Seed);
}

// What the mod saw of the payout table's prestige entries, written by it in
// [merces]. Bad entries are logged and skipped.
fr::PrestigeMemory FreelancerRandomizer::ReadPrestigeMemory() {
    fr::PrestigeMemory s_Memory;

    if (HasSetting(kMerces, "prestige_bonus")) {
        const ZString s_Raw = GetSetting(kMerces, "prestige_bonus", "");
        const std::string_view s_Text = Trim(s_Raw.ToStringView());
        int32_t s_Bonus = 0;
        const auto [s_End, s_Error] = std::from_chars(s_Text.data(), s_Text.data() + s_Text.size(), s_Bonus);

        if (s_Error == std::errc() && s_End == s_Text.data() + s_Text.size() && s_Bonus >= 0)
            s_Memory.bonus = s_Bonus;
        else if (!s_Text.empty())
            Log("merces: persisted prestige_bonus \"{}\" is not a non-negative 32-bit integer; ignored", s_Text);
    }

    for (const auto& [s_Key, s_Into] : {std::pair {std::string_view("prestige_ids"), &s_Memory.prestige},
                                        std::pair {std::string_view("normal_ids"), &s_Memory.normal}}) {
        if (!HasSetting(kMerces, s_Key))
            continue;

        std::vector<std::string> s_Bad;
        *s_Into = fr::ParseIdList(GetSetting(kMerces, s_Key, "").ToStringView(), s_Bad);

        for (const auto& s_Entry : s_Bad)
            Log("merces: persisted {} entry \"{}\" is not an entity id; ignored", s_Key, s_Entry);
    }

    // An id in both lists (a hand edit) counts as neither.
    for (auto s_It = s_Memory.prestige.begin(); s_It != s_Memory.prestige.end();) {
        if (s_Memory.normal.erase(*s_It) > 0) {
            Log("merces: persisted id {:016x} is in both prestige_ids and normal_ids; ignored", *s_It);
            s_It = s_Memory.prestige.erase(s_It);
        }
        else {
            ++s_It;
        }
    }

    return s_Memory;
}

void FreelancerRandomizer::SavePrestigeMemory(const fr::PrestigeMemory& p_Memory) {
    SetSetting(kMerces, "prestige_bonus", p_Memory.bonus ? fmt::format("{}", *p_Memory.bonus) : std::string());
    SetSetting(kMerces, "prestige_ids", fr::FormatIdList(p_Memory.prestige));
    SetSetting(kMerces, "normal_ids", fr::FormatIdList(p_Memory.normal));
}

DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, ZEvergreenCampaignManager*, ZEvergreenCampaignManager_OnGenerate,
                     ZEvergreenCampaignManager* th) {
    (void)p_Hook; // declared by DEFINE_PLUGIN_DETOUR
    try {
        const auto s_Seed = th ? ReadProperty<int32_t>(th->m_rSeed.m_entityRef, "m_nValue") : std::nullopt;

        if (!s_Seed) {
            LogTo(fr::LogTopic::Missions, "campaign generated: seed not readable");
        }
        else if (*s_Seed == 0) {
            LogTo(fr::LogTopic::Missions,
                  "campaign generated with seed 0 (unknown); not used or persisted (previously persisted: {})",
                  OptionalText(ReadPersistedSeed()));
        }
        else {
            LogTo(fr::LogTopic::Missions, "campaign generated with seed {} (previously persisted: {})", *s_Seed,
                  OptionalText(ReadPersistedSeed()));
            // Not the seed the campaign's missions run with: it stands in
            // (and keys the client reqs watch and a pending pick across
            // sessions) until the pick, where OnCampaignPicked adopts the
            // live one.
            m_GeneratedSeed = s_Seed;
            PersistSeed(*s_Seed);
            LogTo(fr::LogTopic::Missions, "persisted generated seed {}", *s_Seed);
            m_Merces.OnCampaignGenerated(*s_Seed);
            // The weapon rarities wait for the pick (OnCampaignPicked): the
            // game generates a new campaign at every safehouse visit until
            // one is picked.
        }

        // Reads the game's picks once they are there (a frame update later).
        m_ClientReqs.OnCampaignGenerated(s_Seed);
    }
    catch (const std::exception& e) {
        LogError("OnGenerate hook failed: {}", e.what());
    }
    catch (...) {
        LogError("OnGenerate hook failed: unknown exception");
    }

    return HookResult<ZEvergreenCampaignManager*>(HookAction::Continue());
}

DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, bool, ZEntitySceneContext_LoadScene, ZEntitySceneContext* th,
                     SSceneInitParameters& parameters) {
    (void)p_Hook; // declared by DEFINE_PLUGIN_DETOUR
    // A setting changed in the overlay: this scene gets the new values.
    ReloadConfigIfChanged("LoadScene");
    m_Variants.ClearOverlay();
    // A new load: the last swap's alias ends here (a full variant sets it
    // again at the end of this detour). Restarts come without a LoadScene and keep it.
    ClearSceneAlias();

    try {
        ResetMapState();
        m_LoadSceneNs.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count(),
            std::memory_order_relaxed);

        const std::string s_Scene(parameters.m_SceneResource.ToStringView());

        if (m_FirstSeeds.Find(m_Scene) && s_Scene != m_Scene)
            LogTo(fr::LogTopic::Missions, "different scene: forgetting the seed {} first started with", m_Scene);

        m_FirstSeeds.ForgetOtherScenes(s_Scene);

        m_Sensors.OnLoadScene();
        m_LoadPending = true;
        m_ClearedWithoutLoad = false;
        m_Scene = s_Scene;
        m_Type = std::string(parameters.m_Type.ToStringView());
        m_IsMission = fr::IsFreelancerMission(m_Type, m_Scene);

        LogTo(fr::LogTopic::Missions, "load scene {} (type \"{}\", {} additional bricks): {}", m_Scene, m_Type,
              parameters.m_aAdditionalBrickResources.size(),
              m_IsMission ? "Freelancer mission" : "not a Freelancer mission");

        // Normally restored at the patched mission's ClearScene already.
        if (!m_EntityPatches.Empty() || !m_RepoPatches.Empty())
            RestorePatches(th ? &th->m_aLoadedBricks : nullptr, "before loading a new scene");

        if (!m_SawLoadingStage && ++m_LoadScenesWithoutStage >= 2 && !m_WarnedStageNotReached) {
            m_WarnedStageNotReached = true;
            Log("{}", kStageNotReached);
        }

        UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) {
            p_Snapshot.scene = m_Scene;
            p_Snapshot.type = m_Type;
            p_Snapshot.isMission = m_IsMission;
            p_Snapshot.status = m_WarnedStageNotReached ? kStageNotReached : "Loading...";
            p_Snapshot.readBack.clear();
        });

        // pro.repo is resident and the globaldata difficulty
        // entities are built from it later in this load, so it is written now,
        // before the original LoadScene runs.
        if (m_IsMission)
            EarlyApply("LoadScene");

        // pro.repo too, before the scene is built: the picked campaign's
        // rarities in every Freelancer scene, vanilla elsewhere. A mission
        // whose seed is another campaign's (its pick was missed) rolls new
        // ones; the picked campaign's own seed is bound at the live read.
        if (m_IsMission && m_SceneSeed && m_SceneSeed->source != fr::SeedSource::Fallback)
            OnRarityMission(m_SceneSeed->seed, m_SceneSeed->source == fr::SeedSource::Live, "LoadScene");

        if (fr::IsFreelancerScene(m_Type))
            ApplyWeaponRepo("LoadScene");
        else
            RestoreWeaponRepo("LoadScene (not a Freelancer scene)");

        m_GuardWeapons.OnSceneLoad(m_Scene, m_Type, m_SceneSeed, m_Config.guardWeapons);

        // After the rarity shuffle: the plan sees this campaign's rarities and costs.
        m_Loadout.OnSceneLoad(m_Scene, m_Type, m_SceneSeed, "LoadScene");
    }
    catch (const std::exception& e) {
        LogError("LoadScene hook failed: {}", e.what());
    }
    catch (...) {
        LogError("LoadScene hook failed: unknown exception");
    }

    // Its own guard, so a throw above does not skip it. Scene and type come
    // from the parameters (m_Scene may be stale after a throw); the seed only
    // when it was resolved for this scene.
    try {
        const std::string s_Scene(parameters.m_SceneResource.ToStringView());
        m_Variants.OnLoadScene(parameters, s_Scene, fr::IsFreelancerMission(parameters.m_Type.ToStringView(), s_Scene),
                               s_Scene == m_Scene ? m_SceneSeed : std::nullopt, m_Config);
    }
    catch (const std::exception& e) {
        LogError("LoadScene variants failed: {}", e.what());
    }
    catch (...) {
        LogError("LoadScene variants failed: unknown exception");
    }

    return HookResult<bool>(HookAction::Continue());
}

DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, void, ZEntitySceneContext_SetLoadingStage, ZEntitySceneContext* th,
                     ESceneLoadingStage stage) {
    (void)p_Hook; // declared by DEFINE_PLUGIN_DETOUR
    try {
        m_SawLoadingStage = true;
        m_Stage.store(static_cast<int>(stage), std::memory_order_relaxed);
        load_fault::SetStage(StageName(stage));
        LogTo(fr::LogTopic::Missions, "loading stage {}", StageLabel());
        m_GuardWeapons.OnLoadingStage(stage, th);

        if (stage == ESceneLoadingStage::eLoading_AssetsLoaded && th && m_ClearedWithoutLoad) {
            // A scene was cleared and no LoadScene followed (seen on restarts):
            // the scene context still holds what is being loaded. pro.repo is
            // written here, before the scene is allocated and its entities built.
            m_ClearedWithoutLoad = false;
            ReloadConfigIfChanged("restart");
            // A swapped load's restart reloads the swapped scene: counted as the contract's (SceneOf.h).
            const std::string_view s_Loaded = th->m_SceneInitParameters.m_SceneResource.ToStringView();
            const std::string s_Scene = SceneOf(th);
            const std::string s_Type(th->m_SceneInitParameters.m_Type.ToStringView());
            const bool s_IsMission = fr::IsFreelancerMission(s_Type, s_Scene);

            LogTo(fr::LogTopic::Missions, "load without LoadScene (a restart, so far): {} (type \"{}\"): {}",
                  SceneLabelOf(s_Loaded), s_Type, s_IsMission ? "Freelancer mission" : "not a Freelancer mission");

            m_LoadSceneNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count(),
                                std::memory_order_relaxed);

            if (s_Scene != m_Scene)
                m_FirstSeeds.ForgetOtherScenes(s_Scene);

            m_Scene = s_Scene;
            m_Type = s_Type;
            m_IsMission = s_IsMission;

            // Normally restored at ClearScene already.
            if (!s_IsMission && (!m_EntityPatches.Empty() || !m_RepoPatches.Empty()))
                RestorePatches(nullptr, "load without LoadScene (not a mission)");

            UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) {
                p_Snapshot.scene = m_Scene;
                p_Snapshot.type = m_Type;
                p_Snapshot.isMission = m_IsMission;
            });

            if (s_IsMission) {
                m_LoadPending = true;
                EarlyApply("restart (eLoading_AssetsLoaded)");
            }

            if (fr::IsFreelancerScene(m_Type))
                ApplyWeaponRepo("restart (eLoading_AssetsLoaded)");
            else
                RestoreWeaponRepo("restart (not a Freelancer scene)");

            m_GuardWeapons.OnSceneLoad(m_Scene, m_Type, m_SceneSeed, m_Config.guardWeapons);
            m_Variants.OnRestart(th->m_SceneInitParameters, m_Scene, m_IsMission);
            m_Loadout.OnSceneLoad(m_Scene, m_Type, m_SceneSeed, "restart (eLoading_AssetsLoaded)");
        }
        else if (stage == ESceneLoadingStage::eLoading_SceneStarted && th) {
            m_DialsWalkMs.reset();
            OnSceneStarted(th);
        }
        else if (stage == ESceneLoadingStage::eLoading_ScenePlaying) {
            const size_t s_Written = CountWrites(m_Applied).first;

            if (m_SceneSeed && th)
                CheckLateSeed(th->m_aLoadedBricks, "scene playing");

            m_ReadBack = {};
            m_ReadBack.readValues = s_Written > 0 || !m_RepoWrites.empty();
            m_ReadBack.checkSeed = m_SceneSeed.has_value();
            m_ReadBack.armed = m_ReadBack.readValues || m_ReadBack.checkSeed;

            if (m_ReadBack.readValues) {
                LogTo(
                    fr::LogTopic::AiDifficulty,
                    "scene playing: will read back {} written pro.repo entries and {} written entity values after 5 s "
                    "and 60 s of play",
                    m_RepoWrites.size(), s_Written);
            }

            if (m_ReadBack.checkSeed)
                LogTo(fr::LogTopic::Missions,
                      "scene playing: will read the live seed again after 5 s and 60 s of play");

            UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) { p_Snapshot.status = "Playing."; });
        }
    }
    catch (const std::exception& e) {
        LogError("SetLoadingStage hook failed: {}", e.what());
    }
    catch (...) {
        LogError("SetLoadingStage hook failed: unknown exception");
    }

    // Its own guard, so a throw above does not skip it.
    try {
        m_Merces.OnLoadingStage(stage, th);

        // The scene start's combined stall: the dials walk and the merces table search.
        if (stage == ESceneLoadingStage::eLoading_SceneStarted) {
            const std::optional<double> s_SearchMs = m_Merces.TakeSearchMs();

            if (m_DialsWalkMs || s_SearchMs) {
                const auto s_Text = [](const std::optional<double>& p_Ms) {
                    return p_Ms ? fmt::format("{:.1f} ms", *p_Ms) : std::string("not run");
                };

                LogTo(fr::LogTopic::Missions, "scene start walks: dials {}, merces table search {}, together {:.1f} ms",
                      s_Text(m_DialsWalkMs), s_Text(s_SearchMs),
                      m_DialsWalkMs.value_or(0.0) + s_SearchMs.value_or(0.0));
            }

            m_DialsWalkMs.reset();
        }
    }
    catch (const std::exception& e) {
        LogError("SetLoadingStage merces failed: {}", e.what());
    }
    catch (...) {
        LogError("SetLoadingStage merces failed: unknown exception");
    }

    // Catch everything themselves.
    m_TargetPool.OnLoadingStage(stage, th, m_SceneSeed);
    // After the target pool: it reads the pool as the target pool left it, and waits a stage
    // while the target pool does (so both always see the same possible targets).
    m_Enforcers.OnLoadingStage(stage, th, m_SceneSeed, m_TargetPool.WaitingForLaterStage());
    m_DoorLocks.OnLoadingStage(stage, th, m_SceneSeed);

    // Guard senses: a Freelancer mission rolling AI dials (m_Mode is set by the load's early apply).
    try {
        SensorDials::Plan s_Plan;
        s_Plan.write = m_IsMission && m_Mode == fr::Mode::Dials && m_Config.sensorDials;
        s_Plan.seed = m_SceneSeed ? m_SceneSeed->seed : 0;
        s_Plan.scene = m_Scene;
        s_Plan.roll = m_Config.roll;
        s_Plan.off = m_Config.sensorOff;

        if (m_Sensors.OnLoadingStage(stage, th, s_Plan))
            PublishSensors();
    }
    catch (const std::exception& e) {
        LogError("SetLoadingStage sensor dials failed: {}", e.what());
    }
    catch (...) {
        LogError("SetLoadingStage sensor dials failed: unknown exception");
    }

    m_MoneyItems.OnLoadingStage(stage, th, m_SceneSeed);
    m_CrateGuns.OnLoadingStage(stage, th, m_SceneSeed);
    m_LegalSniper.OnLoadingStage(stage, fr::IsFreelancerScene(m_Type));
    // After the target pool swap: checks the pool as it was left.
    m_Variants.OnLoadingStage(stage, th);
    // After the variants: reads the variant this load applied.
    m_SafeSpots.OnLoadingStage(stage, th, m_Variants.Applied());
    m_CrateLamps.OnLoadingStage(stage, th, m_Variants.Applied());
    m_SickSpots.OnLoadingStage(stage, th);
    m_SupplierWalk.OnLoadingStage(stage, th);
    m_Loadout.OnLoadingStage(stage, th);
    m_Coin.OnLoadingStage(stage, th);
    m_Fishing.OnLoadingStage(stage, th);
    m_Investigations.OnLoadingStage(stage, th, m_SceneSeed);
    m_Prestige.OnLoadingStage(stage, th);
    m_ClientReqs.OnLoadingStage(stage);

    // A watched load (a full map variant's): the mod's part of this stage is over; what follows until the next
    // stage is the game's.
    load_fault::StageHandled(StageName(stage));

    return HookResult<void>(HookAction::Continue());
}

DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, void, ZEntitySceneContext_ClearScene, ZEntitySceneContext* th,
                     bool bFullyUnloadScene) {
    (void)p_Hook; // declared by DEFINE_PLUGIN_DETOUR
    try {
        const std::string s_Scene =
            th ? SceneLabelOf(th->m_SceneInitParameters.m_SceneResource.ToStringView()) : "<no scene context>";
        LogTo(fr::LogTopic::Missions, "clear scene {} (fully unload: {})", s_Scene, bFullyUnloadScene);
        m_Merces.OnSceneClear();
        m_Variants.OnSceneClear();
        m_SafeSpots.OnSceneClear();
        m_CrateLamps.OnSceneClear();
        m_SickSpots.OnSceneClear();
        m_MoneyItems.OnSceneClear();
        m_CrateGuns.OnSceneClear();
        m_LegalSniper.Restore("scene clear");
        m_SupplierWalk.OnSceneClear();
        m_TargetPool.OnSceneClear();
        m_Enforcers.OnSceneClear();
        m_DoorLocks.OnSceneClear();
        m_Sensors.Restore("scene clear");
        PublishSensors();
        m_ClientReqs.OnSceneClear();
        m_Loadout.OnSceneClear();
        m_Coin.OnSceneClear();
        m_Fishing.OnSceneClear();
        m_Investigations.OnSceneClear();
        m_Prestige.OnSceneClear(m_Loadout.LiveOptinObjective());

        // A clear after LoadScene (the engine normally clears the old scene
        // first). LoadScene restored the old scene's values already;
        // any entity value still recorded is restored and forgotten. pro.repo
        // stays written for the pending load. The overlay keeps "Loading...".
        if (m_LoadPending) {
            try {
                if (!m_EntityPatches.Empty()) {
                    LogTo(fr::LogTopic::AiDifficulty, "clear during a pending load: restoring {} entity dial values",
                          m_EntityPatches.Size());
                    RestoreEntityPatches(th ? &th->m_aLoadedBricks : nullptr);
                }
            }
            catch (const std::exception& e) {
                LogError("ClearScene: restoring entity dials failed: {}", e.what());
            }
            catch (...) {
                LogError("ClearScene: restoring entity dials failed: unknown exception");
            }

            // Whatever was restored or not, these rows referred to entities of the cleared scene.
            if (!m_Applied.empty()) {
                m_Applied.clear();
                PublishRows();
            }

            return HookResult<void>(HookAction::Continue());
        }

        // A failed restore must not skip the state reset below.
        try {
            if (!m_EntityPatches.Empty() || !m_RepoPatches.Empty())
                RestorePatches(th ? &th->m_aLoadedBricks : nullptr, "scene clear");
        }
        catch (const std::exception& e) {
            LogError("ClearScene: restoring patches failed: {}", e.what());
        }
        catch (...) {
            LogError("ClearScene: restoring patches failed: unknown exception");
        }

        // The first-start seed is kept even on a full unload: RestartLevel may
        // fully unload before the same scene's LoadScene, and LoadScene of a
        // different scene forgets it (ForgetOtherScenes).
        ResetMapState();
        m_GuardWeapons.OnSceneClear();

        // Until a LoadScene: a restart loads again without one, and pro.repo
        // is then written at eLoading_AssetsLoaded.
        m_ClearedWithoutLoad = true;

        UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) {
            p_Snapshot.status = m_WarnedStageNotReached ? kStageNotReached : "Level unloaded.";
        });
    }
    catch (const std::exception& e) {
        LogError("ClearScene hook failed: {}", e.what());
    }
    catch (...) {
        LogError("ClearScene hook failed: unknown exception");
    }

    return HookResult<void>(HookAction::Continue());
}

// Always added: hands the game's events to the features that watch them.
DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, void, ZAchievementManagerSimple_OnEventSent,
                     ZAchievementManagerSimple* /*th*/, uint32_t eventIndex, const ZDynamicObject& event) {
    (void)p_Hook; // declared by DEFINE_PLUGIN_DETOUR
    // Each catches everything itself.
    if (m_SupplierWalk.On())
        m_SupplierWalk.OnEventSent(eventIndex, event);

    if (m_ClientReqs.On())
        m_ClientReqs.OnEventSent(eventIndex, event);

    m_Loadout.OnEventSent(eventIndex, event);

    if (m_Investigations.EventsWanted())
        m_Investigations.OnEventSent(eventIndex, event);

    if (m_CrateGuns.PickupsWanted())
        m_CrateGuns.OnEventSent(eventIndex, event);

    if (m_Variants.StartEventsWanted())
        m_Variants.OnStartEvent(eventIndex, event);

    if (m_Variants.ExitEventsWanted())
        m_Variants.OnExitEvent(eventIndex, event);

    return HookResult<void>(HookAction::Continue());
}

// Forgets everything that refers to the current scene's entities.
void FreelancerRandomizer::ResetMapState() {
    m_SceneStartedHandled = false;
    m_Applied.clear();
    m_RepoWrites.clear();
    m_RepoApplied = false;
    m_AppliedAt.clear();
    m_Mode = fr::Mode::NotMission;
    m_EntityValues.clear();
    m_ReadBack = {};
    m_SceneSeed.reset();
    m_LateSeed.reset();

    UpdateSnapshot([](OverlaySnapshot& p_Snapshot) {
        p_Snapshot.mode.clear();
        p_Snapshot.appliedAt.clear();
        p_Snapshot.repoSummary.clear();
        p_Snapshot.entityCheck.clear();
        p_Snapshot.seed.reset();
        p_Snapshot.lateSeed.reset();
        p_Snapshot.found = 0;
        p_Snapshot.rows.reset();
        p_Snapshot.valuesWritten = 0;
    });
}

void FreelancerRandomizer::OnSceneStarted(ZEntitySceneContext* p_Context) {
    if (m_SceneStartedHandled) {
        LogTo(fr::LogTopic::Missions,
              "scene start seen again for the same load; ignored so values are not rolled twice");
        return;
    }

    m_SceneStartedHandled = true;
    m_LoadPending = false;

    // The scene context is authoritative for the scene being started; it
    // differs from the LoadScene record only if a load bypassed LoadScene.
    // A full variant's swapped scene counts as the contract's (SceneOf.h, and
    // matched by name below).
    const std::string_view s_LoadedScene = p_Context->m_SceneInitParameters.m_SceneResource.ToStringView();
    const std::string s_ContextScene = SceneOf(p_Context);

    if (!s_ContextScene.empty() && s_ContextScene != m_Scene && s_LoadedScene != m_Variants.SwappedScene()) {
        m_Scene = std::string(s_ContextScene);
        m_Type = std::string(p_Context->m_SceneInitParameters.m_Type.ToStringView());
        m_IsMission = fr::IsFreelancerMission(m_Type, m_Scene);
        LogTo(fr::LogTopic::Missions,
              "scene start without a matching LoadScene call; using the scene context's {} (type \"{}\")", m_Scene,
              m_Type);

        // What was applied before the load was for another scene: applied
        // again below (the patch books keep that from stacking).
        if (m_RepoApplied) {
            LogTo(fr::LogTopic::AiDifficulty,
                  "values applied at {} were for another scene; applying again at scene start", m_AppliedAt);
            m_RepoApplied = false;
            m_AppliedAt.clear();
            m_RepoWrites.clear();
        }
    }

    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    LogTo(fr::LogTopic::Missions, "scene started: {} ({} loaded bricks)",
          IsSwappedScene(s_LoadedScene) ? SceneLabelOf(s_LoadedScene) : m_Scene, s_Bricks.size());

    std::vector<FoundParam> s_Found;
    WalkScene(s_Bricks, s_Found);

    for (const auto& s_Param : s_Found) {
        LogTo(fr::LogTopic::AiDifficulty, "  {} = {} (count {}, toggle {}) matched by {} in brick {:016X}",
              ParamLabel(s_Param.param), OptionalText(s_Param.value), OptionalText(s_Param.count),
              OptionalText(s_Param.toggle), s_Param.matchedBy, s_Param.brickRid);
    }

    const bool s_Walked = fr::IsFreelancerScene(m_Type);

    if (s_Found.empty() && s_Walked)
        LogTo(fr::LogTopic::AiDifficulty, "difficulty parameters not found in loaded bricks");

    if (!m_IsMission)
        m_Mode = fr::Mode::NotMission;
    else if (!m_RepoApplied)
        m_Mode = fr::ChooseMode(m_Config, true);

    UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) {
        p_Snapshot.scene = m_Scene;
        p_Snapshot.type = m_Type;
        p_Snapshot.isMission = m_IsMission;
        p_Snapshot.mode = fr::ModeName(m_Mode);
        p_Snapshot.found = s_Found.size();
        p_Snapshot.status =
            s_Found.empty() && s_Walked ? "No AI difficulty values found in this level." : "Level started.";
    });

    if (!m_IsMission) {
        if (!m_EntityPatches.Empty() || !m_RepoPatches.Empty())
            RestorePatches(&s_Bricks, "non-mission scene start");

        LogTo(fr::LogTopic::Missions, "not a Freelancer mission: no changes");
        return;
    }

    if (m_RepoApplied) {
        // Applied before the scene loaded: nothing is written again. The live
        // seed is read (a seed found late is kept for the next mission), and
        // the freshly built entities are checked against pro.repo.
        LogTo(fr::LogTopic::AiDifficulty, "scene start: values were applied at {} ({}); not written again", m_AppliedAt,
              fr::ModeName(m_Mode));
        CheckLateSeed(s_Bricks, "scene start");
        CheckEntities(s_Found);
        PublishRows();
        return;
    }

    // Fallback: pro.repo could not be written before the scene loaded (no
    // LoadScene or eLoading_AssetsLoaded apply ran). Scene start is likely
    // too late for the entities built from pro.repo, so they are written as well.
    LogTo(fr::LogTopic::AiDifficulty,
          "scene start: pro.repo was not written before the scene loaded; applying at scene start instead "
          "(likely too late for entities built from pro.repo)");
    const fr::SeedResolution s_Seed = ResolveSceneSeed(ReadLiveSeed(s_Bricks), nullptr);
    UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) { p_Snapshot.seed = s_Seed; });

    if (fr::ModeWrites(m_Mode)) {
        if (m_Mode == fr::Mode::Dials && s_Seed.source == fr::SeedSource::Fallback)
            LogTo(fr::LogTopic::Missions,
                  "no seed known yet: rolling dials with the fallback seed 0 so the mission still varies");

        if (ApplyRepo(m_Mode, s_Seed.seed, "scene start (fallback)"))
            m_AppliedAt = "scene start (fallback)";

        ApplyEntities(m_Mode, s_Found, s_Seed.seed);

        if (m_AppliedAt.empty() && !m_Applied.empty())
            m_AppliedAt = "scene start (fallback, entities only)";
    }
    else {
        LogTo(fr::LogTopic::Missions, "AI values: {}; nothing written", fr::ModeName(m_Mode));
    }

    UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) { p_Snapshot.appliedAt = m_AppliedAt; });
    PublishRows();
}

// The fallback's entity writes: each found entity of a parameter the mode
// changes gets the mode's multiplier on its own base.
void FreelancerRandomizer::ApplyEntities(fr::Mode p_Mode, const std::vector<FoundParam>& p_Found, int32_t p_Seed) {
    size_t s_Unreadable = 0;

    for (const auto& s_Param : p_Found) {
        if (!s_Param.param || !s_Param.value) {
            ++s_Unreadable;
            continue;
        }

        const float s_Base = BaseOf(s_Param);
        const auto s_Factor = fr::ModeFactor(p_Mode, m_Config, p_Seed, m_Scene, *s_Param.param, s_Base);

        if (s_Factor)
            Write(s_Param, s_Base, *s_Factor, static_cast<float>(s_Base * *s_Factor));
    }

    const auto [s_Written, s_Failed] = CountWrites(m_Applied);
    LogTo(fr::LogTopic::AiDifficulty, "entities (fallback): {} written, {} write failed, {} unreadable", s_Written,
          s_Failed, s_Unreadable);
}

// The walk fr::BrickMemory::PlanWalk chooses: none outside Freelancer scenes,
// one full walk per mission scene and session, otherwise the remembered bricks
// when any is loaded (falling back to every loaded brick when that finds too
// little). Remembers where parameters were found.
WalkStats FreelancerRandomizer::WalkScene(const TArray<SBrickAllocationInfo>& p_Bricks,
                                          std::vector<FoundParam>& p_Found) {
    std::vector<uint64_t> s_Loaded;
    s_Loaded.reserve(p_Bricks.size());

    for (size_t i = 0; i < p_Bricks.size(); ++i)
        s_Loaded.push_back(p_Bricks[i].m_RuntimeResourceID.GetID());

    const fr::WalkPlan s_Plan = m_ParamBricks.PlanWalk(m_Scene, fr::IsFreelancerScene(m_Type), m_IsMission, s_Loaded);
    WalkStats s_Stats {};

    if (s_Plan.kind == fr::WalkKind::None) {
        if (!m_LoggedNoWalk) {
            m_LoggedNoWalk = true;
            LogTo(fr::LogTopic::AiDifficulty,
                  "scene type \"{}\" is not evergreen: no entity walk in non-Freelancer scenes (logged once)", m_Type);
        }

        return s_Stats;
    }

    bool s_FullWalk = s_Plan.kind == fr::WalkKind::Full;

    if (s_FullWalk && m_IsMission && !m_ParamBricks.LastFullWalk(m_Scene) && !m_ParamBricks.Empty())
        LogTo(fr::LogTopic::AiDifficulty,
              "first start of this mission scene this session: walking all loaded bricks once");

    if (!s_FullWalk) {
        s_Stats = TimedWalk(p_Bricks, p_Found, &s_Plan.bricks);

        if (m_ParamBricks.RememberedWalkShort(m_Scene, p_Found.size())) {
            if (p_Found.empty()) {
                LogTo(fr::LogTopic::AiDifficulty,
                      "remembered bricks gave no difficulty parameters; walking all loaded bricks");
            }
            else {
                LogTo(fr::LogTopic::AiDifficulty,
                      "remembered bricks gave {} difficulty parameters, fewer than the {} of this scene's last full "
                      "walk; walking all loaded bricks",
                      p_Found.size(), m_ParamBricks.LastFullWalk(m_Scene).value_or(0));
            }

            p_Found.clear();
            s_FullWalk = true;
        }
    }

    if (s_FullWalk) {
        s_Stats = TimedWalk(p_Bricks, p_Found, nullptr);
        m_ParamBricks.RecordFullWalk(m_Scene, p_Found.size());
    }

    // Every brick with a match is remembered, not only the first.
    for (const auto& s_Param : p_Found) {
        if (!m_ParamBricks.Contains(s_Param.brickRid)) {
            m_ParamBricks.Remember(s_Param.brickRid);
            LogTo(fr::LogTopic::AiDifficulty,
                  "remembering brick {:016X} for later scenes (holds difficulty parameters)", s_Param.brickRid);
        }
    }

    if (s_FullWalk)
        LogTo(fr::LogTopic::AiDifficulty, "{} bricks with difficulty parameters remembered this session",
              m_ParamBricks.Size());

    return s_Stats;
}

WalkStats FreelancerRandomizer::TimedWalk(const TArray<SBrickAllocationInfo>& p_Bricks,
                                          std::vector<FoundParam>& p_Found, const std::vector<uint64_t>* p_OnlyBricks) {
    const fr::WalkKind s_Kind = p_OnlyBricks ? fr::WalkKind::Remembered : fr::WalkKind::Full;
    // A full walk of a large mission passes 2,000,000 entities.
    const size_t s_Cap = p_OnlyBricks ? kWalkEntityCap : kWideWalkEntityCap;
    const auto s_Start = std::chrono::steady_clock::now();
    const WalkStats s_Stats =
        FindDifficultyParams(p_Bricks, p_Found, p_OnlyBricks, m_ParamBricks.VisitedEstimate(s_Kind), s_Cap);
    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();
    m_ParamBricks.RecordVisited(s_Kind, s_Stats.visited);
    m_DialsWalkMs = m_DialsWalkMs.value_or(0.0) + s_Ms;

    size_t s_MatchedBricks = 0;

    for (const auto& s_Brick : s_Stats.bricks) {
        if (s_Brick.matched == 0 && !s_Brick.faultStep)
            continue;

        s_MatchedBricks += s_Brick.matched > 0 ? 1 : 0;
        LogTo(fr::LogTopic::AiDifficulty, "brick {:016X}: {} entities visited, {} difficulty parameters{}",
              s_Brick.brickRid, s_Brick.visited, s_Brick.matched,
              s_Brick.faultStep ? fmt::format(" (aborted: fault in {})", s_Brick.faultStep) : "");
    }

    LogTo(fr::LogTopic::AiDifficulty,
          "entity walk ({}): {} bricks walked, {} with difficulty parameters, {} entities visited, {} difficulty "
          "parameters, {} faults{}, {:.1f} ms",
          p_OnlyBricks ? "remembered bricks only" : "all loaded bricks", s_Stats.bricks.size(), s_MatchedBricks,
          s_Stats.visited, s_Stats.matched, s_Stats.faults,
          s_Stats.capped ? fmt::format(", stopped at the {}-entity cap", s_Cap) : std::string(), s_Ms);

    if (s_Ms > kSlowWalkMs) {
        LogTo(fr::LogTopic::AiDifficulty, "warning: entity walk took {:.0f} ms (over {:.0f} ms); {}", s_Ms, kSlowWalkMs,
              s_Stats.capped ? fmt::format("it stopped at the {}-entity cap", s_Cap)
                             : std::string("it ran to the end"));
    }

    return s_Stats;
}

// p_Live is the live seed entity's value, or empty with p_LiveNote saying why
// it was not read (before the scene loads, the seed entity does not exist).
fr::SeedResolution FreelancerRandomizer::ResolveSceneSeed(const std::optional<int32_t>& p_Live,
                                                          const char* p_LiveNote) {
    const auto s_Persisted = ReadPersistedSeed();

    // Every seed source side by side, before anything is persisted.
    LogTo(fr::LogTopic::Missions, "seed sources: live {}, generated {}, persisted {} (0 means unknown)",
          p_LiveNote ? std::string(p_LiveNote) : OptionalText(p_Live), OptionalText(m_GeneratedSeed),
          OptionalText(s_Persisted));

    if (const auto s_First = m_FirstSeeds.Find(m_Scene)) {
        m_SceneSeed = *s_First;
        LogTo(fr::LogTopic::Missions, "reusing seed {} from this scene's first load (source {})", s_First->seed,
              fr::SeedSourceName(s_First->source));
        return *s_First;
    }

    const fr::SeedResolution s_Resolution = fr::ResolveSeed(p_Live, m_GeneratedSeed, s_Persisted);
    m_SceneSeed = s_Resolution;
    m_FirstSeeds.Remember(m_Scene, s_Resolution);

    if (s_Resolution.source == fr::SeedSource::Fallback) {
        LogTo(fr::LogTopic::Missions, "no known seed (every source missing or 0): seed {} ({}); nothing persisted",
              s_Resolution.seed, fr::SeedSourceName(s_Resolution.source));
    }
    else if (fr::ShouldPersist(s_Resolution)) {
        PersistSeed(s_Resolution.seed);
        LogTo(fr::LogTopic::Missions, "seed {} (source {}); persisted", s_Resolution.seed,
              fr::SeedSourceName(s_Resolution.source));
    }
    else {
        LogTo(fr::LogTopic::Missions, "seed {} (source {})", s_Resolution.seed,
              fr::SeedSourceName(s_Resolution.source));
    }

    return s_Resolution;
}

// Reads the live seed entity again after scene start. A live seed that differs
// from a non-live seed used at scene start (persisted, generated or the
// fallback 0) is persisted for the next mission; when the live seed itself was
// used at scene start, a difference is only logged.
void FreelancerRandomizer::CheckLateSeed(const TArray<SBrickAllocationInfo>& p_Bricks, const char* p_Label) {
    const auto s_Live = ReadLiveSeed(p_Bricks);
    LogTo(fr::LogTopic::Missions, "live seed at {}: {}", p_Label, OptionalText(s_Live));

    // The weapon rarities' campaign: bound to (or, for another campaign, rolled from) the mission's live seed.
    if (m_IsMission && fr::IsKnownSeed(s_Live))
        OnRarityMission(*s_Live, true, p_Label);

    if (!m_SceneSeed)
        return;

    switch (fr::DecideLateSeed(*m_SceneSeed, s_Live)) {
        case fr::LateSeed::FoundLate:
            if (m_LateSeed != s_Live) {
                m_LateSeed = s_Live;
                LogTo(fr::LogTopic::Missions,
                      "new seed found late: {} (was {} from {}; will be used from the next mission)", *s_Live,
                      m_SceneSeed->seed, fr::SeedSourceName(m_SceneSeed->source));
                PersistSeed(*s_Live);
                LogTo(fr::LogTopic::Missions, "persisted late seed {}", *s_Live);
                // The next mission of this session too, not the generated seed.
                AdoptCampaignSeed(*s_Live, m_SceneSeed->source == fr::SeedSource::Generated
                                               ? std::optional(m_SceneSeed->seed)
                                               : std::nullopt);
                UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) { p_Snapshot.lateSeed = s_Live; });
            }
            break;
        case fr::LateSeed::Differs:
            LogTo(fr::LogTopic::Missions,
                  "live seed {} differs from the seed {} ({}) used at scene start; this mission keeps its rolls",
                  *s_Live, m_SceneSeed->seed, fr::SeedSourceName(m_SceneSeed->source));
            break;
        case fr::LateSeed::None: break;
    }
}

// The value to roll from: the entity's current value, or its original base
// when it still holds what this mod wrote and has not restored (a missed
// restore, or a re-apply at scene start).
float FreelancerRandomizer::BaseOf(const FoundParam& p_Found) {
    const fr::PatchKey s_Key {reinterpret_cast<uintptr_t>(p_Found.entity.GetEntity()), 0};

    if (!m_EntityPatches.WouldRestoreBase(s_Key, *p_Found.param, *p_Found.value))
        return *p_Found.value;

    const auto s_Base = static_cast<float>(m_EntityPatches.BaseFor(s_Key, *p_Found.param, *p_Found.value));
    LogTo(fr::LogTopic::AiDifficulty, "{}: base restored (was rolled previously): now {:.9g}, base {:.9g}",
          ParamLabel(p_Found.param), *p_Found.value, s_Base);
    return s_Base;
}

void FreelancerRandomizer::Write(const FoundParam& p_Found, float p_Base, double p_Multiplier, float p_Value) {
    ZEntityRef s_Entity = p_Found.entity; // SetProperty is not const
    const bool s_Ok = s_Entity.SetProperty("m_fValue", ZObjectRef::From<float>(p_Value));
    const char* s_Status = s_Ok ? "written" : "write failed";

    if (s_Ok) {
        m_EntityPatches.Record({reinterpret_cast<uintptr_t>(p_Found.entity.GetEntity()), 0}, *p_Found.param, p_Base,
                               p_Value);
    }

    LogTo(fr::LogTopic::AiDifficulty, "{}: entity base {:.9g} x{:.4f} -> {:.9g} {}", ParamLabel(p_Found.param), p_Base,
          p_Multiplier, p_Value, s_Status);
    m_Applied.push_back(
        {p_Found.entity, *p_Found.param, p_Base, p_Multiplier, p_Value, s_Ok, s_Status, false, p_Found.repoId});
}

void FreelancerRandomizer::ReadBack(const char* p_Label) {
    size_t s_Held = 0;
    size_t s_Reset = 0;
    size_t s_Unreadable = 0;

    for (Applied& s_Applied : m_Applied) {
        if (!s_Applied.writeOk)
            continue;

        if (s_Applied.readBackStopped) {
            ++s_Unreadable;
            continue;
        }

        const RawRead32 s_Now = ReadPropertyRaw32(s_Applied.entity, "m_fValue");

        if (s_Now.status == RawRead32::Status::Fault) {
            ++s_Unreadable;
            s_Applied.readBackStopped = true;
            s_Applied.status =
                fmt::format("fault (exception 0x{:08X}), read-back stopped ({})", s_Now.faultCode, p_Label);
        }
        else if (s_Now.status != RawRead32::Status::Ok) {
            ++s_Unreadable;
            s_Applied.status = fmt::format("unreadable ({})", p_Label);
        }
        else if (const float s_Value = std::bit_cast<float>(s_Now.bits); s_Value == s_Applied.written) {
            ++s_Held;
            s_Applied.status = fmt::format("held ({})", p_Label);
        }
        else {
            ++s_Reset;
            s_Applied.status = fmt::format("reset by game (now {:.9g}) ({})", s_Value, p_Label);
        }

        LogTo(fr::LogTopic::AiDifficulty, "read-back {}: {} entity wrote {:.9g}: {}", p_Label,
              ParamLabel(s_Applied.param), s_Applied.written, s_Applied.status);
    }

    // The pro.repo entries, in one lookup pass.
    std::vector<fr::Guid> s_RepoKeys;
    s_RepoKeys.reserve(m_RepoWrites.size());

    for (const auto& s_Write : m_RepoWrites)
        s_RepoKeys.push_back(s_Write.guid);

    std::vector<RepoLookup> s_Repo;
    std::string s_RepoWhy;
    const bool s_RepoReady = !s_RepoKeys.empty() && m_Repo.Ready(s_RepoWhy);

    if (s_RepoReady) {
        if (const uint32_t s_Code = m_Repo.Lookup(s_RepoKeys, s_Repo))
            s_RepoWhy = fmt::format("lookup faulted (exception 0x{:08X})", s_Code);
    }

    size_t s_RepoHeld = 0;
    size_t s_RepoChanged = 0;
    size_t s_RepoUnreadable = 0;

    for (size_t i = 0; i < m_RepoWrites.size(); ++i) {
        RepoWrite& s_Write = m_RepoWrites[i];
        const RepoLookup* s_Now = s_RepoReady && i < s_Repo.size() ? &s_Repo[i] : nullptr;

        if (!s_Now || !RepoUnwritableReason(*s_Now).empty()) {
            ++s_RepoUnreadable;
            s_Write.status =
                fmt::format("unreadable: {} ({})", s_Now ? RepoUnwritableReason(*s_Now) : s_RepoWhy, p_Label);
        }
        else if (s_Now->value == s_Write.written) {
            ++s_RepoHeld;
            s_Write.status = fmt::format("held ({})", p_Label);
        }
        else {
            ++s_RepoChanged;
            s_Write.status = fmt::format("changed (now {:.9g}) ({})", s_Now->value, p_Label);
        }

        if (s_Write.status.rfind("held", 0) != 0) {
            LogTo(fr::LogTopic::AiDifficulty, "read-back {}: {} pro.repo {} wrote {:.9g}: {}", p_Label,
                  ParamLabel(s_Write.param), fr::FormatGuid(s_Write.guid), s_Write.written, s_Write.status);
        }
    }

    LogTo(fr::LogTopic::AiDifficulty,
          "read-back {}: pro.repo: {} held, {} changed, {} unreadable; entities: {} held, {} reset by game, {} "
          "unreadable",
          p_Label, s_RepoHeld, s_RepoChanged, s_RepoUnreadable, s_Held, s_Reset, s_Unreadable);
    PublishRows();
    UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) {
        const size_t s_RepoTotal = s_RepoHeld + s_RepoChanged + s_RepoUnreadable;
        p_Snapshot.readBack =
            fmt::format("AI values still in place after {} of play: {} of {}{}", p_Label, s_RepoHeld, s_RepoTotal,
                        m_Applied.empty() ? std::string()
                                          : fmt::format("; copies in this level: {} of {} kept", s_Held,
                                                        s_Held + s_Reset + s_Unreadable));
    });
}

std::vector<fr::DialEntry> FreelancerRandomizer::DialEntries() const {
    std::vector<fr::DialEntry> s_Entries;
    s_Entries.reserve(m_RepoWrites.size() + m_Applied.size());

    // Tier and collection parameter from the pro.repo table (DialRows.h); both empty for an entry it does not know.
    const auto s_Known = [](const std::optional<fr::Guid>& p_Guid) {
        return p_Guid ? fr::FindRepoEntry(*p_Guid) : std::nullopt;
    };

    for (const auto& s_Write : m_RepoWrites) {
        const auto s_Info = s_Known(s_Write.guid);
        // The GUID's first 8 characters are enough to find it in the log.
        s_Entries.push_back({s_Write.param, "table entry " + fr::FormatGuid(s_Write.guid).substr(0, 8),
                             s_Info ? std::optional(s_Info->tiers) : std::nullopt,
                             s_Info ? s_Info->feeds : std::nullopt, s_Write.base, s_Write.multiplier, s_Write.written,
                             s_Write.status});
    }

    for (const auto& s_Applied : m_Applied) {
        // The entity is the collection's entity, so it feeds its own parameter.
        const auto s_Info = s_Known(s_Applied.repoId);
        s_Entries.push_back({s_Applied.param, "level copy", s_Info ? std::optional(s_Info->tiers) : std::nullopt,
                             s_Applied.param, s_Applied.base, s_Applied.multiplier, s_Applied.written,
                             s_Applied.status});
    }

    return s_Entries;
}

void FreelancerRandomizer::PublishRows() {
    const std::vector<fr::DialEntry> s_Entries = DialEntries();
    auto s_Rows = std::make_shared<std::vector<OverlayRow>>();

    // The parameters a written sensor dial that still holds stands in for: their
    // AI dial rows (no professional entry) give way to the sensor rows.
    std::vector<int32_t> s_Covered;

    for (const SensorDials::Row& s_Sensor : m_Sensors.Rows()) {
        if (const auto s_Param = fr::SensorDialParam(*s_Sensor.dial); s_Param && s_Sensor.ok && s_Sensor.held)
            s_Covered.push_back(*s_Param);
    }

    for (const fr::DialSummary& s_Summary : fr::SummarizeDials(s_Entries)) {
        const fr::DialRowView s_View = fr::ViewDialRow(s_Summary, s_Covered);

        if (!s_View.shown)
            continue;

        const fr::DialInfo* s_Info = fr::FindDialInfo(s_Summary.param);
        const fr::DialSpec* s_Dial = fr::FindDial(s_Summary.param);
        OverlayRow s_Row {
            fr::DialDisplayName(s_Summary.param),
            ParamLabel(s_Summary.param),
            s_Info,
            s_Dial ? s_Dial->dir : int8_t {0},
            s_Summary.freelancer,
            s_View.noEffect,
            fr::FreelancerValueText(s_Summary, false),
            s_Info ? std::string(fr::UnitLabel(s_Info->unit)) : std::string(),
            s_Info && s_Info->unitBasis == fr::Basis::Inferred,
            s_Summary.multipliersDiffer ? std::string("varies") : fmt::format("x{:.4f}", s_Summary.multiplier),
            s_Summary.status,
            {},
            fr::RateDial(s_Summary.param, s_Summary.multiplier, m_Config.roll),
        };

        for (const fr::DialEntry& s_Entry : s_Summary.entries) {
            s_Row.details.push_back(
                fmt::format("{}: {} {:.9g} -> {:.9g} x{:.4f}, {}",
                            s_Entry.tiers ? fr::RepoTierLabel(*s_Entry.tiers) : "difficulty unknown", s_Entry.copy,
                            s_Entry.base, s_Entry.written, s_Entry.multiplier, s_Entry.status));
        }

        s_Rows->push_back(std::move(s_Row));
    }

    // Guard senses: one row per sensor dial written this load.
    for (const SensorDials::Row& s_Sensor : m_Sensors.Rows()) {
        const fr::DialInfo& s_Info = s_Sensor.dial->info;
        OverlayRow s_Row {
            std::string(s_Info.name),
            fr::SensorDialLabel(*s_Sensor.dial),
            &s_Info,
            s_Sensor.dial->dir,
            fr::FreelancerValue::Written,
            !s_Sensor.ok || !s_Sensor.held,
            fr::FormatDialValues(s_Sensor.base, s_Sensor.written, s_Info.unit),
            std::string(fr::UnitLabel(s_Info.unit)),
            s_Info.unitBasis == fr::Basis::Inferred,
            fmt::format("x{:.4f}", s_Sensor.multiplier),
            s_Sensor.status,
            {fmt::format("guard sense: found {:.9g}, game value {:.9g}, new value {:.9g} x{:.4f}, {}", s_Sensor.found,
                         s_Sensor.base, s_Sensor.written, s_Sensor.multiplier, s_Sensor.status)},
            fr::RateMultiplier(s_Sensor.multiplier, s_Sensor.dial->dir, m_Config.roll),
        };
        s_Rows->push_back(std::move(s_Row));
    }

    UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) {
        p_Snapshot.rows = std::move(s_Rows);
        p_Snapshot.valuesWritten = s_Entries.size();
        p_Snapshot.bands = fr::BandsFor(m_Config.roll);
    });
}

void FreelancerRandomizer::PublishSensors() {
    UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) { p_Snapshot.sensorSummary = m_Sensors.Summary(); });
    PublishRows();
}

void FreelancerRandomizer::OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent) {
    try {
        m_Merces.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());
        m_Variants.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());
        m_SafeSpots.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());
        m_CrateLamps.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());
        m_MoneyItems.Tick();
        m_CrateGuns.Tick();
        m_LegalSniper.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());
        m_DoorLocks.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());

        if (m_Sensors.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds()))
            PublishSensors();

        m_SupplierWalk.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());
        m_ClientReqs.Tick();
        m_Loadout.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());
        m_Coin.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());
        m_Fishing.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());
        m_Investigations.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());
        m_Prestige.Tick(p_UpdateEvent.m_GameTimeDelta.ToSeconds());

        if (!m_ReadBack.armed)
            return;

        m_ReadBack.elapsed += p_UpdateEvent.m_GameTimeDelta.ToSeconds();

        const auto s_Check = [this](const char* p_Label) {
            if (m_ReadBack.checkSeed) {
                if (const ZEntitySceneContext* s_Context = CurrentSceneContext())
                    CheckLateSeed(s_Context->m_aLoadedBricks, p_Label);
            }

            if (m_ReadBack.readValues)
                ReadBack(p_Label);
        };

        if (!m_ReadBack.did5 && m_ReadBack.elapsed >= 5.0) {
            m_ReadBack.did5 = true;
            s_Check("5 s");
        }

        if (!m_ReadBack.did60 && m_ReadBack.elapsed >= 60.0) {
            m_ReadBack.did60 = true;
            m_ReadBack.armed = false;
            s_Check("60 s");
        }
    }
    catch (const std::exception& e) {
        m_ReadBack.armed = false;
        LogError("frame update failed: {}; read-back stopped", e.what());
    }
    catch (...) {
        m_ReadBack.armed = false;
        LogError("frame update failed: unknown exception; read-back stopped");
    }
}

// The Status tab's Random values section: the AI values this mission rolled, wildest first by default.
void FreelancerRandomizer::DrawStatusDials(const OverlaySnapshot& p_Snapshot) {
    StatusHeading("Random values");

    if (!p_Snapshot.rows || p_Snapshot.rows->empty()) {
        StatusNone("Nothing rolled yet: the AI values this Freelancer mission rolled show here.");
        return;
    }

    // No ScrollY: the table is as tall as its rows and the window scrolls them (a
    // scrolling table under content taller than the window gets no height).
    constexpr ImGuiTableFlags s_TableFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

    bool s_ExtremeFirst = m_ExtremeFirst;

    if (ImGui::Checkbox("Extreme first", &s_ExtremeFirst) && m_ExtremeFirstDebounce.Accept(ImGui::GetTime()))
        m_ExtremeFirst = s_ExtremeFirst;

    ImGui::SameLine();
    WrappedTextAvail("!! extreme  ! notable. Tap Watch for the colors, a row for what it does.");

    // Pointers into the shared rows, in display order: rows whose roll has no effect go last.
    std::vector<const OverlayRow*> s_Order;

    for (const OverlayRow& s_Row : *p_Snapshot.rows)
        s_Order.push_back(&s_Row);

    std::stable_sort(s_Order.begin(), s_Order.end(), [this](const OverlayRow* a, const OverlayRow* b) {
        if (a->noEffect != b->noEffect)
            return b->noEffect;

        return m_ExtremeFirst && fr::WilderFirst(a->rating, b->rating);
    });

    if (ImGui::BeginTable("##Dials", 6, s_TableFlags)) {
        const float s_Padding = 2.0f * ImGui::GetStyle().CellPadding.x;
        const auto s_Fit = [s_Padding](std::initializer_list<const char*> p_Texts) {
            float s_Width = 0.0f;

            for (const char* s_Text : p_Texts)
                s_Width = std::max(s_Width, ImGui::CalcTextSize(s_Text).x);

            return s_Width + s_Padding;
        };

        // Fixed columns sized to their content, so the stretch columns get the rest even at
        // 512 px. The stretch columns wrap their text at spaces; a status longer than its
        // column is shortened with "..." (the whole entry is in the row's tooltip).
        ImGui::TableSetupColumn("Watch", ImGuiTableColumnFlags_WidthFixed, s_Fit({kWidestWatch, kNoEffect}));
        ImGui::TableSetupColumn(kDialHeader, ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Freelancer value", ImGuiTableColumnFlags_WidthStretch, 4.0f);
        ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed,
                                s_Fit({"Unit", "attention " ICON_MD_HELP_OUTLINE}));
        ImGui::TableSetupColumn("Multiplier", ImGuiTableColumnFlags_WidthFixed,
                                s_Fit({"Multiplier", "x0.0000", "varies"}));
        ImGui::TableSetupColumn("Kept?", ImGuiTableColumnFlags_WidthFixed, s_Fit({"Kept?", "held (60 s)"}));

        // TableHeadersRow by hand, so headers can explain themselves (a tap hovers on the Deck).
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

        for (int i = 0; i < ImGui::TableGetColumnCount(); ++i) {
            ImGui::TableSetColumnIndex(i);
            ImGui::TableHeader(ImGui::TableGetColumnName(i));

            if (i == 0 && ImGui::IsItemHovered())
                DrawDialLegend(p_Snapshot.bands);
            else if (i == 1 && ImGui::IsItemHovered())
                ImGui::SetTooltip("Tap a value's name for what it does, or the rest of its row for "
                                  "every copy the mod changed.");
            else if (i == 3 && ImGui::IsItemHovered())
                ImGui::SetTooltip(ICON_MD_HELP_OUTLINE " marks a unit the mod worked out, not one the game confirmed.");
            else if (i == 5 && ImGui::IsItemHovered())
                ImGui::SetTooltip("Whether the game still had the new value when the mod checked.");
        }

        for (size_t i = 0; i < s_Order.size(); ++i) {
            const OverlayRow& s_Row = *s_Order[i];
            const ImVec4* s_Color =
                s_Row.noEffect ? &ImGui::GetStyle().Colors[ImGuiCol_TextDisabled] : DialRowColor(s_Row.rating);

            if (s_Color)
                ImGui::PushStyleColor(ImGuiCol_Text, *s_Color);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // One selectable spans the row, so a tap anywhere on it hovers; the column says which tooltip.
            ImGui::PushID(static_cast<int>(i));
            ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
            // Not while a drag that started on the row scrolls the window.
            const bool s_RowHovered = ImGui::IsItemHovered() && !m_Window.Dragging();
            ImGui::PopID();
            ImGui::SameLine(0.0f, 0.0f);

            if (s_Row.noEffect)
                ImGui::TextUnformatted(kNoEffect);
            else if (s_Row.rating.level == fr::DialLevel::Extreme)
                ImGui::Text(ICON_MD_WARNING " %s", fr::DialMarker(s_Row.rating).c_str());
            else
                ImGui::TextUnformatted(fr::DialMarker(s_Row.rating).c_str());

            ImGui::TableNextColumn();
            WrappedTextAvail(s_Row.name);
            ImGui::TableNextColumn();
            WrappedTextAvail(s_Row.value);
            ImGui::TableNextColumn();
            WrappedTextAvail(fmt::format("{}{}", s_Row.unit.empty() ? "count" : s_Row.unit,
                                         s_Row.unitInferred ? " " ICON_MD_HELP_OUTLINE : ""));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s_Row.multiplier.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FitText(s_Row.status, ImGui::GetContentRegionAvail().x).c_str());

            if (s_Color)
                ImGui::PopStyleColor();

            // After the pop, so tooltips use the default text color.
            if (s_RowHovered) {
                const int s_Column = ImGui::TableGetHoveredColumn();

                if (s_Column <= 1)
                    DrawDialAbout(s_Row);
                else
                    DrawDialDetails(s_Row.details);
            }
        }

        ImGui::EndTable();
    }
}

void FreelancerRandomizer::OnDrawMenu() {
    if (ImGui::Button(ICON_MD_CASINO " FL RANDOMIZER") && m_MenuButtonDebounce.Accept(ImGui::GetTime()))
        m_ShowWindow = !m_ShowWindow;
}

// backup_guard_notice: "Backup is on the way" (a guard answered the backup;
// a civilian's answer shows nothing) at the top of the screen
// for a few seconds, menu open or not (render thread; the feature counts the
// notices on the game thread).
void FreelancerRandomizer::DrawBackupNotice() {
    const uint32_t s_Serial = m_Investigations.NoticeSerial();
    const double s_Now = ImGui::GetTime();

    if (s_Serial != m_BackupNoticeSeen) {
        m_BackupNoticeSeen = s_Serial;
        m_BackupNoticeText = m_Investigations.NoticeText();
        m_BackupNoticeUntil = s_Now + fr::kBackupNoticeSeconds;
    }

    if (s_Now >= m_BackupNoticeUntil || m_BackupNoticeText.empty())
        return;

    const ImGuiViewport* s_Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(s_Viewport->Pos.x + s_Viewport->Size.x * 0.5f, s_Viewport->Pos.y + s_Viewport->Size.y * 0.12f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.6f);
    constexpr ImGuiWindowFlags s_Flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize |
                                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##fr_backup_notice", nullptr, s_Flags))
        ImGui::TextUnformatted(m_BackupNoticeText.data(), m_BackupNoticeText.data() + m_BackupNoticeText.size());

    ImGui::End();
}

void FreelancerRandomizer::OnDrawUI(bool p_HasFocus) {
    DrawBackupNotice();

    if (!p_HasFocus || !m_ShowWindow)
        return;

    OverlaySnapshot s_Snapshot;
    {
        std::lock_guard s_Lock(m_SnapshotMutex);
        s_Snapshot = m_Snapshot;
    }

    const ImVec2 s_Viewport = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowSize(ImVec2(s_Viewport.x * 0.5f, s_Viewport.y * 0.8f), ImGuiCond_FirstUseEver);

    // Touch: drag the body to scroll, a wide scrollbar (see TouchScroll).
    if (m_Window.Begin(ICON_MD_CASINO " Freelancer Randomizer", &m_ShowWindow)) {
        // Touch-safe tabs (see OverlayTab).
        if (OverlayTab(ICON_MD_INFO_OUTLINE " Status", !m_SettingsTab))
            m_SettingsTab = false;

        ImGui::SameLine();

        if (OverlayTab(ICON_MD_SETTINGS " Settings", m_SettingsTab))
            m_SettingsTab = true;

        ImGui::Separator();

        if (!m_SettingsTab) {
            WrappedTextAvail("This mod is fan-made and not affiliated with or endorsed by IO Interactive. By using "
                             "it, you agree that you are doing so of your own free will and at your own risk.");
            ImGui::Separator();

            if (!s_Snapshot.enabled) {
                WrappedTextAvail(fmt::format("The mod is off. To turn it on, check \"Mod enabled\" in the Settings "
                                             "tab (or set enabled = true under [settings] in {}), then restart the "
                                             "game.",
                                             kSettingsFile));
            }
            else {
                // Sections as in the Settings tab: headings that never fold, in the order a player checks
                // them (this mission, what the mod rolled, the money, then the mod's own bookkeeping); the
                // only fold-outs are the groups inside (each objective's payout, the common objectives).
                StatusHeading("This mission");

                if (!s_Snapshot.status.empty())
                    WrappedTextAvail(s_Snapshot.status);

                if (!s_Snapshot.scene.empty()) {
                    WrappedTextAvail(s_Snapshot.isMission ? "Freelancer mission: yes" : "Freelancer mission: no");
                    m_Variants.DrawOverlay();

                    if (s_Snapshot.seed)
                        WrappedTextAvail(fmt::format("Campaign seed: {}", s_Snapshot.seed->seed));

                    if (s_Snapshot.lateSeed)
                        WrappedTextAvail(
                            fmt::format("New campaign seed: {} (used from the next mission)", *s_Snapshot.lateSeed));
                }

                DrawStatusDials(s_Snapshot);

                StatusHeading("Payouts & objectives");

                if (!m_Merces.HasOverlay() && !m_ClientReqs.HasOverlay())
                    StatusNone("Nothing yet: payouts show in a Freelancer mission, the common objectives in the "
                               "safehouse.");

                m_Merces.DrawOverlay();
                m_ClientReqs.DrawOverlay();

                if (!s_Snapshot.scene.empty()) {
                    // The mod's own bookkeeping, last.
                    StatusHeading("Troubleshooting");
                    WrappedTextAvail(fmt::format("Level: {}", s_Snapshot.scene));

                    // A full variant loads its own level in place of this one.
                    if (const auto s_Alias = CurrentSceneAlias(); s_Alias && s_Alias->logical == s_Snapshot.scene)
                        WrappedTextAvail(fmt::format("Loaded as: {} ({})", s_Alias->actual, s_Alias->name));

                    if (!s_Snapshot.mode.empty())
                        WrappedTextAvail(fmt::format("Mode: {}", s_Snapshot.mode));

                    if (!s_Snapshot.appliedAt.empty())
                        WrappedTextAvail(fmt::format("Changes made at: {}", s_Snapshot.appliedAt));

                    if (!s_Snapshot.readBack.empty())
                        WrappedTextAvail(s_Snapshot.readBack);

                    if (!s_Snapshot.repoSummary.empty())
                        WrappedTextAvail(s_Snapshot.repoSummary);

                    if (!s_Snapshot.entityCheck.empty())
                        WrappedTextAvail(s_Snapshot.entityCheck);

                    if (!s_Snapshot.sensorSummary.empty())
                        WrappedTextAvail(s_Snapshot.sensorSummary);

                    if (s_Snapshot.seed)
                        WrappedTextAvail(fmt::format("Seed source: {}", fr::SeedSourceName(s_Snapshot.seed->source)));

                    WrappedTextAvail(fmt::format("AI difficulty objects found: {}; copies changed: {} ({} values)",
                                                 s_Snapshot.found, s_Snapshot.valuesWritten,
                                                 s_Snapshot.rows ? s_Snapshot.rows->size() : size_t {0}));
                }
            }
        }
        else {
            // Full variants that can't be picked: one note per reason, nothing to tap (a mod update fixes the
            // ones the game's files can't build; the log says what is in the way of the others).
            const auto s_AnyIs = [&](fr::FullAvailability p_Why) {
                return std::any_of(
                    fr::FullVariants().begin(), fr::FullVariants().end(), [&](const fr::FullVariant* p_Full) {
                        return !p_Full->option.empty() && m_LandslidePackage.Availability(*p_Full) == p_Why;
                    });
            };

            if (s_AnyIs(fr::FullAvailability::NotBuildable)) {
                WrappedTextAvail("Some map variants don't work with your version of the game yet; they're marked "
                                 "(unavailable) under Maps > Map variants.");
                ImGui::Separator();
            }

            if (s_AnyIs(fr::FullAvailability::NotInstalled)) {
                WrappedTextAvail("Some map variants couldn't be installed (the log says why); they're marked "
                                 "(unavailable) under Maps > Map variants.");
                ImGui::Separator();
            }

            // A full variant turned off by a load that did not finish: one tap
            // tries it again at its next load.
            for (const fr::FullVariant* s_Full : fr::FullVariants()) {
                if (!m_LandslidePackage.TurnedOff(*s_Full))
                    continue;

                ImGui::PushID(s_Full->key.data(), s_Full->key.data() + s_Full->key.size());

                const std::string s_Note(fr::FullTurnedOffNote(*s_Full));
                WrappedTextAvail(s_Note);

                const std::string s_Label = fmt::format(ICON_MD_REPLAY " Try {} again", s_Full->display);

                if (ImGui::Button(s_Label.c_str()) && m_RearmDebounce.Accept(ImGui::GetTime()))
                    m_LandslidePackage.Rearm(*s_Full);

                ImGui::PopID();
                ImGui::Separator();
            }

            // Every [settings] key, saved at once; used from the next scene load.
            m_SettingsMenu.Draw(s_Snapshot.enabled);
        }
    }

    m_Window.End();
}

DEFINE_ZHM_PLUGIN(FreelancerRandomizer)
