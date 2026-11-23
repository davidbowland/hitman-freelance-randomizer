#include "doctest.h"

#include "Merces.h"
#include "PrestigeCards.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string_view>
#include <vector>

namespace {

// The exclusions these draws were written against (prestige_exclude's default
// before the Timed objectives joined it): 115 only.
const std::set<int32_t> kSuitOnly {115};

std::vector<int32_t> AllObjectives() {
    std::vector<int32_t> all;

    for (int32_t n = 101; n <= 136; ++n)
        all.push_back(n);

    return all;
}

void CheckRules(const std::array<int32_t, fr::kPrestigeCards>& cards) {
    std::set<int32_t> distinct(cards.begin(), cards.end());
    CHECK(distinct.size() == fr::kPrestigeCards);
    std::map<fr::PrestigeCategory, int> perCategory;

    for (const int32_t objective : cards) {
        const fr::PrestigeChoice* choice = fr::PrestigeChoiceByObjective(objective);
        REQUIRE(choice);
        CHECK(choice->excluded == nullptr);
        CHECK(objective != 115);
        CHECK(objective != 136);
        ++perCategory[choice->category];
    }

    CHECK(perCategory[fr::PrestigeCategory::SilentAssassin] <= 1);
    CHECK(perCategory[fr::PrestigeCategory::Timed] <= 1);
    CHECK(perCategory[fr::PrestigeCategory::Multikill] <= 1);

    for (size_t i = 0; i < cards.size(); ++i)
        CHECK(fr::PrestigeCardAccepts(i, cards[i]));
}

// CheckRules, but 115 may fill a card when prestige_exclude leaves too few.
void CheckRulesExcept115(const std::array<int32_t, fr::kPrestigeCards>& cards) {
    std::set<int32_t> distinct(cards.begin(), cards.end());
    CHECK(distinct.size() == fr::kPrestigeCards);
    std::map<fr::PrestigeCategory, int> perCategory;

    for (size_t i = 0; i < cards.size(); ++i) {
        const fr::PrestigeChoice* choice = fr::PrestigeChoiceByObjective(cards[i]);
        REQUIRE(choice);
        CHECK(choice->excluded == nullptr);
        CHECK(fr::PrestigeCardAccepts(i, cards[i]));

        if (choice->category != fr::PrestigeCategory::None)
            CHECK(++perCategory[choice->category] == 1);
    }
}

} // namespace

TEST_CASE("prestige cards: the table holds 101 to 136 once each, with distinct entry ids") {
    std::set<uint64_t> ids;

    for (size_t i = 0; i < fr::kPrestigeChoices.size(); ++i) {
        CHECK(fr::kPrestigeChoices[i].objective == static_cast<int32_t>(101 + i));
        ids.insert(fr::kPrestigeChoices[i].entryId);
    }

    CHECK(ids.size() == fr::kPrestigeChoices.size());
    // Spot checks against the game's picker template.
    CHECK(fr::PrestigeChoiceByObjective(115)->entryId == 0x6c9e057bf1722e55ull);
    CHECK(fr::PrestigeChoiceByObjective(124)->entryId == 0x51607fd0a9b00a19ull);
    CHECK(fr::PrestigeChoiceByObjective(136)->entryId == 0xf3917c0e5b1f94d8ull);
    CHECK(fr::PrestigeChoiceByEntry(0x9b97127ea6e1ae87ull)->objective == 101);
    CHECK(fr::PrestigeChoiceByEntry(0x1234) == nullptr);
    CHECK(fr::PrestigeChoiceByObjective(100) == nullptr);
    CHECK(fr::PrestigeChoiceText(124) == "124 Shoot Target into Water");
    CHECK(fr::PrestigeChoiceText(99) == "99 (unknown)");
}

TEST_CASE("prestige cards: exclusions and categories") {
    // The table's own exclusions; 115 and the Timed 117-120 are prestige_exclude's default instead.
    const std::set<int32_t> excluded {124, 126, 127, 128, 129, 130, 131, 132, 133, 135, 136};

    for (const fr::PrestigeChoice& choice : fr::kPrestigeChoices)
        CHECK_MESSAGE((choice.excluded != nullptr) == excluded.contains(choice.objective), choice.objective);

    CHECK(fr::DefaultPrestigeExclude() == std::set<int32_t> {115, 117, 118, 119, 120});
    CHECK_FALSE(fr::PrestigeChoiceDrawable(*fr::PrestigeChoiceByObjective(115)));
    CHECK(fr::PrestigeChoiceDrawable(*fr::PrestigeChoiceByObjective(115), {}));

    for (const int32_t n : {117, 118, 119, 120})
        CHECK(fr::PrestigeChoiceByObjective(n)->category == fr::PrestigeCategory::Timed);

    for (const int32_t n : {121, 122, 123})
        CHECK(fr::PrestigeChoiceByObjective(n)->category == fr::PrestigeCategory::Multikill);

    for (const int32_t n : {101, 115, 116})
        CHECK(fr::PrestigeChoiceByObjective(n)->category == fr::PrestigeCategory::SilentAssassin);

    CHECK(fr::PrestigeChoiceByObjective(124)->category == fr::PrestigeCategory::None);
    // 124 Shoot Target into Water is not in the game: never drawn, even if a picker offered it.
    CHECK(std::string_view(fr::PrestigeChoiceByObjective(124)->excluded) == fr::kExcludedNotInGame);
    CHECK_FALSE(fr::PrestigeChoiceDrawable(*fr::PrestigeChoiceByObjective(124), {}));
}

