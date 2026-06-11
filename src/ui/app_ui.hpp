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

    // Re-registers the record hotkey with a callback; call after any key change.
    void ApplyRecordHotkey(const std::string& key);
    // Pushes the formatted global hotkey label to the tray context menu.
    void UpdateTrayHotkeyLabel();

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

    char                   m_hotkeyBuf[64]{};
    bool                   m_hotkeyCapture = false;

    // Hotkey config window
    bool                   m_showHotkeyConfig  = false;
    char                   m_recHotkeyBuf[64]{};
    bool                   m_cfgGlobalCapture  = false;
    bool                   m_cfgRecCapture     = false;

    // Pixel trigger position picker
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
