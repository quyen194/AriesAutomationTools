#include "hotkey/hotkey_manager.hpp"
#include "input/key_map.hpp"
#include <ApplicationServices/ApplicationServices.h>
#include <unordered_map>
#include <mutex>
#include <atomic>

// Uses CGEventTap on the session tap to intercept key-down events.
// Requires Accessibility permission (System Settings → Privacy → Accessibility).

struct HotkeyEntry {
    uint32_t                    keycode;
    CGEventFlags                mods;
    IHotkeyManager::Callback    cb;
    std::atomic<bool>           fired{false};
};

static std::mutex                                   s_mutex;
static std::unordered_map<std::string, HotkeyEntry> s_entries;
static CFMachPortRef                                s_tap   = nullptr;
static CFRunLoopSourceRef                           s_src   = nullptr;

static CGEventRef TapCallback(CGEventTapProxy, CGEventType type,
                               CGEventRef event, void*) {
    if (type != kCGEventKeyDown) return event;
    CGKeyCode kc    = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    CGEventFlags fl = CGEventGetFlags(event) &
                      (kCGEventFlagMaskControl|kCGEventFlagMaskShift|
                       kCGEventFlagMaskAlternate|kCGEventFlagMaskCommand);
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto& [name, e] : s_entries) {
        if (e.keycode == kc && e.mods == fl) {
            e.fired = true;
            return nullptr; // consume event
        }
    }
    return event;
}

static void EnsureTap() {
    if (s_tap) return;
    CGEventMask mask = CGEventMaskBit(kCGEventKeyDown);
    s_tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
                             kCGEventTapOptionDefault, mask, TapCallback, nullptr);
    if (!s_tap) return;
    s_src = CFMachPortCreateRunLoopSource(nullptr, s_tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), s_src, kCFRunLoopCommonModes);
    CGEventTapEnable(s_tap, true);
}

class MacOSHotkeyManager : public IHotkeyManager {
public:
    MacOSHotkeyManager()  { EnsureTap(); }
    ~MacOSHotkeyManager() {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_entries.clear();
    }

    bool Register(const std::string& key_name, Callback cb) override {
        CGEventFlags mods = 0;
        std::string base;
        parseKey(key_name, mods, base);
        uint32_t kc = KeyNameToNative(base);
        if (!kc) return false;
        std::lock_guard<std::mutex> lock(s_mutex);
        auto& e = s_entries[key_name];
        e.keycode = kc;
        e.mods    = mods;
        e.cb      = cb;
        e.fired   = false;
        return true;
    }

    void Unregister(const std::string& key_name) override {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_entries.erase(key_name);
    }

    void PollEvents() override {
        std::lock_guard<std::mutex> lock(s_mutex);
        for (auto& [name, e] : s_entries) {
            if (e.fired.exchange(false) && e.cb) e.cb();
        }
    }

private:
    static void parseKey(const std::string& key_name,
                         CGEventFlags& mods, std::string& base) {
        mods = 0;
        std::string s = key_name;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        size_t pos;
        while ((pos = s.find('+')) != std::string::npos) {
            std::string p = s.substr(0, pos);
            s = s.substr(pos + 1);
            if (p == "ctrl"  || p == "control") mods |= kCGEventFlagMaskControl;
            else if (p == "shift")              mods |= kCGEventFlagMaskShift;
            else if (p == "alt")                mods |= kCGEventFlagMaskAlternate;
            else if (p == "win" || p == "cmd")  mods |= kCGEventFlagMaskCommand;
        }
        base = s;
    }
};

std::unique_ptr<IHotkeyManager> CreateHotkeyManager() {
    return std::make_unique<MacOSHotkeyManager>();
}
