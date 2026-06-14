#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
#include "activity_editor.hpp"
#include "window/pixel_checker.hpp"
#include "imgui.h"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <random>
#include <iomanip>
#include <cmath>

// ── Destructor ────────────────────────────────────────────────────────────────
ActivityEditorWidget::~ActivityEditorWidget() {
    if (m_samplePreviewTex) { SDL_DestroyTexture(m_samplePreviewTex); m_samplePreviewTex = nullptr; }
    if (m_crosshairCursor)  { SDL_FreeCursor(m_crosshairCursor);      m_crosshairCursor = nullptr; }
}

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

// ── Activity type metadata ────────────────────────────────────────────────────
// kTypes is used for the Combo in the modal.
// Index 7 (pixel_check) is excluded; indices 8-15 are added.
static const char* kTypes[] = {
    "mouse_move","mouse_click","mouse_drag","mouse_scroll",
    "key_press","type_string","wait",
    // 7 = pixel_check (hidden — not in this list)
    "pixel_range_check",     // displayed index 7 → variant index 8
    "run_workflow","system_action",
    "run_activity","set_variable","loop","if","switch","jump"
};
// Map displayed combo index → variant index
static const int kTypeToVariantIdx[] = {
    0,1,2,3,4,5,6, 8, 9,10, 11,12,13,14,15,16
};
static const int kNumTypes = (int)(sizeof(kTypes)/sizeof(kTypes[0]));

// Map variant index → display combo index (-1 if hidden)
static int VariantToDisplayIdx(const ActivityData& d) {
    if (std::holds_alternative<MouseMoveActivity>(d))        return 0;
    if (std::holds_alternative<MouseClickActivity>(d))       return 1;
    if (std::holds_alternative<MouseDragActivity>(d))        return 2;
    if (std::holds_alternative<MouseScrollActivity>(d))      return 3;
    if (std::holds_alternative<KeyPressActivity>(d))         return 4;
    if (std::holds_alternative<TypeStringActivity>(d))       return 5;
    if (std::holds_alternative<WaitActivity>(d))             return 6;
    if (std::holds_alternative<PixelCheckActivity>(d))       return -1; // hidden
    if (std::holds_alternative<PixelRangeCheckActivity>(d))  return 7;
    if (std::holds_alternative<RunWorkflowActivity>(d))      return 8;
    if (std::holds_alternative<SystemActionActivity>(d))     return 9;
    if (std::holds_alternative<RunActivityActivity>(d))      return 10;
    if (std::holds_alternative<SetVariableActivity>(d))      return 11;
    if (std::holds_alternative<LoopActivity>(d))             return 12;
    if (std::holds_alternative<IfActivity>(d))               return 13;
    if (std::holds_alternative<SwitchActivity>(d))           return 14;
    if (std::holds_alternative<JumpActivity>(d))             return 15;
    return -1;
}

static ActivityData DefaultData(int displayIdx) {
    int vi = kTypeToVariantIdx[displayIdx];
    switch (vi) {
        case 0:  return MouseMoveActivity{};
        case 1:  return MouseClickActivity{};
        case 2:  return MouseDragActivity{};
        case 3:  return MouseScrollActivity{};
        case 4:  return KeyPressActivity{};
        case 5:  return TypeStringActivity{};
        case 6:  return WaitActivity{};
        case 8: { PixelRangeCheckActivity v;
                  v.match_body    = std::make_shared<std::vector<Activity>>();
                  v.no_match_body = std::make_shared<std::vector<Activity>>();
                  return v; }
        case 9:  return RunWorkflowActivity{};
        case 10: return SystemActionActivity{};
        case 11: return RunActivityActivity{};
        case 12: return SetVariableActivity{};
        case 13: { LoopActivity v; v.body = std::make_shared<std::vector<Activity>>(); return v; }
        case 14: { IfActivity v;
                   v.then_body = std::make_shared<std::vector<Activity>>();
                   v.else_body = std::make_shared<std::vector<Activity>>();
                   return v; }
        case 15: { SwitchActivity v;
                   v.default_body = std::make_shared<std::vector<Activity>>();
                   return v; }
        case 16: return JumpActivity{};
        default: return WaitActivity{};
    }
}

// Shared pixel checker
static IPixelChecker* EditorPixelChecker() {
    static std::unique_ptr<IPixelChecker> s_checker = CreatePixelChecker();
    return s_checker.get();
}

static const char* BtnNames[]     = {"left","right","middle"};
static const char* PosModeNames[] = {"absolute","relative"};
static const char* VarOpNames[]   = {"set","increment","decrement","random"};
static const char* CondOpNames[]  = {"eq","neq","gt","lt","gteq","lteq","contains"};

// ── Block-type helpers ────────────────────────────────────────────────────────

bool ActivityEditorWidget::IsBlockType(const ActivityData& d) {
    return std::holds_alternative<LoopActivity>(d)             ||
           std::holds_alternative<IfActivity>(d)               ||
           std::holds_alternative<SwitchActivity>(d)           ||
           std::holds_alternative<PixelRangeCheckActivity>(d);
}

// Returns ImGui color (packed ABGR) for graph column lines
uint32_t ActivityEditorWidget::BlockColor(const ActivityData& d) {
    if (std::holds_alternative<LoopActivity>(d))            return IM_COL32(0x4a, 0x9e, 0xff, 0xff);
    if (std::holds_alternative<IfActivity>(d))              return IM_COL32(0x4a, 0xff, 0x7a, 0xff);
    if (std::holds_alternative<SwitchActivity>(d))          return IM_COL32(0xff, 0xaa, 0x4a, 0xff);
    if (std::holds_alternative<PixelRangeCheckActivity>(d)) return IM_COL32(0xff, 0x55, 0xaa, 0xff);
    return IM_COL32(0x55, 0x55, 0x55, 0xff);
}

std::string ActivityEditorWidget::BlockName(const Activity& a) {
    return std::visit([](auto&& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T,LoopActivity>            ||
                      std::is_same_v<T,IfActivity>              ||
                      std::is_same_v<T,SwitchActivity>          ||
                      std::is_same_v<T,PixelRangeCheckActivity>)
            return v.name;
        return {};
    }, a.data);
}

// Returns the primary (first/only) body list of a block-type activity.
// Loop → body; If → then_body; Switch → default_body; PixelRange → match_body.
std::vector<Activity>* ActivityEditorWidget::GetPrimaryBody(Activity& a) {
    return std::visit([](auto&& v) -> std::vector<Activity>* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, LoopActivity>)
            return v.body ? v.body.get() : nullptr;
        else if constexpr (std::is_same_v<T, IfActivity>)
            return v.then_body ? v.then_body.get() : nullptr;
        else if constexpr (std::is_same_v<T, SwitchActivity>)
            return v.default_body ? v.default_body.get() : nullptr;
        else if constexpr (std::is_same_v<T, PixelRangeCheckActivity>)
            return v.match_body ? v.match_body.get() : nullptr;
        else return nullptr;
    }, a.data);
}

// Auto-assigns a block name if empty (e.g. "Loop 2")
static void AutoNameBlock(Activity& a, const std::vector<Activity>& allActs,
                          const std::string& prefix, int variantIdx) {
    // count existing blocks of the same type
    int n = 0;
    std::function<void(const std::vector<Activity>&)> count;
    count = [&](const std::vector<Activity>& acts) {
        for (auto& act : acts) {
            if ((int)act.data.index() == variantIdx) ++n;
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T,LoopActivity>)
                    if (v.body) count(*v.body);
                if constexpr (std::is_same_v<T,IfActivity>) {
                    if (v.then_body) count(*v.then_body);
                    if (v.else_body) count(*v.else_body);
                }
                if constexpr (std::is_same_v<T,SwitchActivity>) {
                    for (auto& sc : v.cases) if (sc.body) count(*sc.body);
                    if (v.default_body) count(*v.default_body);
                }
                if constexpr (std::is_same_v<T,PixelRangeCheckActivity>) {
                    if (v.match_body)    count(*v.match_body);
                    if (v.no_match_body) count(*v.no_match_body);
                }
            }, act.data);
        }
    };
    count(allActs);

    // Set name on the new activity
    std::string newName = prefix + " " + std::to_string(n + 1);
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T,LoopActivity>            ||
                      std::is_same_v<T,IfActivity>              ||
                      std::is_same_v<T,SwitchActivity>          ||
                      std::is_same_v<T,PixelRangeCheckActivity>)
            if (v.name.empty()) v.name = newName;
    }, a.data);
}

