#include "monitor/activity_monitor.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>
#include <stdexcept>

// Uses XScreenSaverQueryInfo to read idle time — requires libXss.
class LinuxActivityMonitor : public IActivityMonitor {
public:
    LinuxActivityMonitor() {
        m_display = XOpenDisplay(nullptr);
        if (!m_display) throw std::runtime_error("Cannot open X display");
        m_info = XScreenSaverAllocInfo();
    }
    ~LinuxActivityMonitor() {
        if (m_info)    XFree(m_info);
        if (m_display) XCloseDisplay(m_display);
    }

    void Start() override {}
    void Stop()  override {}

    uint64_t MillisSinceLastUserActivity() override {
        if (!m_display || !m_info) return 0;
        XScreenSaverQueryInfo(m_display,
            DefaultRootWindow(m_display), m_info);
        return (uint64_t)m_info->idle;
    }

private:
    Display*          m_display = nullptr;
    XScreenSaverInfo* m_info    = nullptr;
};

std::unique_ptr<IActivityMonitor> CreateActivityMonitor() {
    return std::make_unique<LinuxActivityMonitor>();
}
