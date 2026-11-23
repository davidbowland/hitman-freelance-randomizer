#include "doctest.h"

#include "LockpickFree.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

TEST_CASE("slow lockpick time multiple is clamped to 1..4") {
    CHECK(fr::ClampLockpickSlowPicks(2) == 2);
    CHECK(fr::ClampLockpickSlowPicks(0) == 1);
    CHECK(fr::ClampLockpickSlowPicks(-3) == 1);
    CHECK(fr::ClampLockpickSlowPicks(9) == 4);
}

TEST_CASE("hold is (N - 1) x the pick's span") {
    CHECK(fr::LockpickHoldSeconds(10.0, 14.0, 2) == doctest::Approx(4.0));
    CHECK(fr::LockpickHoldSeconds(10.0, 14.0, 3) == doctest::Approx(8.0));
    CHECK(fr::LockpickHoldSeconds(10.0, 14.0, 4) == doctest::Approx(12.0));
    CHECK(fr::LockpickHoldSeconds(10.0, 14.0, 9) == doctest::Approx(12.0)); // clamped to 4
    CHECK(fr::LockpickHoldSeconds(10.0, 14.0, 1) == 0.0);
    CHECK(fr::LockpickHoldSeconds(10.0, 14.0, 0) == 0.0); // clamped to 1
    CHECK(fr::LockpickHoldSeconds(10.0, 10.0, 2) == 0.0); // no span
    CHECK(fr::LockpickHoldSeconds(10.0, 9.0, 2) == 0.0);  // backwards
    CHECK(fr::LockpickHoldSeconds(0.0, 31.0, 2) == 0.0);  // too long to be a pick
    CHECK(fr::LockpickHoldSeconds(0.0, 0.04, 2) == 0.0);  // hold below the minimum
}

TEST_CASE("1: never held") {
    fr::LockpickTiming t(1);
    t.Started(3, 100.0, true);
    const auto c = t.Completed(3, 104.0);
    CHECK(c.free);
    CHECK_FALSE(c.hold);
    CHECK(t.Held() == 0);
}

TEST_CASE("only a free pick with a seen start is held, then released once when due") {
    fr::LockpickTiming t(2);

    SUBCASE("free pick held") {
        t.Started(3, 100.0, true);
        const auto c = t.Completed(3, 104.0);
        CHECK(c.seenStart);
        CHECK(c.free);
        CHECK(c.hold);
        CHECK(c.holdSeconds == doctest::Approx(4.0));
        CHECK(c.span == doctest::Approx(4.0));
        CHECK(t.Held() == 1);
        CHECK(t.Due(107.0).empty());
        const auto due = t.Due(108.0);
        REQUIRE(due.size() == 1);
        CHECK(due[0] == 3);
        CHECK(t.Due(200.0).empty());
        CHECK(t.Held() == 0);
    }

    SUBCASE("with a lockpick: passes") {
        t.Started(3, 100.0, false);
        const auto c = t.Completed(3, 104.0);
        CHECK(c.seenStart);
        CHECK_FALSE(c.free);
        CHECK_FALSE(c.hold);
    }

    SUBCASE("unknown lockpick state: passes") {
        t.Started(3, 100.0, std::nullopt);
        CHECK_FALSE(t.Completed(3, 104.0).hold);
    }

    SUBCASE("no start seen: passes") {
        const auto c = t.Completed(3, 104.0);
        CHECK_FALSE(c.seenStart);
        CHECK_FALSE(c.hold);
    }

    SUBCASE("other door's start does not count") {
        t.Started(4, 100.0, true);
        CHECK_FALSE(t.Completed(3, 104.0).hold);
    }

    SUBCASE("a released completion is not held again") {
        t.Started(3, 100.0, true);
        CHECK(t.Completed(3, 104.0).hold);
        // The re-signaled LockpickCompleted passing through the hook again.
        CHECK_FALSE(t.Completed(3, 106.0).hold);
    }

    SUBCASE("a second start of a held door passes while the first is held") {
        t.Started(3, 100.0, true);
        CHECK(t.Completed(3, 104.0).hold);
        t.Started(3, 104.5, true);
        CHECK_FALSE(t.Completed(3, 108.0).hold);
        CHECK(t.Held() == 1);
    }
}

