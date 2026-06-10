#include "window_picker.hpp"
#include "imgui.h"

void WindowPickerWidget::Render(IWindowFinder* finder, const WindowTarget& current) {
    // Show current target summary
    const char* typeStr = "Global";
    std::string detail;
    switch (current.type) {
        case WindowTarget::Type::ByTitle:  typeStr = "By Title";  detail = current.title;      break;
        case WindowTarget::Type::ByClass:  typeStr = "By Class";  detail = current.class_name; break;
        case WindowTarget::Type::ByHandle: typeStr = "By Handle"; detail = std::to_string(current.handle); break;
        default: break;
    }
    if (detail.empty())
        ImGui::TextDisabled("Window: %s", typeStr);
    else
        ImGui::TextDisabled("Window: %s — %s", typeStr, detail.c_str());

    ImGui::SameLine();

    if (!m_picking) {
        if (ImGui::Button("🔍 Pick Window") && finder) m_picking = true;
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f,0.2f,0.2f,1.f));
        if (ImGui::Button("Cancel Pick")) m_picking = false;
        ImGui::PopStyleColor();

        if (finder) {
            m_hover = finder->WindowUnderCursor();
            if (m_hover) {
                ImGui::BeginTooltip();
                ImGui::Text("Title:  %s", m_hover->title.c_str());
                ImGui::Text("Class:  %s", m_hover->class_name.c_str());
                ImGui::Text("Handle: %llu", (unsigned long long)m_hover->handle);
                ImGui::Text("Rect:   %d,%d  %dx%d",
                    m_hover->rect.x, m_hover->rect.y,
                    m_hover->rect.w, m_hover->rect.h);
                ImGui::EndTooltip();

                // Confirm on left-click outside our own window
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                    !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
                    if (OnPicked) OnPicked(*m_hover);
                    m_picking = false;
                }
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_picking = false;
    }
}
