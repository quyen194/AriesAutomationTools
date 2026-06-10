#include "window/window_finder.hpp"
#include "window/pixel_checker.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdexcept>
#include <cstring>

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string GetWindowTitle(Display* dpy, Window w) {
    char* name = nullptr;
    if (XFetchName(dpy, w, &name) && name) {
        std::string s(name);
        XFree(name);
        return s;
    }
    return {};
}

static std::string GetWindowClass(Display* dpy, Window w) {
    XClassHint hint{};
    if (XGetClassHint(dpy, w, &hint)) {
        std::string s(hint.res_class ? hint.res_class : "");
        if (hint.res_name)  XFree(hint.res_name);
        if (hint.res_class) XFree(hint.res_class);
        return s;
    }
    return {};
}

static WindowInfo MakeInfo(Display* dpy, Window w) {
    WindowInfo info;
    info.handle     = (uint64_t)w;
    info.title      = GetWindowTitle(dpy, w);
    info.class_name = GetWindowClass(dpy, w);
    XWindowAttributes attr{};
    if (XGetWindowAttributes(dpy, w, &attr)) {
        // Translate to root-relative coords
        int rx = 0, ry = 0;
        Window child;
        XTranslateCoordinates(dpy, w, DefaultRootWindow(dpy), 0, 0, &rx, &ry, &child);
        info.rect = {rx, ry, attr.width, attr.height};
    }
    return info;
}

struct SearchData {
    Display*    dpy;
    std::string target;
    bool        byClass;
    Window      result = None;
};

static void SearchWindows(Display* dpy, Window root, SearchData& d) {
    Window parent;
    Window* children = nullptr;
    unsigned int n = 0;
    if (!XQueryTree(dpy, root, &root, &parent, &children, &n)) return;

    for (unsigned int i = 0; i < n; ++i) {
        std::string val = d.byClass ? GetWindowClass(dpy, children[i])
                                    : GetWindowTitle(dpy, children[i]);
        if (val == d.target) { d.result = children[i]; break; }
        SearchWindows(dpy, children[i], d);
        if (d.result != None) break;
    }
    if (children) XFree(children);
}

// ── Window Finder ─────────────────────────────────────────────────────────────

class LinuxWindowFinder : public IWindowFinder {
public:
    LinuxWindowFinder() {
        m_dpy = XOpenDisplay(nullptr);
        if (!m_dpy) throw std::runtime_error("Cannot open X display");
    }
    ~LinuxWindowFinder() { if (m_dpy) XCloseDisplay(m_dpy); }

    std::optional<WindowInfo> FindByTitle(const std::string& title) override {
        SearchData d{m_dpy, title, false};
        SearchWindows(m_dpy, DefaultRootWindow(m_dpy), d);
        if (d.result == None) return std::nullopt;
        return MakeInfo(m_dpy, d.result);
    }
    std::optional<WindowInfo> FindByClass(const std::string& cls) override {
        SearchData d{m_dpy, cls, true};
        SearchWindows(m_dpy, DefaultRootWindow(m_dpy), d);
        if (d.result == None) return std::nullopt;
        return MakeInfo(m_dpy, d.result);
    }
    std::optional<WindowInfo> FindByHandle(uint64_t handle) override {
        Window w = (Window)handle;
        XWindowAttributes attr{};
        if (!XGetWindowAttributes(m_dpy, w, &attr)) return std::nullopt;
        return MakeInfo(m_dpy, w);
    }
    std::pair<int,int> ClientToScreen(uint64_t handle, int x, int y) override {
        int rx = 0, ry = 0;
        Window child;
        XTranslateCoordinates(m_dpy, (Window)handle,
            DefaultRootWindow(m_dpy), x, y, &rx, &ry, &child);
        return {rx, ry};
    }
    std::optional<WindowInfo> WindowUnderCursor() override {
        Window root, child;
        int rx, ry, wx, wy;
        unsigned int mask;
        XQueryPointer(m_dpy, DefaultRootWindow(m_dpy),
                      &root, &child, &rx, &ry, &wx, &wy, &mask);
        if (child == None) return std::nullopt;
        return MakeInfo(m_dpy, child);
    }

private:
    Display* m_dpy = nullptr;
};

std::unique_ptr<IWindowFinder> CreateWindowFinder() {
    return std::make_unique<LinuxWindowFinder>();
}

// ── Pixel Checker ─────────────────────────────────────────────────────────────

class LinuxPixelChecker : public IPixelChecker {
public:
    LinuxPixelChecker() {
        m_dpy = XOpenDisplay(nullptr);
        if (!m_dpy) throw std::runtime_error("Cannot open X display");
    }
    ~LinuxPixelChecker() { if (m_dpy) XCloseDisplay(m_dpy); }

    uint32_t GetPixelRGB(int x, int y) override {
        XImage* img = XGetImage(m_dpy, DefaultRootWindow(m_dpy),
                                x, y, 1, 1, AllPlanes, ZPixmap);
        if (!img) return 0;
        unsigned long px = XGetPixel(img, 0, 0);
        XDestroyImage(img);
        uint8_t r = (px & img->red_mask)   >> 16;
        uint8_t g = (px & img->green_mask) >>  8;
        uint8_t b = (px & img->blue_mask);
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

private:
    Display* m_dpy = nullptr;
};

std::unique_ptr<IPixelChecker> CreatePixelChecker() {
    return std::make_unique<LinuxPixelChecker>();
}