TEST_CASE("prestige cards: names agree with the safehouse helpers' objective numbers") {
    for (const fr::PrestigeChoice& choice : fr::kPrestigeChoices)
        CHECK(fr::PrestigeObjectiveName(choice.objective) != nullptr);
}

TEST_CASE("prestige cards: every draw follows the rules") {
    const std::vector<int32_t> all = AllObjectives();

    for (int32_t seed : {1, -7, 123456789, 2147483647}) {
        for (uint32_t counter = 0; counter < 300; ++counter) {
            const auto cards = fr::DrawPrestigeCards(seed, counter, all, {}, kSuitOnly);
            REQUIRE(cards);
            CheckRules(*cards);
        }
    }
}

TEST_CASE("prestige cards: draws are deterministic and change with the counter and the seed") {
    const std::vector<int32_t> all = AllObjectives();
    const auto a = fr::DrawPrestigeCards(42, 3, all, {}, kSuitOnly);
    const auto b = fr::DrawPrestigeCards(42, 3, all, {}, kSuitOnly);
    REQUIRE(a);
    CHECK(*a == *b);

    std::set<std::array<int32_t, 3>> byCounter;
    std::set<std::array<int32_t, 3>> bySeed;

    for (uint32_t counter = 0; counter < 20; ++counter)
        byCounter.insert(*fr::DrawPrestigeCards(42, counter, all, {}, kSuitOnly));

    for (int32_t seed = 1; seed <= 20; ++seed)
        bySeed.insert(*fr::DrawPrestigeCards(seed, 0, all, {}, kSuitOnly));

    CHECK(byCounter.size() >= 15);
    CHECK(bySeed.size() >= 15);
}

TEST_CASE("prestige cards: the available set's order does not matter, duplicates count once") {
    std::vector<int32_t> all = AllObjectives();
    std::vector<int32_t> reversed(all.rbegin(), all.rend());
    reversed.push_back(101);
    reversed.push_back(999);

    for (uint32_t counter = 0; counter < 50; ++counter)
        CHECK(*fr::DrawPrestigeCards(9, counter, all, {}, kSuitOnly) ==
              *fr::DrawPrestigeCards(9, counter, reversed, {}, kSuitOnly));
}

TEST_CASE("prestige cards: every allowed objective is drawn sometimes, on every card") {
    const std::vector<int32_t> all = AllObjectives();
    std::array<std::set<int32_t>, 3> seen;

    for (uint32_t counter = 0; counter < 3000; ++counter) {
        const auto cards = *fr::DrawPrestigeCards(77, counter, all, {}, kSuitOnly);

        for (size_t i = 0; i < 3; ++i)
            seen[i].insert(cards[i]);
    }

    // Card A only ever gets a Silent Assassin or Timed objective (its picker shows no other).
    for (const fr::PrestigeChoice& choice : fr::kPrestigeChoices) {
        for (size_t i = 0; i < 3; ++i)
            CHECK_MESSAGE(seen[i].contains(choice.objective) == (fr::PrestigeChoiceDrawable(choice, kSuitOnly) &&
                                                                 fr::PrestigeCardAccepts(i, choice.objective)),
                          choice.objective);
    }
}

TEST_CASE("prestige cards: only available objectives are drawn; too few gives none") {
    const std::vector<int32_t> some {102, 117, 118, 121, 124, 125, 136};

    for (uint32_t counter = 0; counter < 100; ++counter) {
        const auto cards = fr::DrawPrestigeCards(5, counter, some, {}, kSuitOnly);
        REQUIRE(cards);
        CheckRules(*cards);

        for (const int32_t objective : *cards)
            CHECK((objective == 102 || objective == 117 || objective == 118 || objective == 121 || objective == 125));
    }

    // Four timed objectives allow one card only.
    CHECK_FALSE(fr::DrawPrestigeCards(5, 0, std::vector<int32_t> {117, 118, 119, 120}, {}, kSuitOnly));
    // Excluded ones never fill a card.
    CHECK_FALSE(fr::DrawPrestigeCards(5, 0, std::vector<int32_t> {102, 115, 136, 128}, {}, kSuitOnly));
    CHECK_FALSE(fr::DrawPrestigeCards(5, 0, std::vector<int32_t> {}, {}, kSuitOnly));
    // 101 and 116 share the Silent Assassin category.
    CHECK_FALSE(fr::DrawPrestigeCards(5, 0, std::vector<int32_t> {101, 116, 117}, {}, kSuitOnly));
    CHECK(fr::DrawPrestigeCards(5, 0, std::vector<int32_t> {101, 117, 121}, {}, kSuitOnly));
}

