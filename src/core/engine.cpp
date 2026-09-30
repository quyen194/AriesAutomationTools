#include "engine.hpp"
#include "logger.hpp"
#include <algorithm>
#include <chrono>

// Forward declarations from scheduler.cpp
void Scheduler_SetInputSimulator(IInputSimulator* s);
void Scheduler_SetPixelChecker(IPixelChecker* p);

// ─────────────────────────────────────────────────────────────────────────────

WorkflowEngine::WorkflowEngine()  = default;
WorkflowEngine::~WorkflowEngine() { Shutdown(); }

void WorkflowEngine::Init() {
    m_input        = CreateInputSimulator();
    m_monitor      = CreateActivityMonitor();
    m_windowFinder = CreateWindowFinder();
    m_pixelChecker = CreatePixelChecker();
    m_hotkey       = CreateHotkeyManager();

    Scheduler_SetInputSimulator(m_input.get());
    Scheduler_SetPixelChecker(m_pixelChecker.get());

    m_monitor->Start();

    m_monitorStop = false;
    m_monitorThread = std::thread(&WorkflowEngine::MonitorLoop, this);
    Logger::Debug("Engine", "Initialized (input, monitor, window finder, pixel checker, hotkeys)");
}

void WorkflowEngine::Shutdown() {
    if (m_monitorThread.joinable()) Logger::Debug("Engine", "Shutting down");
    StopAll();
    m_monitorStop = true;
    if (m_monitorThread.joinable()) m_monitorThread.join();
    if (m_monitor) m_monitor->Stop();
}

void WorkflowEngine::SetWorkflows(std::vector<Workflow> wfs) {
    StopAll();

    // Keep variables / termination info of the last run across the rebuild
    struct SavedState { std::map<std::string, std::string> vars; Scheduler::Termination term; };
    std::map<std::string, SavedState> saved;
    for (size_t i = 0; i < m_workflows.size() && i < m_schedulers.size(); ++i)
        saved[m_workflows[i].id] = {m_schedulers[i]->GetVariables(),
                                    m_schedulers[i]->GetTermination()};

    m_workflows  = std::move(wfs);
    m_schedulers.clear();
    for (auto& wf : m_workflows) {
        m_schedulers.push_back(std::make_unique<Scheduler>(
            wf, [this](const WindowTarget& wt, int x, int y) {
                return ResolveCoords(wt, x, y);
            }));
        auto it = saved.find(wf.id);
        if (it != saved.end())
            m_schedulers.back()->RestoreState(std::move(it->second.vars),
                                              std::move(it->second.term));
    }
    std::lock_guard<std::mutex> lk(m_pendingMutex);
    m_pendingStarts.assign(m_workflows.size(), false);
    Logger::Debug("Engine", "Workflows loaded into engine: " + std::to_string(m_workflows.size()));
}

std::pair<int,int> WorkflowEngine::ResolveCoords(const WindowTarget& wt, int x, int y) {
    if (!m_windowFinder) return {x, y};
    std::optional<WindowInfo> info;
    switch (wt.type) {
        case WindowTarget::Type::ByTitle:  info = m_windowFinder->FindByTitle(wt.title);      break;
        case WindowTarget::Type::ByClass:  info = m_windowFinder->FindByClass(wt.class_name); break;
        case WindowTarget::Type::ByHandle: info = m_windowFinder->FindByHandle(wt.handle);    break;
        default: return {x, y};
    }
    if (!info) {
        Logger::Debug("Engine", "Target window not found (" + std::string(
            wt.type == WindowTarget::Type::ByTitle ? "title \"" + wt.title + "\"" :
            wt.type == WindowTarget::Type::ByClass ? "class \"" + wt.class_name + "\"" : "handle")
            + ") - using coordinates as absolute");
        return {x, y};
    }
    return m_windowFinder->ClientToScreen(info->handle, x, y);
}

Scheduler* WorkflowEngine::FindScheduler(const std::string& id) {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i].get();
    return nullptr;
}

void WorkflowEngine::StartWorkflow(const std::string& id) {
    if (m_globalPaused) {
        Logger::Debug("Engine", "Start of [" + id + "] ignored: all workflows are paused");
        return;
    }
    for (size_t i = 0; i < m_workflows.size(); ++i) {
        if (m_workflows[i].id != id) continue;
        auto* s = m_schedulers[i].get();
        if (s->IsRunning()) {
            Logger::Debug(m_workflows[i].name, "Start ignored: already running");
            return;
        }
        if (m_workflows[i].smart_detection) {
            std::lock_guard<std::mutex> lk(m_pendingMutex);
            if (!m_pendingStarts[i])
                Logger::Debug(m_workflows[i].name, "Smart detection: waiting for "
                              + std::to_string(m_workflows[i].smart_detection_start_delay_ms)
                              + "ms of user idle before starting");
            m_pendingStarts[i] = true;
        } else {
            s->Start();
        }
        return;
    }
}

