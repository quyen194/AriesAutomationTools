#include "window/window_finder.hpp"
#include "window/pixel_checker.hpp"
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#include <string>

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string CFStringToStd(CFStringRef s) {
    if (!s) return {};
    char buf[512]{};
    CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8);
    return buf;
}

static std::optional<WindowInfo> WindowFromCGEntry(CFDictionaryRef dict) {
    WindowInfo info;

    auto getInt = [&](CFStringRef key) -> int {
        CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(dict, key);
        int v = 0;
        if (n) CFNumberGetValue(n, kCFNumberIntType, &v);
        return v;
    };

    CFStringRef nameRef = (CFStringRef)CFDictionaryGetValue(dict, kCGWindowName);
    CFStringRef ownerRef= (CFStringRef)CFDictionaryGetValue(dict, kCGWindowOwnerName);
    CFNumberRef pidRef  = (CFNumberRef)CFDictionaryGetValue(dict, kCGWindowNumber);

    info.title      = CFStringToStd(nameRef);
    info.class_name = CFStringToStd(ownerRef);
    if (pidRef) { int n; CFNumberGetValue(pidRef, kCFNumberIntType, &n); info.handle = n; }

    CFDictionaryRef bounds = (CFDictionaryRef)CFDictionaryGetValue(dict, kCGWindowBounds);
    if (bounds) {
        CGRect r{}; CGRectMakeWithDictionaryRepresentation(bounds, &r);
        info.rect = {(int)r.origin.x, (int)r.origin.y, (int)r.size.width, (int)r.size.height};
    }
    return info;
}

static CFArrayRef GetWindowList() {
    return CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID);
}

// ── Window Finder ─────────────────────────────────────────────────────────────

class MacOSWindowFinder : public IWindowFinder {
public:
    std::optional<WindowInfo> FindByTitle(const std::string& title) override {
        return searchList([&](CFDictionaryRef d) {
            CFStringRef n = (CFStringRef)CFDictionaryGetValue(d, kCGWindowName);
            return n && CFStringToStd(n) == title;
        });
    }
    std::optional<WindowInfo> FindByClass(const std::string& cls) override {
        return searchList([&](CFDictionaryRef d) {
            CFStringRef n = (CFStringRef)CFDictionaryGetValue(d, kCGWindowOwnerName);
            return n && CFStringToStd(n) == cls;
        });
    }
    std::optional<WindowInfo> FindByHandle(uint64_t handle) override {
        return searchList([&](CFDictionaryRef d) {
            CFNumberRef nr = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowNumber);
            if (!nr) return false;
            int n; CFNumberGetValue(nr, kCFNumberIntType, &n);
            return (uint64_t)n == handle;
        });
    }
    std::pair<int,int> ClientToScreen(uint64_t /*handle*/, int x, int y) override {
        // macOS windows: coords are already screen-based in CG; return as-is
        return {x, y};
    }
    std::optional<WindowInfo> WindowUnderCursor() override {
        CGEventRef ev = CGEventCreate(nullptr);
        CGPoint pt = CGEventGetLocation(ev);
        CFRelease(ev);
        return searchList([&](CFDictionaryRef d) {
            CFDictionaryRef bounds = (CFDictionaryRef)CFDictionaryGetValue(d, kCGWindowBounds);
            if (!bounds) return false;
            CGRect r{}; CGRectMakeWithDictionaryRepresentation(bounds, &r);
            return CGRectContainsPoint(r, pt) != 0;
        });
    }

private:
    template<typename Pred>
    std::optional<WindowInfo> searchList(Pred pred) {
        CFArrayRef list = GetWindowList();
        if (!list) return std::nullopt;
        std::optional<WindowInfo> result;
        CFIndex n = CFArrayGetCount(list);
        for (CFIndex i = 0; i < n; ++i) {
            CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(list, i);
            if (pred(d)) { result = WindowFromCGEntry(d); break; }
        }
        CFRelease(list);
        return result;
    }
};

std::unique_ptr<IWindowFinder> CreateWindowFinder() {
    return std::make_unique<MacOSWindowFinder>();
}

// ── Pixel Checker ─────────────────────────────────────────────────────────────

class MacOSPixelChecker : public IPixelChecker {
public:
    uint32_t GetPixelRGB(int x, int y) override {
        CGImageRef shot = CGWindowListCreateImage(
            CGRectMake((CGFloat)x, (CGFloat)y, 1.0, 1.0),
            kCGWindowListOptionOnScreenOnly,
            kCGNullWindowID,
            kCGWindowImageDefault);
        if (!shot) return 0;

        CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(shot));
        CGImageRelease(shot);
        if (!data) return 0;

        const uint8_t* ptr = CFDataGetBytePtr(data);
        uint32_t result = 0;
        if (ptr) {
            result = ((uint32_t)ptr[0] << 16) | ((uint32_t)ptr[1] << 8) | ptr[2];
        }
        CFRelease(data);
        return result;
    }
};

std::unique_ptr<IPixelChecker> CreatePixelChecker() {
    return std::make_unique<MacOSPixelChecker>();
}
