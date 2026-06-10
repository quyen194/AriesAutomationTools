#include "activity_editor.hpp"
#include "imgui.h"
#include <SDL.h>
#include <algorithm>
#include <sstream>
#include <random>
#include <iomanip>

// ── UUID helper ───────────────────────────────────────────────────────────────
static std::string GenId() {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> d;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << d(rng) << "-" << std::setw(4) << (d(rng)&0xFFFF)
       << "-" << std::setw(4) << (d(rng)&0xFFFF)
       << "-" << std::setw(4) << (d(rng)&0xFFFF)
       << "-" << std::setw(8) << d(rng) << std::setw(4) << (d(rng)&0xFFFF);
    return ss.str();
}

// ── ImGui key -> key name string (matches key_map.cpp names) ─────────────────
static std::string ImGuiKeyToKeyName(ImGuiKey key) {
    switch (key) {
        case ImGuiKey_Tab:          return "tab";
        case ImGuiKey_LeftArrow:    return "left";
        case ImGuiKey_RightArrow:   return "right";
        case ImGuiKey_UpArrow:      return "up";
        case ImGuiKey_DownArrow:    return "down";
        case ImGuiKey_PageUp:       return "prior";
        case ImGuiKey_PageDown:     return "next";
        case ImGuiKey_Home:         return "home";
        case ImGuiKey_End:          return "end";
        case ImGuiKey_Insert:       return "insert";
        case ImGuiKey_Delete:       return "delete";
        case ImGuiKey_Backspace:    return "backspace";
        case ImGuiKey_Space:        return "space";
        case ImGuiKey_Enter:        return "enter";
        case ImGuiKey_Escape:       return "escape";
        case ImGuiKey_LeftCtrl:     return "lctrl";
        case ImGuiKey_LeftShift:    return "lshift";
        case ImGuiKey_LeftAlt:      return "lalt";
        case ImGuiKey_RightCtrl:    return "rctrl";
        case ImGuiKey_RightShift:   return "rshift";
        case ImGuiKey_RightAlt:     return "ralt";
        case ImGuiKey_F1:           return "f1";
        case ImGuiKey_F2:           return "f2";
        case ImGuiKey_F3:           return "f3";
        case ImGuiKey_F4:           return "f4";
        case ImGuiKey_F5:           return "f5";
        case ImGuiKey_F6:           return "f6";
        case ImGuiKey_F7:           return "f7";
        case ImGuiKey_F8:           return "f8";
        case ImGuiKey_F9:           return "f9";
        case ImGuiKey_F10:          return "f10";
        case ImGuiKey_F11:          return "f11";
        case ImGuiKey_F12:          return "f12";
        case ImGuiKey_Keypad0:      return "numpad0";
        case ImGuiKey_Keypad1:      return "numpad1";
        case ImGuiKey_Keypad2:      return "numpad2";
        case ImGuiKey_Keypad3:      return "numpad3";
        case ImGuiKey_Keypad4:      return "numpad4";
        case ImGuiKey_Keypad5:      return "numpad5";
        case ImGuiKey_Keypad6:      return "numpad6";
        case ImGuiKey_Keypad7:      return "numpad7";
        case ImGuiKey_Keypad8:      return "numpad8";
        case ImGuiKey_Keypad9:      return "numpad9";
        case ImGuiKey_KeypadEnter:  return "numpad_enter";
        default: {
            if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
                char c = 'a' + (char)(key - ImGuiKey_A);
                return std::string(1, c);
            }
            if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
                char c = '0' + (char)(key - ImGuiKey_0);
                return std::string(1, c);
            }
            return {};
        }
    }
}