TEST_CASE("a restart replaces the older start; old starts expire") {
    fr::LockpickTiming t(2);
    t.Started(1, 0.0, true);
    t.Started(1, 10.0, true);
    const auto c = t.Completed(1, 13.0);
    CHECK(c.span == doctest::Approx(3.0));
    CHECK(c.holdSeconds == doctest::Approx(3.0));

    t.Started(2, 0.0, true);
    CHECK(t.Expired(30.0).empty());
    const auto e = t.Expired(30.5);
    REQUIRE(e.size() == 1);
    CHECK(e[0] == 2);
    CHECK_FALSE(t.Completed(2, 31.0).seenStart);
}

TEST_CASE("clear forgets starts and holds") {
    fr::LockpickTiming t(2);
    t.Started(1, 0.0, true);
    t.Started(2, 0.0, true);
    CHECK(t.Completed(2, 4.0).hold);
    t.Clear();
    CHECK(t.Held() == 0);
    CHECK(t.Due(100.0).empty());
    CHECK_FALSE(t.Completed(1, 4.0).seenStart);
}

TEST_CASE("timed from the lockpick's choice: a garage door takes 2x from the choice") {
    fr::LockpickTiming t(2);
    t.Chosen(5, 111.66);                          // ItemUsed
    const auto from = t.Started(5, 112.86, true); // the animation's Started
    REQUIRE(from);
    CHECK(*from == doctest::Approx(111.66));
    const auto c = t.Completed(5, 114.61);
    CHECK(c.fromChoice);
    CHECK(c.span == doctest::Approx(2.95));
    CHECK(c.holdSeconds == doctest::Approx(2.95));
    // Released at 117.56 s: 5.90 s from the choice, 2 x the vanilla 2.95 s.
    CHECK(t.Due(117.55).empty());
    CHECK(t.Due(117.57).size() == 1);

    fr::LockpickTiming t3(3);
    t3.Chosen(5, 111.66);
    t3.Started(5, 112.86, true);
    CHECK(t3.Completed(5, 114.61).holdSeconds == doctest::Approx(5.90));
}

TEST_CASE("the choice: a repeated Started keeps it; too old, another door's, or none: the Started") {
    fr::LockpickTiming t(2);

    SUBCASE("a second Started of the same pick is still timed from the choice") {
        t.Chosen(1, 10.0);
        CHECK(t.Started(1, 11.2, true));
        CHECK(t.Started(1, 11.3, true));
        CHECK(t.Completed(1, 13.0).span == doctest::Approx(3.0));
    }

    SUBCASE("more than 3 s before: the Started") {
        t.Chosen(1, 10.0);
        CHECK_FALSE(t.Started(1, 13.5, true));
        const auto c = t.Completed(1, 15.5);
        CHECK_FALSE(c.fromChoice);
        CHECK(c.span == doctest::Approx(2.0));
    }

    SUBCASE("another door's choice does not count") {
        t.Chosen(2, 10.0);
        CHECK_FALSE(t.Started(1, 11.0, true));
    }

    SUBCASE("no choice: the Started") {
        CHECK_FALSE(t.Started(1, 11.0, true));
        CHECK(t.Completed(1, 13.0).holdSeconds == doctest::Approx(2.0));
    }

    SUBCASE("a choice is used once: the next pick without one is timed from its Started") {
        t.Chosen(1, 10.0);
        t.Started(1, 11.0, true);
        t.Completed(1, 13.0);
        t.Due(100.0);
        CHECK_FALSE(t.Started(1, 12.0, true));
    }

    SUBCASE("old choices are forgotten; clear forgets them") {
        t.Chosen(1, 0.0);
        t.Expired(31.0);
        t.Chosen(2, 40.0);
        t.Clear();
        CHECK_FALSE(t.Started(2, 41.0, true));
    }
}

