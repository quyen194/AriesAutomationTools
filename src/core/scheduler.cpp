#include "scheduler.hpp"
#include "input/input_simulator.hpp"
#include "window/pixel_checker.hpp"
#include <cstdlib>
#include <thread>
#include <chrono>
#include <random>
#include <cmath>

// Lazily created singletons shared across all schedulers
static IInputSimulator* g_input   = nullptr;
static IPixelChecker*   g_pixel   = nullptr;

void Scheduler_SetInputSimulator(IInputSimulator* s)  { g_input = s; }
void Scheduler_SetPixelChecker(IPixelChecker* p)      { g_pixel = p; }

// ─────────────────────────────────────────────────────────────────────────────

Scheduler::Scheduler(const Workflow& wf, CoordResolver resolver)
    : m_workflow(wf), m_resolver(std::move(resolver)) {
    m_repeatIntervalMs.store(wf.repeat_interval_ms);
}

Scheduler::~Scheduler() { Stop(); }

void Scheduler::Start() {
    if (m_running.load()) return;
    m_stopFlag = false;
    m_running  = true;
    m_startTimeMs.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    m_thread = std::thread(&Scheduler::Run, this);
}

void Scheduler::Stop() {
    m_stopFlag = true;
    if (m_thread.joinable()) m_thread.join();
    m_running      = false;
    m_currentIndex = -1;
}