// ── Short summary for list row ────────────────────────────────────────────────
static std::string ActivitySummary(const Activity& a) {
    return std::visit([&a](auto&& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        char buf[160]{};
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
        else if constexpr (std::is_same_v<T,PixelRangeCheckActivity>) {
            int nBody = (v.match_body ? (int)v.match_body->size() : 0)
                      + (v.no_match_body ? (int)v.no_match_body->size() : 0);
            snprintf(buf,sizeof(buf),"%s  (%d,%d)-(%d,%d) %s %d%% [%d]",
                v.name.empty() ? "pixel_range" : v.name.c_str(),
                v.x1,v.y1,v.x2,v.y2,
                v.sample.empty()?"no sample":"sampled",v.match_percent,nBody);
        } else if constexpr (std::is_same_v<T,RunWorkflowActivity>)
            snprintf(buf,sizeof(buf),"run_workflow %s",v.workflow_id.c_str());
        else if constexpr (std::is_same_v<T,SystemActionActivity>) {
            static const char* names[] = {"shutdown","restart","sleep","hibernate","lock","logout"};
            int idx = (int)v.action;
            snprintf(buf,sizeof(buf),"system_action %s%s",
                (idx>=0&&idx<6)?names[idx]:"?",v.force?" (force)":"");
        } else if constexpr (std::is_same_v<T,RunActivityActivity>)
            snprintf(buf,sizeof(buf),"run_activity -> %.24s",v.activity_id.c_str());
        else if constexpr (std::is_same_v<T,SetVariableActivity>)
            snprintf(buf,sizeof(buf),"set %s %s %s",
                v.name.c_str(),
                v.op==VarOp::Set?"=":v.op==VarOp::Increment?"+=":
                v.op==VarOp::Decrement?"-=":"rand",
                v.value.c_str());
        else if constexpr (std::is_same_v<T,LoopActivity>) {
            int n = v.body ? (int)v.body->size() : 0;
            snprintf(buf,sizeof(buf),"%s  x%d  [%d steps]",
                v.name.empty()?"loop":v.name.c_str(), v.count, n);
        } else if constexpr (std::is_same_v<T,IfActivity>) {
            snprintf(buf,sizeof(buf),"%s  if %s %s %s",
                v.name.empty()?"if":v.name.c_str(),
                v.cond.lhs.c_str(),
                v.cond.op==ConditionOp::Eq?"==":v.cond.op==ConditionOp::NEq?"!=":
                v.cond.op==ConditionOp::Gt?">":v.cond.op==ConditionOp::Lt?"<":
                v.cond.op==ConditionOp::GtEq?">=":v.cond.op==ConditionOp::LtEq?"<=":"contains",
                v.cond.rhs.c_str());
        } else if constexpr (std::is_same_v<T,SwitchActivity>) {
            snprintf(buf,sizeof(buf),"%s  switch %s  [%d cases]",
                v.name.empty()?"switch":v.name.c_str(),
                v.var_name.c_str(),(int)v.cases.size());
        } else if constexpr (std::is_same_v<T,JumpActivity>) {
            snprintf(buf,sizeof(buf),"jump -> %.32s",
                v.target_id.empty() ? "(none)" : v.target_id.substr(0,24).c_str());
        }
        return buf;
    }, a.data);
}

// ── SDL fullscreen overlay helpers ────────────────────────────────────────────

void ActivityEditorWidget::EnterFullscreenMode() {
    if (!m_sdlWindow || m_origWindowW > 0) return; // already fullscreen or no window
    SDL_GetWindowPosition(m_sdlWindow, &m_origWindowX, &m_origWindowY);
    SDL_GetWindowSize(m_sdlWindow, &m_origWindowW, &m_origWindowH);
    SDL_DisplayMode dm{};
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        SDL_SetWindowBordered(m_sdlWindow, SDL_FALSE);
        SDL_SetWindowAlwaysOnTop(m_sdlWindow, SDL_TRUE);
        SDL_SetWindowPosition(m_sdlWindow, 0, 0);
        SDL_SetWindowSize(m_sdlWindow, dm.w, dm.h);
    }
    if (m_pOverlayOpacity)
        SDL_SetWindowOpacity(m_sdlWindow, *m_pOverlayOpacity);
    m_origCursor = SDL_GetCursor();
    if (!m_crosshairCursor)
        m_crosshairCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    SDL_SetCursor(m_crosshairCursor);
}

void ActivityEditorWidget::ExitFullscreenMode() {
    if (!m_sdlWindow || m_origWindowW <= 0) return;
    SDL_SetWindowOpacity(m_sdlWindow, 1.0f);
    SDL_SetWindowBordered(m_sdlWindow, SDL_TRUE);
    SDL_SetWindowAlwaysOnTop(m_sdlWindow, SDL_FALSE);
    SDL_SetWindowPosition(m_sdlWindow, m_origWindowX, m_origWindowY);
    SDL_SetWindowSize(m_sdlWindow, m_origWindowW, m_origWindowH);
    m_origWindowW = 0;
    if (m_origCursor) { SDL_SetCursor(m_origCursor); m_origCursor = nullptr; }
}

// ── FlatNode pre-pass ─────────────────────────────────────────────────────────

void ActivityEditorWidget::CollectFlatNodes(std::vector<Activity>& list,
                                            int depth,
                                            std::vector<bool> ancestorCont) {
    for (int i = 0; i < (int)list.size(); ++i) {
        auto& a = list[i];
        bool hasMoreSiblings = (i < (int)list.size() - 1);
        bool isBlock = IsBlockType(a.data);
        bool isExpanded = isBlock && m_expandedIds.count(a.id);

        FlatNode node;
        node.act           = &a;
        node.parentList    = &list;
        node.indexInParent = i;
        node.depth         = depth;
        node.kind          = isBlock ? FlatNode::Kind::BlockHeader : FlatNode::Kind::Normal;
        node.isExpanded    = isExpanded;
        node.ancestorContinues = ancestorCont;
        node.blockId       = a.id;
        node.blockName     = BlockName(a);
        m_flatNodes.push_back(node);

        if (isBlock && isExpanded) {
            std::vector<bool> childCont = ancestorCont;
            childCont.push_back(hasMoreSiblings);

            // Visit children according to block type
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T,LoopActivity>) {
                    if (v.body)
                        CollectFlatNodes(*v.body, depth + 1, childCont);
                } else if constexpr (std::is_same_v<T,IfActivity>) {
                    if (v.then_body && !v.then_body->empty())
                        CollectFlatNodes(*v.then_body, depth + 1, childCont);
                    // "else" separator — always shown so user can add to else_body
                    {
                        FlatNode sep;
                        sep.depth = depth;
                        sep.kind  = FlatNode::Kind::SectionSeparator;
                        sep.ancestorContinues = ancestorCont;
                        sep.blockId   = a.id;
                        sep.blockName = BlockName(a);
                        sep.sectionLabel = "else";
                        sep.sectionBody  = v.else_body ? v.else_body.get() : nullptr;
                        m_flatNodes.push_back(sep);
                    }
                    if (v.else_body && !v.else_body->empty())
                        CollectFlatNodes(*v.else_body, depth + 1, childCont);
                } else if constexpr (std::is_same_v<T,SwitchActivity>) {
                    for (auto& sc : v.cases) {
                        {
                            FlatNode sep;
                            sep.depth = depth;
                            sep.kind  = FlatNode::Kind::SectionSeparator;
                            sep.ancestorContinues = ancestorCont;
                            sep.blockId   = a.id;
                            sep.blockName = BlockName(a);
                            sep.sectionLabel = sc.value.empty()
                                ? "case (empty):"
                                : ("case \"" + sc.value + "\":");
                            sep.sectionBody = sc.body ? sc.body.get() : nullptr;
                            m_flatNodes.push_back(sep);
                        }
                        if (sc.body && !sc.body->empty())
                            CollectFlatNodes(*sc.body, depth + 1, childCont);
                    }
                    {
                        FlatNode sep;
                        sep.depth = depth;
                        sep.kind  = FlatNode::Kind::SectionSeparator;
                        sep.ancestorContinues = ancestorCont;
                        sep.blockId   = a.id;
                        sep.blockName = BlockName(a);
                        sep.sectionLabel = "default:";
                        sep.sectionBody  = v.default_body ? v.default_body.get() : nullptr;
                        m_flatNodes.push_back(sep);
                    }
                    if (v.default_body && !v.default_body->empty())
                        CollectFlatNodes(*v.default_body, depth + 1, childCont);
                } else if constexpr (std::is_same_v<T,PixelRangeCheckActivity>) {
                    if (v.match_body && !v.match_body->empty())
                        CollectFlatNodes(*v.match_body, depth + 1, childCont);
                    if (v.no_match_body && !v.no_match_body->empty())
                        CollectFlatNodes(*v.no_match_body, depth + 1, childCont);
                }
            }, a.data);

            // BlockEnd virtual row — at the same depth as the header
            FlatNode endNode;
            endNode.act           = nullptr;
            endNode.parentList    = &list;
            endNode.indexInParent = i;
            endNode.depth         = depth;
            endNode.kind          = FlatNode::Kind::BlockEnd;
            endNode.isExpanded    = false;
            endNode.ancestorContinues = ancestorCont;
            endNode.blockId       = a.id;
            endNode.blockName     = node.blockName;
            m_flatNodes.push_back(endNode);
        }
    }
}

void ActivityEditorWidget::RebuildFlatNodes(Workflow& wf) {
    m_flatNodes.clear();
    CollectFlatNodes(wf.activities, 0, {});
}

// ── Snip state machine (called by AppUI every frame, even when main UI is hidden) ─

void ActivityEditorWidget::RunSnipStateMachine(Workflow& wf) {
    if (m_snipStage == SnipStage::WaitMinimize) {
        // Window was hidden last frame; wait one more frame for OS compositor
        m_snipStage = SnipStage::WaitFrame;
        return;
    }

    if (m_snipStage == SnipStage::WaitFrame) {
        // Capture screenshot pixels — needed for pixel extraction on mouse release.
        // We no longer display it as a texture; SDL_SetWindowOpacity provides the
        // transparent overlay so the user sees the live desktop instead.
        IPixelChecker* checker = EditorPixelChecker();
        if (checker)
            m_snipPixels = checker->CaptureFullScreen(m_snipW, m_snipH);

        // Re-show window as borderless full-screen transparent overlay
        if (m_sdlWindow && m_origWindowW > 0) {
            SDL_DisplayMode dm{};
            if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
                SDL_SetWindowBordered(m_sdlWindow, SDL_FALSE);
                SDL_SetWindowAlwaysOnTop(m_sdlWindow, SDL_TRUE);
                SDL_SetWindowPosition(m_sdlWindow, 0, 0);
                SDL_SetWindowSize(m_sdlWindow, dm.w, dm.h);
                SDL_ShowWindow(m_sdlWindow);
                SDL_RaiseWindow(m_sdlWindow);
            }
            if (m_pOverlayOpacity)
                SDL_SetWindowOpacity(m_sdlWindow, *m_pOverlayOpacity);
            m_origCursor = SDL_GetCursor();
            if (!m_crosshairCursor)
                m_crosshairCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
            SDL_SetCursor(m_crosshairCursor);
        }
        m_snipStage    = SnipStage::Active;
        m_snipDragging = false;
        m_snipX1 = m_snipY1 = m_snipX2 = m_snipY2 = 0;
    }

    if (m_snipStage == SnipStage::Active) {
        RenderSnipOverlay(wf);
        return;
    }

    if (m_snipStage == SnipStage::Done) {
        m_snipPixels.clear();
        m_snipStage = SnipStage::None;
        ExitFullscreenMode();
        m_openModal = true;
    }
}