TEST_CASE("prestige cards: pick state moves on only for a new campaign or a played card") {
    const fr::PrestigePickState none;
    CHECK(fr::AdvancePrestigePick(none, 10) == fr::PrestigePickState {10, 0, false, std::nullopt});

    const std::array<int32_t, 3> cards {118, 102, 104};
    const fr::PrestigePickState shown {10, 4, false, cards};
    CHECK(fr::AdvancePrestigePick(shown, 10) == shown);
    CHECK(fr::AdvancePrestigePick(shown, 11) == fr::PrestigePickState {11, 0, false, std::nullopt});

    // A taken card keeps the cards in the safehouse (a reload, a new session,
    // the laptop opened again): the laptop can move the cards on several times
    // a minute, and a take then a quit moves them on at the next start.
    const fr::PrestigePickState taken {10, 4, true, cards};
    CHECK(fr::AdvancePrestigePick(taken, 10) == taken);

    // A mission start plays it: the next pick draws new cards, once.
    const fr::PrestigePickState played = fr::ConsumePrestigePick(taken);
    CHECK(played == fr::PrestigePickState {10, 5, false, std::nullopt});
    CHECK(fr::ConsumePrestigePick(played) == played); // a restart of the same mission
    CHECK(fr::ConsumePrestigePick(shown) == shown);   // a mission without a card taken
    CHECK(fr::ConsumePrestigePick(none) == none);
}

TEST_CASE("prestige cards: the generated seed's pick state carries over to the campaign's own seed") {
    const std::array<int32_t, 3> cards {101, 121, 111};
    const fr::PrestigePickState drawn {-1676486928, 0, true, cards};

    const fr::PrestigePickState rekeyed = fr::RekeyPrestigePick(drawn, -1676486928, 1769965632);
    CHECK(rekeyed == fr::PrestigePickState {1769965632, 0, true, cards});
    // The next draw with the campaign's own seed is not a new campaign.
    CHECK(fr::AdvancePrestigePick(rekeyed, 1769965632) == rekeyed);

    // Another campaign's state, or a 0, stays.
    CHECK(fr::RekeyPrestigePick(drawn, 5, 1769965632) == drawn);
    CHECK(fr::RekeyPrestigePick(drawn, -1676486928, 0) == drawn);
    CHECK(fr::RekeyPrestigePick(fr::PrestigePickState {}, 0, 1769965632) == fr::PrestigePickState {});
}

TEST_CASE("prestige cards: a quit and reload shows the same cards until a taken card is played") {
    const std::vector<int32_t> all = AllObjectives();
    const fr::PrestigeRejected nothing {};
    fr::PrestigePickState state = fr::AdvancePrestigePick({}, 216926673);
    const auto first = fr::ChoosePrestigeCards(state, all, nothing, kSuitOnly);
    REQUIRE(first);
    state.cards = first;

    // Take card B, quit: the text in the ini is read back at the next start.
    state.taken = true;
    const auto reloaded = fr::ParsePrestigePickState(fr::FormatPrestigePickState(state));
    REQUIRE(reloaded);
    CHECK(*reloaded == state);
    const fr::PrestigePickState again = fr::AdvancePrestigePick(*reloaded, 216926673);
    CHECK(fr::ChoosePrestigeCards(again, all, nothing, kSuitOnly) == first);

    // The kept cards hold even when the draw for this counter would differ now
    // (fewer objectives offered), as long as they are still offered.
    std::vector<int32_t> fewer;

    for (const int32_t objective : all) {
        if (objective % 3 == 0 || std::find(first->begin(), first->end(), objective) != first->end())
            fewer.push_back(objective);
    }

    CHECK(fr::ChoosePrestigeCards(again, fewer, nothing, kSuitOnly) == first);

    // The mission starts: new cards next time.
    const fr::PrestigePickState next = fr::ConsumePrestigePick(again);
    CHECK(next.counter == again.counter + 1);
    CHECK(fr::ChoosePrestigeCards(next, all, nothing, kSuitOnly) ==
          fr::DrawPrestigeCards(216926673, next.counter, all, {}, kSuitOnly));
}

TEST_CASE("prestige cards: kept cards no longer offered or rejected are drawn again") {
    const std::vector<int32_t> all = AllObjectives();
    fr::PrestigePickState state {7, 2, false, std::array<int32_t, 3> {117, 109, 134}};
    fr::PrestigeRejected rejected {};
    CHECK(fr::ChoosePrestigeCards(state, all, rejected, kSuitOnly) == state.cards);

    // Card A's picker would not show 117 (its condition): card A changes.
    rejected[0].push_back(117);
    const auto redrawn = fr::ChoosePrestigeCards(state, all, rejected, kSuitOnly);
    REQUIRE(redrawn);
    CHECK((*redrawn)[0] != 117);
    CheckRules(*redrawn);

    // Not offered any more.
    std::vector<int32_t> without109;

    for (const int32_t objective : all) {
        if (objective != 109)
            without109.push_back(objective);
    }

    const auto withoutCards = fr::ChoosePrestigeCards(state, without109, {}, kSuitOnly);
    REQUIRE(withoutCards);
    CHECK(std::find(withoutCards->begin(), withoutCards->end(), 109) == withoutCards->end());

    // Invalid kept cards (a duplicate, an excluded one, two of a category) are never shown.
    for (const std::array<int32_t, 3> bad :
         {std::array<int32_t, 3> {102, 102, 117}, std::array<int32_t, 3> {115, 102, 117},
          std::array<int32_t, 3> {101, 116, 102}, std::array<int32_t, 3> {117, 118, 102}}) {
        state.cards = bad;
        const auto cards = fr::ChoosePrestigeCards(state, all, {}, kSuitOnly);
        REQUIRE(cards);
        CHECK(*cards != bad);
        CheckRules(*cards);
    }
}

