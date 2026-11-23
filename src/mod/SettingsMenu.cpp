#include "SettingsMenu.h"

#include "OverlayStyle.h"
#include "TextFit.h"
#include "TouchScroll.h"
#include "Variants.h"
#include "WrappedText.h"

#include <IconsMaterialDesign.h>
#include <imgui.h>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr const ImVec4& kPendingColor = kNotableColor;
// A widget takes this share of the row; its label follows it.
constexpr float kWidgetShare = 0.42f;

// Decimals a number's slider shows: enough for its step.
int Decimals(const fr::SettingInfo& p_Info) {
    if (p_Info.type == fr::SettingType::Int || p_Info.step >= 1.0)
        return 0;

    if (p_Info.step >= 0.1)
        return 1;

    return 2;
}

// A checklist's ids by name ("No Disguise, Doors Stay Locked"), or empty
// when the setting has no options.
std::string ChecklistNames(const fr::SettingInfo& p_Info, std::string_view p_Text) {
    const std::vector<fr::SettingListOption> s_Options = fr::SettingListOptions(p_Info);

    if (s_Options.empty())
        return {};

    const std::set<int32_t> s_Ids = fr::SettingListIds(p_Info, p_Text);
    std::string s_Out;

    for (const fr::SettingListOption& s_Option : s_Options) {
        if (s_Ids.contains(s_Option.id))
            s_Out += fmt::format("{}{}", s_Out.empty() ? "" : ", ", s_Option.name);
    }

    return s_Out.empty() ? std::string("(none)") : s_Out;
}

// A value as the help text shows it (a choice: as the combo names it; a
// checklist: the checked names).
std::string ValueLabel(const fr::SettingInfo& p_Info, std::string_view p_Text) {
    if (std::string s_Names = ChecklistNames(p_Info, p_Text); !s_Names.empty())
        return s_Names;

    if (const int s_Index = fr::SettingChoiceIndex(p_Info, p_Text); s_Index >= 0)
        return fr::SettingChoiceLabel(p_Info, fr::SettingChoices(p_Info)[s_Index]);

    if (p_Text.empty())
        return "(empty)";

    // As the checkbox reads.
    if (p_Info.type == fr::SettingType::Bool)
        return fr::SettingBoolOr(p_Info, p_Text) ? "on" : "off";

    if (!p_Info.unit.empty() && (p_Info.type == fr::SettingType::Number || p_Info.type == fr::SettingType::Int))
        return fmt::format("{} {}", p_Text, p_Info.unit);

    return std::string(p_Text);
}

// After a map variant's label when it never comes up in a showdown, with its legend at the top of the group
// and under its last map.
constexpr const char* kNotInShowdownsMark = " *";
constexpr std::string_view kNotInShowdownsLegend = "* Not in showdowns";
// A map variant that can't come up this session: the group's notes over the grayed-out rows, each row's
// tooltip line, and its mark in the row and in variant_force's list.
constexpr std::string_view kNotBuildableLegend =
    "Variants marked (unavailable) don't work with your version of the game yet. A mod update will bring them back.";
constexpr std::string_view kSwitchedOffLegend =
    "Variants marked (unavailable) are off because Full map variants is off.";
constexpr std::string_view kNotInstalledLegend =
    "Variants marked (unavailable) couldn't be installed; the log says why.";
constexpr std::string_view kNotBuildableTip = "Can't be built: the game files it needs have changed.";
constexpr std::string_view kSwitchedOffTip = "Off because Full map variants is off.";
constexpr std::string_view kNotInstalledTip =
    "Can't be installed: another mod or a file error is in the way. See the log.";
constexpr const char* kUnavailableMark = " (unavailable)";

// A legend and a tooltip line per reason.
std::string_view UnavailableLegend(fr::FullAvailability p_Why) {
    switch (p_Why) {
        case fr::FullAvailability::SwitchedOff: return kSwitchedOffLegend;
        case fr::FullAvailability::NotInstalled: return kNotInstalledLegend;
        default: return kNotBuildableLegend;
    }
}

std::string_view UnavailableTip(fr::FullAvailability p_Why) {
    switch (p_Why) {
        case fr::FullAvailability::SwitchedOff: return kSwitchedOffTip;
        case fr::FullAvailability::NotInstalled: return kNotInstalledTip;
        default: return kNotBuildableTip;
    }
}

// The map variant option a variant_chance_<option> setting sets, or null.
const fr::VariantOption* ChanceOption(const fr::SettingInfo& p_Info) {
    constexpr std::string_view s_Prefix = "variant_chance_";

    if (p_Info.key.substr(0, s_Prefix.size()) != s_Prefix)
        return nullptr;

    return fr::FindVariantOption(p_Info.key.substr(s_Prefix.size()));
}

// A chance setting's text as a whole percent.
int32_t ChancePercent(const fr::SettingInfo& p_Info, std::string_view p_Text) {
    return static_cast<int32_t>(std::lround(fr::SettingNumberOr(p_Info, p_Text)));
}