// ── Activity type names ───────────────────────────────────────────────────────
static const char* kTypes[] = {
    "mouse_move","mouse_click","mouse_drag","mouse_scroll",
    "key_press","type_string","wait","pixel_check","run_workflow"
};
static int TypeIndex(const ActivityData& d) {
    if (std::holds_alternative<MouseMoveActivity>(d))   return 0;
    if (std::holds_alternative<MouseClickActivity>(d))  return 1;
    if (std::holds_alternative<MouseDragActivity>(d))   return 2;
    if (std::holds_alternative<MouseScrollActivity>(d)) return 3;
    if (std::holds_alternative<KeyPressActivity>(d))    return 4;
    if (std::holds_alternative<TypeStringActivity>(d))  return 5;
    if (std::holds_alternative<WaitActivity>(d))        return 6;
    if (std::holds_alternative<PixelCheckActivity>(d))  return 7;
    return 8;
}
static ActivityData DefaultData(int idx) {
    switch(idx) {
        case 0: return MouseMoveActivity{};
        case 1: return MouseClickActivity{};
        case 2: return MouseDragActivity{};
        case 3: return MouseScrollActivity{};
        case 4: return KeyPressActivity{};
        case 5: return TypeStringActivity{};
        case 6: return WaitActivity{};
        case 7: return PixelCheckActivity{};
        default: return RunWorkflowActivity{};
    }
}

static const char* BtnNames[]        = {"left","right","middle"};
static const char* PosModeNames[]    = {"absolute","relative"};
static const char* PixelActionNames[] = {"retry","skip_iteration","stop_workflow"};

// ── Short summary for list row ────────────────────────────────────────────────
static std::string ActivitySummary(const Activity& a) {
    return std::visit([](auto&& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        char buf[128]{};
        if constexpr (std::is_same_v<T,MouseMoveActivity>)
            snprintf(buf,sizeof(buf),"mouse_move %s (%d,%d) +%dms",
                v.pos_mode==PositionMode::Absolute?"abs":"rel",v.x,v.y,v.delay_ms);
        else if constexpr (std::is_same_v<T,MouseClickActivity>)
            snprintf(buf,sizeof(buf),"mouse_click %s %s (%d,%d) +%dms",
                v.button==MouseButton::Left?"left":v.button==MouseButton::Right?"right":"mid",
                v.pos_mode==PositionMode::Absolute?"abs":"rel",v.x,v.y,v.delay_ms);
        else if constexpr (std::is_same_v<T,MouseDragActivity>)
            snprintf(buf,sizeof(buf),"mouse_drag (%d,%d)->(%d,%d) %dms",
                v.from_x,v.from_y,v.to_x,v.to_y,v.duration_ms);
        else if constexpr (std::is_same_v<T,MouseScrollActivity>)
            snprintf(buf,sizeof(buf),"mouse_scroll dy=%d +%dms",v.delta_y,v.delay_ms);
        else if constexpr (std::is_same_v<T,KeyPressActivity>) {
            std::string mods;
            for (auto& m : v.modifiers) mods += m + "+";
            snprintf(buf,sizeof(buf),"key_press %s%s +%dms",mods.c_str(),v.key.c_str(),v.delay_ms);
        } else if constexpr (std::is_same_v<T,TypeStringActivity>)
            snprintf(buf,sizeof(buf),"type_string \"%s\" +%dms",v.text.substr(0,20).c_str(),v.delay_ms);
        else if constexpr (std::is_same_v<T,WaitActivity>)
            snprintf(buf,sizeof(buf),"wait %dms +/-%dms",v.duration_ms,v.random_range_ms);
        else if constexpr (std::is_same_v<T,PixelCheckActivity>)
            snprintf(buf,sizeof(buf),"pixel_check #%06X (%d,%d)",v.color_rgb,v.x,v.y);
        else if constexpr (std::is_same_v<T,RunWorkflowActivity>)
            snprintf(buf,sizeof(buf),"run_workflow %s",v.workflow_id.c_str());
        return buf;
    }, a.data);
}

// ─────────────────────────────────────────────────────────────────────────────