TEST_CASE("prestige cards: a draw with rejected cards never gives a card what its picker rejected") {
    const std::vector<int32_t> all = AllObjectives();

    // Game data, Picker1: card A shows only Silent Assassin and Timed objectives.
    fr::PrestigeRejected rejected {};

    for (const fr::PrestigeChoice& choice : fr::kPrestigeChoices) {
        if (choice.category != fr::PrestigeCategory::SilentAssassin && choice.category != fr::PrestigeCategory::Timed)
            rejected[0].push_back(choice.objective);
    }

    for (uint32_t counter = 0; counter < 300; ++counter) {
        const auto cards = fr::DrawPrestigeCards(216926673, counter, all, rejected, kSuitOnly);
        REQUIRE(cards);
        CheckRules(*cards);
        const fr::PrestigeCategory category = fr::PrestigeChoiceByObjective((*cards)[0])->category;
        CHECK((category == fr::PrestigeCategory::SilentAssassin || category == fr::PrestigeCategory::Timed));
        CHECK(fr::DrawPrestigeCards(216926673, counter, all, rejected, kSuitOnly) == cards);
    }

    // Nothing rejected: the same draw as without the argument.
    for (uint32_t counter = 0; counter < 200; ++counter)
        CHECK(fr::DrawPrestigeCards(3, counter, all, {}) == fr::DrawPrestigeCards(3, counter, all));

    // Everything rejected for one card: nothing to draw.
    fr::PrestigeRejected everything {};
    everything[2] = all;
    CHECK_FALSE(fr::DrawPrestigeCards(3, 0, all, everything));
}

TEST_CASE("prestige cards: what a card shows is what a take assigns") {
    // A forced card the picker would not show (game data: forced 109, shown 132).
    CHECK(fr::PrestigeCardRejected(109, 132));
    CHECK_FALSE(fr::PrestigeCardRejected(109, 109));
    CHECK_FALSE(fr::PrestigeCardRejected(std::nullopt, 132));
    CHECK_FALSE(fr::PrestigeCardRejected(109, std::nullopt));

    // Game data: card B showed 121 (then 118) and its Poll emitted 102 (Melee
    // Kill - Epic) both times: the take sends what the card shows.
    CHECK(fr::PrestigeTakeCorrection(121, 102) == 121);
    CHECK(fr::PrestigeTakeCorrection(118, 102) == 118);
    CHECK_FALSE(fr::PrestigeTakeCorrection(109, 109));
    CHECK_FALSE(fr::PrestigeTakeCorrection(std::nullopt, 102)); // nothing seen on the card: left
    CHECK_FALSE(fr::PrestigeTakeCorrection(0, 102));

    // Invariant: after the correction the assigned objective equals the shown one whenever it is known.
    for (int32_t shown = 101; shown <= 136; ++shown) {
        for (int32_t emitted = 101; emitted <= 136; ++emitted)
            CHECK(fr::PrestigeTakeCorrection(shown, emitted).value_or(emitted) == shown);
    }
}

TEST_CASE("prestige cards: pick state text round-trips and rejects bad text") {
    for (const fr::PrestigePickState& state :
         {fr::PrestigePickState {}, fr::PrestigePickState {-123, 7, true, std::nullopt},
          fr::PrestigePickState {2147483647, 0, false, std::nullopt},
          fr::PrestigePickState {216926673, 9, true, std::array<int32_t, 3> {101, 118, 123}}}) {
        CHECK(fr::ParsePrestigePickState(fr::FormatPrestigePickState(state)) == state);
    }

    CHECK(fr::FormatPrestigePickState({-5, 2, true, std::nullopt}) == "-5 2 1");
    CHECK(fr::FormatPrestigePickState({-5, 2, true, std::array<int32_t, 3> {134, 109, 117}}) == "-5 2 1 134 109 117");
    CHECK(fr::ParsePrestigePickState("  12\t3 0 ") == fr::PrestigePickState {12, 3, false, std::nullopt});
    CHECK(fr::ParsePrestigePickState("12 3 1 101 118 123") ==
          fr::PrestigePickState {12, 3, true, std::array<int32_t, 3> {101, 118, 123}});
    CHECK_FALSE(fr::ParsePrestigePickState(""));
    CHECK_FALSE(fr::ParsePrestigePickState("12 3"));
    CHECK_FALSE(fr::ParsePrestigePickState("12 3 2"));
    CHECK_FALSE(fr::ParsePrestigePickState("12 -3 0"));
    CHECK_FALSE(fr::ParsePrestigePickState("x 3 0"));
    CHECK_FALSE(fr::ParsePrestigePickState("12 3 0 9"));
    CHECK_FALSE(fr::ParsePrestigePickState("12 3 0 101 118"));
    CHECK_FALSE(fr::ParsePrestigePickState("12 3 0 101 118 99"));
    CHECK_FALSE(fr::ParsePrestigePickState("12 3 0 101 118 x"));
}

