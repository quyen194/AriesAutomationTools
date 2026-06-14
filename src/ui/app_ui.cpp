#include "app_ui.hpp"
#include "version.h"
#include "imgui.h"
#include <SDL.h>
#include "icon_data.hpp"
#include "imgui_impl_sdlrenderer2.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <random>
#include <iomanip>

// ── ImGui key -> key name (for hotkey capture) ────────────────────────────────
static std::string AppImGuiKeyToName(ImGuiKey key) {
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

static bool IsHotkeyPressed(const std::string& hk) {
    if (hk.empty()) return false;
    bool needCtrl = false, needShift = false, needAlt = false;
    std::string rem = hk;
    for (;;) {
        size_t p = rem.find('+');
        if (p == std::string::npos) break;
        std::string part = rem.substr(0, p);
        if (part == "ctrl" || part == "lctrl" || part == "rctrl")       { needCtrl  = true; rem = rem.substr(p+1); }
        else if (part == "shift"|| part == "lshift"|| part == "rshift") { needShift = true; rem = rem.substr(p+1); }
        else if (part == "alt"  || part == "lalt"  || part == "ralt")   { needAlt   = true; rem = rem.substr(p+1); }
        else break;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (needCtrl  != io.KeyCtrl)  return false;
    if (needShift != io.KeyShift) return false;
    if (needAlt   != io.KeyAlt)   return false;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        ImGuiKey key = (ImGuiKey)k;
        if (!ImGui::IsKeyPressed(key, false)) continue;
        if (AppImGuiKeyToName(key) == rem) return true;
    }
    return false;
}

// Shared capture logic: scans for a key press, fills buf, sets outKey.
// Returns true once a key is captured; sets capturing=false on Escape or capture.
static bool DoCaptureHotkey(bool& capturing, char* buf, int bufSize, std::string& outKey) {
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        ImGuiKey key = (ImGuiKey)k;
        if (!ImGui::IsKeyPressed(key, false)) continue;
        if (key == ImGuiKey_Escape) { capturing = false; return false; }
        if (key == ImGuiKey_LeftCtrl  || key == ImGuiKey_RightCtrl  ||
            key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
            key == ImGuiKey_LeftAlt   || key == ImGuiKey_RightAlt   ||
            key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper) continue;
        auto name = AppImGuiKeyToName(key);
        if (!name.empty()) {
            ImGuiIO& io = ImGui::GetIO();
            outKey.clear();
            if (io.KeyCtrl)  outKey += "ctrl+";
            if (io.KeyShift) outKey += "shift+";
            if (io.KeyAlt)   outKey += "alt+";
            outKey += name;
            strncpy(buf, outKey.c_str(), bufSize - 1);
            buf[bufSize - 1] = '\0';
            capturing = false;
            return true;
        }
        break;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────

AppUI::~AppUI() {
    if (m_trigSampleTex)      { SDL_DestroyTexture(m_trigSampleTex);      m_trigSampleTex      = nullptr; }
    if (m_trigCrosshairCursor){ SDL_FreeCursor(m_trigCrosshairCursor);    m_trigCrosshairCursor= nullptr; }
}

AppUI::AppUI() {
    m_wfList.OnAdd       = [this]() { AddWorkflow(); };
    m_wfList.OnDuplicate = [this](const std::string& id) { DuplicateWorkflow(id); };
    m_wfList.OnDelete    = [this](const std::string& id) {
        m_confirmDeleteWfId = id;
        m_pendingConfirmWf  = true;
    };
    m_wfList.OnSelect = [this](const std::string& id) { m_selectedId = id; };

    m_actEditor.OnChanged = [this]() {
        m_engine.SetWorkflows(m_config.workflows);
        m_dirty = true;
    };

    m_winPicker.OnPicked = [this](const WindowInfo& info) {
        auto it = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
                               [&](auto& w){ return w.id == m_selectedId; });
        if (it == m_config.workflows.end()) return;
        it->window.type       = WindowTarget::Type::ByTitle;
        it->window.title      = info.title;
        it->window.class_name = info.class_name;
        it->window.handle     = info.handle;
        m_dirty = true;
    };

    m_recOverlay.OnFinished = [this](std::vector<Activity> acts) {
        auto it = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
                               [&](auto& w){ return w.id == m_selectedId; });
        if (it == m_config.workflows.end()) return;
        for (auto& a : acts) it->activities.push_back(std::move(a));
        m_engine.SetWorkflows(m_config.workflows);
        m_dirty = true;
    };

    m_recOverlay.OnHotkeyChanged = [this](const std::string& hk) {
        m_config.start_record_hotkey = hk;
        strncpy(m_cfgStartRecBuf, hk.c_str(), sizeof(m_cfgStartRecBuf) - 1);
        m_cfgStartRecBuf[sizeof(m_cfgStartRecBuf) - 1] = '\0';
        ApplyStartRecordHotkey(hk);
        m_dirty = true;
    };

    m_recOverlay.OnStopHotkeyChanged = [this](const std::string& hk) {
        m_config.stop_record_hotkey = hk;
        strncpy(m_cfgStopRecBuf, hk.c_str(), sizeof(m_cfgStopRecBuf) - 1);
        m_cfgStopRecBuf[sizeof(m_cfgStopRecBuf) - 1] = '\0';
        ApplyStopRecordHotkey(hk);
        m_dirty = true;
    };
}

void AppUI::Init(const std::string& config_path, SDL_Window* sdlWindow) {
    m_configPath = config_path;
    m_sdlWindow  = sdlWindow;
    try {
        m_config = ConfigManager::Load(config_path);
    } catch (...) {}

    m_engine.Init();
    m_engine.SetWorkflows(m_config.workflows);

    // Initialise all hotkey buffers
    strncpy(m_cfgStartRecBuf,    m_config.start_record_hotkey.c_str(), sizeof(m_cfgStartRecBuf)-1);
    strncpy(m_cfgStopRecBuf,     m_config.stop_record_hotkey.c_str(),  sizeof(m_cfgStopRecBuf)-1);
    strncpy(m_cfgStartAllBuf,    m_config.start_all_hotkey.c_str(),    sizeof(m_cfgStartAllBuf)-1);
    strncpy(m_cfgStopAllBuf,     m_config.stop_all_hotkey.c_str(),     sizeof(m_cfgStopAllBuf)-1);
    strncpy(m_cfgPauseAllBuf,    m_config.pause_all_hotkey.c_str(),    sizeof(m_cfgPauseAllBuf)-1);
    strncpy(m_cfgResumeAllBuf,   m_config.resume_all_hotkey.c_str(),   sizeof(m_cfgResumeAllBuf)-1);

    m_recOverlay.SetHotkey(m_config.start_record_hotkey);
    m_recOverlay.SetStopHotkey(m_config.stop_record_hotkey);
    ApplyStartRecordHotkey(m_config.start_record_hotkey);
    ApplyStopRecordHotkey(m_config.stop_record_hotkey);
    ApplyStartAllHotkey(m_config.start_all_hotkey);
    ApplyStopAllHotkey(m_config.stop_all_hotkey);
    ApplyPauseAllHotkey(m_config.pause_all_hotkey);
    ApplyResumeAllHotkey(m_config.resume_all_hotkey);
    m_actEditor.SetWorkflows(&m_config.workflows);
    m_actEditor.SetSDLContext(sdlWindow, SDL_GetRenderer(sdlWindow));
    m_actEditor.SetOverlayOpacity(&m_config.pick_overlay_opacity);

    m_engine.SetTriggerCallback([this](const std::string& id) {
        m_engine.StartWorkflow(id);
    });

    m_triggers.Start(m_config.workflows, nullptr,
                     [this](const std::string& id) { m_engine.StartWorkflow(id); });

    if (!m_config.workflows.empty()) m_selectedId = m_config.workflows[0].id;

    m_tray.Init(kIconPixels, 32, 32);
    UpdateTrayWorkflows();

    // Build an SDL texture for the About dialog icon display
    SDL_Renderer* renderer = SDL_GetRenderer(sdlWindow);
    if (renderer) {
        SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
            const_cast<uint8_t*>(kIconPixels), 32, 32, 32, 32*4,
            0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
        if (surf) {
            m_iconTexture = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_FreeSurface(surf);
        }
    }
}

