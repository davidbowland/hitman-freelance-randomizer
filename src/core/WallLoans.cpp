#include "WallLoans.h"

#include "Merces.h" // OneLine

#include <algorithm>
#include <charconv>

namespace fr {

namespace {

bool Contains(const std::vector<Guid>* list, const Guid& id) {
    return list && std::find(list->begin(), list->end(), id) != list->end();
}

std::string_view TrimView(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

template <typename T>
std::optional<T> ParseNumber(std::string_view text) {
    T value {};
    text = TrimView(text);
    const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::string Shown(const WallLoan& loan) {
    return loan.name.empty() ? FormatGuid(loan.id) : "\"" + loan.name + "\" (" + FormatGuid(loan.id) + ")";
}

} // namespace

bool RemovesFromSource(LoadoutSource source) {
    return source == LoadoutSource::Transient;
}

const WallLoan* WallLoans::Find(const Guid& id) const {
    const auto it = std::find_if(loans.begin(), loans.end(), [&](const WallLoan& loan) { return loan.id == id; });
    return it == loans.end() ? nullptr : &*it;
}

bool WallLoans::Add(const Guid& id, std::string_view name) {
    if (Find(id))
        return false;
    WallLoan loan;
    loan.id = id;
    loan.name = OneLine(name, 80);
    loans.push_back(std::move(loan));
    return true;
}

LoanStatus JudgeLoan(const LoadoutState& state, const Guid& id) {
    const auto* wall = state.List("PersistentItems");

    if (!wall || wall->empty())
        return LoanStatus::Unknown;
    if (Contains(wall, id))
        return LoanStatus::OnWall;
    if (Contains(state.List("InventoryItems"), id))
        return LoanStatus::InLoadout;
    return LoanStatus::Missing;
}

const char* LoanStatusName(LoanStatus status) {
    switch (status) {
        case LoanStatus::OnWall: return "on the gear wall";
        case LoanStatus::InLoadout: return "in 47's loadout";
        case LoanStatus::Missing: return "MISSING (neither on the wall nor in the loadout)";
        case LoanStatus::Unknown: return "unknown (no gear wall list seen)";
    }
    return "?";
}

std::vector<LoanOutcome> ReviewLoans(WallLoans& loans, const LoadoutState& state,
                                     std::optional<int64_t> departureUnix) {
    std::vector<LoanOutcome> outcomes;
    std::vector<WallLoan> kept;

    for (WallLoan& loan : loans.loans) {
        LoanOutcome outcome;
        outcome.status = JudgeLoan(state, loan.id);

        if (outcome.status == LoanStatus::OnWall) {
            outcome.closed = true;
        }
        else if (outcome.status == LoanStatus::Missing && departureUnix) {
            const bool newDeparture = loan.lastRepairDeparture != *departureUnix;

            if (newDeparture && loan.repairs >= kMaxLoanRepairs) {
                outcome.gaveUp = true;
                outcome.closed = true;
            }
            else {
                outcome.repair = true;
                if (newDeparture) {
                    ++loan.repairs;
                    loan.lastRepairDeparture = *departureUnix;
                }
            }
        }

        outcome.loan = loan;
        if (!outcome.closed)
            kept.push_back(loan);
        outcomes.push_back(std::move(outcome));
    }

    loans.loans = std::move(kept);
    return outcomes;
}

std::string FormatWallLoans(const WallLoans& loans) {
    std::string text;
    for (const WallLoan& loan : loans.loans)
        text += "loan=" + FormatGuid(loan.id) + "," + std::to_string(loan.repairs) + "," +
                std::to_string(loan.lastRepairDeparture) + "," + OneLine(loan.name, 80) + "\n";
    return text;
}

WallLoans ParseWallLoans(std::string_view text) {
    WallLoans loans;

    while (!text.empty()) {
        const size_t end = text.find('\n');
        const std::string_view line = TrimView(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);

        if (!line.starts_with("loan="))
            continue;

        std::string_view rest = line.substr(5);
        std::string_view fields[3];
        bool ok = true;

        for (std::string_view& field : fields) {
            const size_t comma = rest.find(',');
            if (comma == std::string_view::npos) {
                ok = false;
                break;
            }
            field = rest.substr(0, comma);
            rest = rest.substr(comma + 1);
        }

        if (!ok)
            continue;

        const auto id = ParseGuid(TrimView(fields[0]));
        const auto repairs = ParseNumber<int32_t>(fields[1]);
        const auto last = ParseNumber<int64_t>(fields[2]);

        if (!id || !repairs || !last || *repairs < 0 || loans.Find(*id))
            continue;

        WallLoan loan;
        loan.id = *id;
        loan.repairs = *repairs;
        loan.lastRepairDeparture = *last;
        loan.name = std::string(TrimView(rest));
        loans.loans.push_back(std::move(loan));
    }

    return loans;
}

std::string LoanOutcomeText(const LoanOutcome& outcome) {
    const std::string item = Shown(outcome.loan);

    if (outcome.gaveUp)
        return "loadout: " + item + " is still missing from your gear wall after " + std::to_string(kMaxLoanRepairs) +
               " tries to put it back; giving up (please send this log: the gear wall did not take it)";

    if (outcome.repair)
        return "loadout: " + item +
               " was not carried out and is missing from your gear wall; returned to your gear wall in this mission "
               "(try " +
               std::to_string(outcome.loan.repairs) + " of " + std::to_string(kMaxLoanRepairs) +
               "; it shows on the wall once this mission is saved)";

    switch (outcome.status) {
        case LoanStatus::OnWall: return "loadout: " + item + " is back on your gear wall; the loan is closed";
        case LoanStatus::InLoadout:
            return "loadout: " + item +
                   " was carried out and is in your loadout; hang it on the wall or pack it (if it is ever lost, the "
                   "mod puts it back on the wall)";
        case LoanStatus::Missing:
            return "loadout: " + item +
                   " is missing from your gear wall; it is put back at your next Freelancer mission";
        case LoanStatus::Unknown:
            return "loadout: " + item + ": the gear wall is not known yet; checked again at the next load";
    }
    return "loadout: " + item;
}

size_t CountOf(const std::vector<Guid>* list, const Guid& id) {
    return list ? static_cast<size_t>(std::count(list->begin(), list->end(), id)) : 0;
}

WallReturnPlan PlanWallReturn(const WallReturnInput& input) {
    if (!input.delivered)
        return WallReturnPlan::NotDelivered;
    if (input.packed > 0)
        return WallReturnPlan::Packed;
    if (input.wall == 0)
        return WallReturnPlan::NotOnWall;
    if (input.wall > 1)
        return WallReturnPlan::WallCopies;
    if (input.heroCopies > 1)
        return WallReturnPlan::ExtraCopy;
    return WallReturnPlan::Return;
}

std::string WallReturnPlanText(WallReturnPlan plan) {
    switch (plan) {
        case WallReturnPlan::Return: return "returned to your gear wall at the exit (not saved in your loadout)";
        case WallReturnPlan::NotDelivered: return "not returned: it was not given to 47 in this mission";
        case WallReturnPlan::Packed: return "not returned: you packed it too, so it stays as the game saves it";
        case WallReturnPlan::NotOnWall: return "not returned: it was not on your gear wall when the mission started";
        case WallReturnPlan::WallCopies:
            return "not returned: your gear wall has more than one, so if 47 carries it out it comes back in your "
                   "loadout";
        case WallReturnPlan::ExtraCopy:
            return "not returned: 47 also picked up another one in the mission, so if he carries it out it comes "
                   "back in your loadout";
    }
    return "?";
}

WallReturnVerdict JudgeWallReturn(size_t wall, size_t packed, std::optional<size_t> savedWall,
                                  std::optional<size_t> savedInventory) {
    if (!savedWall || !savedInventory)
        return WallReturnVerdict::Partial;
    const bool inLoadout = *savedInventory > packed;
    if (*savedWall > wall || (inLoadout && *savedWall >= wall))
        return WallReturnVerdict::Doubled;
    if (*savedWall == wall)
        return WallReturnVerdict::Back;
    return inLoadout ? WallReturnVerdict::InLoadout : WallReturnVerdict::Missing;
}

const char* WallReturnVerdictText(WallReturnVerdict verdict) {
    switch (verdict) {
        case WallReturnVerdict::Back: return "OK: back on your gear wall, not in your loadout";
        case WallReturnVerdict::InLoadout:
            return "NOT RETURNED: saved in your loadout and off the wall (the game saved before the mod's return)";
        case WallReturnVerdict::Missing:
            return "MISSING: in neither list; put back on your gear wall at your next Freelancer mission";
        case WallReturnVerdict::Doubled: return "WRONG: DOUBLED (more copies than at the start; please send this log)";
        case WallReturnVerdict::Partial: return "unknown: this save lacks the gear wall or the loadout";
    }
    return "?";
}

} // namespace fr