TEST_CASE("hiding the Unlock prompt: due after the delay or at the subaction's Completed, never after release") {
    fr::LockpickTiming t(2);
    t.Started(3, 100.0, true);
    REQUIRE(t.Completed(3, 104.0).hold); // held until 108
    CHECK(t.IsHeld(3));

    SUBCASE("after the delay") {
        CHECK(t.HidesDue(104.4).empty());
        const auto due = t.HidesDue(104.5);
        REQUIRE(due.size() == 1);
        CHECK(due[0] == 3);
        CHECK(t.HidesDue(105.0).empty()); // given once
    }

    SUBCASE("the subaction's Completed makes it due at once") {
        t.SubactionDone(3, 104.2);
        CHECK(t.HidesDue(104.2).size() == 1);
    }

    SUBCASE("another door's Completed does not") {
        t.SubactionDone(4, 104.2);
        CHECK(t.HidesDue(104.2).empty());
    }

    SUBCASE("clear forgets hides") {
        t.Clear();
        CHECK_FALSE(t.IsHeld(3));
        CHECK(t.HidesDue(200.0).empty());
    }
}

TEST_CASE("hiding: a door released before its hide is due is never hidden; a pass hides nothing") {
    fr::LockpickTiming t(2);
    t.Started(3, 100.0, true);
    REQUIRE(t.Completed(3, 100.1).hold); // a 0.1 s hold, shorter than the delay
    CHECK(t.Due(100.3).size() == 1);
    CHECK_FALSE(t.IsHeld(3));
    CHECK(t.HidesDue(101.0).empty());

    t.Started(4, 100.0, false);
    CHECK_FALSE(t.Completed(4, 104.0).hold);
    CHECK(t.HidesDue(200.0).empty());
}

TEST_CASE("the carry check at completion decides, the start's only when it is unknown") {
    fr::LockpickTiming t(2);

    SUBCASE("lockpick at the start, none at completion: held") {
        t.Started(3, 100.0, false);
        const auto c = t.Completed(3, 104.0, true);
        CHECK(c.freeAtCompletion);
        CHECK(c.free);
        CHECK(c.hold);
    }

    SUBCASE("none at the start, a lockpick at completion: passes") {
        t.Started(3, 100.0, true);
        const auto c = t.Completed(3, 104.0, false);
        CHECK(c.freeAtCompletion);
        CHECK_FALSE(c.free);
        CHECK_FALSE(c.hold);
    }

    SUBCASE("unknown at completion: the start's check") {
        t.Started(3, 100.0, true);
        const auto c = t.Completed(3, 104.0, std::nullopt);
        CHECK_FALSE(c.freeAtCompletion);
        CHECK(c.hold);
    }

    SUBCASE("no start seen: passes whatever the check") {
        CHECK_FALSE(t.Completed(3, 104.0, true).hold);
    }
}

TEST_CASE("grant modes: parsed by name, each with its item and count") {
    CHECK(fr::ParseLockpickGrantMode("off") == fr::LockpickGrantMode::Off);
    CHECK(fr::ParseLockpickGrantMode(" Slow_Lockpick ") == fr::LockpickGrantMode::SlowLockpick);
    CHECK(fr::ParseLockpickGrantMode("NAILS") == fr::LockpickGrantMode::Nails);
    CHECK_FALSE(fr::ParseLockpickGrantMode("true").has_value());
    CHECK_FALSE(fr::ParseLockpickGrantMode("").has_value());

    for (const auto mode :
         {fr::LockpickGrantMode::Off, fr::LockpickGrantMode::SlowLockpick, fr::LockpickGrantMode::Nails})
        CHECK(fr::ParseLockpickGrantMode(fr::LockpickGrantModeName(mode)) == mode);

    CHECK(fr::LockpickGrantItemId(fr::LockpickGrantMode::Off).empty());
    CHECK(fr::LockpickGrantItemId(fr::LockpickGrantMode::SlowLockpick) == fr::kSlowLockpickId);
    CHECK(fr::LockpickGrantItemId(fr::LockpickGrantMode::Nails) == fr::kRustyNailId);

    CHECK(fr::LockpickGrantSlows(fr::LockpickGrantMode::SlowLockpick));
    CHECK_FALSE(fr::LockpickGrantSlows(fr::LockpickGrantMode::Nails));
    CHECK_FALSE(fr::LockpickGrantSlows(fr::LockpickGrantMode::Off));

    // Vanilla nails never come back to the safehouse: only the lockpick is kept out of the save.
    CHECK(fr::LockpickGrantGuardsSave(fr::LockpickGrantMode::SlowLockpick));
    CHECK_FALSE(fr::LockpickGrantGuardsSave(fr::LockpickGrantMode::Nails));
    CHECK_FALSE(fr::LockpickGrantGuardsSave(fr::LockpickGrantMode::Off));

    CHECK(fr::LockpickGrantCount(fr::LockpickGrantMode::Off, 5) == 0);
    CHECK(fr::LockpickGrantCount(fr::LockpickGrantMode::SlowLockpick, 5) == 1);
    CHECK(fr::LockpickGrantCount(fr::LockpickGrantMode::Nails, 5) == 5);
    CHECK(fr::LockpickGrantCount(fr::LockpickGrantMode::Nails, 0) == fr::kMinLockpickNails);
    CHECK(fr::LockpickGrantCount(fr::LockpickGrantMode::Nails, 99) == fr::kMaxLockpickNails);
}

