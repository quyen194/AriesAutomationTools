#pragma once
#include "workflow.hpp"
#include "window/pixel_checker.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <ctime>
#include <mutex>

// Watches all workflows whose trigger is Schedule or Pixel.
// When a trigger fires, calls the provided callback with the workflow id.
class TriggerManager {
public:
    using FireCallback = std::function<void(const std::string& workflow_id)>;

    TriggerManager() = default;
    ~TriggerManager() { Stop(); }

    void Start(const std::vector<Workflow>& workflows,
               IPixelChecker* pixel,
               FireCallback cb);
    void Stop();

    // Call after workflow list changes
    void Reload(const std::vector<Workflow>& workflows);

private:
    void Run();
    bool CronMatches(const std::string& expr, const std::tm& t) const;
    static int ParseCronField(const std::string& field, int cur, int minV, int maxV);

    std::vector<Workflow>  m_workflows;
    IPixelChecker*         m_pixel = nullptr;
    FireCallback           m_cb;
    std::thread            m_thread;
    std::atomic<bool>      m_stop{false};
    std::mutex             m_mutex;
};
