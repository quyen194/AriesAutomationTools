#pragma once
#include <string>
#include <memory>
#include "core/workflow.hpp"

struct IInputSimulator {
    virtual ~IInputSimulator() = default;
    virtual void MouseMove(int x, int y) = 0;
    virtual void MouseClick(MouseButton btn, int x, int y, bool double_click) = 0;
    virtual void MouseDrag(MouseButton btn, int x0, int y0, int x1, int y1, int duration_ms) = 0;
    virtual void MouseScroll(int x, int y, int delta_x, int delta_y) = 0;
    virtual void KeyPress(const std::string& key_name,
                          const std::vector<std::string>& modifiers) = 0;
    virtual void TypeString(const std::string& text, int char_delay_ms) = 0;
};

std::unique_ptr<IInputSimulator> CreateInputSimulator();
