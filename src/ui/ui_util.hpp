#pragma once
#include "imgui.h"

// With multi-viewports enabled, ImGui coordinates are absolute desktop coordinates.
// Returns the work area of the monitor that contains `pt` (falls back to the main viewport).
inline void MonitorWorkRectAt(ImVec2 pt, ImVec2& outMin, ImVec2& outMax) {
    const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    for (const ImGuiPlatformMonitor& m : pio.Monitors) {
        if (pt.x >= m.MainPos.x && pt.x < m.MainPos.x + m.MainSize.x &&
            pt.y >= m.MainPos.y && pt.y < m.MainPos.y + m.MainSize.y) {
            outMin = m.WorkPos;
            outMax = ImVec2(m.WorkPos.x + m.WorkSize.x, m.WorkPos.y + m.WorkSize.y);
            return;
        }
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    outMin = vp->Pos;
    outMax = ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
}

// Position for a cursor-following panel of approx size `sz`: offset from the mouse and
// clamped to the monitor under the mouse (so it can leave the app window).
inline ImVec2 CursorPanelPos(ImVec2 sz, float offset = 16.f) {
    ImVec2 mp = ImGui::GetIO().MousePos;
    ImVec2 lo, hi;
    MonitorWorkRectAt(mp, lo, hi);
    ImVec2 pos(mp.x + offset, mp.y + offset);
    if (pos.x + sz.x > hi.x) pos.x = hi.x - sz.x;
    if (pos.y + sz.y > hi.y) pos.y = hi.y - sz.y;
    if (pos.x < lo.x) pos.x = lo.x;
    if (pos.y < lo.y) pos.y = lo.y;
    return pos;
}
