#include "input/input_simulator.hpp"
#include "input/key_map.hpp"
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <thread>
#include <chrono>
#include <cmath>

static CGMouseButton ToCGButton(MouseButton b) {
    switch (b) {
        case MouseButton::Right:  return kCGMouseButtonRight;
        case MouseButton::Middle: return kCGMouseButtonCenter;
        default:                  return kCGMouseButtonLeft;
    }
}
static CGEventType DownType(MouseButton b) {
    switch (b) {
        case MouseButton::Right:  return kCGEventRightMouseDown;
        case MouseButton::Middle: return kCGEventOtherMouseDown;
        default:                  return kCGEventLeftMouseDown;
    }
}
static CGEventType UpType(MouseButton b) {
    switch (b) {
        case MouseButton::Right:  return kCGEventRightMouseUp;
        case MouseButton::Middle: return kCGEventOtherMouseUp;
        default:                  return kCGEventLeftMouseUp;
    }
}
static CGEventType DragType(MouseButton b) {
    switch (b) {
        case MouseButton::Right:  return kCGEventRightMouseDragged;
        case MouseButton::Middle: return kCGEventOtherMouseDragged;
        default:                  return kCGEventLeftMouseDragged;
    }
}

class MacOSInputSimulator : public IInputSimulator {
public:
    void MouseMove(int x, int y) override {
        CGPoint pt{(CGFloat)x, (CGFloat)y};
        CGEventRef ev = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, pt, kCGMouseButtonLeft);
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);
    }

    void MouseClick(MouseButton btn, int x, int y, bool double_click) override {
        CGPoint pt{(CGFloat)x, (CGFloat)y};
        auto post = [&](CGEventType t) {
            CGEventRef e = CGEventCreateMouseEvent(nullptr, t, pt, ToCGButton(btn));
            CGEventPost(kCGHIDEventTap, e);
            CFRelease(e);
        };
        post(DownType(btn));
        post(UpType(btn));
        if (double_click) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CGEventRef e = CGEventCreateMouseEvent(nullptr, DownType(btn), pt, ToCGButton(btn));
            CGEventSetIntegerValueField(e, kCGMouseEventClickState, 2);
            CGEventPost(kCGHIDEventTap, e); CFRelease(e);
            e = CGEventCreateMouseEvent(nullptr, UpType(btn), pt, ToCGButton(btn));
            CGEventSetIntegerValueField(e, kCGMouseEventClickState, 2);
            CGEventPost(kCGHIDEventTap, e); CFRelease(e);
        }
    }

    void MouseDrag(MouseButton btn, int x0, int y0, int x1, int y1, int duration_ms) override {
        CGPoint pt0{(CGFloat)x0, (CGFloat)y0};
        CGEventRef down = CGEventCreateMouseEvent(nullptr, DownType(btn), pt0, ToCGButton(btn));
        CGEventPost(kCGHIDEventTap, down);
        CFRelease(down);

        int steps = std::max(1, duration_ms / 10);
        for (int i = 1; i <= steps; ++i) {
            CGPoint pt{(CGFloat)(x0 + (x1-x0)*i/steps), (CGFloat)(y0 + (y1-y0)*i/steps)};
            CGEventRef drag = CGEventCreateMouseEvent(nullptr, DragType(btn), pt, ToCGButton(btn));
            CGEventPost(kCGHIDEventTap, drag);
            CFRelease(drag);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        CGPoint pt1{(CGFloat)x1, (CGFloat)y1};
        CGEventRef up = CGEventCreateMouseEvent(nullptr, UpType(btn), pt1, ToCGButton(btn));
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(up);
    }

    void MouseScroll(int x, int y, int delta_x, int delta_y) override {
        MouseMove(x, y);
        CGEventRef ev = CGEventCreateScrollWheelEvent(nullptr,
            kCGScrollEventUnitLine, 2, delta_y, delta_x);
        CGEventPost(kCGHIDEventTap, ev);
        CFRelease(ev);
    }

    void KeyPress(const std::string& key_name,
                  const std::vector<std::string>& modifiers) override {
        CGEventFlags flags = buildFlags(modifiers);
        uint32_t kc = KeyNameToNative(key_name);
        if (!kc) return;

        CGEventRef down = CGEventCreateKeyboardEvent(nullptr, kc, true);
        CGEventRef up   = CGEventCreateKeyboardEvent(nullptr, kc, false);
        CGEventSetFlags(down, flags);
        CGEventSetFlags(up,   flags);
        CGEventPost(kCGHIDEventTap, down); CFRelease(down);
        CGEventPost(kCGHIDEventTap, up);   CFRelease(up);
    }

    void TypeString(const std::string& text, int char_delay_ms) override {
        for (char c : text) {
            // Use Unicode input via kCGEventKeyboardSetUnicodeString
            CGEventRef down = CGEventCreateKeyboardEvent(nullptr, 0, true);
            CGEventRef up   = CGEventCreateKeyboardEvent(nullptr, 0, false);
            UniChar uc = (UniChar)c;
            CGEventKeyboardSetUnicodeString(down, 1, &uc);
            CGEventKeyboardSetUnicodeString(up,   1, &uc);
            CGEventPost(kCGHIDEventTap, down); CFRelease(down);
            CGEventPost(kCGHIDEventTap, up);   CFRelease(up);
            if (char_delay_ms > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(char_delay_ms));
        }
    }

private:
    CGEventFlags buildFlags(const std::vector<std::string>& mods) {
        CGEventFlags f = 0;
        for (auto& m : mods) {
            std::string lm = m;
            std::transform(lm.begin(), lm.end(), lm.begin(), ::tolower);
            if (lm == "ctrl"  || lm == "control") f |= kCGEventFlagMaskControl;
            else if (lm == "shift")               f |= kCGEventFlagMaskShift;
            else if (lm == "alt")                 f |= kCGEventFlagMaskAlternate;
            else if (lm == "win" || lm == "cmd")  f |= kCGEventFlagMaskCommand;
        }
        return f;
    }
};

std::unique_ptr<IInputSimulator> CreateInputSimulator() {
    return std::make_unique<MacOSInputSimulator>();
}
