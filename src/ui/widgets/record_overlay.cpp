#include "record_overlay.hpp"
#include "imgui.h"
#include <algorithm>
#include <string>

static std::string RecKeyToName(ImGuiKey key) {
    switch (key) {
        case ImGuiKey_Tab:        return "tab";
        case ImGuiKey_LeftArrow:  return "left";
        case ImGuiKey_RightArrow: return "right";
        case ImGuiKey_UpArrow:    return "up";
        case ImGuiKey_DownArrow:  return "down";
        case ImGuiKey_PageUp:     return "prior";
        case ImGuiKey_PageDown:   return "next";
        case ImGuiKey_Home:       return "home";
        case ImGuiKey_End:        return "end";
        case ImGuiKey_Insert:     return "insert";
        case ImGuiKey_Delete:     return "delete";
        case ImGuiKey_Backspace:  return "backspace";
        case ImGuiKey_Space:      return "space";
        case ImGuiKey_Enter:      return "enter";
        case ImGuiKey_Escape:     return "escape";
        case ImGuiKey_F1:  return "f1";  case ImGuiKey_F2:  return "f2";
        case ImGuiKey_F3:  return "f3";  case ImGuiKey_F4:  return "f4";
        case ImGuiKey_F5:  return "f5";  case ImGuiKey_F6:  return "f6";
        case ImGuiKey_F7:  return "f7";  case ImGuiKey_F8:  return "f8";
        case ImGuiKey_F9:  return "f9";  case ImGuiKey_F10: return "f10";
        case ImGuiKey_F11: return "f11"; case ImGuiKey_F12: return "f12";
        case ImGuiKey_Keypad0: return "numpad0"; case ImGuiKey_Keypad1: return "numpad1";
        case ImGuiKey_Keypad2: return "numpad2"; case ImGuiKey_Keypad3: return "numpad3";
        case ImGuiKey_Keypad4: return "numpad4"; case ImGuiKey_Keypad5: return "numpad5";
        case ImGuiKey_Keypad6: return "numpad6"; case ImGuiKey_Keypad7: return "numpad7";
        case ImGuiKey_Keypad8: return "numpad8"; case ImGuiKey_Keypad9: return "numpad9";
        case ImGuiKey_KeypadEnter: return "numpad_enter";
        default: {
            if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
                return std::string(1, (char)('a' + (key - ImGuiKey_A)));
            if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
                return std::string(1, (char)('0' + (key - ImGuiKey_0)));
            return {};
        }
    }
}

void RecordOverlayWidget::SetHotkey(const std::string& hk) {
    strncpy(m_hotkeyBuf, hk.c_str(), sizeof(m_hotkeyBuf) - 1);
    m_hotkeyBuf[sizeof(m_hotkeyBuf) - 1] = '\0';
}

void RecordOverlayWidget::Open() {
    m_windowOpen = true;
}

std::vector<Activity> RecordOverlayWidget::ApplyFilters(
    std::vector<Activity> acts,
    const std::vector<RecordedEvent>& evs) const
{
    if (m_stopMoveMsEnabled && m_stopMoveMsThreshold > 0) {
        std::vector<Activity> filtered;
        for (size_t k = 0; k < acts.size(); ++k) {
            bool isMove = std::holds_alternative<MouseMoveActivity>(acts[k].data);
            if (isMove && k < evs.size()) {
                uint64_t dt = (k + 1 < evs.size())
                    ? evs[k + 1].timestamp_ms - evs[k].timestamp_ms
                    : (uint64_t)m_stopMoveMsThreshold;
                if (dt < (uint64_t)m_stopMoveMsThreshold)
                    continue;
            }
            filtered.push_back(acts[k]);
        }
        acts = std::move(filtered);
    }

    if (m_clicksOnly) {
        acts.erase(
            std::remove_if(acts.begin(), acts.end(), [](const Activity& a) {
                return std::holds_alternative<MouseMoveActivity>(a.data) ||
                       std::holds_alternative<MouseScrollActivity>(a.data);
            }),
            acts.end());
    }

    return acts;
}

