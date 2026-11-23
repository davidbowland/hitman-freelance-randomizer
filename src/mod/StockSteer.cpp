// Stock steering (see StockSteer.h). Every raw engine read and write follows
// the guard pattern in Guard.h.

#include "StockSteer.h"

#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "PinIds.h"
#include "SupplierRules.h"
#include "StockSteerMath.h"

#include <Globals.h>

#include <Glacier/Reflection.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZTypeRegistry.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <string_view>

namespace {

constexpr size_t kMaxNamesListed = 16; // names per held / eligible list
constexpr size_t kMaxOptionLines = 60; // per-gun plan lines per load

std::optional<float> ReadFloat(uintptr_t p_Address) {
    float s_Value = 0.0f;

    if (!p_Address || ReadEngineBytes(p_Address, &s_Value, sizeof(s_Value)))
        return std::nullopt;

    return s_Value;
}

std::optional<bool> ReadBool(uintptr_t p_Address) {
    uint8_t s_Value = 0;

    if (!p_Address || ReadEngineBytes(p_Address, &s_Value, sizeof(s_Value)))
        return std::nullopt;

    return s_Value != 0;
}

bool WriteFloat(uintptr_t p_Address, double p_Value) {
    const auto s_Value = static_cast<float>(p_Value);
    float s_Back = 0.0f;
    return p_Address && !WriteEngineBytes(p_Address, &s_Value, sizeof(s_Value)) &&
           !ReadEngineBytes(p_Address, &s_Back, sizeof(s_Back)) && s_Back == s_Value;
}

// The stored float equals p_Value as written (float32).
bool Holds(uintptr_t p_Address, double p_Value) {
    const std::optional<float> s_Now = ReadFloat(p_Address);
    return s_Now && *s_Now == static_cast<float>(p_Value);
}

constexpr std::string_view kTag = "stock steer";

} // namespace

void StockSteer::Configure(bool p_Supplier, bool p_Gear, bool p_ForcePick) {
    m_SupplierOn = p_Supplier;
    m_GearOn = p_Supplier && p_Gear;
    m_ForcePickOn = p_ForcePick;

    if (On())
        LogTo(fr::LogTopic::SupplierCrates,
              "stock steer: supplier_steer {} (weights only, never a choice list or condition; steered at {} s of "
              "play, checked every {} s until the roll, restored {} s after it); supplier_steer_gear {} (the options "
              "in no selector, less firearms, as one family; restored {} s after the first offer under one, or {} s "
              "after SupplierVisited; only offers and visits after the steer count); supplier_force_pick {} (a "
              "silent gun selector with an eligible gun gets its Pick signaled {} s after the burst unless the "
              "supplier filled a stand with another item that visit, its answer judged {} s later)",
              m_SupplierOn, fr::kStockSteerAt, fr::kStockSteerCheckEvery, fr::kStockRestoreAfter, m_GearOn,
              fr::kGearRestoreAfterOffer, fr::kGearRestoreAfterVisit, m_ForcePickOn, fr::kForcePickQuietSeconds,
              fr::kForcePickOutcomeSeconds);
}

// --- Setup ---

void StockSteer::Prepare(std::string_view p_Scene, std::vector<OptionInfo> p_Options,
                         std::vector<SelectorInfo> p_Selectors) noexcept {
    Contained(kTag, "prepare", [&] {
        std::lock_guard s_Lock(m_Mutex);
        Forget();

        if (!On())
            return;

        if (*Globals::TypeRegistry && !m_Float32Type.load(std::memory_order_relaxed))
            m_Float32Type.store((*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("float32"))),
                                std::memory_order_release);

        m_Scene = std::string(p_Scene);
        m_Options = std::move(p_Options);
        m_SteeredOf.assign(m_Options.size(), SIZE_MAX);
        const uint64_t s_Seed = fr::kCampaignSeedEntityId;
        size_t s_NoMath = 0;
        std::string s_Labels;

        // The options some selector draws (any seed): the rest are in no selector.
        std::vector<bool> s_InSelector(m_Options.size(), false);

        for (const SelectorInfo& s_Selector : p_Selectors) {
            for (const size_t s_Option : s_Selector.options) {
                if (s_Option < s_InSelector.size())
                    s_InSelector[s_Option] = true;
            }
        }

        // Each option in no selector: its block, from its position among them.
        const auto s_LooseCount = static_cast<size_t>(std::count(s_InSelector.begin(), s_InSelector.end(), false));
        m_LooseBlock.assign(m_Options.size(), '\0');

        for (size_t i = 0, s_Position = 0; i < m_Options.size(); ++i) {
            if (!s_InSelector[i])
                m_LooseBlock[i] = fr::LooseBlockOf(s_Position++, s_LooseCount);
        }

        for (SelectorInfo& s_Selector : p_Selectors) {
            if (s_Selector.seedId != s_Seed || !s_Selector.entity)
                continue;

            Slot s_Slot;
            const size_t s_SlotIndex = m_Slots.size();

            for (const size_t s_Option : s_Selector.options) {
                if (s_Option >= m_Options.size() || m_SteeredOf[s_Option] != SIZE_MAX)
                    continue;

                m_SteeredOf[s_Option] = m_Steered.size();
                s_Slot.steered.push_back(m_Steered.size());
                Steered s_Steered;
                s_Steered.option = s_Option;
                s_Steered.selector = s_SlotIndex;
                m_Steered.push_back(s_Steered);

                if (m_Options[s_Option].math)
                    Watch(m_Options[s_Option].math, m_Steered.size() - 1, kRoleMath);
                else
                    ++s_NoMath;
            }

            Watch(s_Selector.entity, s_SlotIndex, kRoleSelector);
            const std::string s_Category = s_Slot.steered.empty()
                                               ? std::string("?")
                                               : m_Options[m_Steered[s_Slot.steered.front()].option].category;
            s_Labels += fmt::format("{}{} ({}, {} options)", s_Labels.empty() ? "" : "; ", s_Selector.label,
                                    s_Category.empty() ? "?" : s_Category, s_Slot.steered.size());
            s_Slot.info = std::move(s_Selector);
            m_Slots.push_back(std::move(s_Slot));
        }

        if (m_Slots.empty()) {
            LogTo(fr::LogTopic::SupplierCrates,
                  "stock steer supplier: {}: no selector with m_Seed {:016x} among the {} the supplier walk mapped; "
                  "nothing "
                  "steered",
                  ShortSceneName(m_Scene), s_Seed, p_Selectors.size());
            Forget();
            return;
        }

        m_Prepared = true;
        m_Armed.store(true, std::memory_order_release);
        LogTo(fr::LogTopic::SupplierCrates,
              "stock steer supplier: {}: {} selectors with m_Seed {:016x}: {}; {} options watched ({} without a weight "
              "math); {}{}",
              ShortSceneName(m_Scene), m_Slots.size(), s_Seed, s_Labels, m_Steered.size(), s_NoMath,
              m_SupplierOn ? fmt::format("steering at {} s of play", fr::kStockSteerAt)
                           : std::string("not steered (supplier_steer off)"),
              m_ForcePickOn ? "; force pick on" : "");

        if (m_GearOn)
            PrepareGear(s_InSelector);
        else if (m_SupplierOn)
            LogTo(fr::LogTopic::SupplierCrates,
                  "stock steer supplier gear: {}: off (supplier_steer_gear): the non-gun slots keep the game's odds",
                  ShortSceneName(m_Scene));
    });
}

