#include "trigger_manager.hpp"
#include "window/pixel_checker.hpp"
#include <sstream>
#include <chrono>
#include <mutex>
#include <ctime>

void TriggerManager::Start(const std::vector<Workflow>& workflows,
                            IPixelChecker* pixel,
                            FireCallback cb) {
    m_workflows = workflows;
    m_pixel     = pixel;
    m_cb        = cb;
    m_stop      = false;
    m_thread    = std::thread(&TriggerManager::Run, this);
}

void TriggerManager::Stop() {
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
}

void TriggerManager::Reload(const std::vector<Workflow>& workflows) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_workflows = workflows;
}

void TriggerManager::Run() {
    while (!m_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::vector<Workflow> wfs;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            wfs = m_workflows;
        }

        std::time_t now_t = std::time(nullptr);
        std::tm now_tm;
#if defined(_WIN32)
        localtime_s(&now_tm, &now_t);
#else
        localtime_r(&now_t, &now_tm);
#endif

        for (auto& wf : wfs) {
            if (!wf.enabled) continue;

            auto& trig = wf.trigger;
            if (trig.type == StartTrigger::Type::Schedule) {
                if (CronMatches(trig.cron_expr, now_tm))
                    if (m_cb) m_cb(wf.id);

            } else if (trig.type == StartTrigger::Type::Pixel) {
                if (m_pixel) {
                    uint32_t c = m_pixel->GetPixelRGB(trig.pixel_x, trig.pixel_y);
                    if (ColorsMatch(c, trig.pixel_color, trig.pixel_tolerance))
                        if (m_cb) m_cb(wf.id);
                }
            }
        }
    }
}

// ── Simplified cron matching ──────────────────────────────────────────────────
// Supports: "*", "*/N", "N" for each of the 5 fields: min hour dom mon dow

int TriggerManager::ParseCronField(const std::string& field, int cur, int minV, int maxV) {
    (void)minV; (void)maxV;
    if (field == "*") return 0; // always match
    if (field.substr(0, 2) == "*/") {
        int step = std::stoi(field.substr(2));
        return (step > 0 && cur % step == 0) ? 0 : 1;
    }
    return (std::stoi(field) == cur) ? 0 : 1;
}

bool TriggerManager::CronMatches(const std::string& expr, const std::tm& t) const {
    // Parse 5-field cron: "min hour dom mon dow"
    std::istringstream ss(expr);
    std::string fields[5];
    for (int i = 0; i < 5; ++i) { if (!(ss >> fields[i])) fields[i] = "*"; }

    bool min_ok  = ParseCronField(fields[0], t.tm_min,  0, 59) == 0;
    bool hour_ok = ParseCronField(fields[1], t.tm_hour, 0, 23) == 0;
    bool dom_ok  = ParseCronField(fields[2], t.tm_mday, 1, 31) == 0;
    bool mon_ok  = ParseCronField(fields[3], t.tm_mon+1,1, 12) == 0;
    bool dow_ok  = ParseCronField(fields[4], t.tm_wday, 0,  6) == 0;

    return min_ok && hour_ok && dom_ok && mon_ok && dow_ok;
}