void AppUI::Shutdown() {
    if (m_iconTexture) { SDL_DestroyTexture(m_iconTexture); m_iconTexture = nullptr; }
    m_tray.Shutdown();
    m_triggers.Stop();
    m_engine.Shutdown();
    if (m_dirty) SaveConfig();
}

bool AppUI::RequestQuit() {
    // If "close to tray" is enabled, minimize to tray instead of quitting
    if (m_config.close_to_tray) {
        MinimizeToTray();
        return false;
    }
    if (!m_dirty) {
        m_shouldQuit = true;
        return true;
    }
    // Bring the window to front so the user can see the confirmation dialog.
    // This matters when the request came from the tray context menu while the
    // window was hidden or behind other windows.
    if (m_sdlWindow) {
        if (!m_windowVisible) {
            SDL_ShowWindow(m_sdlWindow);
            m_windowVisible = true;
        }
        SDL_RaiseWindow(m_sdlWindow);
    }
    m_quitRequested = true;
    return false;
}

void AppUI::OnWindowMinimized() {
    if (m_config.minimize_to_tray) MinimizeToTray();
}

void AppUI::MinimizeToTray() {
    if (m_sdlWindow) SDL_HideWindow(m_sdlWindow);
    m_windowVisible = false;
}

void AppUI::RestoreFromTray() {
    if (m_sdlWindow) {
        SDL_ShowWindow(m_sdlWindow);
        SDL_RaiseWindow(m_sdlWindow);
    }
    m_windowVisible = true;
}

void AppUI::UpdateTrayWorkflows() {
    std::vector<TrayWorkflowDesc> descs;
    descs.reserve(m_config.workflows.size());
    for (auto& wf : m_config.workflows) {
        TrayWorkflowDesc d;
        d.id      = wf.id;
        d.name    = wf.name;
        d.running = m_engine.IsRunning(wf.id);
        d.paused  = m_engine.IsPaused(wf.id);
        descs.push_back(d);
    }
    m_tray.UpdateWorkflows(descs);
}

static void DrawAnimDot(uint8_t* pixels, int cx, int cy, int r, int g, int b) {
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            if (dx*dx + dy*dy > 9) continue;
            int px = cx + dx, py = cy + dy;
            if (px < 0 || px >= 32 || py < 0 || py >= 32) continue;
            int i = (py * 32 + px) * 4;
            // soft blend: 70% dot, 30% original
            pixels[i+0] = (uint8_t)(r * 7 / 10 + pixels[i+0] * 3 / 10);
            pixels[i+1] = (uint8_t)(g * 7 / 10 + pixels[i+1] * 3 / 10);
            pixels[i+2] = (uint8_t)(b * 7 / 10 + pixels[i+2] * 3 / 10);
            pixels[i+3] = 255;
        }
    }
}

void AppUI::UpdateTrayIcon() {
    bool anyRunning = false;
    for (auto& wf : m_config.workflows)
        if (m_engine.IsRunning(wf.id)) { anyRunning = true; break; }

    if (!anyRunning) {
        if (m_animActive) {
            m_animActive = false;
            m_tray.UpdateIcon(kIconPixels, 32, 32);
        }
        return;
    }

    uint32_t now = SDL_GetTicks();
    if (!m_animActive) {
        m_animActive   = true;
        m_animFrame    = 0;
        m_animLastTick = now;
    }
    if (now - m_animLastTick < 220) return;
    m_animLastTick = now;
    m_animFrame    = (m_animFrame + 1) % 8;

    // 8-position dot orbiting the ring clockwise: right -> bottom-right -> bottom -> ...
    static const int kDotX[8] = {27, 24, 16,  8,  5,  8, 16, 24};
    static const int kDotY[8] = {16, 24, 27, 24, 16,  8,  5,  8};

    // Dot colors: leading dot is bright amber, trailing dot is dimmer cyan
    uint8_t frame[32*32*4];
    std::memcpy(frame, kIconPixels, sizeof(frame));

    // Draw trailing dot (previous position, dimmer)
    int prev = (m_animFrame + 7) % 8;
    DrawAnimDot(frame, kDotX[prev], kDotY[prev], 60, 200, 230);
    // Draw leading dot (current, bright amber)
    DrawAnimDot(frame, kDotX[m_animFrame], kDotY[m_animFrame], 255, 190, 30);
    m_tray.UpdateIcon(frame, 32, 32);
}

void AppUI::PollTrayActions() {
    UpdateTrayWorkflows();
    UpdateTrayIcon();
    std::vector<TrayPendingAction> actions;
    m_tray.Poll(actions);
    for (auto& a : actions) {
        switch (a.action) {
            case TrayAction::ShowWindow:
                if (m_windowVisible) MinimizeToTray();
                else                  RestoreFromTray();
                break;
            case TrayAction::Exit:
                RequestQuit();
                break;
            case TrayAction::StartAll:
                m_engine.StartAll();
                break;
            case TrayAction::StopAll:
                m_engine.StopAll();
                break;
            case TrayAction::PauseAll:
                m_engine.PauseAll();
                break;
            case TrayAction::ResumeAll:
                m_engine.ResumeAll();
                break;
            case TrayAction::StartWorkflow:
                m_engine.StartWorkflow(a.wfId);
                break;
            case TrayAction::StopWorkflow:
                m_engine.StopWorkflow(a.wfId);
                break;
            case TrayAction::PauseWorkflow:
                m_engine.PauseWorkflow(a.wfId);
                break;
            case TrayAction::ResumeWorkflow:
                m_engine.ResumeWorkflow(a.wfId);
                break;
        }
    }
}

void AppUI::SaveConfig() {
    try {
        ConfigManager::Save(m_config, m_configPath);
        m_dirty = false;
    } catch (...) {}
}

void AppUI::DiscardConfig() {
    try {
        m_engine.StopAll();
        m_config = ConfigManager::Load(m_configPath);
        m_engine.SetWorkflows(m_config.workflows);
        strncpy(m_cfgStartRecBuf,  m_config.start_record_hotkey.c_str(), sizeof(m_cfgStartRecBuf)-1);
        strncpy(m_cfgStopRecBuf,   m_config.stop_record_hotkey.c_str(),  sizeof(m_cfgStopRecBuf)-1);
        strncpy(m_cfgStartAllBuf,  m_config.start_all_hotkey.c_str(),    sizeof(m_cfgStartAllBuf)-1);
        strncpy(m_cfgStopAllBuf,   m_config.stop_all_hotkey.c_str(),     sizeof(m_cfgStopAllBuf)-1);
        strncpy(m_cfgPauseAllBuf,  m_config.pause_all_hotkey.c_str(),    sizeof(m_cfgPauseAllBuf)-1);
        strncpy(m_cfgResumeAllBuf, m_config.resume_all_hotkey.c_str(),   sizeof(m_cfgResumeAllBuf)-1);
        m_recOverlay.SetHotkey(m_config.start_record_hotkey);
        m_recOverlay.SetStopHotkey(m_config.stop_record_hotkey);
        ApplyStartRecordHotkey(m_config.start_record_hotkey);
        ApplyStopRecordHotkey(m_config.stop_record_hotkey);
        ApplyStartAllHotkey(m_config.start_all_hotkey);
        ApplyStopAllHotkey(m_config.stop_all_hotkey);
        ApplyPauseAllHotkey(m_config.pause_all_hotkey);
        ApplyResumeAllHotkey(m_config.resume_all_hotkey);
        m_actEditor.SetWorkflows(&m_config.workflows);
        m_dirty = false;
        bool selValid = std::any_of(m_config.workflows.begin(), m_config.workflows.end(),
            [&](auto& w){ return w.id == m_selectedId; });
        if (!selValid)
            m_selectedId = m_config.workflows.empty() ? "" : m_config.workflows[0].id;
    } catch (...) {}
}

void AppUI::LoadConfig(const std::string& path) {
    try {
        m_engine.StopAll();
        m_config     = ConfigManager::Load(path);
        m_configPath = path;
        m_engine.SetWorkflows(m_config.workflows);
        m_actEditor.SetWorkflows(&m_config.workflows);
        m_dirty = false;
        if (!m_config.workflows.empty()) m_selectedId = m_config.workflows[0].id;
    } catch (...) {}
}