// The gear pseudo-slot: every option in no selector that is not a firearm
// and has its weight math (fr::IsGearSteerOption). Under m_Mutex.
void StockSteer::PrepareGear(const std::vector<bool>& p_InSelector) {
    Slot s_Slot;
    s_Slot.gear = true;
    s_Slot.info.label = "supplier gear (options in no gun slot)";
    const size_t s_SlotIndex = m_Slots.size();
    size_t s_Loose = 0, s_Firearms = 0, s_NoMath = 0;
    m_Loose.assign(m_Options.size(), false);

    for (size_t i = 0; i < m_Options.size(); ++i) {
        const OptionInfo& s_Option = m_Options[i];
        const bool s_InSelector = i < p_InSelector.size() && p_InSelector[i];
        const bool s_Math = s_Option.math && s_Option.faAddress && s_Option.fbAddress;

        if (s_InSelector)
            continue;

        m_Loose[i] = true;
        ++s_Loose;
        s_Firearms += s_Option.firearm ? 1 : 0;
        s_NoMath += !s_Option.firearm && !s_Math ? 1 : 0;

        if (!fr::IsGearSteerOption({s_InSelector, s_Option.firearm, s_Math}) || m_SteeredOf[i] != SIZE_MAX)
            continue;

        m_SteeredOf[i] = m_Steered.size();
        s_Slot.steered.push_back(m_Steered.size());
        Steered s_Steered;
        s_Steered.option = i;
        s_Steered.selector = s_SlotIndex;
        m_Steered.push_back(s_Steered);
        Watch(s_Option.math, m_Steered.size() - 1, kRoleMath);
    }

    if (s_Slot.steered.empty()) {
        LogTo(fr::LogTopic::SupplierCrates,
              "stock steer supplier gear: {}: {} options in no selector, none to steer ({} firearms left alone, {} "
              "without a weight math)",
              ShortSceneName(m_Scene), s_Loose, s_Firearms, s_NoMath);
        return;
    }

    LogTo(fr::LogTopic::SupplierCrates,
          "stock steer supplier gear: {}: {} options in no selector: {} steered as one family, {} firearms left alone, "
          "{} without a weight math; steering at {} s of play",
          ShortSceneName(m_Scene), s_Loose, s_Slot.steered.size(), s_Firearms, s_NoMath, fr::kStockSteerAt);
    m_GearSlot = s_SlotIndex;
    m_Slots.push_back(std::move(s_Slot));
}

const char* StockSteer::Prefix(const Slot& p_Slot) const {
    return p_Slot.gear ? "stock steer supplier gear" : "stock steer supplier";
}

bool StockSteer::Eligible(const OptionInfo& p_Option) const {
    fr::StockFlags s_Flags;

    for (const uintptr_t s_Address : p_Option.unownedFlags) {
        if (const std::optional<bool> s_Value = ReadBool(s_Address))
            (*s_Value ? s_Flags.unownedTrue : s_Flags.unownedFalse) += 1;
    }

    s_Flags.entitled = ReadBool(p_Option.entitledFlag);
    return fr::StockEligible(s_Flags);
}

std::string StockSteer::SeedText(const SelectorInfo& p_Selector) const {
    int32_t s_Value = 0;

    if (!p_Selector.entity)
        return "no selector entity";

    if (!p_Selector.seedAddress || ReadEngineBytes(p_Selector.seedAddress, &s_Value, sizeof(s_Value)))
        return fmt::format("seed {:016x} = ?", p_Selector.seedId);

    return fmt::format("seed {:016x} = {}", p_Selector.seedId, s_Value);
}

