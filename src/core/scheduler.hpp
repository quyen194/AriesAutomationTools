#pragma once
#include "workflow.hpp"
#include <atomic>
#include <thread>
#include <functional>

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

    // Read-only view of current activity index (for UI status)
    int CurrentActivityIndex() const { return m_currentIndex.load(); }

private:
    void Run();
    void SleepInterruptible(int ms);
    bool IsStopped() const { return m_stopFlag.load(); }

    Workflow           m_workflow;
    CoordResolver      m_resolver;
    std::thread        m_thread;
    std::atomic<bool>  m_stopFlag{false};
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_suspended{false};
    std::atomic<int>   m_currentIndex{-1};
};
