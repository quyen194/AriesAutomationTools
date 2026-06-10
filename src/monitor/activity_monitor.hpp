#pragma once
#include <cstdint>
#include <memory>

struct IActivityMonitor {
    virtual ~IActivityMonitor() = default;
    virtual void     Start() = 0;
    virtual void     Stop()  = 0;
    // milliseconds since the last real user mouse/keyboard event
    virtual uint64_t MillisSinceLastUserActivity() = 0;
};

std::unique_ptr<IActivityMonitor> CreateActivityMonitor();