// A tap on the last item: pressed and released on it without dragging (a
// drag that starts on it scrolls the window, see TouchScroll). For items that
// are not buttons, such as a row's label.
bool Tapped() {
    const ImGuiIO& s_Io = ImGui::GetIO();
    const ImVec2 s_Min = ImGui::GetItemRectMin();
    const ImVec2 s_Max = ImGui::GetItemRectMax();
    const ImVec2 s_Press = s_Io.MouseClickedPos[ImGuiMouseButton_Left];
    const float s_Slop = s_Io.MouseDragThreshold;

    return ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
           s_Io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] <= s_Slop * s_Slop && s_Press.x >= s_Min.x &&
           s_Press.y >= s_Min.y && s_Press.x <= s_Max.x && s_Press.y <= s_Max.y;
}

// What p_Option's map's chances add up to, in percent (p_ValueOf: a setting's text; p_Missing: options that
// can't come up, never picked); 100 less this is the normal map's share. Over 100: the roll scales them down.
int32_t PlaceChanceSum(const fr::VariantOption& p_Option,
                       const std::function<std::string(const fr::SettingInfo&)>& p_ValueOf,
                       const SettingsMenu::UnavailableOptions& p_Missing) {
    int32_t s_Sum = 0;

    for (const fr::VariantOption* s_Sibling : fr::VariantSiblings(p_Option)) {
        if (p_Missing.contains(std::string(s_Sibling->name)))
            continue;

        if (const fr::SettingInfo* s_Chance = fr::FindSetting(fr::VariantChanceKey(*s_Sibling)))
            s_Sum += std::clamp(ChancePercent(*s_Chance, p_ValueOf(*s_Chance)), int32_t {0}, int32_t {100});
    }

    return s_Sum;
}

// A variant chance's label under its map's heading: "Sapienza: The Icon" is "The Icon", "Colorado:
// daylight" is "Daylight".
std::string PlaceRowLabel(std::string_view p_Label, std::string_view p_Place) {
    if (p_Place.empty() || p_Label.size() <= p_Place.size() + 2 || p_Label.substr(0, p_Place.size()) != p_Place ||
        p_Label.substr(p_Place.size(), 2) != ": ")
        return std::string(p_Label);

    std::string s_Out(p_Label.substr(p_Place.size() + 2));

    if (s_Out[0] >= 'a' && s_Out[0] <= 'z')
        s_Out[0] = static_cast<char>(s_Out[0] - 'a' + 'A');

    return s_Out;
}

// The rows of a group, in table order.
std::vector<const fr::SettingInfo*> GroupRows(fr::SettingGroup p_Group) {
    std::vector<const fr::SettingInfo*> s_Rows;

    for (const fr::SettingInfo& s_Info : fr::SettingsTable()) {
        if (s_Info.group == p_Group)
            s_Rows.push_back(&s_Info);
    }

    return s_Rows;
}

std::function<std::string(const fr::SettingInfo&)> ValueOf(const std::map<std::string, std::string>& p_Values) {
    return [&p_Values](const fr::SettingInfo& p_Info) {
        const auto s_It = p_Values.find(std::string(p_Info.key));
        return s_It != p_Values.end() ? s_It->second : std::string(p_Info.def);
    };
}

// While p_Blocked, the items drawn in its lifetime take no input, so an item under the finger
// cannot activate from the same tap. BeginDisabled's flag without its dimming.
class InputBlock {
public:
    explicit InputBlock(bool p_Blocked) : m_Blocked(p_Blocked) {
        if (m_Blocked)
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    }

    ~InputBlock() {
        if (m_Blocked)
            ImGui::PopItemFlag();
    }

    InputBlock(const InputBlock&) = delete;
    InputBlock& operator=(const InputBlock&) = delete;

private:
    bool m_Blocked;
};

} // namespace

bool SettingsMenu::Accept() {
    const double s_Now = ImGui::GetTime();
    return !m_Guard.Blocked(s_Now) && m_Tap.Accept(s_Now);
}

void SettingsMenu::ArmGuard() {
    m_Guard.Arm(ImGui::GetTime());
}

bool SettingsMenu::Guarded() const {
    return m_Guard.Blocked(ImGui::GetTime());
}

void SettingsMenu::SetUnavailable(UnavailableOptions p_Options) {
    std::lock_guard s_Lock(m_Mutex);
    m_Missing = std::move(p_Options);
}

void SettingsMenu::Loaded(std::map<std::string, std::string> p_Values) {
    std::lock_guard s_Lock(m_Mutex);
    m_Values = std::move(p_Values);

    // A setting that needs a game restart stays marked until then.
    std::erase_if(m_Pending, [](const std::string& p_Key) {
        const fr::SettingInfo* s_Info = fr::FindSetting(p_Key);
        return !s_Info || s_Info->applies == fr::SettingApplies::NextLoad;
    });
}

void SettingsMenu::Commit(const fr::SettingInfo& p_Info, const std::string& p_Text, bool p_Fit) {
    if (m_Writer)
        m_Writer(p_Info, p_Text);

    {
        std::lock_guard s_Lock(m_Mutex);
        m_Values[std::string(p_Info.key)] = p_Text;
        m_Pending.insert(std::string(p_Info.key));
    }

    m_Changed.store(true);

    if (p_Fit)
        FitVariantChances(p_Info, p_Text);
}