std::string StockSteer::OptionText(size_t p_Option) const {
    const OptionInfo& s_Option = m_Options[p_Option];
    return fmt::format("{} [{}{}{}]", s_Option.name, s_Option.category.empty() ? "?" : s_Option.category,
                       s_Option.hasRarity ? fmt::format(", rarity {}", s_Option.rarity) : std::string(),
                       s_Option.entitlement.empty() ? std::string() : ", " + s_Option.entitlement);
}

// --- Steering (under m_Mutex) ---

void StockSteer::Steer() {
    m_SteerDone = true;
    m_NextCheck = m_Play + fr::kStockSteerCheckEvery;

    // The supplier already rolled: the gear is restored on the next tick.
    if (m_GearClock.Steered(m_Play) && m_GearSlot < m_Slots.size())
        m_Slots[m_GearSlot].restoreWhy = "SupplierVisited before the steer";

    if (!m_SupplierOn)
        return;

    for (Slot& s_Slot : m_Slots)
        Plan(s_Slot, true);
}

// Eligible guns' m_fB lowered to equal weights (fr::PlanSupplierSteer); on a
// later check the game's m_fB is taken anew where it changed what was
// written, and guns no longer eligible get theirs back.
void StockSteer::Plan(Slot& p_Slot, bool p_First) {
    p_Slot.planned = true;
    std::vector<fr::SupplierSteerOption> s_Inputs;
    std::string s_Before;
    double s_BeforeTotal = 0.0;

    for (const size_t s_Index : p_Slot.steered) {
        Steered& s_Steered = m_Steered[s_Index];
        const OptionInfo& s_Option = m_Options[s_Steered.option];
        const std::optional<float> s_Now = ReadFloat(s_Option.fbAddress);

        // The game's m_fB: read until written; afterwards only a value that is not ours is the game's.
        if (s_Now && (s_Steered.fBHeld < 0.0 || *s_Now != static_cast<float>(s_Steered.fBHeld))) {
            if (s_Steered.fBHeld >= 0.0)
                ++p_Slot.rewrittenAgain;

            s_Steered.fB = *s_Now;
            s_Steered.fBHeld = -1.0;
        }

        s_Steered.eligible = (p_Slot.gear || s_Option.firearm) && Eligible(s_Option);
        s_Steered.fA = fr::StockRarityValue(s_Option.rarity, s_Option.hasRarity);
        s_Inputs.push_back({s_Steered.fA, s_Now ? s_Steered.fB : 0.0, s_Steered.eligible});

        if (s_Steered.eligible && s_Steered.fB > 0.0)
            s_BeforeTotal += s_Steered.fA / s_Steered.fB;
    }

    const fr::SupplierSteerPlan s_Plan = fr::PlanSupplierSteer(s_Inputs);
    const size_t s_OldEligible = p_Slot.eligible;
    p_Slot.eligible = s_Plan.eligible;
    p_Slot.weight = s_Plan.weight;
    size_t s_Written = 0;
    size_t s_Failed = 0;
    size_t s_Lines = 0;
    std::string s_Names;

    for (size_t k = 0; k < p_Slot.steered.size(); ++k) {
        Steered& s_Steered = m_Steered[p_Slot.steered[k]];
        const OptionInfo& s_Option = m_Options[s_Steered.option];
        const double s_Target = s_Plan.target[k];
        s_Steered.target = s_Target;

        if (s_Steered.eligible)
            s_Names += fmt::format("{}{}", s_Names.empty() ? "" : ", ", s_Option.name);

        if (s_Target >= 0.0 && s_Plan.fB[k] < s_Steered.fB) {
            if (!Holds(s_Option.fbAddress, s_Plan.fB[k])) {
                WriteFloat(s_Option.fbAddress, s_Plan.fB[k]) ? ++s_Written : ++s_Failed;
            }

            s_Steered.fBHeld = s_Plan.fB[k];
        }
        else if (s_Steered.fBHeld >= 0.0) {
            // No longer eligible: the game's m_fB back.
            WriteFloat(s_Option.fbAddress, s_Steered.fB) ? ++s_Written : ++s_Failed;
            s_Steered.fBHeld = -1.0;
        }

        if (p_First && s_Target >= 0.0 && s_Lines < kMaxOptionLines) {
            ++s_Lines;
            LogTo(fr::LogTopic::SupplierCrates,
                  "{}: {} {} {}: m_fA {} (predicted from rarity), m_fB {} -> {}: weight {:.4g} -> "
                  "{:.4g}, share among the eligible {:.1f}% -> {:.1f}%",
                  Prefix(p_Slot), p_Slot.info.label, p_Slot.gear ? "item" : "gun", OptionText(s_Steered.option),
                  s_Steered.fA, s_Steered.fB, s_Plan.fB[k], s_Steered.fA / s_Steered.fB, s_Target,
                  s_BeforeTotal > 0.0 ? 100.0 * (s_Steered.fA / s_Steered.fB) / s_BeforeTotal : 0.0,
                  100.0 / static_cast<double>(s_Plan.eligible));
        }
    }

    const std::string s_Category =
        p_Slot.steered.empty() ? std::string("?") : m_Options[m_Steered[p_Slot.steered.front()].option].category;

    const char* s_Noun = p_Slot.gear ? "item" : "gun";
    // The gear family's names are many: the first ones only.
    const std::string s_Listed = p_Slot.gear && s_Names.size() > 600 ? s_Names.substr(0, 600) + ", ..." : s_Names;

    if (p_First) {
        std::string s_What;

        if (s_Plan.eligible == 0)
            s_What = fmt::format("no {} you do not own and are entitled to: the {} stays empty or vanilla (nothing "
                                 "written)",
                                 s_Noun, p_Slot.gear ? "family" : "slot");
        else if (s_Plan.eligible == 1)
            s_What = fmt::format("one eligible {}: nothing written", s_Noun);
        else
            s_What = fmt::format("target weight {:.4g}: each eligible {} now weighs it ({} m_fB lowered, {} written, "
                                 "{} writes failed); the weight their math sends at the roll is re-asserted to it",
                                 s_Plan.weight, s_Noun, s_Plan.lowered, s_Written, s_Failed);

        LogTo(fr::LogTopic::SupplierCrates,
              "{}: {} ({}) t={:.2f} s: {} {}s, {} eligible (not owned, entitled){}; {}; {}", Prefix(p_Slot),
              p_Slot.info.label,
              p_Slot.gear          ? "non-gun"
              : s_Category.empty() ? "?"
                                   : s_Category,
              m_Play, p_Slot.steered.size(), s_Noun, s_Plan.eligible,
              s_Listed.empty() ? std::string() : fmt::format(": {}", s_Listed), s_What, SeedText(p_Slot.info));
    }
    else if (s_Plan.eligible != s_OldEligible || s_Written || s_Failed) {
        LogTo(fr::LogTopic::SupplierCrates,
              "{}: {} ({}) check t={:.2f} s: {} eligible (was {}){}; {} values written again ({} "
              "failed), weight {:.4g}",
              Prefix(p_Slot), p_Slot.info.label,
              p_Slot.gear          ? "non-gun"
              : s_Category.empty() ? "?"
                                   : s_Category,
              m_Play, s_Plan.eligible, s_OldEligible, s_Listed.empty() ? std::string() : fmt::format(": {}", s_Listed),
              s_Written, s_Failed, s_Plan.weight);
    }
}

