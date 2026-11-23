#pragma once

// For the overlay's buttons (header-only).

namespace fr {

// Accepts a control's activation unless one was accepted less than the window
// earlier. On the Steam Deck one touchscreen tap reaches ImGui as two clicks a
// few milliseconds apart; without this a toggle button flips twice.
class Debounce {
public:
    explicit constexpr Debounce(double p_WindowSeconds) : m_Window(p_WindowSeconds) {}

    // p_Now: a monotonic time in seconds (ImGui::GetTime()).
    bool Accept(double p_Now) {
        if (m_HasAccepted && p_Now - m_LastAccepted < m_Window)
            return false;

        m_HasAccepted = true;
        m_LastAccepted = p_Now;
        return true;
    }

private:
    double m_Window;
    bool m_HasAccepted = false;
    double m_LastAccepted = 0.0;
};

// The window both mods use for a tap.
inline constexpr double kTapDebounceSeconds = 0.25;

// Ignores every control of a panel for a while after a drop-down list closes
// (a choice made in it, or a tap outside it): the list covered the rows
// beneath. A touchscreen's second click of the same tap, or a quick follow-up,
// would otherwise land on whatever the list was covering.
class TapGuard {
public:
    explicit constexpr TapGuard(double p_Seconds) : m_Seconds(p_Seconds) {}

    // p_Now: a monotonic time in seconds (ImGui::GetTime()). Arming again
    // extends the block to one window from p_Now.
    void Arm(double p_Now) {
        m_Armed = true;
        m_Until = p_Now + m_Seconds;
    }

    // A clock that went back (a new ImGui context) never blocks for longer
    // than one window.
    bool Blocked(double p_Now) const { return m_Armed && p_Now < m_Until && m_Until - p_Now <= m_Seconds; }

private:
    double m_Seconds;
    bool m_Armed = false;
    double m_Until = 0.0;
};

// How long the Settings tab ignores its controls after such a tap.
inline constexpr double kTapGuardSeconds = 0.5;

} // namespace fr
