#include "logger.hpp"
#include <mutex>
#include <deque>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <atomic>

namespace fs = std::filesystem;

namespace Logger {

static std::mutex            s_mutex;
static std::deque<Entry>     s_entries;
static std::string           s_filePath;
static std::atomic<uint64_t> s_version{0};

static constexpr size_t    kMaxEntries  = 1000;
static constexpr uintmax_t kMaxFileSize = 5u * 1024u * 1024u;

static std::string NowString() {
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void SetFile(const std::string& path) {
    std::lock_guard<std::mutex> lk(s_mutex);
    s_filePath = path;
    // Simple rotation: keep one previous file once the log grows too large
    std::error_code ec;
    if (fs::exists(path, ec) && fs::file_size(path, ec) > kMaxFileSize) {
        fs::rename(path, path + ".old", ec);
    }
}

const std::string& FilePath() { return s_filePath; }

static void Add(Level level, const std::string& source, const std::string& message) {
    Entry e{NowString(), level, source, message};
    std::lock_guard<std::mutex> lk(s_mutex);
    if (!s_filePath.empty()) {
        std::ofstream f(fs::u8path(s_filePath), std::ios::app);
        if (f) {
            f << e.time << (level == Level::Error ? " [ERROR] " : " [INFO]  ");
            if (!source.empty()) f << "[" << source << "] ";
            f << message << "\n";
        }
    }
    s_entries.push_back(std::move(e));
    if (s_entries.size() > kMaxEntries) s_entries.pop_front();
    ++s_version;
}

void Info (const std::string& source, const std::string& message) { Add(Level::Info,  source, message); }
void Error(const std::string& source, const std::string& message) { Add(Level::Error, source, message); }

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
