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

    // Returns the default config path next to the executable.
    static std::string DefaultPath();
};
