#include "scheduler.hpp"
#include "input/input_simulator.hpp"
#include "window/pixel_checker.hpp"
#include <cstdlib>
#include <thread>
#include <chrono>
#include <random>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <sstream>

// Lazily created singletons shared across all schedulers
static IInputSimulator* g_input   = nullptr;
static IPixelChecker*   g_pixel   = nullptr;

void Scheduler_SetInputSimulator(IInputSimulator* s)  { g_input = s; }
void Scheduler_SetPixelChecker(IPixelChecker* p)      { g_pixel = p; }

// ── Flow control signals for the recursive activity runner ────────────────────

enum class FlowSignal { Continue, SkipIter, Stop };

// ── Runtime variable helpers ──────────────────────────────────────────────────

static std::string ResolveValue(const std::string& expr, bool is_var,
    const std::unordered_map<std::string,std::string>& vars) {
    if (is_var) {
        auto it = vars.find(expr);
        return it != vars.end() ? it->second : "";
    }
    return expr;
}

static bool EvalCondition(const Condition& c,
    const std::unordered_map<std::string,std::string>& vars) {
    std::string lv = ResolveValue(c.lhs, c.lhs_is_var, vars);
    std::string rv = ResolveValue(c.rhs, c.rhs_is_var, vars);

    bool numericOk = false;
    double lNum = 0.0, rNum = 0.0;
    try { lNum = std::stod(lv); rNum = std::stod(rv); numericOk = true; }
    catch (...) {}

    switch (c.op) {
        case ConditionOp::Eq:       return numericOk ? (lNum == rNum) : (lv == rv);
        case ConditionOp::NEq:      return numericOk ? (lNum != rNum) : (lv != rv);
        case ConditionOp::Gt:       return numericOk && (lNum > rNum);
        case ConditionOp::Lt:       return numericOk && (lNum < rNum);
        case ConditionOp::GtEq:     return numericOk && (lNum >= rNum);
        case ConditionOp::LtEq:     return numericOk && (lNum <= rNum);
        case ConditionOp::Contains: return lv.find(rv) != std::string::npos;
        default:                    return false;
    }
}

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

    int loopsLeft = m_workflow.repeat_count;

    // Runtime variables — cleared at the start of each outer-loop iteration
    std::unordered_map<std::string,std::string> variables;

    // Recursive activity runner.
    // updateIndex=true → updates m_currentIndex per item (top-level call only).
    std::function<FlowSignal(const std::vector<Activity>&,
                              std::unordered_set<std::string>&,
                              bool)> runActivities;

    runActivities = [&](const std::vector<Activity>& acts,
                        std::unordered_set<std::string>& calledIds,
                        bool updateIndex) -> FlowSignal {
        bool skipIteration = false;

        for (int i = 0; i < (int)acts.size() && !IsStopped(); ++i) {
            if (!acts[i].enabled) continue;
            if (updateIndex) m_currentIndex.store(i);

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
                    SleepInterruptible(v.duration_ms + randExtra(v.random_range_ms));

                } else if constexpr (std::is_same_v<T, PixelCheckActivity>) {
                    if (!g_pixel) return;
                    auto [ax, ay] = resolveCoords(v.pos_mode, v.x, v.y);
                    auto start = std::chrono::steady_clock::now();
                    bool matched = false;
                    while (!IsStopped()) {
                        uint32_t c = g_pixel->GetPixelRGB(ax, ay);
                        if (ColorsMatch(c, v.color_rgb, v.tolerance)) {
                            matched = true; break;
                        }
                        if (v.on_no_match == PixelCheckAction::SkipIteration) {
                            skipIteration = true; return;
                        }
                        if (v.on_no_match == PixelCheckAction::StopWorkflow) {
                            m_stopFlag = true; return;
                        }
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count();
                        if (v.retry_timeout_ms > 0 && elapsed >= v.retry_timeout_ms) {
                            skipIteration = true; return;
                        }
                        SleepInterruptible(v.retry_interval_ms);
                    }
                    if (matched) SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, PixelRangeCheckActivity>) {
                    if (!g_pixel) { SleepInterruptible(v.delay_ms); return; }
                    if (v.sample.empty() || v.sample_w <= 0 || v.sample_h <= 0) {
                        SleepInterruptible(v.delay_ms); return;
                    }
                    int left = std::min(v.x1, v.x2);
                    int top  = std::min(v.y1, v.y2);
                    auto [ax, ay] = resolveCoords(v.pos_mode, left, top);

                    PixelBuffer sample;
                    sample.width  = v.sample_w;
                    sample.height = v.sample_h;
                    sample.pixels = v.sample;

                    bool matched = false;
                    int elapsed = 0;
                    do {
                        PixelBuffer cur = g_pixel->CaptureRegion(ax, ay, v.sample_w, v.sample_h);
                        if (BuffersMatchPercent(sample, cur, v.tolerance) >= v.match_percent) {
                            matched = true; break;
                        }
                        if (v.retry_timeout_ms == 0) break;
                        SleepInterruptible(v.retry_interval_ms);
                        elapsed += v.retry_interval_ms;
                    } while (elapsed < v.retry_timeout_ms && !IsStopped());

                    auto& branch = matched ? v.match_body : v.no_match_body;
                    if (branch && !branch->empty()) {
                        auto sig = runActivities(*branch, calledIds, false);
                        if (sig == FlowSignal::Stop) { skipIteration = true; return; }
                    }
                    SleepInterruptible(v.delay_ms);

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

                } else if constexpr (std::is_same_v<T, RunActivityActivity>) {
                    if (calledIds.count(v.activity_id)) { SleepInterruptible(v.delay_ms); return; }
                    for (const auto& a : m_workflow.activities) {
                        if (a.id == v.activity_id && a.enabled) {
                            calledIds.insert(v.activity_id);
                            std::vector<Activity> one = {a};
                            runActivities(one, calledIds, false);
                            calledIds.erase(v.activity_id);
                            break;
                        }
                    }
                    SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, SetVariableActivity>) {
                    switch (v.op) {
                        case VarOp::Set:
                            variables[v.name] = v.value;
                            break;
                        case VarOp::Increment: {
                            int cur = 0;
                            if (variables.count(v.name))
                                try { cur = std::stoi(variables[v.name]); } catch (...) {}
                            variables[v.name] = std::to_string(cur + v.step);
                            break;
                        }
                        case VarOp::Decrement: {
                            int cur = 0;
                            if (variables.count(v.name))
                                try { cur = std::stoi(variables[v.name]); } catch (...) {}
                            variables[v.name] = std::to_string(cur - v.step);
                            break;
                        }
                        case VarOp::Random: {
                            std::uniform_int_distribution<int> d(v.rand_min, v.rand_max);
                            variables[v.name] = std::to_string(d(rng));
                            break;
                        }
                    }
                    SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, LoopActivity>) {
                    if (!v.body) { SleepInterruptible(v.delay_ms); return; }
                    int remaining = v.count;  // 0 = infinite
                    int iter = 1;
                    while (!IsStopped() && (remaining == 0 || remaining-- > 0)) {
                        if (!v.iter_var.empty())
                            variables[v.iter_var] = std::to_string(iter);
                        auto sig = runActivities(*v.body, calledIds, false);
                        if (sig == FlowSignal::Stop) { skipIteration = true; return; }
                        // SkipIter → skip this loop body iteration, continue outer loop
                        ++iter;
                    }
                    SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, IfActivity>) {
                    bool condTrue = EvalCondition(v.cond, variables);
                    auto& branch = condTrue ? v.then_body : v.else_body;
                    if (branch && !branch->empty()) {
                        auto sig = runActivities(*branch, calledIds, false);
                        if (sig == FlowSignal::Stop) { skipIteration = true; return; }
                    }
                    SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, SwitchActivity>) {
                    std::string val = variables.count(v.var_name)
                                    ? variables.at(v.var_name) : "";
                    bool matched = false;
                    for (const auto& sc : v.cases) {
                        if (sc.value == val && sc.body) {
                            auto sig = runActivities(*sc.body, calledIds, false);
                            if (sig == FlowSignal::Stop) { skipIteration = true; return; }
                            matched = true; break;
                        }
                    }
                    if (!matched && v.default_body && !v.default_body->empty()) {
                        auto sig = runActivities(*v.default_body, calledIds, false);
                        if (sig == FlowSignal::Stop) { skipIteration = true; return; }
                    }
                    SleepInterruptible(v.delay_ms);
                }

            }, acts[i].data);

            if (skipIteration) break;
        }

        if (IsStopped()) return FlowSignal::Stop;
        return skipIteration ? FlowSignal::SkipIter : FlowSignal::Continue;
    };

    while (!IsStopped()) {
        variables.clear();
        std::unordered_set<std::string> calledIds;

        runActivities(m_workflow.activities, calledIds, true);

        m_currentIndex = -1;
        if (IsStopped()) break;

        if (m_workflow.repeat_count > 0) {
            if (--loopsLeft <= 0) break;
        }

        m_waitingRepeat.store(true);
        SleepInterruptible(m_repeatIntervalMs.load());
        m_waitingRepeat.store(false);
    }

    m_running = false;
}