void ActivityEditorWidget::Render(Workflow& wf, int currentStep) {
    // Sync selection vector length
    m_selection.resize(wf.activities.size(), false);

    ImGui::Text("Activities (%d)", (int)wf.activities.size());
    ImGui::SameLine();
    if (ImGui::Button("+ Add##act")) {
        m_draft            = Activity{GenId(), true, MouseClickActivity{}};
        m_editIdx          = -1;
        m_openModal        = true;
        m_keyCaptureActive = false;
        m_scrollCapture    = false;
        m_pickStage        = PickStage::None;
    }

    // ── Batch operations bar ──────────────────────────────────────────────────
    {
        int total = (int)wf.activities.size();
        int selCount = 0;
        for (bool b : m_selection) if (b) ++selCount;

        bool allSel = (total > 0 && selCount == total);
        if (ImGui::Checkbox("##selAll", &allSel)) {
            std::fill(m_selection.begin(), m_selection.end(), allSel);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select all");
        ImGui::SameLine();

        bool hasSelection = selCount > 0;
        if (!hasSelection) ImGui::BeginDisabled();

        if (ImGui::SmallButton("Del Sel")) {
            for (int i = total - 1; i >= 0; --i)
                if (m_selection[i]) wf.activities.erase(wf.activities.begin() + i);
            m_selection.clear();
            m_editIdx = -1;
            if (OnChanged) OnChanged();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Enable Sel")) {
            for (int i = 0; i < (int)wf.activities.size(); ++i)
                if (m_selection[i]) wf.activities[i].enabled = true;
            if (OnChanged) OnChanged();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Disable Sel")) {
            for (int i = 0; i < (int)wf.activities.size(); ++i)
                if (m_selection[i]) wf.activities[i].enabled = false;
            if (OnChanged) OnChanged();
        }

        if (!hasSelection) ImGui::EndDisabled();
    }

    ImGui::BeginChild("##actlist", ImVec2(0, -60), true);
    for (int i = 0; i < (int)wf.activities.size(); ++i) {
        auto& a = wf.activities[i];
        ImGui::PushID(i);

        // Multi-select checkbox
        bool sel = (i < (int)m_selection.size()) ? m_selection[i] : false;
        if (ImGui::Checkbox("##sel", &sel)) {
            if (i < (int)m_selection.size()) m_selection[i] = sel;
        }
        ImGui::SameLine();

        bool en = a.enabled;
        if (ImGui::Checkbox("##en", &en)) { a.enabled = en; if (OnChanged) OnChanged(); }
        ImGui::SameLine();

        bool isCurrent = (currentStep == i);

        // Auto-scroll to current step when it changes
        if (isCurrent && currentStep != m_lastScrolledStep) {
            ImGui::SetScrollHereY(0.5f);
            m_lastScrolledStep = currentStep;
        }

        char label[256];
        snprintf(label, sizeof(label), "%2d. %s", i+1, ActivitySummary(a).c_str());

        if (isCurrent) {
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.1f,0.65f,0.1f,0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.1f,0.65f,0.1f,0.75f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.1f,0.65f,0.1f,0.90f));
        }
        if (ImGui::Selectable(label, isCurrent, ImGuiSelectableFlags_AllowOverlap)) {
            m_draft            = a;
            m_editIdx          = i;
            m_openModal        = true;
            m_keyCaptureActive = false;
            m_scrollCapture    = false;
            m_pickStage        = PickStage::None;
        }
        if (isCurrent) ImGui::PopStyleColor(3);

        ImGui::SameLine();
        RenderMoveButtons(wf, i);

        ImGui::PopID();
    }
    // Reset scroll tracking when workflow stops
    if (currentStep < 0) m_lastScrolledStep = -1;
    ImGui::EndChild();

    ImGui::Separator();

    // Pick overlay is rendered here (outside the modal) when active
    if (m_pickStage != PickStage::None) {
        RenderPickOverlay();
    }

    if (m_openModal) {
        ImGui::OpenPopup("##actmodal");
        m_openModal = false;
    }
    RenderModal(wf);
}

