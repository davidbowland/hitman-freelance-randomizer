#pragma once

#include "DragScroll.h"

#include <imgui.h>

// ImGui::Begin/End for an overlay window used on the Steam Deck's touchscreen
// (render thread). Dragging the body scrolls the window, a short press is a
// tap, only the title bar moves it, and its scrollbar is wide. The settings
// are scoped to this window's Begin, so other mods' windows keep ImGui's
// defaults. A drag scrolls this window only, never a child region it starts
// on. A widget that drags itself (a slider, a text field) calls KeepItemDrag
// so a press on it never scrolls.
class TouchScroll {
public:
    // As ImGui::Begin; End must be called whatever it returns.
    bool Begin(const char* p_Name, bool* p_Open, ImGuiWindowFlags p_Flags = 0);
    void End();

    // True while a drag scrolls the window (hover tooltips may skip themselves).
    bool Dragging() const { return m_Drag.Dragging(); }

    // After a widget that drags itself: if this press activated it, the press
    // does not scroll the window currently in a TouchScroll Begin/End.
    static void KeepItemDrag();

private:
    // Whether p_Point is on the current window's body (right after its Begin).
    static bool InBody(ImVec2 p_Point, ImGuiWindowFlags p_Flags);
    void Update(ImGuiWindowFlags p_Flags);

    fr::DragScroll m_Drag {0.0f};
};