// ─────────────────────────────────────────────────────────────────────────────

void AppUI::Render() {
    // Hoist selected-workflow lookup — needed by RunSnipStateMachine and main UI
    auto selIt = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
        [&](auto& w){ return w.id == m_selectedId; });

    // Activity editor snip SM must run every frame (manages window show/hide/opacity)
    if (selIt != m_config.workflows.end())
        m_actEditor.RunSnipStateMachine(*selIt);

    // ── Trigger snip state machine ────────────────────────────────────────────
    if (m_trigSnipStage == TrigSnipStage::WaitMinimize) {
        m_trigSnipStage = TrigSnipStage::WaitFrame;
        return; // extra frame so window is fully hidden
    }
    if (m_trigSnipStage == TrigSnipStage::WaitFrame) {
        // Capture screenshot pixels — kept for pixel extraction on mouse release.
        // No texture created: SDL_SetWindowOpacity provides live desktop transparency.
        IPixelChecker* checker = m_engine.PixelChecker();
        if (checker)
            m_trigSnipPixels = checker->CaptureFullScreen(m_trigSnipW, m_trigSnipH);
        if (m_sdlWindow && m_trigSnipOrigW > 0) {
            SDL_DisplayMode dm{};
            if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
                SDL_SetWindowBordered(m_sdlWindow, SDL_FALSE);
                SDL_SetWindowAlwaysOnTop(m_sdlWindow, SDL_TRUE);
                SDL_SetWindowPosition(m_sdlWindow, 0, 0);
                SDL_SetWindowSize(m_sdlWindow, dm.w, dm.h);
                SDL_ShowWindow(m_sdlWindow);
                SDL_RaiseWindow(m_sdlWindow);
            }
            SDL_SetWindowOpacity(m_sdlWindow, m_config.pick_overlay_opacity);
            m_trigOrigCursor = SDL_GetCursor();
            if (!m_trigCrosshairCursor)
                m_trigCrosshairCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
            SDL_SetCursor(m_trigCrosshairCursor);
        }
        m_trigSnipStage    = TrigSnipStage::Active;
        m_trigSnipDragging = false;
        m_trigSnipX1 = m_trigSnipY1 = m_trigSnipX2 = m_trigSnipY2 = 0;
        return;
    }
    if (m_trigSnipStage == TrigSnipStage::Done) {
        m_trigSnipPixels.clear();
        m_trigSnipStage = TrigSnipStage::None;
        // Restore window
        if (m_sdlWindow && m_trigSnipOrigW > 0) {
            SDL_SetWindowOpacity(m_sdlWindow, 1.0f);
            SDL_SetWindowBordered(m_sdlWindow, SDL_TRUE);
            SDL_SetWindowAlwaysOnTop(m_sdlWindow, SDL_FALSE);
            SDL_SetWindowPosition(m_sdlWindow, m_trigSnipOrigX, m_trigSnipOrigY);
            SDL_SetWindowSize(m_sdlWindow, m_trigSnipOrigW, m_trigSnipOrigH);
            m_trigSnipOrigW = 0;
            if (m_trigOrigCursor) { SDL_SetCursor(m_trigOrigCursor); m_trigOrigCursor = nullptr; }
        }
    }

    // Compute capture state before polling OS hotkeys so we can suppress them during capture
    bool anyCapture = m_cfgStartRecCapture || m_cfgStopRecCapture
                   || m_cfgStartAllCapture || m_cfgStopAllCapture
                   || m_cfgPauseAllCapture || m_cfgResumeAllCapture
                   || m_wfHkStartCapture || m_wfHkStopCapture
                   || m_wfHkPauseCapture || m_wfHkResumeCapture
                   || m_recOverlay.IsHotkeyCapturing();

    // Only dispatch OS hotkey callbacks when no hotkey capture is in progress
    if (!anyCapture) m_engine.PollHotkeys();
    PollTrayActions();

    // Per-workflow software hotkeys — only when window is focused and not capturing
    {
        ImGuiIO& kio = ImGui::GetIO();
        if (!kio.WantTextInput && !anyCapture) {
            for (auto& wf : m_config.workflows) {
                if (!wf.hotkey_start.empty()  && IsHotkeyPressed(wf.hotkey_start))
                    m_engine.StartWorkflow(wf.id);
                if (!wf.hotkey_stop.empty()   && IsHotkeyPressed(wf.hotkey_stop))
                    m_engine.StopWorkflow(wf.id);
                if (!wf.hotkey_pause.empty()  && IsHotkeyPressed(wf.hotkey_pause))
                    m_engine.PauseWorkflow(wf.id);
                if (!wf.hotkey_resume.empty() && IsHotkeyPressed(wf.hotkey_resume))
                    m_engine.ResumeWorkflow(wf.id);
            }
        }
    }

    // Determine if any pick/capture overlay is active — hides the main UI
    bool overlayActive = m_actEditor.IsOverlayActive()
                      || m_trigPickMode != TrigPickMode::None
                      || m_trigSnipStage == TrigSnipStage::Active;

    if (!overlayActive) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGuiWindowFlags mainFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_MenuBar;

        ImGui::Begin("##main", nullptr, mainFlags);

        RenderMenuBar();
        RenderTopBar();

        // Quit confirmation modal
        RenderQuitConfirmModal();

        // Workflow delete confirmation modal
        if (m_pendingConfirmWf) {
            ImGui::OpenPopup("Confirm Delete##wf");
            m_pendingConfirmWf = false;
        }
        if (ImGui::BeginPopupModal("Confirm Delete##wf", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            auto it = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
                [&](auto& w){ return w.id == m_confirmDeleteWfId; });
            const char* name = (it != m_config.workflows.end()) ? it->name.c_str() : "?";
            ImGui::Text("Delete workflow \"%s\"?", name);
            ImGui::Separator();
            if (ImGui::Button("Yes##wfdel", ImVec2(80, 0))) {
                DeleteWorkflow(m_confirmDeleteWfId);
                m_confirmDeleteWfId.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No##wfdel", ImVec2(80, 0))) {
                m_confirmDeleteWfId.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        float leftW = 200.0f;
        ImGui::BeginChild("##left", ImVec2(leftW, 0), true);
        m_wfList.Render(
            m_config.workflows,
            [this](const std::string& id) -> WorkflowStatus {
                if (m_engine.IsStarting(id))        return WorkflowStatus::Starting;
                if (!m_engine.IsRunning(id))        return WorkflowStatus::Idle;
                if (m_engine.IsPaused(id))          return WorkflowStatus::Paused;
                if (m_engine.IsSuspended(id))       return WorkflowStatus::Interrupted;
                if (m_engine.IsWaitingRepeat(id))   return WorkflowStatus::WaitingRepeat;
                return WorkflowStatus::Running;
            },
            m_selectedId);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##right", ImVec2(0, 0), true);
        if (selIt != m_config.workflows.end()) {
            RenderWorkflowPanel(*selIt);
        } else {
            ImGui::TextDisabled("Select a workflow on the left.");
        }
        ImGui::EndChild();

        ImGui::End();
    }

    // Overlays always rendered (floating windows work outside the main window context)
    m_actEditor.RenderPickOverlayIfActive();
    if (m_trigPickMode != TrigPickMode::None) RenderTriggerPickOverlay();
    if (m_trigSnipStage == TrigSnipStage::Active) RenderTriggerSnipOverlay();
    RenderHotkeyConfigWindow();
    RenderAboutDialog();
    m_recOverlay.Render(m_recorder);
}

// ─────────────────────────────────────────────────────────────────────────────

