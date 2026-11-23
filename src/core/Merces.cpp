#include "Merces.h"

#include "PrestigeCards.h" // the official prestige names
#include "Scene.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#include <system_error>

namespace fr {

namespace {

bool IsJsonSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// JsonScalarsWithKeyContaining for one part; appends to found.
void ScalarsForPart(std::string_view json, std::string_view part, std::vector<JsonScalar>& found) {
    if (part.empty())
        return;

    for (size_t at = json.find(part); at != std::string_view::npos; at = json.find(part, at + 1)) {
        // The match lies in a key: its opening quote follows '{' or ',' and its
        // closing quote is followed by ':' (a string value is preceded by ':').
        const size_t open = at == 0 ? std::string_view::npos : json.rfind('"', at - 1);
        const size_t close = json.find('"', at + part.size());

        if (open == std::string_view::npos || open == 0 || close == std::string_view::npos || close + 2 >= json.size())
            continue;

        if ((json[open - 1] != '{' && json[open - 1] != ',') || json[close + 1] != ':')
            continue;

        const size_t valueAt = close + 2;
        size_t valueEnd = 0;

        if (json[valueAt] == '{' || json[valueAt] == '[')
            continue;

        if (json[valueAt] == '"') {
            valueEnd = json.find('"', valueAt + 1);

            if (valueEnd == std::string_view::npos)
                continue;

            ++valueEnd;
        }
        else {
            valueEnd = std::min(json.find_first_of(",}]", valueAt), json.size());

            if (valueEnd == valueAt)
                continue;
        }

        found.push_back({json.substr(open + 1, close - open - 1), json.substr(valueAt, valueEnd - valueAt)});
        // A key containing the part twice is reported once.
        at = close;
    }
}

} // namespace

std::optional<size_t> PayoutAmountIndex(int64_t value) {
    for (size_t i = 0; i < kPayoutAmounts.size(); ++i) {
        if (kPayoutAmounts[i] == value)
            return i;
    }

    return std::nullopt;
}

NumericKind NumericKindOf(std::string_view typeName) {
    if (typeName == "int32")
        return NumericKind::Int32;

    if (typeName == "int64")
        return NumericKind::Int64;

    if (typeName == "float32")
        return NumericKind::Float32;

    return NumericKind::Other;
}

size_t NumericKindSize(NumericKind kind) {
    switch (kind) {
        case NumericKind::Int32: return 4;
        case NumericKind::Int64: return 8;
        case NumericKind::Float32: return 4;
        case NumericKind::Other: break;
    }

    return 0;
}

std::string_view JsonEventName(std::string_view json) {
    constexpr std::string_view key = "\"Name\"";
    const size_t keyAt = json.find(key);

    if (keyAt == std::string_view::npos)
        return {};

    size_t at = keyAt + key.size();

    while (at < json.size() && IsJsonSpace(json[at]))
        ++at;

    if (at >= json.size() || json[at] != ':')
        return {};

    ++at;

    while (at < json.size() && IsJsonSpace(json[at]))
        ++at;

    if (at >= json.size() || json[at] != '"')
        return {};

    const size_t start = at + 1;
    const size_t end = json.find('"', start);

    if (end == std::string_view::npos)
        return {};

    return json.substr(start, end - start);
}

std::vector<JsonScalar> JsonScalarsWithKeyContaining(std::string_view json, std::span<const std::string_view> parts) {
    std::vector<JsonScalar> found;

    for (const std::string_view part : parts)
        ScalarsForPart(json, part, found);

    // Text order; a key matched by more than one part once.
    std::sort(found.begin(), found.end(),
              [](const JsonScalar& a, const JsonScalar& b) { return a.key.data() < b.key.data(); });
    found.erase(std::unique(found.begin(), found.end(),
                            [](const JsonScalar& a, const JsonScalar& b) { return a.key.data() == b.key.data(); }),
                found.end());
    return found;
}

std::string OneLine(std::string_view text, size_t maxChars) {
    std::string line(text.substr(0, maxChars));

    for (char& c : line) {
        const auto u = static_cast<unsigned char>(c);

        if (u < 0x20 || u == 0x7F)
            c = ' ';
    }

    if (text.size() > maxChars)
        line += "... (" + std::to_string(text.size()) + " chars)";

    return line;
}

const char* PayoutKindName(PayoutKind kind) {
    switch (kind) {
        case PayoutKind::Normal: return "normal";
        case PayoutKind::Prestige: return "prestige";
        case PayoutKind::Unknown: break;
    }

    return "unknown";
}

bool PrestigeMemory::Observe(uint64_t id, bool isPrestige) {
    std::set<uint64_t>& into = isPrestige ? prestige : normal;
    std::set<uint64_t>& from = isPrestige ? normal : prestige;
    const bool moved = from.erase(id) > 0;
    return into.insert(id).second || moved;
}

PayoutClass ClassifyPayout(const PrestigeMemory& memory, uint64_t id, int32_t base) {
    if (memory.prestige.contains(id))
        return {memory.bonus ? PayoutKind::Prestige : PayoutKind::Unknown, true};

    if (memory.normal.contains(id))
        return {PayoutKind::Normal, true};

    return {base >= 1 && base <= kNormalPayoutMax ? PayoutKind::Normal : PayoutKind::Unknown, false};
}

PayoutPlan
PlanPayoutWriteTo(int32_t current, PayoutKind kind, int32_t bonus, std::optional<int32_t> lastWritten,
                  std::optional<int32_t> lastBase,
                  const std::function<std::optional<int32_t>(int32_t base, PayoutKind kind, int32_t bonus)>& target) {
    // Checked first: a kept table holds this mod's value, which must not be
    // taken as a new base (that would double twice).
    if (lastWritten && current != lastBase) {
        const bool withBonus =
            kind == PayoutKind::Prestige && static_cast<int64_t>(current) == static_cast<int64_t>(*lastWritten) + bonus;

        if (current == *lastWritten || withBonus)
            return {PayoutAction::AlreadyWritten, current};
    }

    if (!PayoutAmountIndex(current))
        return {PayoutAction::NotAnAmount, current};

    if (kind == PayoutKind::Unknown)
        return {PayoutAction::Unknown, current};

    const std::optional<int32_t> planned = target(current, kind, bonus);

    if (!planned)
        return {PayoutAction::NoTarget, current};

    return {*planned == current ? PayoutAction::Unchanged : PayoutAction::Write, *planned};
}

std::optional<int32_t> CommonBonus(std::span<const int32_t> deltas, bool* disagree) {
    *disagree = false;
    std::optional<int32_t> common;

    for (const int32_t delta : deltas) {
        if (delta <= 0)
            continue;

        if (common && *common != delta) {
            *disagree = true;
            return std::nullopt;
        }

        common = delta;
    }

    return common;
}

LearnResult LearnFromScene(PrestigeMemory& memory, std::span<const EntryChange> changes) {
    LearnResult result;
    std::vector<int32_t> deltas;
    deltas.reserve(changes.size());

    for (const EntryChange& change : changes)
        deltas.push_back(change.delta);

    result.bonus = CommonBonus(deltas, &result.disagree);
    size_t knownUnchanged = 0;

    for (const EntryChange& change : changes) {
        const bool known = memory.prestige.contains(change.id);
        const bool withBonus = result.bonus && change.delta == *result.bonus;
        result.withBonus += withBonus ? 1 : 0;
        result.knownRead += known ? 1 : 0;
        result.knownWithBonus += known && withBonus ? 1 : 0;
        knownUnchanged += known && change.delta == 0 ? 1 : 0;
    }

    if (result.disagree)
        return result;

    if (!result.bonus) {
        // Every known prestige entry read is unchanged: the game no longer
        // adds a bonus.
        if (memory.bonus && *memory.bonus != 0 && result.knownRead > 0 && knownUnchanged == result.knownRead) {
            memory.bonus = 0;
            result.bonusZero = true;
            result.changed = true;
        }

        return result;
    }

    result.trusted =
        result.withBonus >= kMinBonusEntries || (result.knownRead > 0 && 2 * result.knownWithBonus > result.knownRead);

    if (!result.trusted)
        return result;

    const std::optional<int32_t> previous = memory.bonus;

    if (previous != result.bonus) {
        memory.bonus = result.bonus;
        result.changed = true;
    }

    for (const EntryChange& change : changes) {
        if (change.delta == *result.bonus) {
            result.changed |= memory.Observe(change.id, true);
        }
        else if (change.delta == 0) {
            if (memory.prestige.contains(change.id) && previous && *previous != *result.bonus) {
                ++result.keptPrestige;
                continue;
            }

            result.changed |= memory.Observe(change.id, false);
        }
    }

    return result;
}

std::string FormatIdList(const std::set<uint64_t>& ids) {
    std::string out;

    for (const uint64_t id : ids) {
        char text[17];
        std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(id));
        out += (out.empty() ? "" : ",") + std::string(text);
    }

