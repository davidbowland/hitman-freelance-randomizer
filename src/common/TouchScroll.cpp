#include "TouchScroll.h"

#include <algorithm>

// imgui_internal.h is not in the DevPkg; the SDK's static imgui.lib defines
// it (ImGui 1.92). It only resets the context's active widget id.
namespace ImGui {
IMGUI_API void ClearActiveID();
}

namespace {

// Scrollbar width in font heights, and at least kMinScrollbar px (about 30 px on the Deck).
constexpr float kScrollbarEms = 1.75f;
constexpr float kMinScrollbar = 28.0f;
// A press that moves less than this (font heights) is a tap, not a drag.
constexpr float kDragThresholdEms = 0.5f;
// The resize grip in the window's bottom-right corner (ImGui draws it 1.35 font heights wide).
constexpr float kGripEms = 1.4f;

// The window between TouchScroll::Begin and End (render thread).
TouchScroll* g_Current = nullptr;

} // namespace

bool TouchScroll::Begin(const char* p_Name, bool* p_Open, ImGuiWindowFlags p_Flags) {
    ImGuiIO& s_Io = ImGui::GetIO();
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, std::max(kMinScrollbar, ImGui::GetFontSize() * kScrollbarEms));

    // Read by Begin for this window (and inherited by its child windows); restored at once.
    const bool s_TitleBarOnly = s_Io.ConfigWindowsMoveFromTitleBarOnly;
    s_Io.ConfigWindowsMoveFromTitleBarOnly = true;
    const bool s_Visible = ImGui::Begin(p_Name, p_Open, p_Flags);
    s_Io.ConfigWindowsMoveFromTitleBarOnly = s_TitleBarOnly;

    g_Current = this;

    if (s_Visible)
        Update(p_Flags);
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        m_Drag.Release();

    return s_Visible;
}

void TouchScroll::End() {
    ImGui::End();
    ImGui::PopStyleVar();
    g_Current = nullptr;
}

void TouchScroll::KeepItemDrag() {
    if (g_Current && ImGui::IsItemActivated())
        g_Current->m_Drag.Block();
}

bool TouchScroll::InBody(ImVec2 p_Point, ImGuiWindowFlags p_Flags) {
    const ImGuiStyle& s_Style = ImGui::GetStyle();
    const ImVec2 s_Pos = ImGui::GetWindowPos();
    const ImVec2 s_Size = ImGui::GetWindowSize();
    // Right after Begin: the content's right edge, left of the scrollbar.
    const float s_Right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const float s_Top = s_Pos.y + ((p_Flags & ImGuiWindowFlags_NoTitleBar) ? 0.0f : ImGui::GetFrameHeight());
    const float s_Bottom = s_Pos.y + s_Size.y - s_Style.WindowPadding.y;
    const float s_Grip = ImGui::GetFontSize() * kGripEms;

    if (p_Point.x < s_Pos.x + s_Style.WindowPadding.x * 0.5f || p_Point.x >= s_Right || p_Point.y < s_Top ||
        p_Point.y >= s_Bottom)
        return false;

    return p_Point.x < s_Pos.x + s_Size.x - s_Grip || p_Point.y < s_Pos.y + s_Size.y - s_Grip;
}

// Right after Begin, before the window's widgets: they run with this frame's
// press state.
void TouchScroll::Update(ImGuiWindowFlags p_Flags) {
    const ImVec2 s_Mouse = ImGui::GetIO().MousePos;

    if (m_Drag.Pressed() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // A drag ends: the widget it started on is still active and would take
        // this release as a click. Clearing it first makes the release a no-op.
        if (m_Drag.Release())
            ImGui::ClearActiveID();

        return;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Only a press on this window's own body: not its title bar, scrollbar,
        // resize grip, a child region or another window above it.
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && InBody(s_Mouse, p_Flags)) {
            m_Drag = fr::DragScroll(ImGui::GetFontSize() * kDragThresholdEms);
            m_Drag.Press(s_Mouse.y, ImGui::GetScrollY());
        }
        else {
            m_Drag.Release();
        }

        return;
    }

    if (m_Drag.Pressed()) {
        // Applied by ImGui at the next frame's Begin.
        if (const std::optional<float> s_Scroll = m_Drag.Move(s_Mouse.y, ImGui::GetScrollMaxY()))
            ImGui::SetScrollY(*s_Scroll);
    }
}