void ActivityEditorWidget::RenderPickOverlayIfActive() {
    if (m_pickStage != PickStage::None)
        RenderPickOverlay();
}

// ── Main Render ───────────────────────────────────────────────────────────────

void ActivityEditorWidget::Render(Workflow& wf, int currentStep) {
    ImGuiIO& io = ImGui::GetIO();

    // Rebuild FlatNode list
    RebuildFlatNodes(wf);

    // ── Header: count + Add button ────────────────────────────────────────────
    ImGui::Text("Activities (%d)", (int)wf.activities.size());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sequence of actions executed in order");
    ImGui::SameLine();
    if (ImGui::Button("+ Add##act")) {
        Activity a{GenId(), true, MouseClickActivity{}};
        m_draft            = a;
        m_editIdx          = -1;
        m_openModal        = true;
        m_keyCaptureActive = false;
        m_scrollCapture    = false;
        m_pickStage        = PickStage::None;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a new activity to the workflow root");

    // ── Batch ops bar ─────────────────────────────────────────────────────────
    {
        int selCount = (int)m_selectedIds.size();

        if (ImGui::SmallButton("All##sa")) {
            m_selectedIds.clear();
            for (auto& fn : m_flatNodes)
                if (fn.act && fn.kind != FlatNode::Kind::BlockEnd)
                    m_selectedIds.insert(fn.act->id);
            m_lastClickedId.clear();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select all");
        ImGui::SameLine();
        if (ImGui::SmallButton("None##sa")) {
            m_selectedIds.clear(); m_lastClickedId.clear();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Deselect all");

        if (selCount > 0) {
            // Find first selected node for move operations
            FlatNode* firstSel = nullptr;
            for (auto& fn : m_flatNodes)
                if (fn.act && m_selectedIds.count(fn.act->id)) { firstSel = &fn; break; }

            if (selCount == 1 && firstSel) {
                auto* pl = firstSel->parentList;
                int idx  = firstSel->indexInParent;
                ImGui::SameLine();
                ImGui::BeginDisabled(idx == 0);
                if (ImGui::SmallButton("^##bmv")) {
                    std::swap((*pl)[idx], (*pl)[idx-1]);
                    if (OnChanged) OnChanged();
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move up");
                ImGui::SameLine();
                ImGui::BeginDisabled(idx >= (int)pl->size()-1);
                if (ImGui::SmallButton("v##bmv")) {
                    std::swap((*pl)[idx], (*pl)[idx+1]);
                    if (OnChanged) OnChanged();
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move down");
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Dup##bsel")) {
                // Duplicate selected root-level activities
                std::vector<int> selIdxs;
                for (int i = 0; i < (int)wf.activities.size(); ++i)
                    if (m_selectedIds.count(wf.activities[i].id))
                        selIdxs.push_back(i);
                if (!selIdxs.empty()) {
                    int insertAt = selIdxs.back() + 1;
                    for (int i = (int)selIdxs.size()-1; i >= 0; --i) {
                        Activity copy = wf.activities[selIdxs[i]];
                        copy.id = GenId();
                        wf.activities.insert(wf.activities.begin() + insertAt, std::move(copy));
                    }
                    if (OnChanged) OnChanged();
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duplicate selected");

            ImGui::SameLine();
            if (ImGui::SmallButton("Del##bsel")) {
                m_confirmDeleteId    = "";
                m_confirmDeleteBatch = true;
                m_pendingConfirmOpen = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete selected");

            ImGui::SameLine();
            if (ImGui::SmallButton("On##bsel")) {
                for (auto& fn : m_flatNodes)
                    if (fn.act && m_selectedIds.count(fn.act->id))
                        fn.act->enabled = true;
                if (OnChanged) OnChanged();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Off##bsel")) {
                for (auto& fn : m_flatNodes)
                    if (fn.act && m_selectedIds.count(fn.act->id))
                        fn.act->enabled = false;
                if (OnChanged) OnChanged();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%d sel)", selCount);
        }
    }

    ImGui::BeginChild("##actlist", ImVec2(0, -60), true);

    const float kGraphColW = 12.f; // pixels per depth level in graph column
    const float kToggleW   = 16.f; // width for expand/collapse button

    auto* dl = ImGui::GetWindowDrawList();

    int visibleIdx = 0; // selectable row index (BlockEnd skipped)
    for (int ni = 0; ni < (int)m_flatNodes.size(); ++ni) {
        auto& fn = m_flatNodes[ni];
        ImGui::PushID(ni);

        float indentPx = fn.depth * kGraphColW;

        // ── Draw graph column lines ───────────────────────────────────────────
        ImVec2 rowMin = ImGui::GetCursorScreenPos();
        float rowH    = ImGui::GetTextLineHeightWithSpacing();
        float rowMidY = rowMin.y + rowH * 0.5f;

        uint32_t graphCol = IM_COL32(0x55,0x55,0x55,0xff);
        if (fn.act) graphCol = BlockColor(fn.act->data);

        // Ancestor vertical lines
        for (int d = 0; d < fn.depth; ++d) {
            float lx = rowMin.x + d * kGraphColW + kGraphColW * 0.5f;
            bool cont = (d < (int)fn.ancestorContinues.size()) && fn.ancestorContinues[d];
            uint32_t lc = IM_COL32(0x55,0x55,0x55,0x88);
            if (cont)
                dl->AddLine(ImVec2(lx, rowMin.y), ImVec2(lx, rowMin.y + rowH), lc, 1.5f);
            else if (d == fn.depth - 1)
                dl->AddLine(ImVec2(lx, rowMin.y), ImVec2(lx, rowMidY), lc, 1.5f);
        }
        // Horizontal connector at own depth
        if (fn.depth > 0) {
            float hx0 = rowMin.x + (fn.depth-1) * kGraphColW + kGraphColW * 0.5f;
            float hx1 = rowMin.x + fn.depth * kGraphColW;
            dl->AddLine(ImVec2(hx0, rowMidY), ImVec2(hx1, rowMidY),
                        IM_COL32(0x55,0x55,0x55,0x88), 1.5f);
        }
        // Block header: draw downward half-bar below mid → connects to first child
        if (fn.kind == FlatNode::Kind::BlockHeader && fn.isExpanded) {
            float bx = rowMin.x + fn.depth * kGraphColW + kGraphColW * 0.5f;
            dl->AddLine(ImVec2(bx, rowMidY), ImVec2(bx, rowMin.y + rowH),
                        graphCol, 2.0f);
        }
        // BlockEnd: draw upward half-bar above mid → closes the block
        if (fn.kind == FlatNode::Kind::BlockEnd) {
            float bx = rowMin.x + fn.depth * kGraphColW + kGraphColW * 0.5f;
            dl->AddLine(ImVec2(bx, rowMin.y), ImVec2(bx, rowMidY),
                        graphCol, 2.0f);
        }

        // Indent content
        ImGui::Dummy(ImVec2(indentPx, 0));
        ImGui::SameLine(0, 0);

        // ── BlockEnd row ──────────────────────────────────────────────────────
        if (fn.kind == FlatNode::Kind::BlockEnd) {
            // Find the block's Activity* from flat nodes (linear, list is small)
            Activity* blockAct = nullptr;
            for (auto& fn2 : m_flatNodes)
                if (fn2.kind == FlatNode::Kind::BlockHeader && fn2.blockId == fn.blockId && fn2.act)
                    { blockAct = fn2.act; break; }

            std::string endLabel = "-- end " + (fn.blockName.empty() ? fn.blockId.substr(0,8) : fn.blockName) + " --";
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f,0.55f,0.55f,1.f));
            ImGui::TextUnformatted(endLabel.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked()) {
                m_expandedIds.erase(fn.blockId);
            }

            // Drag-drop target: drop onto BlockEnd → append to block's primary body
            if (blockAct && ImGui::BeginDragDropTarget()) {
                struct ActDragPayload { std::vector<Activity>* srcList; int srcIdx; };
                if (auto* p = ImGui::AcceptDragDropPayload("ACT_TREE")) {
                    auto& payload = *(const ActDragPayload*)p->Data;
                    auto* body = GetPrimaryBody(*blockAct);
                    if (body) {
                        Activity moved = (*payload.srcList)[payload.srcIdx];
                        payload.srcList->erase(payload.srcList->begin() + payload.srcIdx);
                        body->push_back(std::move(moved));
                        if (OnChanged) OnChanged();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // "[+]" button: add a new child activity to the block's primary body
            if (blockAct) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f,0.85f,0.4f,1.f));
                char addLbl[32]; snprintf(addLbl, sizeof(addLbl), "[+]##end%d", ni);
                if (ImGui::SmallButton(addLbl)) {
                    m_addTargetList    = GetPrimaryBody(*blockAct);
                    m_expandedIds.insert(blockAct->id);
                    m_draft            = Activity{GenId(), true, MouseClickActivity{}};
                    m_editIdx          = -1;
                    m_openModal        = true;
                    m_keyCaptureActive = false;
                    m_scrollCapture    = false;
                    m_pickStage        = PickStage::None;
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add child activity to this block");
            }

            ImGui::PopID();
            continue;
        }

        // ── SectionSeparator rows (else / case / default) ─────────────────────
        if (fn.kind == FlatNode::Kind::SectionSeparator) {
            std::string sepText = "-- " + fn.sectionLabel + " --";
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.35f, 1.f));
            ImGui::TextUnformatted(sepText.c_str());
            ImGui::PopStyleColor();
            if (fn.sectionBody != nullptr) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f,0.85f,0.4f,1.f));
                char addLbl[32]; snprintf(addLbl, sizeof(addLbl), "[+]##sep%d", ni);
                if (ImGui::SmallButton(addLbl)) {
                    m_addTargetList    = fn.sectionBody;
                    m_expandedIds.insert(fn.blockId);
                    m_draft            = Activity{GenId(), true, MouseClickActivity{}};
                    m_editIdx          = -1;
                    m_openModal        = true;
                    m_keyCaptureActive = false;
                    m_scrollCapture    = false;
                    m_pickStage        = PickStage::None;
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add child activity to this section");
            }
            ImGui::PopID();
            continue;
        }

        // ── Normal / BlockHeader rows ─────────────────────────────────────────
        auto& a = *fn.act;
        bool isCurrent  = (currentStep >= 0 && fn.parentList == &wf.activities &&
                           fn.indexInParent == currentStep);
        bool isSelected = m_selectedIds.count(a.id) > 0;

        if (isCurrent && fn.indexInParent != m_lastScrolledStep) {
            ImGui::SetScrollHereY(0.5f);
            m_lastScrolledStep = fn.indexInParent;
        }

        if (isCurrent)
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.1f,0.65f,0.1f,0.55f)),
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.1f,0.65f,0.1f,0.75f)),
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.1f,0.65f,0.1f,0.90f));
        if (!a.enabled && !isCurrent)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.5f,0.5f,1.f));

        // Expand/collapse toggle for block types
        if (fn.kind == FlatNode::Kind::BlockHeader) {
            const char* toggleLbl = fn.isExpanded ? "v##t" : ">##t";
            if (ImGui::SmallButton(toggleLbl)) {
                if (fn.isExpanded) m_expandedIds.erase(a.id);
                else               m_expandedIds.insert(a.id);
            }
            ImGui::SameLine(0, 2);
        } else {
            // Spacer to align with block headers
            ImGui::Dummy(ImVec2(kToggleW, 0));
            ImGui::SameLine(0, 2);
        }

        std::string summary;
        if (auto* ja = std::get_if<JumpActivity>(&a.data)) {
            if (ja->target_id.empty()) {
                summary = "jump -> (none)";
            } else {
                int tNum = 0; bool found = false;
                for (auto& fn2 : m_flatNodes) {
                    if (!fn2.act) continue;
                    ++tNum;
                    if (fn2.act->id == ja->target_id) {
                        std::string ts = ActivitySummary(*fn2.act);
                        if (ts.size() > 25) ts = ts.substr(0, 25) + "..";
                        char tmp[160];
                        snprintf(tmp, sizeof(tmp), "jump -> #%d %s", tNum, ts.c_str());
                        summary = tmp; found = true; break;
                    }
                }
                if (!found) summary = "jump -> (invalid)";
            }
        } else {
            summary = ActivitySummary(a);
        }
        char label[256];
        snprintf(label, sizeof(label), "%2d. %s##sel%d",
                 visibleIdx+1, summary.c_str(), ni);

        bool highlighted = isSelected || isCurrent;
        ImGuiSelectableFlags selFlags =
            ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick;
        if (ImGui::Selectable(label, highlighted, selFlags)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                m_draft            = a;
                m_editIdx          = fn.indexInParent;
                // Store reference to the parent list via a tag — we use indexInParent + parentList
                m_openModal        = true;
                m_keyCaptureActive = false;
                m_scrollCapture    = false;
                m_pickStage        = PickStage::None;
            } else {
                if (io.KeyShift && !m_lastClickedId.empty()) {
                    // Range select within visible nodes
                    bool inRange = false;
                    for (auto& fn2 : m_flatNodes) {
                        if (fn2.kind == FlatNode::Kind::BlockEnd || !fn2.act) continue;
                        if (fn2.act->id == m_lastClickedId || fn2.act->id == a.id)
                            inRange = !inRange, m_selectedIds.insert(fn2.act->id);
                        else if (inRange)
                            m_selectedIds.insert(fn2.act->id);
                    }
                    m_selectedIds.insert(a.id);
                } else if (io.KeyCtrl) {
                    if (m_selectedIds.count(a.id)) m_selectedIds.erase(a.id);
                    else m_selectedIds.insert(a.id);
                    m_lastClickedId = a.id;
                } else {
                    m_selectedIds.clear();
                    m_selectedIds.insert(a.id);
                    m_lastClickedId = a.id;
                }
            }
        }

        if (!a.enabled && !isCurrent) ImGui::PopStyleColor();
        if (isCurrent)               ImGui::PopStyleColor(3);

        // Drag-drop source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            // Payload: pointer to parentList and index
            struct ActDragPayload { std::vector<Activity>* srcList; int srcIdx; };
            ActDragPayload payload{fn.parentList, fn.indexInParent};
            ImGui::SetDragDropPayload("ACT_TREE", &payload, sizeof(payload));
            auto s = ActivitySummary(a);
            if (s.size() > 28) s = s.substr(0, 28) + "..";
            ImGui::Text("Move: %s", s.c_str());
            ImGui::EndDragDropSource();
        }
        // Drag-drop target (same list = reorder; different list = cross-body move)
        if (ImGui::BeginDragDropTarget()) {
            struct ActDragPayload { std::vector<Activity>* srcList; int srcIdx; };
            if (auto* p = ImGui::AcceptDragDropPayload("ACT_TREE")) {
                auto& payload = *(const ActDragPayload*)p->Data;
                auto* srcList = payload.srcList;
                int src = payload.srcIdx;

                if (fn.kind == FlatNode::Kind::BlockHeader) {
                    // Drop onto a block header → insert at front of its primary body
                    auto* body = GetPrimaryBody(a);
                    if (body && !(srcList == body && src == 0)) {
                        Activity moved = (*srcList)[src];
                        srcList->erase(srcList->begin() + src);
                        body->insert(body->begin(), std::move(moved));
                        m_expandedIds.insert(a.id);
                        if (OnChanged) OnChanged();
                    }
                } else {
                    // Drop onto a normal row → reorder within or across lists
                    auto* dstList = fn.parentList;
                    int dst = fn.indexInParent;
                    if (srcList == dstList) {
                        if (src != dst) {
                            Activity moved = (*srcList)[src];
                            srcList->erase(srcList->begin() + src);
                            int insertDst = (src < dst) ? dst - 1 : dst;
                            dstList->insert(dstList->begin() + insertDst, std::move(moved));
                            if (OnChanged) OnChanged();
                        }
                    } else {
                        // Cross-list: erase from src, insert before dst in dstList
                        Activity moved = (*srcList)[src];
                        srcList->erase(srcList->begin() + src);
                        dstList->insert(dstList->begin() + dst, std::move(moved));
                        if (OnChanged) OnChanged();
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem("##ctx")) {
            if (fn.kind == FlatNode::Kind::BlockHeader) {
                if (ImGui::MenuItem("Add child activity")) {
                    m_addTargetList    = GetPrimaryBody(a);
                    m_expandedIds.insert(a.id);
                    m_draft            = Activity{GenId(), true, MouseClickActivity{}};
                    m_editIdx          = -1;
                    m_openModal        = true;
                    m_keyCaptureActive = false;
                    m_scrollCapture    = false;
                    m_pickStage        = PickStage::None;
                }
                ImGui::Separator();
            }
            if (ImGui::MenuItem("Edit")) {
                m_draft            = a;
                m_editIdx          = fn.indexInParent;
                m_openModal        = true;
                m_keyCaptureActive = false;
                m_scrollCapture    = false;
                m_pickStage        = PickStage::None;
            }
            if (ImGui::MenuItem("Duplicate")) {
                Activity copy = a; copy.id = GenId();
                fn.parentList->insert(fn.parentList->begin() + fn.indexInParent + 1,
                                      std::move(copy));
                if (OnChanged) OnChanged();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select All"))
                for (auto& fn2 : m_flatNodes)
                    if (fn2.act && fn2.kind != FlatNode::Kind::BlockEnd)
                        m_selectedIds.insert(fn2.act->id);
            if (ImGui::MenuItem("Select None")) { m_selectedIds.clear(); m_lastClickedId.clear(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Move Up",   nullptr, false, fn.indexInParent > 0)) {
                std::swap((*fn.parentList)[fn.indexInParent],
                          (*fn.parentList)[fn.indexInParent-1]);
                if (OnChanged) OnChanged();
            }
            if (ImGui::MenuItem("Move Down", nullptr, false,
                                fn.indexInParent < (int)fn.parentList->size()-1)) {
                std::swap((*fn.parentList)[fn.indexInParent],
                          (*fn.parentList)[fn.indexInParent+1]);
                if (OnChanged) OnChanged();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(a.enabled ? "Disable" : "Enable")) {
                a.enabled = !a.enabled;
                if (OnChanged) OnChanged();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) {
                m_confirmDeleteId    = a.id;
                m_confirmDeleteBatch = false;
                m_pendingConfirmOpen = true;
            }
            ImGui::EndPopup();
        }

        // [+] button: always visible on block headers to add a child activity
        if (fn.kind == FlatNode::Kind::BlockHeader) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f,0.85f,0.4f,1.f));
            char addHdrLbl[32]; snprintf(addHdrLbl, sizeof(addHdrLbl), "[+]##hdr%d", ni);
            if (ImGui::SmallButton(addHdrLbl)) {
                m_addTargetList    = GetPrimaryBody(a);
                m_expandedIds.insert(a.id);
                m_draft            = Activity{GenId(), true, MouseClickActivity{}};
                m_editIdx          = -1;
                m_openModal        = true;
                m_keyCaptureActive = false;
                m_scrollCapture    = false;
                m_pickStage        = PickStage::None;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add child activity to this block");
        }

        // Inline action buttons (visible when selected)
        if (isSelected) {
            ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.f, ImGui::GetStyle().ItemSpacing.y));
            if (ImGui::SmallButton(a.enabled ? "off##row" : "on##row")) {
                a.enabled = !a.enabled;
                if (OnChanged) OnChanged();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X##row")) {
                m_confirmDeleteId    = a.id;
                m_confirmDeleteBatch = false;
                m_pendingConfirmOpen = true;
            }
            ImGui::PopStyleVar();
        }

        ++visibleIdx;
        ImGui::PopID();
    }

    if (currentStep < 0) m_lastScrolledStep = -1;

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
            ImGui::Text("Delete %d selected %s?",
                (int)m_selectedIds.size(),
                m_selectedIds.size()==1?"activity":"activities");
        } else {
            ImGui::Text("Delete this activity?");
        }
        ImGui::Separator();
        if (ImGui::Button("Yes##cdel", ImVec2(80,0))) {
            if (m_confirmDeleteBatch) {
                // Delete all selected from their respective parent lists
                // Collect (parentList*, index) sorted descending per list
                std::vector<std::pair<std::vector<Activity>*,int>> toDelete;
                for (auto& fn2 : m_flatNodes)
                    if (fn2.act && fn2.kind != FlatNode::Kind::BlockEnd
                        && m_selectedIds.count(fn2.act->id))
                        toDelete.push_back({fn2.parentList, fn2.indexInParent});
                // Sort descending by index within each list to erase safely
                std::sort(toDelete.begin(), toDelete.end(),
                    [](auto& a, auto& b){ return a.second > b.second; });
                for (auto& [pl, idx] : toDelete)
                    if (idx >= 0 && idx < (int)pl->size())
                        pl->erase(pl->begin() + idx);
                m_selectedIds.clear();
                m_editIdx = -1;
            } else if (!m_confirmDeleteId.empty()) {
                for (auto& fn2 : m_flatNodes) {
                    if (fn2.act && fn2.act->id == m_confirmDeleteId) {
                        fn2.parentList->erase(fn2.parentList->begin() + fn2.indexInParent);
                        m_selectedIds.erase(m_confirmDeleteId);
                        break;
                    }
                }
                m_editIdx = -1;
            }
            m_confirmDeleteId    = "";
            m_confirmDeleteBatch = false;
            if (OnChanged) OnChanged();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No##cdel", ImVec2(80,0))) {
            m_confirmDeleteId    = "";
            m_confirmDeleteBatch = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_openModal) {
        ImGui::OpenPopup("##actmodal");
        m_openModal = false;
    }
    RenderModal(wf);
}

