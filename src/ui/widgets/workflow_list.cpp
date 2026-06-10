#include "workflow_list.hpp"
#include "imgui.h"

void WorkflowListWidget::Render(
        const std::vector<Workflow>& workflows,
        std::function<bool(const std::string&)> isRunning,
        const std::string& selectedId)
{
    ImGui::BeginGroup();
    ImGui::Text("Workflows");
    ImGui::Separator();

    float listH = ImGui::GetContentRegionAvail().y - 30.0f;
    ImGui::BeginChild("##wflist", ImVec2(0, listH), false);
    for (auto& wf : workflows) {
        bool running = isRunning(wf.id);
        bool selected = (wf.id == selectedId);

        ImGui::PushStyleColor(ImGuiCol_Text,
            running ? ImVec4(0.2f, 0.9f, 0.2f, 1.f)
                    : ImVec4(0.5f, 0.5f, 0.5f, 1.f));
        ImGui::Text(running ? "[R]" : "[ ]");
        ImGui::PopStyleColor();
        ImGui::SameLine();

        char label[256];
        snprintf(label, sizeof(label), "%s##%s", wf.name.c_str(), wf.id.c_str());
        if (ImGui::Selectable(label, selected)) {
            if (OnSelect) OnSelect(wf.id);
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