void AppUI::RenderTriggerSnipOverlay() {
    ImGuiIO& io = ImGui::GetIO();

    // Main full-screen transparent capture window (all events + drawing)
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("##trigsnipoverlay", nullptr, flags)) {
        auto* dl  = ImGui::GetWindowDrawList();
        ImVec2 sz = io.DisplaySize;
        ImVec2 mp = io.MousePos;

        // Update drag state
        if (ImGui::IsMouseClicked(0) && !m_trigSnipDragging) {
            m_trigSnipX1 = m_trigSnipX2 = (int)mp.x;
            m_trigSnipY1 = m_trigSnipY2 = (int)mp.y;
            m_trigSnipDragging = true;
        }
        if (m_trigSnipDragging && ImGui::IsMouseDown(0)) {
            m_trigSnipX2 = (int)mp.x;
            m_trigSnipY2 = (int)mp.y;
        }

        int x1 = std::min(m_trigSnipX1, m_trigSnipX2);
        int y1 = std::min(m_trigSnipY1, m_trigSnipY2);
        int x2 = std::max(m_trigSnipX1, m_trigSnipX2);
        int y2 = std::max(m_trigSnipY1, m_trigSnipY2);

        if (m_trigSnipDragging) {
            auto drawDashed = [&](ImVec2 p1, ImVec2 p2) {
                const float dashLen = 8.f, gapLen = 4.f;
                float dx = p2.x-p1.x, dy = p2.y-p1.y;
                float len = sqrtf(dx*dx+dy*dy);
                if (len < 1.f) return;
                float nx = dx/len, ny = dy/len, pos = 0.f; bool on = true;
                while (pos < len) {
                    float end = std::min(pos + (on ? dashLen : gapLen), len);
                    if (on) dl->AddLine(ImVec2(p1.x+nx*pos,p1.y+ny*pos),
                                        ImVec2(p1.x+nx*end,p1.y+ny*end),
                                        IM_COL32(255,255,255,220), 2.f);
                    pos = end; on = !on;
                }
            };
            drawDashed(ImVec2((float)x1,(float)y1), ImVec2((float)x2,(float)y1));
            drawDashed(ImVec2((float)x2,(float)y1), ImVec2((float)x2,(float)y2));
            drawDashed(ImVec2((float)x2,(float)y2), ImVec2((float)x1,(float)y2));
            drawDashed(ImVec2((float)x1,(float)y2), ImVec2((float)x1,(float)y1));

            char sizeLabel[64];
            snprintf(sizeLabel, sizeof(sizeLabel), "%d x %d", x2-x1, y2-y1);
            dl->AddText(ImVec2((float)x1+4,(float)y2+4), IM_COL32(255,255,255,255), sizeLabel);
        } else {
            float mx = mp.x, my = mp.y;
            dl->AddLine(ImVec2(0,my), ImVec2(sz.x,my), IM_COL32(255,255,255,80), 1.f);
            dl->AddLine(ImVec2(mx,0), ImVec2(mx,sz.y), IM_COL32(255,255,255,80), 1.f);
        }

        // Instruction text (bottom-left)
        ImGui::SetCursorPos(ImVec2(8, io.DisplaySize.y - 28.f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,0.3f,1));
        ImGui::Text("Drag to select region. Release to capture. Esc = cancel.");
        ImGui::PopStyleColor();

        // Confirm on mouse release
        if (m_trigSnipDragging && ImGui::IsMouseReleased(0)) {
            m_trigSnipDragging = false;
            int w = x2 - x1, h = y2 - y1;
            if (w > 0 && h > 0 && !m_trigSnipPixels.empty() &&
                m_trigSnipW > 0 && m_trigSnipH > 0 && m_trigPickTarget) {
                m_trigPickTarget->pixel_x1 = x1; m_trigPickTarget->pixel_y1 = y1;
                m_trigPickTarget->pixel_x2 = x2; m_trigPickTarget->pixel_y2 = y2;
                m_trigPickTarget->pixel_sample_w = w;
                m_trigPickTarget->pixel_sample_h = h;
                m_trigPickTarget->pixel_sample.clear();
                m_trigPickTarget->pixel_sample.reserve((size_t)w * h);
                for (int row = y1; row < y2 && row < m_trigSnipH; ++row)
                    for (int col = x1; col < x2 && col < m_trigSnipW; ++col)
                        m_trigPickTarget->pixel_sample.push_back(
                            m_trigSnipPixels[(size_t)row * m_trigSnipW + col]);
                m_trigPickTarget = nullptr;
                m_dirty = true;
                m_trigSampleHash = 0;
            }
            m_trigSnipStage = TrigSnipStage::Done;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_trigSnipDragging = false;
            m_trigSnipStage = TrigSnipStage::Done;
        }
    }
    ImGui::End();

    // Opacity HUD — top-right corner
    {
        ImGuiWindowFlags hudFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 260.f, 8.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        if (ImGui::Begin("##trigsniphud", nullptr, hudFlags)) {
            ImGui::TextDisabled("Overlay opacity");
            float pct = m_config.pick_overlay_opacity * 100.f;
            ImGui::SetNextItemWidth(200.f);
            if (ImGui::SliderFloat("##tsopa", &pct, 5.f, 95.f, "%.0f%%")) {
                m_config.pick_overlay_opacity = pct / 100.f;
                if (m_sdlWindow)
                    SDL_SetWindowOpacity(m_sdlWindow, m_config.pick_overlay_opacity);
                m_dirty = true;
            }
        }
        ImGui::End();
    }
}

void AppUI::RenderQuitConfirmModal() {
    if (m_quitRequested) {
        ImGui::OpenPopup("Unsaved Changes##quit");
        m_quitRequested = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Changes##quit", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes. What would you like to do?");
        ImGui::Separator();
        if (ImGui::Button("Save & Exit", ImVec2(110, 0))) {
            SaveConfig();
            m_shouldQuit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard & Exit", ImVec2(110, 0))) {
            m_shouldQuit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void AppUI::RenderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save Config", "Ctrl+S")) SaveConfig();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::MenuItem("Discard Changes")) DiscardConfig();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && !m_dirty)
            ImGui::SetTooltip("No unsaved changes");

        ImGui::Separator();
        if (ImGui::MenuItem("Minimize to Tray", nullptr, m_config.minimize_to_tray)) {
            m_config.minimize_to_tray = !m_config.minimize_to_tray;
            m_dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When checked, clicking the minimize button sends the window\n"
                              "to the system tray instead of the taskbar");

        if (ImGui::MenuItem("Close to Tray", nullptr, m_config.close_to_tray)) {
            m_config.close_to_tray = !m_config.close_to_tray;
            m_dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When checked, clicking the window's X button minimizes to tray\n"
                              "instead of prompting to exit");

        ImGui::Separator();
        if (ImGui::MenuItem("Single Instance", nullptr, m_config.single_instance)) {
            m_config.single_instance = !m_config.single_instance;
            m_dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When checked, prevents a second copy of the app from launching.\n"
                              "Takes effect on the next launch.");

        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            RequestQuit();

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Workflows")) {
        if (ImGui::MenuItem("Start All"))  m_engine.StartAll();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start all enabled workflows");

        if (ImGui::MenuItem("Stop All"))   m_engine.StopAll();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop all running workflows");

        ImGui::Separator();

        if (ImGui::MenuItem("Pause All"))  m_engine.PauseAll();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause all currently running workflows");

        if (ImGui::MenuItem("Resume All")) m_engine.ResumeAll();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resume all paused workflows");

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Settings")) {
        if (ImGui::MenuItem("Hotkeys...")) m_showHotkeyConfig = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Configure global and record hotkeys");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About...")) m_showAbout = true;
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void AppUI::RenderTopBar() {
    bool anyRunning = m_engine.AnyRunning();
    bool anyPaused  = m_engine.AnyPaused();

    if (ImGui::Button(">> Start All")) m_engine.StartAll();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start all enabled workflows");
    ImGui::SameLine();
    if (ImGui::Button("[Stop All]"))  m_engine.StopAll();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop all running workflows");
    ImGui::SameLine();

    ImGui::BeginDisabled(!anyRunning || anyPaused);
    if (ImGui::Button("|| Pause All")) m_engine.PauseAll();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Pause all running workflows (they stay alive but stop executing)");
    ImGui::SameLine();

    ImGui::BeginDisabled(!anyPaused);
    if (ImGui::Button("> Resume All")) m_engine.ResumeAll();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Resume all paused workflows");
    ImGui::SameLine();

    ImGui::Separator(); ImGui::SameLine();

    if (m_dirty) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.7f,0,1));
        ImGui::Text("*");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unsaved changes");
        ImGui::SameLine();
        if (ImGui::SmallButton("Save")) SaveConfig();
        ImGui::SameLine();
        if (ImGui::SmallButton("Discard")) DiscardConfig();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reload config from disk, discarding all changes");
    }

    ImGui::SameLine(ImGui::GetContentRegionMax().x - 140.0f);
    const char* statusText;
    ImVec4 statusColor;
    if (anyRunning && anyPaused) {
        statusText  = "PAUSED";
        statusColor = ImVec4(1.f, 0.7f, 0.f, 1.f);
    } else if (anyRunning) {
        statusText  = "RUNNING";
        statusColor = ImVec4(0.2f, 0.9f, 0.2f, 1.f);
    } else {
        statusText  = "IDLE";
        statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.f);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
    ImGui::Text("Status: %s", statusText);
    ImGui::PopStyleColor();

    ImGui::Separator();
}

