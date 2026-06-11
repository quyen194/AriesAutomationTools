#include "hotkey/hotkey_manager.hpp"
#include "input/key_map.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>

// Hotkeys are registered on a dedicated background thread so SDL's message
// pump (PeekMessage NULL,0,0,PM_REMOVE) cannot consume WM_HOTKEY messages
// before PollEvents() gets to see them.
class WinHotkeyManager : public IHotkeyManager {
public:
    WinHotkeyManager() {
        m_thread = std::thread([this]{ ThreadFunc(); });
    }

    ~WinHotkeyManager() {
        m_stop.store(true, std::memory_order_relaxed);
        DWORD tid = m_threadId.load(std::memory_order_relaxed);
        if (tid) PostThreadMessageA(tid, WM_APP, 0, 0);
        if (m_thread.joinable()) m_thread.join();
    }

    bool Register(const std::string& key_name, Callback cb) override {
        UINT mods = 0;
        std::string base;
        ParseKey(key_name, mods, base);
        uint32_t vk = KeyNameToNative(base);
        if (!vk) return false;

        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            m_cmds.push_back({true, key_name, mods, vk, std::move(cb)});
        }
        WakeThread();
        return true;
    }

    void Unregister(const std::string& key_name) override {
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            m_cmds.push_back({false, key_name, 0, 0, {}});
        }
        WakeThread();
    }

    // Called from UI thread each frame; dispatches callbacks that fired on the bg thread.
    void PollEvents() override {
        std::vector<Callback> toFire;
        {
            std::lock_guard<std::mutex> lock(m_firedMutex);
            toFire.swap(m_fired);
        }
        for (auto& cb : toFire)
            if (cb) cb();
    }

private:
    struct Cmd {
        bool        isRegister;
        std::string key_name;
        UINT        mods, vk;
        Callback    cb;
    };

    std::mutex            m_cmdMutex;
    std::vector<Cmd>      m_cmds;

    std::mutex            m_firedMutex;
    std::vector<Callback> m_fired;

    std::thread           m_thread;
    std::atomic<DWORD>    m_threadId{0};
    std::atomic<bool>     m_stop{false};

    // Only touched by the background thread (no locking needed):
    std::unordered_map<int, Callback>    m_cbMap;
    std::unordered_map<std::string, int> m_keyMap;
    int m_nextId = 1;

    void WakeThread() {
        DWORD tid = m_threadId.load(std::memory_order_relaxed);
        if (tid) PostThreadMessageA(tid, WM_APP, 0, 0);
    }

    static void ParseKey(const std::string& key_name, UINT& mods, std::string& base) {
        mods = 0;
        std::string s = key_name;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        size_t pos;
        while ((pos = s.find('+')) != std::string::npos) {
            std::string part = s.substr(0, pos);
            s = s.substr(pos + 1);
            if      (part == "ctrl" || part == "control") mods |= MOD_CONTROL;
            else if (part == "shift")                      mods |= MOD_SHIFT;
            else if (part == "alt")                        mods |= MOD_ALT;
            else if (part == "win")                        mods |= MOD_WIN;
        }
        base = s;
    }

    void ProcessCommands() {
        std::vector<Cmd> pending;
        {
            std::lock_guard<std::mutex> lock(m_cmdMutex);
            pending.swap(m_cmds);
        }
        for (auto& cmd : pending) {
            if (cmd.isRegister) {
                // Unregister old binding for this name if it exists
                auto it = m_keyMap.find(cmd.key_name);
                if (it != m_keyMap.end()) {
                    UnregisterHotKey(nullptr, it->second);
                    m_cbMap.erase(it->second);
                    m_keyMap.erase(it);
                }
                int id = m_nextId++;
                if (RegisterHotKey(nullptr, id, cmd.mods | MOD_NOREPEAT, cmd.vk)) {
                    m_cbMap[id]            = std::move(cmd.cb);
                    m_keyMap[cmd.key_name] = id;
                }
            } else {
                auto it = m_keyMap.find(cmd.key_name);
                if (it != m_keyMap.end()) {
                    UnregisterHotKey(nullptr, it->second);
                    m_cbMap.erase(it->second);
                    m_keyMap.erase(it);
                }
            }
        }
    }

    void ThreadFunc() {
        // PeekMessage initialises this thread's Win32 message queue; must happen
        // before we publish m_threadId so PostThreadMessageA can reach us.
        MSG dummy{};
        PeekMessageA(&dummy, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

        m_threadId.store(GetCurrentThreadId(), std::memory_order_relaxed);

        while (!m_stop.load(std::memory_order_relaxed)) {
            ProcessCommands();

            // Dispatch fired hotkeys to the fired queue (read by UI thread in PollEvents).
            MSG msg{};
            while (PeekMessageA(&msg, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
                auto it = m_cbMap.find((int)msg.wParam);
                if (it != m_cbMap.end()) {
                    std::lock_guard<std::mutex> lock(m_firedMutex);
                    m_fired.push_back(it->second);
                }
            }
            // Drain wake-up messages posted by Register/Unregister.
            while (PeekMessageA(&msg, nullptr, WM_APP, WM_APP, PM_REMOVE)) {}

            // Sleep until a hotkey fires or a command is posted; 20 ms fallback.
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 20,
                                      QS_HOTKEY | QS_POSTMESSAGE);
        }

        // Cleanup: unregister everything on the owning thread.
        for (auto& [id, _] : m_cbMap)
            UnregisterHotKey(nullptr, id);
    }
};

std::unique_ptr<IHotkeyManager> CreateHotkeyManager() {
    return std::make_unique<WinHotkeyManager>();
}
