#include "workflow_list.hpp"
#include "imgui.h"

void WorkflowListWidget::Render(
        const std::vector<Workflow>& workflows,
        std::function<WorkflowStatus(const std::string&)> getStatus,
        const std::string& selectedId)
{
    ImGui::BeginGroup();
    ImGui::Text("Workflows");
    ImGui::Separator();

    float listH = ImGui::GetContentRegionAvail().y - 30.0f;
    ImGui::BeginChild("##wflist", ImVec2(0, listH), false);
    for (auto& wf : workflows) {
        WorkflowStatus st = getStatus(wf.id);
        bool selected = (wf.id == selectedId);

        // Status indicator badge
        const char* badge;
        ImVec4 badgeColor;
        switch (st) {
            case WorkflowStatus::Starting:
                badge = "[~]"; badgeColor = ImVec4(0.3f, 0.8f, 1.0f, 1.f); break;
            case WorkflowStatus::Running:
                badge = "[R]"; badgeColor = ImVec4(0.2f, 0.9f, 0.2f, 1.f); break;
            case WorkflowStatus::WaitingRepeat:
                badge = "[W]"; badgeColor = ImVec4(0.3f, 0.75f, 0.55f, 1.f); break;
            case WorkflowStatus::Interrupted:
                badge = "[!]"; badgeColor = ImVec4(1.0f, 0.55f, 0.1f, 1.f); break;
            case WorkflowStatus::Paused:
                badge = "[P]"; badgeColor = ImVec4(1.0f, 0.85f, 0.0f, 1.f); break;
            default:
                badge = "[ ]"; badgeColor = ImVec4(0.5f, 0.5f, 0.5f, 1.f); break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, badgeColor);
        ImGui::Text("%s", badge);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // Name color/hover based on status
        bool active = (st != WorkflowStatus::Idle);
        if (!wf.enabled) {
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.45f, 0.45f, 0.45f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f,  0.3f,  0.3f,  0.6f));
        } else if (st == WorkflowStatus::Starting) {
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.5f,  0.9f,  1.0f,  1.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.1f,  0.4f,  0.6f,  0.5f));
        } else if (st == WorkflowStatus::Running) {
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.3f,  1.0f,  0.3f,  1.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.1f,  0.6f,  0.1f,  0.5f));
        } else if (st == WorkflowStatus::WaitingRepeat) {
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.4f,  0.9f,  0.7f,  1.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.1f,  0.4f,  0.3f,  0.5f));
        } else if (st == WorkflowStatus::Interrupted) {
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.0f,  0.7f,  0.3f,  1.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.5f,  0.3f,  0.05f, 0.5f));
        } else if (st == WorkflowStatus::Paused) {
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.0f,  0.9f,  0.2f,  1.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.5f,  0.45f, 0.05f, 0.5f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.85f, 0.85f, 0.85f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.5f,  0.25f, 0.4f));
        }

        char label[256];
        snprintf(label, sizeof(label), "%s##%s", wf.name.c_str(), wf.id.c_str());
        if (ImGui::Selectable(label, selected)) {
            if (OnSelect) OnSelect(wf.id);
        }
        ImGui::PopStyleColor(2);

        if (ImGui::IsItemHovered()) {
            if (!wf.enabled)
                ImGui::SetTooltip("Workflow is disabled");
            else if (st == WorkflowStatus::WaitingRepeat)
                ImGui::SetTooltip("Waiting for repeat interval before next run");
            else if (st == WorkflowStatus::Interrupted)
                ImGui::SetTooltip("Interrupted by smart detection (user is active)");
            else if (st == WorkflowStatus::Starting)
                ImGui::SetTooltip("Waiting for user idle before starting");
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("+##add")) { if (OnAdd) OnAdd(); }
    ImGui::SameLine();
    if (ImGui::Button("Dup##dup") && !selectedId.empty()) { if (OnDuplicate) OnDuplicate(selectedId); }
    ImGui::SameLine();
    if (ImGui::Button("Del##del") && !selectedId.empty()) { if (OnDelete) OnDelete(selectedId); }

    ImGui::EndGroup();
}