void AppUI::RenderWorkflowPanel(Workflow& wf) {
    char nameBuf[128]{};
    strncpy(nameBuf, wf.name.c_str(), sizeof(nameBuf)-1);
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("##wfname", nameBuf, sizeof(nameBuf)))
        { wf.name = nameBuf; m_dirty = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Enabled", &wf.enabled)) m_dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable or disable this workflow");

    bool running      = m_engine.IsRunning(wf.id);
    bool paused       = m_engine.IsPaused(wf.id);
    bool suspended    = m_engine.IsSuspended(wf.id);
    bool waiting      = m_engine.IsWaitingRepeat(wf.id);
    bool starting     = m_engine.IsStarting(wf.id);
    ImGui::SameLine();
    if (!running && !starting) {
        if (ImGui::Button(">> Start")) m_engine.StartWorkflow(wf.id);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start this workflow now");
    } else if (starting) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.7f, 1.f));
        if (ImGui::Button("[Cancel]")) m_engine.StopWorkflow(wf.id);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cancel the pending start");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f,0.1f,0.1f,1.f));
        if (ImGui::Button("[Stop]"))  m_engine.StopWorkflow(wf.id);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop this workflow");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (!paused) {
            if (ImGui::Button("||")) m_engine.PauseWorkflow(wf.id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause this workflow");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f,0.5f,0.8f,1.f));
            ImGui::PushStyleColor(ImGuiCol_Text,   ImVec4(1.f,1.f,0.3f,1.f));
            if (ImGui::Button(">")) m_engine.ResumeWorkflow(wf.id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resume this paused workflow");
            ImGui::PopStyleColor(2);
        }
    }

    ImGui::SameLine(ImGui::GetContentRegionMax().x - 200.0f);
    const char* statusText;
    ImVec4 statusColor;
    if (starting) {
        statusText  = "STARTING";
        statusColor = ImVec4(0.3f, 0.8f, 1.0f, 1.f);
    } else if (paused) {
        statusText  = "PAUSED";
        statusColor = ImVec4(1.f, 0.85f, 0.f, 1.f);
    } else if (suspended) {
        statusText  = "INTERRUPTED";
        statusColor = ImVec4(1.0f, 0.55f, 0.1f, 1.f);
    } else if (waiting) {
        statusText  = "WAITING";
        statusColor = ImVec4(0.4f, 0.9f, 0.7f, 1.f);
    } else if (running) {
        statusText  = "RUNNING";
        statusColor = ImVec4(0.2f, 0.9f, 0.2f, 1.f);
    } else {
        statusText  = "IDLE";
        statusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.f);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
    ImGui::Text("Workflow Status: %s", statusText);
    ImGui::PopStyleColor();

    ImGui::Separator();
    RenderWindowTargetEditor(wf.window);
    ImGui::Separator();
    RenderTriggerEditor(wf.trigger, wf.id);
    ImGui::Separator();

    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Repeat interval (ms)", &wf.repeat_interval_ms)) {
        wf.repeat_interval_ms = std::max(100, wf.repeat_interval_ms);
        m_engine.UpdateRepeatInterval(wf.id, wf.repeat_interval_ms);
        m_dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Wait this many ms between each run of the workflow");
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Repeat count (0=inf)", &wf.repeat_count))
        { wf.repeat_count = std::max(0, wf.repeat_count); m_dirty = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Number of times to run (0 = repeat forever until stopped)");

    if (ImGui::Checkbox("Smart detection", &wf.smart_detection)) m_dirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Automatically pause the workflow when user activity is detected\n"
            "(mouse movement or key press), then resume after the idle period.\n"
            "Useful to avoid conflicts when you need to use the computer yourself.");
    if (wf.smart_detection) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputInt("Idle ms##sd", &wf.smart_detection_idle_ms))
            { wf.smart_detection_idle_ms = std::max(100, wf.smart_detection_idle_ms); m_dirty = true; }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Resume the workflow after this many ms of user inactivity");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        if (ImGui::InputInt("Start delay ms##sd", &wf.smart_detection_start_delay_ms))
            { wf.smart_detection_start_delay_ms = std::max(100, wf.smart_detection_start_delay_ms); m_dirty = true; }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Wait this many ms of user inactivity before the workflow starts");
    }

    ImGui::Separator();
    RenderWorkflowHotkeys(wf);
    ImGui::Separator();

    // [Rec] — open overlay (filter config + manual start); [Rec!] — start recording directly
    if (!m_recorder.IsRecording() && !m_recOverlay.IsOpen()) {
        if (ImGui::Button("[Rec]"))
            m_recOverlay.Open();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open recording window to configure filters, then start recording");
        ImGui::SameLine();
        if (ImGui::Button("[Rec!]"))
            m_recorder.Start();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Start recording immediately (uses current filter settings)");
    } else if (m_recorder.IsRecording()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.35f, 0.35f, 1.f));
        ImGui::Text("[Recording...]");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Recording in progress — use the overlay panel to stop");
    }

    ImGui::Separator();
    m_actEditor.Render(wf, m_engine.CurrentActivityIndex(wf.id));
}

void AppUI::RenderWindowTargetEditor(WindowTarget& wt) {
    static const char* kWTypes[] = {"Global","By Title","By Class","By Handle"};
    int typeIdx = (int)wt.type;
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("Window##wt", &typeIdx, kWTypes, 4)) {
        wt.type = (WindowTarget::Type)typeIdx;
        m_dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Which window to bring to focus before executing activities.\n"
                          "Global = no targeting (uses whatever window is active).\n"
                          "By Title/Class = focus window matching the given property.");
    ImGui::SameLine();
    m_winPicker.Render(m_engine.WindowFinder(), wt);

    if (wt.type == WindowTarget::Type::ByTitle) {
        static char buf[256]{};
        strncpy(buf, wt.title.c_str(), sizeof(buf)-1);
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputText("Title##wt", buf, sizeof(buf)))
            { wt.title = buf; m_dirty = true; }
    } else if (wt.type == WindowTarget::Type::ByClass) {
        static char buf[256]{};
        strncpy(buf, wt.class_name.c_str(), sizeof(buf)-1);
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputText("Class##wt", buf, sizeof(buf)))
            { wt.class_name = buf; m_dirty = true; }
    } else if (wt.type == WindowTarget::Type::ByHandle) {
        ImGui::Text("Handle: %llu", (unsigned long long)wt.handle);
    }
}