// ── Snipping tool overlay ─────────────────────────────────────────────────────

void ActivityEditorWidget::RenderSnipOverlay(Workflow& wf) {
    ImGuiIO& io = ImGui::GetIO();

    // Main full-screen transparent capture window (all events + drawing here)
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("##snipoverlay", nullptr, flags)) {
        auto* dl  = ImGui::GetWindowDrawList();
        ImVec2 sz = io.DisplaySize;
        ImVec2 mp = io.MousePos;

        // Update drag state
        if (ImGui::IsMouseClicked(0) && !m_snipDragging) {
            m_snipX1 = m_snipX2 = (int)mp.x;
            m_snipY1 = m_snipY2 = (int)mp.y;
            m_snipDragging = true;
        }
        if (m_snipDragging && ImGui::IsMouseDown(0)) {
            m_snipX2 = (int)mp.x;
            m_snipY2 = (int)mp.y;
        }

        int x1 = std::min(m_snipX1, m_snipX2);
        int y1 = std::min(m_snipY1, m_snipY2);
        int x2 = std::max(m_snipX1, m_snipX2);
        int y2 = std::max(m_snipY1, m_snipY2);

        if (m_snipDragging) {
            // Dashed selection border
            auto drawDashed = [&](ImVec2 p1, ImVec2 p2) {
                const float dashLen = 8.f, gapLen = 4.f;
                float dx = p2.x - p1.x, dy = p2.y - p1.y;
                float len = sqrtf(dx*dx + dy*dy);
                if (len < 1.f) return;
                float nx = dx/len, ny = dy/len;
                float pos = 0.f; bool on = true;
                while (pos < len) {
                    float end = std::min(pos + (on ? dashLen : gapLen), len);
                    if (on)
                        dl->AddLine(ImVec2(p1.x + nx*pos, p1.y + ny*pos),
                                    ImVec2(p1.x + nx*end, p1.y + ny*end),
                                    IM_COL32(255,255,255,220), 2.f);
                    pos = end; on = !on;
                }
            };
            drawDashed(ImVec2((float)x1,(float)y1), ImVec2((float)x2,(float)y1));
            drawDashed(ImVec2((float)x2,(float)y1), ImVec2((float)x2,(float)y2));
            drawDashed(ImVec2((float)x2,(float)y2), ImVec2((float)x1,(float)y2));
            drawDashed(ImVec2((float)x1,(float)y2), ImVec2((float)x1,(float)y1));

            // Size label
            char sizeLabel[64];
            snprintf(sizeLabel, sizeof(sizeLabel), "%d x %d", x2-x1, y2-y1);
            dl->AddText(ImVec2((float)x1+4, (float)y2+4),
                        IM_COL32(255,255,255,255), sizeLabel);
        } else {
            // Crosshair guide lines
            float mx = mp.x, my = mp.y;
            dl->AddLine(ImVec2(0, my), ImVec2(sz.x, my), IM_COL32(255,255,255,80), 1.f);
            dl->AddLine(ImVec2(mx, 0), ImVec2(mx, sz.y), IM_COL32(255,255,255,80), 1.f);
        }

        // Instruction text (bottom-left)
        ImGui::SetCursorPos(ImVec2(8, io.DisplaySize.y - 28.f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,0.3f,1));
        ImGui::Text("Drag to select region. Release to capture. Esc = cancel.");
        ImGui::PopStyleColor();

        // Confirm on mouse release
        if (m_snipDragging && ImGui::IsMouseReleased(0)) {
            m_snipDragging = false;
            int w = x2 - x1;
            int h = y2 - y1;
            if (w > 0 && h > 0 && !m_snipPixels.empty() &&
                m_snipW > 0 && m_snipH > 0) {
                std::visit([&](auto&& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, PixelRangeCheckActivity>) {
                        v.x1 = x1; v.y1 = y1; v.x2 = x2; v.y2 = y2;
                        v.sample_w = w; v.sample_h = h;
                        v.sample.clear(); v.sample.reserve(w * h);
                        for (int row = y1; row < y2 && row < m_snipH; ++row)
                            for (int col = x1; col < x2 && col < m_snipW; ++col)
                                v.sample.push_back(m_snipPixels[(size_t)row * m_snipW + col]);
                    }
                }, m_draft.data);
            }
            m_snipStage = SnipStage::Done;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_snipDragging = false;
            m_snipStage    = SnipStage::Done; // cancel — no changes applied
        }
    }
    ImGui::End();

    // Opacity HUD — top-right corner, separate floating window
    {
        ImGuiWindowFlags hudFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 260.f, 8.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        if (ImGui::Begin("##sniphud", nullptr, hudFlags)) {
            ImGui::TextDisabled("Overlay opacity");
            if (m_pOverlayOpacity) {
                float pct = *m_pOverlayOpacity * 100.f;
                ImGui::SetNextItemWidth(200.f);
                if (ImGui::SliderFloat("##snipopa", &pct, 5.f, 95.f, "%.0f%%")) {
                    *m_pOverlayOpacity = pct / 100.f;
                    if (m_sdlWindow) SDL_SetWindowOpacity(m_sdlWindow, *m_pOverlayOpacity);
                }
            }
        }
        ImGui::End();
    }
}

