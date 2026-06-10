#pragma once
#include <string>
#include <optional>
#include <memory>
#include <cstdint>

struct Rect { int x, y, w, h; };

struct WindowInfo {
    uint64_t    handle = 0;
    std::string title;
    std::string class_name;
    Rect        rect{};
};

struct IWindowFinder {
    virtual ~IWindowFinder() = default;
    virtual std::optional<WindowInfo> FindByTitle(const std::string& title) = 0;
    virtual std::optional<WindowInfo> FindByClass(const std::string& class_name) = 0;
    virtual std::optional<WindowInfo> FindByHandle(uint64_t handle) = 0;
    // translate window-relative client coords to absolute screen coords
    virtual std::pair<int,int> ClientToScreen(uint64_t handle, int x, int y) = 0;
    // returns info for window under current cursor position (for spy-tool picker)
    virtual std::optional<WindowInfo> WindowUnderCursor() = 0;
};

std::unique_ptr<IWindowFinder> CreateWindowFinder();