TEST_CASE("lockpick-like names: lockpicks and rusty nails, not ice picks") {
    CHECK(fr::IsLockpickLikeName("(Tool) Lock pick S3", ""));
    CHECK(fr::IsLockpickLikeName("", "(Tool) Lockpick S3"));
    CHECK(fr::IsLockpickLikeName("", "Tool_Lockpick"));
    CHECK(fr::IsLockpickLikeName("ui_prop_tool_lock_pick_name", ""));
    CHECK(fr::IsLockpickLikeName("", "Tool_Rusty_Nail_OneTimeUse"));
    CHECK_FALSE(fr::IsLockpickLikeName("Rusty Old Nail", "")); // the display name: pro.repo names are read
    CHECK_FALSE(fr::IsLockpickLikeName("(Melee) Ice Pick", "Melee_IcePick"));
    CHECK_FALSE(fr::IsLockpickLikeName("Screwdriver", "Tool_Screwdriver"));
}

TEST_CASE("a free pick: a given lockpick and none of 47's own") {
    CHECK_FALSE(fr::IsFreePick(std::nullopt).has_value());
    CHECK(fr::IsFreePick(fr::LockpickCarry {0, 1}) == true);
    CHECK(fr::IsFreePick(fr::LockpickCarry {1, 1}) == false); // his own one is vanilla
    CHECK(fr::IsFreePick(fr::LockpickCarry {1, 0}) == false);
    CHECK(fr::IsFreePick(fr::LockpickCarry {0, 0}) == false); // a pick with neither: not ours
}

TEST_CASE("the saved inventory list takes no push or remove of the given id while given items exist") {
    CHECK(fr::GrantListBlocks(true, true, true));
    CHECK_FALSE(fr::GrantListBlocks(false, true, true));
    CHECK_FALSE(fr::GrantListBlocks(true, false, true));
    CHECK_FALSE(fr::GrantListBlocks(true, true, false));
}

namespace {

fr::Guid G(std::string_view text) {
    return *fr::ParseGuid(text);
}

const fr::Guid kSlow = G(fr::kSlowLockpickId);
const fr::Guid kMk3 = G("d3387f28-866d-4262-88cb-6e5b1076bac0");
const fr::Guid kCollectors = G("3c52b8c3-f4a5-4fcd-ae16-ff45b56d4351");
const fr::Guid kClassic = G("2eacd4f6-0018-41a5-800d-5fd85f9ecefe");
const fr::Guid kFish = G("4d0d6b2a-dd81-474c-a412-3bf19af8233d");

bool IsPick(const fr::Guid& id) {
    return id == kSlow || id == kMk3 || id == kCollectors || id == kClassic;
}

} // namespace

TEST_CASE("item event names") {
    CHECK(fr::ParseHeroItemEvent("ItemPickedUp") == fr::HeroItemEvent::PickedUp);
    CHECK(fr::ParseHeroItemEvent("ItemRemovedFromInventory") == fr::HeroItemEvent::Removed);
    CHECK(fr::ParseHeroItemEvent("ItemDropped") == fr::HeroItemEvent::Dropped);
    CHECK(fr::ParseHeroItemEvent("ItemThrown") == fr::HeroItemEvent::Thrown);
    CHECK_FALSE(fr::ParseHeroItemEvent("ItemStashed"));
    CHECK_FALSE(fr::ParseHeroItemEvent("CpdSet"));
}

