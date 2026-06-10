#include "monitor/activity_monitor.hpp"
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

// Uses IOKit HIDSystem to read idle time (same source as CGEventTap but simpler).
class MacOSActivityMonitor : public IActivityMonitor {
public:
    void Start() override {}
    void Stop()  override {}

    uint64_t MillisSinceLastUserActivity() override {
        io_service_t svc = IOServiceGetMatchingService(
            kIOMainPortDefault, IOServiceMatching("IOHIDSystem"));
        if (!svc) return 0;

        CFTypeRef idle = IORegistryEntryCreateCFProperty(
            svc, CFSTR("HIDIdleTime"), kCFAllocatorDefault, 0);
        IOObjectRelease(svc);
        if (!idle) return 0;

        uint64_t ns = 0;
        if (CFGetTypeID(idle) == CFNumberGetTypeID())
            CFNumberGetValue((CFNumberRef)idle, kCFNumberSInt64Type, &ns);
        CFRelease(idle);

        return ns / 1000000ULL; // nanoseconds → milliseconds
    }
};

std::unique_ptr<IActivityMonitor> CreateActivityMonitor() {
    return std::make_unique<MacOSActivityMonitor>();
}
