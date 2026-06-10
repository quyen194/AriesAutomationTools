#include "hotkey/hotkey_manager.hpp"
#include "input/key_map.hpp"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <unordered_map>
#include <mutex>
#include <stdexcept>

class LinuxHotkeyManager : public IHotkeyManager {
public:
    LinuxHotkeyManager() {
        m_dpy = XOpenDisplay(nullptr);
        if (!m_dpy) throw std::runtime_error("Cannot open X display");
        m_root = DefaultRootWindow(m_dpy);
        XSelectInput(m_dpy, m_root, KeyPressMask);
    }
    ~LinuxHotkeyManager() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [key, entry] : m_entries)
            XUngrabKey(m_dpy, entry.keycode, entry.mods, m_root);
        XCloseDisplay(m_dpy);
    }

    bool Register(const std::string& key_name, Callback cb) override {
        unsigned int mods = 0;
        std::string base;
        parseKey(key_name, mods, base);
        uint32_t sym = KeyNameToNative(base);
        if (!sym) return false;
        KeyCode kc = XKeysymToKeycode(m_dpy, sym);
        if (!kc) return false;

        std::lock_guard<std::mutex> lock(m_mutex);
        XGrabKey(m_dpy, kc, mods, m_root, False, GrabModeAsync, GrabModeAsync);
        m_entries[key_name] = {kc, mods, cb};
        return true;
    }

    void Unregister(const std::string& key_name) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key_name);
        if (it == m_entries.end()) return;
        XUngrabKey(m_dpy, it->second.keycode, it->second.mods, m_root);
        m_entries.erase(it);
    }

    void PollEvents() override {
        XEvent ev;
        while (XCheckTypedWindowEvent(m_dpy, m_root, KeyPress, &ev)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& [name, entry] : m_entries) {
                if (ev.xkey.keycode == entry.keycode &&
                    (ev.xkey.state & ~(LockMask|Mod2Mask)) == entry.mods) {
                    if (entry.cb) entry.cb();
                    break;
                }
            }
        }
    }

private:
    struct Entry { KeyCode keycode; unsigned int mods; Callback cb; };

    Display*  m_dpy  = nullptr;
    Window    m_root = None;
    std::mutex m_mutex;
    std::unordered_map<std::string, Entry> m_entries;

    static void parseKey(const std::string& key_name,
                         unsigned int& mods, std::string& base) {
        mods = 0;
        std::string s = key_name;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        size_t pos;
        while ((pos = s.find('+')) != std::string::npos) {
            std::string p = s.substr(0, pos);
            s = s.substr(pos + 1);
            if (p == "ctrl" || p == "control") mods |= ControlMask;
            else if (p == "shift")             mods |= ShiftMask;
            else if (p == "alt")               mods |= Mod1Mask;
            else if (p == "win")               mods |= Mod4Mask;
        }
        base = s;
    }
};

std::unique_ptr<IHotkeyManager> CreateHotkeyManager() {
    return std::make_unique<LinuxHotkeyManager>();
}
