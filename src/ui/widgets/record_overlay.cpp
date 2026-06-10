#include "record_overlay.hpp"
#include "imgui.h"

void RecordOverlayWidget::Render(RecordEngine& engine) {
    if (!engine.IsRecording() && !m_showReview) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 260, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(250, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (engine.IsRecording()) {
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.7f,0.1f,0.1f,1.f));
        ImGui::Begin("* Recording", nullptr, flags);
        ImGui::PopStyleColor();

        ImGui::Text("Events captured: %d", (int)engine.Events().size());

        if (ImGui::Button("[Stop]", ImVec2(-1, 0))) {
            engine.Stop();
            m_captured    = engine.ToActivities(m_captureTimings, m_fixedDelay);
            m_showReview  = true;
        }
        ImGui::End();

    } else if (m_showReview) {
        ImGui::Begin("Review Recording", nullptr, flags);

        ImGui::Checkbox("Use captured timing", &m_captureTimings);
        if (!m_captureTimings) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            ImGui::InputInt("ms##fixd", &m_fixedDelay);
            m_fixedDelay = std::max(0, m_fixedDelay);
        }

        ImGui::Text("%d activities captured:", (int)m_captured.size());
        ImGui::BeginChild("##recacts", ImVec2(0, 160), true);
        for (int i = 0; i < (int)m_captured.size(); ++i) {
            ImGui::PushID(i);
            auto& a = m_captured[i];
            auto getDelay = [](ActivityData& d) -> int& {
                return std::visit([](auto&& v) -> int& {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, WaitActivity>)
                        return v.duration_ms;
                    else
                        return v.delay_ms;
                }, d);
            };
            int& delayRef = getDelay(a.data);
            ImGui::Text("%2d.", i+1); ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            ImGui::InputInt("##dl", &delayRef, 0);
            delayRef = std::max(0, delayRef);
            ImGui::SameLine();
            ImGui::TextUnformatted(a.enabled ? "[on]" : "[off]");
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (ImGui::Button("Accept##rec", ImVec2(115, 0))) {
            if (OnFinished) OnFinished(m_captured);
            m_showReview = false;
            m_captured.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard##rec", ImVec2(115, 0))) {
            m_showReview = false;
            m_captured.clear();
        }
        ImGui::End();
    }
}
