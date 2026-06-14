#include "input/input_simulator.hpp"
#include "input/key_map.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <thread>
#include <chrono>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────

static DWORD BtnDownFlag(MouseButton b) {
    switch (b) {
        case MouseButton::Right:  return MOUSEEVENTF_RIGHTDOWN;
        case MouseButton::Middle: return MOUSEEVENTF_MIDDLEDOWN;
        default:                  return MOUSEEVENTF_LEFTDOWN;
    }
}
static DWORD BtnUpFlag(MouseButton b) {
    switch (b) {
        case MouseButton::Right:  return MOUSEEVENTF_RIGHTUP;
        case MouseButton::Middle: return MOUSEEVENTF_MIDDLEUP;
        default:                  return MOUSEEVENTF_LEFTUP;
    }
}

class WinInputSimulator : public IInputSimulator {
public:
    void MouseMove(int x, int y) override {
        SetCursorPos(x, y);
    }

    void GetMousePos(int& x, int& y) override {
        POINT pt{};
        GetCursorPos(&pt);
        x = pt.x; y = pt.y;
    }

    void MouseClick(MouseButton btn, int x, int y, bool double_click) override {
        SetCursorPos(x, y);
        doClick(btn);
        if (double_click) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            doClick(btn);
        }
    }

    void MouseDrag(MouseButton btn, int x0, int y0, int x1, int y1, int duration_ms) override {
        SetCursorPos(x0, y0);
        INPUT in{};
        in.type       = INPUT_MOUSE;
        in.mi.dwFlags = BtnDownFlag(btn);
        SendInput(1, &in, sizeof(INPUT));

        const int steps = std::max(1, duration_ms / 10);
        for (int i = 1; i <= steps; ++i) {
            int cx = x0 + (x1 - x0) * i / steps;
            int cy = y0 + (y1 - y0) * i / steps;
            SetCursorPos(cx, cy);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        in.mi.dwFlags = BtnUpFlag(btn);
        SendInput(1, &in, sizeof(INPUT));
    }

    void MouseScroll(int x, int y, int delta_x, int delta_y) override {
        SetCursorPos(x, y);
        if (delta_y != 0) {
            INPUT in{};
            in.type           = INPUT_MOUSE;
            in.mi.dwFlags     = MOUSEEVENTF_WHEEL;
            in.mi.mouseData   = (DWORD)(delta_y * WHEEL_DELTA);
            SendInput(1, &in, sizeof(INPUT));
        }
        if (delta_x != 0) {
            INPUT in{};
            in.type           = INPUT_MOUSE;
            in.mi.dwFlags     = MOUSEEVENTF_HWHEEL;
            in.mi.mouseData   = (DWORD)(delta_x * WHEEL_DELTA);
            SendInput(1, &in, sizeof(INPUT));
        }
    }

    void KeyPress(const std::string& key_name,
                  const std::vector<std::string>& modifiers) override {
        // Press modifiers
        for (auto& m : modifiers) pressMod(m, true);

        uint32_t vk = KeyNameToNative(key_name);
        if (vk) {
            sendKey((WORD)vk, true);
            sendKey((WORD)vk, false);
        }

        // Release modifiers in reverse
        for (int i = (int)modifiers.size() - 1; i >= 0; --i)
            pressMod(modifiers[i], false);
    }

    void TypeString(const std::string& text, int char_delay_ms) override {
        for (unsigned char c : text) {
            SHORT vk = VkKeyScanA((CHAR)c);
            if (vk == -1) continue;

            bool needShift = (vk >> 8) & 1;
            WORD code = vk & 0xFF;

            if (needShift) sendKey(VK_SHIFT, true);
            sendKey(code, true);
            sendKey(code, false);
            if (needShift) sendKey(VK_SHIFT, false);

            if (char_delay_ms > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(char_delay_ms));
        }
    }

private:
    void doClick(MouseButton btn) {
        INPUT ins[2]{};
        ins[0].type       = INPUT_MOUSE;
        ins[0].mi.dwFlags = BtnDownFlag(btn);
        ins[1].type       = INPUT_MOUSE;
        ins[1].mi.dwFlags = BtnUpFlag(btn);
        SendInput(2, ins, sizeof(INPUT));
    }

    void sendKey(WORD vk, bool down) {
        // Extended keys need KEYEVENTF_EXTENDEDKEY for correct delivery
        static const WORD kExtended[] = {
            VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
            VK_HOME, VK_END, VK_INSERT, VK_DELETE,
            VK_PRIOR, VK_NEXT, VK_NUMLOCK,
            VK_RCONTROL, VK_RMENU, VK_RWIN, VK_DIVIDE
        };
        bool extended = false;
        for (WORD ek : kExtended) if (vk == ek) { extended = true; break; }

        INPUT in{};
        in.type       = INPUT_KEYBOARD;
        in.ki.wVk     = vk;
        in.ki.wScan   = (WORD)MapVirtualKey(vk, MAPVK_VK_TO_VSC);
        in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) |
                        (extended ? KEYEVENTF_EXTENDEDKEY : 0);
        SendInput(1, &in, sizeof(INPUT));
    }

    void pressMod(const std::string& mod, bool down) {
        uint32_t vk = KeyNameToNative(mod);
        if (vk) sendKey((WORD)vk, down);
    }
};

std::unique_ptr<IInputSimulator> CreateInputSimulator() {
    return std::make_unique<WinInputSimulator>();
}