TEST_CASE("PrestigeChoiceInChain: the first choice entry in the owner chain") {
    // The element itself is a choice entry.
    {
        const std::array<uint64_t, 2> chain {0x9b97127ea6e1ae87ull, 0xd7ebe074ac4b64a3ull};
        const auto found = fr::PrestigeChoiceInChain(chain);
        REQUIRE(found);
        CHECK(found->choice->objective == 101);
        CHECK(found->depth == 0);
    }
    // The element is the entry's exposed RandomSelectorEntityChoice: its owner is the entry.
    {
        const std::array<uint64_t, 3> chain {0x1234ull, 0x3b02668960a3a4f8ull, 0x45df1f848528bca7ull};
        const auto found = fr::PrestigeChoiceInChain(chain);
        REQUIRE(found);
        CHECK(found->choice->objective == 103);
        CHECK(found->depth == 1);
    }
    // Deeper still.
    {
        const std::array<uint64_t, 4> chain {0x1ull, 0x2ull, 0x51607fd0a9b00a19ull, 0x8b754413bc7f64e2ull};
        const auto found = fr::PrestigeChoiceInChain(chain);
        REQUIRE(found);
        CHECK(found->choice->objective == 124);
        CHECK(found->depth == 2);
    }
    // A 0 ends the chain: ids after it are not looked at.
    {
        const std::array<uint64_t, 3> chain {0x1ull, 0x0ull, 0x9b97127ea6e1ae87ull};
        CHECK_FALSE(fr::PrestigeChoiceInChain(chain));
    }
    CHECK_FALSE(fr::PrestigeChoiceInChain(std::span<const uint64_t>()));
    const std::array<uint64_t, 2> none {0x1ull, 0xd7ebe074ac4b64a3ull};
    CHECK_FALSE(fr::PrestigeChoiceInChain(none));
}

TEST_CASE("PickerOfSelector: the RandomSelector under one of the pickers") {
    const std::array<uintptr_t, fr::kPrestigeCards> pickers {0x1000, 0x2000, 0x3000};
    CHECK(fr::PickerOfSelector(fr::kPrestigeSelectorId, 0x1000, pickers) == std::optional<size_t>(0));
    CHECK(fr::PickerOfSelector(fr::kPrestigeSelectorId, 0x3000, pickers) == std::optional<size_t>(2));
    // Another id under a picker (a choice entry), or the selector id elsewhere.
    CHECK_FALSE(fr::PickerOfSelector(0x9b97127ea6e1ae87ull, 0x1000, pickers));
    CHECK_FALSE(fr::PickerOfSelector(fr::kPrestigeSelectorId, 0x4000, pickers));
    CHECK_FALSE(fr::PickerOfSelector(fr::kPrestigeSelectorId, 0, pickers));
    // A picker not found (null) never matches a null parent.
    const std::array<uintptr_t, fr::kPrestigeCards> missing {0, 0x2000, 0};
    CHECK_FALSE(fr::PickerOfSelector(fr::kPrestigeSelectorId, 0, missing));
    CHECK(fr::PickerOfSelector(fr::kPrestigeSelectorId, 0x2000, missing) == std::optional<size_t>(1));
}

namespace {

fr::PrestigeSlot Slot(int32_t objective, float weight) {
    return fr::PrestigeSlot {objective, weight};
}

} // namespace

TEST_CASE("PrestigeObjectivesOffered: offered with a weight above 0 by every picker, in table order") {
    std::vector<std::vector<fr::PrestigeSlot>> pickers(3);
    // 134 listed first, 102 later: the result is in table order.
    for (auto& slots : pickers) {
        slots.push_back(Slot(134, 1.0f));
        slots.push_back(Slot(102, 1.0f));
        slots.push_back(Slot(105, 1.0f));
        slots.push_back(fr::PrestigeSlot {std::nullopt, 1.0f}); // not named: never offered
    }

    CHECK(fr::PrestigeObjectivesOffered(pickers) == std::vector<int32_t> {102, 105, 134});

    // Weight 0 in one picker only (as the game zeroes an unavailable choice): not offered.
    pickers[0][1].weight = 0.0f;
    CHECK(fr::PrestigeObjectivesOffered(pickers) == std::vector<int32_t> {105, 134});

    // Missing from one picker: not offered.
    pickers[2].erase(pickers[2].begin());
    CHECK(fr::PrestigeObjectivesOffered(pickers) == std::vector<int32_t> {105});

    // A duplicate element with a weight above 0 is enough.
    pickers[0].push_back(Slot(102, 2.0f));
    pickers[2].push_back(Slot(134, 0.5f));
    CHECK(fr::PrestigeObjectivesOffered(pickers) == std::vector<int32_t> {102, 105, 134});

    CHECK(fr::PrestigeObjectivesOffered(std::span<const std::vector<fr::PrestigeSlot>>()).empty());
}

TEST_CASE("ForcedPrestigeWeights: only the drawn objective keeps a weight; the array is never needed") {
    const std::vector<fr::PrestigeSlot> slots {
        Slot(132, 1.0f), Slot(102, 1.0f), Slot(105, 3.0f), fr::PrestigeSlot {std::nullopt, 1.0f}, Slot(102, 0.0f),
    };

    const auto forced = fr::ForcedPrestigeWeights(slots, 102);
    REQUIRE(forced);
    // Same length and order as the elements; drawn elements keep theirs (0 stays 0).
    CHECK(*forced == std::vector<float> {0.0f, 1.0f, 0.0f, 0.0f, 0.0f});

    const auto smg = fr::ForcedPrestigeWeights(slots, 105);
    REQUIRE(smg);
    CHECK(*smg == std::vector<float> {0.0f, 0.0f, 3.0f, 0.0f, 0.0f});

    // Not offered, or only with weight 0: the selector could not pick it.
    CHECK_FALSE(fr::ForcedPrestigeWeights(slots, 134));
    const std::vector<fr::PrestigeSlot> zero {Slot(102, 0.0f), Slot(105, 1.0f)};
    CHECK_FALSE(fr::ForcedPrestigeWeights(zero, 102));
    CHECK_FALSE(fr::ForcedPrestigeWeights(std::span<const fr::PrestigeSlot>(), 102));
}

