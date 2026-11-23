#pragma once

// For the overlay's windows (header-only).

#include <algorithm>
#include <cmath>
#include <optional>

namespace fr {

// Touchscreen drag-to-scroll for one window: a press on the window that then
// moves more than the threshold vertically scrolls the content with the
// finger. A press that stays within the threshold is a tap and never
// scrolls. All values are in pixels; the scroll is the window's scroll
// offset (0 at the top, larger further down).
class DragScroll {
public:
    explicit constexpr DragScroll(float p_Threshold) : m_Threshold(p_Threshold) {}

    // A press on the window's body at height p_Y, with the window scrolled to p_Scroll.
    void Press(float p_Y, float p_Scroll) {
        m_State = State::Pending;
        m_PressY = p_Y;
        m_LastY = p_Y;
        m_Scroll = p_Scroll;
    }

    // The press landed on a widget that drags itself (a slider, a text field):
    // it does not scroll before it is released.
    void Block() {
        if (m_State != State::Idle)
            m_State = State::Blocked;
    }

    // Still pressed, now at height p_Y; p_MaxScroll is the window's largest
    // scroll. The scroll to set, or nothing (not dragging, or no change).
    std::optional<float> Move(float p_Y, float p_MaxScroll) {
        if (m_State == State::Pending) {
            const float s_Moved = p_Y - m_PressY;

            if (std::fabs(s_Moved) <= m_Threshold)
                return std::nullopt;

            // Measured from the threshold, so the content does not jump when the drag starts.
            m_State = State::Dragging;
            m_LastY = m_PressY + std::copysign(m_Threshold, s_Moved);
        }

        if (m_State != State::Dragging)
            return std::nullopt;

        // Incremental and clamped: after dragging past an end, the way back moves the content at once.
        const float s_Scroll = std::clamp(m_Scroll - (p_Y - m_LastY), 0.0f, std::max(p_MaxScroll, 0.0f));
        m_LastY = p_Y;

        if (s_Scroll == m_Scroll)
            return std::nullopt;

        m_Scroll = s_Scroll;
        return s_Scroll;
    }

    // The press ended. True if it was a drag (the widget it started on must not
    // count it as a click).
    bool Release() {
        const bool s_Dragged = m_State == State::Dragging;
        m_State = State::Idle;
        return s_Dragged;
    }

    bool Pressed() const { return m_State != State::Idle; }
    bool Dragging() const { return m_State == State::Dragging; }

private:
    enum class State { Idle, Pending, Dragging, Blocked };

    float m_Threshold;
    State m_State = State::Idle;
    float m_PressY = 0.0f;
    float m_LastY = 0.0f;
    float m_Scroll = 0.0f;
};

} // namespace fr