void AppUI::RenderTriggerPickOverlay() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 pos = io.MousePos;
    pos.x += 16.f; pos.y += 16.f;
    if (pos.x + 260 > io.DisplaySize.x) pos.x = io.DisplaySize.x - 260;
    if (pos.y + 100 > io.DisplaySize.y) pos.y = io.DisplaySize.y - 100;
    if (pos.x < 0) pos.x = 0;
    if (pos.y < 0) pos.y = 0;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##trigpickoverlay", nullptr, flags)) {
        int gx = 0, gy = 0;
        SDL_GetGlobalMouseState(&gx, &gy);

        const char* lbl = (m_trigPickMode == TrigPickMode::RangeTo) ? "Pick end corner"
                        : (m_trigPickMode == TrigPickMode::RangeFrom) ? "Pick start corner"
                        : "Pick pixel";
        ImGui::TextColored(ImVec4(1,0.9f,0.3f,1), "%s: %d, %d", lbl, gx, gy);
        if (m_trigPickMode == TrigPickMode::RangeTo && m_trigPickTarget)
            ImGui::Text("Rect: %d x %d",
                std::abs(gx - m_trigPickTarget->pixel_x1)+1,
                std::abs(gy - m_trigPickTarget->pixel_y1)+1);
        ImGui::TextDisabled("Enter = confirm   Esc = cancel");

        auto capturePixel = [](int x, int y) -> uint32_t {
#if defined(_WIN32)
            HDC dc = GetDC(nullptr);
            if (dc) {
                COLORREF c = GetPixel(dc, x, y);
                ReleaseDC(nullptr, dc);
                if (c != CLR_INVALID)
                    return ((uint32_t)GetRValue(c)<<16)|((uint32_t)GetGValue(c)<<8)|(uint32_t)GetBValue(c);
            }
#endif
            return 0;
        };

        bool confirm = ImGui::Button("Capture##trigpick") ||
                       ImGui::IsKeyPressed(ImGuiKey_Enter, false);
        if (confirm && m_trigPickTarget) {
            if (m_trigPickMode == TrigPickMode::Single) {
                m_trigPickTarget->pixel_x1 = gx; m_trigPickTarget->pixel_y1 = gy;
                m_trigPickTarget->pixel_x2 = gx; m_trigPickTarget->pixel_y2 = gy;
                m_trigPickTarget->pixel_sample_w = 1;
                m_trigPickTarget->pixel_sample_h = 1;
                m_trigPickTarget->pixel_sample = { capturePixel(gx, gy) };
                m_trigPickTarget = nullptr;
                m_trigPickMode   = TrigPickMode::None;
                if (m_sdlWindow) SDL_SetWindowOpacity(m_sdlWindow, 1.0f);
                m_dirty = true;
                m_trigSampleHash = 0;
            } else if (m_trigPickMode == TrigPickMode::RangeFrom) {
                m_trigPickTarget->pixel_x1 = gx;
                m_trigPickTarget->pixel_y1 = gy;
                m_trigPickMode = TrigPickMode::RangeTo; // advance to end corner
            } else if (m_trigPickMode == TrigPickMode::RangeTo) {
                int x1 = std::min(m_trigPickTarget->pixel_x1, gx);
                int y1 = std::min(m_trigPickTarget->pixel_y1, gy);
                int x2 = std::max(m_trigPickTarget->pixel_x1, gx);
                int y2 = std::max(m_trigPickTarget->pixel_y1, gy);
                m_trigPickTarget->pixel_x1 = x1; m_trigPickTarget->pixel_y1 = y1;
                m_trigPickTarget->pixel_x2 = x2; m_trigPickTarget->pixel_y2 = y2;
                // Capture sample via pixel checker
                IPixelChecker* checker = m_engine.PixelChecker();
                if (checker) {
                    PixelBuffer buf = checker->CaptureRegion(x1, y1, x2-x1+1, y2-y1+1);
                    if (!buf.Empty()) {
                        m_trigPickTarget->pixel_sample_w = buf.width;
                        m_trigPickTarget->pixel_sample_h = buf.height;
                        m_trigPickTarget->pixel_sample   = std::move(buf.pixels);
                    }
                }
                m_trigPickTarget = nullptr;
                m_trigPickMode   = TrigPickMode::None;
                if (m_sdlWindow) SDL_SetWindowOpacity(m_sdlWindow, 1.0f);
                m_dirty = true;
                m_trigSampleHash = 0;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_trigPickMode   = TrigPickMode::None;
            m_trigPickTarget = nullptr;
            if (m_sdlWindow) SDL_SetWindowOpacity(m_sdlWindow, 1.0f);
        }
    }
    ImGui::End();

    // Opacity HUD — top-right corner
    {
        ImGuiIO& hudIo = ImGui::GetIO();
        ImGuiWindowFlags hudFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos(ImVec2(hudIo.DisplaySize.x - 260.f, 8.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        if (ImGui::Begin("##trigpickhud", nullptr, hudFlags)) {
            ImGui::TextDisabled("Overlay opacity");
            float pct = m_config.pick_overlay_opacity * 100.f;
            ImGui::SetNextItemWidth(200.f);
            if (ImGui::SliderFloat("##tpopa", &pct, 5.f, 95.f, "%.0f%%")) {
                m_config.pick_overlay_opacity = pct / 100.f;
                if (m_sdlWindow)
                    SDL_SetWindowOpacity(m_sdlWindow, m_config.pick_overlay_opacity);
                m_dirty = true;
            }
        }
        ImGui::End();
    }
}

void AppUI::RenderTriggerEditor(StartTrigger& trig, const std::string& wfId) {
    static const char* kTTypes[] = {"Manual","Schedule","Pixel color"};
    int typeIdx = (int)trig.type;
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("Trigger##tr", &typeIdx, kTTypes, 3)) {
        trig.type = (StartTrigger::Type)typeIdx;
        m_dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How this workflow is started:\n"
                          "Manual = click Start or use hotkey\n"
                          "Schedule = run on a cron schedule\n"
                          "Pixel color = run when a screen pixel matches a color");

    if (trig.type == StartTrigger::Type::Schedule) {
        static std::string s_wfId;
        static char s_hh[16] = "*";
        static char s_mm[16] = "*";

        if (wfId != s_wfId) {
            s_wfId = wfId;
            std::string f[5] = {"*","*","*","*","*"};
            std::istringstream ss(trig.cron_expr);
            for (int i = 0; i < 5; ++i) if (!(ss >> f[i])) f[i] = "*";
            strncpy(s_mm, f[0].c_str(), sizeof(s_mm)-1);
            strncpy(s_hh, f[1].c_str(), sizeof(s_hh)-1);
        }

        std::string f[5] = {"*","*","*","*","*"};
        {
            std::istringstream ss(trig.cron_expr);
            for (int i = 0; i < 5; ++i) if (!(ss >> f[i])) f[i] = "*";
        }

        bool changed = false;

        ImGui::Text("HH:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(55);
        if (ImGui::InputText("##hh_tr", s_hh, sizeof(s_hh))) { f[1] = s_hh; changed = true; }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hour (0-23, * = any, */2 = every 2 hours)");

        ImGui::SameLine(0, 12);
        ImGui::Text("MM:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(55);
        if (ImGui::InputText("##mm_tr", s_mm, sizeof(s_mm))) { f[0] = s_mm; changed = true; }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minute (0-59, * = any, */5 = every 5 min)");

        static const char* kDays[32] = {
            "*","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15",
            "16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31"
        };
        static const char* kMonths[13] = {
            "*","Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
        };
        static const char* kDOW[8] = { "*","Sun","Mon","Tue","Wed","Thu","Fri","Sat" };

        int dayIdx = 0;
        for (int i = 1; i <= 31; ++i) if (f[2] == kDays[i]) { dayIdx = i; break; }

        int monIdx = 0;
        if (f[3] != "*") { try { int n=std::stoi(f[3]); if(n>=1&&n<=12) monIdx=n; } catch(...){} }

        int dowIdx = 0;
        if (f[4] != "*") { try { int n=std::stoi(f[4]); if(n>=0&&n<=6) dowIdx=n+1; } catch(...){} }

        ImGui::Text("Day:");  ImGui::SameLine();
        ImGui::SetNextItemWidth(58);
        if (ImGui::Combo("##day_tr", &dayIdx, kDays, 32))
            { f[2] = kDays[dayIdx]; changed = true; }

        ImGui::SameLine(0, 12);
        ImGui::Text("Mon:");  ImGui::SameLine();
        ImGui::SetNextItemWidth(58);
        if (ImGui::Combo("##mon_tr", &monIdx, kMonths, 13))
            { f[3] = monIdx==0 ? "*" : std::to_string(monIdx); changed = true; }

        ImGui::SameLine(0, 12);
        ImGui::Text("DOW:");  ImGui::SameLine();
        ImGui::SetNextItemWidth(58);
        if (ImGui::Combo("##dow_tr", &dowIdx, kDOW, 8))
            { f[4] = dowIdx==0 ? "*" : std::to_string(dowIdx-1); changed = true; }

        if (changed) {
            trig.cron_expr = std::string(f[0])+" "+f[1]+" "+f[2]+" "+f[3]+" "+f[4];
            m_dirty = true;
        }

        ImGui::TextDisabled("cron: %s", trig.cron_expr.c_str());

    } else if (trig.type == StartTrigger::Type::Pixel) {
        if (!ImGui::CollapsingHeader("Pixel trigger config##tr")) return;

        // Capture region (snip tool — hide app, screenshot, overlay)
        if (ImGui::Button("Capture region##tr")) {
            if (m_sdlWindow) {
                SDL_GetWindowPosition(m_sdlWindow, &m_trigSnipOrigX, &m_trigSnipOrigY);
                SDL_GetWindowSize(m_sdlWindow, &m_trigSnipOrigW, &m_trigSnipOrigH);
                SDL_HideWindow(m_sdlWindow);
            }
            m_trigPickTarget  = &trig;
            m_trigSnipStage   = TrigSnipStage::WaitMinimize;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hide the app and drag to capture a screen region");

        ImGui::SameLine();

        // Pick range (two-corner overlay picker)
        if (ImGui::Button("Pick range##tr")) {
            m_trigPickMode   = TrigPickMode::RangeFrom;
            m_trigPickTarget = &trig;
            if (m_sdlWindow)
                SDL_SetWindowOpacity(m_sdlWindow, m_config.pick_overlay_opacity);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click two corners to define the region");

        // Manual coordinates
        ImGui::SetNextItemWidth(90); if (ImGui::InputInt("X1##tr", &trig.pixel_x1)) m_dirty = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90); if (ImGui::InputInt("Y1##tr", &trig.pixel_y1)) m_dirty = true;
        ImGui::SetNextItemWidth(90); if (ImGui::InputInt("X2##tr", &trig.pixel_x2)) m_dirty = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90); if (ImGui::InputInt("Y2##tr", &trig.pixel_y2)) m_dirty = true;

        // Sample info + preview
        if (trig.pixel_sample.empty()) {
            ImGui::TextColored(ImVec4(1.f,0.55f,0.3f,1.f), "Sample: (none)");
        } else {
            ImGui::Text("Sample: %d x %d (%d px)",
                trig.pixel_sample_w, trig.pixel_sample_h,
                trig.pixel_sample_w * trig.pixel_sample_h);
            ImGui::SameLine();
            if (ImGui::Button("Clear##tr")) {
                trig.pixel_sample.clear();
                trig.pixel_sample_w = trig.pixel_sample_h = 0;
                if (m_trigSampleTex) { SDL_DestroyTexture(m_trigSampleTex); m_trigSampleTex = nullptr; }
                m_trigSampleHash = 0;
                m_dirty = true;
            }

            // Rebuild preview texture when sample changes
            SDL_Renderer* renderer = SDL_GetRenderer(m_sdlWindow);
            if (renderer && trig.pixel_sample_w > 0 && trig.pixel_sample_h > 0) {
                size_t hash = (size_t)trig.pixel_sample_w * 100003u
                            + (size_t)trig.pixel_sample_h * 10007u
                            + trig.pixel_sample.size();
                if (hash != m_trigSampleHash || !m_trigSampleTex) {
                    if (m_trigSampleTex) { SDL_DestroyTexture(m_trigSampleTex); m_trigSampleTex = nullptr; }
                    SDL_Texture* tex = SDL_CreateTexture(renderer,
                        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                        trig.pixel_sample_w, trig.pixel_sample_h);
                    if (tex) {
                        std::vector<uint32_t> argb(trig.pixel_sample.size());
                        for (size_t i = 0; i < trig.pixel_sample.size(); ++i)
                            argb[i] = 0xFF000000u | trig.pixel_sample[i];
                        SDL_UpdateTexture(tex, nullptr, argb.data(), trig.pixel_sample_w * 4);
                        m_trigSampleTex  = tex;
                        m_trigSampleHash = hash;
                    }
                }
                if (m_trigSampleTex) {
                    const float maxSz = 64.f;
                    float scale = std::min(maxSz / (float)trig.pixel_sample_w,
                                          maxSz / (float)trig.pixel_sample_h);
                    ImGui::Image((ImTextureID)(intptr_t)m_trigSampleTex,
                        ImVec2((float)trig.pixel_sample_w * scale,
                               (float)trig.pixel_sample_h * scale));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Captured sample (%dx%d)",
                            trig.pixel_sample_w, trig.pixel_sample_h);
                }
            }
        }

        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Tolerance##tr", &trig.pixel_tolerance)) m_dirty = true;
        trig.pixel_tolerance = std::max(0, std::min(255, trig.pixel_tolerance));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Allowed color difference per channel (0 = exact, 255 = any)");

        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Match percent##tr", &trig.pixel_match_percent)) m_dirty = true;
        trig.pixel_match_percent = std::max(0, std::min(100, trig.pixel_match_percent));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimum percentage of pixels that must match to trigger");

        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("Poll interval (ms)##tr", &trig.pixel_poll_ms)) m_dirty = true;
        trig.pixel_poll_ms = std::max(50, trig.pixel_poll_ms);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How often to check the pixel region in milliseconds");
    }
}