TEST_CASE("ledger: the random loadout's Collector's Lockpick blocks the grant") {
    fr::HeroItemLedger ledger;
    ledger.Expect(kFish); // packed
    ledger.Event(fr::HeroItemEvent::PickedUp, kFish, 1.0);
    // The random loadout's CreateItem of the Collector's Lockpick: no ItemPickedUp ever came.
    ledger.Expect(kCollectors);
    const auto carry = ledger.Carry(IsPick);
    CHECK(carry.own == 1);
    CHECK(carry.given == 0);
    CHECK(fr::IsFreePick(carry) == false);
}

TEST_CASE("ledger: the given lockpick, its ItemPickedUp not counted twice; his own makes picks vanilla") {
    fr::HeroItemLedger ledger;
    CHECK(ledger.Carry(IsPick).own == 0);
    CHECK(fr::LockpickCarryVerdict(ledger.Carry(IsPick)) == std::string_view("no lockpick seen (vanilla)"));

    ledger.Expect(kSlow, true);
    CHECK(ledger.GivenPending(kSlow) == 1);
    ledger.Event(fr::HeroItemEvent::PickedUp, kSlow, 19.2); // the grant's ItemPickedUp
    CHECK(ledger.GivenPending(kSlow) == 0);
    CHECK(ledger.Given(kSlow) == 1);
    CHECK(ledger.Own(kSlow) == 0);
    CHECK(fr::IsFreePick(ledger.Carry(IsPick)) == true);
    CHECK(fr::LockpickCarryVerdict(ledger.Carry(IsPick)) == std::string_view("with the given lockpick only"));

    // He buys a Lockpick Mk III: vanilla from now on.
    ledger.Event(fr::HeroItemEvent::PickedUp, kMk3, 90.0);
    CHECK(ledger.Carry(IsPick).own == 1);
    CHECK(fr::IsFreePick(ledger.Carry(IsPick)) == false);
    CHECK(fr::LockpickCarryVerdict(ledger.Carry(IsPick)) == std::string_view("with his own lockpick (vanilla)"));

    // He throws it away (ItemRemovedFromInventory and ItemThrown together: one removal).
    ledger.Event(fr::HeroItemEvent::Removed, kMk3, 120.0);
    ledger.Event(fr::HeroItemEvent::Thrown, kMk3, 120.0);
    CHECK(ledger.Own(kMk3) == 0);
    CHECK(fr::IsFreePick(ledger.Carry(IsPick)) == true);

    // Non-lockpicks never count as lockpicks.
    ledger.Event(fr::HeroItemEvent::PickedUp, kFish, 130.0);
    CHECK(ledger.Carry(IsPick).own == 0);
}

TEST_CASE("ledger: a dropped given lockpick picked up again is given, not his own") {
    fr::HeroItemLedger ledger;
    ledger.Expect(kSlow, true);
    ledger.Event(fr::HeroItemEvent::PickedUp, kSlow, 19.0);
    ledger.Event(fr::HeroItemEvent::Dropped, kSlow, 40.0);
    CHECK(ledger.Given(kSlow) == 0);
    CHECK(fr::IsFreePick(ledger.Carry(IsPick)) == false); // no lockpick at all
    ledger.Event(fr::HeroItemEvent::PickedUp, kSlow, 50.0);
    CHECK(ledger.Given(kSlow) == 1);
    CHECK(ledger.Own(kSlow) == 0);
}

TEST_CASE("ledger: two removals of one id seconds apart are two; never below zero") {
    fr::HeroItemLedger ledger;
    ledger.Expect(kMk3);
    ledger.Expect(kMk3);
    ledger.Event(fr::HeroItemEvent::Removed, kMk3, 10.0);
    ledger.Event(fr::HeroItemEvent::Removed, kMk3, 10.5); // the same kind twice: two items
    CHECK(ledger.Own(kMk3) == 0);
    ledger.Event(fr::HeroItemEvent::Dropped, kMk3, 30.0);
    CHECK(ledger.Own(kMk3) == 0);
    // A removal long after another of the other kind is its own.
    ledger.Event(fr::HeroItemEvent::PickedUp, kMk3, 40.0);
    ledger.Event(fr::HeroItemEvent::PickedUp, kMk3, 41.0);
    ledger.Event(fr::HeroItemEvent::Removed, kMk3, 50.0);
    ledger.Event(fr::HeroItemEvent::Dropped, kMk3, 60.0);
    CHECK(ledger.Own(kMk3) == 0);
}

