#include "doctest.h"

#include "DragScroll.h"

TEST_CASE("drag_scroll_tap_never_scrolls") {
    fr::DragScroll s_Drag(10.0f);
    s_Drag.Press(100.0f, 50.0f);
    CHECK(s_Drag.Pressed());
    CHECK_FALSE(s_Drag.Move(108.0f, 500.0f).has_value());
    CHECK_FALSE(s_Drag.Move(90.0f, 500.0f).has_value());
    CHECK_FALSE(s_Drag.Dragging());
    CHECK_FALSE(s_Drag.Release());
    CHECK_FALSE(s_Drag.Pressed());
}

TEST_CASE("drag_scroll_follows_the_finger_from_the_threshold") {
    fr::DragScroll s_Drag(10.0f);
    s_Drag.Press(100.0f, 50.0f);

    // Up 15 px: 5 past the threshold, so the content moves up 5 (scrolls down).
    CHECK(s_Drag.Move(85.0f, 500.0f) == doctest::Approx(55.0f));
    CHECK(s_Drag.Dragging());
    CHECK(s_Drag.Move(60.0f, 500.0f) == doctest::Approx(80.0f));
    // Back down.
    CHECK(s_Drag.Move(100.0f, 500.0f) == doctest::Approx(40.0f));
    // No movement, no new scroll.
    CHECK_FALSE(s_Drag.Move(100.0f, 500.0f).has_value());
    CHECK(s_Drag.Release());
    CHECK_FALSE(s_Drag.Dragging());
}

TEST_CASE("drag_scroll_clamps_without_a_dead_zone") {
    fr::DragScroll s_Drag(10.0f);
    s_Drag.Press(100.0f, 20.0f);

    // Down far past the top: clamped at 0.
    CHECK(s_Drag.Move(200.0f, 500.0f) == doctest::Approx(0.0f));
    CHECK_FALSE(s_Drag.Move(300.0f, 500.0f).has_value());
    // The way back scrolls at once.
    CHECK(s_Drag.Move(290.0f, 500.0f) == doctest::Approx(10.0f));

    // And at the bottom.
    s_Drag.Press(100.0f, 480.0f);
    CHECK(s_Drag.Move(0.0f, 500.0f) == doctest::Approx(500.0f));
    CHECK(s_Drag.Move(5.0f, 500.0f) == doctest::Approx(495.0f));

    // Content that fits: nothing to scroll.
    s_Drag.Press(100.0f, 0.0f);
    CHECK_FALSE(s_Drag.Move(0.0f, 0.0f).has_value());
    CHECK_FALSE(s_Drag.Move(0.0f, -3.0f).has_value());
}

TEST_CASE("drag_scroll_blocked_press") {
    fr::DragScroll s_Drag(10.0f);
    s_Drag.Press(100.0f, 50.0f);
    s_Drag.Block();
    CHECK_FALSE(s_Drag.Move(0.0f, 500.0f).has_value());
    CHECK_FALSE(s_Drag.Dragging());
    CHECK(s_Drag.Pressed());
    CHECK_FALSE(s_Drag.Release());

    // Block without a press does nothing; the next press scrolls again.
    s_Drag.Block();
    CHECK_FALSE(s_Drag.Pressed());
    s_Drag.Press(100.0f, 50.0f);
    CHECK(s_Drag.Move(80.0f, 500.0f) == doctest::Approx(60.0f));
}