void SettingsMenu::FitVariantChances(const fr::SettingInfo& p_Info, const std::string& p_Text) {
    const fr::VariantOption* s_Edited = ChanceOption(p_Info);

    if (!s_Edited)
        return;

    // The chances as the ini has them (0 for an option that can't come up); the edit then
    // lowers the other options of its map (each written like any edit) so the map never adds
    // up to more than 100 %. An option that can't come up is never written.
    const auto s_Missing = [this](const fr::VariantOption& p_Option) {
        return m_MissingNow.contains(std::string(p_Option.name));
    };
    fr::VariantChances s_Before {};
    {
        std::lock_guard s_Lock(m_Mutex);

        for (const fr::VariantOption* s_Option : fr::AllVariantOptions()) {
            if (s_Missing(*s_Option))
                continue;

            if (const fr::SettingInfo* s_Info = fr::FindSetting(fr::VariantChanceKey(*s_Option))) {
                const auto s_It = m_Values.find(std::string(s_Info->key));
                fr::SetChance(s_Before, *s_Option,
                              ChancePercent(*s_Info, s_It != m_Values.end() ? s_It->second : s_Info->def));
            }
        }
    }

    const fr::VariantChances s_After = fr::FitVariantChances(s_Before, *s_Edited, ChancePercent(p_Info, p_Text));

    for (const fr::VariantOption* s_Option : fr::VariantSiblings(*s_Edited)) {
        if (s_Option == s_Edited || s_Missing(*s_Option) ||
            fr::ChanceOf(s_After, *s_Option) == fr::ChanceOf(s_Before, *s_Option))
            continue;

        if (const fr::SettingInfo* s_Info = fr::FindSetting(fr::VariantChanceKey(*s_Option)))
            Commit(*s_Info, fr::NormalizeSettingNumber(*s_Info, fr::ChanceOf(s_After, *s_Option)), false);
    }
}