void RecordOverlayWidget::TriggerReview(RecordEngine& engine) {
    m_engineRef  = &engine;
    m_captured   = ApplyFilters(engine.ToActivities(m_captureTimings, m_fixedDelay),
                                engine.Events());
    m_reviewSelected.assign(m_captured.size(), true);
    m_showReview = true;
    m_windowOpen = false;
}

void RecordOverlayWidget::Render(RecordEngine& engine) {
    if (!m_windowOpen && !engine.IsRecording() && !m_showReview) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 270, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;

    // ── Pre-start: configure and start ───────────────────────────────────────
    if (m_windowOpen && !engine.IsRecording()) {
        ImGui::Begin("Recording", nullptr, flags);

        // Hotkey setting
        ImGui::Text("Hotkey:"); ImGui::SameLine();
        if (m_hotkeyCapture) {
            ImGui::TextColored(ImVec4(1,0.9f,0.3f,1), "Press key...");
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel##hkc")) m_hotkeyCapture = false;
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                ImGuiKey key = (ImGuiKey)k;
                if (!ImGui::IsKeyPressed(key, false)) continue;
                if (key == ImGuiKey_Escape) { m_hotkeyCapture = false; break; }
                if (key == ImGuiKey_LeftCtrl  || key == ImGuiKey_RightCtrl  ||
                    key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
                    key == ImGuiKey_LeftAlt   || key == ImGuiKey_RightAlt   ||
                    key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper) continue;
                auto name = RecKeyToName(key);
                if (!name.empty()) {
                    std::string hk;
                    if (io.KeyCtrl)  hk += "ctrl+";
                    if (io.KeyShift) hk += "shift+";
                    if (io.KeyAlt)   hk += "alt+";
                    hk += name;
                    strncpy(m_hotkeyBuf, hk.c_str(), sizeof(m_hotkeyBuf) - 1);
                    if (OnHotkeyChanged) OnHotkeyChanged(hk);
                    m_hotkeyCapture = false;
                }
                break;
            }
        } else {
            ImGui::SetNextItemWidth(80);
            if (ImGui::InputText("##rchk", m_hotkeyBuf, sizeof(m_hotkeyBuf),
                                  ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (OnHotkeyChanged) OnHotkeyChanged(std::string(m_hotkeyBuf));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Hotkey to start/stop recording (e.g. f8, ctrl+r)");
            ImGui::SameLine();
            if (ImGui::SmallButton("Capture##rchk")) m_hotkeyCapture = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click then press a key combination");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Filters (disabled once started):");

        ImGui::Checkbox("Clicks only##ro", &m_clicksOnly);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drop mouse-move and scroll; keep only clicks and key presses");

        ImGui::Checkbox("Snap moves##ro", &m_stopMoveMsEnabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Only keep mouse-move events where the cursor paused >= N ms");
        if (m_stopMoveMsEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(72);
            ImGui::InputInt("ms##smmv", &m_stopMoveMsThreshold, 0, 0);
            m_stopMoveMsThreshold = std::max(10, m_stopMoveMsThreshold);
        }

        ImGui::Separator();
        float hw = (ImGui::GetContentRegionAvail().x - 4) * 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f,0.55f,0.1f,1.f));
        if (ImGui::Button("Start##recstart", ImVec2(hw, 0))) {
            engine.Start();
            m_windowOpen = false;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 4);
        if (ImGui::Button("Cancel##recopen", ImVec2(hw, 0)))
            m_windowOpen = false;

        ImGui::End();
        return;
    }

    // ── Recording in progress ─────────────────────────────────────────────────
    if (engine.IsRecording()) {
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.7f, 0.1f, 0.1f, 1.f));
        ImGui::Begin("* Recording", nullptr, flags);
        ImGui::PopStyleColor();

        int rawCount = (int)engine.Events().size();
        ImGui::Text("Events captured: %d", rawCount);

        ImGui::Separator();
        ImGui::TextDisabled("Filters (set before start):");
        ImGui::BeginDisabled(true);
        bool co = m_clicksOnly, sm = m_stopMoveMsEnabled;
        ImGui::Checkbox("Clicks only##rod", &co);
        ImGui::Checkbox("Snap moves##rod", &sm);
        if (sm) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(72);
            int thresh = m_stopMoveMsThreshold;
            ImGui::InputInt("ms##smmvd", &thresh, 0, 0);
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::Button("[Stop]", ImVec2(-1, 0))) {
            engine.Stop();
            m_engineRef  = &engine;
            m_captured   = ApplyFilters(engine.ToActivities(m_captureTimings, m_fixedDelay),
                                        engine.Events());
            m_reviewSelected.assign(m_captured.size(), true);
            m_showReview = true;
        }
        ImGui::End();
        return;
    }

    // ── Review screen ─────────────────────────────────────────────────────────
    if (m_showReview) {
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
            ImGui::SetNextItemWidth(72);
            if (ImGui::InputInt("ms##smmv2", &m_stopMoveMsThreshold, 0, 0)) regen = true;
            m_stopMoveMsThreshold = std::max(10, m_stopMoveMsThreshold);
        }

        if (regen && m_engineRef) {
            m_captured = ApplyFilters(
                m_engineRef->ToActivities(m_captureTimings, m_fixedDelay),
                m_engineRef->Events());
            m_reviewSelected.assign(m_captured.size(), true);
        }

        int acceptCount = 0;
        for (bool b : m_reviewSelected) if (b) ++acceptCount;
        ImGui::Text("%d / %d accepted:", acceptCount, (int)m_captured.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("All##rsel"))
            std::fill(m_reviewSelected.begin(), m_reviewSelected.end(), true);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select all activities");
        ImGui::SameLine();
        if (ImGui::SmallButton("None##rsel"))
            std::fill(m_reviewSelected.begin(), m_reviewSelected.end(), false);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Deselect all activities");

        ImGui::BeginChild("##recacts", ImVec2(0, 150), true);
        for (int i = 0; i < (int)m_captured.size(); ++i) {
            ImGui::PushID(i);
            auto& a = m_captured[i];

            bool sel = (i < (int)m_reviewSelected.size()) ? m_reviewSelected[i] : true;
            if (ImGui::Checkbox("##rsel", &sel))
                if (i < (int)m_reviewSelected.size()) m_reviewSelected[i] = sel;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Include this activity when accepting");
            ImGui::SameLine();

            auto getDelay = [](ActivityData& d) -> int& {
                return std::visit([](auto&& v) -> int& {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, WaitActivity>) return v.duration_ms;
                    else                                            return v.delay_ms;
                }, d);
            };
            int& delayRef = getDelay(a.data);

            if (!sel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f,0.45f,0.45f,1.f));
            ImGui::Text("%2d.", i + 1); ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            ImGui::InputInt("##dl", &delayRef, 0);
            delayRef = std::max(0, delayRef);
            ImGui::SameLine();
            ImGui::TextUnformatted(a.enabled ? "[on]" : "[off]");
            if (!sel) ImGui::PopStyleColor();

            ImGui::PopID();
        }
        ImGui::EndChild();

        if (ImGui::Button("Accept##rec", ImVec2(115, 0))) {
            std::vector<Activity> toAdd;
            for (size_t k = 0; k < m_captured.size(); ++k)
                if (k < m_reviewSelected.size() && m_reviewSelected[k])
                    toAdd.push_back(m_captured[k]);
            if (OnFinished) OnFinished(toAdd);
            m_showReview = false;
            m_captured.clear();
            m_reviewSelected.clear();
            m_engineRef = nullptr;
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard##rec", ImVec2(115, 0))) {
            m_showReview = false;
            m_captured.clear();
            m_reviewSelected.clear();
            m_engineRef = nullptr;
        }
        ImGui::End();
    }
}
