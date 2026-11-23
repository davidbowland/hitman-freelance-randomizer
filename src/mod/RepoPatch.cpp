// Writing pro.repo before the scene loads (every entry whose "Parameter"
// names a parameter the mode changes), checking the entities built from it at
// scene start, and restoring pro.repo and (fallback) entity values.

#include "FreelancerRandomizer.h"

#include "DialRows.h"
#include "Dials.h"
#include "Footsteps.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "ParamNames.h"

#include <Glacier/Enums.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <unordered_map>
#include <utility>

namespace {

using fr::ParamLabel;

constexpr int32_t kAlwaysEmitFootsteps =
    static_cast<int32_t>(EDifficultyParameter::EDP_AIAudioPerceptionAlwaysEmitFootstepSoundEvents);

// The Parameter text of a scanned entry.
std::string_view ParameterText(const RepoParamEntry& p_Entry) {
    return std::string_view(p_Entry.parameter);
}

// The known parameter a scanned entry names, or empty (also when our copy of
// the text was cut). pro.repo itself cuts names at 63 characters; those match
// the one enumerator that starts with the text (fr::ParamFromRepoName).
std::optional<int32_t> ParameterOf(const RepoParamEntry& p_Entry) {
    if (p_Entry.parameterCut)
        return std::nullopt;

    return fr::ParamFromRepoName(ParameterText(p_Entry));
}

bool WritableKind(RepoValueKind p_Kind) {
    return p_Kind == RepoValueKind::Float32 || p_Kind == RepoValueKind::Float64;
}

double MsSince(std::chrono::steady_clock::time_point p_Start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_Start).count();
}

fr::PatchKey EntityKey(const ZEntityRef& p_Entity) {
    return {reinterpret_cast<uintptr_t>(p_Entity.GetEntity()), 0};
}

fr::PatchKey RepoKey(const fr::Guid& p_Guid) {
    return {p_Guid.High(), p_Guid.Low()};
}

std::vector<uint64_t> LoadedBrickIds(const TArray<SBrickAllocationInfo>& p_Bricks) {
    std::vector<uint64_t> s_Ids;
    s_Ids.reserve(p_Bricks.size());

    for (size_t i = 0; i < p_Bricks.size(); ++i)
        s_Ids.push_back(p_Bricks[i].m_RuntimeResourceID.GetID());

    return s_Ids;
}

} // namespace

void FreelancerRandomizer::RestorePatches(const TArray<SBrickAllocationInfo>* p_Bricks, const char* p_When) {
    LogTo(fr::LogTopic::AiDifficulty,
          "restoring values patched in {} ({}): {} entity values, {} pro.repo values, {} pro.repo toggles",
          m_PatchedScene, p_When, m_EntityPatches.Size(), m_RepoPatches.Size(), m_RepoToggles.Size());
    RestoreEntityPatches(p_Bricks);
    RestoreRepoPatches();
}

// Writes back the originals of entities found again by a walk of the
// remembered bricks (a stale pointer is never written through).
void FreelancerRandomizer::RestoreEntityPatches(const TArray<SBrickAllocationInfo>* p_Bricks) {
    if (m_EntityPatches.Empty())
        return;

    std::vector<FoundParam> s_Found;

    if (p_Bricks) {
        const std::vector<uint64_t> s_Remembered = m_ParamBricks.LoadedRemembered(LoadedBrickIds(*p_Bricks));

        if (!s_Remembered.empty())
            FindDifficultyParams(*p_Bricks, s_Found, &s_Remembered,
                                 m_ParamBricks.VisitedEstimate(fr::WalkKind::Remembered));
    }

    std::unordered_map<uint64_t, const FoundParam*> s_ByEntity;

    for (const auto& s_Param : s_Found)
        s_ByEntity.emplace(EntityKey(s_Param.entity).a, &s_Param);

    size_t s_Restored = 0, s_Already = 0, s_Elsewhere = 0, s_Failed = 0, s_Missing = 0;

    for (const auto& [s_Key, s_Patch] : m_EntityPatches.Entries()) {
        const auto s_It = s_ByEntity.find(s_Key.a);
        const FoundParam* s_Param = s_It == s_ByEntity.end() ? nullptr : s_It->second;

        if (!s_Param || !s_Param->param || *s_Param->param != s_Patch.tag || !s_Param->value) {
            ++s_Missing;
            continue;
        }

        switch (fr::DecideRestore(*s_Param->value, s_Patch)) {
            case fr::RestoreDecision::Restore: {
                ZEntityRef s_Entity = s_Param->entity;

                if (s_Entity.SetProperty("m_fValue", ZObjectRef::From<float>(static_cast<float>(s_Patch.original)))) {
                    ++s_Restored;
                }
                else {
                    ++s_Failed;
                    Log("restore: {} {:.9g} -> {:.9g}: write failed", fr::ParamLabel(s_Patch.tag), *s_Param->value,
                        s_Patch.original);
                }
                break;
            }
            case fr::RestoreDecision::AlreadyOriginal: ++s_Already; break;
            case fr::RestoreDecision::ChangedElsewhere:
                ++s_Elsewhere;
                LogTo(fr::LogTopic::AiDifficulty,
                      "restore: {} is {:.9g} (wrote {:.9g}, original {:.9g}): changed elsewhere, left alone",
                      fr::ParamLabel(s_Patch.tag), *s_Param->value, s_Patch.written, s_Patch.original);
                break;
        }
    }

    LogTo(fr::LogTopic::AiDifficulty,
          "restore entities: {} restored, {} already original, {} changed elsewhere, {} write failed, {} not found "
          "again (brick not loaded or entity gone; left alone)",
          s_Restored, s_Already, s_Elsewhere, s_Failed, s_Missing);
    m_EntityPatches.Clear();
}