void SettingsMenu::Draw(bool p_Live) {
    std::map<std::string, std::string> s_Values;
    std::set<std::string> s_Pending;
    {
        std::lock_guard s_Lock(m_Mutex);
        s_Values = m_Values;
        s_Pending = m_Pending;
        m_MissingNow = m_Missing;
    }

    // A drop-down open in the last frame and closed now (by a choice, or by a tap outside it, which
    // ImGui handles at the end of that frame): guard the rows before any is drawn, so the rest of
    // that tap cannot reach the row the list covered.
    if (m_ComboOpen && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
        ArmGuard();

    m_ComboOpen = false;

    if (s_Values.empty()) {
        WrappedTextAvail("Settings are still loading.");
        return;
    }

    WrappedTextAvail(p_Live ? "Changes save at once and take effect at the next mission start, restart or "
                              "safehouse visit. Tap a setting's name or " ICON_MD_HELP_OUTLINE " for what it does."
                            : "The mod is off: changes save now and take effect after you restart the game.");

    // Changes waiting for a game restart, then those waiting for the next load (only while the mod is live).
    size_t s_ForRestart = 0;

    for (const std::string& s_Key : s_Pending) {
        const fr::SettingInfo* s_Info = fr::FindSetting(s_Key);

        if (!p_Live || (s_Info && s_Info->applies == fr::SettingApplies::Restart))
            ++s_ForRestart;
    }

    const auto s_Waiting = [](size_t p_Count, const char* p_For) {
        WrappedTextAvail(fmt::format(ICON_MD_SCHEDULE " {} change{} wait{} for {}.", p_Count, p_Count == 1 ? "" : "s",
                                     p_Count == 1 ? "s" : "", p_For),
                         &kPendingColor);
    };

    if (s_Pending.size() > s_ForRestart)
        s_Waiting(s_Pending.size() - s_ForRestart, "the next mission or safehouse visit");

    if (s_ForRestart > 0)
        s_Waiting(s_ForRestart, "a game restart");

    // Mod enabled: at the top, never folded.
    for (const fr::SettingGroup s_Group : fr::SettingSectionGroups(fr::SettingSection::General))
        DrawGroup(s_Group, s_Values, s_Pending, p_Live, false);

    // Every group starts closed, so each section is a short index of group headers.
    ImGui::Spacing();

    {
        const InputBlock s_Block(Guarded());

        if (ImGui::Button(ICON_MD_UNFOLD_MORE " Expand all") && Accept()) {
            OpenAll(true);
        }

        ImGui::SameLine();

        if (ImGui::Button(ICON_MD_UNFOLD_LESS " Collapse all") && Accept()) {
            OpenAll(false);
        }
    }

    ImGui::Spacing();

    // In the window itself, not a child region: the window scrolls it (a drag scrolls only the
    // window it starts on, see TouchScroll).
    for (size_t i = 0; i < static_cast<size_t>(fr::SettingSection::Count); ++i) {
        if (static_cast<fr::SettingSection>(i) != fr::SettingSection::General)
            DrawSection(static_cast<fr::SettingSection>(i), s_Values, s_Pending, p_Live);
    }
}

void SettingsMenu::OpenAll(bool p_Open) {
    m_Open.fill(p_Open);
}

void SettingsMenu::DrawSection(fr::SettingSection p_Section, const std::map<std::string, std::string>& p_Values,
                               const std::set<std::string>& p_Pending, bool p_Live) {
    const auto s_ValueOf = ValueOf(p_Values);
    const std::vector<fr::SettingGroup> s_Groups = fr::SettingSectionGroups(p_Section);
    // The section's rows, and whether any differs from its default.
    std::vector<const fr::SettingInfo*> s_Rows;
    bool s_AnyChanged = false;

    for (const fr::SettingGroup s_Group : s_Groups) {
        for (const fr::SettingInfo* s_Info : GroupRows(s_Group)) {
            s_Rows.push_back(s_Info);
            s_AnyChanged = s_AnyChanged || !fr::IsDefaultSetting(*s_Info, s_ValueOf(*s_Info));
        }
    }

    if (s_Rows.empty())
        return;

    // Never folded: a heading with a divider, with room above it (the groups below fold).
    const int s_Index = static_cast<int>(p_Section);
    const std::string s_Name(fr::SettingSectionName(p_Section));
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::SeparatorText(s_Name.c_str());

    ImGui::PushID(fmt::format("settings-section-{}", s_Index).c_str());

    if (const std::string_view s_Intro = fr::SettingSectionIntro(p_Section); !s_Intro.empty())
        WrappedTextAvail(s_Intro);

    // The only reset: the whole section, under its heading, while any of its settings differs from its
    // default. Two taps: the first asks, Yes resets (a full-size button each, for the touchscreen).
    if (!s_AnyChanged && m_ConfirmReset == s_Index)
        m_ConfirmReset = -1;

    if (s_AnyChanged) {
        // The reset's buttons replace each other under the finger.
        const InputBlock s_Block(Guarded());

        if (m_ConfirmReset != s_Index) {
            if (ImGui::Button(fmt::format(ICON_MD_RESTORE " Reset {} to defaults", s_Name).c_str()) && Accept()) {
                m_ConfirmReset = s_Index;
            }
        }
        else {
            WrappedTextAvail(fmt::format("Reset every {} setting to its default?", s_Name), &kPendingColor);

            if (ImGui::Button(ICON_MD_RESTORE " Yes, reset") && Accept()) {
                for (const fr::SettingInfo* s_Info : s_Rows) {
                    // Every default, as is: the default chances fit their maps.
                    if (!fr::IsDefaultSetting(*s_Info, s_ValueOf(*s_Info)))
                        Commit(*s_Info, std::string(s_Info->def), false);
                }

                m_ConfirmReset = -1;
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel") && Accept()) {
                m_ConfirmReset = -1;
            }
        }

        ImGui::Spacing();
    }

    // Each group under its own folding header (none for a section of one group under the section's own
    // name: its rows go right under the heading).
    for (const fr::SettingGroup s_Group : s_Groups)
        DrawGroup(s_Group, p_Values, p_Pending, p_Live, !fr::SettingGroupIsSection(s_Group));

    ImGui::PopID();
    ImGui::Spacing();
}

void SettingsMenu::DrawGroup(fr::SettingGroup p_Group, const std::map<std::string, std::string>& p_Values,
                             const std::set<std::string>& p_Pending, bool p_Live, bool p_Heading) {
    const auto s_ValueOf = ValueOf(p_Values);
    const std::vector<const fr::SettingInfo*> s_Rows = GroupRows(p_Group);

    if (s_Rows.empty())
        return;

    const size_t s_Index = static_cast<size_t>(p_Group);

    if (p_Heading) {
        // The only fold-outs: a framed, full-width tap target.
        const std::string s_Label = fmt::format("{}###settings-group-{}", fr::SettingGroupName(p_Group), s_Index);
        bool s_Now = false;
        {
            const InputBlock s_Block(Guarded());
            ImGui::SetNextItemOpen(m_Open[s_Index]);
            s_Now = ImGui::CollapsingHeader(s_Label.c_str());
        }

        // Opening or closing moves every row below it.
        if (s_Now != m_Open[s_Index] && Accept()) {
            m_Open[s_Index] = s_Now;
        }

        if (!m_Open[s_Index])
            return;
    }

    ImGui::PushID(static_cast<int>(s_Index));

    if (const std::string_view s_Intro = fr::SettingGroupIntro(p_Group); !s_Intro.empty())
        WrappedTextAvail(s_Intro);

    // The variant chances go under their maps' headings (DrawVariantPlaces), after the other rows.
    for (const fr::SettingInfo* s_Info : s_Rows) {
        if (ChanceOption(*s_Info))
            continue;

        bool s_ParentOn = true;
        const fr::SettingInfo* s_Parent = s_Info->parent.empty() ? nullptr : fr::FindSetting(s_Info->parent);

        if (s_Parent)
            s_ParentOn = fr::SettingBoolOr(*s_Parent, s_ValueOf(*s_Parent));

        // Indented under its switch when the switch is in this group; one in another group (a
        // footstep range, or in Advanced) is not: its help names the switch.
        DrawRow(*s_Info, s_ValueOf(*s_Info), p_Pending.count(std::string(s_Info->key)) > 0, s_ParentOn, p_Live,
                s_Parent && s_Parent->group == p_Group);
    }

    if (p_Group == fr::SettingGroup::Variants)
        DrawVariantPlaces(s_Rows, p_Values, p_Pending, p_Live);

    ImGui::PopID();
    ImGui::Spacing();
}

void SettingsMenu::DrawVariantPlaces(const std::vector<const fr::SettingInfo*>& p_Rows,
                                     const std::map<std::string, std::string>& p_Values,
                                     const std::set<std::string>& p_Pending, bool p_Live) {
    const auto s_ValueOf = ValueOf(p_Values);

    // The chance rows by map (the rows are A-Z by label, so a map's rows are together).
    struct Place {
        std::string_view name;
        const fr::VariantOption* option;
        std::vector<const fr::SettingInfo*> rows;
    };
    std::vector<Place> s_Places;

    for (const fr::SettingInfo* s_Info : p_Rows) {
        if (const fr::VariantOption* s_Option = ChanceOption(*s_Info)) {
            const std::string_view s_Name = fr::VariantPlaceName(*s_Option);

            if (s_Places.empty() || s_Places.back().name != s_Name)
                s_Places.push_back({s_Name, s_Option, {}});

            s_Places.back().rows.push_back(s_Info);
        }
    }

    if (s_Places.empty())
        return;

    bool s_VariantsOn = true;

    if (const fr::SettingInfo* s_Switch = fr::FindSetting("variants"))
        s_VariantsOn = fr::SettingBoolOr(*s_Switch, s_ValueOf(*s_Switch));

    ImGui::Spacing();
    WrappedTextAvail(kNotInShowdownsLegend);

    // Shown, not only in a tooltip: a disabled row's tooltip can't be reached with a controller.
    for (const fr::FullAvailability s_Why :
         {fr::FullAvailability::NotBuildable, fr::FullAvailability::NotInstalled, fr::FullAvailability::SwitchedOff}) {
        if (std::any_of(m_MissingNow.begin(), m_MissingNow.end(),
                        [&](const auto& p_Option) { return p_Option.second == s_Why; }))
            WrappedTextAvail(UnavailableLegend(s_Why));
    }

    for (const Place& s_Place : s_Places) {
        // Not folded: a heading with a divider, its sliders right under it. The heading says what is left
        // for the normal map, or that the chances are over 100 %.
        const int32_t s_Sum = PlaceChanceSum(*s_Place.option, s_ValueOf, m_MissingNow);
        const std::string s_Share =
            s_Sum <= 100 ? fmt::format("normal map {}%", 100 - s_Sum) : fmt::format("chances add up to {}%", s_Sum);
        const std::string s_Label = fmt::format("{}: {}", s_Place.name, s_Share);

        if (!s_VariantsOn)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

        if (s_Sum > 100)
            ImGui::PushStyleColor(ImGuiCol_Text, kPendingColor);

        ImGui::Spacing();
        // The style's own divider thickness (the SDK's ImGui has no SeparatorTextBorderSize var).
        ImGui::SeparatorText(s_Label.c_str());

        if (s_Sum > 100)
            ImGui::PopStyleColor();

        if (!s_VariantsOn)
            ImGui::PopStyleVar();

        // Wrapped at the window's edge, never cut off.
        if (s_Sum > 100) {
            WrappedTextAvail(fmt::format("The chances add up to {}%, so each is scaled down to fit. Move a slider "
                                         "to fix it.",
                                         s_Sum),
                             &kPendingColor);
        }

        for (const fr::SettingInfo* s_Info : s_Place.rows) {
            DrawRow(*s_Info, s_ValueOf(*s_Info), p_Pending.count(std::string(s_Info->key)) > 0, s_VariantsOn, p_Live,
                    false, PlaceRowLabel(s_Info->label, s_Place.name));
        }
    }

    // Again under the last map, where the marked rows end.
    ImGui::Spacing();
    WrappedTextAvail(kNotInShowdownsLegend);
}

void SettingsMenu::Tooltip(const fr::SettingInfo& p_Info, bool p_Live, std::string_view p_Choice) {
    ImGui::BeginTooltip();
    // Wrapped at spaces only (WrappedText), never inside a word.
    const float s_Width = WrapWidthTo(ImGui::GetFontSize() * kTooltipWrapEms);
    WrappedText(p_Info.label, s_Width);

    if (!p_Choice.empty())
        WrappedText(fmt::format("Chosen: {}", p_Choice), s_Width);

    ImGui::Separator();
    WrappedText(p_Info.help, s_Width);

    if (const fr::VariantOption* s_Option = ChanceOption(p_Info)) {
        if (const auto s_It = m_MissingNow.find(std::string(s_Option->name)); s_It != m_MissingNow.end())
            WrappedText(UnavailableTip(s_It->second), s_Width);
    }

    if (!p_Info.parent.empty()) {
        if (const fr::SettingInfo* s_Parent = fr::FindSetting(p_Info.parent))
            WrappedText(fmt::format("Only with \"{}\" on.", s_Parent->label), s_Width);
    }

    WrappedText(fmt::format("Default: {}", ValueLabel(p_Info, p_Info.def)), s_Width);
    const std::string_view s_When =
        p_Live ? fr::SettingAppliesText(p_Info.applies) : fr::SettingAppliesText(fr::SettingApplies::Restart);
    WrappedText(fmt::format("A change takes effect {}.", s_When), s_Width);
    WrappedTextDisabled(fmt::format("Settings file key: {}", p_Info.key), s_Width);
    ImGui::EndTooltip();
}

void SettingsMenu::DrawRow(const fr::SettingInfo& p_Info, const std::string& p_Value, bool p_Pending, bool p_ParentOn,
                           bool p_Live, bool p_Indent, std::string_view p_Shown) {
    // Checked as the row starts, so a guard armed by a row above in this frame already holds.
    const bool s_Blocked = Guarded();
    const InputBlock s_Block(s_Blocked);
    const std::string s_Key(p_Info.key);
    const fr::VariantOption* s_Chance = ChanceOption(p_Info);
    // A variant that can't come up: marked, disabled, its slider and label grayed out as a locked checkbox is.
    const bool s_Missing = s_Chance && m_MissingNow.contains(std::string(s_Chance->name));
    const std::string s_Label = fmt::format("{}{}{}", p_Shown.empty() ? p_Info.label : p_Shown,
                                            s_Chance && !fr::OfferedInShowdowns(*s_Chance) ? kNotInShowdownsMark : "",
                                            s_Missing ? kUnavailableMark : "");
    // Inside the row's guard block, BeginDisabled does not dim: the disabled alpha is pushed then.
    const bool s_MissingDim = s_Missing && s_Blocked;
    const bool s_Dim = !p_ParentOn && !s_Missing;
    ImGui::PushID(s_Key.c_str());

    if (p_Indent)
        ImGui::Indent();

    // A setting whose switch is off still edits; it is only dimmed.
    if (s_Dim)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

    if (s_MissingDim)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * ImGui::GetStyle().DisabledAlpha);

    ImGui::BeginDisabled(s_Missing);

    const ImGuiStyle& s_Style = ImGui::GetStyle();
    const char* s_PendingText = (p_Live && p_Info.applies == fr::SettingApplies::NextLoad)
                                    ? ICON_MD_SCHEDULE " next mission"
                                    : ICON_MD_RESTART_ALT " game restart";
    // The marks after the label on its line: the help mark and, for a saved change, its mark.
    const float s_Marks = s_Style.ItemSpacing.x + ImGui::CalcTextSize(ICON_MD_HELP_OUTLINE).x +
                          (p_Pending ? s_Style.ItemSpacing.x + ImGui::CalcTextSize(s_PendingText).x : 0.0f);
    const float s_Width = std::max(120.0f, ImGui::GetContentRegionAvail().x * kWidgetShare);
    const std::vector<fr::SettingListOption> s_Options = fr::SettingListOptions(p_Info);
    // The widget drew the label itself (a checkbox whose label fits on the line, a checklist's
    // header); otherwise the label follows the widget, wrapped (below the switch).
    bool s_LabelDrawn = false;
    // A choice shortened to fit its box: the tooltip shows it whole.
    std::string s_ShortenedChoice;

    switch (p_Info.type) {
        case fr::SettingType::Bool: {
            bool s_On = fr::SettingBoolOr(p_Info, p_Value);
            s_LabelDrawn = ImGui::GetFrameHeight() + s_Style.ItemInnerSpacing.x +
                               ImGui::CalcTextSize(s_Label.c_str()).x + s_Marks <=
                           ImGui::GetContentRegionAvail().x;

            if (ImGui::Checkbox(s_LabelDrawn ? s_Label.c_str() : "##value", &s_On) && Accept())
                Commit(p_Info, s_On ? "true" : "false");

            break;
        }

        case fr::SettingType::Int:
        case fr::SettingType::Number: {
            const auto s_Editing = m_Editing.find(s_Key);
            double s_Value = fr::SettingNumberOr(p_Info, s_Editing != m_Editing.end() ? s_Editing->second : p_Value);
            ImGuiSliderFlags s_Flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput;

            if (p_Info.logScale)
                s_Flags |= ImGuiSliderFlags_Logarithmic;

            ImGui::SetNextItemWidth(s_Width);

            // Every slider moves in its row's steps (5% for most percents, 50 merces, ...):
            // exact values are hard to hit on the touchscreen, and the d-pad moves one
            // step. The slider runs over the steps and shows the value, so it never shows
            // one between them. A value typed in the ini off the steps (33) shows as is
            // until moved.
            int s_Step = fr::SettingStepIndex(p_Info, s_Value);
            const double s_StepValue = fr::SettingStepValue(p_Info, s_Step);
            const double s_ShownValue = fr::SameSettingValue(p_Info, fr::NormalizeSettingNumber(p_Info, s_Value),
                                                             fr::FormatSettingNumber(s_Value))
                                            ? s_StepValue
                                            : s_Value;
            std::string s_Shown =
                fmt::format("{:.{}f}{}{}", s_ShownValue, Decimals(p_Info), p_Info.unit.empty() ? "" : " ", p_Info.unit);
            // The slider's text is a printf format: a percent sign is written twice.
            for (size_t i = s_Shown.find('%'); i != std::string::npos; i = s_Shown.find('%', i + 2))
                s_Shown.insert(i, 1, '%');

            if (ImGui::SliderInt("##value", &s_Step, 0, fr::SettingStepCount(p_Info), s_Shown.c_str(), s_Flags))
                m_Editing[s_Key] = fr::NormalizeSettingNumber(p_Info, fr::SettingStepValue(p_Info, s_Step));

            // A slider takes the drag (a vertical swipe on it does not scroll).
            TouchScroll::KeepItemDrag();

            // Written once, when the slider is let go (the SDK rewrites the ini on every write).
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (const auto s_It = m_Editing.find(s_Key); s_It != m_Editing.end()) {
                    if (!fr::SameSettingValue(p_Info, s_It->second, p_Value))
                        Commit(p_Info, s_It->second);

                    m_Editing.erase(s_It);
                }
            }
            else if (!ImGui::IsItemActive()) {
                m_Editing.erase(s_Key);
            }

            break;
        }

        case fr::SettingType::Choice: {
            const std::vector<std::string_view> s_Choices = fr::SettingChoices(p_Info);
            const int s_Index = fr::SettingChoiceIndex(p_Info, p_Value);
            const std::string s_Preview =
                s_Index < 0
                    ? fmt::format("{} (not a choice)", p_Value)
                    : fmt::format("{}{}", fr::SettingChoiceLabel(p_Info, s_Choices[s_Index]),
                                  m_MissingNow.contains(std::string(s_Choices[s_Index])) ? kUnavailableMark : "");
            // Wide enough for the longest choice when the row has room (two thirds of it at
            // most, leaving the rest to the label); a choice that still does not fit is
            // shortened at a word, with "...", and shown whole in the tooltip.
            float s_Widest = ImGui::CalcTextSize(s_Preview.c_str()).x;

            for (const std::string_view s_Choice : s_Choices)
                s_Widest = std::max(s_Widest, ImGui::CalcTextSize(fr::SettingChoiceLabel(p_Info, s_Choice).c_str()).x);

            const float s_Chrome = 2.0f * s_Style.FramePadding.x + ImGui::GetFrameHeight();
            const float s_ComboWidth =
                std::clamp(s_Widest + s_Chrome, s_Width, std::max(s_Width, ImGui::GetContentRegionAvail().x * 0.66f));
            const std::string s_Shown = FitText(s_Preview, s_ComboWidth - s_Chrome);

            if (s_Shown != s_Preview)
                s_ShortenedChoice = s_Preview;

            ImGui::SetNextItemWidth(s_ComboWidth);

            if (ImGui::BeginCombo("##value", s_Shown.c_str())) {
                m_ComboOpen = true;

                for (size_t i = 0; i < s_Choices.size(); ++i) {
                    const bool s_Selected = static_cast<int>(i) == s_Index;
                    // variant_force: a variant that can't come up is listed, but can't be chosen.
                    const bool s_Unavailable = m_MissingNow.contains(std::string(s_Choices[i]));
                    const std::string s_Name = fmt::format("{}{}", fr::SettingChoiceLabel(p_Info, s_Choices[i]),
                                                           s_Unavailable ? kUnavailableMark : "");
                    const ImGuiSelectableFlags s_Flags =
                        s_Unavailable ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None;

                    // The list closes over the rows below it: they ignore the rest of the tap.
                    if (ImGui::Selectable(s_Name.c_str(), s_Selected, s_Flags)) {
                        ArmGuard();

                        if (!s_Selected)
                            Commit(p_Info, std::string(s_Choices[i]));
                    }

                    if (s_Selected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }

            break;
        }

        case fr::SettingType::List: {
            if (!s_Options.empty()) {
                // A header that unfolds the checkboxes (drawn below the row's marks).
                const size_t s_Checked = fr::SettingListIds(p_Info, p_Value).size();
                const std::string s_Header = fmt::format("{} ({} checked)###{}", s_Label, s_Checked, s_Key);
                ImGui::SetNextItemOpen(m_ListOpen[s_Key]);
                const bool s_Now = ImGui::TreeNodeEx(s_Header.c_str(), ImGuiTreeNodeFlags_NoTreePushOnOpen);

                // Unfolding or folding moves every row below it.
                if (s_Now != m_ListOpen[s_Key] && Accept()) {
                    m_ListOpen[s_Key] = s_Now;
                }

                s_LabelDrawn = true;
                break;
            }

            const auto s_Editing = m_Editing.find(s_Key);
            const std::string& s_Text = s_Editing != m_Editing.end() ? s_Editing->second : p_Value;
            char s_Buffer[512] {};
            std::memcpy(s_Buffer, s_Text.data(), std::min(s_Text.size(), sizeof(s_Buffer) - 1));
            ImGui::SetNextItemWidth(s_Width);

            if (ImGui::InputText("##value", s_Buffer, sizeof(s_Buffer)))
                m_Editing[s_Key] = s_Buffer;

            // A text field takes the drag (it selects text).
            TouchScroll::KeepItemDrag();

            // Written when the field is left (Enter or a tap elsewhere).
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (const auto s_It = m_Editing.find(s_Key); s_It != m_Editing.end()) {
                    if (s_It->second != p_Value)
                        Commit(p_Info, s_It->second);

                    m_Editing.erase(s_It);
                }
            }
            else if (!ImGui::IsItemActive()) {
                m_Editing.erase(s_Key);
            }

            break;
        }
    }

    // A disabled row still shows its tooltip.
    constexpr ImGuiHoveredFlags kRowHover = ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled;
    bool s_Hovered = ImGui::IsItemHovered(kRowHover);

    if (!s_LabelDrawn) {
        // After the widget, wrapped at spaces to the room the marks leave, so a long label
        // goes on in a second line instead of being cut off at the window's edge (never
        // inside a word; its lines are one item, so a tap on any of them counts).
        ImGui::SameLine(0.0f, s_Style.ItemInnerSpacing.x);
        const float s_Room = std::max(ImGui::GetContentRegionAvail().x - s_Marks, ImGui::GetFontSize() * 6.0f);
        WrappedText(s_Label, s_Room);
        s_Hovered = s_Hovered || ImGui::IsItemHovered(kRowHover);

        // A checkbox's label toggles it, as the checkbox's own label does.
        if (p_Info.type == fr::SettingType::Bool && !s_Missing && Tapped() && Accept())
            Commit(p_Info, fr::SettingBoolOr(p_Info, p_Value) ? "false" : "true");
    }

    ImGui::EndDisabled();

    if (s_MissingDim)
        ImGui::PopStyleVar();

    if (s_Hovered)
        Tooltip(p_Info, p_Live, s_ShortenedChoice);

    if (s_Dim)
        ImGui::PopStyleVar();

    // The help mark: a tap shows the tooltip without touching the setting.
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_MD_HELP_OUTLINE);

    if (ImGui::IsItemHovered())
        Tooltip(p_Info, p_Live, s_ShortenedChoice);

    if (p_Pending) {
        ImGui::SameLine();
        ImGui::TextColored(kPendingColor, "%s", s_PendingText);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Saved; the mod uses it %s.",
                              (p_Live ? std::string(fr::SettingAppliesText(p_Info.applies))
                                      : std::string(fr::SettingAppliesText(fr::SettingApplies::Restart)))
                                  .c_str());
    }

    if (!s_Options.empty() && m_ListOpen[s_Key]) {
        if (!p_ParentOn)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

        DrawChecklist(p_Info, p_Value, s_Blocked);

        if (!p_ParentOn)
            ImGui::PopStyleVar();
    }

    if (p_Indent)
        ImGui::Unindent();

    ImGui::PopID();
}