    return out;
}

std::set<uint64_t> ParseIdList(std::string_view text, std::vector<std::string>& bad) {
    std::set<uint64_t> ids;

    while (!text.empty()) {
        const size_t comma = text.find(',');
        std::string_view entry = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);

        while (!entry.empty() && IsJsonSpace(entry.front()))
            entry.remove_prefix(1);

        while (!entry.empty() && IsJsonSpace(entry.back()))
            entry.remove_suffix(1);

        if (entry.empty())
            continue;

        uint64_t id = 0;
        const auto [end, error] = std::from_chars(entry.data(), entry.data() + entry.size(), id, 16);

        if (entry.size() > 16 || error != std::errc() || end != entry.data() + entry.size()) {
            bad.emplace_back(entry);
            continue;
        }

        ids.insert(id);
    }

    return ids;
}

const char* TableBrickTierName(TableBrickTier tier) {
    switch (tier) {
        case TableBrickTier::Held: return "held the table earlier";
        case TableBrickTier::Named: return "named like an evergreen or mission brick";
        case TableBrickTier::Other: return "other";
    }

    return "other";
}

TableBrickOrder OrderTableBricks(std::span<const uint64_t> loaded, const std::set<uint64_t>& held,
                                 const std::set<uint64_t>& named) {
    TableBrickOrder result;
    result.order.reserve(loaded.size());
    result.tiers.reserve(loaded.size());

    for (const TableBrickTier tier : {TableBrickTier::Held, TableBrickTier::Named, TableBrickTier::Other}) {
        for (size_t i = 0; i < loaded.size(); ++i) {
            const TableBrickTier own = held.contains(loaded[i])    ? TableBrickTier::Held
                                       : named.contains(loaded[i]) ? TableBrickTier::Named
                                                                   : TableBrickTier::Other;

            if (own != tier)
                continue;

            result.order.push_back(i);
            result.tiers.push_back(tier);
        }
    }

    return result;
}