// Writes back the originals in pro.repo. Kept for a retry when pro.repo is
// not available or the pass faulted.
void FreelancerRandomizer::RestoreRepoPatches() {
    if (m_RepoPatches.Empty())
        return;

    std::string s_Why;

    if (!m_Repo.Ready(s_Why)) {
        Log("restore pro.repo: {}; {} values stay patched (retried before the next scene is patched)", s_Why,
            m_RepoPatches.Size());
        return;
    }

    const auto s_Entries = m_RepoPatches.Entries();
    std::vector<fr::Guid> s_Keys;
    s_Keys.reserve(s_Entries.size());

    for (const auto& [s_Key, s_Patch] : s_Entries)
        s_Keys.push_back(fr::Guid::FromHalves(s_Key.a, s_Key.b));

    std::vector<RepoLookup> s_Lookups;

    if (const uint32_t s_Code = m_Repo.Lookup(s_Keys, s_Lookups)) {
        Log("restore pro.repo: lookup faulted (exception 0x{:08X}); {} values stay patched", s_Code,
            m_RepoPatches.Size());
        return;
    }

    size_t s_Restored = 0, s_Already = 0, s_Elsewhere = 0, s_Failed = 0, s_Missing = 0;
    size_t s_ToggleRestored = 0, s_ToggleAlready = 0, s_ToggleElsewhere = 0, s_ToggleFailed = 0, s_ToggleMissing = 0;

    for (size_t i = 0; i < s_Entries.size(); ++i) {
        const fr::Patch& s_Patch = s_Entries[i].second;
        const RepoLookup& s_Lookup = s_Lookups[i];
        const std::string s_Reason = RepoUnwritableReason(s_Lookup);

        if (!s_Reason.empty()) {
            ++s_Missing;
            LogTo(fr::LogTopic::AiDifficulty, "restore pro.repo {} ({}): {}; forgotten", fr::FormatGuid(s_Keys[i]),
                  fr::ParamLabel(s_Patch.tag), s_Reason);
            continue;
        }

        // Footsteps: the Toggle this mod turned on goes back too (same entry, same rules).
        if (const fr::Patch* s_Toggle = m_RepoToggles.Find(s_Entries[i].first)) {
            if (!s_Lookup.hasToggle) {
                ++s_ToggleMissing;
            }
            else {
                switch (fr::DecideRestore(s_Lookup.toggle ? 1.0 : 0.0, *s_Toggle)) {
                    case fr::RestoreDecision::Restore:
                        if (const uint32_t s_Code = m_Repo.WriteToggle(s_Lookup, s_Toggle->original != 0.0)) {
                            ++s_ToggleFailed;
                            Log("restore pro.repo {} ({}) Toggle: write fault (exception 0x{:08X})",
                                fr::FormatGuid(s_Keys[i]), fr::ParamLabel(s_Toggle->tag), s_Code);
                        }
                        else {
                            ++s_ToggleRestored;
                        }
                        break;
                    case fr::RestoreDecision::AlreadyOriginal: ++s_ToggleAlready; break;
                    case fr::RestoreDecision::ChangedElsewhere:
                        ++s_ToggleElsewhere;
                        LogTo(fr::LogTopic::AiDifficulty,
                              "restore pro.repo {} ({}) Toggle is {} (wrote {}, original {}): changed elsewhere, left "
                              "alone",
                              fr::FormatGuid(s_Keys[i]), fr::ParamLabel(s_Toggle->tag), s_Lookup.toggle,
                              s_Toggle->written != 0.0, s_Toggle->original != 0.0);
                        break;
                }
            }
        }

        switch (fr::DecideRestore(s_Lookup.value, s_Patch)) {
            case fr::RestoreDecision::Restore:
                if (const uint32_t s_Code = m_Repo.Write(s_Lookup, s_Patch.original)) {
                    ++s_Failed;
                    Log("restore pro.repo {} ({}): write fault (exception 0x{:08X})", fr::FormatGuid(s_Keys[i]),
                        fr::ParamLabel(s_Patch.tag), s_Code);
                }
                else {
                    ++s_Restored;
                }
                break;
            case fr::RestoreDecision::AlreadyOriginal: ++s_Already; break;
            case fr::RestoreDecision::ChangedElsewhere:
                ++s_Elsewhere;
                LogTo(fr::LogTopic::AiDifficulty,
                      "restore pro.repo {} ({}) is {:.17g} (wrote {:.17g}, original {:.17g}): changed elsewhere, left "
                      "alone",
                      fr::FormatGuid(s_Keys[i]), fr::ParamLabel(s_Patch.tag), s_Lookup.value, s_Patch.written,
                      s_Patch.original);
                break;
        }
    }

    LogTo(fr::LogTopic::AiDifficulty,
          "restore pro.repo: {} restored, {} already original, {} changed elsewhere, {} write faulted, {} no longer "
          "writable",
          s_Restored, s_Already, s_Elsewhere, s_Failed, s_Missing);

    if (!m_RepoToggles.Empty()) {
        LogTo(fr::LogTopic::AiDifficulty,
              "restore pro.repo toggles (footsteps): {} restored, {} already original, {} changed elsewhere, {} write "
              "faulted, {} without a Toggle now",
              s_ToggleRestored, s_ToggleAlready, s_ToggleElsewhere, s_ToggleFailed, s_ToggleMissing);
    }

    m_RepoPatches.Clear();
    m_RepoToggles.Clear();
}

