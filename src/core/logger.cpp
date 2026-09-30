#include "logger.hpp"
#include <mutex>
#include <deque>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <atomic>
#include <thread>
#include <sstream>
#include <functional>
#include <cstdio>

namespace fs = std::filesystem;

namespace Logger {

static std::mutex            s_mutex;
static std::deque<Entry>     s_entries;
static std::string           s_dir;
static std::string           s_curPath;   // file currently open in s_file
static std::ofstream         s_file;
static std::atomic<uint64_t> s_version{0};
static std::atomic<int>      s_fileLevel{(int)Level::Info};

static constexpr size_t kMaxEntries = 1000;
static constexpr const char* kPrefix = "aries_automation_tools_";
static constexpr const char* kExt    = ".log";

static std::tm LocalTm(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

static std::string PathFor(const std::tm& tm) {
    char name[64];
    std::strftime(name, sizeof(name), "aries_automation_tools_%Y%m%d_%H.log", &tm);
    return (fs::u8path(s_dir) / name).u8string();
}

static const char* LevelTag(Level l) {
    switch (l) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Error: return "ERROR";
    }
    return "?    ";
}

void Init(const std::string& dir) {
    std::lock_guard<std::mutex> lk(s_mutex);
    s_dir = dir;
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    if (s_file.is_open()) s_file.close();
    s_curPath.clear();
}

std::string Dir() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_dir;
}

std::string FilePath() {
    std::lock_guard<std::mutex> lk(s_mutex);
    if (s_dir.empty()) return {};
    return PathFor(LocalTm(std::time(nullptr)));
}

void  SetFileLevel(Level min) { s_fileLevel.store((int)min); }
Level FileLevel()            { return (Level)s_fileLevel.load(); }

static void Add(Level level, const std::string& source, const std::string& message) {
    const bool toFile = (int)level >= s_fileLevel.load();
    // Debug entries are file-only; skip all the work when they are filtered out
    if (level == Level::Debug && !toFile) return;
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    int ms = (int)(std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch()).count() % 1000);
    std::tm tm = LocalTm(t);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);

    // Short numeric thread id — tells UI / scheduler / trigger threads apart
    unsigned tid = (unsigned)(std::hash<std::thread::id>{}(std::this_thread::get_id()) % 100000);

    std::lock_guard<std::mutex> lk(s_mutex);
    if (toFile && !s_dir.empty()) {
        std::string path = PathFor(tm);
        if (path != s_curPath) {           // hour changed (or first write)
            if (s_file.is_open()) s_file.close();
            s_file.open(fs::u8path(path), std::ios::app);
            s_curPath = path;
        }
        if (s_file) {
            char msBuf[8];
            snprintf(msBuf, sizeof(msBuf), ".%03d", ms);
            s_file << ts << msBuf << " [" << LevelTag(level) << "] [T" << tid << "] ";
            if (!source.empty()) s_file << "[" << source << "] ";
            s_file << message << "\n";
            s_file.flush();   // keep the file complete if the app crashes
        }
    }
    if (level == Level::Debug) return;
    s_entries.push_back(Entry{ts, level, source, message});
    if (s_entries.size() > kMaxEntries) s_entries.pop_front();
    ++s_version;
}

void Debug(const std::string& source, const std::string& message) { Add(Level::Debug, source, message); }
void Info (const std::string& source, const std::string& message) { Add(Level::Info,  source, message); }
void Error(const std::string& source, const std::string& message) { Add(Level::Error, source, message); }

int PurgeOldFiles(int days) {
    std::string dir;
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        dir = s_dir;
    }
    if (dir.empty() || days < 0) return 0;

    std::time_t cutoff = std::time(nullptr) - (std::time_t)days * 24 * 3600;
    const std::string prefix = kPrefix, ext = kExt;
    int removed = 0;
    std::error_code ec;
    for (auto& ent : fs::directory_iterator(fs::u8path(dir), ec)) {
        if (!ent.is_regular_file(ec)) continue;
        std::string name = ent.path().filename().u8string();
        // aries_automation_tools_YYYYMMDD_HH.log
        if (name.size() != prefix.size() + 11 + ext.size()) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.compare(name.size() - ext.size(), ext.size(), ext) != 0) continue;
        std::tm tm{};
        int y = 0, mo = 0, d = 0, h = 0;
        if (sscanf(name.c_str() + prefix.size(), "%4d%2d%2d_%2d", &y, &mo, &d, &h) != 4) continue;
        tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h;
        tm.tm_isdst = -1;
        std::time_t fileTime = std::mktime(&tm);
        if (fileTime == (std::time_t)-1 || fileTime >= cutoff) continue;
        {
            std::lock_guard<std::mutex> lk(s_mutex);
            if (ent.path().u8string() == s_curPath) continue;
        }
        if (fs::remove(ent.path(), ec)) ++removed;
    }
    return removed;
}

std::vector<Entry> Snapshot() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return {s_entries.begin(), s_entries.end()};
}

uint64_t Version() { return s_version.load(); }

void Clear() {
    std::lock_guard<std::mutex> lk(s_mutex);
    s_entries.clear();
    ++s_version;
}

} // namespace Logger
