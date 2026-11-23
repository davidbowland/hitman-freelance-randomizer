#include "LockpickFree.h"

#include "Loadout.h"
#include "Scene.h" // ToLower

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace fr {

int32_t ClampLockpickSlowPicks(int32_t picks) {
    return std::clamp(picks, kMinLockpickSlowPicks, kMaxLockpickSlowPicks);
}

double LockpickHoldSeconds(double startedAt, double completedAt, int32_t picks) {
    const double span = completedAt - startedAt;

    if (!std::isfinite(span) || span <= 0.0 || span > kMaxLockpickSpanSeconds)
        return 0.0;

    const double hold = (ClampLockpickSlowPicks(picks) - 1) * span;
    return hold < kMinLockpickHoldSeconds ? 0.0 : hold;
}

void LockpickTiming::Chosen(size_t door, double now) {
    std::erase_if(m_Chosen, [&](const Choice& c) { return c.door == door; });
    m_Chosen.push_back({door, now});
}

std::optional<double> LockpickTiming::Started(size_t door, double now, std::optional<bool> free) {
    // The choice is kept until the completion, so a repeated Started of the
    // same pick is timed from it too.
    std::optional<double> choiceAt;
    const auto c = std::find_if(m_Chosen.begin(), m_Chosen.end(), [&](const Choice& x) { return x.door == door; });

    if (c != m_Chosen.end() && now - c->at >= 0.0 && now - c->at <= kLockpickChoiceToStartSeconds)
        choiceAt = c->at;

    // A new start replaces an older one of the same door (a canceled pick).
    std::erase_if(m_Started, [&](const Start& s) { return s.door == door; });
    m_Started.push_back({door, choiceAt.value_or(now), free, choiceAt.has_value()});
    return choiceAt;
}

namespace {

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

bool LockpickLike(std::string_view name) {
    const std::string lower = ToLower(name);

    for (const std::string_view word : {"lockpick", "lock pick", "lock_pick", "rusty nail", "rusty_nail"}) {
        if (lower.find(word) != std::string::npos)
            return true;
    }

    return false;
}

} // namespace

std::optional<LockpickGrantMode> ParseLockpickGrantMode(std::string_view text) {
    const std::string lower = ToLower(Trim(text));

    if (lower == "off")
        return LockpickGrantMode::Off;
    if (lower == "slow_lockpick")
        return LockpickGrantMode::SlowLockpick;
    if (lower == "nails")
        return LockpickGrantMode::Nails;

    return std::nullopt;
}

const char* LockpickGrantModeName(LockpickGrantMode mode) {
    switch (mode) {
        case LockpickGrantMode::Off: return "off";
        case LockpickGrantMode::SlowLockpick: return "slow_lockpick";
        case LockpickGrantMode::Nails: return "nails";
    }
    return "?";
}

std::string_view LockpickGrantItemId(LockpickGrantMode mode) {
    switch (mode) {
        case LockpickGrantMode::SlowLockpick: return kSlowLockpickId;
        case LockpickGrantMode::Nails: return kRustyNailId;
        default: return {};
    }
}

bool LockpickGrantSlows(LockpickGrantMode mode) {
    return mode == LockpickGrantMode::SlowLockpick;
}

bool LockpickGrantGuardsSave(LockpickGrantMode mode) {
    return mode == LockpickGrantMode::SlowLockpick;
}

int32_t ClampLockpickNails(int32_t nails) {
    return std::clamp(nails, kMinLockpickNails, kMaxLockpickNails);
}

int32_t LockpickGrantCount(LockpickGrantMode mode, int32_t nails) {
    switch (mode) {
        case LockpickGrantMode::SlowLockpick: return 1;
        case LockpickGrantMode::Nails: return ClampLockpickNails(nails);
        default: return 0;
    }
}

bool IsLockpickLikeName(std::string_view title, std::string_view commonName) {
    return LockpickLike(title) || LockpickLike(commonName);
}

std::optional<bool> IsFreePick(const std::optional<LockpickCarry>& carry) {
    if (!carry)
        return std::nullopt;

    return carry->own == 0 && carry->given > 0;
}

const char* LockpickCarryVerdict(const std::optional<LockpickCarry>& carry) {
    if (!carry)
        return "unknown";
    if (carry->own > 0)
        return "with his own lockpick (vanilla)";
    if (carry->given > 0)
        return "with the given lockpick only";
    return "no lockpick seen (vanilla)";
}

std::optional<HeroItemEvent> ParseHeroItemEvent(std::string_view name) {
    if (name == "ItemPickedUp")
        return HeroItemEvent::PickedUp;
    if (name == "ItemRemovedFromInventory")
        return HeroItemEvent::Removed;
    if (name == "ItemDropped")
        return HeroItemEvent::Dropped;
    if (name == "ItemThrown")
        return HeroItemEvent::Thrown;
    return std::nullopt;
}

void HeroItemLedger::Expect(const Guid& id, bool given) {
    Entry& e = m_Items[id];

    if (given) {
        ++e.given;
        ++e.givenPending;
    }
    else {
        ++e.own;
        ++e.ownPending;
    }
}

void HeroItemLedger::Retract(const Guid& id, bool given) {
    const auto it = m_Items.find(id);

    if (it == m_Items.end())
        return;

    Entry& e = it->second;
    size_t& count = given ? e.given : e.own;
    size_t& pending = given ? e.givenPending : e.ownPending;

    if (pending > 0 && count > 0) {
        --pending;
        --count;
    }
}

void HeroItemLedger::Event(HeroItemEvent event, const Guid& id, double at) {
    Entry& e = m_Items[id];

    if (event == HeroItemEvent::PickedUp) {
        if (e.givenPending > 0)
            --e.givenPending; // a given item arriving: counted at its Expect
        else if (e.ownPending > 0)
            --e.ownPending;
        else if (e.givenOut > 0) {
            --e.givenOut; // a given item he dropped, picked up again
            ++e.given;
        }
        else
            ++e.own;
        return;
    }

    // A removal of another kind just after one of this id is the same removal.
    if (e.lastRemoval && *e.lastRemoval != event && std::abs(at - e.lastRemovalAt) <= kHeroItemSameRemovalSeconds) {
        e.lastRemoval.reset();
        return;
    }

    e.lastRemoval = event;
    e.lastRemovalAt = at;

    if (e.given > 0) {
        --e.given;
        ++e.givenOut;
        e.givenPending = std::min(e.givenPending, e.given);
    }
    else if (e.own > 0) {
        --e.own;
        e.ownPending = std::min(e.ownPending, e.own);
    }
}

size_t HeroItemLedger::Own(const Guid& id) const {
    const auto it = m_Items.find(id);
    return it == m_Items.end() ? 0 : it->second.own;
}

size_t HeroItemLedger::Given(const Guid& id) const {
    const auto it = m_Items.find(id);
    return it == m_Items.end() ? 0 : it->second.given;
}

size_t HeroItemLedger::GivenPending(const Guid& id) const {
    const auto it = m_Items.find(id);
    return it == m_Items.end() ? 0 : it->second.givenPending;
}

LockpickCarry HeroItemLedger::Carry(const std::function<bool(const Guid&)>& isLockpick) const {
    LockpickCarry out;

    // Given items of other kinds (the item grants') are not lockpicks either.
    for (const auto& [id, e] : m_Items) {
        if ((e.own == 0 && e.given == 0) || !isLockpick || !isLockpick(id))
            continue;
        out.own += e.own;
        out.given += e.given;
    }

    return out;
}

bool GrantListBlocks(bool givenThisScene, bool pushOrRemove, bool sameId) {
    return givenThisScene && pushOrRemove && sameId;
}

LockpickTiming::Completion LockpickTiming::Completed(size_t door, double now, std::optional<bool> freeNow) {
    Completion out;
    const auto it = std::find_if(m_Started.begin(), m_Started.end(), [&](const Start& s) { return s.door == door; });

    if (it == m_Started.end())
        return out;

    out.seenStart = true;
    out.freeAtCompletion = freeNow.has_value();
    out.free = freeNow ? *freeNow : it->free.value_or(false);
    out.span = now - it->at;
    out.fromChoice = it->fromChoice;
    const double startedAt = it->at;
    m_Started.erase(it);
    std::erase_if(m_Chosen, [&](const Choice& c) { return c.door == door; });

    if (!out.free)
        return out;

    // A completion of a door already held is let through (not held twice).
    if (std::any_of(m_Held.begin(), m_Held.end(), [&](const Hold& h) { return h.door == door; }))
        return out;

    out.holdSeconds = LockpickHoldSeconds(startedAt, now, m_Picks);
    out.hold = out.holdSeconds > 0.0;

    if (out.hold) {
        m_Held.push_back({door, now + out.holdSeconds});
        m_Hides.push_back({door, now + kLockpickHideDelaySeconds});
    }

    return out;
}

std::vector<size_t> LockpickTiming::Due(double now) {
    std::vector<size_t> out;

    std::erase_if(m_Held, [&](const Hold& h) {
        if (h.releaseAt > now)
            return false;

        out.push_back(h.door);
        return true;
    });

    // A door released before its hide was due is never hidden.
    std::erase_if(m_Hides, [&](const Hide& x) { return !IsHeld(x.door); });
    return out;
}

std::vector<size_t> LockpickTiming::Expired(double now) {
    std::vector<size_t> out;

    std::erase_if(m_Started, [&](const Start& s) {
        if (now - s.at <= kLockpickStartedExpirySeconds)
            return false;

        out.push_back(s.door);
        return true;
    });

    std::erase_if(m_Chosen, [&](const Choice& c) { return now - c.at > kLockpickStartedExpirySeconds; });
    return out;
}

void LockpickTiming::SubactionDone(size_t door, double now) {
    for (Hide& h : m_Hides) {
        if (h.door == door)
            h.at = std::min(h.at, now);
    }
}

std::vector<size_t> LockpickTiming::HidesDue(double now) {
    std::vector<size_t> out;

    std::erase_if(m_Hides, [&](const Hide& h) {
        if (!IsHeld(h.door))
            return true;
        if (h.at > now)
            return false;

        out.push_back(h.door);
        return true;
    });

    return out;
}

bool LockpickTiming::IsHeld(size_t door) const {
    return std::any_of(m_Held.begin(), m_Held.end(), [&](const Hold& h) { return h.door == door; });
}

void LockpickTiming::Clear() {
    m_Chosen.clear();
    m_Started.clear();
    m_Held.clear();
    m_Hides.clear();
}

UnlockEntryState UnlockEntryAfterAnimating(std::optional<bool> groupVisible, std::optional<bool> conditionList) {
    if (groupVisible.value_or(false) || conditionList.value_or(false))
        return UnlockEntryState::StillShown;
    if (groupVisible || conditionList)
        return UnlockEntryState::Hidden;
    return UnlockEntryState::Unknown;
}

const char* UnlockEntryStateName(UnlockEntryState state) {
    switch (state) {
        case UnlockEntryState::Hidden: return "hidden";
        case UnlockEntryState::StillShown: return "STILL SHOWN";
        case UnlockEntryState::Unknown: return "unknown (conditions not read)";
    }
    return "?";
}

bool LockpickFlagFallback(bool animatingSet, UnlockEntryState entry) {
    return !animatingSet || entry == UnlockEntryState::StillShown;
}

void AnimatingResets::Start(size_t door) {
    std::erase_if(m_Doors, [&](const Entry& e) { return e.door == door; });
    m_Doors.push_back({door, 0, 0, std::nullopt});
}

AnimatingResets::Step AnimatingResets::Read(size_t door, std::optional<bool> value, double now) {
    Step out;
    const auto it = std::find_if(m_Doors.begin(), m_Doors.end(), [&](const Entry& e) { return e.door == door; });

    if (it == m_Doors.end() || value != std::optional<bool>(false))
        return out;

    out.setTrue = true;
    out.resets = ++it->resets;
    ++it->unlogged;

    if (!it->lastLogAt || now - *it->lastLogAt >= kAnimatingResetLogSeconds) {
        out.log = true;
        out.unlogged = it->unlogged;
        it->unlogged = 0;
        it->lastLogAt = now;
    }

    return out;
}

std::optional<size_t> AnimatingResets::Stop(size_t door) {
    const auto it = std::find_if(m_Doors.begin(), m_Doors.end(), [&](const Entry& e) { return e.door == door; });

    if (it == m_Doors.end())
        return std::nullopt;

    const size_t resets = it->resets;
    m_Doors.erase(it);
    return resets;
}

std::vector<size_t> AnimatingResets::Doors() const {
    std::vector<size_t> out;

    for (const Entry& e : m_Doors)
        out.push_back(e.door);

    return out;
}

} // namespace fr