bool TableSearchDone(bool found, std::span<const uint64_t> unwalked, const std::set<uint64_t>& held) {
    if (!found)
        return false;

    return std::none_of(unwalked.begin(), unwalked.end(), [&](uint64_t rid) { return held.contains(rid); });
}

LearnedEntryIds::LearnedEntryIds(const PrestigeMemory& memory) {
    m_Ids.insert(memory.prestige.begin(), memory.prestige.end());
    m_Ids.insert(memory.normal.begin(), memory.normal.end());
}

bool LearnedEntryIds::Match(uint64_t id) {
    if (!m_Ids.contains(id))
        return false;

    m_Found.insert(id);
    return true;
}

bool LearnedEntryIds::MostlyFound() const {
    return !m_Ids.empty() && 100 * m_Found.size() >= kLearnedFoundPercent * m_Ids.size();
}

bool TableFound(size_t tableIdInstances, const LearnedEntryIds& learned) {
    return tableIdInstances > 0 || learned.MostlyFound();
}

bool LearnedInstanceTrusted(size_t learnedChildren, size_t learnedSize) {
    return learnedChildren >= kMinBonusEntries && 100 * learnedChildren >= kLearnedFoundPercent * learnedSize;
}

bool IsLikelyTableBrickPath(std::string_view path) {
    const size_t slash = path.rfind('/');
    const std::string name = ToLower(slash == std::string_view::npos ? path : path.substr(slash + 1));

    return name.find("evergreen") != std::string::npos || name.find("freelancer") != std::string::npos ||
           name.find("mission") != std::string::npos;
}

