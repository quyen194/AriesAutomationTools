#include "engine.hpp"
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
}

void WorkflowEngine::Shutdown() {
    StopAll();
    m_monitorStop = true;
    if (m_monitorThread.joinable()) m_monitorThread.join();
    if (m_monitor) m_monitor->Stop();
}

void WorkflowEngine::SetWorkflows(std::vector<Workflow> wfs) {
    StopAll();
    m_workflows  = std::move(wfs);
    m_schedulers.clear();
    for (auto& wf : m_workflows) {
        m_schedulers.push_back(std::make_unique<Scheduler>(
            wf, [this](const WindowTarget& wt, int x, int y) {
                return ResolveCoords(wt, x, y);
            }));
    }
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
    if (!info) return {x, y};
    return m_windowFinder->ClientToScreen(info->handle, x, y);
}

Scheduler* WorkflowEngine::FindScheduler(const std::string& id) {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i].get();
    return nullptr;
}

void WorkflowEngine::StartWorkflow(const std::string& id) {
    if (m_globalPaused) return;
    auto* s = FindScheduler(id);
    if (s && !s->IsRunning()) s->Start();
}

void WorkflowEngine::StopWorkflow(const std::string& id) {
    auto* s = FindScheduler(id);
    if (s) s->Stop();
}

void WorkflowEngine::StartAll() {
    if (m_globalPaused) return;
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].enabled && !m_schedulers[i]->IsRunning())
            m_schedulers[i]->Start();
}

void WorkflowEngine::StopAll() {
    for (auto& s : m_schedulers) s->Stop();
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

int WorkflowEngine::CurrentActivityIndex(const std::string& id) const {
    for (size_t i = 0; i < m_workflows.size(); ++i)
        if (m_workflows[i].id == id) return m_schedulers[i]->CurrentActivityIndex();
    return -1;
}

void WorkflowEngine::SetGlobalHotkey(const std::string& key_name) {
    if (!m_hotkey) return;
    if (!m_hotkeyName.empty()) m_hotkey->Unregister(m_hotkeyName);
    m_hotkeyName = key_name;
    m_hotkey->Register(key_name, [this]() {
        bool paused = !m_globalPaused.load();
        m_globalPaused = paused;
        for (auto& s : m_schedulers) s->SetSuspended(paused);
    });
}

void WorkflowEngine::PollHotkeys() {
    if (m_hotkey) m_hotkey->PollEvents();
}

void WorkflowEngine::RequestChain(const std::string& workflow_id) {
    if (m_triggerCb) m_triggerCb(workflow_id);
}

void WorkflowEngine::MonitorLoop() {
    while (!m_monitorStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        for (size_t i = 0; i < m_workflows.size(); ++i) {
            auto& wf = m_workflows[i];
            auto& sc = m_schedulers[i];
            if (!wf.smart_detection || !sc->IsRunning()) continue;

            uint64_t idle_ms = m_monitor->MillisSinceLastUserActivity();
            // Compute when the last input event occurred (absolute time)
            int64_t last_active_ms = now_ms - (int64_t)idle_ms;
            // Only suspend if user was active AFTER this workflow started.
            // This prevents the "Start" button click itself from immediately
            // triggering a suspension.
            bool active_after_start = last_active_ms > sc->GetStartTimeMs();
            sc->SetSuspended(active_after_start && idle_ms < (uint64_t)wf.smart_detection_idle_ms);
        }
    }
}