// "2 of 2 held (A 0.578, B 0.578)" for the steered weights, read now.
std::string StockSteer::HeldText(const Slot& p_Slot) const {
    std::vector<fr::SteeredWeight> s_Weights;

    for (const size_t s_Index : p_Slot.steered) {
        const Steered& s_Steered = m_Steered[s_Index];
        const OptionInfo& s_Option = m_Options[s_Steered.option];

        if (s_Steered.target >= 0.0)
            s_Weights.push_back(
                {s_Option.name, ReadFloat(s_Option.weightAddress).value_or(std::nan("")), s_Steered.target});
    }

    return fr::SteeredWeightsText(s_Weights, kMaxNamesListed);
}

// A gun selector's pick signal.
void StockSteer::OnPick(size_t p_Slot, const char* p_What) {
    Slot& s_Slot = m_Slots[p_Slot];
    ++s_Slot.picks;
    s_Slot.lastPick = m_Play;

    if (s_Slot.picks > 1)
        return;

    s_Slot.firstPick = m_Play;
    LogTo(
        fr::LogTopic::SupplierCrates,
        "stock steer supplier roll: {} {} t={:.2f} s{}: {}; steered weights {}; math outputs during the window {}, {} "
        "rewritten to the steered weight",
        s_Slot.info.label, p_What, m_Play,
        !m_SupplierOn     ? " (not steered: supplier_steer off)"
        : !s_Slot.planned ? " (BEFORE THE STEER: this roll is vanilla)"
                          : "",
        SeedText(s_Slot.info), s_Slot.planned ? HeldText(s_Slot) : std::string("none yet"), s_Slot.outputs,
        s_Slot.rewrites);
}

void StockSteer::Restore(Slot& p_Slot) {
    p_Slot.restored = true;
    size_t s_Written = 0;
    size_t s_Failed = 0;
    // The gear family has no roll line: whether the steered weights held, read now.
    const std::string s_Held = p_Slot.gear ? HeldText(p_Slot) : std::string();

    for (const size_t s_Index : p_Slot.steered) {
        Steered& s_Steered = m_Steered[s_Index];
        const OptionInfo& s_Option = m_Options[s_Steered.option];

        if (s_Steered.fBHeld >= 0.0) {
            WriteFloat(s_Option.fbAddress, s_Steered.fB) ? ++s_Written : ++s_Failed;
            s_Steered.fBHeld = -1.0;
        }

        // The weight the game meant: its m_fA over the game's m_fB (its output at the roll was
        // computed with the lowered m_fB).
        const std::optional<float> s_FA = ReadFloat(s_Option.faAddress);

        if (s_Steered.target >= 0.0 && s_Steered.gameWeight && s_FA && s_Steered.fB > 0.0)
            WriteFloat(s_Option.weightAddress, *s_FA / s_Steered.fB) ? ++s_Written : ++s_Failed;
    }

    LogTo(fr::LogTopic::SupplierCrates, "{}: {} t={:.2f} s ({}): originals restored, {} values written, {} failed{}",
          Prefix(p_Slot), p_Slot.info.label, m_Play,
          !p_Slot.restoreWhy.empty() ? p_Slot.restoreWhy
                                     : fmt::format("{:.2f} s after its last pick signal", m_Play - p_Slot.lastPick),
          s_Written, s_Failed, p_Slot.gear ? "; steered weights before the restore: " + s_Held : std::string());
}

