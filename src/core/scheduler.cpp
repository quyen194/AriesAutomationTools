#include "scheduler.hpp"
#include "variables.hpp"
#include "logger.hpp"
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

using VarMap = std::map<std::string,std::string>;

static std::string ResolveValue(const std::string& expr, bool is_var, const VarMap& vars) {
    if (is_var) {
        auto it = vars.find(expr);
        return it != vars.end() ? it->second : "";
    }
    return expr;
}

static bool EvalCondition(const Condition& c, const VarMap& vars) {
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

static const char* ActivityTypeName(const ActivityData& d) {
    static const char* kNames[] = {
        "mouse_move", "mouse_click", "mouse_drag", "mouse_scroll", "key_press",
        "type_string", "wait", "pixel_check", "pixel_range_check", "run_workflow",
        "system_action", "run_activity", "set_variable", "loop", "if", "switch",
        "jump", "get_mouse_position", "clear_variables"
    };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == std::variant_size_v<ActivityData>,
                  "ActivityTypeName out of sync with ActivityData");
    return kNames[d.index()];
}

// ─────────────────────────────────────────────────────────────────────────────

Scheduler::Scheduler(const Workflow& wf, CoordResolver resolver)
    : m_workflow(wf), m_resolver(std::move(resolver)) {
    m_repeatIntervalMs.store(wf.repeat_interval_ms);
}

Scheduler::~Scheduler() { Stop(); }

