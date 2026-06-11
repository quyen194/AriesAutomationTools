#pragma once
#include "workflow.hpp"
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>

// Runs a single Workflow's activity sequence on a background thread.
// Call Start() to begin; Stop() to interrupt and join.
class Scheduler {
public:
    using CoordResolver = std::function<std::pair<int,int>(const WindowTarget&, int, int)>;

    explicit Scheduler(const Workflow& wf,
                       CoordResolver   resolver);
    ~Scheduler();

    void Start();
    void Stop();    // blocks until thread exits
    bool IsRunning() const { return m_running.load(); }

    void SetSuspended(bool s) { m_suspended.store(s); }
    bool IsSuspended()  const { return m_suspended.load(); }

    // User-initiated pause (distinct from smart-detection suspend)
    void SetUserPaused(bool p) { m_userPaused.store(p); }
    bool IsUserPaused()  const { return m_userPaused.load(); }

    // Live-update repeat interval without restarting the scheduler
    void SetRepeatInterval(int ms) { m_repeatIntervalMs.store(ms); }

    // True while sleeping between repetitions (distinct from smart-detection suspend)
    bool IsWaitingRepeat() const { return m_waitingRepeat.load(); }

    // Milliseconds since epoch when Start() was last called (0 if never started)
    int64_t GetStartTimeMs() const { return m_startTimeMs.load(); }

    // Read-only view of current activity index (for UI status)
    int CurrentActivityIndex() const { return m_currentIndex.load(); }

private:
    void Run();
    void SleepInterruptible(int ms);
    bool IsStopped() const { return m_stopFlag.load(); }

    Workflow           m_workflow;
    CoordResolver      m_resolver;
    std::thread        m_thread;
    std::atomic<bool>    m_stopFlag{false};
    std::atomic<bool>    m_running{false};
    std::atomic<bool>    m_suspended{false};
    std::atomic<bool>    m_userPaused{false};
    std::atomic<bool>    m_waitingRepeat{false};
    std::atomic<int>     m_currentIndex{-1};
    std::atomic<int>     m_repeatIntervalMs{0};
    std::atomic<int64_t> m_startTimeMs{0};
};