// --- Per frame ---

void StockSteer::Tick(double p_Play) noexcept {
    if (!Armed())
        return;

    std::vector<ForceTarget> s_Force;

    Contained(kTag, "frame", [&] {
        std::lock_guard s_Lock(m_Mutex);

        if (!m_Prepared)
            return;

        m_Play = p_Play;

        if (!m_SteerDone) {
            if (m_Play >= fr::kStockSteerAt)
                Steer();
        }
        else {
            const bool s_Check = m_Play >= m_NextCheck;

            if (s_Check)
                m_NextCheck = m_Play + fr::kStockSteerCheckEvery;

            for (Slot& s_Slot : m_Slots) {
                if (s_Slot.gear) {
                    if (s_Slot.restored)
                        continue;

                    if (m_GearClock.Due(m_Play))
                        Restore(s_Slot);
                    else if (s_Check)
                        Plan(s_Slot, false);

                    continue;
                }

                if (s_Slot.picks == 0) {
                    if (s_Check && m_SupplierOn)
                        Plan(s_Slot, false);
                }
                else if (!s_Slot.restored && m_Play >= s_Slot.lastPick + fr::kStockRestoreAfter) {
                    if (m_SupplierOn)
                        Restore(s_Slot);
                    else
                        s_Slot.restored = true; // nothing was written
                }
            }
        }

        if (!m_ForcePickOn)
            return;

        for (Slot& s_Slot : m_Slots) {
            if (s_Slot.forcedAt >= 0.0 && !s_Slot.forceJudged &&
                m_Play >= s_Slot.forcedAt + fr::kForcePickOutcomeSeconds)
                JudgeForced(s_Slot);
        }

        if (m_GunBurst.Due(m_Play))
            s_Force = EndBurst();
    });

    // Outside the lock: the selector's answer comes back through OnOutputPin in this call.
    for (const ForceTarget& s_Target : s_Force) {
        const uint32_t s_Code = SendInputPin(s_Target.entity, fr::kPinPick);

        Contained(kTag, "forced pick", [&] {
            std::lock_guard s_Lock(m_Mutex);

            if (!m_Prepared || s_Target.slot >= m_Slots.size())
                return;

            const Slot& s_Slot = m_Slots[s_Target.slot];
            LogTo(fr::LogTopic::SupplierCrates,
                  "stock force pick: {} t={:.2f} s: Pick {}; answered within the call: OnPickDone x{}, OnPickFailed "
                  "x{}; the outcome is judged {} s from now",
                  s_Slot.info.label, m_Play,
                  s_Code ? fmt::format("FAULTED (0x{:08X})", s_Code) : std::string("signaled"), s_Slot.forceDone,
                  s_Slot.forceFailed, fr::kForcePickOutcomeSeconds);
        });
    }
}

// --- Force pick (under m_Mutex) ---

// The slot's eligible guns (firearms whose flags say not owned and entitled), read now.
size_t StockSteer::EligibleGuns(const Slot& p_Slot, std::string* p_Names) const {
    size_t s_Count = 0;

    for (const size_t s_Index : p_Slot.steered) {
        const OptionInfo& s_Option = m_Options[m_Steered[s_Index].option];

        if (!s_Option.firearm || !Eligible(s_Option))
            continue;

        if (p_Names && s_Count < kMaxNamesListed)
            *p_Names += fmt::format("{}{}", p_Names->empty() ? "" : ", ", s_Option.name);

        ++s_Count;
    }

    return s_Count;
}

// A gun selector's OnPickDone / OnPickFailed: the forced Pick's answer, or a
// signal of the burst (the first one opens it).
void StockSteer::OnGunSignal(size_t p_Slot, bool p_Done) {
    Slot& s_Slot = m_Slots[p_Slot];

    if (!m_ForcePickOn)
        return;

    if (p_Done)
        s_Slot.everDone = true;

    // The forced Pick's answer is not part of a burst (it would open one and get judged again).
    if (s_Slot.forcedAt >= 0.0 && !s_Slot.forceJudged) {
        ++(p_Done ? s_Slot.forceDone : s_Slot.forceFailed);

        if (s_Slot.forceDone + s_Slot.forceFailed == 1)
            LogTo(fr::LogTopic::SupplierCrates,
                  "stock force pick: {} answered the forced Pick with {} t={:.2f} s ({:.2f} s of play after it)",
                  s_Slot.info.label, p_Done ? "OnPickDone" : "OnPickFailed", m_Play, m_Play - s_Slot.forcedAt);

        return;
    }

    if (m_GunBurst.OnSignal(m_Play)) {
        m_StandOffers.clear();

        for (Slot& s_Other : m_Slots) {
            s_Other.burstDone = 0;
            s_Other.burstFailed = 0;
            s_Other.forced = false;
        }

        LogTo(fr::LogTopic::SupplierCrates,
              "stock force pick: visit #{}: the gun selectors' burst opened t={:.2f} s by {} {}", m_GunBurst.Visits(),
              m_Play, s_Slot.info.label, p_Done ? "OnPickDone" : "OnPickFailed");
    }

    ++(p_Done ? s_Slot.burstDone : s_Slot.burstFailed);
}