void SettingsMenu::DrawChecklist(const fr::SettingInfo& p_Info, const std::string& p_Value, bool p_Blocked) {
    const std::set<int32_t> s_Ids = fr::SettingListIds(p_Info, p_Value);
    bool s_AnyLocked = false;
    ImGui::Indent();

    for (const fr::SettingListOption& s_Option : fr::SettingListOptions(p_Info)) {
        bool s_On = s_Ids.contains(s_Option.id);
        // Checking it would leave the shuffle too few to pick from: the box stays unchecked.
        std::set<int32_t> s_With = s_Ids;
        s_With.insert(s_Option.id);
        const bool s_Locked = !s_On && !fr::SettingListLeavesEnough(p_Info, s_With);
        s_AnyLocked = s_AnyLocked || s_Locked;

        ImGui::PushID(s_Option.id);
        // Inside the row's guard block, BeginDisabled does not dim: a locked box keeps its look.
        const bool s_Dim = s_Locked && p_Blocked;

        if (s_Dim)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * ImGui::GetStyle().DisabledAlpha);

        ImGui::BeginDisabled(s_Locked);

        // A choice in the unfolded list: the other boxes and rows ignore the rest of the tap.
        if (ImGui::Checkbox(s_Option.name.c_str(), &s_On) && Accept()) {
            Commit(p_Info, fr::SettingListToggle(p_Info, p_Value, s_Option.id, s_On));
        }

        ImGui::EndDisabled();

        if (s_Dim)
            ImGui::PopStyleVar();

        if (s_Locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Uncheck another first: the shuffle needs enough left to pick from.");

        ImGui::PopID();
    }

    if (s_AnyLocked) {
        WrappedTextDisabled("The dimmed ones must stay unchecked: the shuffle needs enough left to pick from.",
                            ImGui::GetContentRegionAvail().x);
    }

    ImGui::Unindent();
}