void Scheduler::Start() {
    if (m_running.load()) return;
    // Thread may have finished on its own (repeat count reached / terminated)
    if (m_thread.joinable()) m_thread.join();
    {
        std::lock_guard<std::mutex> lk(m_stateMutex);
        m_vars.clear();
        m_termination = Termination{};
    }
    m_terminated   = false;
    m_currentIndex = -1;
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

std::map<std::string, std::string> Scheduler::GetVariables() const {
    std::lock_guard<std::mutex> lk(m_stateMutex);
    return m_vars;
}

Scheduler::Termination Scheduler::GetTermination() const {
    std::lock_guard<std::mutex> lk(m_stateMutex);
    return m_termination;
}

void Scheduler::RestoreState(std::map<std::string, std::string> vars, Termination term) {
    std::lock_guard<std::mutex> lk(m_stateMutex);
    m_vars        = std::move(vars);
    m_termination = std::move(term);
    m_terminated  = m_termination.terminated;
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

    // Runtime variables live in m_vars. Only this thread writes them (under
    // m_stateMutex so the UI can snapshot); reads here need no lock.
    const VarMap& variables = m_vars;
    auto setVar = [&](const std::string& name, const std::string& value) {
        if (name.empty()) return;
        std::lock_guard<std::mutex> lk(m_stateMutex);
        m_vars[name] = value;
    };
    auto eraseVar = [&](const std::string& name) {
        std::lock_guard<std::mutex> lk(m_stateMutex);
        m_vars.erase(name);
    };

    // Execution path (activity ids / 1-based step numbers) for error reporting
    std::vector<std::string> idStack;
    std::vector<int>         stepStack;
    std::vector<std::string> typeStack;

    // Stops the workflow with a runtime error at the current step
    auto fail = [&](const std::string& reason) {
        std::string where = "Step ";
        for (size_t k = 0; k < stepStack.size(); ++k)
            where += (k ? " > " : "") + std::to_string(stepStack[k]);
        if (!typeStack.empty()) where += " (" + typeStack.back() + ")";
        std::string msg = where + ": " + reason;
        {
            std::lock_guard<std::mutex> lk(m_stateMutex);
            m_termination.terminated = true;
            m_termination.path       = idStack;
            m_termination.message    = msg;
        }
        m_terminated = true;
        m_stopFlag   = true;
        Logger::Error(m_workflow.name, "TERMINATED - " + msg);
    };

    // Fills `out` with a copy of `a.data` whose variable-bound fields are
    // substituted. On failure calls fail() and returns false.
    auto resolveBindings = [&](const Activity& a, ActivityData& out) -> bool {
        out = a.data;
        std::string err;
        ForEachBindableField(out, [&](const char* key, const char* label, auto& field) {
            if (!err.empty()) return;
            auto bit = a.var_bind.find(key);
            if (bit == a.var_bind.end() || bit->second.empty()) return;
            const std::string& var = bit->second;
            auto vit = variables.find(var);
            using F = std::decay_t<decltype(field)>;
            if constexpr (std::is_same_v<F, int>) {
                if (vit == variables.end())
                    err = std::string("field '") + label + "' expects an integer, but variable '"
                        + var + "' is not set";
                else if (!ParseStrictInt(vit->second, field))
                    err = std::string("field '") + label + "' expects an integer, but variable '"
                        + var + "' = \"" + vit->second + "\" is not an integer";
            } else {
                field = (vit != variables.end()) ? vit->second : std::string();
            }
        });
        if (!err.empty()) { fail(err); return false; }
        return true;
    };

    // Recursive activity runner.
    // updateIndex=true → updates m_currentIndex per item (top-level call only).
    std::function<FlowSignal(const std::vector<Activity>&,
                              std::unordered_set<std::string>&,
                              bool)> runActivities;

    runActivities = [&](const std::vector<Activity>& acts,
                        std::unordered_set<std::string>& calledIds,
                        bool updateIndex) -> FlowSignal {
        bool skipIteration = false;
        int  jumpTarget    = -1;  // set by JumpActivity; applied after std::visit

        for (int i = 0; i < (int)acts.size() && !IsStopped(); ++i) {
            if (!acts[i].enabled) continue;
            if (updateIndex) m_currentIndex.store(i);

            // Spin while suspended or user-paused
            while ((m_suspended.load() || m_userPaused.load()) && !IsStopped())
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (IsStopped()) break;

            idStack.push_back(acts[i].id);
            stepStack.push_back(i + 1);
            typeStack.push_back(ActivityTypeName(acts[i].data));
            struct PathPop {
                std::vector<std::string>& a; std::vector<int>& b; std::vector<std::string>& c;
                ~PathPop() { a.pop_back(); b.pop_back(); c.pop_back(); }
            } pathPop{idStack, stepStack, typeStack};

            // Substitute variable-bound fields (copy only when something is bound)
            const ActivityData* dataPtr = &acts[i].data;
            ActivityData resolved;
            if (!acts[i].var_bind.empty()) {
                if (!resolveBindings(acts[i], resolved)) break;
                dataPtr = &resolved;
            }

            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;

                if constexpr (std::is_same_v<T, MouseMoveActivity>) {
                    if (!g_input) return;
                    auto [ax, ay] = resolveCoords(v.pos_mode, v.x, v.y);
                    if (v.smooth_move && v.smooth_duration_ms > 0) {
                        int sx, sy;
                        g_input->GetMousePos(sx, sy);
                        const int steps = std::max(1, v.smooth_duration_ms / 10);
                        for (int s = 1; s <= steps && !IsStopped(); ++s) {
                            int cx = sx + (ax - sx) * s / steps;
                            int cy = sy + (ay - sy) * s / steps;
                            g_input->MouseMove(cx, cy);
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                    } else {
                        g_input->MouseMove(ax, ay);
                    }
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
                    g_input->MouseDrag(v.button, ax0, ay0, ax1, ay1, std::max(1, v.duration_ms));
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
                        if (BuffersMatchPercent(sample, cur, std::clamp(v.tolerance, 0, 255))
                                >= std::clamp(v.match_percent, 0, 100)) {
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
                    if (v.name.empty()) { fail("variable name is empty"); return; }
                    switch (v.op) {
                        case VarOp::Set:
                            // Empty value clears the variable (back to "not set")
                            if (v.value.empty()) eraseVar(v.name);
                            else                 setVar(v.name, v.value);
                            break;
                        case VarOp::Increment:
                        case VarOp::Decrement: {
                            int cur = 0;  // a variable that is not set counts as 0
                            auto it = variables.find(v.name);
                            if (it != variables.end() && !ParseStrictInt(it->second, cur)) {
                                fail(std::string(v.op == VarOp::Increment ? "increment" : "decrement")
                                     + " expects an integer, but variable '" + v.name
                                     + "' = \"" + it->second + "\" is not an integer");
                                return;
                            }
                            long long next = (long long)cur
                                + (v.op == VarOp::Increment ? v.step : -(long long)v.step);
                            setVar(v.name, std::to_string(next));
                            break;
                        }
                        case VarOp::Random: {
                            std::uniform_int_distribution<int> d(std::min(v.rand_min, v.rand_max),
                                                                 std::max(v.rand_min, v.rand_max));
                            setVar(v.name, std::to_string(d(rng)));
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
                            setVar(v.iter_var, std::to_string(iter));
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
                    std::string val = ResolveValue(v.var_name, v.var_is_var, variables);
                    bool matched = false;
                    for (const auto& sc : v.cases) {
                        if (ResolveValue(sc.value, sc.value_is_var, variables) == val && sc.body) {
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

                } else if constexpr (std::is_same_v<T, JumpActivity>) {
                    SleepInterruptible(v.delay_ms);
                    for (int t = 0; t < (int)acts.size(); ++t) {
                        if (acts[t].id == v.target_id) {
                            jumpTarget = t - 1; // -1 because the for loop does ++i
                            break;
                        }
                    }

                } else if constexpr (std::is_same_v<T, GetMousePositionActivity>) {
                    if (!g_input) return;
                    int sx = 0, sy = 0;
                    g_input->GetMousePos(sx, sy);
                    if (v.pos_mode == PositionMode::Relative) {
                        auto [ox, oy] = m_resolver(m_workflow.window, 0, 0);
                        sx -= ox; sy -= oy;
                    }
                    setVar(v.x_var, std::to_string(sx));
                    setVar(v.y_var, std::to_string(sy));
                    SleepInterruptible(v.delay_ms);

                } else if constexpr (std::is_same_v<T, ClearVariablesActivity>) {
                    {
                        std::lock_guard<std::mutex> lk(m_stateMutex);
                        m_vars.clear();
                    }
                    SleepInterruptible(v.delay_ms);
                }

            }, *dataPtr);

            if (jumpTarget >= 0) { i = jumpTarget; jumpTarget = -1; }
            if (skipIteration) break;
        }

        if (IsStopped()) return FlowSignal::Stop;
        return skipIteration ? FlowSignal::SkipIter : FlowSignal::Continue;
    };

    Logger::Info(m_workflow.name, "Started");

    while (!IsStopped()) {
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

    if (!m_terminated.load())
        Logger::Info(m_workflow.name, IsStopped() ? "Stopped" : "Finished");
    m_running = false;
}