void WorkflowEngine::StopWorkflow(const std::string& id) {
    for (size_t i = 0; i < m_workflows.size(); ++i) {
        if (m_workflows[i].id != id) continue;
        {
            std::lock_guard<std::mutex> lk(m_pendingMutex);
            if (m_pendingStarts[i])
                Logger::Debug(m_workflows[i].name, "Pending start cancelled");
            m_pendingStarts[i] = false;
        }
        if (m_schedulers[i]->IsRunning())
            Logger::Debug(m_workflows[i].name, "Stop requested");
        m_schedulers[i]->Stop();
        return;
    }
}

void WorkflowEngine::StartAll() {
    if (m_globalPaused) {
        Logger::Debug("Engine", "Start All ignored: all workflows are paused");
        return;
    }
    Logger::Debug("Engine", "Start All");
    for (size_t i = 0; i < m_workflows.size(); ++i) {
        if (!m_workflows[i].enabled || m_schedulers[i]->IsRunning()) continue;
        if (m_workflows[i].smart_detection) {
            std::lock_guard<std::mutex> lk(m_pendingMutex);
            m_pendingStarts[i] = true;
        } else {
            m_schedulers[i]->Start();
        }
    }
}

void WorkflowEngine::StopAll() {
    if (AnyRunning()) Logger::Debug("Engine", "Stop All");
    {
        std::lock_guard<std::mutex> lk(m_pendingMutex);
        std::fill(m_pendingStarts.begin(), m_pendingStarts.end(), false);
    }
    for (auto& s : m_schedulers) s->Stop();
}

void WorkflowEngine::PauseWorkflow(const std::string& id) {
    auto* s = FindScheduler(id);
    if (s && s->IsRunning()) {
        Logger::Debug("Engine", "Pause [" + id + "]");
        s->SetUserPaused(true);
    }
}

void WorkflowEngine::ResumeWorkflow(const std::string& id) {
    auto* s = FindScheduler(id);
    if (s) {
        if (s->IsUserPaused()) Logger::Debug("Engine", "Resume [" + id + "]");
        s->SetUserPaused(false);
    }
}

void WorkflowEngine::PauseAll() {
    Logger::Debug("Engine", "Pause All");
    m_globalPaused = true;
    for (auto& s : m_schedulers) if (s->IsRunning()) s->SetUserPaused(true);
}

void WorkflowEngine::ResumeAll() {
    Logger::Debug("Engine", "Resume All");
    m_globalPaused = false;
    for (auto& s : m_schedulers) s->SetUserPaused(false);
}

bool WorkflowEngine::IsPaused(const std::string& id) const {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i]->IsUserPaused();
    return false;
}

bool WorkflowEngine::AnyPaused() const {
    for (auto& s : m_schedulers) if (s->IsUserPaused()) return true;
    return false;
}

bool WorkflowEngine::IsRunning(const std::string& id) const {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i]->IsRunning();
    return false;
}

bool WorkflowEngine::AnyRunning() const {
    for (auto& s : m_schedulers) if (s->IsRunning()) return true;
    return false;
}

bool WorkflowEngine::IsSuspended(const std::string& id) const {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i]->IsSuspended();
    return false;
}

bool WorkflowEngine::IsWaitingRepeat(const std::string& id) const {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i]->IsWaitingRepeat();
    return false;
}

void WorkflowEngine::UpdateRepeatInterval(const std::string& id, int ms) {
    for (size_t i = 0; i < m_workflows.size(); ++i) {
        if (m_workflows[i].id != id) continue;
        if (m_schedulers[i]->IsRunning())
            Logger::Debug(m_workflows[i].name, "Repeat interval updated live: " + std::to_string(ms) + "ms");
        m_workflows[i].repeat_interval_ms = ms;
        m_schedulers[i]->SetRepeatInterval(ms);
        return;
    }
}

bool WorkflowEngine::IsStarting(const std::string& id) const {
    for (size_t i = 0; i < m_workflows.size(); ++i) {
        if (m_workflows[i].id != id) continue;
        std::lock_guard<std::mutex> lk(m_pendingMutex);
        return m_pendingStarts[i];
    }
    return false;
}

int WorkflowEngine::CurrentActivityIndex(const std::string& id) const {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i]->CurrentActivityIndex();
    return -1;
}

std::map<std::string, std::string> WorkflowEngine::GetVariables(const std::string& id) const {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i]->GetVariables();
    return {};
}

bool WorkflowEngine::IsTerminated(const std::string& id) const {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i]->IsTerminated();
    return false;
}

Scheduler::Termination WorkflowEngine::GetTermination(const std::string& id) const {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i]->GetTermination();
    return {};
}

void WorkflowEngine::SetStartAllHotkey(const std::string& key_name) {
    if (!m_hotkey) return;
    if (!m_startAllHotkeyName.empty()) m_hotkey->Unregister(m_startAllHotkeyName);
    m_startAllHotkeyName = key_name;
    if (!key_name.empty())
        m_hotkey->Register(key_name, [this, key_name]() {
            Logger::Info("User", "Hotkey " + key_name + ": Start All");
            StartAll();
        });
}