void Scheduler::SleepInterruptible(int ms) {
    constexpr int kSlice = 10;
    while (ms > 0 && !IsStopped()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(ms, kSlice)));
        ms -= kSlice;
        // Spin-wait while suspended or user-paused (still check stop flag)
        while ((m_suspended.load() || m_userPaused.load()) && !IsStopped())
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void Scheduler::Run() {
    std::mt19937 rng{std::random_device{}()};
    auto randExtra = [&](int range) -> int {
        if (range <= 0) return 0;
        return std::uniform_int_distribution<int>(0, range)(rng);
    };

    auto resolveCoords = [&](PositionMode mode, int x, int y) -> std::pair<int,int> {
        if (mode == PositionMode::Relative)
            return m_resolver(m_workflow.window, x, y);
        return {x, y};
    };

    int loopsLeft = m_workflow.repeat_count; // 0 = infinite

    while (!IsStopped()) {
        const auto& acts = m_workflow.activities;
        bool skipIteration = false;

        for (int i = 0; i < (int)acts.size() && !IsStopped(); ++i) {
            if (!acts[i].enabled) continue;
            m_currentIndex = i;

            // Spin while suspended or user-paused
            while ((m_suspended.load() || m_userPaused.load()) && !IsStopped())
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (IsStopped()) break;

            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;

                if constexpr (std::is_same_v<T, MouseMoveActivity>) {
                    if (!g_input) return;
                    auto [ax, ay] = resolveCoords(v.pos_mode, v.x, v.y);
                    g_input->MouseMove(ax, ay);
                    SleepInterruptible(v.delay_ms + randExtra(v.delay_rand_ms));

                } else if constexpr (std::is_same_v<T, MouseClickActivity>) {
                    if (!g_input) return;
                    auto [ax, ay] = resolveCoords(v.pos_mode, v.x, v.y);
                    g_input->MouseClick(v.button, ax, ay, v.double_click);
                    SleepInterruptible(v.delay_ms + randExtra(v.delay_rand_ms));

                } else if constexpr (std::is_same_v<T, MouseDragActivity>) {
                    if (!g_input) return;
                    auto [ax0, ay0] = resolveCoords(v.pos_mode, v.from_x, v.from_y);
                    auto [ax1, ay1] = resolveCoords(v.pos_mode, v.to_x,   v.to_y);
                    g_input->MouseDrag(v.button, ax0, ay0, ax1, ay1, v.duration_ms);
                    SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, MouseScrollActivity>) {
                    if (!g_input) return;
                    auto [ax, ay] = resolveCoords(v.pos_mode, v.x, v.y);
                    g_input->MouseScroll(ax, ay, v.delta_x, v.delta_y);
                    SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, KeyPressActivity>) {
                    if (!g_input) return;
                    g_input->KeyPress(v.key, v.modifiers);
                    SleepInterruptible(v.delay_ms + randExtra(v.delay_rand_ms));

                } else if constexpr (std::is_same_v<T, TypeStringActivity>) {
                    if (!g_input) return;
                    g_input->TypeString(v.text, v.delay_between_chars_ms);
                    SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, WaitActivity>) {
                    int dur = v.duration_ms + randExtra(v.random_range_ms);
                    SleepInterruptible(dur);

                } else if constexpr (std::is_same_v<T, PixelCheckActivity>) {
                    if (!g_pixel) return;
                    auto [ax, ay] = resolveCoords(v.pos_mode, v.x, v.y);
                    auto start = std::chrono::steady_clock::now();
                    bool matched = false;
                    while (!IsStopped()) {
                        uint32_t c = g_pixel->GetPixelRGB(ax, ay);
                        if (ColorsMatch(c, v.color_rgb, v.tolerance)) {
                            matched = true;
                            break;
                        }
                        if (v.on_no_match == PixelCheckAction::SkipIteration) {
                            skipIteration = true; break;
                        }
                        if (v.on_no_match == PixelCheckAction::StopWorkflow) {
                            m_stopFlag = true; break;
                        }
                        // Retry
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count();
                        if (v.retry_timeout_ms > 0 && elapsed >= v.retry_timeout_ms) {
                            skipIteration = true; break;
                        }
                        SleepInterruptible(v.retry_interval_ms);
                    }
                    if (matched) SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, PixelRangeCheckActivity>) {
                    if (!g_pixel) return;
                    if (v.sample.empty() || v.sample_w <= 0 || v.sample_h <= 0) {
                        // No reference sample captured — nothing to compare, pass through
                        SleepInterruptible(v.delay_ms);
                        return;
                    }
                    int left = std::min(v.x1, v.x2);
                    int top  = std::min(v.y1, v.y2);
                    auto [ax, ay] = resolveCoords(v.pos_mode, left, top);

                    PixelBuffer sample;
                    sample.width  = v.sample_w;
                    sample.height = v.sample_h;
                    sample.pixels = v.sample;

                    auto start = std::chrono::steady_clock::now();
                    bool matched = false;
                    while (!IsStopped()) {
                        PixelBuffer cur =
                            g_pixel->CaptureRegion(ax, ay, v.sample_w, v.sample_h);
                        if (BuffersMatchPercent(sample, cur, v.tolerance) >= v.match_percent) {
                            matched = true;
                            break;
                        }
                        if (v.on_no_match == PixelCheckAction::SkipIteration) {
                            skipIteration = true; break;
                        }
                        if (v.on_no_match == PixelCheckAction::StopWorkflow) {
                            m_stopFlag = true; break;
                        }
                        // Retry
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count();
                        if (v.retry_timeout_ms > 0 && elapsed >= v.retry_timeout_ms) {
                            skipIteration = true; break;
                        }
                        SleepInterruptible(v.retry_interval_ms);
                    }
                    if (matched) SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, RunWorkflowActivity>) {
                    // Chaining is handled by WorkflowEngine; Scheduler just sleeps
                    SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, SystemActionActivity>) {
                    const char* cmd = nullptr;
#if defined(_WIN32)
                    switch (v.action) {
                        case SystemAction::Shutdown:
                            cmd = v.force ? "shutdown /s /f /t 0" : "shutdown /s /t 0"; break;
                        case SystemAction::Restart:
                            cmd = v.force ? "shutdown /r /f /t 0" : "shutdown /r /t 0"; break;
                        case SystemAction::Sleep:
                            cmd = "rundll32.exe powrprof.dll,SetSuspendState 0,1,0"; break;
                        case SystemAction::Hibernate:
                            cmd = v.force ? "shutdown /h /f" : "shutdown /h"; break;
                        case SystemAction::Lock:
                            cmd = "rundll32.exe user32.dll,LockWorkStation"; break;
                        case SystemAction::LogOut:
                            cmd = v.force ? "shutdown /l /f" : "shutdown /l"; break;
                    }
#elif defined(__APPLE__)
                    switch (v.action) {
                        case SystemAction::Shutdown:
                            cmd = "osascript -e 'tell application \"System Events\" to shut down'"; break;
                        case SystemAction::Restart:
                            cmd = "osascript -e 'tell application \"System Events\" to restart'"; break;
                        case SystemAction::Sleep:
                        case SystemAction::Hibernate:
                            cmd = "pmset sleepnow"; break;
                        case SystemAction::Lock:
                            cmd = "pmset displaysleepnow"; break;
                        case SystemAction::LogOut:
                            cmd = "osascript -e 'tell application \"System Events\" to log out'"; break;
                    }
#else
                    switch (v.action) {
                        case SystemAction::Shutdown:  cmd = "systemctl poweroff";    break;
                        case SystemAction::Restart:   cmd = "systemctl reboot";      break;
                        case SystemAction::Sleep:     cmd = "systemctl suspend";     break;
                        case SystemAction::Hibernate: cmd = "systemctl hibernate";   break;
                        case SystemAction::Lock:      cmd = "loginctl lock-session"; break;
                        case SystemAction::LogOut:    cmd = "loginctl terminate-user $USER"; break;
                    }
#endif
                    if (cmd) std::system(cmd);
                    SleepInterruptible(v.delay_ms);
                }
            }, acts[i].data);

            if (skipIteration) break;
        }

        m_currentIndex = -1;
        if (IsStopped()) break;

        // Repeat count check
        if (m_workflow.repeat_count > 0) {
            if (--loopsLeft <= 0) break;
        }

        m_waitingRepeat.store(true);
        SleepInterruptible(m_repeatIntervalMs.load());
        m_waitingRepeat.store(false);
    }

    m_running = false;
}