// In a Freelancer mission's LoadScene detour (before the original runs), or at eLoading_AssetsLoaded of a load without LoadScene (a restart):
// resolves the seed (the live seed entity does not exist yet) and writes
// pro.repo, which the globaldata difficulty entities are built from later in
// this load. Nothing is written again at scene start.
void FreelancerRandomizer::EarlyApply(const char* p_When) {
    m_Mode = fr::ChooseMode(m_Config, true);

    const fr::SeedResolution s_Seed = ResolveSceneSeed(std::nullopt, "not read (before the scene loads)");

    UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) {
        p_Snapshot.mode = fr::ModeName(m_Mode);
        p_Snapshot.seed = s_Seed;
    });

    if (!fr::ModeWrites(m_Mode)) {
        m_RepoApplied = true; // nothing to write; no fallback at scene start either
        m_AppliedAt = p_When;
        UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) { p_Snapshot.appliedAt = m_AppliedAt; });
        LogTo(fr::LogTopic::AiDifficulty, "{}: {}: no changes", p_When, fr::ModeName(m_Mode));
        return;
    }

    if (m_Mode == fr::Mode::Dials && s_Seed.source == fr::SeedSource::Fallback)
        LogTo(fr::LogTopic::AiDifficulty,
              "no seed known yet: rolling dials with the fallback seed 0 so the mission still varies");

    m_RepoApplied = ApplyRepo(m_Mode, s_Seed.seed, p_When);

    if (!m_RepoApplied) {
        LogTo(fr::LogTopic::AiDifficulty, "{}: pro.repo not written; values are applied at scene start instead",
              p_When);
        return;
    }

    m_AppliedAt = p_When;
    UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) { p_Snapshot.appliedAt = m_AppliedAt; });
    PublishRows();
}