TEST_CASE("ledger: a packed lockpick's own ItemPickedUp is not a second lockpick; a retracted expect is gone") {
    fr::HeroItemLedger ledger;
    ledger.Expect(kMk3);
    ledger.Event(fr::HeroItemEvent::PickedUp, kMk3, 0.5);
    CHECK(ledger.Own(kMk3) == 1);

    ledger.Expect(kSlow, true);
    ledger.Retract(kSlow, true); // the CreateItem faulted
    CHECK(ledger.Given(kSlow) == 0);
    CHECK(ledger.GivenPending(kSlow) == 0);

    ledger.Clear();
    CHECK(ledger.Carry(IsPick).own == 0);
}

TEST_CASE("Unlock entry after Animating: either condition true is still shown; read false is hidden") {
    using S = fr::UnlockEntryState;
    CHECK(fr::UnlockEntryAfterAnimating(false, false) == S::Hidden);
    CHECK(fr::UnlockEntryAfterAnimating(false, std::nullopt) == S::Hidden);
    CHECK(fr::UnlockEntryAfterAnimating(std::nullopt, false) == S::Hidden);
    CHECK(fr::UnlockEntryAfterAnimating(true, false) == S::StillShown);
    CHECK(fr::UnlockEntryAfterAnimating(false, true) == S::StillShown);
    CHECK(fr::UnlockEntryAfterAnimating(std::nullopt, true) == S::StillShown);
    CHECK(fr::UnlockEntryAfterAnimating(std::nullopt, std::nullopt) == S::Unknown);
}

TEST_CASE("\"Can use lockpick\" fallback: only when Animating was not set or the entry still shows") {
    using S = fr::UnlockEntryState;
    CHECK_FALSE(fr::LockpickFlagFallback(true, S::Hidden));
    CHECK_FALSE(fr::LockpickFlagFallback(true, S::Unknown));
    CHECK(fr::LockpickFlagFallback(true, S::StillShown));
    CHECK(fr::LockpickFlagFallback(false, S::Hidden));
    CHECK(fr::LockpickFlagFallback(false, S::Unknown));
}

namespace {

bool Watched(const fr::AnimatingResets& r, size_t door) {
    const std::vector<size_t> doors = r.Doors();
    return std::find(doors.begin(), doors.end(), door) != doors.end();
}

} // namespace

TEST_CASE("Animating re-sets: only a watched door reading false; logs rate-limited with the count") {
    fr::AnimatingResets r;
    CHECK_FALSE(r.Read(3, false, 10.0).setTrue); // not watched
    r.Start(3);
    CHECK(Watched(r, 3));
    CHECK_FALSE(Watched(r, 4));

    CHECK_FALSE(r.Read(3, true, 10.0).setTrue);
    CHECK_FALSE(r.Read(3, std::nullopt, 10.0).setTrue); // unreadable: left alone

    auto s = r.Read(3, false, 10.0);
    CHECK(s.setTrue);
    CHECK(s.log); // the first at once
    CHECK(s.resets == 1);
    CHECK(s.unlogged == 1);

    s = r.Read(3, false, 10.5);
    CHECK(s.setTrue);
    CHECK_FALSE(s.log);
    CHECK(s.resets == 2);
    s = r.Read(3, false, 11.0);
    CHECK_FALSE(s.log);

    s = r.Read(3, false, 10.0 + fr::kAnimatingResetLogSeconds);
    CHECK(s.log);
    CHECK(s.resets == 4);
    CHECK(s.unlogged == 3); // 10.5, 11.0 and this one

    const auto stopped = r.Stop(3);
    REQUIRE(stopped);
    CHECK(*stopped == 4);
    CHECK_FALSE(Watched(r, 3));
    CHECK_FALSE(r.Stop(3)); // a second stop: no SetFalse due
}

TEST_CASE("Animating re-sets: a new hold of a door starts its count again; clear and doors") {
    fr::AnimatingResets r;
    r.Start(1);
    r.Start(2);
    r.Read(1, false, 5.0);
    r.Start(1);
    CHECK(r.Size() == 2);
    const auto s = r.Read(1, false, 5.1);
    CHECK(s.resets == 1);
    CHECK(s.log);
    CHECK(r.Doors() == std::vector<size_t> {2, 1});
    r.Clear();
    CHECK(r.Size() == 0);
    CHECK(r.Doors().empty());
}