void AppUI::AddWorkflow() {
    Workflow wf;
    wf.id   = GenId();
    wf.name = "New Workflow";
    m_config.workflows.push_back(wf);
    m_selectedId = wf.id;
    m_engine.SetWorkflows(m_config.workflows);
    m_dirty = true;
}

void AppUI::DuplicateWorkflow(const std::string& id) {
    auto it = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
                           [&](auto& w){ return w.id == id; });
    if (it == m_config.workflows.end()) return;
    Workflow copy = *it;
    copy.id   = GenId();
    copy.name = copy.name + " (copy)";
    for (auto& a : copy.activities) a.id = GenId();
    m_config.workflows.insert(it + 1, copy);
    m_selectedId = copy.id;
    m_engine.SetWorkflows(m_config.workflows);
    m_dirty = true;
}

void AppUI::DeleteWorkflow(const std::string& id) {
    m_engine.StopWorkflow(id);
    auto it = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
                           [&](auto& w){ return w.id == id; });
    if (it == m_config.workflows.end()) return;
    m_config.workflows.erase(it);
    m_selectedId = m_config.workflows.empty() ? "" : m_config.workflows[0].id;
    m_engine.SetWorkflows(m_config.workflows);
    m_dirty = true;
}

void AppUI::ApplyStartRecordHotkey(const std::string& key) {
    m_engine.SetRecordHotkey(key, [this]() {
        if (m_recOverlay.IsHotkeyCapturing()) return;
        if (!m_recorder.IsRecording()) {
            m_recOverlay.Close();
            m_recorder.Start();
        }
    });
}

void AppUI::ApplyStopRecordHotkey(const std::string& key) {
    m_engine.SetStopRecordHotkey(key, [this]() {
        if (m_recOverlay.IsHotkeyCapturing()) return;
        if (m_recorder.IsRecording()) {
            m_recorder.Stop();
            m_recOverlay.TriggerReview(m_recorder);
        }
    });
}

void AppUI::ApplyStartAllHotkey(const std::string& key)  { m_engine.SetStartAllHotkey(key); }
void AppUI::ApplyStopAllHotkey(const std::string& key)   { m_engine.SetStopAllHotkey(key); }
void AppUI::ApplyPauseAllHotkey(const std::string& key)  { m_engine.SetPauseAllHotkey(key); }
void AppUI::ApplyResumeAllHotkey(const std::string& key) { m_engine.SetResumeAllHotkey(key); }

