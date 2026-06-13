#include "record_engine.hpp"
#include "input/key_map.hpp"
#include <chrono>
#include <algorithm>

#if defined(_WIN32)
#  include <windows.h>
#endif

static uint64_t NowMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// ── UUID generator (simple random hex) ───────────────────────────────────────
#include <random>
#include <sstream>
#include <iomanip>
static std::string GenId() {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> d(0, 0xFFFFFFFF);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << d(rng) << "-"
       << std::setw(4) << (d(rng) & 0xFFFF) << "-"
       << std::setw(4) << (d(rng) & 0xFFFF) << "-"
       << std::setw(4) << (d(rng) & 0xFFFF) << "-"
       << std::setw(12) << ((uint64_t)d(rng) << 16 | (d(rng) & 0xFFFF));
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────

#if defined(_WIN32)
RecordEngine* RecordEngine::s_instance = nullptr;

// Hook procs do the absolute minimum: copy raw bytes, push to queue, return.
// All filtering and processing happens in ConsumerLoop on a separate thread.
LRESULT CALLBACK RecordEngine::MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance && s_instance->m_recording.load()) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        RawHookEvent rev;
        rev.source       = RawHookEvent::Source::Mouse;
        rev.wParam       = (UINT)wParam;
        rev.x            = info->pt.x;
        rev.y            = info->pt.y;
        rev.mouseData    = info->mouseData;
        rev.timestamp_ms = NowMs();
        { std::lock_guard<std::mutex> lk(s_instance->m_rawMutex);
          s_instance->m_rawQueue.push(rev); }
        s_instance->m_rawCv.notify_one();
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK RecordEngine::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance && s_instance->m_recording.load()) {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            RawHookEvent rev;
            rev.source       = RawHookEvent::Source::Keyboard;
            rev.wParam       = (UINT)wParam;
            rev.vkCode       = info->vkCode;
            rev.timestamp_ms = NowMs();
            { std::lock_guard<std::mutex> lk(s_instance->m_rawMutex);
              s_instance->m_rawQueue.push(rev); }
            s_instance->m_rawCv.notify_one();
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
#endif

RecordEngine::RecordEngine()  = default;
RecordEngine::~RecordEngine() { if (m_recording) Stop(); }

void RecordEngine::Start() {
    m_events.clear();
    m_lastMoveX = -9999;
    m_lastMoveY = -9999;
    m_recording = true;

#if defined(_WIN32)
    // Consumer thread first so it's ready before any events arrive
    m_consumerRunning = true;
    m_consumerThread = std::thread([this]() { ConsumerLoop(); });

    m_hookThreadId.store(0);
    m_hookThread = std::thread([this]() {
        InstallHooks();
        m_hookThreadId.store(GetCurrentThreadId());
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        RemoveHooks();
    });
#endif
}

void RecordEngine::Stop() {
    m_recording = false;
#if defined(_WIN32)
    // Join hook thread first — guarantees no more pushes to m_rawQueue
    if (m_hookThread.joinable()) {
        DWORD tid;
        while ((tid = m_hookThreadId.load()) == 0) {}
        PostThreadMessageA(tid, WM_QUIT, 0, 0);
        m_hookThread.join();
    }
    // Signal consumer to drain remaining events then exit
    if (m_consumerThread.joinable()) {
        m_consumerRunning = false;
        m_rawCv.notify_one();
        m_consumerThread.join();
    }
#endif
}

void RecordEngine::InstallHooks() {
#if defined(_WIN32)
    s_instance   = this;
    if (!IsDebuggerPresent()) {
        m_mouseHook  = SetWindowsHookExA(WH_MOUSE_LL,   MouseProc,    nullptr, 0);
        m_keyHook    = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardProc, nullptr, 0);
    }
#endif
    // Linux: XRecord / macOS: CGEventTap would go here (future impl)
}

void RecordEngine::RemoveHooks() {
#if defined(_WIN32)
    if (m_mouseHook) { UnhookWindowsHookEx((HHOOK)m_mouseHook); m_mouseHook = nullptr; }
    if (m_keyHook)   { UnhookWindowsHookEx((HHOOK)m_keyHook);   m_keyHook   = nullptr; }
    s_instance = nullptr;
#endif
}

#if defined(_WIN32)
void RecordEngine::ConsumerLoop() {
    while (true) {
        RawHookEvent rev;
        {
            std::unique_lock<std::mutex> lk(m_rawMutex);
            m_rawCv.wait(lk, [this]{
                return !m_rawQueue.empty() || !m_consumerRunning.load();
            });
            if (m_rawQueue.empty()) break;  // stopped and queue drained
            rev = m_rawQueue.front();
            m_rawQueue.pop();
        }
        ProcessRawEvent(rev);
    }
}

