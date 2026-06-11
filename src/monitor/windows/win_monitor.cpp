#include "monitor/activity_monitor.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <thread>

// Timestamp of the last non-injected (real) user input, as GetTickCount() value.
static std::atomic<DWORD> s_lastRealInputTick{0};

static LRESULT CALLBACK KbLowLevelProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* ks = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        // LLKHF_INJECTED (0x10): event came from SendInput/keybd_event — skip it.
        if (!(ks->flags & LLKHF_INJECTED))
            s_lastRealInputTick.store(GetTickCount(), std::memory_order_relaxed);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static LRESULT CALLBACK MsLowLevelProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        // LLMHF_INJECTED (0x01): event came from SendInput/mouse_event — skip it.
        if (!(ms->flags & LLMHF_INJECTED))
            s_lastRealInputTick.store(GetTickCount(), std::memory_order_relaxed);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

class WinActivityMonitor : public IActivityMonitor {
    std::thread        m_thread;
    std::atomic<DWORD> m_threadId{0};
    std::atomic<bool>  m_ready{false};

public:
    void Start() override {
        s_lastRealInputTick.store(GetTickCount(), std::memory_order_relaxed);
        m_ready.store(false, std::memory_order_relaxed);
        m_thread = std::thread([this]() {
            m_threadId.store(GetCurrentThreadId(), std::memory_order_release);
            HHOOK kbHook = SetWindowsHookEx(WH_KEYBOARD_LL, KbLowLevelProc, nullptr, 0);
            HHOOK msHook = SetWindowsHookEx(WH_MOUSE_LL,    MsLowLevelProc, nullptr, 0);
            m_ready.store(true, std::memory_order_release);
            MSG msg;
            while (GetMessage(&msg, nullptr, 0, 0) > 0)
                DispatchMessage(&msg);
            if (kbHook) UnhookWindowsHookEx(kbHook);
            if (msHook) UnhookWindowsHookEx(msHook);
            m_threadId.store(0, std::memory_order_release);
        });
        // Spin until the hook thread has installed its hooks (nearly instant).
        while (!m_ready.load(std::memory_order_acquire))
            std::this_thread::yield();
    }

    void Stop() override {
        DWORD tid = m_threadId.load(std::memory_order_acquire);
        if (tid) PostThreadMessage(tid, WM_QUIT, 0, 0);
        if (m_thread.joinable()) m_thread.join();
    }

    uint64_t MillisSinceLastUserActivity() override {
        DWORD now  = GetTickCount();
        DWORD last = s_lastRealInputTick.load(std::memory_order_relaxed);
        return (uint64_t)(now - last);
    }
};

std::unique_ptr<IActivityMonitor> CreateActivityMonitor() {
    return std::make_unique<WinActivityMonitor>();
}