// Writes every pro.repo entry whose Parameter names a parameter the mode
// changes: all difficulty entries (casual, professional, master), each with
// the parameter's multiplier on its own base (fr::ModeFactor; dials depend
// only on seed, scene and parameter). The original is recorded in the
// repository PatchBook by GUID; a value still holding what was written is
// rolled from its original (no stacking); an entry already patched for a
// different parameter is skipped. Returns false when pro.repo is unavailable
// or its scan faulted before anything was written.
bool FreelancerRandomizer::ApplyRepo(fr::Mode p_Mode, int32_t p_Seed, const char* p_When) {
    std::string s_Why;

    if (!m_Repo.Ready(s_Why)) {
        LogTo(fr::LogTopic::AiDifficulty, "{}: pro.repo: {}; not written", p_When, s_Why);
        return false;
    }

    const auto s_Start = std::chrono::steady_clock::now();
    std::vector<RepoParamEntry> s_Entries;
    size_t s_Overflow = 0;
    size_t s_Scanned = 0;
    const uint32_t s_ScanCode = m_Repo.ScanParameters(s_Entries, &s_Overflow, &s_Scanned);

    LogTo(fr::LogTopic::AiDifficulty, "{}: pro.repo: {} entries, {} with a Parameter ({} scanned{}){}, {:.1f} ms",
          p_When, m_Repo.EntryCount(), s_Entries.size(), s_Scanned,
          s_Overflow ? fmt::format(", {} more over the {} limit", s_Overflow, ProRepository::kMaxParamEntries)
                     : std::string(),
          s_ScanCode ? fmt::format("; the scan faulted (exception 0x{:08X}), so only entries before the fault are "
                                   "written",
                                   s_ScanCode)
                     : std::string(),
          MsSince(s_Start));

    struct Counts {
        size_t written = 0;
        size_t notFloat = 0;
        size_t otherParam = 0;
        size_t failed = 0;
        std::string values; // "<guid> base -> value; ..."
        double factor = 0.0;
    };

    std::map<int32_t, Counts> s_Counts;
    std::map<int32_t, std::string> s_Footsteps; // per gait: what its entries got
    std::string s_AlwaysEmit;                   // EDP_AIAudioPerceptionAlwaysEmitFootstepSoundEvents' entries
    m_PatchedScene = m_Scene;

    for (const RepoParamEntry& s_Entry : s_Entries) {
        const std::optional<int32_t> s_Param = ParameterOf(s_Entry);

        if (!s_Param)
            continue;

        const fr::PatchKey s_Key = RepoKey(s_Entry.guid);

        if (p_Mode == fr::Mode::Dials && *s_Param == kAlwaysEmitFootsteps) {
            ApplyAlwaysEmitEntry(s_Entry, s_AlwaysEmit);
            continue;
        }

        // Footsteps: Value 0 and Toggle false in vanilla, so the dial roll alone never moves them.
        if (p_Mode == fr::Mode::Dials && fr::FootstepGaitIndex(*s_Param) >= 0) {
            ApplyFootstepEntry(s_Entry, *s_Param, p_Seed, s_Footsteps);
            continue;
        }

        if (!s_Entry.hasValue || !WritableKind(s_Entry.kind)) {
            if (fr::ModeFactor(p_Mode, m_Config, p_Seed, m_Scene, *s_Param, 1.0)) {
                ++s_Counts[*s_Param].notFloat;
                LogTo(fr::LogTopic::AiDifficulty, "{} pro.repo {}: {}; skipped", ParamLabel(s_Param),
                      fr::FormatGuid(s_Entry.guid),
                      s_Entry.hasValue ? fmt::format("Value is {}, not float32/float64",
                                                     s_Entry.valueType[0] ? s_Entry.valueType : "<unknown type>")
                                       : std::string("no Value key"));
            }

            continue;
        }

        const double s_Current = s_Entry.value;
        const double s_Base = m_RepoPatches.BaseFor(s_Key, *s_Param, s_Current);
        const std::optional<double> s_Factor = fr::ModeFactor(p_Mode, m_Config, p_Seed, m_Scene, *s_Param, s_Base);

        if (!s_Factor)
            continue;

        Counts& s_Count = s_Counts[*s_Param];
        s_Count.factor = *s_Factor;

        if (const fr::Patch* s_Previous = m_RepoPatches.Find(s_Key); s_Previous && s_Previous->tag != *s_Param) {
            ++s_Count.otherParam;
            LogTo(fr::LogTopic::AiDifficulty, "{} pro.repo {}: entry already patched for {}; skipped",
                  ParamLabel(s_Param), fr::FormatGuid(s_Entry.guid), ParamLabel(static_cast<int32_t>(s_Previous->tag)));
            continue;
        }

        const double s_Value = fr::ScaleRepoValue(s_Base, *s_Factor, s_Entry.kind == RepoValueKind::Float32);

        if (const uint32_t s_Code = m_Repo.Write(s_Entry, s_Value)) {
            ++s_Count.failed;
            Log("{} pro.repo {}: write fault (exception 0x{:08X})", ParamLabel(s_Param), fr::FormatGuid(s_Entry.guid),
                s_Code);
            continue;
        }

        if (s_Base != s_Current) {
            LogTo(fr::LogTopic::AiDifficulty,
                  "{} pro.repo {}: base restored (was written previously): now {:.17g}, base {:.17g}",
                  ParamLabel(s_Param), fr::FormatGuid(s_Entry.guid), s_Current, s_Base);
        }

        m_RepoPatches.Record(s_Key, *s_Param, s_Base, s_Value);
        m_RepoWrites.push_back({s_Entry.guid, *s_Param, s_Entry.kind, s_Base, *s_Factor, s_Value, "written"});
        ++s_Count.written;
        const std::optional<fr::RepoEntryInfo> s_Known = fr::FindRepoEntry(s_Entry.guid);
        s_Count.values +=
            fmt::format("{}{} ({}) {:.9g} -> {:.9g}{}", s_Count.values.empty() ? "" : "; ",
                        fr::FormatGuid(s_Entry.guid), s_Known ? fr::RepoTierLabel(s_Known->tiers) : "tier unknown",
                        s_Base, s_Value, s_Entry.kind == RepoValueKind::Float32 ? " (float32)" : "");
    }

    // What Freelancer gets per parameter, as the overlay shows it, plus the unit ("(?)" after a guessed one).
    std::map<int32_t, std::string> s_Freelancer;

    for (const fr::DialSummary& s_Summary : fr::SummarizeDials(DialEntries()))
        s_Freelancer[s_Summary.param] = fr::FreelancerValueText(s_Summary, true);

    Counts s_Total;
    size_t s_Params = 0;

    for (const auto& [s_Param, s_Count] : s_Counts) {
        s_Total.written += s_Count.written;
        s_Total.notFloat += s_Count.notFloat;
        s_Total.otherParam += s_Count.otherParam;
        s_Total.failed += s_Count.failed;
        s_Params += s_Count.written > 0 ? 1 : 0;

        const auto s_Value = s_Freelancer.find(s_Param);
        LogTo(fr::LogTopic::AiDifficulty, "pro.repo write: {} ({}) x{:.4f}: Freelancer {}; {} entries{}{}{}{}",
              ParamLabel(s_Param), fr::DialDisplayName(s_Param), s_Count.factor,
              s_Value == s_Freelancer.end() ? std::string("not written") : s_Value->second, s_Count.written,
              s_Count.values.empty() ? std::string() : ": " + s_Count.values,
              s_Count.notFloat ? fmt::format("; {} without a float Value", s_Count.notFloat) : std::string(),
              s_Count.otherParam ? fmt::format("; {} patched for another parameter", s_Count.otherParam)
                                 : std::string(),
              s_Count.failed ? fmt::format("; {} write faults", s_Count.failed) : std::string());
    }

    if (p_Mode == fr::Mode::Dials)
        LogFootsteps(p_Seed, s_Footsteps, s_AlwaysEmit);

    LogTo(fr::LogTopic::AiDifficulty,
          "{}: pro.repo: {} entries of {} parameters written ({}); {} skipped without a float Value, {} skipped as "
          "patched for another parameter, {} write faults{}",
          p_When, s_Total.written, s_Params, fr::ModeName(p_Mode), s_Total.notFloat, s_Total.otherParam, s_Total.failed,
          s_ScanCode ? " (Parameter scan incomplete)" : "");

    UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) {
        p_Snapshot.repoSummary = fmt::format("Difficulty table entries changed: {} ({} values, every difficulty)",
                                             s_Total.written, s_Params);
    });

    return s_ScanCode == 0 || s_Total.written > 0;
}

