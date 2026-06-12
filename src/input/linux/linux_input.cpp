#include "input/input_simulator.hpp"
#include "input/key_map.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
// Xlib.h defines KeyPress/KeyRelease/ButtonPress/ButtonRelease as event-type
// integers (e.g. #define KeyPress 2), which clash with our virtual method names.
#undef KeyPress
#undef KeyRelease
#undef ButtonPress
#undef ButtonRelease
#include <thread>
#include <chrono>
#include <stdexcept>
#include <cmath>

class LinuxInputSimulator : public IInputSimulator {
public:
    LinuxInputSimulator() {
        m_display = XOpenDisplay(nullptr);
        if (!m_display) throw std::runtime_error("Cannot open X display");
    }
    ~LinuxInputSimulator() {
        if (m_display) XCloseDisplay(m_display);
    }

    void MouseMove(int x, int y) override {
        XTestFakeMotionEvent(m_display, -1, x, y, 0);
        XFlush(m_display);
    }

    void MouseClick(MouseButton btn, int x, int y, bool double_click) override {
        MouseMove(x, y);
        doClick(BtnNum(btn));
        if (double_click) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            doClick(BtnNum(btn));
        }
    }

    void MouseDrag(MouseButton btn, int x0, int y0, int x1, int y1, int duration_ms) override {
        MouseMove(x0, y0);
        XTestFakeButtonEvent(m_display, BtnNum(btn), True, 0);
        XFlush(m_display);
        int steps = std::max(1, duration_ms / 10);
        for (int i = 1; i <= steps; ++i) {
            int cx = x0 + (x1 - x0) * i / steps;
            int cy = y0 + (y1 - y0) * i / steps;
            XTestFakeMotionEvent(m_display, -1, cx, cy, 0);
            XFlush(m_display);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        XTestFakeButtonEvent(m_display, BtnNum(btn), False, 0);
        XFlush(m_display);
    }

    void MouseScroll(int x, int y, int delta_x, int delta_y) override {
        MouseMove(x, y);
        // X11 button 4=scroll up, 5=scroll down, 6=left, 7=right
        auto scroll = [&](int button, int times) {
            for (int i = 0; i < std::abs(times); ++i) {
                XTestFakeButtonEvent(m_display, button, True,  0);
                XTestFakeButtonEvent(m_display, button, False, 0);
            }
        };
        if (delta_y > 0) scroll(4, delta_y);
        if (delta_y < 0) scroll(5, -delta_y);
        if (delta_x > 0) scroll(7, delta_x);
        if (delta_x < 0) scroll(6, -delta_x);
        XFlush(m_display);
    }

    void KeyPress(const std::string& key_name,
                  const std::vector<std::string>& modifiers) override {
        for (auto& m : modifiers) pressKey(KeyNameToNative(m), true);
        uint32_t sym = KeyNameToNative(key_name);
        if (sym) {
            KeyCode kc = XKeysymToKeycode(m_display, sym);
            XTestFakeKeyEvent(m_display, kc, True,  0);
            XTestFakeKeyEvent(m_display, kc, False, 0);
        }
        for (int i = (int)modifiers.size()-1; i >= 0; --i)
            pressKey(KeyNameToNative(modifiers[i]), false);
        XFlush(m_display);
    }

    void TypeString(const std::string& text, int char_delay_ms) override {
        for (char c : text) {
            KeySym sym = XStringToKeysym(std::string(1, c).c_str());
            if (sym == NoSymbol) continue;
            KeyCode kc = XKeysymToKeycode(m_display, sym);
            XTestFakeKeyEvent(m_display, kc, True,  0);
            XTestFakeKeyEvent(m_display, kc, False, 0);
            XFlush(m_display);
            if (char_delay_ms > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(char_delay_ms));
        }
    }

private:
    Display* m_display = nullptr;

    static int BtnNum(MouseButton b) {
        switch (b) {
            case MouseButton::Right:  return 3;
            case MouseButton::Middle: return 2;
            default:                  return 1;
        }
    }

    void doClick(int btn) {
        XTestFakeButtonEvent(m_display, btn, True,  0);
        XTestFakeButtonEvent(m_display, btn, False, 0);
        XFlush(m_display);
    }

    void pressKey(uint32_t sym, bool down) {
        if (!sym) return;
        KeyCode kc = XKeysymToKeycode(m_display, sym);
        XTestFakeKeyEvent(m_display, kc, down ? True : False, 0);
        XFlush(m_display);
    }
};

std::unique_ptr<IInputSimulator> CreateInputSimulator() {
    return std::make_unique<LinuxInputSimulator>();
}
