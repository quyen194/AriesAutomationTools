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
    : m_workflow(wf), m_resolver(std::move(resolver)) {}

Scheduler::~Scheduler() { Stop(); }

void Scheduler::Start() {
    if (m_running.load()) return;
    m_stopFlag = false;
    m_running  = true;
    m_thread   = std::thread(&Scheduler::Run, this);
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
        // Spin-wait while suspended (still check stop flag)
        while (m_suspended.load() && !IsStopped())
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

            // Spin while suspended
            while (m_suspended.load() && !IsStopped())
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

                } else if constexpr (std::is_same_v<T, RunWorkflowActivity>) {
                    // Chaining is handled by WorkflowEngine; Scheduler just sleeps
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

        SleepInterruptible(m_workflow.repeat_interval_ms);
    }

    m_running = false;
}
