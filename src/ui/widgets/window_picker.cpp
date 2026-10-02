#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
#include "window_picker.hpp"
#include "imgui.h"

void WindowPickerWidget::Render(IWindowFinder* finder, const WindowTarget& /*current*/) {
    if (!m_picking) {
        if (ImGui::Button("[Pick Window]") && finder) {
            m_picking = true;
            m_hover   = std::nullopt;
#if defined(_WIN32)
            // Prime prev state so the initial press doesn't immediately fire
            m_prevLBtn = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
#endif
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f,0.2f,0.2f,1.f));
        if (ImGui::Button("Cancel Pick")) {
            // Cancel: clear hover BEFORE stopping so we never self-select
            m_hover   = std::nullopt;
            m_picking = false;
        }
        ImGui::PopStyleColor();

        if (finder) {
            // The info tooltip is its own OS window (multi-viewport) and can follow
            // the cursor outside the app; never pick one of our own viewport windows.
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
            if (m_hover) {
                ImGui::BeginTooltip();
                ImGui::Text("Title:  %s", m_hover->title.c_str());
                ImGui::Text("Class:  %s", m_hover->class_name.c_str());
                ImGui::Text("Handle: %llu", (unsigned long long)m_hover->handle);
                ImGui::Text("Rect:   %d,%d  %dx%d",
                    m_hover->rect.x, m_hover->rect.y,
                    m_hover->rect.w, m_hover->rect.h);
                ImGui::EndTooltip();
            }

#if defined(_WIN32)
            // Falling-edge detection on LButton: fired when released after being pressed.
            // This avoids the initial press (that opened pick mode) from instantly confirming.
            bool curLBtn = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            bool clickedNow = m_prevLBtn && !curLBtn;  // pressed last frame, released this frame
            m_prevLBtn = curLBtn;

            if (clickedNow && m_hover.has_value()) {
                if (OnPicked) OnPicked(*m_hover);
                m_hover   = std::nullopt;
                m_picking = false;
            }
#else
            // Fallback: ImGui mouse click (only works within our window)
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
                if (m_hover.has_value()) {
                    if (OnPicked) OnPicked(*m_hover);
                }
                m_hover   = std::nullopt;
                m_picking = false;
            }
#endif
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            // Cancel: clear hover first to prevent self-select
            m_hover   = std::nullopt;
            m_picking = false;
        }
    }
}