void RecordEngine::ProcessRawEvent(const RawHookEvent& rev) {
    if (rev.source == RawHookEvent::Source::Mouse) {
        // Filter clicks on the recorder's own overlay window
        if (rev.wParam == WM_LBUTTONDOWN || rev.wParam == WM_RBUTTONDOWN || rev.wParam == WM_MBUTTONDOWN) {
            POINT pt = {rev.x, rev.y};
            HWND hwnd = WindowFromPoint(pt);
            if (m_ignoreHandle &&
                (uint64_t)(uintptr_t)GetAncestor(hwnd, GA_ROOT) == m_ignoreHandle)
                return;
        }

        RecordedEvent ev;
        ev.timestamp_ms = rev.timestamp_ms;
        ev.x = rev.x;
        ev.y = rev.y;

        switch (rev.wParam) {
            case WM_MOUSEMOVE: {
                int dx = rev.x - m_lastMoveX;
                int dy = rev.y - m_lastMoveY;
                if (dx*dx + dy*dy >= 100) {
                    ev.type = RecordedEvent::Type::MouseMove;
                    std::lock_guard<std::mutex> lk(m_eventsMutex);
                    m_events.push_back(ev);
                    m_lastMoveX = rev.x;
                    m_lastMoveY = rev.y;
                }
                break;
            }
            case WM_LBUTTONDOWN:
                ev.type = RecordedEvent::Type::MouseClick; ev.button = 0;
                { std::lock_guard<std::mutex> lk(m_eventsMutex); m_events.push_back(ev); }
                break;
            case WM_RBUTTONDOWN:
                ev.type = RecordedEvent::Type::MouseClick; ev.button = 1;
                { std::lock_guard<std::mutex> lk(m_eventsMutex); m_events.push_back(ev); }
                break;
            case WM_MBUTTONDOWN:
                ev.type = RecordedEvent::Type::MouseClick; ev.button = 2;
                { std::lock_guard<std::mutex> lk(m_eventsMutex); m_events.push_back(ev); }
                break;
            case WM_MOUSEWHEEL: {
                short delta = (short)HIWORD(rev.mouseData);
                ev.type = RecordedEvent::Type::MouseScroll;
                ev.scroll_dy = delta / WHEEL_DELTA;
                { std::lock_guard<std::mutex> lk(m_eventsMutex); m_events.push_back(ev); }
                break;
            }
            default: break;
        }
    } else {
        RecordedEvent ev;
        ev.timestamp_ms = rev.timestamp_ms;
        ev.type         = RecordedEvent::Type::KeyPress;
        ev.key_name     = NativeToKeyName(rev.vkCode);
        if (ev.key_name.empty()) {
            char buf[2] = {(char)rev.vkCode, 0};
            ev.key_name = buf;
        }
        auto held = [](int vk){ return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        if (held(VK_CONTROL) && ev.key_name != "ctrl" && ev.key_name != "lctrl")
            ev.modifiers.push_back("ctrl");
        if (held(VK_SHIFT) && ev.key_name != "shift" && ev.key_name != "lshift")
            ev.modifiers.push_back("shift");
        if (held(VK_MENU) && ev.key_name != "alt" && ev.key_name != "lalt")
            ev.modifiers.push_back("alt");
        std::lock_guard<std::mutex> lk(m_eventsMutex);
        m_events.push_back(ev);
    }
}
#endif

std::vector<Activity> RecordEngine::ToActivities(bool capture_timing, int fixed_delay_ms) const {
    std::vector<Activity> result;
    for (size_t i = 0; i < m_events.size(); ++i) {
        const auto& ev = m_events[i];
        int delay = fixed_delay_ms;
        if (capture_timing && i + 1 < m_events.size())
            delay = (int)(m_events[i+1].timestamp_ms - ev.timestamp_ms);

        Activity a;
        a.id      = GenId();
        a.enabled = true;

        switch (ev.type) {
            case RecordedEvent::Type::MouseMove: {
                MouseMoveActivity v;
                v.pos_mode = PositionMode::Absolute;
                v.x = ev.x; v.y = ev.y;
                v.delay_ms = delay;
                a.data = v;
                break;
            }
            case RecordedEvent::Type::MouseClick: {
                MouseClickActivity v;
                v.pos_mode = PositionMode::Absolute;
                v.x = ev.x; v.y = ev.y;
                v.button   = (MouseButton)ev.button;
                v.delay_ms = delay;
                a.data = v;
                break;
            }
            case RecordedEvent::Type::MouseScroll: {
                MouseScrollActivity v;
                v.x = ev.x; v.y = ev.y;
                v.delta_y  = ev.scroll_dy;
                v.delay_ms = delay;
                a.data = v;
                break;
            }
            case RecordedEvent::Type::KeyPress: {
                KeyPressActivity v;
                v.key       = ev.key_name;
                v.modifiers = ev.modifiers;
                v.delay_ms  = delay;
                a.data = v;
                break;
            }
        }
        result.push_back(a);
    }
    return result;
}
