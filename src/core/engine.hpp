#pragma once
#include "workflow.hpp"
#include "scheduler.hpp"
#include "input/input_simulator.hpp"
#include "monitor/activity_monitor.hpp"
#include "window/window_finder.hpp"
#include "window/pixel_checker.hpp"
#include "hotkey/hotkey_manager.hpp"
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include <functional>

class WorkflowEngine {
public:
    WorkflowEngine();
    ~WorkflowEngine();

    void Init();   // call once after config is loaded
    void Shutdown();

    // Workflow management (call from UI thread, not while scheduler running)
    void SetWorkflows(std::vector<Workflow> wfs);
    std::vector<Workflow>& Workflows() { return m_workflows; }

    void StartWorkflow(const std::string& id);
    void StopWorkflow(const std::string& id);
    void StartAll();
    void StopAll();
    void PauseWorkflow(const std::string& id);
    void ResumeWorkflow(const std::string& id);
    void PauseAll();
    void ResumeAll();
    bool IsRunning(const std::string& id) const;
    bool AnyRunning() const;
    bool IsPaused(const std::string& id) const;
    bool AnyPaused() const;
    bool IsGloballyPaused() const { return m_globalPaused.load(); }

    // Global hotkey toggle
    void SetGlobalHotkey(const std::string& key_name);
    // Poll hotkey events — call once per frame from UI thread
    void PollHotkeys();

    // Chain request from RunWorkflowActivity (called from scheduler thread)
    void RequestChain(const std::string& workflow_id);

    // Current activity index for a workflow (for UI status bar)
    int  CurrentActivityIndex(const std::string& id) const;

    IWindowFinder* WindowFinder() { return m_windowFinder.get(); }

    // Called when user wants to trigger a workflow directly (pixel/schedule trigger)
    using TriggerCallback = std::function<void(const std::string& workflow_id)>;
    void SetTriggerCallback(TriggerCallback cb) { m_triggerCb = cb; }

private:
    std::pair<int,int> ResolveCoords(const WindowTarget& wt, int x, int y);
    Scheduler* FindScheduler(const std::string& id);

    std::vector<Workflow>                       m_workflows;
    std::vector<std::unique_ptr<Scheduler>>     m_schedulers;
    std::unique_ptr<IInputSimulator>            m_input;
    std::unique_ptr<IActivityMonitor>           m_monitor;
    std::unique_ptr<IWindowFinder>              m_windowFinder;
    std::unique_ptr<IPixelChecker>              m_pixelChecker;
    std::unique_ptr<IHotkeyManager>             m_hotkey;

    // Smart detection polling thread
    std::thread                                 m_monitorThread;
    std::atomic<bool>                           m_monitorStop{false};
    void MonitorLoop();

    TriggerCallback                             m_triggerCb;
    std::string                                 m_hotkeyName;
    std::atomic<bool>                           m_globalPaused{false};
};