// The burst is over: one verdict line per gun selector, and the ones to force.
std::vector<StockSteer::ForceTarget> StockSteer::EndBurst() {
    m_GunBurst.Close();
    std::vector<fr::ForcePickSlot> s_Inputs;
    std::vector<size_t> s_Indices;
    std::vector<std::string> s_Names;
    std::string s_Stands;

    for (const size_t s_Option : m_StandOffers) {
        if (s_Option < m_Options.size() && s_Option < m_LooseBlock.size())
            s_Stands += fmt::format("{}{} (block {})", s_Stands.empty() ? "" : ", ", m_Options[s_Option].name,
                                    m_LooseBlock[s_Option]);
    }

    for (size_t i = 0; i < m_Slots.size(); ++i) {
        const Slot& s_Slot = m_Slots[i];

        if (s_Slot.gear)
            continue;

        std::string s_List;
        const size_t s_Eligible = EligibleGuns(s_Slot, &s_List);
        s_Inputs.push_back(
            {s_Slot.burstDone, s_Slot.burstFailed, s_Slot.everDone, s_Eligible, s_Slot.forced, m_StandOffers.size()});
        s_Indices.push_back(i);
        s_Names.push_back(std::move(s_List));
    }

    std::vector<ForceTarget> s_Force;

    for (size_t k = 0; k < s_Inputs.size(); ++k) {
        Slot& s_Slot = m_Slots[s_Indices[k]];
        const fr::ForcePickVerdict s_Verdict = fr::JudgeForcePick(s_Inputs[k]);
        const std::string s_Category =
            s_Slot.steered.empty() ? std::string("?") : m_Options[m_Steered[s_Slot.steered.front()].option].category;

        LogTo(fr::LogTopic::SupplierCrates,
              "stock force pick: visit #{} burst t={:.2f}-{:.2f} s, judged t={:.2f} s: {} ({}): OnPickDone x{}, "
              "OnPickFailed x{}{}, {} eligible{}: {}{}",
              m_GunBurst.Visits(), m_GunBurst.First(), m_GunBurst.Last(), m_Play, s_Slot.info.label,
              s_Category.empty() ? "?" : s_Category, s_Inputs[k].done, s_Inputs[k].failed,
              s_Inputs[k].pickedBefore ? " (picked earlier this scene)" : "", s_Inputs[k].eligible,
              s_Names[k].empty() ? std::string() : fmt::format(" ({})", s_Names[k]),
              fr::ForcePickVerdictText(s_Verdict),
              s_Verdict == fr::ForcePickVerdict::StandFilled ? fmt::format(" (other stands: {})", s_Stands)
                                                             : std::string());

        if (s_Verdict != fr::ForcePickVerdict::Force)
            continue;

        s_Slot.forced = true;
        s_Slot.forcedVisit = m_GunBurst.Visits();
        s_Slot.forcedAt = m_Play;
        s_Slot.forceDone = 0;
        s_Slot.forceFailed = 0;
        s_Slot.forceJudged = false;
        s_Slot.forceOffers.clear();
        LogTo(fr::LogTopic::SupplierCrates,
              "stock force pick: {} t={:.2f} s: signaling its Pick input; {}; steered weights now: {}",
              s_Slot.info.label, m_Play, SeedText(s_Slot.info),
              m_SupplierOn && s_Slot.planned ? HeldText(s_Slot) : std::string("not steered (vanilla weights)"));
        s_Force.push_back({s_Indices[k], s_Slot.info.entity});
    }

    return s_Force;
}

// fr::kForcePickOutcomeSeconds after a forced Pick: what came of it.
void StockSteer::JudgeForced(Slot& p_Slot) {
    p_Slot.forceJudged = true;
    const fr::ForcePickOutcome s_Outcome = fr::ForcePickOutcomeOf(p_Slot.forceDone, p_Slot.forceFailed);
    p_Slot.forceOutcome = fmt::format("visit #{} t={:.2f} s: {}{}", p_Slot.forcedVisit, p_Slot.forcedAt,
                                      fr::ForcePickOutcomeText(s_Outcome),
                                      p_Slot.forceOffers.empty() ? "" : ", offered " + p_Slot.forceOffers);
    LogTo(fr::LogTopic::SupplierCrates,
          "stock force pick outcome: {} visit #{} (forced t={:.2f} s, judged t={:.2f} s): {}: OnPickDone x{}, "
          "OnPickFailed x{}; offers under it since the Pick: {}",
          p_Slot.info.label, p_Slot.forcedVisit, p_Slot.forcedAt, m_Play, fr::ForcePickOutcomeText(s_Outcome),
          p_Slot.forceDone, p_Slot.forceFailed, p_Slot.forceOffers.empty() ? std::string("none") : p_Slot.forceOffers);

    if (s_Outcome == fr::ForcePickOutcome::Swallowed)
        LogTo(fr::LogTopic::SupplierCrates,
              "stock force pick: {} SWALLOWED the forced Pick (no pick signal, nothing offered); {}", p_Slot.info.label,
              SeedText(p_Slot.info));
}

// --- Pins and offers (any thread) ---

void StockSteer::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept {
    if (!p_Entity.m_pObj || m_Watch.Empty())
        return;

    Contained(kTag, "output pin", [&] { Pin(p_Entity, p_PinId, p_Data); });
}

