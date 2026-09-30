#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Thread-safe application log. Entries are kept in memory (for the Log panel)
// and appended to a log file once SetFile() has been called.
namespace Logger {

enum class Level { Info, Error };

struct Entry {
    std::string time;      // "YYYY-MM-DD HH:MM:SS"
    Level       level = Level::Info;
    std::string source;    // workflow name (or empty)
    std::string message;
};

void SetFile(const std::string& path);
const std::string& FilePath();

void Info (const std::string& source, const std::string& message);
void Error(const std::string& source, const std::string& message);

// Copy of the in-memory entries (oldest first)
std::vector<Entry> Snapshot();
// Incremented on every new entry / clear — lets the UI detect changes cheaply
uint64_t Version();
void Clear();

} // namespace Logger
