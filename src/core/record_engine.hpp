#pragma once
#include "workflow.hpp"
#include <vector>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

struct RecordedEvent {
    enum class Type { MouseMove, MouseClick, MouseScroll, KeyPress } type;
    int     x = 0, y = 0;
    int     button = 0;       // MouseButton cast
    bool    double_click = false;
    int     scroll_dx = 0, scroll_dy = 0;
    std::string key_name;
    std::vector<std::string> modifiers;
    uint64_t timestamp_ms = 0;
};

class RecordEngine {
public:
    RecordEngine();
    ~RecordEngine();

    void Start();
    void Stop();
    bool IsRecording() const { return m_recording.load(); }

    // Events captured so far (readable from UI thread after Stop())
    const std::vector<RecordedEvent>& Events() const { return m_events; }

    // Convert captured events to Activity list.
    // capture_timing: if true, uses real captured delays; else uses fixed_delay_ms.
    std::vector<Activity> ToActivities(bool capture_timing, int fixed_delay_ms = 100) const;

    // Window handle to filter out (the recorder UI itself)
    void SetIgnoreHandle(uint64_t handle) { m_ignoreHandle = handle; }

private:
    std::vector<RecordedEvent> m_events;
    std::mutex                 m_eventsMutex;
    std::atomic<bool>          m_recording{false};
    std::thread                m_hookThread;
    uint64_t                   m_ignoreHandle = 0;
    int                        m_lastMoveX = -9999;
    int                        m_lastMoveY = -9999;

    void InstallHooks();
    void RemoveHooks();

#if defined(_WIN32)
    static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    void*                m_mouseHook     = nullptr;
    void*                m_keyHook       = nullptr;
    std::atomic<DWORD>   m_hookThreadId  {0};
    static RecordEngine* s_instance;
#endif
};
