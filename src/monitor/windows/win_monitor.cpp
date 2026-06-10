#include "monitor/activity_monitor.hpp"
#include <windows.h>

class WinActivityMonitor : public IActivityMonitor {
public:
    void Start() override {}
    void Stop()  override {}

    uint64_t MillisSinceLastUserActivity() override {
        LASTINPUTINFO lii{};
        lii.cbSize = sizeof(lii);
        if (!GetLastInputInfo(&lii)) return 0;
        DWORD tick = GetTickCount();
        return (uint64_t)(tick - lii.dwTime);
    }
};

std::unique_ptr<IActivityMonitor> CreateActivityMonitor() {
    return std::make_unique<WinActivityMonitor>();
}
