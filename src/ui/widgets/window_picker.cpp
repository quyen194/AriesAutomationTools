#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
#include "window_picker.hpp"
#include "imgui.h"
#include "ui/ui_util.hpp"
#include <SDL.h>

void WindowPickerWidget::Stop() {
    // Clear hover BEFORE stopping so we never self-select
    m_hover   = std::nullopt;
    m_picking = false;
    if (OnPickEnd) OnPickEnd();
}

void WindowPickerWidget::Render(IWindowFinder* finder, const WindowTarget& /*current*/) {
    if (!m_picking) {
        if (ImGui::Button("[Pick Window]") && finder) {
            m_picking = true;
            m_hover   = std::nullopt;
#if defined(_WIN32)
            // Prime prev state so the initial press doesn't immediately fire
            m_prevLBtn = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
#endif
            if (OnPickBegin) OnPickBegin();
        }
    } else {
        // Reachable only if the user restores the app window mid-pick
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f,0.2f,0.2f,1.f));
        if (ImGui::Button("Cancel Pick")) Stop();
        ImGui::PopStyleColor();
    }
}

void WindowPickerWidget::Update(IWindowFinder* finder) {
    if (!m_picking) return;
    if (!finder) { Stop(); return; }

    // The info panel is its own OS window (multi-viewport) and follows the
    // cursor outside the app; never pick one of our own viewport windows.
    auto under = finder->WindowUnderCursor();
    bool ownWindow = false;
    if (under) {
        for (ImGuiViewport* vp : ImGui::GetPlatformIO().Viewports)
            if (vp->PlatformHandleRaw &&
                (uint64_t)(uintptr_t)vp->PlatformHandleRaw == under->handle) {
                ownWindow = true; break;
            }
    }
    m_hover = ownWindow ? std::nullopt : under;

    // The app window is minimized, so ImGui's mouse position is stale and a
    // tooltip would stay inside the minimized main viewport. Use a standalone
    // top-most panel with its own viewport, placed at the OS cursor.
    int gx = 0, gy = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    ImVec2 pos((float)gx + 16.f, (float)gy + 16.f);
    ImVec2 lo, hi;
    MonitorWorkRectAt(ImVec2((float)gx, (float)gy), lo, hi);
    const ImVec2 sz(320.f, 100.f);
    if (pos.x + sz.x > hi.x) pos.x = hi.x - sz.x;
    if (pos.y + sz.y > hi.y) pos.y = hi.y - sz.y;
    if (pos.x < lo.x) pos.x = lo.x;
    if (pos.y < lo.y) pos.y = lo.y;

    ImGuiWindowClass wc;
    wc.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge | ImGuiViewportFlags_TopMost |
                                  ImGuiViewportFlags_NoFocusOnAppearing |
                                  ImGuiViewportFlags_NoFocusOnClick | ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&wc);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("##winpickhud", nullptr, flags)) {
        if (m_hover) {
            ImGui::Text("Title:  %s", m_hover->title.c_str());
            ImGui::Text("Class:  %s", m_hover->class_name.c_str());
            ImGui::Text("Handle: %llu", (unsigned long long)m_hover->handle);
            ImGui::Text("Rect:   %d,%d  %dx%d",
                m_hover->rect.x, m_hover->rect.y,
                m_hover->rect.w, m_hover->rect.h);
        } else {
            ImGui::TextDisabled("Hover a window");
        }
        ImGui::TextDisabled("Click = pick   Esc = cancel");
    }
    ImGui::End();

#if defined(_WIN32)
    // Falling-edge detection on LButton: fired when released after being pressed.
    // This avoids the initial press (that opened pick mode) from instantly confirming.
    bool curLBtn = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool clickedNow = m_prevLBtn && !curLBtn;  // pressed last frame, released this frame
    m_prevLBtn = curLBtn;

    if (clickedNow && m_hover.has_value()) {
        WindowInfo picked = *m_hover;
        Stop();
        if (OnPicked) OnPicked(picked);
        return;
    }
    // App window is minimized and has no keyboard focus — poll Esc from the OS
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { Stop(); return; }
#else
    // Fallback: ImGui mouse click (only works within our window)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        std::optional<WindowInfo> picked = m_hover;
        Stop();
        if (picked && OnPicked) OnPicked(*picked);
        return;
    }
#endif

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) Stop();
}
