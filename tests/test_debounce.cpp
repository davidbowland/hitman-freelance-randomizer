#include "doctest.h"

#include "Debounce.h"

TEST_CASE("debounce_accepts_one_tap_per_window") {
    fr::Debounce s_Tap(0.25);
    CHECK(s_Tap.Accept(0.0));
    CHECK_FALSE(s_Tap.Accept(0.01));
    CHECK_FALSE(s_Tap.Accept(0.24));
    CHECK(s_Tap.Accept(0.25));
}

TEST_CASE("tap_guard_never_blocks_before_it_is_armed") {
    const fr::TapGuard s_Guard(fr::kTapGuardSeconds);
    CHECK_FALSE(s_Guard.Blocked(0.0));
    CHECK_FALSE(s_Guard.Blocked(123.0));
}

TEST_CASE("tap_guard_blocks_for_its_window_after_arm") {
    fr::TapGuard s_Guard(0.5);
    s_Guard.Arm(10.0);
    // The same frame (a tap's press and release together) and a touchscreen's second click.
    CHECK(s_Guard.Blocked(10.0));
    CHECK(s_Guard.Blocked(10.01));
    CHECK(s_Guard.Blocked(10.49));
    CHECK_FALSE(s_Guard.Blocked(10.5));
    CHECK_FALSE(s_Guard.Blocked(11.0));
}

TEST_CASE("tap_guard_rearm_extends_the_block") {
    fr::TapGuard s_Guard(0.5);
    s_Guard.Arm(10.0);
    s_Guard.Arm(10.3);
    CHECK(s_Guard.Blocked(10.7));
    CHECK_FALSE(s_Guard.Blocked(10.8));
}

TEST_CASE("tap_guard_ignores_a_clock_that_went_back") {
    fr::TapGuard s_Guard(0.5);
    s_Guard.Arm(1000.0);
    // A new ImGui context restarts its clock at 0: no 1000 s block.
    CHECK_FALSE(s_Guard.Blocked(0.0));
    CHECK_FALSE(s_Guard.Blocked(5.0));
    s_Guard.Arm(5.0);
    CHECK(s_Guard.Blocked(5.2));
}