TEST_CASE("ForcedPrestigeWeights: every drawn card leaves exactly one objective pickable") {
    std::vector<std::vector<fr::PrestigeSlot>> pickers(3);

    for (auto& slots : pickers) {
        for (const fr::PrestigeChoice& choice : fr::kPrestigeChoices)
            slots.push_back(Slot(choice.objective, 1.0f));
    }

    const std::vector<int32_t> offered = fr::PrestigeObjectivesOffered(pickers);
    const auto cards = fr::DrawPrestigeCards(228851961, 0, offered);
    REQUIRE(cards);

    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        const auto forced = fr::ForcedPrestigeWeights(pickers[i], (*cards)[i]);
        REQUIRE(forced);
        std::set<int32_t> pickable;

        for (size_t k = 0; k < forced->size(); ++k) {
            if ((*forced)[k] > 0.0f)
                pickable.insert(*pickers[i][k].objective);
        }

        CHECK(pickable == std::set<int32_t> {(*cards)[i]});
    }
}

TEST_CASE("PrestigeGuardVerdict: done, wait, a failed pick, a timeout") {
    using G = fr::PrestigeGuard;
    std::array<fr::PrestigePickerProgress, fr::kPrestigeCards> p {};

    CHECK(fr::PrestigeGuardVerdict(p, 0.0, 1.0) == G::Wait);
    CHECK(fr::PrestigeGuardVerdict(p, 0.99, 1.0) == G::Wait);
    // Every element pointed at one choice: Picker1's selector signals OnPickFailed, nothing else comes.
    p[0].pickFailed = true;
    CHECK(fr::PrestigeGuardVerdict(p, 0.0, 1.0) == G::Failed);

    p = {};
    p[0].pickDone = true;
    CHECK(fr::PrestigeGuardVerdict(p, 0.5, 1.0) == G::Wait);
    // Picker 1 done but the chain stopped there: failed once the time is up.
    CHECK(fr::PrestigeGuardVerdict(p, 1.0, 1.0) == G::Failed);

    p[1].pickDone = true;
    p[2].pickDone = true;
    CHECK(fr::PrestigeGuardVerdict(p, 0.01, 1.0) == G::Done);
    CHECK(fr::PrestigeGuardVerdict(p, 5.0, 1.0) == G::Done);

    // A failure wins over done (the loop guard after OnPickDone).
    p[2].pickFailed = true;
    CHECK(fr::PrestigeGuardVerdict(p, 0.01, 1.0) == G::Failed);
}

TEST_CASE("prestige cards: Perfect Run is never drawn, even when every picker offers it") {
    // Game data: with 136 swapped into the pickers, its card shows no laptop
    // screen and taking it assigns nothing.
    const std::vector<int32_t> all = AllObjectives();
    const fr::PrestigeRejected nothing {};
    const fr::PrestigeChoice& perfectRun = *fr::PrestigeChoiceByObjective(136);
    CHECK(perfectRun.excluded != nullptr);
    CHECK_FALSE(fr::PrestigeChoiceDrawable(perfectRun));
    CHECK_FALSE(fr::PrestigeChoiceDrawable(perfectRun, {}));

    for (uint32_t counter = 0; counter < 3000; ++counter) {
        const auto cards = fr::DrawPrestigeCards(77, counter, all, nothing, {});
        REQUIRE(cards);
        CHECK(std::find(cards->begin(), cards->end(), 136) == cards->end());
    }

    // 136 cannot fill a card.
    CHECK_FALSE(fr::DrawPrestigeCards(5, 0, std::vector<int32_t> {101, 117, 136}, nothing, {}));

    // Kept cards holding 136: drawn again without it.
    const std::array<int32_t, 3> kept {136, 102, 117};
    const fr::PrestigePickState state {7, 2, false, kept};
    const auto redrawn = fr::ChoosePrestigeCards(state, all, nothing);
    REQUIRE(redrawn);
    CHECK(*redrawn != kept);
    CHECK(std::find(redrawn->begin(), redrawn->end(), 136) == redrawn->end());
    CHECK(redrawn == fr::DrawPrestigeCards(7, 2, all, nothing));
}

TEST_CASE("PrestigeRedrawPath: a new pick only through the game's reset") {
    using R = fr::PrestigeRedraw;
    // Game data: re-picks through SignalFork_Void_04 .In alone moved card B's
    // laptop screen to 103 while its text and its take stayed on 134.
    CHECK(fr::PrestigeRedrawPath(true, 0, 16) == R::ThroughReset);
    CHECK(fr::PrestigeRedrawPath(true, 15, 16) == R::ThroughReset);
    // The cap: the cards stay as shown (text, screen and take agree).
    CHECK(fr::PrestigeRedrawPath(true, 16, 16) == R::Keep);
    CHECK(fr::PrestigeRedrawPath(true, 17, 16) == R::Keep);
    // No reset fork: no re-pick at all (never SignalFork_Void_04 .In alone).
    CHECK(fr::PrestigeRedrawPath(false, 0, 16) == R::Keep);
}

