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

LRESULT CALLBACK RecordEngine::MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance && s_instance->m_recording.load()) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        // Filter clicks on the recorder's own window
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN) {
            HWND hwnd = WindowFromPoint(info->pt);
            if (s_instance->m_ignoreHandle &&
                (uint64_t)(uintptr_t)GetAncestor(hwnd, GA_ROOT) == s_instance->m_ignoreHandle) {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
        }

        RecordedEvent ev;
        ev.timestamp_ms = NowMs();
        ev.x = info->pt.x;
        ev.y = info->pt.y;

        switch (wParam) {
            case WM_MOUSEMOVE: {
                int dx = ev.x - s_instance->m_lastMoveX;
                int dy = ev.y - s_instance->m_lastMoveY;
                if (dx*dx + dy*dy >= 100) { // only record if moved >10px
                    ev.type = RecordedEvent::Type::MouseMove;
                    s_instance->m_events.push_back(ev);
                    s_instance->m_lastMoveX = ev.x;
                    s_instance->m_lastMoveY = ev.y;
                }
                break;
            }
            case WM_LBUTTONDOWN:
                ev.type = RecordedEvent::Type::MouseClick; ev.button = 0;
                s_instance->m_events.push_back(ev);
                break;
            case WM_RBUTTONDOWN:
                ev.type = RecordedEvent::Type::MouseClick; ev.button = 1;
                s_instance->m_events.push_back(ev);
                break;
            case WM_MBUTTONDOWN:
                ev.type = RecordedEvent::Type::MouseClick; ev.button = 2;
                s_instance->m_events.push_back(ev);
                break;
            case WM_MOUSEWHEEL: {
                short delta = HIWORD(info->mouseData);
                ev.type = RecordedEvent::Type::MouseScroll;
                ev.scroll_dy = delta / WHEEL_DELTA;
                s_instance->m_events.push_back(ev);
                break;
            }
            default: break;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK RecordEngine::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance && s_instance->m_recording.load()) {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            RecordedEvent ev;
            ev.timestamp_ms = NowMs();
            ev.type         = RecordedEvent::Type::KeyPress;
            ev.key_name     = NativeToKeyName(info->vkCode);
            if (ev.key_name.empty()) {
                char buf[2] = {(char)info->vkCode, 0};
                ev.key_name = buf;
            }
            // Detect held modifiers
            auto held = [](int vk){ return (GetAsyncKeyState(vk) & 0x8000) != 0; };
            if (held(VK_CONTROL) && ev.key_name != "ctrl" && ev.key_name != "lctrl")
                ev.modifiers.push_back("ctrl");
            if (held(VK_SHIFT) && ev.key_name != "shift" && ev.key_name != "lshift")
                ev.modifiers.push_back("shift");
            if (held(VK_MENU) && ev.key_name != "alt" && ev.key_name != "lalt")
                ev.modifiers.push_back("alt");
            s_instance->m_events.push_back(ev);
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
    InstallHooks();
}

void RecordEngine::Stop() {
    m_recording = false;
    RemoveHooks();
}

void RecordEngine::InstallHooks() {
#if defined(_WIN32)
    s_instance   = this;
    m_mouseHook  = SetWindowsHookExA(WH_MOUSE_LL,   MouseProc,    nullptr, 0);
    m_keyHook    = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardProc, nullptr, 0);
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
