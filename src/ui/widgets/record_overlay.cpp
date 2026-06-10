#include "record_overlay.hpp"
#include "imgui.h"

std::vector<Activity> RecordOverlayWidget::ApplyFilters(
    std::vector<Activity> acts,
    const std::vector<RecordedEvent>& evs) const
{
    // Keep only mouse-move events where the cursor paused >= threshold ms after
    if (m_stopMoveMsEnabled && m_stopMoveMsThreshold > 0) {
        std::vector<Activity> filtered;
        for (size_t k = 0; k < acts.size(); ++k) {
            bool isMove = std::holds_alternative<MouseMoveActivity>(acts[k].data);
            if (isMove && k < evs.size()) {
                uint64_t dt = (k + 1 < evs.size())
                    ? evs[k + 1].timestamp_ms - evs[k].timestamp_ms
                    : (uint64_t)m_stopMoveMsThreshold; // last event: keep
                if (dt < (uint64_t)m_stopMoveMsThreshold)
                    continue;
            }
            filtered.push_back(acts[k]);
        }
        acts = std::move(filtered);
    }

    // Drop all mouse-move and scroll; keep only clicks and key presses
    if (m_clicksOnly) {
        acts.erase(
            std::remove_if(acts.begin(), acts.end(), [](const Activity& a) {
                return std::holds_alternative<MouseMoveActivity>(a.data) ||
                       std::holds_alternative<MouseScrollActivity>(a.data);
            }),
            acts.end()
        );
    }

    return acts;
}

void RecordOverlayWidget::TriggerReview(RecordEngine& engine) {
    m_engineRef  = &engine;
    m_captured   = ApplyFilters(engine.ToActivities(m_captureTimings, m_fixedDelay),
                                engine.Events());
    m_showReview = true;
}

void RecordOverlayWidget::Render(RecordEngine& engine) {
    if (!engine.IsRecording() && !m_showReview) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 270, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;

    if (engine.IsRecording()) {
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.7f, 0.1f, 0.1f, 1.f));
        ImGui::Begin("* Recording", nullptr, flags);
        ImGui::PopStyleColor();

        ImGui::Text("Events captured: %d", (int)engine.Events().size());

        ImGui::Separator();
        ImGui::TextDisabled("Filter options:");
        ImGui::Checkbox("Clicks only##ro", &m_clicksOnly);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drop mouse-move and scroll; keep only clicks and key presses");

        ImGui::Checkbox("Snap moves##ro", &m_stopMoveMsEnabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Only keep mouse-move events where the cursor paused >= N ms");
        if (m_stopMoveMsEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(55);
            ImGui::InputInt("ms##smmv", &m_stopMoveMsThreshold);
            m_stopMoveMsThreshold = std::max(10, m_stopMoveMsThreshold);
        }

        if (ImGui::Button("[Stop]", ImVec2(-1, 0))) {
            engine.Stop();
            m_engineRef  = &engine;
            m_captured   = ApplyFilters(engine.ToActivities(m_captureTimings, m_fixedDelay),
                                        engine.Events());
            m_showReview = true;
        }
        ImGui::End();

    } else if (m_showReview) {
        ImGui::Begin("Review Recording", nullptr, flags);

        bool regen = false;
        if (ImGui::Checkbox("Use captured timing", &m_captureTimings)) regen = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Use real inter-event delays from the recording");
        if (!m_captureTimings) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputInt("ms##fixd", &m_fixedDelay)) regen = true;
            m_fixedDelay = std::max(0, m_fixedDelay);
        }

        if (ImGui::Checkbox("Clicks only##ro2", &m_clicksOnly)) regen = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drop mouse-move and scroll; keep only clicks and key presses");

        if (ImGui::Checkbox("Snap moves##ro2", &m_stopMoveMsEnabled)) regen = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Only keep mouse-move events where the cursor paused >= N ms");
        if (m_stopMoveMsEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(55);
            if (ImGui::InputInt("ms##smmv2", &m_stopMoveMsThreshold)) regen = true;
            m_stopMoveMsThreshold = std::max(10, m_stopMoveMsThreshold);
        }

        if (regen && m_engineRef) {
            m_captured = ApplyFilters(
                m_engineRef->ToActivities(m_captureTimings, m_fixedDelay),
                m_engineRef->Events());
        }

        ImGui::Text("%d activities:", (int)m_captured.size());
        ImGui::BeginChild("##recacts", ImVec2(0, 150), true);
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
            ImGui::Text("%2d.", i + 1); ImGui::SameLine();
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
            m_engineRef  = nullptr;
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard##rec", ImVec2(115, 0))) {
            m_showReview = false;
            m_captured.clear();
            m_engineRef  = nullptr;
        }
        ImGui::End();
    }
}