// ── Coordinate picker overlay ─────────────────────────────────────────────────
void ActivityEditorWidget::RenderPickOverlay() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 pos = io.MousePos;
    pos.x += 16.f; pos.y += 16.f;
    // Keep overlay on screen
    if (pos.x + 220 > io.DisplaySize.x) pos.x = io.DisplaySize.x - 220;
    if (pos.y + 80  > io.DisplaySize.y) pos.y = io.DisplaySize.y - 80;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##pickoverlay", nullptr, flags)) {
        int gx = 0, gy = 0;
        SDL_GetGlobalMouseState(&gx, &gy);

        const char* label = (m_pickStage == PickStage::DragTo) ? "Pick end pos" : "Pick pos";
        ImGui::TextColored(ImVec4(1,0.9f,0.3f,1), "%s: %d, %d", label, gx, gy);
        ImGui::TextDisabled("Enter = confirm   Esc = cancel");

        if (ImGui::Button("Capture##pick") || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            ApplyPickedCoords(gx, gy);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_pickStage = PickStage::None;
            m_openModal = true;
        }
    }
    ImGui::End();
}

void ActivityEditorWidget::ApplyPickedCoords(int x, int y) {
    bool done = true;
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T,MouseMoveActivity>  ||
                      std::is_same_v<T,MouseClickActivity>  ||
                      std::is_same_v<T,MouseScrollActivity>) {
            v.x = x; v.y = y;
        } else if constexpr (std::is_same_v<T,MouseDragActivity>) {
            if (m_pickStage == PickStage::DragFrom) {
                v.from_x = x; v.from_y = y;
                m_pickStage = PickStage::DragTo;
                done = false;
            } else {
                v.to_x = x; v.to_y = y;
            }
        }
    }, m_draft.data);

    if (done) {
        m_pickStage = PickStage::None;
        m_openModal = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void ActivityEditorWidget::RenderMoveButtons(Workflow& wf, int idx) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2,0));
    if (ImGui::SmallButton("^") && idx > 0) {
        std::swap(wf.activities[idx], wf.activities[idx-1]);
        if (OnChanged) OnChanged();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("v") && idx < (int)wf.activities.size()-1) {
        std::swap(wf.activities[idx], wf.activities[idx+1]);
        if (OnChanged) OnChanged();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("X")) {
        wf.activities.erase(wf.activities.begin() + idx);
        if (OnChanged) OnChanged();
    }
    ImGui::PopStyleVar();
}

