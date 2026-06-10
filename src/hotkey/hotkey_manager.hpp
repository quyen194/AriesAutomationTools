#pragma once
#include <string>
#include <functional>
#include <memory>

struct IHotkeyManager {
    using Callback = std::function<void()>;

    virtual ~IHotkeyManager() = default;
    // Register a named-string hotkey (e.g. "F9", "ctrl+shift+p")
    virtual bool Register(const std::string& key_name, Callback cb) = 0;
    virtual void Unregister(const std::string& key_name) = 0;
    // Must be called once per frame from the UI thread to dispatch callbacks
    virtual void PollEvents() = 0;
};

std::unique_ptr<IHotkeyManager> CreateHotkeyManager();