void StockSteer::Pin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) {
    const std::optional<uint32_t> s_Value = m_Watch.Lookup(p_Entity.m_pObj);

    if (!s_Value)
        return;

    const uint32_t s_Role = *s_Value & 3;
    const size_t s_Index = *s_Value >> 2;

    if (s_Role == kRoleSelector) {
        if (p_PinId != fr::kPinOnPickDone && p_PinId != fr::kPinOnPickFailed)
            return;

        std::lock_guard s_Lock(m_Mutex);

        if (m_Armed.load(std::memory_order_relaxed) && s_Index < m_Slots.size()) {
            OnGunSignal(s_Index, p_PinId == fr::kPinOnPickDone);
            OnPick(s_Index, p_PinId == fr::kPinOnPickDone ? "OnPickDone" : "OnPickFailed");
        }

        return;
    }

    if (s_Role != kRoleMath)
        return;

    PinPayload s_Payload;

    if (ReadPinPayload(p_Data, &s_Payload) || !s_Payload.data ||
        s_Payload.type != m_Float32Type.load(std::memory_order_acquire))
        return;

    std::lock_guard s_Lock(m_Mutex);

    if (!m_Armed.load(std::memory_order_relaxed) || !m_SteerDone || s_Index >= m_Steered.size())
        return;

    Steered& s_Steered = m_Steered[s_Index];
    Slot& s_Slot = m_Slots[s_Steered.selector];

    if (s_Slot.restored)
        return;

    const auto s_Address = reinterpret_cast<uintptr_t>(s_Payload.data);
    const std::optional<float> s_Sent = ReadFloat(s_Address);

    if (!s_Sent)
        return;

    ++s_Slot.outputs;
    s_Steered.gameWeight = *s_Sent;

    if (s_Steered.target < 0.0)
        return;

    const auto s_Target = static_cast<float>(s_Steered.target);

    if (*s_Sent != s_Target && WriteEngineBytes(s_Address, &s_Target, sizeof(s_Target)))
        return;

    if (++s_Slot.rewrites == 1)
        LogTo(fr::LogTopic::SupplierCrates,
              "{}: {} t={:.2f} s: the game sent weight {} for {} (its math output); {} on its way to "
              "the choice",
              Prefix(s_Slot), s_Slot.info.label, m_Play, *s_Sent, m_Options[s_Steered.option].name,
              *s_Sent == s_Target ? "already the steered weight" : fmt::format("rewritten to {}", s_Target));
}

void StockSteer::OnOffer(size_t p_Option) noexcept {
    if (!Armed())
        return;

    Contained(kTag, "offer", [&] {
        std::lock_guard s_Lock(m_Mutex);

        if (!m_Prepared || p_Option >= m_SteeredOf.size())
            return;

        // The gear family's restore: the first offer since the steer under an option in no selector
        // (a loose pistol too).
        if (m_GearSlot < m_Slots.size() && !m_Slots[m_GearSlot].restored && p_Option < m_Loose.size() &&
            m_Loose[p_Option] && m_GearClock.Offer(m_Play))
            m_Slots[m_GearSlot].restoreWhy = fmt::format(
                "{:.0f} s after the first offer since the steer under an option in no selector, {} at t={:.2f} s",
                fr::kGearRestoreAfterOffer, m_Options[p_Option].name, m_Play);

        // Force pick: an item on one of the supplier's alternative stands during the gun selectors' burst.
        if (m_ForcePickOn && m_GunBurst.Open() && p_Option < m_LooseBlock.size() && m_LooseBlock[p_Option] != '\0' &&
            fr::IsAlternativeStandBlock(m_LooseBlock[p_Option]) &&
            std::find(m_StandOffers.begin(), m_StandOffers.end(), p_Option) == m_StandOffers.end()) {
            m_StandOffers.push_back(p_Option);
            LogTo(fr::LogTopic::SupplierCrates,
                  "stock force pick: visit #{} t={:.2f} s: {} offered on an alternative stand (block {}): no "
                  "gun selector is forced this visit",
                  m_GunBurst.Visits(), m_Play, OptionText(p_Option), m_LooseBlock[p_Option]);
        }

        if (m_SteeredOf[p_Option] == SIZE_MAX)
            return;

        Steered& s_Steered = m_Steered[m_SteeredOf[p_Option]];

        if (s_Steered.offered)
            return;

        s_Steered.offered = true;
        Slot& s_Slot = m_Slots[s_Steered.selector];
        const OptionInfo& s_Option = m_Options[p_Option];
        const char* s_Verdict =
            s_Slot.gear           ? (s_Steered.eligible
                                         ? "an eligible item (not owned, entitled)"
                                         : "an item the flags call owned or not entitled (the game's condition allowed it)")
            : !s_Option.firearm   ? "not a gun"
            : !s_Steered.eligible ? "a gun the flags call owned or not entitled (the game's condition allowed it)"
                                  : "an eligible gun (not owned, entitled)";

        s_Slot.offers += fmt::format("{}{}", s_Slot.offers.empty() ? "" : "; ", s_Option.name);
        const bool s_Forced = s_Slot.forcedAt >= 0.0 && !s_Slot.forceJudged;

        if (s_Forced)
            s_Slot.forceOffers += fmt::format("{}{}", s_Slot.forceOffers.empty() ? "" : "; ", OptionText(p_Option));

        LogTo(fr::LogTopic::SupplierCrates, "{} offer: {} from {} t={:.2f} s: {}{}", Prefix(s_Slot),
              OptionText(p_Option), s_Slot.info.label, m_Play, s_Verdict,
              s_Forced ? " (after the mod's forced Pick)" : "");
    });
}