void WorkflowEngine::SetStopAllHotkey(const std::string& key_name) {
    if (!m_hotkey) return;
    if (!m_stopAllHotkeyName.empty()) m_hotkey->Unregister(m_stopAllHotkeyName);
    m_stopAllHotkeyName = key_name;
    if (!key_name.empty())
        m_hotkey->Register(key_name, [this, key_name]() {
            Logger::Info("User", "Hotkey " + key_name + ": Stop All");
            StopAll();
        });
}

void WorkflowEngine::SetPauseAllHotkey(const std::string& key_name) {
    if (!m_hotkey) return;
    if (!m_pauseAllHotkeyName.empty()) m_hotkey->Unregister(m_pauseAllHotkeyName);
    m_pauseAllHotkeyName = key_name;
    if (!key_name.empty())
        m_hotkey->Register(key_name, [this, key_name]() {
            Logger::Info("User", "Hotkey " + key_name + ": Pause All");
            PauseAll();
        });
}

void WorkflowEngine::SetResumeAllHotkey(const std::string& key_name) {
    if (!m_hotkey) return;
    if (!m_resumeAllHotkeyName.empty()) m_hotkey->Unregister(m_resumeAllHotkeyName);
    m_resumeAllHotkeyName = key_name;
    if (!key_name.empty())
        m_hotkey->Register(key_name, [this, key_name]() {
            Logger::Info("User", "Hotkey " + key_name + ": Resume All");
            ResumeAll();
        });
}

void WorkflowEngine::SetRecordHotkey(const std::string& key_name,
                                       std::function<void()> callback) {
    if (!m_hotkey) return;
    if (!m_recordHotkeyName.empty()) m_hotkey->Unregister(m_recordHotkeyName);
    m_recordHotkeyName = key_name;
    if (!key_name.empty() && callback)
        m_hotkey->Register(key_name, [key_name, cb = std::move(callback)]() {
            Logger::Info("User", "Hotkey " + key_name + ": Start Recording");
            cb();
        });
}

void WorkflowEngine::SetStopRecordHotkey(const std::string& key_name,
                                          std::function<void()> callback) {
    if (!m_hotkey) return;
    if (!m_stopRecordHotkeyName.empty()) m_hotkey->Unregister(m_stopRecordHotkeyName);
    m_stopRecordHotkeyName = key_name;
    if (!key_name.empty() && callback)
        m_hotkey->Register(key_name, [key_name, cb = std::move(callback)]() {
            Logger::Info("User", "Hotkey " + key_name + ": Stop Recording");
            cb();
        });
}

void WorkflowEngine::PollHotkeys() {
    if (m_hotkey) m_hotkey->PollEvents();
}

void WorkflowEngine::RequestChain(const std::string& workflow_id) {
    Logger::Debug("Engine", "Chain request -> [" + workflow_id + "]");
    if (m_triggerCb) m_triggerCb(workflow_id);
}

void WorkflowEngine::MonitorLoop() {
    while (!m_monitorStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        if (!m_monitor) continue;
        uint64_t idle_ms = m_monitor->MillisSinceLastUserActivity();
        bool     locked  = m_monitor->IsSessionLocked();

        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        for (size_t i = 0; i < m_workflows.size(); ++i) {
            auto& wf = m_workflows[i];
            auto& sc = m_schedulers[i];

            // STARTING: pending start waiting for sufficient user idle
            bool pending = false;
            {
                std::lock_guard<std::mutex> lk(m_pendingMutex);
                pending = m_pendingStarts[i];
            }
            bool lockBlocked = locked && wf.smart_detection_pause_on_lock;
            if (pending && !sc->IsRunning()) {
                // Hold the pending start while the session is locked (if enabled)
                if (!lockBlocked && idle_ms >= (uint64_t)wf.smart_detection_start_delay_ms) {
                    {
                        std::lock_guard<std::mutex> lk(m_pendingMutex);
                        m_pendingStarts[i] = false;
                    }
                    Logger::Debug(wf.name, "Smart detection: user idle " + std::to_string(idle_ms)
                                  + "ms -> starting");
                    sc->Start();
                }
                continue;
            }

            // Running smart detection: suspend when user is active after start
            if (!wf.smart_detection || !sc->IsRunning()) continue;

            int64_t last_active_ms = now_ms - (int64_t)idle_ms;
            // Only suspend if user was active AFTER this workflow started.
            // This prevents the "Start" button click itself from immediately
            // triggering a suspension.
            bool active_after_start = last_active_ms > sc->GetStartTimeMs();
            bool suspend = lockBlocked ||
                           (active_after_start && idle_ms < (uint64_t)wf.smart_detection_idle_ms);
            if (suspend != sc->IsSuspended())
                Logger::Debug(wf.name, suspend
                    ? std::string("Smart detection: suspended (") + (lockBlocked ? "session locked" : "user active") + ")"
                    : "Smart detection: resumed (user idle " + std::to_string(idle_ms) + "ms)");
            sc->SetSuspended(suspend);
        }
    }
}