TEST_CASE("prestige cards: prestige_exclude's ids are never drawn; an empty one lets 115 in") {
    const std::vector<int32_t> all = AllObjectives();
    const fr::PrestigeRejected nothing {};
    const std::set<int32_t> mine {102, 109, 117};
    std::set<int32_t> seen;

    for (uint32_t counter = 0; counter < 2000; ++counter) {
        const auto cards = fr::DrawPrestigeCards(77, counter, all, nothing, mine);
        REQUIRE(cards);

        for (const int32_t objective : *cards) {
            CHECK_FALSE(mine.contains(objective));
            seen.insert(objective);
        }

        // Kept cards holding an excluded one are drawn again.
        const fr::PrestigePickState kept {77, counter, false, std::array<int32_t, 3> {102, 101, 121}};
        const auto redrawn = fr::ChoosePrestigeCards(kept, all, nothing, mine);
        REQUIRE(redrawn);
        CHECK(std::find(redrawn->begin(), redrawn->end(), 102) == redrawn->end());
    }

    CHECK(seen.contains(115));
    CHECK(seen.contains(101));
    CHECK_FALSE(seen.contains(126)); // the table's exclusions stay

    // The default argument is prestige_exclude's default.
    for (uint32_t counter = 0; counter < 100; ++counter)
        CHECK(fr::DrawPrestigeCards(5, counter, all, nothing, fr::DefaultPrestigeExclude()) ==
              fr::DrawPrestigeCards(5, counter, all));
}

TEST_CASE("prestige cards: the never-pick options and the guard") {
    const auto options = fr::PrestigeExcludeOptions();
    const std::set<int32_t> listed(options.begin(), options.end());
    CHECK(listed.size() == options.size());
    CHECK(listed.contains(115));
    CHECK(listed.contains(134));

    // Never offered by a picker or excluded by the table (136 Perfect Run among them): not listed.
    for (const int32_t n : {113, 114, 124, 126, 127, 128, 129, 130, 131, 132, 133, 135, 136})
        CHECK_FALSE(listed.contains(n));

    CHECK(fr::PrestigeExcludeLeavesEnough(fr::DefaultPrestigeExclude()));
    CHECK(fr::PrestigeExcludeLeavesEnough({}));

    // Everything but one per category: three cards still fit.
    std::set<int32_t> allBut(listed);
    allBut.erase(101);
    allBut.erase(117);
    allBut.erase(121);
    CHECK(fr::PrestigeExcludeLeavesEnough(allBut));

    // Two Silent Assassins and a Timed one: two cards only.
    std::set<int32_t> sameCategory(listed);
    sameCategory.erase(101);
    sameCategory.erase(116);
    sameCategory.erase(117);
    CHECK_FALSE(fr::PrestigeExcludeLeavesEnough(sameCategory));

    // Three of no category.
    std::set<int32_t> plain(listed);
    plain.erase(102);
    plain.erase(103);
    plain.erase(134);
    // Card A takes only a Silent Assassin or Timed objective: three of no category are not enough.
    CHECK_FALSE(fr::PrestigeExcludeLeavesEnough(plain));
    plain.erase(119);
    CHECK(fr::PrestigeExcludeLeavesEnough(plain));
    CHECK_FALSE(fr::PrestigeExcludeLeavesEnough(listed));

    // A list with 101-112, 115-122 never picked (123, 125 and 134 left): no card A.
    std::set<int32_t> bat(listed);
    for (const int32_t n : {123, 125, 134})
        bat.erase(n);
    CHECK_FALSE(fr::PrestigeExcludeLeavesEnough(bat));
    bat.erase(118);
    CHECK(fr::PrestigeExcludeLeavesEnough(bat));
}

TEST_CASE("PrestigeCardAccepts: card A only Silent Assassin or Timed; B and C anything") {
    for (const int32_t n : {101, 115, 116, 117, 118, 119, 120})
        CHECK(fr::PrestigeCardAccepts(0, n));

    // Each one Picker1 shows another objective for.
    for (const int32_t n : {103, 104, 105, 106, 109, 112, 123, 125, 134})
        CHECK_FALSE(fr::PrestigeCardAccepts(0, n));

    for (const fr::PrestigeChoice& choice : fr::kPrestigeChoices) {
        CHECK(fr::PrestigeCardAccepts(1, choice.objective));
        CHECK(fr::PrestigeCardAccepts(2, choice.objective));
    }

    CHECK_FALSE(fr::PrestigeCardAccepts(3, 101));
    CHECK_FALSE(fr::PrestigeCardAccepts(0, 99));
}

TEST_CASE("prestige cards: every placement of the allowed objectives is tried") {
    // What the pickers offer in game: every never-pick option (not 113, 114, 124).
    const auto options = fr::PrestigeExcludeOptions();
    const std::vector<int32_t> all(options.begin(), options.end());
    const fr::PrestigeRejected nothing {};

    // Allowed: 125, 134 and 117. Only 117 fits card A, whatever the shuffle puts first.
    std::set<int32_t> excluded;
    for (const int32_t n : fr::PrestigeExcludeOptions())
        excluded.insert(n);
    for (const int32_t n : {117, 125, 134})
        excluded.erase(n);

    std::set<std::array<int32_t, 3>> seen;

    for (uint32_t counter = 0; counter < 200; ++counter) {
        const auto cards = fr::DrawPrestigeCards(-731135754, counter, all, nothing, excluded);
        REQUIRE(cards);
        CHECK((*cards)[0] == 117);
        CHECK(std::set<int32_t> {(*cards)[1], (*cards)[2]} == std::set<int32_t> {125, 134});
        seen.insert(*cards);
        // Assign finds the same: nothing to fill.
        CHECK(fr::AssignPrestigeCards(-731135754, counter, all, nothing, excluded) == cards);
    }

    // Both orders of B and C turn up.
    CHECK(seen.size() == 2);

    // A rejection on card C moves the objective to card B instead of giving up.
    fr::PrestigeRejected rejected {};
    rejected[2].push_back(125);

    for (uint32_t counter = 0; counter < 50; ++counter) {
        const auto cards = fr::DrawPrestigeCards(-731135754, counter, all, rejected, excluded);
        REQUIRE(cards);
        CHECK(*cards == std::array<int32_t, 3> {117, 125, 134});
    }
}