void AppUI::RenderHotkeyConfigWindow() {
    if (!m_showHotkeyConfig) {
        m_cfgStartRecCapture = m_cfgStopRecCapture = false;
        m_cfgStartAllCapture = m_cfgStopAllCapture = false;
        m_cfgPauseAllCapture = m_cfgResumeAllCapture = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Hotkey Configuration", &m_showHotkeyConfig,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // Helper lambdas to reduce repetition
    auto renderHotkeyRow = [&](const char* id, char* buf, int bufSz,
                                bool& capturing, std::string& cfgKey,
                                std::function<void()> onApply, bool canClear = true)
    {
        if (capturing) {
            ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1), "Press key combination...");
            ImGui::SameLine();
            char cancelId[32];
            snprintf(cancelId, sizeof(cancelId), "Cancel##%s", id);
            if (ImGui::SmallButton(cancelId)) capturing = false;

            std::string captured;
            if (DoCaptureHotkey(capturing, buf, bufSz, captured)) {
                cfgKey = captured;
                if (onApply) onApply();
                m_dirty = true;
            }
        } else {
            ImGui::SetNextItemWidth(130);
            char inputId[32];
            snprintf(inputId, sizeof(inputId), "##hk_%s", id);
            ImGui::InputText(inputId, buf, bufSz, ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            char capId[32];
            snprintf(capId, sizeof(capId), "Capture##%s", id);
            if (ImGui::Button(capId)) capturing = true;
            if (canClear && !cfgKey.empty()) {
                ImGui::SameLine();
                char clrId[32];
                snprintf(clrId, sizeof(clrId), "Clear##%s", id);
                if (ImGui::Button(clrId)) {
                    cfgKey = "";
                    buf[0] = '\0';
                    if (onApply) onApply();
                    m_dirty = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Disable this hotkey");
            }
        }
    };

    // ── Global action hotkeys ─────────────────────────────────────────────────
    ImGui::SeparatorText("Global Action Hotkeys (OS-level, work even when minimized)");
    ImGui::Spacing();

    ImGui::Text("Start All"); ImGui::SameLine(110);
    renderHotkeyRow("sa", m_cfgStartAllBuf, sizeof(m_cfgStartAllBuf),
        m_cfgStartAllCapture, m_config.start_all_hotkey,
        [&]{ ApplyStartAllHotkey(m_config.start_all_hotkey); });

    ImGui::Text("Stop All"); ImGui::SameLine(110);
    renderHotkeyRow("sta", m_cfgStopAllBuf, sizeof(m_cfgStopAllBuf),
        m_cfgStopAllCapture, m_config.stop_all_hotkey,
        [&]{ ApplyStopAllHotkey(m_config.stop_all_hotkey); });

    ImGui::Text("Pause All"); ImGui::SameLine(110);
    renderHotkeyRow("pa", m_cfgPauseAllBuf, sizeof(m_cfgPauseAllBuf),
        m_cfgPauseAllCapture, m_config.pause_all_hotkey,
        [&]{ ApplyPauseAllHotkey(m_config.pause_all_hotkey); });

    ImGui::Text("Resume All"); ImGui::SameLine(110);
    renderHotkeyRow("ra", m_cfgResumeAllBuf, sizeof(m_cfgResumeAllBuf),
        m_cfgResumeAllCapture, m_config.resume_all_hotkey,
        [&]{ ApplyResumeAllHotkey(m_config.resume_all_hotkey); });

    ImGui::Spacing();

    // ── Recording hotkeys ─────────────────────────────────────────────────────
    ImGui::SeparatorText("Recording Hotkeys (OS-level, work even when minimized)");
    ImGui::Spacing();

    ImGui::Text("Start Rec"); ImGui::SameLine(110);
    renderHotkeyRow("srec", m_cfgStartRecBuf, sizeof(m_cfgStartRecBuf),
        m_cfgStartRecCapture, m_config.start_record_hotkey,
        [&]{
            m_recOverlay.SetHotkey(m_config.start_record_hotkey);
            ApplyStartRecordHotkey(m_config.start_record_hotkey);
        });

    ImGui::Text("Stop Rec"); ImGui::SameLine(110);
    renderHotkeyRow("erec", m_cfgStopRecBuf, sizeof(m_cfgStopRecBuf),
        m_cfgStopRecCapture, m_config.stop_record_hotkey,
        [&]{ ApplyStopRecordHotkey(m_config.stop_record_hotkey); });

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("All hotkeys work globally (no administrator rights required).");
    ImGui::TextDisabled("If a key does not respond, another application may own it.");
    ImGui::Spacing();

    if (ImGui::Button("Close", ImVec2(90, 0))) m_showHotkeyConfig = false;

    ImGui::End();
}

void AppUI::RenderWorkflowHotkeys(Workflow& wf) {
    // Re-sync buffers when the selected workflow changes
    if (m_wfHkId != wf.id) {
        m_wfHkId = wf.id;
        strncpy(m_wfHkStartBuf,  wf.hotkey_start.c_str(),  sizeof(m_wfHkStartBuf)-1);
        strncpy(m_wfHkStopBuf,   wf.hotkey_stop.c_str(),   sizeof(m_wfHkStopBuf)-1);
        strncpy(m_wfHkPauseBuf,  wf.hotkey_pause.c_str(),  sizeof(m_wfHkPauseBuf)-1);
        strncpy(m_wfHkResumeBuf, wf.hotkey_resume.c_str(), sizeof(m_wfHkResumeBuf)-1);
        m_wfHkStartCapture = m_wfHkStopCapture = m_wfHkPauseCapture = m_wfHkResumeCapture = false;
    }

    if (!ImGui::CollapsingHeader("Workflow Hotkeys (window must be focused)")) return;

    auto row = [&](const char* label, char* buf, int bufSz, bool& cap, std::string& key) {
        ImGui::Text("%s", label); ImGui::SameLine(90);
        if (cap) {
            ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1), "Press key...");
            ImGui::SameLine();
            char cid[48]; snprintf(cid, sizeof(cid), "Cancel##wfhk%s", label);
            if (ImGui::SmallButton(cid)) cap = false;
            std::string captured;
            if (DoCaptureHotkey(cap, buf, bufSz, captured)) {
                key = captured;
                m_dirty = true;
            }
        } else {
            ImGui::SetNextItemWidth(120);
            char iid[48]; snprintf(iid, sizeof(iid), "##wfhk%s", label);
            ImGui::InputText(iid, buf, bufSz, ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            char capid[48]; snprintf(capid, sizeof(capid), "Capture##wfhk%s", label);
            if (ImGui::SmallButton(capid)) cap = true;
            if (!key.empty()) {
                ImGui::SameLine();
                char clrid[48]; snprintf(clrid, sizeof(clrid), "Clear##wfhk%s", label);
                if (ImGui::SmallButton(clrid)) { key.clear(); buf[0] = '\0'; m_dirty = true; }
            }
        }
    };

    row("Start",  m_wfHkStartBuf,  sizeof(m_wfHkStartBuf),  m_wfHkStartCapture,  wf.hotkey_start);
    row("Stop",   m_wfHkStopBuf,   sizeof(m_wfHkStopBuf),   m_wfHkStopCapture,   wf.hotkey_stop);
    row("Pause",  m_wfHkPauseBuf,  sizeof(m_wfHkPauseBuf),  m_wfHkPauseCapture,  wf.hotkey_pause);
    row("Resume", m_wfHkResumeBuf, sizeof(m_wfHkResumeBuf), m_wfHkResumeCapture, wf.hotkey_resume);
}

void AppUI::RenderAboutDialog() {
    if (!m_showAbout) return;

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowFocus();
    if (!ImGui::Begin("About Aries Automation Tools", &m_showAbout,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // Icon + title side by side
    if (m_iconTexture) {
        ImGui::Image((ImTextureID)(intptr_t)m_iconTexture, ImVec2(64, 64));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.82f, 0.94f, 1.0f));
    ImGui::Text(APP_PRODUCT_NAME_STR);
    ImGui::PopStyleColor();
    ImGui::TextDisabled("Version " APP_VERSION_STR);
    ImGui::Spacing();
    ImGui::TextDisabled("Portable desktop automation for Windows.");
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextWrapped(
        "Define workflows of mouse, keyboard, and wait activities "
        "triggered by timer, cron schedule, or pixel-color events. "
        "Single portable EXE - no installer required.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Credits table
    auto row2 = [](const char* label, const char* value) {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(90);
        ImGui::Text("%s", value);
    };
    row2("Company:", "Aries Inc");
    row2("Author:",  "Cong Quyen Knight");
    row2("Email:",  "quyen19492@gmail.com");
    row2("Year:",   "2026");

    ImGui::Spacing();
    ImGui::TextDisabled("Built with Dear ImGui, SDL2, nlohmann/json.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float btnW = 90.0f;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - btnW) * 0.5f +
                          ImGui::GetCursorPosX());
    if (ImGui::Button("Close", ImVec2(btnW, 0))) m_showAbout = false;

    ImGui::End();
}
