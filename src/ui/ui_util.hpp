#pragma once
#include "imgui.h"
#include <SDL.h>
#include <algorithm>
#include <cstdint>

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

// Delay between hiding the app window and taking a snip screenshot. Windows (DWM)
// fades hidden windows out over ~200 ms; capturing earlier blends a ghost of the app
// into the screenshot.
constexpr unsigned kSnipHideDelayMs = 350;

// Turns `win` into a borderless always-on-top overlay covering display 0.
// The window is deliberately 1 px taller than the display: some GPU drivers promote a
// window that exactly covers the monitor to exclusive fullscreen, which blanks the
// monitor during the mode switch and ignores SDL_SetWindowOpacity (solid black overlay).
inline bool CoverDisplayWithWindow(SDL_Window* win) {
    SDL_DisplayMode dm{};
    if (!win || SDL_GetCurrentDisplayMode(0, &dm) != 0) return false;
    SDL_SetWindowBordered(win, SDL_FALSE);
    SDL_SetWindowAlwaysOnTop(win, SDL_TRUE);
    SDL_SetWindowPosition(win, 0, 0);
    SDL_SetWindowSize(win, dm.w, dm.h + 1);
    return true;
}

// Draws the snip screenshot (texture `tex`, w x h, pixel (c,r) at desktop (c,r)) as the
// overlay background: dimmed, with the selection [x1,x2)x[y1,y2) at full brightness.
inline void DrawSnipBackground(ImDrawList* dl, unsigned int tex, int w, int h,
                               bool hasSel, int x1, int y1, int x2, int y2) {
    if (!tex || w <= 0 || h <= 0) return;
    ImTextureID id = (ImTextureID)(intptr_t)tex;
    dl->AddImage(id, ImVec2(0, 0), ImVec2((float)w, (float)h));
    dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)w, (float)h), IM_COL32(0, 0, 0, 110));
    x1 = std::clamp(x1, 0, w); x2 = std::clamp(x2, 0, w);
    y1 = std::clamp(y1, 0, h); y2 = std::clamp(y2, 0, h);
    if (hasSel && x2 > x1 && y2 > y1)
        dl->AddImage(id, ImVec2((float)x1, (float)y1), ImVec2((float)x2, (float)y2),
                     ImVec2((float)x1 / w, (float)y1 / h), ImVec2((float)x2 / w, (float)y2 / h));
}
