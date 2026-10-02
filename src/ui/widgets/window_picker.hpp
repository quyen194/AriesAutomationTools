#pragma once
#include "window/window_finder.hpp"
#include "core/workflow.hpp"
#include <functional>
#include <optional>

// Spy-tool style window picker.
// Render() shows a button; when clicked, enters pick mode (crosshair cursor).
// While in pick mode, hovering any window shows its info in a floating panel.
// Clicking confirms the selection and fires OnPicked.
// Update() must be called every frame (outside any ImGui window) because the
// app window is minimized while picking and Render() is then not reachable.
struct WindowPickerWidget {
    std::function<void(const WindowInfo&)> OnPicked;
    std::function<void()> OnPickBegin;   // pick mode entered (app minimizes itself)
    std::function<void()> OnPickEnd;     // pick mode left — picked or cancelled (app restores)

    void Render(IWindowFinder* finder, const WindowTarget& current);
    void Update(IWindowFinder* finder);
    bool IsPicking() const { return m_picking; }

private:
    void Stop();

    bool m_picking  = false;
    bool m_prevLBtn = false;   // previous-frame LButton state for falling-edge detection
    std::optional<WindowInfo> m_hover;
};