std::optional<int32_t> PrestigeHelperObjective(uint64_t id) {
    for (const PrestigeHelper& helper : kPrestigeHelpers) {
        if (helper.id == id)
            return helper.objective;
    }

    return std::nullopt;
}

const char* PrestigeObjectiveName(int32_t objective) {
    const PrestigeChoice* choice = PrestigeChoiceByObjective(objective);
    return choice ? choice->name : nullptr;
}

const char* DisplayActionName(DisplayAction action) {
    switch (action) {
        case DisplayAction::Write: return "write";
        case DisplayAction::InStep: return "in step";
        case DisplayAction::Unchanged: return "unchanged";
        case DisplayAction::Unrecognized: return "unrecognized";
    }

    return "unrecognized";
}

DisplayPlan PlanDisplayWrite(int32_t now, int32_t base, int32_t written, int32_t bonus,
                             std::optional<int32_t> lastWrite) {
    const auto plus = [](int32_t a, int32_t b) { return static_cast<int64_t>(a) + b; };

    if (written == base)
        return {DisplayAction::Unchanged, now, false};

    if (lastWrite && (now == *lastWrite || (bonus > 0 && plus(*lastWrite, bonus) == now)))
        return {DisplayAction::InStep, now, false};

    if (now == base)
        return {DisplayAction::Write, written, false};

    if (bonus > 0 && plus(base, bonus) == now && plus(written, bonus) <= INT32_MAX)
        return {DisplayAction::Write, written + bonus, true};

    if (now == written || (bonus > 0 && plus(written, bonus) == now))
        return {DisplayAction::InStep, now, false};

    return {DisplayAction::Unrecognized, now, false};
}

std::optional<int32_t> ObjectiveFromName(std::string_view name) {
    std::optional<int32_t> found;
    size_t i = 0;

    while (i < name.size()) {
        if (name[i] < '0' || name[i] > '9') {
            ++i;
            continue;
        }

        size_t end = i;
        int32_t value = 0;

        while (end < name.size() && name[end] >= '0' && name[end] <= '9') {
            if (end - i < 4)
                value = value * 10 + (name[end] - '0');

            ++end;
        }

        if (end - i == 3 && value >= kPrestigeObjectiveFirst && value <= kPrestigeObjectiveLast) {
            if (found && *found != value)
                return std::nullopt;

            found = value;
        }

        i = end;
    }

    return found;
}

std::optional<int32_t> AgreedObjective(std::span<const int32_t> candidates) {
    std::optional<int32_t> agreed;

    for (const int32_t candidate : candidates) {
        if (candidate < kPrestigeObjectiveFirst || candidate > kPrestigeObjectiveLast)
            continue;

        if (agreed && *agreed != candidate)
            return std::nullopt;

        agreed = candidate;
    }

    return agreed;
}

std::optional<int32_t> ObjectiveByUniqueAmount(int32_t shown, int32_t bonus, std::span<const PrestigeBase> bases) {
    std::optional<int32_t> match;

    for (const PrestigeBase& entry : bases) {
        if (static_cast<int64_t>(entry.base) + bonus != shown)
            continue;

        if (match && *match != entry.objective)
            return std::nullopt;

        match = entry.objective;
    }

    return match;
}

} // namespace fr