void StockSteer::OnSupplierVisited() noexcept {
    if (!Armed())
        return;

    Contained(kTag, "supplier visited", [&] {
        std::lock_guard s_Lock(m_Mutex);

        if (!m_Prepared || m_GearSlot >= m_Slots.size() || m_Slots[m_GearSlot].restored)
            return;

        if (!m_SteerDone) {
            m_GearClock.Visit(m_Play);
            LogTo(fr::LogTopic::SupplierCrates,
                  "stock steer supplier gear: SupplierVisited t={:.2f} s, before the steer: restored right after the "
                  "steer",
                  m_Play);
            return;
        }

        if (m_GearClock.Visit(m_Play))
            m_Slots[m_GearSlot].restoreWhy =
                fmt::format("{:.0f} s after SupplierVisited at t={:.2f} s", fr::kGearRestoreAfterVisit, m_Play);

        LogTo(fr::LogTopic::SupplierCrates,
              "stock steer supplier gear: SupplierVisited t={:.2f} s: restore due at t={:.2f} s ({})", m_Play,
              m_GearClock.RestoreAt().value_or(-1.0), m_Slots[m_GearSlot].restoreWhy);
    });
}

// --- Clear ---

void StockSteer::Clear() noexcept {
    Contained(kTag, "scene clear", [&] {
        std::lock_guard s_Lock(m_Mutex);

        for (const Slot& s_Slot : m_Slots) {
            if (s_Slot.gear) {
                LogTo(fr::LogTopic::SupplierCrates,
                      "stock steer supplier gear summary {}: {} options, {} eligible items at the last plan{}; {} math "
                      "outputs in the window, {} rewritten; {} values the game changed back (written again); offers: "
                      "{}; {}",
                      ShortSceneName(m_Scene), s_Slot.steered.size(), s_Slot.eligible,
                      s_Slot.eligible >= 2 ? fmt::format(" (target weight {:.4g})", s_Slot.weight) : std::string(),
                      s_Slot.outputs, s_Slot.rewrites, s_Slot.rewrittenAgain,
                      s_Slot.offers.empty() ? std::string("none seen") : s_Slot.offers,
                      s_Slot.restored ? "restored (" + s_Slot.restoreWhy + ")"
                      : !s_Slot.planned
                          ? std::string("never steered (scene left before the steer)")
                          : std::string("no offer and no SupplierVisited seen since the steer: the steered "
                                        "values stay as the scene unloads"));
                continue;
            }

            LogTo(fr::LogTopic::SupplierCrates,
                  "stock steer supplier summary {}: {}: {} options, {} eligible guns at the last plan{}; {} pick "
                  "signals{}; {} math outputs in the window, {} rewritten; {} values the game changed back (written "
                  "again); offers: {}; {}",
                  ShortSceneName(m_Scene), s_Slot.info.label, s_Slot.steered.size(), s_Slot.eligible,
                  s_Slot.eligible >= 2 ? fmt::format(" (weight {:.4g} each)", s_Slot.weight) : std::string(),
                  s_Slot.picks,
                  s_Slot.firstPick >= 0.0 ? fmt::format(" (first at t={:.2f} s)", s_Slot.firstPick) : std::string(),
                  s_Slot.outputs, s_Slot.rewrites, s_Slot.rewrittenAgain,
                  s_Slot.offers.empty() ? std::string("none seen") : s_Slot.offers,
                  !m_SupplierOn     ? "not steered"
                  : s_Slot.restored ? "restored"
                  : !s_Slot.planned ? "never steered (scene left before the steer)"
                                    : "no roll seen: the steered values stay as the scene unloads");

            if (m_ForcePickOn)
                LogTo(fr::LogTopic::SupplierCrates, "stock force pick summary {}: {}: {}", ShortSceneName(m_Scene),
                      s_Slot.info.label,
                      s_Slot.forcedAt < 0.0 ? std::string("never forced")
                      : s_Slot.forceJudged
                          ? "forced, " + s_Slot.forceOutcome
                          : fmt::format("forced visit #{} t={:.2f} s, scene left before the outcome "
                                        "(OnPickDone x{}, OnPickFailed x{})",
                                        s_Slot.forcedVisit, s_Slot.forcedAt, s_Slot.forceDone, s_Slot.forceFailed));
        }

        Forget();
    });
}

void StockSteer::Restore(const char* p_Why) noexcept {
    Contained(kTag, "restore", [&] {
        std::lock_guard s_Lock(m_Mutex);

        for (Slot& s_Slot : m_Slots) {
            if (s_Slot.planned && !s_Slot.restored) {
                s_Slot.restoreWhy = p_Why;
                Restore(s_Slot);
            }
        }

        Forget();
    });
}

// Under m_Mutex.
void StockSteer::Forget() {
    m_Armed.store(false, std::memory_order_relaxed);

    m_Watch.Clear();
    m_Scene.clear();
    m_Prepared = false;
    m_SteerDone = false;
    m_Play = 0.0;
    m_NextCheck = 0.0;
    m_Options.clear();
    m_Slots.clear();
    m_Steered.clear();
    m_SteeredOf.clear();
    m_Loose.clear();
    m_LooseBlock.clear();
    m_StandOffers.clear();
    m_GearSlot = SIZE_MAX;
    m_GearClock.Reset();
    m_GunBurst = fr::GunPickBurst();
}

// Under m_Mutex.
void StockSteer::Watch(const void* p_Entity, size_t p_Index, uint32_t p_Role) {
    if (!m_Watch.Watch(p_Entity, static_cast<uint32_t>(p_Index << 2) | p_Role))
        LogTo(fr::LogTopic::SupplierCrates, "stock steer: watch table full; an entity's pins are not seen");
}
