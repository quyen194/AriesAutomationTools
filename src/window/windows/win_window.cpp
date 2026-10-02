#include "window/window_finder.hpp"
#include "window/pixel_checker.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <algorithm>

// ── Window Finder ─────────────────────────────────────────────────────────────

// Window titles/classes are read via the wide APIs and converted to UTF-8.
// The *A variants return the ANSI codepage (CP932 on Japanese Windows), which is
// not valid UTF-8: ImGui shows garbage and nlohmann::json throws on dump().
static std::string Utf8FromWide(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.resize((size_t)n - 1);   // drop the terminating NUL
    return s;
}

static std::string WindowTitleUtf8(HWND hwnd) {
    wchar_t buf[512]{};
    GetWindowTextW(hwnd, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    return Utf8FromWide(buf);
}

static std::string WindowClassUtf8(HWND hwnd) {
    wchar_t buf[512]{};
    GetClassNameW(hwnd, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    return Utf8FromWide(buf);
}

static WindowInfo InfoFromHwnd(HWND hwnd) {
    WindowInfo info;
    if (!hwnd) return info;
    info.handle     = (uint64_t)(uintptr_t)hwnd;
    info.title      = WindowTitleUtf8(hwnd);
    info.class_name = WindowClassUtf8(hwnd);
    RECT r{};
    GetWindowRect(hwnd, &r);
    info.rect = {r.left, r.top, r.right - r.left, r.bottom - r.top};
    return info;
}

struct EnumData {
    std::string target;
    bool        byClass = false;
    WindowInfo  result;
    bool        found = false;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    auto* d = reinterpret_cast<EnumData*>(lParam);
    if (d->target == (d->byClass ? WindowClassUtf8(hwnd) : WindowTitleUtf8(hwnd))) {
        d->result = InfoFromHwnd(hwnd);
        d->found  = true;
        return FALSE; // stop enumeration
    }
    return TRUE;
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

    std::vector<uint32_t> CaptureFullScreen(int& out_w, int& out_h) override {
        out_w = 0; out_h = 0;
        if (!m_dc) return {};
        // Use virtual screen metrics to handle multi-monitor setups
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (vw <= 0 || vh <= 0) return {};

        HDC mem = CreateCompatibleDC(m_dc);
        if (!mem) return {};
        HBITMAP bmp = CreateCompatibleBitmap(m_dc, vw, vh);
        if (!bmp) { DeleteDC(mem); return {}; }

        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, vw, vh, m_dc, vx, vy, SRCCOPY);

        BITMAPINFO bi{};
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = vw;
        bi.bmiHeader.biHeight      = -vh;
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        std::vector<uint32_t> pixels;
        std::vector<uint8_t> raw((size_t)vw * vh * 4);
        if (GetDIBits(mem, bmp, 0, vh, raw.data(), &bi, DIB_RGB_COLORS) == vh) {
            pixels.resize((size_t)vw * vh);
            for (size_t i = 0; i < pixels.size(); ++i) {
                pixels[i] = ((uint32_t)raw[i*4+2] << 16) |
                            ((uint32_t)raw[i*4+1] <<  8) |
                             (uint32_t)raw[i*4+0];
            }
            out_w = vw; out_h = vh;
        }

        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        return pixels;
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
