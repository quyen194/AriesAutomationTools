#include "hotkey/hotkey_manager.hpp"
#include "input/key_map.hpp"
#include <windows.h>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <stdexcept>

// Uses Windows RegisterHotKey (thread-safe: registered on a dedicated message thread,
// callbacks dispatched back to main via PollEvents()).

class WinHotkeyManager : public IHotkeyManager {
public:
    WinHotkeyManager()  = default;
    ~WinHotkeyManager() {
        // Unregister all
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, _] : m_callbacks)
            UnregisterHotKey(nullptr, id);
    }

    bool Register(const std::string& key_name, Callback cb) override {
        // Parse "ctrl+shift+f9" style
        UINT mods = 0;
        std::string base;
        {
            std::string s = key_name;
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            size_t pos;
            while ((pos = s.find('+')) != std::string::npos) {
                std::string part = s.substr(0, pos);
                s = s.substr(pos + 1);
                if (part == "ctrl"  || part == "control") mods |= MOD_CONTROL;
                else if (part == "shift")                  mods |= MOD_SHIFT;
                else if (part == "alt")                    mods |= MOD_ALT;
                else if (part == "win")                    mods |= MOD_WIN;
            }
            base = s;
        }
        uint32_t vk = KeyNameToNative(base);
        if (!vk) return false;

        // Find a free ID (1–0xBFFF per MSDN)
        std::lock_guard<std::mutex> lock(m_mutex);
        int id = m_nextId++;
        mods |= MOD_NOREPEAT;
        if (!RegisterHotKey(nullptr, id, mods, vk)) return false;

        m_callbacks[id] = cb;
        m_keyNames[key_name] = id;
        return true;
    }

    void Unregister(const std::string& key_name) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_keyNames.find(key_name);
        if (it == m_keyNames.end()) return;
        int id = it->second;
        UnregisterHotKey(nullptr, id);
        m_callbacks.erase(id);
        m_keyNames.erase(it);
    }

    void PollEvents() override {
        MSG msg{};
        // Non-blocking peek of all pending WM_HOTKEY messages
        while (PeekMessageA(&msg, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_callbacks.find((int)msg.wParam);
            if (it != m_callbacks.end() && it->second)
                it->second();
        }
    }

private:
    std::mutex m_mutex;
    int m_nextId = 1;
    std::unordered_map<int, Callback>     m_callbacks;
    std::unordered_map<std::string, int>  m_keyNames;
};

std::unique_ptr<IHotkeyManager> CreateHotkeyManager() {
    return std::make_unique<WinHotkeyManager>();
}
