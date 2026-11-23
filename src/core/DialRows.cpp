#include "DialRows.h"

#include "DialInfo.h"

#include <Glacier/Enums.h>

#include <algorithm>
#include <map>
#include <string_view>
#include <utility>

namespace fr {

namespace {

struct TierEntry {
    std::string_view guid;
    uint8_t tiers;
    int32_t param;
    int32_t feeds; // EDP_Invalid when in no collection
    double value;
};

constexpr int32_t P(EDifficultyParameter param) {
    return static_cast<int32_t>(param);
}

using enum EDifficultyParameter;

// Every pro.repo entry whose Parameter names a difficulty parameter, with the
// difficulty collections it is in ("Easy" eDL_EASY, "Default" eDL_NORMAL,
// "Hard" eDL_HARD), the parameter of the collection entity built from it, and
// its Value, as measured in game. Fixed game data; it changes only
// if an update rebuilds pro.repo.
constexpr TierEntry kTiers[] = {
#include "RepoTiers.inc"
};

const TierEntry* Find(const Guid& guid) {
    const std::string text = FormatGuid(guid);

    for (const auto& entry : kTiers) {
        if (entry.guid == text)
            return &entry;
    }

    return nullptr;
}

bool IsProfessional(const DialEntry& entry) {
    return entry.tiers && (*entry.tiers & kProfessional);
}

} // namespace

std::optional<RepoEntryInfo> FindRepoEntry(const Guid& guid) {
    const TierEntry* entry = Find(guid);

    if (!entry)
        return std::nullopt;

    const bool inCollection = entry->feeds != P(EDP_Invalid);
    return RepoEntryInfo {entry->tiers, entry->param, inCollection ? std::optional(entry->feeds) : std::nullopt,
                          entry->value};
}

std::vector<double> ProfessionalRepoValues(int32_t param) {
    std::vector<double> values;

    for (const auto& entry : kTiers) {
        if (entry.param == param && (entry.tiers & kProfessional))
            values.push_back(entry.value);
    }

    return values;
}

std::string RepoTierLabel(uint8_t tiers) {
    constexpr std::pair<uint8_t, const char*> kNames[] = {
        {kCasual, "casual"}, {kProfessional, "professional"}, {kMaster, "master"}};
    std::string label;

    for (const auto& [bit, name] : kNames) {
        if (tiers & bit)
            label += (label.empty() ? "" : "+") + std::string(name);
    }

    return label.empty() ? "other" : label;
}

std::vector<DialSummary> SummarizeDials(std::span<const DialEntry> entries) {
    std::vector<DialSummary> out;
    std::map<int32_t, size_t> index;

    for (const DialEntry& entry : entries) {
        const auto [it, added] = index.emplace(entry.param, out.size());

        if (added) {
            out.push_back({entry.param,
                           entry.multiplier,
                           false,
                           FreelancerValue::Missing,
                           0,
                           0,
                           std::nullopt,
                           std::nullopt,
                           {},
                           {}});
        }

        out[it->second].entries.push_back(entry);
    }

    for (DialSummary& summary : out) {
        const DialEntry* only = nullptr;  // the only professional entry
        const DialEntry* match = nullptr; // the only professional entry feeding this parameter
        size_t matches = 0;
        const DialEntry* unknown = nullptr;
        const DialEntry* uncollected = nullptr; // the first uncollected entity
        bool sameStatus = true;

        for (const DialEntry& entry : summary.entries) {
            if (IsProfessional(entry)) {
                ++summary.professionalEntries;
                only = &entry;

                if (entry.feeds == summary.param) {
                    ++matches;
                    match = &entry;
                }
            }

            if (entry.uncollected) {
                ++summary.uncollectedEntries;

                if (!uncollected)
                    uncollected = &entry;
            }
            else if (!entry.tiers && !unknown) {
                unknown = &entry;
            }

            summary.multipliersDiffer |= entry.multiplier != summary.entries.front().multiplier;
            sameStatus &= entry.status == summary.entries.front().status;
        }

        const DialEntry* chosen = nullptr;

        if (summary.professionalEntries == 1)
            chosen = only;
        else if (summary.professionalEntries > 1 && matches == 1)
            chosen = match;

        const std::vector<double> repoValues = ProfessionalRepoValues(summary.param);
        const bool repoZero =
            !repoValues.empty() && std::all_of(repoValues.begin(), repoValues.end(), [](double v) { return v == 0.0; });

        if (chosen)
            summary.freelancer = FreelancerValue::Written;
        else if (summary.professionalEntries > 1)
            summary.freelancer = FreelancerValue::Ambiguous;
        else if (uncollected)
            summary.freelancer = FreelancerValue::Uncollected;
        else if (repoZero)
            summary.freelancer = FreelancerValue::Zero;
        else if (unknown)
            summary.freelancer = FreelancerValue::TierUnknown;
        else
            summary.freelancer = FreelancerValue::Missing;

        const DialEntry* shown = summary.freelancer == FreelancerValue::Written       ? chosen
                                 : summary.freelancer == FreelancerValue::Uncollected ? uncollected
                                 : summary.freelancer == FreelancerValue::TierUnknown ? unknown
                                                                                      : nullptr;

        if (shown) {
            summary.multiplier = shown->multiplier;
            summary.before = shown->base;
            summary.after = shown->written;
        }

        if (chosen)
            summary.status = chosen->status;
        else if (summary.freelancer == FreelancerValue::Uncollected && summary.uncollectedEntries == 1)
            summary.status = uncollected->status;
        else
            summary.status = sameStatus ? summary.entries.front().status : "mixed";
    }

    return out;
}

bool NoEffect(const DialSummary& summary) {
    return summary.freelancer == FreelancerValue::Zero || summary.freelancer == FreelancerValue::Missing;
}

DialRowView ViewDialRow(const DialSummary& summary, std::span<const int32_t> coveredParams) {
    if (!NoEffect(summary))
        return {true, false};

    const bool covered = std::find(coveredParams.begin(), coveredParams.end(), summary.param) != coveredParams.end();
    return {!covered, !covered};
}

std::string FreelancerValueText(const DialSummary& summary, bool withUnit) {
    const auto change = [&] {
        const DialInfo* info = FindDialInfo(summary.param);
        const DialUnit unit = info ? info->unit : DialUnit::Count;

        if (withUnit && info)
            return FormatDialChange(*summary.before, *summary.after, unit, info->unitBasis);

        return FormatDialValues(*summary.before, *summary.after, unit);
    };

    switch (summary.freelancer) {
        case FreelancerValue::Written: return change();
        case FreelancerValue::Ambiguous: return std::to_string(summary.professionalEntries) + " Professional copies";
        case FreelancerValue::Uncollected: {
            if (summary.uncollectedEntries <= 1)
                return change() + " (level copy, may have no effect)";

            const DialInfo* info = FindDialInfo(summary.param);
            const DialUnit unit = info ? info->unit : DialUnit::Count;
            std::string values;

            for (const DialEntry& entry : summary.entries) {
                if (entry.uncollected)
                    values += (values.empty() ? "" : ", ") + FormatDialValues(entry.base, entry.written, unit);
            }

            // The unit as FormatDialChange adds it: its text after the bare values.
            std::string suffix;

            if (withUnit && info) {
                const std::string full = FormatDialChange(*summary.before, *summary.after, unit, info->unitBasis);
                suffix = full.substr(FormatDialValues(*summary.before, *summary.after, unit).size());
            }

            return values + suffix + " (" + std::to_string(summary.uncollectedEntries) +
                   " level copies, may have no effect)";
        }
        case FreelancerValue::Zero: return "Professional value is 0 (unchanged)";
        case FreelancerValue::TierUnknown: return change() + " (difficulty unknown)";
        case FreelancerValue::Missing: break;
    }

    return "no professional entry";
}

} // namespace fr
