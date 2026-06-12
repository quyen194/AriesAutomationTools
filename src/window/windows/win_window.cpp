#include "window/window_finder.hpp"
#include "window/pixel_checker.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <algorithm>

// ── Window Finder ─────────────────────────────────────────────────────────────

struct EnumData {
    std::string target;
    bool        byClass = false;
    WindowInfo  result;
    bool        found = false;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    auto* d = reinterpret_cast<EnumData*>(lParam);

    char buf[512]{};
    if (d->byClass)
        GetClassNameA(hwnd, buf, sizeof(buf));
    else
        GetWindowTextA(hwnd, buf, sizeof(buf));

    if (d->target == buf) {
        RECT r{};
        GetWindowRect(hwnd, &r);
        d->result.handle     = (uint64_t)(uintptr_t)hwnd;
        d->result.class_name = [&]{ char c[512]{}; GetClassNameA(hwnd,c,sizeof(c)); return std::string(c); }();
        d->result.title      = [&]{ char t[512]{}; GetWindowTextA(hwnd,t,sizeof(t)); return std::string(t); }();
        d->result.rect       = {r.left, r.top, r.right - r.left, r.bottom - r.top};
        d->found = true;
        return FALSE; // stop enumeration
    }
    return TRUE;
}

static WindowInfo InfoFromHwnd(HWND hwnd) {
    WindowInfo info;
    if (!hwnd) return info;
    info.handle = (uint64_t)(uintptr_t)hwnd;
    char buf[512]{};
    GetWindowTextA(hwnd, buf, sizeof(buf));
    info.title = buf;
    GetClassNameA(hwnd, buf, sizeof(buf));
    info.class_name = buf;
    RECT r{};
    GetWindowRect(hwnd, &r);
    info.rect = {r.left, r.top, r.right - r.left, r.bottom - r.top};
    return info;
}

class WinWindowFinder : public IWindowFinder {
public:
    std::optional<WindowInfo> FindByTitle(const std::string& title) override {
        EnumData d; d.target = title; d.byClass = false;
        EnumWindows(EnumWindowsProc, (LPARAM)&d);
        if (d.found) return d.result;
        return std::nullopt;
    }

    std::optional<WindowInfo> FindByClass(const std::string& cls) override {
        EnumData d; d.target = cls; d.byClass = true;
        EnumWindows(EnumWindowsProc, (LPARAM)&d);
        if (d.found) return d.result;
        return std::nullopt;
    }

    std::optional<WindowInfo> FindByHandle(uint64_t handle) override {
        HWND hwnd = (HWND)(uintptr_t)handle;
        if (!IsWindow(hwnd)) return std::nullopt;
        return InfoFromHwnd(hwnd);
    }

    std::pair<int,int> ClientToScreen(uint64_t handle, int x, int y) override {
        HWND hwnd = (HWND)(uintptr_t)handle;
        POINT p{x, y};
        ::ClientToScreen(hwnd, &p);
        return {p.x, p.y};
    }

    std::optional<WindowInfo> WindowUnderCursor() override {
        POINT p{};
        GetCursorPos(&p);
        HWND hwnd = WindowFromPoint(p);
        if (!hwnd) return std::nullopt;
        // Walk up to top-level window
        HWND top = GetAncestor(hwnd, GA_ROOT);
        if (top) hwnd = top;
        return InfoFromHwnd(hwnd);
    }
};

std::unique_ptr<IWindowFinder> CreateWindowFinder() {
    return std::make_unique<WinWindowFinder>();
}

// ── Pixel Checker ─────────────────────────────────────────────────────────────

class WinPixelChecker : public IPixelChecker {
public:
    WinPixelChecker()  { m_dc = GetDC(nullptr); }
    ~WinPixelChecker() { if (m_dc) ReleaseDC(nullptr, m_dc); }

    uint32_t GetPixelRGB(int x, int y) override {
        COLORREF c = ::GetPixel(m_dc, x, y);
        if (c == CLR_INVALID) return 0;
        return ((uint32_t)GetRValue(c) << 16) |
               ((uint32_t)GetGValue(c) <<  8) |
                (uint32_t)GetBValue(c);
    }

    PixelBuffer CaptureRegion(int x, int y, int w, int h) override {
        PixelBuffer buf;
        if (w <= 0 || h <= 0 || !m_dc) return buf;

        HDC mem = CreateCompatibleDC(m_dc);
        if (!mem) return buf;
        HBITMAP bmp = CreateCompatibleBitmap(m_dc, w, h);
        if (!bmp) { DeleteDC(mem); return buf; }

        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, w, h, m_dc, x, y, SRCCOPY);

        BITMAPINFO bi{};
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h; // negative = top-down rows
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        std::vector<uint8_t> raw((size_t)w * h * 4);
        if (GetDIBits(mem, bmp, 0, h, raw.data(), &bi, DIB_RGB_COLORS) == h) {
            buf.width  = w;
            buf.height = h;
            buf.pixels.resize((size_t)w * h);
            for (size_t i = 0; i < buf.pixels.size(); ++i) {
                // DIB memory layout is BGRA
                buf.pixels[i] = ((uint32_t)raw[i*4+2] << 16) |
                                ((uint32_t)raw[i*4+1] <<  8) |
                                 (uint32_t)raw[i*4+0];
            }
        }

        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        return buf;
    }

private:
    HDC m_dc = nullptr;
};

std::unique_ptr<IPixelChecker> CreatePixelChecker() {
    return std::make_unique<WinPixelChecker>();
}
