#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Thread-safe application log.
//  - Every entry is appended to an hourly file in the log directory:
//      <dir>/aries_automation_tools_YYYYMMDD_HH.log
//  - Three log levels (low -> high):
//      DEBUG — detailed trace (every executed activity, resolved coords,
//              trigger checks, smart detection, ...). File only, never shown
//              in the Log panel.
//      INFO  — user operations (create/edit/delete, start/stop, save, ...)
//              and main events (workflow started/finished, trigger fired).
//      ERROR — failures (workflow TERMINATED, config load/save errors, ...).
//  - SetFileLevel() picks the minimum level written to the file.
namespace Logger {

enum class Level { Debug, Info, Error };

struct Entry {
    std::string time;      // "YYYY-MM-DD HH:MM:SS"
    Level       level = Level::Info;
    std::string source;    // workflow name / subsystem (or empty)
    std::string message;
};

// Sets the log directory (created if missing). Until called, entries are
// kept in memory only.
void Init(const std::string& dir);
std::string Dir();
// Path of the file the next entry will be written to
std::string FilePath();

// Minimum level written to the file (default Info)
void  SetFileLevel(Level min);
Level FileLevel();

void Debug(const std::string& source, const std::string& message);
void Info (const std::string& source, const std::string& message);
void Error(const std::string& source, const std::string& message);

// Deletes log files whose date is older than `days` days (the current file
// is never removed). Returns the number of files deleted.
int PurgeOldFiles(int days);

// Copy of the in-memory entries (oldest first)
std::vector<Entry> Snapshot();
// Incremented on every new entry / clear — lets the UI detect changes cheaply
uint64_t Version();
void Clear();

} // namespace Logger