// Footsteps (Footsteps.h): one pro.repo entry of an enabled gait gets Toggle
// true and Value = the configured range x the dial roll; a disabled gait's
// entries stay vanilla. Value and Toggle are recorded for the restore (the
// original from memory when the entry still holds an earlier write).
// p_Report collects per gait what its entries got, for LogFootsteps.
void FreelancerRandomizer::ApplyFootstepEntry(const RepoParamEntry& p_Entry, int32_t p_Param, int32_t p_Seed,
                                              std::map<int32_t, std::string>& p_Report) {
    const fr::FootstepDecision s_Step = fr::DecideFootstep(p_Seed, m_Scene, p_Param, m_Config.footsteps, m_Config.off);

    if (s_Step.skip != fr::FootstepSkip::None)
        return;

    const fr::PatchKey s_Key = RepoKey(p_Entry.guid);
    const std::optional<fr::RepoEntryInfo> s_Known = fr::FindRepoEntry(p_Entry.guid);
    const std::string s_Tier = s_Known ? fr::RepoTierLabel(s_Known->tiers) : "tier unknown";
    std::string& s_Report = p_Report[p_Param];
    const auto s_Add = [&](const std::string& p_Text) { s_Report += (s_Report.empty() ? "" : "; ") + p_Text; };

    if (!p_Entry.hasValue || !WritableKind(p_Entry.kind)) {
        s_Add(fmt::format("{} ({}): no float Value, skipped", fr::FormatGuid(p_Entry.guid), s_Tier));
        return;
    }

    if (const fr::Patch* s_Previous = m_RepoPatches.Find(s_Key); s_Previous && s_Previous->tag != p_Param) {
        s_Add(fmt::format("{} ({}): already patched for {}, skipped", fr::FormatGuid(p_Entry.guid), s_Tier,
                          ParamLabel(static_cast<int32_t>(s_Previous->tag))));
        return;
    }

    const double s_Original = m_RepoPatches.BaseFor(s_Key, p_Param, p_Entry.value);
    const double s_Value = fr::ScaleRepoValue(s_Step.base, s_Step.multiplier, p_Entry.kind == RepoValueKind::Float32);

    if (const uint32_t s_Code = m_Repo.Write(p_Entry, s_Value)) {
        s_Add(fmt::format("{} ({}): Value write fault (exception 0x{:08X})", fr::FormatGuid(p_Entry.guid), s_Tier,
                          s_Code));
        return;
    }

    m_RepoPatches.Record(s_Key, p_Param, s_Original, s_Value);

    // The Toggle: without it the range is most likely ignored (Death Awaits sets both).
    std::string s_Toggle;

    if (!p_Entry.hasToggle) {
        s_Toggle = "no Toggle key (footsteps may stay off)";
    }
    else {
        const double s_ToggleOriginal = m_RepoToggles.BaseFor(s_Key, p_Param, p_Entry.toggle ? 1.0 : 0.0);
        const uint32_t s_Code = p_Entry.toggle ? 0 : m_Repo.WriteToggle(p_Entry, true);

        if (s_Code) {
            s_Toggle = fmt::format("Toggle write fault (exception 0x{:08X}; footsteps stay off)", s_Code);
        }
        else {
            m_RepoToggles.Record(s_Key, p_Param, s_ToggleOriginal, 1.0);
            s_Toggle = fmt::format("Toggle {} -> true", s_ToggleOriginal != 0.0);
        }
    }

    // The overlay's row: "0 -> 19.3", the roll as its multiplier.
    m_RepoWrites.push_back({p_Entry.guid, p_Param, p_Entry.kind, s_Original, s_Step.multiplier, s_Value, "written"});
    s_Add(fmt::format("{} ({}): Value {:.9g} -> {:.9g}, {}", fr::FormatGuid(p_Entry.guid), s_Tier, s_Original, s_Value,
                      s_Toggle));
}

