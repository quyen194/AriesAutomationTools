#pragma once
#include <cstdint>
#include <memory>

struct IActivityMonitor {
    virtual ~IActivityMonitor() = default;
    virtual void     Start() = 0;
    virtual void     Stop()  = 0;
    // milliseconds since the last real user mouse/keyboard event
    virtual uint64_t MillisSinceLastUserActivity() = 0;
    // true while the user session is locked (lock screen / secure desktop)
    virtual bool     IsSessionLocked() { return false; }
};

std::unique_ptr<IActivityMonitor> CreateActivityMonitor();
