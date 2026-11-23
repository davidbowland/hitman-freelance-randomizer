#include "DialInfo.h"

#include "Dials.h"
#include "ParamNames.h"

#include <Glacier/Enums.h>

#include <cmath>
#include <cstdio>

namespace fr {

namespace {

constexpr Basis D = Basis::Documented;
constexpr Basis N = Basis::Named;
constexpr Basis I = Basis::Inferred;

// Sources. Values: pro.repo.
// Death Awaits: the mod's Nexus description (nexusmods.com/hitman3/mods/592), one
//   comment per parameter it changes, e.g. "EDP_AIAttentionGainMultiplier 0.75 > Much more attentive."
// Hearing table: Hitman Forum "Guide for Hitman cheat sheets", post 13 "Hitman AI Hearing Ranges"
//   (credited to BluntsNBeatz); its professional/casual/master values match pro.repo exactly, and its
//   noise-range chart is drawn in meters.
// Spartan: the Spartan mod's Nexus description (nexusmods.com/hitman3/mods/1203): on vanilla master
//   "guards search for about 60 seconds before giving up" (EDP_ManhuntGroupMaxSearchTime is 60 there).
constexpr std::string_view kDA = "Death Awaits notes (Nexus 592)";
constexpr std::string_view kHT = "Hitman Forum hearing table";
constexpr std::string_view kHTDA = "Hitman Forum hearing table; Death Awaits notes (Nexus 592)";
constexpr std::string_view kSP = "Spartan notes (Nexus 1203)";
constexpr std::string_view kNone = "";

#define FR_INFO(param, name, unit, unitBasis, description, basis, source)                                              \
    DialInfo {                                                                                                         \
        static_cast<int32_t>(EDifficultyParameter::param), name, DialUnit::unit, unitBasis, description, basis, source \
    }

// Dial-table order. Every description is one line in plain English; Inferred
// ones are readings of the name and values, not facts.
constexpr DialInfo kInfo[] = {
    // Hearing: how far away (meters) an NPC hears the sound.
    FR_INFO(EDP_HearRangeBulletImpact, "Bullet impact hearing range", Meters, D,
            "How far away an NPC hears a bullet hit a surface.", D, kHTDA),
    FR_INFO(EDP_HearRangeBulletImpactOnBody, "Bullet-in-body hearing range", Meters, D,
            "How far away an NPC hears a bullet hit a body.", D, kHTDA),
    FR_INFO(EDP_HearRangeCloseCombatBreakNeck, "Neck snap hearing range", Meters, D,
            "How far away an NPC hears 47 snap a neck.", D, kHTDA),
    FR_INFO(EDP_HearRangeCloseCombatChoke, "Choke hearing range", Meters, D,
            "How far away an NPC hears 47 choke someone out.", D, kHTDA),
    FR_INFO(EDP_HearRangeCloseCombatFiberWire, "Fiber wire hearing range", Meters, D,
            "How far away an NPC hears a fiber wire kill.", D, kHTDA),
    FR_INFO(EDP_HearRangeCloseCombatHitFromBehind, "Blunt hit hearing range", Meters, N,
            "How far away an NPC hears 47 hit someone from behind with a blunt object.", D, kDA),
    FR_INFO(EDP_HearRangeCloseCombatL1, "Close combat hearing range 1", Meters, D,
            "How far away an NPC hears the quietest level of a close-combat fight.", D, kHTDA),
    FR_INFO(EDP_HearRangeCloseCombatL2, "Close combat hearing range 2", Meters, N,
            "How far away an NPC hears the middle level of a close-combat fight (punching and kicking).", D, kDA),
    FR_INFO(EDP_HearRangeCloseCombatL3, "Close combat hearing range 3", Meters, N,
            "How far away an NPC hears the loudest level of a close-combat fight.", D, kDA),
    FR_INFO(EDP_HearRangeEventAccident, "Accident hearing range", Meters, N,
            "How far away an NPC hears an accident happen.", I, kNone),
    FR_INFO(EDP_HearRangeEventAlarm, "Alarm hearing range", Meters, N, "How far away an NPC hears an alarm go off.", I,
            kNone),
    FR_INFO(EDP_HearRangeEventExplosion, "Explosion hearing range", Meters, N,
            "How far away an NPC hears an explosion.", I, kNone),
    FR_INFO(EDP_HearRangeEventRadio, "Radio hearing range", Meters, N, "How far away an NPC hears a radio playing.", I,
            kNone),
    FR_INFO(EDP_HearRangeEventStrange, "Strange noise hearing range", Meters, N,
            "How far away an NPC hears a strange noise that makes it curious.", D, kDA),
    FR_INFO(EDP_HearRangeEventThrownItem, "Thrown item hearing range", Meters, N,
            "How far away an NPC hears a thrown item land (how far a coin lures).", D, kDA),
    FR_INFO(EDP_HearRangeFootstepWalkSlow, "Footsteps: slow walk", Meters, D,
            "How far away an NPC hears 47 walking slowly.", D, kHTDA),
    FR_INFO(EDP_HearRangeFootstepWalk, "Footsteps: walk", Meters, D, "How far away an NPC hears 47 walking.", D, kHTDA),
    FR_INFO(EDP_HearRangeFootstepRun, "Footsteps: run", Meters, D, "How far away an NPC hears 47 running.", D, kHTDA),
    FR_INFO(EDP_HearRangeFootstepCrouchWalkSlow, "Footsteps: slow crouch walk", Meters, D,
            "How far away an NPC hears 47 crouch-walking slowly.", D, kHTDA),
    FR_INFO(EDP_HearRangeFootstepCrouchWalk, "Footsteps: crouch walk", Meters, D,
            "How far away an NPC hears 47 crouch-walking.", D, kHTDA),
    FR_INFO(EDP_HearRangeFootstepCrouchRun, "Footsteps: crouch run", Meters, D,
            "How far away an NPC hears 47 crouch-running.", D, kHTDA),
    FR_INFO(EDP_HearRangeFootstepVegetationLVAWalkSlow, "Vegetation footsteps: slow walk", Meters, D,
            "How far away an NPC hears 47 walking slowly through vegetation.", D, kHTDA),
    FR_INFO(EDP_HearRangeFootstepVegetationLVAWalk, "Vegetation footsteps: walk", Meters, D,
            "How far away an NPC hears 47 walking through vegetation.", D, kHTDA),
    FR_INFO(EDP_HearRangeFootstepVegetationLVARun, "Vegetation footsteps: run", Meters, D,
            "How far away an NPC hears 47 running through vegetation.", D, kHT),
    FR_INFO(EDP_HearRangeFootstepVegetationLVACrouchWalkSlow, "Vegetation footsteps: slow crouch walk", Meters, D,
            "How far away an NPC hears 47 crouch-walking slowly through vegetation.", D, kHTDA),
    FR_INFO(EDP_HearRangeFootstepVegetationLVACrouchWalk, "Vegetation footsteps: crouch walk", Meters, D,
            "How far away an NPC hears 47 crouch-walking through vegetation.", D, kHTDA),
    FR_INFO(EDP_HearRangeFootstepVegetationLVACrouchRun, "Vegetation footsteps: crouch run", Meters, D,
            "How far away an NPC hears 47 crouch-running through vegetation.", D, kHTDA),
    FR_INFO(EDP_HearRangeGunEmpty, "Empty gun click hearing range", Meters, N,
            "How far away an NPC hears the click of a gun fired with no ammo left.", D, kDA),
    FR_INFO(EDP_HearRangeGunShot, "Gunshot hearing range", Meters, D,
            "How far away an NPC hears an unsilenced gunshot, by 47 or an NPC.", D, kHTDA),
    FR_INFO(EDP_HearRangeGunSilenced, "Silenced gunshot hearing range", Meters, D,
            "How far away an NPC hears a silenced gunshot.", D, kHTDA),
    FR_INFO(EDP_HearRangeGunSuperSilenced, "Subsonic gunshot hearing range", Meters, D,
            "How far away an NPC hears a shot from a subsonic (super-silenced) weapon.", D, kHTDA),
    FR_INFO(EDP_HearRangeSpeakAngry, "Angry shout hearing range", Meters, D,
            "How far away an NPC hears another NPC shout angrily.", D, kHTDA),
    FR_INFO(EDP_HearRangeSpeakHelp, "Call for help hearing range", Meters, D,
            "How far away an NPC hears a call for help.", D, kHTDA),
    FR_INFO(EDP_HearRangeSpeakHelpLoud, "Loud call for help hearing range", Meters, D,
            "How far away an NPC hears a loud call for help.", D, kHTDA),
    FR_INFO(EDP_HearRangeSpeakPain, "Pain cry hearing range", Meters, D,
            "How far away an NPC hears someone cry out in pain.", D, kHTDA),
    FR_INFO(EDP_HearRangeSpeakPainLow, "Muffled pain hearing range", Meters, D,
            "How far away an NPC hears the quieter pain sound of someone attacked from behind.", D, kHTDA),
    FR_INFO(EDP_HearRangeSpeakWarning, "Warning shout hearing range", Meters, D,
            "How far away an NPC hears a guard shout a warning.", D, kHTDA),

    // Attention.
    FR_INFO(EDP_AIAttentionGainMultiplier, "Attention gain", Multiplier, N,
            "Scales how fast NPCs build up attention toward 47; higher means they notice him sooner.", D, kDA),
    FR_INFO(EDP_AIIllegalActionAttentionModifier, "Illegal action attention", Multiplier, I,
            "Scales the attention NPCs gain from seeing 47 do something illegal.", I, kNone),
    FR_INFO(EDP_AIRunningAttentionModifier, "Running attention", Multiplier, I,
            "Scales the extra attention 47 draws while running.", D, kDA),
    FR_INFO(EDP_AISuspiciousMovementDisguiseAttentionModifier, "Suspicious movement in disguise", Multiplier, I,
            "Scales the attention 47 draws by moving suspiciously while disguised.", D, kDA),

    // Thresholds: lower means NPCs react sooner.
    FR_INFO(EDP_MinAttentionToForceLookAt, "Attention to look at 47", Attention, I,
            "How much attention an NPC needs before it turns to look at 47; lower means sooner.", D, kDA),
    FR_INFO(EDP_MinAttentionToForceLookAtIfIncreasing, "Attention to look at 47 (rising)", Attention, I,
            "The same look-at threshold while the NPC's attention is still rising; lower means sooner.", D, kDA),
    FR_INFO(EDP_SightReactionMinAttention, "Attention to react to sight", Attention, I,
            "How much attention an NPC needs before it reacts to what it sees of 47; lower means sooner.", D, kDA),
    FR_INFO(EDP_SightInvestigationMinAttention, "Attention to investigate", Attention, I,
            "How much attention an NPC needs before it investigates what it saw; lower means sooner.", D, kDA),

    // Search.
    FR_INFO(EDP_ManhuntGroupMaxSearchTime, "Manhunt search time", Seconds, N,
            "How long guards keep searching for 47 before giving up.", D, kSP),
    FR_INFO(EDP_ManhuntGroupSearchAreaHackMaxDistance, "Manhunt search area limit", Meters, N,
            "Upper limit on the size of the area a manhunt searches (\"Hack\" in the name is unexplained).", D, kDA),
    FR_INFO(EDP_ManhuntGroupSearchAreaRadius, "Manhunt search radius", Meters, N,
            "Radius of the area guards search during a manhunt.", D, kDA),
    FR_INFO(EDP_AISuspiciousLostTargetTime, "Suspicious NPC gives up after", Seconds, N,
            "How long a suspicious NPC keeps looking for 47 after losing sight of him.", I, kNone),
    FR_INFO(EDP_ArrestTargetLostTime, "Arrest gives up after", Seconds, N,
            "How long a guard trying to arrest 47 keeps after him once he is out of sight.", D, kDA),
    FR_INFO(EDP_AIInvestigateCautious_Disturbance_Radius, "Investigation radius", Meters, N,
            "Radius around a disturbance that NPCs search in a cautious investigation.", D, kDA),
    FR_INFO(EDP_AIInvestigateCautious_Search_ClearDisturbanceBackRadius, "Investigation clear radius (behind)", Meters,
            N, "How far behind a disturbance an investigating NPC checks before calling it clear.", I, kNone),
    FR_INFO(EDP_AIInvestigateCautious_Search_ClearDisturbanceFrontRadius, "Investigation clear radius (front)", Meters,
            N, "How far in front of a disturbance an investigating NPC checks before calling it clear.", I, kNone),
    FR_INFO(EDP_AIInvestigateDisguiseMembersFollowDistance, "Disguise group follow distance", Meters, N,
            "How far NPCs who share 47's disguise follow him when suspicious (mods disagree on what it measures).", I,
            kNone),

    // Warnings: shorter means guards act sooner.
    FR_INFO(EDP_AIDisguise_FollowWarningsDelay, "Enforcer follow delay", Seconds, N,
            "Delay before enforcers who doubt 47's disguise start following him.", D, kDA),
    FR_INFO(EDP_AISentry_WarningDelay, "Sentry warning delay", Seconds, N,
            "Delay before a sentry (a guard posted at an entrance) warns 47.", D, kDA),
    FR_INFO(EDP_AIEscortOut_MaxTimeToHonorWarning, "Time to obey a warning", Seconds, N,
            "How long 47 has to leave a restricted area after a warning before guards escalate.", D, kDA),
    FR_INFO(EDP_AIEscortOut_MaxForbiddenMovement, "Escort wiggle room", Meters, I,
            "How far 47 may move the wrong way while being escorted out of a restricted area.", D, kDA),

    // Combat timing.
    FR_INFO(EDP_AICombat_ShootingAccuracyInitialBuildUp_MinimumDuration, "Aim build-up time (min)", Seconds, N,
            "Shortest time an NPC takes, once it starts shooting, to reach full accuracy.", I, kNone),
    FR_INFO(EDP_AICombat_ShootingAccuracyInitialBuildUp_MaximumDuration, "Aim build-up time (max)", Seconds, N,
            "Longest time an NPC takes, once it starts shooting, to reach full accuracy.", I, kNone),
    FR_INFO(EDP_AICombat_FlashGrenadeThrow_GlobalCooldown, "Flashbang cooldown", Seconds, N,
            "Minimum time between any two flashbangs thrown by NPCs.", D, kDA),
    FR_INFO(EDP_AICombat_FlashGrenadeThrow_AggressionTriggerCooldown, "Flashbang cooldown (aggression)", Seconds, N,
            "Time before NPCs can again throw a flashbang in response to 47 being aggressive.", D, kDA),
    FR_INFO(EDP_AICombat_FlashGrenadeThrow_TargetCampingTriggerCooldown, "Flashbang cooldown (camping)", Seconds, N,
            "Time before NPCs can again throw a flashbang to flush 47 out of a spot he stays in.", D, kDA),
    FR_INFO(EDP_AICombat_FlashGrenadeThrow_LargeRelocationTriggerCooldown, "Flashbang cooldown (relocation)", Seconds,
            N, "Time before NPCs can again throw a flashbang after 47 moves a long way.", I, kNone),

    // Combat other.
    FR_INFO(EDP_AICombat_ShootingAccuracyResetPeriod_Minimum, "Aim reset period (min)", Seconds, N,
            "Shortest time before an NPC's aim resets and has to build up again.", I, kNone),
    FR_INFO(EDP_AICombat_ShootingAccuracyResetPeriod_Maximum_FewCombatants, "Aim reset period (few fighting)", Seconds,
            N, "Longest time before an NPC's aim resets, when few NPCs are fighting.", I, kNone),
    FR_INFO(EDP_AICombat_ShootingAccuracyResetPeriod_Maximum_MediumCombatants, "Aim reset period (some fighting)",
            Seconds, N, "Longest time before an NPC's aim resets, when a medium number of NPCs are fighting.", I,
            kNone),
    FR_INFO(EDP_AICombat_ShootingAccuracyResetPeriod_Maximum_ManyCombatants, "Aim reset period (many fighting)",
            Seconds, N, "Longest time before an NPC's aim resets, when many NPCs are fighting.", I, kNone),
    FR_INFO(EDP_AICombat_ShootingAccuracy_MinimumDistance, "Aim minimum distance", Meters, N,
            "A distance used in NPC aim, probably the range inside which NPCs shoot at full accuracy.", I, kNone),
    FR_INFO(EDP_AIEscortOut_EscortEscalateDistance, "Escort escalation distance", Meters, N,
            "How far 47 may get from the guard escorting him out before the escort escalates.", D, kDA),
    FR_INFO(EDP_AIEscortOut_EscortHardWarningDistance, "Escort hard warning distance", Meters, N,
            "Distance at which a guard escorting 47 out gives him a final warning.", I, kNone),

    // Damage.
    FR_INFO(EDP_HitmanDamageReceivedMultiplier, "Damage taken by 47", Multiplier, N, "Scales all damage 47 takes.", I,
            kNone),
    FR_INFO(EDP_DamageMultiplierHandgun_NPC, "NPC pistol damage", Multiplier, N,
            "Scales the damage NPCs do to 47 with pistols.", D, kDA),
    FR_INFO(EDP_DamageMultiplierSlowgun_NPC, "NPC slowgun damage", Multiplier, N,
            "Scales the damage NPCs do to 47 with \"slowgun\" weapons (which weapons these are is unknown).", I, kNone),
    FR_INFO(EDP_DamageMultiplierAssaultRifle_NPC, "NPC assault rifle damage", Multiplier, N,
            "Scales the damage NPCs do to 47 with assault rifles.", D, kDA),
    FR_INFO(EDP_DamageMultiplierSMG_NPC, "NPC SMG damage", Multiplier, N,
            "Scales the damage NPCs do to 47 with submachine guns.", D, kDA),
    FR_INFO(EDP_DamageMultiplierSniper_NPC, "NPC sniper damage", Multiplier, N,
            "Scales the damage NPCs do to 47 with sniper rifles.", I, kNone),
    FR_INFO(EDP_DamageMultiplierRPG_NPC, "NPC rocket launcher damage", Multiplier, N,
            "Scales the damage NPCs do to 47 with rocket launchers.", I, kNone),
    FR_INFO(EDP_DamageMultiplierKnife_NPC, "NPC knife damage", Multiplier, N,
            "Scales the damage NPCs do to 47 with knives.", I, kNone),
    FR_INFO(EDP_DamageMultiplierShotgun_NPC, "NPC shotgun damage", Multiplier, N,
            "Scales the damage NPCs do to 47 with shotguns.", D, kDA),
    FR_INFO(EDP_DamageMultiplierSpotter_NPC, "NPC spotter damage", Multiplier, N,
            "Scales the damage of NPC \"spotter\" attacks (what a spotter is here is unknown).", I, kNone),
    FR_INFO(EDP_SpotHitmanShootingDistance, "Spot 47 shooting distance", Meters, N,
            "Probably how far away NPCs notice 47 firing a gun.", I, kNone),
    FR_INFO(EDP_MaxCoverShootActorCountMultiplier, "Shooters in cover", Multiplier, N,
            "Probably scales how many NPCs may shoot from cover at the same time.", I, kNone),

    // Damage (player side).
    FR_INFO(EDP_NPCDamageReceivedMultiplier, "Damage taken by NPCs", Multiplier, N, "Scales all damage NPCs take.", I,
            kNone),

    // Player health.
    FR_INFO(EDP_PlayerHitPointsMax, "47's maximum health", HitPoints, I, "47's maximum health.", I, kNone),
    FR_INFO(EDP_PlayerHitPointsRegenerationPerSecond, "Health regeneration rate", PerSecond, N,
            "Health 47 regains per second while regenerating.", D, kDA),
    FR_INFO(EDP_PlayerHitPointsRegenerationDelay, "Health regeneration delay", Seconds, N,
            "How long after taking damage 47's health starts to regenerate.", D, kDA),

    // Instinct.
    FR_INFO(EDP_InstinctModeBurnRate, "Instinct drain rate", PerSecond, I,
            "How fast the Instinct meter drains while Instinct is in use.", I, kNone),
    FR_INFO(EDP_InstinctRegenRate, "Instinct refill rate", PerSecond, I, "How fast the Instinct meter refills.", I,
            kNone),
    FR_INFO(EDP_InstinctRegenCap, "Instinct refill cap", Count, I, "How far the Instinct meter refills on its own.", I,
            kNone),

    // Social: greetings and stares; symmetric (variety only).
    FR_INFO(EDP_SocialGreetingAngle, "Greeting angle", Degrees, I,
            "How wide the angle in front of an NPC is within which it may greet 47.", I, kNone),
    FR_INFO(EDP_SocialGreetingCooldown, "Greeting cooldown", Seconds, N, "Time before an NPC greets again.", I, kNone),
    FR_INFO(EDP_SocialGreetingCooldownAlertLevel, "Greeting cooldown (alert level)", Seconds, N,
            "A greeting cooldown tied to the NPC's alert level.", I, kNone),
    FR_INFO(EDP_SocialGreetingCooldownLastSpeak, "Greeting cooldown (after speaking)", Seconds, N,
            "Time after an NPC last spoke before it may greet.", I, kNone),
    FR_INFO(EDP_SocialGreetingCooldownPlayer, "Greeting cooldown (47)", Seconds, N, "Time before NPCs greet 47 again.",
            I, kNone),
    FR_INFO(EDP_SocialGreetingCooldownSequence, "Greeting cooldown (sequence)", Seconds, N,
            "A greeting cooldown tied to greeting sequences.", I, kNone),
    FR_INFO(EDP_SocialGreetingDistance, "Greeting distance", Meters, N, "How close 47 must be for an NPC to greet him.",
            I, kNone),
    FR_INFO(EDP_SocialLookAtTimeMax, "Social look time (max)", Seconds, N,
            "Longest time an NPC looks at 47 in passing.", I, kNone),
    FR_INFO(EDP_SocialLookAtTimeMin, "Social look time (min)", Seconds, N,
            "Shortest time an NPC looks at 47 in passing.", I, kNone),
    FR_INFO(EDP_SocialSillyCooldown, "Odd behavior reaction cooldown", Seconds, N,
            "Time before an NPC reacts again to 47 behaving oddly (\"silly\" in the name).", I, kNone),
    FR_INFO(EDP_SocialSillyDistance, "Odd behavior reaction distance", Meters, N,
            "How close 47 must be for an NPC to react to him behaving oddly.", I, kNone),
    FR_INFO(EDP_SocialSillyTriggerTime, "Odd behavior trigger time", Seconds, N,
            "How long 47 must behave oddly before an NPC reacts.", I, kNone),
    FR_INFO(EDP_SocialStandingCooldown, "Standing close cooldown", Seconds, N,
            "Time before an NPC reacts again to 47 standing close to it.", I, kNone),
    FR_INFO(EDP_SocialStandingDistance, "Standing close distance", Meters, N,
            "How close 47 must stand for an NPC to react to him.", I, kNone),
    FR_INFO(EDP_SocialStandingDistanceImmediate, "Standing close distance (at once)", Meters, N,
            "Within this distance an NPC reacts to 47 standing close straight away.", I, kNone),
    FR_INFO(EDP_SocialStandingTriggerTime, "Standing close trigger time", Seconds, N,
            "How long 47 must stand close to an NPC before it reacts.", I, kNone),
};

#undef FR_INFO

} // namespace

std::span<const DialInfo> DialInfoTable() {
    return kInfo;
}

const DialInfo* FindDialInfo(int32_t param) {
    for (const auto& info : kInfo) {
        if (info.param == param)
            return &info;
    }
    return nullptr;
}

std::string_view UnitLabel(DialUnit unit) {
    switch (unit) {
        case DialUnit::Meters: return "m";
        case DialUnit::Seconds: return "s";
        case DialUnit::Multiplier: return "x";
        case DialUnit::Attention: return "attention";
        case DialUnit::Count: return "";
        case DialUnit::PerSecond: return "/s";
        case DialUnit::Percent: return "%";
        case DialUnit::Degrees: return "deg";
        case DialUnit::HitPoints: return "HP";
    }
    return "";
}

std::string DialDisplayName(int32_t param) {
    if (const DialInfo* info = FindDialInfo(param))
        return std::string(info->name);
    return ParamLabel(param);
}

std::string FormatDialNumber(double value) {
    if (!std::isfinite(value))
        return std::isnan(value) ? "nan" : (value > 0 ? "inf" : "-inf");

    char text[64] = {};
    // %.4g switches to exponent form at 1e4; integral values up to 1e6 print whole instead.
    if (std::abs(value) >= 1e4 && std::abs(value) < 1e6)
        std::snprintf(text, sizeof(text), "%.0f", value);
    else
        std::snprintf(text, sizeof(text), "%.4g", value);
    return text;
}

std::string FormatDialValues(double before, double after, DialUnit unit) {
    const std::string x = unit == DialUnit::Multiplier ? "x" : "";
    return x + FormatDialNumber(before) + " -> " + x + FormatDialNumber(after);
}

std::string FormatDialChange(double before, double after, DialUnit unit, Basis unitBasis) {
    const std::string unsure = unitBasis == Basis::Inferred ? " (?)" : "";

    if (unit == DialUnit::Multiplier)
        return FormatDialValues(before, after, unit) + unsure;

    const std::string_view label = UnitLabel(unit);
    // "%" and "/s" attach to the number; a count has no label.
    const bool attached = unit == DialUnit::Percent || unit == DialUnit::PerSecond;
    const std::string suffix = label.empty() ? std::string() : (attached ? "" : " ") + std::string(label);

    return FormatDialValues(before, after, unit) + suffix + unsure;
}

std::string_view HarderWhen(int8_t dir) {
    if (dir > 0)
        return "larger";
    if (dir < 0)
        return "smaller";
    return "neither";
}

std::string_view BasisLabel(Basis basis) {
    switch (basis) {
        case Basis::Documented: return "documented";
        case Basis::Named: return "from its name";
        case Basis::Inferred: return "inferred";
    }
    return "inferred";
}

} // namespace fr
