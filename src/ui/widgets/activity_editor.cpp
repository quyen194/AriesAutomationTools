#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
#include "activity_editor.hpp"
#include "imgui.h"
#include <SDL.h>
#include <algorithm>
#include <cctype>
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

// ── ImGui key -> key name string ──────────────────────────────────────────────
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

static const char* BtnNames[]         = {"left","right","middle"};
static const char* PosModeNames[]     = {"absolute","relative"};
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
    m_selection.resize(wf.activities.size(), false);
    ImGuiIO& io = ImGui::GetIO();

    // ── Header: count + Add button ────────────────────────────────────────────
    ImGui::Text("Activities (%d)", (int)wf.activities.size());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sequence of actions executed in order");
    ImGui::SameLine();
    if (ImGui::Button("+ Add##act")) {
        m_draft            = Activity{GenId(), true, MouseClickActivity{}};
        m_editIdx          = -1;
        m_openModal        = true;
        m_keyCaptureActive = false;
        m_scrollCapture    = false;
        m_pickStage        = PickStage::None;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a new activity");

    // ── Batch ops bar ─────────────────────────────────────────────────────────
    {
        int total    = (int)wf.activities.size();
        int selCount = 0;
        int firstSel = -1, lastSel = -1;
        for (int j = 0; j < (int)m_selection.size(); ++j) {
            if (m_selection[j]) {
                if (firstSel < 0) firstSel = j;
                lastSel = j;
                ++selCount;
            }
        }

        bool allSel = (total > 0 && selCount == total);
        if (ImGui::Checkbox("##selAll", &allSel)) {
            std::fill(m_selection.begin(), m_selection.end(), allSel);
            if (!allSel) m_lastClickedIdx = -1;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select all / deselect all");

        if (selCount > 0) {
            // Move up/down (single selection only)
            if (selCount == 1 && firstSel >= 0) {
                ImGui::SameLine();
                ImGui::BeginDisabled(firstSel == 0);
                if (ImGui::SmallButton("^##bmv")) {
                    std::swap(wf.activities[firstSel], wf.activities[firstSel - 1]);
                    std::swap(m_selection[firstSel], m_selection[firstSel - 1]);
                    m_lastClickedIdx = firstSel - 1;
                    if (OnChanged) OnChanged();
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move up");

                ImGui::SameLine();
                ImGui::BeginDisabled(firstSel >= total - 1);
                if (ImGui::SmallButton("v##bmv")) {
                    std::swap(wf.activities[firstSel], wf.activities[firstSel + 1]);
                    std::swap(m_selection[firstSel], m_selection[firstSel + 1]);
                    m_lastClickedIdx = firstSel + 1;
                    if (OnChanged) OnChanged();
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move down");
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Del##bsel")) {
                m_confirmDeleteBatch = true;
                m_confirmDeleteIdx   = -1;
                m_pendingConfirmOpen = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete selected activities");
            ImGui::SameLine();
            if (ImGui::SmallButton("On##bsel")) {
                for (int i = 0; i < (int)wf.activities.size(); ++i)
                    if (i < (int)m_selection.size() && m_selection[i])
                        wf.activities[i].enabled = true;
                if (OnChanged) OnChanged();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable selected activities");
            ImGui::SameLine();
            if (ImGui::SmallButton("Off##bsel")) {
                for (int i = 0; i < (int)wf.activities.size(); ++i)
                    if (i < (int)m_selection.size() && m_selection[i])
                        wf.activities[i].enabled = false;
                if (OnChanged) OnChanged();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Disable selected activities");
            ImGui::SameLine();
            ImGui::TextDisabled("(%d sel)", selCount);
        }
    }

    // Deferred per-item operations from context menu
    int ctxEnableIdx  = -1;
    int ctxDisableIdx = -1;
    int ctxMoveUp     = -1;
    int ctxMoveDown   = -1;

    ImGui::BeginChild("##actlist", ImVec2(0, -60), true);

    for (int i = 0; i < (int)wf.activities.size(); ++i) {
        auto& a = wf.activities[i];
        ImGui::PushID(i);

        bool isSelected = (i < (int)m_selection.size()) && m_selection[i];
        bool isCurrent  = (currentStep == i);

        if (isCurrent && currentStep != m_lastScrolledStep) {
            ImGui::SetScrollHereY(0.5f);
            m_lastScrolledStep = currentStep;
        }

        if (isCurrent) {
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.1f,0.65f,0.1f,0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.1f,0.65f,0.1f,0.75f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.1f,0.65f,0.1f,0.90f));
        }
        if (!a.enabled && !isCurrent)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1.f));

        char label[256];
        snprintf(label, sizeof(label), "%2d. %s", i+1, ActivitySummary(a).c_str());

        bool highlighted = isSelected || isCurrent;
        ImGuiSelectableFlags selFlags =
            ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick;
        if (ImGui::Selectable(label, highlighted, selFlags)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                m_draft            = a;
                m_editIdx          = i;
                m_openModal        = true;
                m_keyCaptureActive = false;
                m_scrollCapture    = false;
                m_pickStage        = PickStage::None;
            } else {
                if (io.KeyShift && m_lastClickedIdx >= 0) {
                    std::fill(m_selection.begin(), m_selection.end(), false);
                    int lo = std::min(i, m_lastClickedIdx);
                    int hi = std::max(i, m_lastClickedIdx);
                    for (int j = lo; j <= hi && j < (int)m_selection.size(); ++j)
                        m_selection[j] = true;
                } else if (io.KeyCtrl) {
                    if (i < (int)m_selection.size()) m_selection[i] = !m_selection[i];
                    m_lastClickedIdx = i;
                } else {
                    std::fill(m_selection.begin(), m_selection.end(), false);
                    if (i < (int)m_selection.size()) m_selection[i] = true;
                    m_lastClickedIdx = i;
                }
            }
        }
        if (!a.enabled && !isCurrent) ImGui::PopStyleColor();
        if (isCurrent)               ImGui::PopStyleColor(3);

        // Drag-drop source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("ACT_IDX", &i, sizeof(int));
            auto s = ActivitySummary(a);
            if (s.size() > 28) s = s.substr(0, 28) + "..";
            ImGui::Text("Move %d. %s", i+1, s.c_str());
            ImGui::EndDragDropSource();
        }
        // Drag-drop target
        if (ImGui::BeginDragDropTarget()) {
            if (auto* p = ImGui::AcceptDragDropPayload("ACT_IDX")) {
                int src = *(const int*)p->Data;
                if (src != i) {
                    Activity moved = wf.activities[src];
                    wf.activities.erase(wf.activities.begin() + src);
                    int dst = (src < i) ? i - 1 : i;
                    wf.activities.insert(wf.activities.begin() + dst, moved);
                    m_selection.clear();
                    m_selection.resize(wf.activities.size(), false);
                    if (dst < (int)m_selection.size()) m_selection[dst] = true;
                    m_lastClickedIdx = dst;
                    if (OnChanged) OnChanged();
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem("##ctx")) {
            if (ImGui::MenuItem("Edit")) {
                m_draft            = a;
                m_editIdx          = i;
                m_openModal        = true;
                m_keyCaptureActive = false;
                m_scrollCapture    = false;
                m_pickStage        = PickStage::None;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All"))
                std::fill(m_selection.begin(), m_selection.end(), true);
            if (ImGui::MenuItem("Select None")) {
                std::fill(m_selection.begin(), m_selection.end(), false);
                m_lastClickedIdx = -1;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Move Up",   "Ctrl+Up",   false, i > 0))
                ctxMoveUp = i;
            if (ImGui::MenuItem("Move Down", "Ctrl+Down", false, i < (int)wf.activities.size()-1))
                ctxMoveDown = i;
            ImGui::Separator();
            if (ImGui::MenuItem(a.enabled ? "Disable" : "Enable")) {
                if (a.enabled) ctxDisableIdx = i; else ctxEnableIdx = i;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) {
                m_confirmDeleteIdx   = i;
                m_confirmDeleteBatch = false;
                m_pendingConfirmOpen = true;
            }
            ImGui::EndPopup();
        }

        // Inline action buttons (visible when selected)
        if (isSelected) {
            ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                ImVec2(2.f, ImGui::GetStyle().ItemSpacing.y));
            if (ImGui::SmallButton(a.enabled ? "off##row" : "on##row")) {
                a.enabled = !a.enabled;
                if (OnChanged) OnChanged();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(a.enabled ? "Disable this activity" : "Enable this activity");
            ImGui::SameLine();
            if (ImGui::SmallButton("X##row")) {
                m_confirmDeleteIdx   = i;
                m_confirmDeleteBatch = false;
                m_pendingConfirmOpen = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this activity");
            ImGui::PopStyleVar();
        }

        ImGui::PopID();
    }

    if (currentStep < 0) m_lastScrolledStep = -1;

    // Process deferred operations
    if (ctxMoveUp >= 1 && ctxMoveUp < (int)wf.activities.size()) {
        std::swap(wf.activities[ctxMoveUp], wf.activities[ctxMoveUp - 1]);
        if (ctxMoveUp   < (int)m_selection.size() &&
            ctxMoveUp-1 < (int)m_selection.size())
            std::swap(m_selection[ctxMoveUp], m_selection[ctxMoveUp - 1]);
        if (OnChanged) OnChanged();
    }
    if (ctxMoveDown >= 0 && ctxMoveDown + 1 < (int)wf.activities.size()) {
        std::swap(wf.activities[ctxMoveDown], wf.activities[ctxMoveDown + 1]);
        if (ctxMoveDown   < (int)m_selection.size() &&
            ctxMoveDown+1 < (int)m_selection.size())
            std::swap(m_selection[ctxMoveDown], m_selection[ctxMoveDown + 1]);
        if (OnChanged) OnChanged();
    }
    if (ctxEnableIdx >= 0 && ctxEnableIdx < (int)wf.activities.size()) {
        wf.activities[ctxEnableIdx].enabled = true;
        if (OnChanged) OnChanged();
    }
    if (ctxDisableIdx >= 0 && ctxDisableIdx < (int)wf.activities.size()) {
        wf.activities[ctxDisableIdx].enabled = false;
        if (OnChanged) OnChanged();
    }

    // Keyboard navigation
    if (ImGui::IsWindowFocused()) {
        int n = (int)wf.activities.size();
        if (n > 0 && !io.WantTextInput) {
            int firstSel = -1, lastSel = -1, selCount = 0;
            for (int j = 0; j < (int)m_selection.size(); ++j) {
                if (m_selection[j]) {
                    if (firstSel < 0) firstSel = j;
                    lastSel = j;
                    ++selCount;
                }
            }

            bool up   = ImGui::IsKeyPressed(ImGuiKey_UpArrow,  true);
            bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);

            if (io.KeyCtrl && selCount == 1 && firstSel >= 0) {
                if (up && firstSel > 0) {
                    std::swap(wf.activities[firstSel], wf.activities[firstSel - 1]);
                    std::swap(m_selection[firstSel], m_selection[firstSel - 1]);
                    m_lastClickedIdx = firstSel - 1;
                    if (OnChanged) OnChanged();
                } else if (down && lastSel < n - 1) {
                    std::swap(wf.activities[lastSel], wf.activities[lastSel + 1]);
                    std::swap(m_selection[lastSel], m_selection[lastSel + 1]);
                    m_lastClickedIdx = lastSel + 1;
                    if (OnChanged) OnChanged();
                }
            } else if (!io.KeyCtrl) {
                if (up && firstSel > 0) {
                    if (!io.KeyShift)
                        std::fill(m_selection.begin(), m_selection.end(), false);
                    m_selection[firstSel - 1] = true;
                    if (!io.KeyShift) m_lastClickedIdx = firstSel - 1;
                }
                if (down && lastSel >= 0 && lastSel < n - 1) {
                    if (!io.KeyShift)
                        std::fill(m_selection.begin(), m_selection.end(), false);
                    m_selection[lastSel + 1] = true;
                    if (!io.KeyShift) m_lastClickedIdx = lastSel + 1;
                }
                if (down && firstSel < 0 && n > 0) {
                    m_selection[0] = true;
                    m_lastClickedIdx = 0;
                }
            }
        }
    }

    ImGui::EndChild();

    ImGui::Separator();

    // ── Confirm delete modal ───────────────────────────────────────────────────
    if (m_pendingConfirmOpen) {
        ImGui::OpenPopup("Confirm Delete##act");
        m_pendingConfirmOpen = false;
    }
    if (ImGui::BeginPopupModal("Confirm Delete##act", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        if (m_confirmDeleteBatch) {
            int cnt = 0;
            for (bool b : m_selection) if (b) ++cnt;
            ImGui::Text("Delete %d selected %s?", cnt, cnt == 1 ? "activity" : "activities");
        } else {
            ImGui::Text("Delete activity %d?", m_confirmDeleteIdx + 1);
        }
        ImGui::Separator();
        if (ImGui::Button("Yes##cdel", ImVec2(80, 0))) {
            if (m_confirmDeleteBatch) {
                int total = (int)wf.activities.size();
                for (int i = total - 1; i >= 0; --i)
                    if (i < (int)m_selection.size() && m_selection[i])
                        wf.activities.erase(wf.activities.begin() + i);
                m_selection.clear();
                m_editIdx = -1;
            } else if (m_confirmDeleteIdx >= 0 &&
                       m_confirmDeleteIdx < (int)wf.activities.size()) {
                wf.activities.erase(wf.activities.begin() + m_confirmDeleteIdx);
                m_selection.clear();
                m_editIdx = -1;
            }
            m_confirmDeleteIdx   = -1;
            m_confirmDeleteBatch = false;
            if (OnChanged) OnChanged();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No##cdel", ImVec2(80, 0))) {
            m_confirmDeleteIdx   = -1;
            m_confirmDeleteBatch = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Pick overlay (rendered outside modal when active)
    if (m_pickStage != PickStage::None)
        RenderPickOverlay();

    if (m_openModal) {
        ImGui::OpenPopup("##actmodal");
        m_openModal = false;
    }
    RenderModal(wf);
}

// ── Coordinate + color picker overlay ────────────────────────────────────────
void ActivityEditorWidget::RenderPickOverlay() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 pos = io.MousePos;
    pos.x += 16.f; pos.y += 16.f;
    if (pos.x + 240 > io.DisplaySize.x) pos.x = io.DisplaySize.x - 240;
    if (pos.y + 100 > io.DisplaySize.y) pos.y = io.DisplaySize.y - 100;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##pickoverlay", nullptr, flags)) {
        int gx = 0, gy = 0;
        SDL_GetGlobalMouseState(&gx, &gy);

        const char* lbl = (m_pickStage == PickStage::DragTo) ? "Pick end pos" : "Pick pos";
        ImGui::TextColored(ImVec4(1,0.9f,0.3f,1), "%s: %d, %d", lbl, gx, gy);

        bool isPixelPick = std::holds_alternative<PixelCheckActivity>(m_draft.data);
        if (isPixelPick) {
#if defined(_WIN32)
            HDC dc = GetDC(nullptr);
            if (dc) {
                COLORREF c = GetPixel(dc, gx, gy);
                ReleaseDC(nullptr, dc);
                if (c != CLR_INVALID) {
                    float r = GetRValue(c)/255.f, g2 = GetGValue(c)/255.f, b = GetBValue(c)/255.f;
                    ImGui::ColorButton("##pcol", ImVec4(r, g2, b, 1.f),
                        ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip,
                        ImVec2(14, 14));
                    ImGui::SameLine();
                    ImGui::Text("#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
                }
            }
#endif
        }

        ImGui::TextDisabled("Enter = confirm   Esc = cancel");

        if (ImGui::Button("Capture##pick") || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            ApplyPickedCoords(gx, gy);
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
        } else if constexpr (std::is_same_v<T,PixelCheckActivity>) {
            v.x = x; v.y = y;
#if defined(_WIN32)
            HDC dc = GetDC(nullptr);
            if (dc) {
                COLORREF c = GetPixel(dc, x, y);
                ReleaseDC(nullptr, dc);
                if (c != CLR_INVALID) {
                    v.color_rgb = ((uint32_t)GetRValue(c) << 16) |
                                  ((uint32_t)GetGValue(c) << 8)  |
                                   (uint32_t)GetBValue(c);
                }
            }
#endif
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

void ActivityEditorWidget::RenderModal(Workflow& wf) {
    ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_Always);
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
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Type of action to perform");
    ImGui::Checkbox("Enabled", &m_draft.enabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Uncheck to skip this activity without deleting it");
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
            ImGui::SetNextItemWidth(140);
            if (ImGui::Combo("Position mode", &idx, PosModeNames, 2))
                pm = idx == 1 ? PositionMode::Relative : PositionMode::Absolute;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Absolute = global screen coordinates\n"
                                  "Relative = offset from the target window's origin");
        };
        auto btnCombo = [](MouseButton& b) {
            int idx = (b == MouseButton::Right) ? 1 : (b == MouseButton::Middle) ? 2 : 0;
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("Button", &idx, BtnNames, 3))
                b = (MouseButton)idx;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Which mouse button to click");
        };
        auto delayFields = [](int& dm, int& dr) {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Delay after (ms)", &dm);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Wait this many ms before executing the next activity");
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Random range (ms)", &dr);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Add a random delay of 0 to N ms (makes timing less predictable)");
            dm = std::max(0, dm); dr = std::max(0, dr);
        };
        auto xyPick = [this](int& x, int& y, PickStage stage) {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("X", &x);
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Y", &y);
            if (ImGui::Button("Pick position##xy")) {
                m_pickStage = stage;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click, then hover to pick a screen position");
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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Send a double-click instead of a single click");
            delayFields(v.delay_ms, v.delay_rand_ms);

        } else if constexpr (std::is_same_v<T,MouseDragActivity>) {
            posMode(v.pos_mode);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("From X", &v.from_x);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("From Y", &v.from_y);
            if (ImGui::Button("Pick start##dfs")) {
                m_pickStage = PickStage::DragFrom;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick the drag start position");
            ImGui::SetNextItemWidth(120); ImGui::InputInt("To X", &v.to_x);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("To Y", &v.to_y);
            if (ImGui::Button("Pick end##dfe")) {
                m_pickStage = PickStage::DragTo;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick the drag end position");
            btnCombo(v.button);
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Duration (ms)", &v.duration_ms);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How long the drag motion takes");
            v.duration_ms = std::max(1, v.duration_ms);
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Delay after (ms)", &v.delay_ms);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Wait this many ms before the next activity");

        } else if constexpr (std::is_same_v<T,MouseScrollActivity>) {
            posMode(v.pos_mode);
            xyPick(v.x, v.y, PickStage::Single);
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Delta X", &v.delta_x);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Horizontal scroll amount");

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
                ImGui::SetNextItemWidth(120);
                ImGui::InputInt("Delta Y", &v.delta_y);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Vertical scroll amount (positive = up, negative = down)");
                if (ImGui::Button("Capture scroll##sc")) {
                    m_scrollCapture = true;
                    m_scrollAccum   = 0.f;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Scroll your mouse wheel to capture the delta");
            }
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Delay after (ms)", &v.delay_ms);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Wait this many ms before the next activity");

        } else if constexpr (std::is_same_v<T,KeyPressActivity>) {
            if (m_keyCaptureActive) {
                ImGui::TextColored(ImVec4(1,0.9f,0.3f,1), "Press any key... (Esc to cancel)");
                for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                    ImGuiKey key = (ImGuiKey)k;
                    if (!ImGui::IsKeyPressed(key, false)) continue;
                    if (key == ImGuiKey_Escape) { m_keyCaptureActive = false; break; }
                    if (key == ImGuiKey_LeftCtrl  || key == ImGuiKey_RightCtrl  ||
                        key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
                        key == ImGuiKey_LeftAlt   || key == ImGuiKey_RightAlt   ||
                        key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper) continue;
                    auto name = ImGuiKeyToKeyName(key);
                    if (!name.empty()) {
                        v.key = name;
                        v.modifiers.clear();
                        ImGuiIO& kio = ImGui::GetIO();
                        if (kio.KeyCtrl)  v.modifiers.push_back("ctrl");
                        if (kio.KeyShift) v.modifiers.push_back("shift");
                        if (kio.KeyAlt)   v.modifiers.push_back("alt");
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
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Click then press a key (hold Ctrl/Shift/Alt for modifiers)");
                ImGui::Text("Modifiers:");
                ImGui::SameLine();
                ImGui::TextDisabled("%s", v.modifiers.empty() ? "(none)" : [&]{
                    std::string s;
                    for (auto& m : v.modifiers) s += m + " ";
                    return s;
                }().c_str());
            }
            delayFields(v.delay_ms, v.delay_rand_ms);

        } else if constexpr (std::is_same_v<T,TypeStringActivity>) {
            static char textBuf[512]{};
            strncpy(textBuf, v.text.c_str(), sizeof(textBuf)-1);
            if (ImGui::InputTextMultiline("Text", textBuf, sizeof(textBuf), ImVec2(0,80)))
                v.text = textBuf;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Text to type character by character");
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Char delay (ms)", &v.delay_between_chars_ms);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Delay between each typed character");
            v.delay_between_chars_ms = std::max(0, v.delay_between_chars_ms);
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Delay after (ms)", &v.delay_ms);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Wait this many ms before the next activity");

        } else if constexpr (std::is_same_v<T,WaitActivity>) {
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Duration (ms)", &v.duration_ms);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How long to wait");
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Random range (ms)", &v.random_range_ms);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Add a random extra delay of 0 to N ms");
            v.duration_ms     = std::max(0, v.duration_ms);
            v.random_range_ms = std::max(0, v.random_range_ms);

        } else if constexpr (std::is_same_v<T,PixelCheckActivity>) {
            posMode(v.pos_mode);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("X", &v.x);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Y", &v.y);
            if (ImGui::Button("Pick position##pcx")) {
                m_pickStage = PickStage::Single;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to hover over screen; also captures the pixel color");

            float col[3] = {
                ((v.color_rgb>>16)&0xFF)/255.f,
                ((v.color_rgb>> 8)&0xFF)/255.f,
                ( v.color_rgb     &0xFF)/255.f
            };
            ImGui::SetNextItemWidth(200);
            if (ImGui::ColorEdit3("Color", col)) {
                v.color_rgb = ((uint32_t)(col[0]*255)<<16) |
                              ((uint32_t)(col[1]*255)<< 8) |
                               (uint32_t)(col[2]*255);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Target pixel color to match");
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Tolerance", &v.tolerance);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Allowed color difference per channel (0 = exact, 255 = any)");
            v.tolerance = std::max(0, std::min(255, v.tolerance));

            int actionIdx = (v.on_no_match == PixelCheckAction::SkipIteration) ? 1
                          : (v.on_no_match == PixelCheckAction::StopWorkflow)   ? 2 : 0;
            ImGui::SetNextItemWidth(160);
            if (ImGui::Combo("On no match", &actionIdx, PixelActionNames, 3))
                v.on_no_match = (PixelCheckAction)actionIdx;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("What to do if the pixel color does not match");
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Retry interval (ms)", &v.retry_interval_ms);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How often to re-check the pixel when retrying");
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Retry timeout (ms, 0=inf)", &v.retry_timeout_ms);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Stop retrying after this many ms (0 = retry forever)");
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Delay after (ms)", &v.delay_ms);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Wait this many ms before the next activity");

        } else if constexpr (std::is_same_v<T,RunWorkflowActivity>) {
            if (m_workflows && !m_workflows->empty()) {
                ImGui::TextDisabled("Filter:"); ImGui::SameLine();
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##wffilter", m_wfFilterBuf, sizeof(m_wfFilterBuf));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Type to filter workflows by name");

                ImGui::BeginChild("##wflist_rw", ImVec2(0, 110), true);
                std::string filterLow(m_wfFilterBuf);
                for (auto& c : filterLow) c = (char)std::tolower((unsigned char)c);

                for (auto& wf2 : *m_workflows) {
                    std::string nameLow = wf2.name;
                    for (auto& c : nameLow) c = (char)std::tolower((unsigned char)c);
                    if (!filterLow.empty() && nameLow.find(filterLow) == std::string::npos)
                        continue;
                    bool selected = (v.workflow_id == wf2.id);
                    char lbl[256];
                    snprintf(lbl, sizeof(lbl), "%s##rwf%s", wf2.name.c_str(), wf2.id.c_str());
                    if (ImGui::Selectable(lbl, selected))
                        v.workflow_id = wf2.id;
                }
                ImGui::EndChild();
                ImGui::TextDisabled("ID: %.24s", v.workflow_id.c_str());
            } else {
                static char idBuf[64]{};
                strncpy(idBuf, v.workflow_id.c_str(), sizeof(idBuf)-1);
                if (ImGui::InputText("Workflow ID", idBuf, sizeof(idBuf)))
                    v.workflow_id = idBuf;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("ID of the workflow to run as a sub-workflow");
            }
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Delay after (ms)", &v.delay_ms);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Wait this many ms before the next activity");
        }
    }, data);
}

void ActivityEditorWidget::RenderMoveButtons(Workflow& /*wf*/, int /*idx*/) {}