// ── Coordinate + color picker overlay ────────────────────────────────────────
void ActivityEditorWidget::RenderPickOverlay() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 pos = io.MousePos;
    pos.x += 16.f; pos.y += 16.f;
    // Clamp to screen (both upper and lower bounds)
    if (pos.x + 240 > io.DisplaySize.x) pos.x = io.DisplaySize.x - 240;
    if (pos.y + 110 > io.DisplaySize.y) pos.y = io.DisplaySize.y - 110;
    if (pos.x < 0) pos.x = 0;
    if (pos.y < 0) pos.y = 0;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##pickoverlay", nullptr, flags)) {
        int gx = 0, gy = 0;
        SDL_GetGlobalMouseState(&gx, &gy);

        const char* lbl = "Click to pick pos";
        if (m_pickStage == PickStage::DragFrom)
            lbl = "Click drag start";
        else if (m_pickStage == PickStage::DragTo)
            lbl = "Click drag end";
        else if (m_pickStage == PickStage::RangeFrom)
            lbl = "Click range start";
        else if (m_pickStage == PickStage::RangeTo)
            lbl = "Click range end";
        ImGui::TextColored(ImVec4(1,0.9f,0.3f,1), "%s: %d, %d", lbl, gx, gy);

        if (m_pickStage == PickStage::RangeTo) {
            if (auto* pr = std::get_if<PixelRangeCheckActivity>(&m_draft.data))
                ImGui::Text("Rect: %d x %d", std::abs(gx - pr->x1)+1, std::abs(gy - pr->y1)+1);
        }

        bool isPixelPick = std::holds_alternative<PixelCheckActivity>(m_draft.data) ||
                           std::holds_alternative<PixelRangeCheckActivity>(m_draft.data);
        if (isPixelPick) {
#if !defined(__APPLE__)
            uint32_t c = EditorPixelChecker()->GetPixelRGB(gx, gy);
            float r = ((c>>16)&0xFF)/255.f, g2 = ((c>>8)&0xFF)/255.f, b = (c&0xFF)/255.f;
            ImGui::ColorButton("##pcol", ImVec4(r,g2,b,1.f),
                ImGuiColorEditFlags_NoPicker|ImGuiColorEditFlags_NoTooltip, ImVec2(14,14));
            ImGui::SameLine();
            ImGui::Text("#%06X", c);
#endif
        }

        ImGui::TextDisabled("Click = confirm   Esc = cancel");

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
            ApplyPickedCoords(gx, gy);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_pickStage = PickStage::None;
            ExitFullscreenMode();
            m_openModal = true;
        }
    }
    ImGui::End();

    // Drag line: draw start point + line to cursor during DragTo stage
    if (m_pickStage == PickStage::DragTo) {
        if (auto* drag = std::get_if<MouseDragActivity>(&m_draft.data)) {
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            ImVec2 p1((float)drag->from_x, (float)drag->from_y);
            ImVec2 p2 = ImGui::GetIO().MousePos;
            const ImU32 col = IM_COL32(255, 200, 50, 220);
            bg->AddCircleFilled(p1, 6.f, col);
            bg->AddLine(p1, p2, col, 2.f);
            bg->AddCircleFilled(p2, 6.f, IM_COL32(255, 200, 50, 120));
        }
    }

    // Opacity HUD — top-right corner, separate floating window
    {
        ImGuiIO& io2 = ImGui::GetIO();
        ImGuiWindowFlags hudFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos(ImVec2(io2.DisplaySize.x - 260.f, 8.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        if (ImGui::Begin("##pickhud", nullptr, hudFlags)) {
            ImGui::TextDisabled("Overlay opacity");
            if (m_pOverlayOpacity) {
                float pct = *m_pOverlayOpacity * 100.f;
                ImGui::SetNextItemWidth(200.f);
                if (ImGui::SliderFloat("##pickopa", &pct, 5.f, 95.f, "%.0f%%")) {
                    *m_pOverlayOpacity = pct / 100.f;
                    if (m_sdlWindow) SDL_SetWindowOpacity(m_sdlWindow, *m_pOverlayOpacity);
                }
            }
        }
        ImGui::End();
    }
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
            v.color_rgb = EditorPixelChecker()->GetPixelRGB(x, y);
        } else if constexpr (std::is_same_v<T,MouseDragActivity>) {
            if (m_pickStage == PickStage::DragFrom) {
                v.from_x = x; v.from_y = y;
                m_pickStage = PickStage::DragTo;
                done = false;
            } else {
                v.to_x = x; v.to_y = y;
            }
        } else if constexpr (std::is_same_v<T,PixelRangeCheckActivity>) {
            if (m_pickStage == PickStage::RangeFrom) {
                v.x1 = x; v.y1 = y;
                m_pickStage = PickStage::RangeTo;
                done = false;
            } else {
                v.x2 = x; v.y2 = y;
                // Capture sample from the picked rect
                int left = std::min(v.x1, v.x2);
                int top  = std::min(v.y1, v.y2);
                int w    = std::abs(v.x2 - v.x1) + 1;
                int h    = std::abs(v.y2 - v.y1) + 1;
                PixelBuffer buf = EditorPixelChecker()->CaptureRegion(left, top, w, h);
                if (!buf.Empty()) {
                    v.sample_w = buf.width; v.sample_h = buf.height;
                    v.sample   = std::move(buf.pixels);
                }
            }
        }
    }, m_draft.data);

    if (done) {
        m_pickStage = PickStage::None;
        ExitFullscreenMode();
        m_openModal = true;
    }
}

