#pragma once

// The overlay's Settings tab: every row of fr::SettingsTable(), under section
// headings (fr::SettingSection, never folded) in folding groups (the only
// fold-outs), the map variant chances under a heading per map, with the ini's
// current value, a tooltip, a reset per section (two taps), and a mark on
// values changed since the mod last read the ini. Edits are written
// at once through the writer (the SDK's settings API, which saves the ini);
// the mod re-reads the ini at the next scene load (TakeChanged).

#include "Debounce.h"
#include "LandslideFull.h"
#include "SettingsTable.h"

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class SettingsMenu {
public:
    // Writes one setting's text to the ini (render thread; the writer takes
    // its own lock around the SDK call).
    using Writer = std::function<void(const fr::SettingInfo&, const std::string&)>;

    void SetWriter(Writer p_Writer) { m_Writer = std::move(p_Writer); }

    // Game thread, after the mod read the ini: the ini's text per key (a
    // missing key: its default). Clears the "changed" marks.
    void Loaded(std::map<std::string, std::string> p_Values);

    // The map variant options (VariantOption::name) that can't come up this
    // session, and why (fr::FullOptionAvailability, never Available).
    using UnavailableOptions = std::map<std::string, fr::FullAvailability>;

    // Game thread, at Init: their chances are grayed out, and variant_force
    // can't pick them.
    void SetUnavailable(UnavailableOptions p_Options);

    // Game thread: true once after any edit since the last call.
    bool TakeChanged() { return m_Changed.exchange(false); }

    // p_Live: the mod reads the ini again at the next scene load (false while
    // the mod is disabled: everything waits for a restart).
    void Draw(bool p_Live); // render thread

private:
    void DrawSection(fr::SettingSection p_Section, const std::map<std::string, std::string>& p_Values,
                     const std::set<std::string>& p_Pending, bool p_Live);
    // p_Heading: under its own folded header (else always open, as Mod enabled is).
    void DrawGroup(fr::SettingGroup p_Group, const std::map<std::string, std::string>& p_Values,
                   const std::set<std::string>& p_Pending, bool p_Live, bool p_Heading);
    // The map variant chances among p_Rows, under a heading per map.
    void DrawVariantPlaces(const std::vector<const fr::SettingInfo*>& p_Rows,
                           const std::map<std::string, std::string>& p_Values, const std::set<std::string>& p_Pending,
                           bool p_Live);
    // p_Indent: under its switch. p_Shown: the label to show, when not the table's (a variant chance
    // under its map's heading leaves the map's name out).
    void DrawRow(const fr::SettingInfo& p_Info, const std::string& p_Value, bool p_Pending, bool p_ParentOn,
                 bool p_Live, bool p_Indent, std::string_view p_Shown = {});
    // Opens or closes every group.
    void OpenAll(bool p_Open);
    // p_Fit: a variant chance also lowers the other chances of its map so they
    // add up to 100 % at most (each lowered one written too).
    void Commit(const fr::SettingInfo& p_Info, const std::string& p_Text, bool p_Fit = true);
    void FitVariantChances(const fr::SettingInfo& p_Info, const std::string& p_Text);
    // p_Choice: a choice shortened to fit its box, shown whole (empty: none).
    void Tooltip(const fr::SettingInfo& p_Info, bool p_Live, std::string_view p_Choice = {});
    // A List of ids with options (fr::SettingListOptions): one checkbox per id
    // under the row's header, while it is open. p_Blocked: drawn inside the guard's block.
    void DrawChecklist(const fr::SettingInfo& p_Info, const std::string& p_Value, bool p_Blocked);

    Writer m_Writer;
    std::atomic<bool> m_Changed {false};

    std::mutex m_Mutex;                          // guards the two below
    std::map<std::string, std::string> m_Values; // the ini's text per key, as last read or written
    std::set<std::string> m_Pending;             // keys written and not yet in effect
    UnavailableOptions m_Missing;                // SetUnavailable's options

    // Render thread only.
    UnavailableOptions m_MissingNow;              // m_Missing, copied each frame
    std::map<std::string, std::string> m_Editing; // a slider or text being edited (written when released)
    std::array<bool, static_cast<size_t>(fr::SettingGroup::Count)> m_Open {}; // a group unfolded (all start closed)
    int m_ConfirmReset = -1;                // the section whose reset asks "Yes, reset?" (-1: none)
    std::map<std::string, bool> m_ListOpen; // a checklist row unfolded (closed until tapped)
    // A Deck tap reaches ImGui as two clicks: one accepted per window.
    fr::Debounce m_Tap {fr::kTapDebounceSeconds};
    // Every control ignores input for a while after a drop-down list closes (a choice, or a tap
    // outside it): the list covered the rows below it. Checked per control as it is drawn, so one
    // armed earlier in the frame already blocks the rest of it.
    fr::TapGuard m_Guard {fr::kTapGuardSeconds};
    bool m_ComboOpen = false; // a drop-down was open in the last frame drawn

    // A tap on a control counts: not inside the guard's block, and the first of a Deck tap's clicks.
    bool Accept();
    // After a drop-down list closes: the controls ignore input for kTapGuardSeconds.
    void ArmGuard();
    bool Guarded() const;
};
