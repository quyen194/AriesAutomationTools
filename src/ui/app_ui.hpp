#pragma once
#include "core/engine.hpp"
#include "core/record_engine.hpp"
#include "core/trigger_manager.hpp"
#include "config/config_manager.hpp"
#include "ui/widgets/workflow_list.hpp"
#include "ui/widgets/activity_editor.hpp"
#include "ui/widgets/window_picker.hpp"
#include "ui/widgets/record_overlay.hpp"
#include "ui/tray_manager.hpp"
#include <string>

struct SDL_Window;

class AppUI {
public:
    AppUI();
    ~AppUI() = default;

    void Init(const std::string& config_path, SDL_Window* sdlWindow);
    void Render();
    void Shutdown();

    // Returns true if caller should quit the main loop immediately (no dirty check needed)
    bool RequestQuit();
    bool ShouldQuit() const { return m_shouldQuit; }

    // Called from main loop when SDL minimize event fires
    void OnWindowMinimized();

private:
    void RenderMenuBar();
    void RenderTopBar();
    void RenderWorkflowPanel(Workflow& wf);
    void RenderWorkflowHotkeys(Workflow& wf);
    void RenderTriggerEditor(StartTrigger& trig, const std::string& wfId);
    void RenderWindowTargetEditor(WindowTarget& wt);
    void RenderQuitConfirmModal();
    void RenderHotkeyConfigWindow();

    void AddWorkflow();
    void DuplicateWorkflow(const std::string& id);
    void DeleteWorkflow(const std::string& id);
    void SaveConfig();
    void DiscardConfig();
    void LoadConfig(const std::string& path);
    std::string EnsureId(Workflow& wf);

    // Apply OS-level hotkeys via the engine.
    void ApplyStartRecordHotkey(const std::string& key);
    void ApplyStopRecordHotkey(const std::string& key);
    void ApplyStartAllHotkey(const std::string& key);
    void ApplyStopAllHotkey(const std::string& key);
    void ApplyPauseAllHotkey(const std::string& key);
    void ApplyResumeAllHotkey(const std::string& key);

    void MinimizeToTray();
    void RestoreFromTray();
    void PollTrayActions();
    void UpdateTrayWorkflows();
    void UpdateTrayIcon();

    AppConfig              m_config;
    std::string            m_configPath;
    std::string            m_selectedId;
    bool                   m_dirty = false;

    SDL_Window*            m_sdlWindow  = nullptr;
    bool                   m_windowVisible = true;

    WorkflowEngine         m_engine;
    RecordEngine           m_recorder;
    TriggerManager         m_triggers;

    WorkflowListWidget     m_wfList;
    ActivityEditorWidget   m_actEditor;
    WindowPickerWidget     m_winPicker;
    RecordOverlayWidget    m_recOverlay;

    TrayManager            m_tray;

    // ── Hotkey Configuration window ───────────────────────────────────────────
    bool                   m_showHotkeyConfig = false;

    // Start / stop recording hotkeys
    char                   m_cfgStartRecBuf[64]{};
    bool                   m_cfgStartRecCapture = false;
    char                   m_cfgStopRecBuf[64]{};
    bool                   m_cfgStopRecCapture  = false;

    // Global action hotkeys
    char                   m_cfgStartAllBuf[64]{};
    bool                   m_cfgStartAllCapture = false;
    char                   m_cfgStopAllBuf[64]{};
    bool                   m_cfgStopAllCapture  = false;
    char                   m_cfgPauseAllBuf[64]{};
    bool                   m_cfgPauseAllCapture = false;
    char                   m_cfgResumeAllBuf[64]{};
    bool                   m_cfgResumeAllCapture = false;

    // ── Per-workflow hotkeys (in workflow panel) ───────────────────────────────
    std::string            m_wfHkId;            // tracks which workflow's buffers are loaded
    char                   m_wfHkStartBuf[64]{};
    bool                   m_wfHkStartCapture  = false;
    char                   m_wfHkStopBuf[64]{};
    bool                   m_wfHkStopCapture   = false;
    char                   m_wfHkPauseBuf[64]{};
    bool                   m_wfHkPauseCapture  = false;
    char                   m_wfHkResumeBuf[64]{};
    bool                   m_wfHkResumeCapture = false;

    // ── Pixel trigger position picker ─────────────────────────────────────────
    bool          m_trigPickActive = false;
    StartTrigger* m_trigPickTarget = nullptr;
    void RenderTriggerPickOverlay();

    // Workflow delete confirmation
    std::string   m_confirmDeleteWfId;
    bool          m_pendingConfirmWf = false;

    // Quit confirmation
    bool          m_quitRequested  = false;
    bool          m_shouldQuit     = false;

    // Tray icon animation
    uint32_t      m_animLastTick = 0;
    int           m_animFrame    = 0;
    bool          m_animActive   = false;
};
