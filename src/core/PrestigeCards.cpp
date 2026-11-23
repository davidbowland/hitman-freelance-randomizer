#include "PrestigeCards.h"

#include "Rng.h"

#include <algorithm>
#include <charconv>
#include <vector>

namespace fr {

const PrestigeChoice* PrestigeChoiceByObjective(int32_t objective) {
    for (const PrestigeChoice& choice : kPrestigeChoices) {
        if (choice.objective == objective)
            return &choice;
    }

    return nullptr;
}

const PrestigeChoice* PrestigeChoiceByEntry(uint64_t entryId) {
    for (const PrestigeChoice& choice : kPrestigeChoices) {
        if (choice.entryId == entryId)
            return &choice;
    }

    return nullptr;
}

std::optional<ChainChoice> PrestigeChoiceInChain(std::span<const uint64_t> p_Chain) {
    for (size_t i = 0; i < p_Chain.size() && p_Chain[i] != 0; ++i) {
        if (const PrestigeChoice* choice = PrestigeChoiceByEntry(p_Chain[i]))
            return ChainChoice {choice, i};
    }

    return std::nullopt;
}

std::optional<size_t> PickerOfSelector(uint64_t p_Id, uintptr_t p_Parent, std::span<const uintptr_t> p_Pickers) {
    if (p_Id != kPrestigeSelectorId || p_Parent == 0)
        return std::nullopt;

    for (size_t i = 0; i < p_Pickers.size(); ++i) {
        if (p_Pickers[i] == p_Parent)
            return i;
    }

    return std::nullopt;
}

const std::set<int32_t>& DefaultPrestigeExclude() {
    static const std::set<int32_t> s_Default {115, 117, 118, 119, 120};
    return s_Default;
}

bool PrestigeChoiceDrawable(const PrestigeChoice& p_Choice, const std::set<int32_t>& p_Excluded) {
    return !p_Choice.excluded && !p_Excluded.contains(p_Choice.objective);
}

namespace {

// No picker's m_Choices lists them.
constexpr std::array<int32_t, 3> kNeverOffered {113, 114, 124};

constexpr bool IsExcludeOption(const PrestigeChoice& choice) {
    return !choice.excluded &&
           std::find(kNeverOffered.begin(), kNeverOffered.end(), choice.objective) == kNeverOffered.end();
}

constexpr size_t CountExcludeOptions() {
    size_t n = 0;

    for (const PrestigeChoice& choice : kPrestigeChoices)
        n += IsExcludeOption(choice) ? 1 : 0;

    return n;
}

constexpr std::array<int32_t, CountExcludeOptions()> MakeExcludeOptions() {
    std::array<int32_t, CountExcludeOptions()> out {};
    size_t n = 0;

    for (const PrestigeChoice& choice : kPrestigeChoices) {
        if (IsExcludeOption(choice))
            out[n++] = choice.objective;
    }

    return out;
}

constexpr auto kExcludeOptions = MakeExcludeOptions();

} // namespace

std::span<const int32_t> PrestigeExcludeOptions() {
    return kExcludeOptions;
}

bool PrestigeExcludeLeavesEnough(const std::set<int32_t>& p_Excluded) {
    return DrawPrestigeCards(0, 0, kExcludeOptions, PrestigeRejected {}, p_Excluded).has_value();
}

std::string PrestigeChoiceText(int32_t objective) {
    const PrestigeChoice* choice = PrestigeChoiceByObjective(objective);
    return std::to_string(objective) + " " + (choice ? choice->name : "(unknown)");
}

namespace {

bool Contains(std::span<const int32_t> p_Values, int32_t p_Value) {
    return std::find(p_Values.begin(), p_Values.end(), p_Value) != p_Values.end();
}

} // namespace

bool PrestigeCardAccepts(size_t p_Card, int32_t p_Objective) {
    const PrestigeChoice* choice = PrestigeChoiceByObjective(p_Objective);

    if (!choice || p_Card >= kPrestigeCards)
        return false;

    if (p_Card == 0)
        return choice->category == PrestigeCategory::SilentAssassin || choice->category == PrestigeCategory::Timed;

    return true;
}

namespace {

struct Candidate {
    const PrestigeChoice* choice;
    int outside; // 1: in prestige_exclude (a fill only when the allowed ones cannot fill the cards)
};

void Shuffle(std::vector<Candidate>& p_List, uint64_t p_State) {
    // Fisher-Yates.
    for (size_t i = p_List.size(); i > 1; --i) {
        const size_t j = static_cast<size_t>(SplitMix64(p_State) % i);
        std::swap(p_List[i - 1], p_List[j]);
    }
}

// The allowed objectives in the order the seed and the counter shuffle them,
// then (p_Fill) those only prestige_exclude leaves out, shuffled apart. Both
// in table order before the shuffle, so the order of p_Available never matters.
std::vector<Candidate> Candidates(int32_t seed, uint32_t counter, std::span<const int32_t> p_Available,
                                  const std::set<int32_t>& p_Excluded, bool p_Fill) {
    std::vector<Candidate> allowed;
    std::vector<Candidate> fill;

    for (const PrestigeChoice& choice : kPrestigeChoices) {
        if (choice.excluded || !Contains(p_Available, choice.objective))
            continue;

        if (!p_Excluded.contains(choice.objective))
            allowed.push_back({&choice, 0});
        else if (p_Fill)
            fill.push_back({&choice, 1});
    }

    Shuffle(allowed, DialHash(seed, "", "prestige_shuffle", static_cast<uint64_t>(counter)));
    Shuffle(fill, DialHash(seed, "", "prestige_shuffle_fill", static_cast<uint64_t>(counter)));
    allowed.insert(allowed.end(), fill.begin(), fill.end());
    return allowed;
}

bool Fits(size_t p_Card, const PrestigeChoice* p_Choice, const PrestigeRejected& p_Rejected) {
    return PrestigeCardAccepts(p_Card, p_Choice->objective) && !Contains(p_Rejected[p_Card], p_Choice->objective);
}

bool Clash(const PrestigeChoice* a, const PrestigeChoice* b) {
    return a == b || (a->category != PrestigeCategory::None && a->category == b->category);
}

// Every way to put three candidates on the three cards (each card's picker
// accepting its own, no two of a category): the one with the fewest objectives
// from prestige_exclude, and among those the first in the shuffled order (card
// A first, then B, then C). With nothing to fill and no card rule in the way,
// that is the first candidate each card can take, as the old card-by-card draw.
std::optional<std::array<int32_t, kPrestigeCards>> Assign(const std::vector<Candidate>& p_List,
                                                          const PrestigeRejected& p_Rejected) {
    const size_t n = p_List.size();

    for (int target = 0; target <= static_cast<int>(kPrestigeCards); ++target) {
        for (size_t a = 0; a < n; ++a) {
            const Candidate& ca = p_List[a];

            if (ca.outside > target || !Fits(0, ca.choice, p_Rejected))
                continue;

            for (size_t b = 0; b < n; ++b) {
                const Candidate& cb = p_List[b];

                if (ca.outside + cb.outside > target || Clash(ca.choice, cb.choice) || !Fits(1, cb.choice, p_Rejected))
                    continue;

                for (size_t c = 0; c < n; ++c) {
                    const Candidate& cc = p_List[c];

                    if (ca.outside + cb.outside + cc.outside != target || Clash(ca.choice, cc.choice) ||
                        Clash(cb.choice, cc.choice) || !Fits(2, cc.choice, p_Rejected))
                        continue;

                    return std::array<int32_t, kPrestigeCards> {ca.choice->objective, cb.choice->objective,
                                                                cc.choice->objective};
                }
            }
        }
    }

    return std::nullopt;
}

// How many of the cards are outside the allowed list (in p_Excluded), when
// the cards could be shown as they are (cards in p_Excluded count as fills);
// empty when they could not.
std::optional<size_t> KeptOutside(const std::array<int32_t, kPrestigeCards>& p_Cards,
                                  std::span<const int32_t> p_Available, const PrestigeRejected& p_Rejected,
                                  const std::set<int32_t>& p_Excluded) {
    std::array<const PrestigeChoice*, kPrestigeCards> choices {};
    size_t outside = 0;

    for (size_t card = 0; card < kPrestigeCards; ++card) {
        const PrestigeChoice* choice = PrestigeChoiceByObjective(p_Cards[card]);

        if (!choice || choice->excluded || !Contains(p_Available, choice->objective) || !Fits(card, choice, p_Rejected))
            return std::nullopt;

        if (p_Excluded.contains(choice->objective))
            ++outside;

        for (size_t earlier = 0; earlier < card; ++earlier) {
            if (Clash(choices[earlier], choice))
                return std::nullopt;
        }

        choices[card] = choice;
    }

    return outside;
}

} // namespace

std::optional<std::array<int32_t, kPrestigeCards>> DrawPrestigeCards(int32_t seed, uint32_t counter,
                                                                     std::span<const int32_t> p_Available) {
    return DrawPrestigeCards(seed, counter, p_Available, PrestigeRejected {});
}

std::optional<std::array<int32_t, kPrestigeCards>> DrawPrestigeCards(int32_t seed, uint32_t counter,
                                                                     std::span<const int32_t> p_Available,
                                                                     const PrestigeRejected& p_Rejected,
                                                                     const std::set<int32_t>& p_Excluded) {
    return Assign(Candidates(seed, counter, p_Available, p_Excluded, false), p_Rejected);
}

std::optional<std::array<int32_t, kPrestigeCards>> AssignPrestigeCards(int32_t seed, uint32_t counter,
                                                                       std::span<const int32_t> p_Available,
                                                                       const PrestigeRejected& p_Rejected,
                                                                       const std::set<int32_t>& p_Excluded) {
    return Assign(Candidates(seed, counter, p_Available, p_Excluded, true), p_Rejected);
}

size_t PrestigeCardsOutside(const std::array<int32_t, kPrestigeCards>& p_Cards, const std::set<int32_t>& p_Excluded) {
    return static_cast<size_t>(std::count_if(p_Cards.begin(), p_Cards.end(),
                                             [&](int32_t objective) { return p_Excluded.contains(objective); }));
}

std::vector<int32_t> PrestigeObjectivesOffered(std::span<const std::vector<PrestigeSlot>> p_Pickers) {
    std::vector<int32_t> offered;

    if (p_Pickers.empty())
        return offered;

    for (const PrestigeChoice& choice : kPrestigeChoices) {
        const bool everyPicker =
            std::all_of(p_Pickers.begin(), p_Pickers.end(), [&](const std::vector<PrestigeSlot>& slots) {
                return std::any_of(slots.begin(), slots.end(), [&](const PrestigeSlot& slot) {
                    return slot.objective == choice.objective && slot.weight > 0.0f;
                });
            });

        if (everyPicker)
            offered.push_back(choice.objective);
    }

    return offered;
}

std::optional<std::vector<float>> ForcedPrestigeWeights(std::span<const PrestigeSlot> p_Slots, int32_t p_Drawn) {
    std::vector<float> weights;
    bool pickable = false;

    for (const PrestigeSlot& slot : p_Slots) {
        if (slot.objective == p_Drawn) {
            weights.push_back(slot.weight);
            pickable = pickable || slot.weight > 0.0f;
        }
        else {
            weights.push_back(0.0f);
        }
    }

    if (!pickable)
        return std::nullopt;

    return weights;
}

PrestigeGuard PrestigeGuardVerdict(std::span<const PrestigePickerProgress> p_Pickers, double p_Elapsed,
                                   double p_Timeout) {
    if (std::any_of(p_Pickers.begin(), p_Pickers.end(), [](const PrestigePickerProgress& p) { return p.pickFailed; }))
        return PrestigeGuard::Failed;

    if (std::all_of(p_Pickers.begin(), p_Pickers.end(), [](const PrestigePickerProgress& p) { return p.pickDone; }))
        return PrestigeGuard::Done;

    return p_Elapsed >= p_Timeout ? PrestigeGuard::Failed : PrestigeGuard::Wait;
}

PrestigePickState AdvancePrestigePick(const PrestigePickState& p_State, int32_t p_Seed) {
    if (p_State.seed != p_Seed)
        return PrestigePickState {p_Seed, 0, false, std::nullopt};

    return p_State;
}

PrestigePickState ConsumePrestigePick(const PrestigePickState& p_State) {
    if (p_State.seed == 0 || !p_State.taken)
        return p_State;

    return PrestigePickState {p_State.seed, p_State.counter + 1, false, std::nullopt};
}

PrestigePickState RekeyPrestigePick(const PrestigePickState& p_State, int32_t p_From, int32_t p_To) {
    if (p_From == 0 || p_To == 0 || p_State.seed != p_From)
        return p_State;

    PrestigePickState next = p_State;
    next.seed = p_To;
    return next;
}

std::optional<std::array<int32_t, kPrestigeCards>> ChoosePrestigeCards(const PrestigePickState& p_State,
                                                                       std::span<const int32_t> p_Available,
                                                                       const PrestigeRejected& p_Rejected,
                                                                       const std::set<int32_t>& p_Excluded) {
    const auto assigned = AssignPrestigeCards(p_State.seed, p_State.counter, p_Available, p_Rejected, p_Excluded);

    if (p_State.cards && assigned) {
        const auto kept = KeptOutside(*p_State.cards, p_Available, p_Rejected, p_Excluded);

        if (kept && *kept <= PrestigeCardsOutside(*assigned, p_Excluded))
            return p_State.cards;
    }

    return assigned;
}

bool PrestigeCardRejected(std::optional<int32_t> p_Forced, std::optional<int32_t> p_Shown) {
    return p_Forced && p_Shown && *p_Forced != *p_Shown;
}

PrestigeRedraw PrestigeRedrawPath(bool p_ResetForkFound, int p_Redraws, int p_MaxRedraws) {
    return p_ResetForkFound && p_Redraws < p_MaxRedraws ? PrestigeRedraw::ThroughReset : PrestigeRedraw::Keep;
}

std::optional<int32_t> PrestigeTakeCorrection(std::optional<int32_t> p_Shown, int32_t p_Emitted) {
    if (!p_Shown || *p_Shown == 0 || *p_Shown == p_Emitted)
        return std::nullopt;

    return p_Shown;
}

std::string FormatPrestigePickState(const PrestigePickState& p_State) {
    std::string text =
        std::to_string(p_State.seed) + " " + std::to_string(p_State.counter) + " " + (p_State.taken ? "1" : "0");

    if (p_State.cards) {
        for (const int32_t card : *p_State.cards)
            text += " " + std::to_string(card);
    }

    return text;
}

std::optional<PrestigePickState> ParsePrestigePickState(std::string_view p_Text) {
    std::vector<std::string_view> fields;
    size_t i = 0;

    while (i < p_Text.size()) {
        while (i < p_Text.size() && (p_Text[i] == ' ' || p_Text[i] == '\t'))
            ++i;

        const size_t start = i;

        while (i < p_Text.size() && p_Text[i] != ' ' && p_Text[i] != '\t')
            ++i;

        if (i > start)
            fields.push_back(p_Text.substr(start, i - start));
    }

    if (fields.size() != 3 && fields.size() != 3 + kPrestigeCards)
        return std::nullopt;

    PrestigePickState state;
    const auto parse = [](std::string_view text, auto& out) {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
        return error == std::errc() && end == text.data() + text.size();
    };

    if (!parse(fields[0], state.seed) || !parse(fields[1], state.counter))
        return std::nullopt;

    if (fields[2] == "1")
        state.taken = true;
    else if (fields[2] != "0")
        return std::nullopt;

    if (fields.size() > 3) {
        std::array<int32_t, kPrestigeCards> cards {};

        for (size_t i = 0; i < kPrestigeCards; ++i) {
            if (!parse(fields[3 + i], cards[i]) || !PrestigeChoiceByObjective(cards[i]))
                return std::nullopt;
        }

        state.cards = cards;
    }

    return state;
}

} // namespace fr
