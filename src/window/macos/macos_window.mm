#include "window/window_finder.hpp"
#include "window/pixel_checker.hpp"
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <dispatch/dispatch.h>
#include <mutex>
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
//
// macOS 14+   : ScreenCaptureKit (SCScreenshotManager), Apple's supported API.
// macOS 13    : CGDisplayCreateImageForRect fallback (deprecated in 15, but the
//               SCK path is taken there; CGWindowListCreateImage was removed
//               from the macOS 15 SDK entirely).

// Normalize any CGImage into a w x h packed-0xRRGGBB buffer by drawing it into
// an RGBA bitmap context (also handles Retina 2x sources by scaling down).
static PixelBuffer CGImageToBuffer(CGImageRef img, int w, int h) {
    PixelBuffer buf;
    if (!img || w <= 0 || h <= 0) return buf;

    std::vector<uint8_t> raw((size_t)w * h * 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        raw.data(), w, h, 8, (size_t)w * 4, cs,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big); // memory = R,G,B,A
    CGColorSpaceRelease(cs);
    if (!ctx) return buf;

    CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), img);
    CGContextRelease(ctx);

    buf.width  = w;
    buf.height = h;
    buf.pixels.resize((size_t)w * h);
    for (size_t i = 0; i < buf.pixels.size(); ++i)
        buf.pixels[i] = ((uint32_t)raw[i*4+0] << 16) |
                        ((uint32_t)raw[i*4+1] <<  8) |
                         (uint32_t)raw[i*4+2];
    return buf;
}

class MacOSPixelChecker : public IPixelChecker {
public:
    uint32_t GetPixelRGB(int x, int y) override {
        PixelBuffer b = CaptureRegion(x, y, 1, 1);
        return b.Empty() ? 0 : b.pixels[0];
    }

    PixelBuffer CaptureRegion(int x, int y, int w, int h) override {
        if (w <= 0 || h <= 0) return {};
        std::lock_guard<std::mutex> lock(m_mutex);

        // Resolve which display contains the point; rect becomes display-local
        CGPoint pt = CGPointMake((CGFloat)x, (CGFloat)y);
        CGDirectDisplayID display = CGMainDisplayID();
        uint32_t count = 0;
        CGGetDisplaysWithPoint(pt, 1, &display, &count);

        CGRect bounds = CGDisplayBounds(display);
        CGRect local  = CGRectMake(pt.x - bounds.origin.x, pt.y - bounds.origin.y,
                                   (CGFloat)w, (CGFloat)h);

        if (@available(macOS 14.0, *)) {
            PixelBuffer buf = CaptureWithSCK(display, local, w, h);
            if (!buf.Empty()) return buf;
            // fall through to the legacy path if SCK failed (e.g. no permission)
        }
        return CaptureLegacy(display, local, w, h);
    }

private:
    PixelBuffer CaptureWithSCK(CGDirectDisplayID displayID, CGRect localRect,
                               int w, int h) API_AVAILABLE(macos(14.0)) {
        SCDisplay* display = FindSCDisplay(displayID);
        if (!display) return {};

        SCContentFilter* filter =
            [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
        SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
        config.sourceRect  = localRect;       // points, display-local
        config.width       = (size_t)w;       // output pixels: 1 per point
        config.height      = (size_t)h;
        config.showsCursor = NO;

        __block CGImageRef image = nullptr;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [SCScreenshotManager captureImageWithFilter:filter
                                      configuration:config
                                  completionHandler:^(CGImageRef img, NSError* error) {
            if (img && !error) image = CGImageRetain(img);
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

        if (!image) {
            m_shareable = nil;   // display list may be stale; refetch next time
            return {};
        }
        PixelBuffer buf = CGImageToBuffer(image, w, h);
        CGImageRelease(image);
        return buf;
    }

    SCDisplay* FindSCDisplay(CGDirectDisplayID displayID) API_AVAILABLE(macos(14.0)) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            SCShareableContent* content = (SCShareableContent*)m_shareable;
            if (content) {
                for (SCDisplay* d in content.displays)
                    if (d.displayID == displayID) return d;
            }
            if (attempt == 1) break;

            // (Re)fetch shareable content synchronously
            __block SCShareableContent* fetched = nil;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                                       onScreenWindowsOnly:YES
                                                         completionHandler:
                ^(SCShareableContent* c, NSError* error) {
                    if (c && !error) fetched = c;
                    dispatch_semaphore_signal(sem);
                }];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
            m_shareable = fetched;
        }
        return nil;
    }

    PixelBuffer CaptureLegacy(CGDirectDisplayID display, CGRect rect, int w, int h) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        CGImageRef shot = CGDisplayCreateImageForRect(display, rect);
#pragma clang diagnostic pop
        if (!shot) return {};
        PixelBuffer buf = CGImageToBuffer(shot, w, h);
        CGImageRelease(shot);
        return buf;
    }

    std::vector<uint32_t> CaptureFullScreen(int& out_w, int& out_h) override {
        out_w = 0; out_h = 0;
        CGDirectDisplayID display = CGMainDisplayID();
        CGRect bounds = CGDisplayBounds(display);
        int sw = (int)bounds.size.width;
        int sh = (int)bounds.size.height;
        PixelBuffer buf = CaptureRegion((int)bounds.origin.x, (int)bounds.origin.y, sw, sh);
        if (buf.Empty()) return {};
        out_w = sw; out_h = sh;
        return buf.pixels;
    }

    std::mutex m_mutex;          // schedulers + trigger thread may call concurrently
    id m_shareable = nil;        // cached SCShareableContent (macOS 14+ only)
};

std::unique_ptr<IPixelChecker> CreatePixelChecker() {
    return std::make_unique<MacOSPixelChecker>();
}