// ── Modal editor ──────────────────────────────────────────────────────────────

void ActivityEditorWidget::RenderModal(Workflow& wf) {
    ImGui::SetNextWindowSize(ImVec2(500, 460), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("##actmodal", nullptr, ImGuiWindowFlags_NoResize)) return;

    ImGui::Text(m_editIdx < 0 ? "Add Activity" : "Edit Activity");
    ImGui::Separator();

    int dispIdx = VariantToDisplayIdx(m_draft.data);
    if (dispIdx < 0) dispIdx = 0; // hidden type (pixel_check) → show as first
    if (ImGui::Combo("Type", &dispIdx, kTypes, kNumTypes)) {
        m_draft.data       = DefaultData(dispIdx);
        m_keyCaptureActive = false;
        m_scrollCapture    = false;
        m_scrollAccum      = 0.f;
        // Auto-name new block types
        if (IsBlockType(m_draft.data)) {
            const char* prefix = (dispIdx == 7) ? "Pixel Check"
                               : (dispIdx == 12) ? "Loop"
                               : (dispIdx == 13) ? "If"
                               : (dispIdx == 14) ? "Switch" : "";
            int vi = kTypeToVariantIdx[dispIdx];
            AutoNameBlock(m_draft, wf.activities, prefix, vi);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Type of action to perform");
    ImGui::Checkbox("Enabled", &m_draft.enabled);
    ImGui::Separator();

    float fieldsH = ImGui::GetContentRegionAvail().y
                    - ImGui::GetFrameHeightWithSpacing() * 2.0f
                    - ImGui::GetStyle().ItemSpacing.y * 2.0f;
    ImGui::BeginChild("##actfields", ImVec2(0, fieldsH), false);
    RenderActivityFields(m_draft.data, wf);
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(100,0))) {
        if (m_addTargetList) {
            // Add to a specific block body (set by "[+]" or "Add child activity")
            m_addTargetList->push_back(m_draft);
            m_addTargetList = nullptr;
        } else if (m_editIdx < 0) {
            wf.activities.push_back(m_draft);
        } else {
            // Find by activity ID — works for both top-level and nested activities
            for (auto& fn : m_flatNodes) {
                if (fn.act && fn.act->id == m_draft.id && fn.parentList) {
                    (*fn.parentList)[fn.indexInParent] = m_draft;
                    break;
                }
            }
        }
        if (OnChanged) OnChanged();
        m_keyCaptureActive = false;
        m_scrollCapture    = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100,0))) {
        m_addTargetList    = nullptr;
        m_keyCaptureActive = false;
        m_scrollCapture    = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ── Activity field rendering ──────────────────────────────────────────────────

void ActivityEditorWidget::RenderActivityFields(ActivityData& data, const Workflow& wf) {
    bool isGlobal = (wf.window.type == WindowTarget::Type::Global);

    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;

        // Position mode helper: forced absolute for Global workflows
        auto posMode = [&](PositionMode& pm) {
            if (isGlobal) {
                pm = PositionMode::Absolute;
                ImGui::TextDisabled("Position mode: absolute (global workflow)");
            } else {
                int idx = (pm == PositionMode::Relative) ? 1 : 0;
                ImGui::SetNextItemWidth(140);
                if (ImGui::Combo("Position mode", &idx, PosModeNames, 2))
                    pm = idx == 1 ? PositionMode::Relative : PositionMode::Absolute;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Absolute = global screen coordinates\n"
                                      "Relative = offset from the target window's origin");
            }
        };
        auto btnCombo = [](MouseButton& b) {
            int idx = (b == MouseButton::Right) ? 1 : (b == MouseButton::Middle) ? 2 : 0;
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("Button", &idx, BtnNames, 3)) b = (MouseButton)idx;
        };
        auto delayFields = [](int& dm, int& dr) {
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)", &dm);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Random range (ms)", &dr);
            dm = std::max(0,dm); dr = std::max(0,dr);
        };
        auto xyPick = [&](int& x, int& y, PickStage stage, PositionMode curPm) {
            ImGui::SetNextItemWidth(120); ImGui::InputInt("X", &x);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Y", &y);
            if (ImGui::Button("Pick position##xy")) {
                if (!isGlobal && curPm == PositionMode::Relative &&
                    wf.window.title.empty() && wf.window.class_name.empty() &&
                    wf.window.handle == 0) {
                    m_showNoWindowDlg = true;
                } else {
                    EnterFullscreenMode();
                    m_pickStage = stage;
                    ImGui::CloseCurrentPopup();
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click then hover over screen to pick");
        };

        if constexpr (std::is_same_v<T,MouseMoveActivity>) {
            posMode(v.pos_mode);
            xyPick(v.x, v.y, PickStage::Single, v.pos_mode);
            delayFields(v.delay_ms, v.delay_rand_ms);

        } else if constexpr (std::is_same_v<T,MouseClickActivity>) {
            posMode(v.pos_mode);
            xyPick(v.x, v.y, PickStage::Single, v.pos_mode);
            btnCombo(v.button);
            ImGui::Checkbox("Double click", &v.double_click);
            delayFields(v.delay_ms, v.delay_rand_ms);

        } else if constexpr (std::is_same_v<T,MouseDragActivity>) {
            posMode(v.pos_mode);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("From X", &v.from_x);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("From Y", &v.from_y);
            ImGui::TextDisabled("  ----->");
            ImGui::SetNextItemWidth(120); ImGui::InputInt("To X", &v.to_x);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("To Y", &v.to_y);
            if (ImGui::Button("Pick drag##drag")) {
                EnterFullscreenMode(); m_pickStage = PickStage::DragFrom; ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click start point, then click end point");
            btnCombo(v.button);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Duration (ms)", &v.duration_ms);
            v.duration_ms = std::max(1, v.duration_ms);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)", &v.delay_ms);

        } else if constexpr (std::is_same_v<T,MouseScrollActivity>) {
            posMode(v.pos_mode);
            xyPick(v.x, v.y, PickStage::Single, v.pos_mode);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delta X", &v.delta_x);
            if (m_scrollCapture) {
                m_scrollAccum += ImGui::GetIO().MouseWheel;
                ImGui::TextColored(ImVec4(1,0.9f,0.3f,1), "Scroll now... delta Y: %d", (int)m_scrollAccum);
                if (ImGui::Button("Done##sc") || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
                    v.delta_y = (int)m_scrollAccum; m_scrollCapture = false; m_scrollAccum = 0.f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel##sc")) { m_scrollCapture = false; m_scrollAccum = 0.f; }
            } else {
                ImGui::SetNextItemWidth(120); ImGui::InputInt("Delta Y", &v.delta_y);
                if (ImGui::Button("Capture scroll##sc")) { m_scrollCapture = true; m_scrollAccum = 0.f; }
            }
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)", &v.delay_ms);

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
                        v.key = name; v.modifiers.clear();
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
                ImGui::Text("Modifiers:");
                ImGui::SameLine();
                ImGui::TextDisabled("%s", v.modifiers.empty() ? "(none)" : [&]{
                    std::string s; for (auto& m : v.modifiers) s += m + " "; return s;
                }().c_str());
            }
            delayFields(v.delay_ms, v.delay_rand_ms);

        } else if constexpr (std::is_same_v<T,TypeStringActivity>) {
            static char textBuf[512]{};
            strncpy(textBuf, v.text.c_str(), sizeof(textBuf)-1);
            if (ImGui::InputTextMultiline("Text", textBuf, sizeof(textBuf), ImVec2(0,80)))
                v.text = textBuf;
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Char delay (ms)", &v.delay_between_chars_ms);
            v.delay_between_chars_ms = std::max(0, v.delay_between_chars_ms);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)", &v.delay_ms);

        } else if constexpr (std::is_same_v<T,WaitActivity>) {
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Duration (ms)", &v.duration_ms);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Random range (ms)", &v.random_range_ms);
            v.duration_ms     = std::max(0, v.duration_ms);
            v.random_range_ms = std::max(0, v.random_range_ms);

        } else if constexpr (std::is_same_v<T,PixelCheckActivity>) {
            // Legacy type — read-only display with basic editing
            posMode(v.pos_mode);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("X", &v.x);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Y", &v.y);
            float col[3] = {((v.color_rgb>>16)&0xFF)/255.f,
                            ((v.color_rgb>> 8)&0xFF)/255.f,
                            ( v.color_rgb     &0xFF)/255.f};
            ImGui::SetNextItemWidth(200);
            if (ImGui::ColorEdit3("Color", col))
                v.color_rgb = ((uint32_t)(col[0]*255)<<16)|((uint32_t)(col[1]*255)<<8)|(uint32_t)(col[2]*255);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Tolerance", &v.tolerance);
            v.tolerance = std::max(0, std::min(255, v.tolerance));
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)", &v.delay_ms);
            ImGui::TextDisabled("(legacy pixel_check — consider using pixel_range_check)");

        } else if constexpr (std::is_same_v<T,PixelRangeCheckActivity>) {
            // Name field
            static char nameBuf[64]{};
            strncpy(nameBuf, v.name.c_str(), sizeof(nameBuf)-1);
            if (ImGui::InputText("Name##prc", nameBuf, sizeof(nameBuf))) v.name = nameBuf;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Display name in the activity tree");

            posMode(v.pos_mode);

            // Snip capture button (primary workflow) — hide app, screenshot, overlay
            if (ImGui::Button("Capture region##prc")) {
                if (m_sdlWindow) {
                    SDL_GetWindowPosition(m_sdlWindow, &m_origWindowX, &m_origWindowY);
                    SDL_GetWindowSize(m_sdlWindow, &m_origWindowW, &m_origWindowH);
                    SDL_HideWindow(m_sdlWindow);
                }
                m_snipStage = SnipStage::WaitMinimize;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Hide app, take screenshot, drag to select region and capture sample");

            ImGui::SameLine();

            // Manual coordinate + old-style pick (secondary)
            if (ImGui::Button("Pick range##prc")) {
                EnterFullscreenMode(); m_pickStage = PickStage::RangeFrom; ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick start then end corner (no screenshot)");

            ImGui::SetNextItemWidth(120); ImGui::InputInt("X1##prc", &v.x1);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Y1##prc", &v.y1);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("X2##prc", &v.x2);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Y2##prc", &v.y2);

            if (v.sample.empty()) {
                ImGui::TextColored(ImVec4(1.f,0.55f,0.3f,1.f), "Sample: (none)");
            } else {
                ImGui::Text("Sample: %d x %d (%d px)", v.sample_w, v.sample_h, v.sample_w*v.sample_h);
                ImGui::SameLine();
                if (ImGui::Button("Clear##prc")) {
                    v.sample.clear(); v.sample_w = v.sample_h = 0;
                    if (m_samplePreviewTex) { SDL_DestroyTexture(m_samplePreviewTex); m_samplePreviewTex = nullptr; }
                    m_samplePreviewHash = 0;
                }

                // Rebuild preview texture when sample changes
                if (m_sdlRenderer && v.sample_w > 0 && v.sample_h > 0) {
                    size_t hash = (size_t)v.sample_w * 100003u
                                + (size_t)v.sample_h * 10007u
                                + v.sample.size();
                    if (hash != m_samplePreviewHash || !m_samplePreviewTex) {
                        if (m_samplePreviewTex) { SDL_DestroyTexture(m_samplePreviewTex); m_samplePreviewTex = nullptr; }
                        SDL_Texture* tex = SDL_CreateTexture(m_sdlRenderer,
                            SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                            v.sample_w, v.sample_h);
                        if (tex) {
                            std::vector<uint32_t> argb(v.sample.size());
                            for (size_t i = 0; i < v.sample.size(); ++i)
                                argb[i] = 0xFF000000u | v.sample[i];
                            SDL_UpdateTexture(tex, nullptr, argb.data(), v.sample_w * 4);
                            m_samplePreviewTex = tex;
                            m_samplePreviewHash = hash;
                        }
                    }
                    if (m_samplePreviewTex) {
                        const float maxSz = 64.f;
                        float scale = std::min(maxSz / (float)v.sample_w, maxSz / (float)v.sample_h);
                        float dispW = (float)v.sample_w * scale;
                        float dispH = (float)v.sample_h * scale;
                        ImGui::Image((ImTextureID)(intptr_t)m_samplePreviewTex, ImVec2(dispW, dispH));
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Captured sample (%dx%d)", v.sample_w, v.sample_h);
                    }
                }
            }

            ImGui::SetNextItemWidth(120); ImGui::InputInt("Tolerance##prc", &v.tolerance);
            v.tolerance = std::max(0, std::min(255, v.tolerance));
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Match percent##prc", &v.match_percent);
            v.match_percent = std::max(0, std::min(100, v.match_percent));
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Retry interval (ms)##prc", &v.retry_interval_ms);
            v.retry_interval_ms = std::max(0, v.retry_interval_ms);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Retry timeout (ms, 0=once)##prc", &v.retry_timeout_ms);
            v.retry_timeout_ms = std::max(0, v.retry_timeout_ms);
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)##prc", &v.delay_ms);
            ImGui::TextDisabled("Children (match/no-match) are edited inline in the activity list.");

        } else if constexpr (std::is_same_v<T,SystemActionActivity>) {
            static const char* actionNames[] = {"Shutdown","Restart","Sleep","Hibernate","Lock","Log out"};
            int idx = (int)v.action;
            ImGui::SetNextItemWidth(140);
            if (ImGui::Combo("Action", &idx, actionNames, 6)) v.action = (SystemAction)idx;
            bool forceApplies = (v.action==SystemAction::Shutdown||v.action==SystemAction::Restart||
                                 v.action==SystemAction::Hibernate||v.action==SystemAction::LogOut);
            ImGui::BeginDisabled(!forceApplies);
            ImGui::Checkbox("Force (skip save dialogs)", &v.force);
            ImGui::EndDisabled();
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)", &v.delay_ms);
            v.delay_ms = std::max(0, v.delay_ms);

        } else if constexpr (std::is_same_v<T,RunWorkflowActivity>) {
            if (m_workflows && !m_workflows->empty()) {
                ImGui::TextDisabled("Filter:"); ImGui::SameLine();
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##wffilter", m_wfFilterBuf, sizeof(m_wfFilterBuf));
                ImGui::BeginChild("##wflist_rw", ImVec2(0,110), true);
                std::string filterLow(m_wfFilterBuf);
                for (auto& c : filterLow) c = (char)std::tolower((unsigned char)c);
                for (auto& wf2 : *m_workflows) {
                    std::string nameLow = wf2.name;
                    for (auto& c : nameLow) c = (char)std::tolower((unsigned char)c);
                    if (!filterLow.empty() && nameLow.find(filterLow)==std::string::npos) continue;
                    bool selected = (v.workflow_id == wf2.id);
                    char lbl[256]; snprintf(lbl,sizeof(lbl),"%s##rwf%s",wf2.name.c_str(),wf2.id.c_str());
                    if (ImGui::Selectable(lbl, selected)) v.workflow_id = wf2.id;
                }
                ImGui::EndChild();
                ImGui::TextDisabled("ID: %.24s", v.workflow_id.c_str());
            } else {
                static char idBuf[64]{};
                strncpy(idBuf, v.workflow_id.c_str(), sizeof(idBuf)-1);
                if (ImGui::InputText("Workflow ID", idBuf, sizeof(idBuf))) v.workflow_id = idBuf;
            }
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)", &v.delay_ms);

        } else if constexpr (std::is_same_v<T,RunActivityActivity>) {
            // Selectable list of all activities in the workflow
            ImGui::Text("Select activity to reuse:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##actfilter_ra", m_actFilterBuf, sizeof(m_actFilterBuf));
            ImGui::BeginChild("##actlist_ra", ImVec2(0,110), true);
            std::string filterLow(m_actFilterBuf);
            for (auto& c : filterLow) c = (char)std::tolower((unsigned char)c);
            // Walk flat nodes to show all activities
            for (auto& fn : m_flatNodes) {
                if (!fn.act || fn.kind == FlatNode::Kind::BlockEnd) continue;
                std::string summary = ActivitySummary(*fn.act);
                std::string sumLow  = summary;
                for (auto& c : sumLow) c = (char)std::tolower((unsigned char)c);
                if (!filterLow.empty() && sumLow.find(filterLow)==std::string::npos) continue;
                bool sel = (v.activity_id == fn.act->id);
                char lbl[256]; snprintf(lbl,sizeof(lbl),"%s##ra%s",summary.c_str(),fn.act->id.c_str());
                if (ImGui::Selectable(lbl, sel)) v.activity_id = fn.act->id;
            }
            ImGui::EndChild();
            if (!v.activity_id.empty())
                ImGui::TextDisabled("ID: %.24s", v.activity_id.c_str());
            else
                ImGui::TextColored(ImVec4(1,0.5f,0.3f,1), "No activity selected");
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)", &v.delay_ms);

        } else if constexpr (std::is_same_v<T,SetVariableActivity>) {
            static char nameBuf[64]{};
            strncpy(nameBuf, v.name.c_str(), sizeof(nameBuf)-1);
            if (ImGui::InputText("Variable name##sv", nameBuf, sizeof(nameBuf))) v.name = nameBuf;

            int opIdx = (int)v.op;
            ImGui::SetNextItemWidth(120);
            if (ImGui::Combo("Operation##sv", &opIdx, VarOpNames, 4)) v.op = (VarOp)opIdx;

            if (v.op == VarOp::Set) {
                static char valBuf[256]{};
                strncpy(valBuf, v.value.c_str(), sizeof(valBuf)-1);
                if (ImGui::InputText("Value##sv", valBuf, sizeof(valBuf))) v.value = valBuf;
            } else if (v.op == VarOp::Increment || v.op == VarOp::Decrement) {
                ImGui::SetNextItemWidth(120); ImGui::InputInt("Step##sv", &v.step);
                v.step = std::max(1, v.step);
            } else if (v.op == VarOp::Random) {
                ImGui::SetNextItemWidth(120); ImGui::InputInt("Min##sv", &v.rand_min);
                ImGui::SetNextItemWidth(120); ImGui::InputInt("Max##sv", &v.rand_max);
                if (v.rand_max < v.rand_min) v.rand_max = v.rand_min;
            }
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)##sv", &v.delay_ms);

        } else if constexpr (std::is_same_v<T,LoopActivity>) {
            static char nameBuf[64]{};
            strncpy(nameBuf, v.name.c_str(), sizeof(nameBuf)-1);
            if (ImGui::InputText("Name##lp", nameBuf, sizeof(nameBuf))) v.name = nameBuf;
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Count (0=infinite)##lp", &v.count);
            v.count = std::max(0, v.count);
            static char iterBuf[64]{};
            strncpy(iterBuf, v.iter_var.c_str(), sizeof(iterBuf)-1);
            if (ImGui::InputText("Write iteration to var##lp", iterBuf, sizeof(iterBuf)))
                v.iter_var = iterBuf;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Variable name to store the current iteration number (1-based).\n"
                                  "E.g. set to \"i\" and use $i in other activities.\n"
                                  "Leave empty to skip.");
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)##lp", &v.delay_ms);
            ImGui::TextDisabled("Children: edit inline in the activity list.");

        } else if constexpr (std::is_same_v<T,IfActivity>) {
            static char nameBuf[64]{};
            strncpy(nameBuf, v.name.c_str(), sizeof(nameBuf)-1);
            if (ImGui::InputText("Name##if", nameBuf, sizeof(nameBuf))) v.name = nameBuf;
            ImGui::Separator();
            ImGui::Text("Condition:");
            // LHS
            ImGui::Checkbox("LHS is var##if", &v.cond.lhs_is_var); ImGui::SameLine();
            static char lhsBuf[64]{}; strncpy(lhsBuf, v.cond.lhs.c_str(), sizeof(lhsBuf)-1);
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputText("LHS##if", lhsBuf, sizeof(lhsBuf))) v.cond.lhs = lhsBuf;
            // OP
            int opIdx = (int)v.cond.op;
            ImGui::SetNextItemWidth(100);
            if (ImGui::Combo("Op##if", &opIdx, CondOpNames, 7)) v.cond.op = (ConditionOp)opIdx;
            // RHS
            ImGui::Checkbox("RHS is var##if", &v.cond.rhs_is_var); ImGui::SameLine();
            static char rhsBuf[64]{}; strncpy(rhsBuf, v.cond.rhs.c_str(), sizeof(rhsBuf)-1);
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputText("RHS##if", rhsBuf, sizeof(rhsBuf))) v.cond.rhs = rhsBuf;
            ImGui::Separator();
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)##if", &v.delay_ms);
            ImGui::TextDisabled("then/else branches: edit inline in the activity list.");

        } else if constexpr (std::is_same_v<T,SwitchActivity>) {
            static char nameBuf[64]{};
            strncpy(nameBuf, v.name.c_str(), sizeof(nameBuf)-1);
            if (ImGui::InputText("Name##sw", nameBuf, sizeof(nameBuf))) v.name = nameBuf;
            static char varBuf[64]{};
            strncpy(varBuf, v.var_name.c_str(), sizeof(varBuf)-1);
            if (ImGui::InputText("Variable##sw", varBuf, sizeof(varBuf))) v.var_name = varBuf;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Runtime variable to test");
            ImGui::Separator();
            ImGui::Text("Cases:");
            ImGui::BeginChild("##cases_sw", ImVec2(0, 100), true);
            for (int ci = 0; ci < (int)v.cases.size(); ++ci) {
                ImGui::PushID(ci);
                static char cvBuf[64]{};
                strncpy(cvBuf, v.cases[ci].value.c_str(), sizeof(cvBuf)-1);
                ImGui::SetNextItemWidth(120);
                if (ImGui::InputText("##cv", cvBuf, sizeof(cvBuf))) v.cases[ci].value = cvBuf;
                ImGui::SameLine();
                if (ImGui::SmallButton("X##del_case")) {
                    v.cases.erase(v.cases.begin() + ci); --ci;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (ImGui::Button("+ Case##sw")) {
                SwitchCase sc;
                sc.body = std::make_shared<std::vector<Activity>>();
                v.cases.push_back(std::move(sc));
            }
            ImGui::Separator();
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)##sw", &v.delay_ms);
            ImGui::TextDisabled("Case bodies: edit inline in the activity list.");

        } else if constexpr (std::is_same_v<T,JumpActivity>) {
            ImGui::Text("Jump to activity:");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##actfilter_jmp", m_actFilterBuf, sizeof(m_actFilterBuf));
            ImGui::BeginChild("##actlist_jmp", ImVec2(0,130), true);
            std::string filterLow(m_actFilterBuf);
            for (auto& c : filterLow) c = (char)std::tolower((unsigned char)c);
            int jVisNum = 0;
            for (auto& fn2 : m_flatNodes) {
                if (!fn2.act) continue;
                ++jVisNum;
                std::string summary = ActivitySummary(*fn2.act);
                std::string sumLow  = summary;
                for (auto& c : sumLow) c = (char)std::tolower((unsigned char)c);
                if (!filterLow.empty() && sumLow.find(filterLow)==std::string::npos) continue;
                bool sel = (v.target_id == fn2.act->id);
                char lbl[256]; snprintf(lbl,sizeof(lbl),"#%d %s##jmp%s",jVisNum,summary.c_str(),fn2.act->id.c_str());
                if (ImGui::Selectable(lbl, sel)) v.target_id = fn2.act->id;
            }
            ImGui::EndChild();
            if (!v.target_id.empty()) {
                int tNum = 0; bool found = false;
                for (auto& fn2 : m_flatNodes) {
                    if (!fn2.act) continue;
                    ++tNum;
                    if (fn2.act->id == v.target_id) {
                        std::string ts = ActivitySummary(*fn2.act);
                        if (ts.size() > 30) ts = ts.substr(0, 30) + "..";
                        ImGui::TextDisabled("-> #%d %s", tNum, ts.c_str());
                        found = true; break;
                    }
                }
                if (!found) ImGui::TextDisabled("ID: %.24s (not found)", v.target_id.c_str());
            } else {
                ImGui::TextColored(ImVec4(1,0.5f,0.3f,1), "No target selected");
            }
            ImGui::SetNextItemWidth(120); ImGui::InputInt("Delay after (ms)##jmp", &v.delay_ms);
            v.delay_ms = std::max(0, v.delay_ms);
            ImGui::TextDisabled("Jumps within the same activity list (body/branch/root).");
        }

    }, data);

    // "No window selected" guard dialog
    if (m_showNoWindowDlg) {
        ImGui::OpenPopup("No Window##guard");
        m_showNoWindowDlg = false;
    }
    if (ImGui::BeginPopupModal("No Window##guard", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("No target window selected.");
        ImGui::Text("Pick a window first, or switch to Absolute mode.");
        ImGui::Separator();
        if (ImGui::Button("Use Absolute##grd", ImVec2(110,0))) {
            std::visit([](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T,MouseMoveActivity>   ||
                              std::is_same_v<T,MouseClickActivity>   ||
                              std::is_same_v<T,MouseDragActivity>    ||
                              std::is_same_v<T,MouseScrollActivity>  ||
                              std::is_same_v<T,PixelCheckActivity>   ||
                              std::is_same_v<T,PixelRangeCheckActivity>)
                    v.pos_mode = PositionMode::Absolute;
            }, data);
            // Then enter pick mode
            EnterFullscreenMode();
            m_pickStage = PickStage::Single;
            ImGui::CloseCurrentPopup();
            ImGui::CloseCurrentPopup(); // Also close modal
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##grd", ImVec2(80,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