// Footsteps.h: EDP_AIAudioPerceptionAlwaysEmitFootstepSoundEvents'
// entry gets Value 1 and Toggle true while footsteps are on (vanilla has both
// already); both recorded for the restore. The Toggle is written only when the
// Value was recorded: the restore finds a Toggle by its Value's record.
// p_Report: before and after, per entry.
void FreelancerRandomizer::ApplyAlwaysEmitEntry(const RepoParamEntry& p_Entry, std::string& p_Report) {
    const auto s_Add = [&](const std::string& p_Text) { p_Report += (p_Report.empty() ? "" : "; ") + p_Text; };
    const std::string s_Before =
        fmt::format("Value {}, Toggle {}", p_Entry.hasValue ? fmt::format("{:.9g}", p_Entry.value) : "none",
                    p_Entry.hasToggle ? (p_Entry.toggle ? "true" : "false") : "none");
    const fr::AlwaysEmitDecision s_Decision = fr::DecideAlwaysEmit(m_Config.footsteps);

    if (!s_Decision.write) {
        s_Add(fmt::format("{}: {}; left as it is ({})", fr::FormatGuid(p_Entry.guid), s_Before, s_Decision.why));
        return;
    }

    const fr::PatchKey s_Key = RepoKey(p_Entry.guid);

    if (const fr::Patch* s_Previous = m_RepoPatches.Find(s_Key);
        s_Previous && s_Previous->tag != kAlwaysEmitFootsteps) {
        s_Add(fmt::format("{}: already patched for {}, skipped", fr::FormatGuid(p_Entry.guid),
                          ParamLabel(static_cast<int32_t>(s_Previous->tag))));
        return;
    }

    std::string s_Value = "no float Value";
    bool s_ValueRecorded = false;

    if (p_Entry.hasValue && WritableKind(p_Entry.kind)) {
        const double s_Original = m_RepoPatches.BaseFor(s_Key, kAlwaysEmitFootsteps, p_Entry.value);
        const uint32_t s_Code = p_Entry.value == fr::kAlwaysEmitValue ? 0 : m_Repo.Write(p_Entry, fr::kAlwaysEmitValue);

        if (s_Code) {
            s_Value = fmt::format("Value write fault (exception 0x{:08X})", s_Code);
        }
        else {
            m_RepoPatches.Record(s_Key, kAlwaysEmitFootsteps, s_Original, fr::kAlwaysEmitValue);
            s_ValueRecorded = true;
            s_Value = fmt::format("Value {:.9g} -> {:.9g}", s_Original, fr::kAlwaysEmitValue);
        }
    }

    std::string s_Toggle = "no Toggle key";

    if (p_Entry.hasToggle && !s_ValueRecorded) {
        s_Toggle = "Toggle left as it is (no Value change to restore it with)";
    }
    else if (p_Entry.hasToggle) {
        const double s_Original = m_RepoToggles.BaseFor(s_Key, kAlwaysEmitFootsteps, p_Entry.toggle ? 1.0 : 0.0);
        const uint32_t s_Code = p_Entry.toggle ? 0 : m_Repo.WriteToggle(p_Entry, true);

        if (s_Code) {
            s_Toggle = fmt::format("Toggle write fault (exception 0x{:08X})", s_Code);
        }
        else {
            m_RepoToggles.Record(s_Key, kAlwaysEmitFootsteps, s_Original, 1.0);
            s_Toggle = fmt::format("Toggle {} -> true", s_Original != 0.0);
        }
    }

    const std::optional<fr::RepoEntryInfo> s_Known = fr::FindRepoEntry(p_Entry.guid);
    s_Add(fmt::format("{} ({}): before {}; {}, {}", fr::FormatGuid(p_Entry.guid),
                      !s_Known              ? std::string("tier unknown")
                      : s_Known->tiers == 0 ? std::string("in no difficulty collection")
                                            : fr::RepoTierLabel(s_Known->tiers),
                      s_Before, s_Value, s_Toggle));
}