void ActivityEditorWidget::RenderModal(Workflow& wf) {
    ImGui::SetNextWindowSize(ImVec2(480, 540), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("##actmodal", nullptr, ImGuiWindowFlags_NoResize)) return;

    ImGui::Text(m_editIdx < 0 ? "Add Activity" : "Edit Activity");
    ImGui::Separator();

    int typeIdx = TypeIndex(m_draft.data);
    if (ImGui::Combo("Type", &typeIdx, kTypes, IM_ARRAYSIZE(kTypes))) {
        m_draft.data       = DefaultData(typeIdx);
        m_keyCaptureActive = false;
        m_scrollCapture    = false;
        m_scrollAccum      = 0.f;
    }
    ImGui::Checkbox("Enabled", &m_draft.enabled);
    ImGui::Separator();

    RenderActivityFields(m_draft.data);

    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(100, 0))) {
        if (m_editIdx < 0) {
            wf.activities.push_back(m_draft);
        } else {
            wf.activities[m_editIdx] = m_draft;
        }
        if (OnChanged) OnChanged();
        m_keyCaptureActive = false;
        m_scrollCapture    = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        m_keyCaptureActive = false;
        m_scrollCapture    = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ActivityEditorWidget::RenderActivityFields(ActivityData& data) {
    std::visit([this](auto&& v) {
        using T = std::decay_t<decltype(v)>;

        auto posMode = [](PositionMode& pm) {
            int idx = (pm == PositionMode::Relative) ? 1 : 0;
            if (ImGui::Combo("Position mode", &idx, PosModeNames, 2))
                pm = idx == 1 ? PositionMode::Relative : PositionMode::Absolute;
        };
        auto btnCombo = [](MouseButton& b) {
            int idx = (b == MouseButton::Right) ? 1 : (b == MouseButton::Middle) ? 2 : 0;
            if (ImGui::Combo("Button", &idx, BtnNames, 3))
                b = (MouseButton)idx;
        };
        auto delayFields = [](int& dm, int& dr) {
            ImGui::InputInt("Delay after (ms)", &dm);
            ImGui::InputInt("Random range (ms)", &dr);
            dm = std::max(0, dm); dr = std::max(0, dr);
        };

        // Helper: XY fields then Pick button on next line
        auto xyPick = [this](int& x, int& y, PickStage stage) {
            ImGui::InputInt("X", &x);
            ImGui::InputInt("Y", &y);
            if (ImGui::Button("Pick position##xy")) {
                m_pickStage = stage;
                ImGui::CloseCurrentPopup();
            }
        };

        if constexpr (std::is_same_v<T,MouseMoveActivity>) {
            posMode(v.pos_mode);
            xyPick(v.x, v.y, PickStage::Single);
            delayFields(v.delay_ms, v.delay_rand_ms);

        } else if constexpr (std::is_same_v<T,MouseClickActivity>) {
            posMode(v.pos_mode);
            xyPick(v.x, v.y, PickStage::Single);
            btnCombo(v.button);
            ImGui::Checkbox("Double click", &v.double_click);
            delayFields(v.delay_ms, v.delay_rand_ms);

        } else if constexpr (std::is_same_v<T,MouseDragActivity>) {
            posMode(v.pos_mode);

            ImGui::InputInt("From X", &v.from_x);
            ImGui::InputInt("From Y", &v.from_y);
            if (ImGui::Button("Pick start##dfs")) {
                m_pickStage = PickStage::DragFrom;
                ImGui::CloseCurrentPopup();
            }

            ImGui::InputInt("To X", &v.to_x);
            ImGui::InputInt("To Y", &v.to_y);
            if (ImGui::Button("Pick end##dfe")) {
                m_pickStage = PickStage::DragTo;
                ImGui::CloseCurrentPopup();
            }

            btnCombo(v.button);
            ImGui::InputInt("Duration (ms)", &v.duration_ms);
            v.duration_ms = std::max(1, v.duration_ms);
            ImGui::InputInt("Delay after (ms)", &v.delay_ms);

        } else if constexpr (std::is_same_v<T,MouseScrollActivity>) {
            posMode(v.pos_mode);
            xyPick(v.x, v.y, PickStage::Single);
            ImGui::InputInt("Delta X", &v.delta_x);

            if (m_scrollCapture) {
                m_scrollAccum += ImGui::GetIO().MouseWheel;
                ImGui::TextColored(ImVec4(1,0.9f,0.3f,1),
                    "Scroll now... delta Y: %d", (int)m_scrollAccum);
                if (ImGui::Button("Done##sc") || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
                    v.delta_y       = (int)m_scrollAccum;
                    m_scrollCapture = false;
                    m_scrollAccum   = 0.f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel##sc")) {
                    m_scrollCapture = false;
                    m_scrollAccum   = 0.f;
                }
            } else {
                ImGui::InputInt("Delta Y", &v.delta_y);
                if (ImGui::Button("Capture scroll##sc")) {
                    m_scrollCapture = true;
                    m_scrollAccum   = 0.f;
                }
            }
            ImGui::InputInt("Delay after (ms)", &v.delay_ms);

        } else if constexpr (std::is_same_v<T,KeyPressActivity>) {
            if (m_keyCaptureActive) {
                ImGui::TextColored(ImVec4(1,0.9f,0.3f,1), "Press any key... (Esc to cancel)");

                for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                    ImGuiKey key = (ImGuiKey)k;
                    if (!ImGui::IsKeyPressed(key, false)) continue;

                    if (key == ImGuiKey_Escape) {
                        m_keyCaptureActive = false;
                        break;
                    }
                    // Skip modifier-only presses — wait for a regular key
                    if (key == ImGuiKey_LeftCtrl  || key == ImGuiKey_RightCtrl  ||
                        key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
                        key == ImGuiKey_LeftAlt   || key == ImGuiKey_RightAlt   ||
                        key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper) continue;

                    auto name = ImGuiKeyToKeyName(key);
                    if (!name.empty()) {
                        v.key = name;
                        v.modifiers.clear();
                        ImGuiIO& io = ImGui::GetIO();
                        if (io.KeyCtrl)  v.modifiers.push_back("ctrl");
                        if (io.KeyShift) v.modifiers.push_back("shift");
                        if (io.KeyAlt)   v.modifiers.push_back("alt");
                        m_keyCaptureActive = false;
                    }
                    break;
                }
            } else {
                static char keyBuf[64]{};
                strncpy(keyBuf, v.key.c_str(), sizeof(keyBuf)-1);
                if (ImGui::InputText("Key##kp", keyBuf, sizeof(keyBuf))) v.key = keyBuf;
                ImGui::TextDisabled("e.g. space, f1, a, enter");
                if (ImGui::Button("Capture key##kp")) m_keyCaptureActive = true;

                // Show current modifiers
                ImGui::Text("Modifiers:");
                ImGui::SameLine();
                ImGui::TextDisabled("%s", v.modifiers.empty() ? "(none)" : [&]{
                    std::string s;
                    for (auto& m : v.modifiers) s += m + " ";
                    return s;
                }().c_str());
                ImGui::TextDisabled("(hold Ctrl/Shift/Alt while pressing Capture key)");
            }
            delayFields(v.delay_ms, v.delay_rand_ms);

        } else if constexpr (std::is_same_v<T,TypeStringActivity>) {
            static char textBuf[512]{};
            strncpy(textBuf, v.text.c_str(), sizeof(textBuf)-1);
            if (ImGui::InputTextMultiline("Text", textBuf, sizeof(textBuf), ImVec2(0,80)))
                v.text = textBuf;
            ImGui::InputInt("Char delay (ms)", &v.delay_between_chars_ms);
            v.delay_between_chars_ms = std::max(0, v.delay_between_chars_ms);
            ImGui::InputInt("Delay after (ms)", &v.delay_ms);

        } else if constexpr (std::is_same_v<T,WaitActivity>) {
            ImGui::InputInt("Duration (ms)", &v.duration_ms);
            ImGui::InputInt("Random range (ms)", &v.random_range_ms);
            v.duration_ms     = std::max(0, v.duration_ms);
            v.random_range_ms = std::max(0, v.random_range_ms);

        } else if constexpr (std::is_same_v<T,PixelCheckActivity>) {
            posMode(v.pos_mode);
            ImGui::InputInt("X", &v.x); ImGui::InputInt("Y", &v.y);

            float col[3] = {
                ((v.color_rgb>>16)&0xFF)/255.f,
                ((v.color_rgb>> 8)&0xFF)/255.f,
                ( v.color_rgb     &0xFF)/255.f
            };
            if (ImGui::ColorEdit3("Color", col)) {
                v.color_rgb = ((uint32_t)(col[0]*255)<<16) |
                              ((uint32_t)(col[1]*255)<< 8) |
                               (uint32_t)(col[2]*255);
            }
            ImGui::InputInt("Tolerance", &v.tolerance);
            v.tolerance = std::max(0, std::min(255, v.tolerance));

            int actionIdx = (v.on_no_match == PixelCheckAction::SkipIteration) ? 1
                          : (v.on_no_match == PixelCheckAction::StopWorkflow)   ? 2 : 0;
            if (ImGui::Combo("On no match", &actionIdx, PixelActionNames, 3))
                v.on_no_match = (PixelCheckAction)actionIdx;
            ImGui::InputInt("Retry interval (ms)", &v.retry_interval_ms);
            ImGui::InputInt("Retry timeout (ms, 0=inf)", &v.retry_timeout_ms);
            ImGui::InputInt("Delay after (ms)", &v.delay_ms);

        } else if constexpr (std::is_same_v<T,RunWorkflowActivity>) {
            static char idBuf[64]{};
            strncpy(idBuf, v.workflow_id.c_str(), sizeof(idBuf)-1);
            if (ImGui::InputText("Workflow ID", idBuf, sizeof(idBuf)))
                v.workflow_id = idBuf;
            ImGui::InputInt("Delay after (ms)", &v.delay_ms);
        }
    }, data);
}
