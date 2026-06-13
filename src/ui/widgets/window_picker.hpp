#pragma once
#include "window/window_finder.hpp"
#include "core/workflow.hpp"
#include <functional>
#include <optional>

// Spy-tool style window picker.
// Render() shows a button; when clicked, enters pick mode (crosshair cursor).
// While in pick mode, hovering any window shows its info in a tooltip.
// Clicking confirms the selection and fires OnPicked.
struct WindowPickerWidget {
    std::function<void(const WindowInfo&)> OnPicked;

    void Render(IWindowFinder* finder, const WindowTarget& current);

private:
    bool m_picking  = false;
    bool m_prevLBtn = false;   // previous-frame LButton state for falling-edge detection
    std::optional<WindowInfo> m_hover;
};