TEST_CASE("prestige cards: a short list fills only the card it cannot fill, from prestige_exclude") {
    // What the pickers offer in game: every never-pick option (not 113, 114, 124).
    const auto options = fr::PrestigeExcludeOptions();
    const std::vector<int32_t> all(options.begin(), options.end());
    const fr::PrestigeRejected nothing {};

    // Everything but 123, 125 and 134 never picked. Card A's picker takes none
    // of them, so the game's own cards come back (117, 134, 101).
    std::set<int32_t> excluded;
    for (int32_t n = 101; n <= 112; ++n)
        excluded.insert(n);
    for (int32_t n = 115; n <= 122; ++n)
        excluded.insert(n);

    std::set<int32_t> cardA;

    for (uint32_t counter = 0; counter < 300; ++counter) {
        CHECK_FALSE(fr::DrawPrestigeCards(-731135754, counter, all, nothing, excluded));
        const auto cards = fr::AssignPrestigeCards(-731135754, counter, all, nothing, excluded);
        REQUIRE(cards);
        CheckRulesExcept115(*cards);
        // One card from outside the list: card A, a Silent Assassin or Timed objective.
        CHECK(fr::PrestigeCardsOutside(*cards, excluded) == 1);
        CHECK(excluded.contains((*cards)[0]));
        CHECK(fr::PrestigeCardAccepts(0, (*cards)[0]));
        cardA.insert((*cards)[0]);
        // B and C are two of the player's: 125 and one of 123 and 134 (or 123 and 134).
        CHECK_FALSE(excluded.contains((*cards)[1]));
        CHECK_FALSE(excluded.contains((*cards)[2]));
        // Deterministic.
        CHECK(fr::AssignPrestigeCards(-731135754, counter, all, nothing, excluded) == cards);
    }

    // The fill varies with the counter.
    CHECK(cardA.size() >= 3);

    // The pick state the player had (123 125 134 kept): not shown as is, the
    // new cards keep two of the list.
    const fr::PrestigePickState state {-731135754, 2, false, std::array<int32_t, 3> {123, 125, 134}};
    const auto chosen = fr::ChoosePrestigeCards(state, all, nothing, excluded);
    REQUIRE(chosen);
    CHECK(fr::PrestigeCardsOutside(*chosen, excluded) == 1);
    CHECK(fr::PrestigeCardAccepts(0, (*chosen)[0]));

    // Cards already holding one fill are kept (the laptop opened again)...
    const fr::PrestigePickState again {-731135754, 2, false, *chosen};
    CHECK(fr::ChoosePrestigeCards(again, all, nothing, excluded) == chosen);
    // ...but not once the list allows a card A: then no fill is needed.
    std::set<int32_t> withTimed(excluded);
    withTimed.erase(118);
    const auto better = fr::ChoosePrestigeCards(again, all, nothing, withTimed);
    REQUIRE(better);
    CHECK(fr::PrestigeCardsOutside(*better, withTimed) == 0);
    CHECK((*better)[0] == 118);

    // Fewer than three on the list: two fills, still never a showdown objective or 136.
    std::set<int32_t> onlyBat;
    for (const int32_t n : fr::PrestigeExcludeOptions())
        if (n != 125)
            onlyBat.insert(n);

    for (uint32_t counter = 0; counter < 100; ++counter) {
        const auto cards = fr::AssignPrestigeCards(5, counter, all, nothing, onlyBat);
        REQUIRE(cards);
        CheckRulesExcept115(*cards);
        CHECK(fr::PrestigeCardsOutside(*cards, onlyBat) == 2);
        CHECK(std::find(cards->begin(), cards->end(), 125) != cards->end());
    }

    // Everything in the list rejected for card A this visit: card A is filled, the rest stay the player's.
    std::set<int32_t> withTwoTimed(excluded);
    withTwoTimed.erase(117);
    fr::PrestigeRejected rejected {};
    rejected[0].push_back(117);
    const auto filled = fr::AssignPrestigeCards(-731135754, 0, all, rejected, withTwoTimed);
    REQUIRE(filled);
    CHECK((*filled)[0] != 117);
    CHECK(fr::PrestigeCardsOutside(*filled, withTwoTimed) == 1);

    // Nothing available at all: nothing.
    CHECK_FALSE(fr::AssignPrestigeCards(5, 0, std::vector<int32_t> {}, nothing, excluded));
    // Only objectives the table excludes: nothing (never a showdown objective or 136).
    CHECK_FALSE(fr::AssignPrestigeCards(5, 0, std::vector<int32_t> {126, 127, 128, 136}, nothing, {}));
}