// One line per gait: on (base, roll, the range written, each entry) or why off.
void FreelancerRandomizer::LogFootsteps(int32_t p_Seed, const std::map<int32_t, std::string>& p_Report,
                                        const std::string& p_AlwaysEmit) {
    const fr::FootstepConfig& s_Cfg = m_Config.footsteps;
    LogTo(fr::LogTopic::AiDifficulty,
          "footsteps: {} (footsteps = {}); roll sigma {} lean {} x{}..x{} (footsteps' own); vegetation footsteps are "
          "not reachable (no pro.repo entry; the game keeps its built-in vegetation ranges)",
          s_Cfg.on ? "on" : "OFF: every footstep entry stays vanilla (Value 0, Toggle false)", s_Cfg.on,
          s_Cfg.roll.sigma, s_Cfg.roll.lean, s_Cfg.roll.clampMin, s_Cfg.roll.clampMax);
    LogTo(fr::LogTopic::AiDifficulty, "footsteps always emit ({}): {}", ParamLabel(kAlwaysEmitFootsteps),
          p_AlwaysEmit.empty() ? std::string("no pro.repo entry found") : p_AlwaysEmit);

    for (const fr::FootstepGait& s_Gait : fr::FootstepGaits()) {
        const fr::FootstepDecision s_Step = fr::DecideFootstep(p_Seed, m_Scene, s_Gait.param, s_Cfg, m_Config.off);

        if (s_Step.skip != fr::FootstepSkip::None) {
            LogTo(fr::LogTopic::AiDifficulty, "footsteps: {} ({}): {}; vanilla (not heard)", s_Gait.key,
                  ParamLabel(s_Gait.param),
                  s_Step.skip == fr::FootstepSkip::GaitOff ? fmt::format("footsteps_{} = false", s_Gait.key)
                                                           : std::string(fr::FootstepSkipName(s_Step.skip)));
            continue;
        }

        const fr::RollResult s_Roll {s_Step.multiplier, s_Step.attempts, s_Step.fellBack};
        const std::string s_Note = fr::RollNote(s_Roll);
        const auto s_It = p_Report.find(s_Gait.param);
        LogTo(fr::LogTopic::AiDifficulty, "footsteps: {} ({}): base {:.9g} m {} -> {:.4g} m{}; entries: {}", s_Gait.key,
              ParamLabel(s_Gait.param), s_Step.base,
              s_Step.rolled ? fmt::format("x{:.4f}", s_Step.multiplier) : std::string("(in off: not rolled)"),
              s_Step.value, s_Note.empty() ? std::string() : " (" + s_Note + ")",
              s_It == p_Report.end() ? std::string("none found in pro.repo") : s_It->second);
    }
}

