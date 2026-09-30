#pragma once
#include "core/workflow.hpp"
#include <string>
#include <stdexcept>

class ConfigManager {
public:
    // Load config from file. Throws std::runtime_error on parse failure.
    static AppConfig Load(const std::string& path);

    // Save config to file. Throws std::runtime_error on write failure.
    static void Save(const AppConfig& config, const std::string& path);

    // Per-user app data directory (created if missing):
    //   Windows: %APPDATA%\AriesAutomationTools
    //   macOS:   ~/Library/Application Support/AriesAutomationTools
    //   Linux:   $XDG_CONFIG_HOME/AriesAutomationTools (or ~/.config/...)
    static std::string DataDir();

    // Returns <DataDir>/config.json. If it does not exist yet, a legacy
    // config.json next to the executable is copied there first.
    static std::string DefaultPath();
};
