#pragma once
#include "core/engine.hpp"
#include "core/record_engine.hpp"
#include "core/trigger_manager.hpp"
#include "config/config_manager.hpp"
#include "ui/widgets/workflow_list.hpp"
#include "ui/widgets/activity_editor.hpp"
#include "ui/widgets/window_picker.hpp"
#include "ui/widgets/record_overlay.hpp"
#include <string>

class AppUI {
public:
    AppUI();
    ~AppUI() = default;

    void Init(const std::string& config_path);
    void Render();  // call once per frame
    void Shutdown();

private:
    void RenderMenuBar();
    void RenderTopBar();
    void RenderWorkflowPanel(Workflow& wf);
    void RenderTriggerEditor(StartTrigger& trig, const std::string& wfId);
    void RenderWindowTargetEditor(WindowTarget& wt);

    void AddWorkflow();
    void DuplicateWorkflow(const std::string& id);
    void DeleteWorkflow(const std::string& id);
    void SaveConfig();
    void LoadConfig(const std::string& path);
    std::string EnsureId(Workflow& wf);

    AppConfig              m_config;
    std::string            m_configPath;
    std::string            m_selectedId;
    bool                   m_dirty = false;

    WorkflowEngine         m_engine;
    RecordEngine           m_recorder;
    TriggerManager         m_triggers;

    WorkflowListWidget     m_wfList;
    ActivityEditorWidget   m_actEditor;
    WindowPickerWidget     m_winPicker;
    RecordOverlayWidget    m_recOverlay;

    char                   m_hotkeyBuf[32]{};

    // Pixel trigger position picker
    bool          m_trigPickActive = false;
    StartTrigger* m_trigPickTarget = nullptr;
    void RenderTriggerPickOverlay();
};