// At scene start after an early apply: the globaldata entities were just
// built from pro.repo, so each entity of a changed parameter should hold its
// linked entry's written value (m_RepositoryId). Logs the counts; nothing is
// written.
void FreelancerRandomizer::CheckEntities(const std::vector<FoundParam>& p_Found) {
    constexpr size_t kMaxDetailLines = 60;

    size_t s_Changed = 0, s_Match = 0, s_Original = 0, s_Other = 0, s_NotPatched = 0, s_OtherParam = 0,
           s_Unreadable = 0, s_Details = 0;
    const int32_t s_Seed = m_SceneSeed ? m_SceneSeed->seed : 0;
    m_EntityValues.clear();

    const auto s_Detail = [&](const std::string& p_Line) {
        if (++s_Details <= kMaxDetailLines)
            LogTo(fr::LogTopic::AiDifficulty, "  scene start check: {}", p_Line);
        else if (s_Details == kMaxDetailLines + 1)
            LogTo(fr::LogTopic::AiDifficulty, "  scene start check: further lines not logged");
    };

    for (const auto& s_Param : p_Found) {
        if (!s_Param.param || !fr::ModeFactor(m_Mode, m_Config, s_Seed, m_Scene, *s_Param.param, 1.0))
            continue;

        ++s_Changed;

        if (!s_Param.value || !s_Param.repoId || s_Param.repoId->IsEmpty()) {
            ++s_Unreadable;
            s_Detail(fmt::format("{}: value or m_RepositoryId unreadable or empty", ParamLabel(s_Param.param)));
            continue;
        }

        std::string& s_Values = m_EntityValues[*s_Param.param];
        s_Values += fmt::format("{}{:.9g}", s_Values.empty() ? "" : " / ", *s_Param.value);

        const fr::Patch* s_Patch = m_RepoPatches.Find(RepoKey(*s_Param.repoId));

        if (!s_Patch) {
            ++s_NotPatched;
            s_Detail(fmt::format("{} = {:.9g}: linked pro.repo {} was not written", ParamLabel(s_Param.param),
                                 *s_Param.value, fr::FormatGuid(*s_Param.repoId)));
            continue;
        }

        if (s_Patch->tag != *s_Param.param) {
            ++s_OtherParam;
            s_Detail(fmt::format("{} = {:.9g}: linked pro.repo {} was written for {}", ParamLabel(s_Param.param),
                                 *s_Param.value, fr::FormatGuid(*s_Param.repoId),
                                 ParamLabel(static_cast<int32_t>(s_Patch->tag))));
        }

        if (fr::EntityMatchesRepo(*s_Param.value, s_Patch->written)) {
            ++s_Match;
        }
        else if (fr::EntityMatchesRepo(*s_Param.value, s_Patch->original)) {
            ++s_Original;
            s_Detail(fmt::format("{} = {:.9g}: still the original (pro.repo {} written {:.9g}, original {:.9g})",
                                 ParamLabel(s_Param.param), *s_Param.value, fr::FormatGuid(*s_Param.repoId),
                                 s_Patch->written, s_Patch->original));
        }
        else {
            ++s_Other;
            s_Detail(fmt::format("{} = {:.9g}: neither written nor original (pro.repo {} written {:.9g}, original "
                                 "{:.9g})",
                                 ParamLabel(s_Param.param), *s_Param.value, fr::FormatGuid(*s_Param.repoId),
                                 s_Patch->written, s_Patch->original));
        }
    }

    LogTo(fr::LogTopic::AiDifficulty,
          "scene start check: {} difficulty entities of changed parameters: {} hold their pro.repo entry's written "
          "value, {} still hold the original, {} hold another value, {} are linked to an entry that was not written, "
          "{} unreadable{}",
          s_Changed, s_Match, s_Original, s_Other, s_NotPatched, s_Unreadable,
          s_OtherParam ? fmt::format(" ({} linked to an entry written for another parameter)", s_OtherParam)
                       : std::string());

    for (const int32_t s_Param : fr::CheckedParams(m_Mode)) {
        std::string s_Repo;

        for (const auto& s_Write : m_RepoWrites) {
            if (s_Write.param == s_Param)
                s_Repo += fmt::format("{}{:.9g}", s_Repo.empty() ? "" : " / ", s_Write.written);
        }

        const auto s_It = m_EntityValues.find(s_Param);
        LogTo(fr::LogTopic::AiDifficulty, "scene start check: {}: entities [{}], pro.repo written [{}]",
              ParamLabel(s_Param), s_It == m_EntityValues.end() ? std::string("none found") : s_It->second,
              s_Repo.empty() ? std::string("none") : s_Repo);
    }

    // Footsteps: the entities built from the written entries should hold the range and m_bToggle true.
    if (!m_RepoToggles.Empty()) {
        for (const auto& s_Param : p_Found) {
            if (!s_Param.param || fr::FootstepGaitIndex(*s_Param.param) < 0 || !s_Param.repoId)
                continue;

            const fr::Patch* s_Toggle = m_RepoToggles.Find(RepoKey(*s_Param.repoId));
            const std::optional<fr::RepoEntryInfo> s_Known = fr::FindRepoEntry(*s_Param.repoId);
            LogTo(fr::LogTopic::AiDifficulty, "footsteps check at scene start: {} entity {} ({}) = {}, toggle {}: {}",
                  ParamLabel(s_Param.param), fr::FormatGuid(*s_Param.repoId),
                  s_Known ? fr::RepoTierLabel(s_Known->tiers) : "tier unknown",
                  s_Param.value ? fmt::format("{:.9g}", *s_Param.value) : std::string("<unreadable>"),
                  s_Param.toggle ? (*s_Param.toggle ? "true" : "false") : "<unreadable>",
                  !s_Toggle                           ? "its entry's Toggle was not written"
                  : s_Param.toggle && *s_Param.toggle ? "TOGGLE ON (built from the written entry)"
                                                      : "TOGGLE STILL OFF (the entity did not take the Toggle)");
        }
    }

    UpdateSnapshot([&](OverlaySnapshot& p_Snapshot) {
        p_Snapshot.entityCheck =
            fmt::format("Level copies at the start: {} of {} have the new value ({} still the game's, {} other).",
                        s_Match, s_Changed, s_Original, s_Other + s_NotPatched + s_Unreadable);
    });
}
