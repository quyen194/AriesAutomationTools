#include "tray_manager.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <cstdint>
#include <vector>
#include <string>

// NIN_SELECT is not always defined in older SDKs
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif

// Menu ID layout:
//   1001 = Exit
//   1002 = StartAll
//   1003 = StopAll
//   1004 = PauseAll
//   1005 = ResumeAll
//   2000+n = Start workflow[n]
//   3000+n = Stop  workflow[n]
//   4000+n = Pause workflow[n]
//   5000+n = Resume workflow[n]

#define IDM_EXIT        1001
#define IDM_STARTALL    1002
#define IDM_STOPALL     1003
#define IDM_PAUSEALL    1004
#define IDM_RESUMEALL   1005
#define IDM_BASE_START  2000
#define IDM_BASE_STOP   3000
#define IDM_BASE_PAUSE  4000
#define IDM_BASE_RESUME 5000
#define WM_TRAYICON     (WM_USER + 1)

static const char* kWndClass = "AriesTrayHost";

static HICON CreateIconFromRGBA(const uint8_t* pixels, int w, int h) {
    BITMAPV5HEADER bi{};
    bi.bV5Size        = sizeof(BITMAPV5HEADER);
    bi.bV5Width       = w;
    bi.bV5Height      = -h;  // top-down
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    HDC hdc = GetDC(nullptr);
    uint8_t* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi),
                                     DIB_RGB_COLORS, reinterpret_cast<void**>(&bits),
                                     nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hBmp) return nullptr;

    // Convert source RGBA -> BGRA (Windows DIB order)
    for (int i = 0; i < w * h; ++i) {
        bits[i * 4 + 0] = pixels[i * 4 + 2]; // B
        bits[i * 4 + 1] = pixels[i * 4 + 1]; // G
        bits[i * 4 + 2] = pixels[i * 4 + 0]; // R
        bits[i * 4 + 3] = pixels[i * 4 + 3]; // A
    }

    HBITMAP hMask = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO ii   = {TRUE, 0, 0, hMask, hBmp};
    HICON hIcon   = CreateIconIndirect(&ii);
    DeleteObject(hMask);
    DeleteObject(hBmp);
    return hIcon;
}

struct TrayManager::Impl {
    HWND   hwnd  = nullptr;
    HICON  hIcon = nullptr;
    bool   added = false;

    std::vector<TrayWorkflowDesc> workflows;
    std::vector<TrayPendingAction> pending;
    std::string globalHotkeyLabel;

    // ── helpers ────────────────────────────────────────────────────────────────

    static HMENU BuildSubMenu(const std::vector<TrayWorkflowDesc>& wfs,
                               int idBase,
                               bool (*filter)(const TrayWorkflowDesc&))
    {
        HMENU m = CreatePopupMenu();
        int n = 0;
        for (int i = 0; i < (int)wfs.size(); ++i) {
            if (!filter(wfs[i])) continue;
            AppendMenuA(m, MF_STRING, idBase + i, wfs[i].name.c_str());
            ++n;
        }
        if (n == 0) AppendMenuA(m, MF_STRING | MF_GRAYED, 0, "(none)");
        return m;
    }

    void ShowContextMenu() {
        POINT pt;
        GetCursorPos(&pt);

        // SetForegroundWindow only works on a VISIBLE window. The helper hwnd is
        // normally invisible (1×1, WS_EX_TOOLWINDOW), so we must show it first.
        // Without this, TrackPopupMenuEx shows the menu but every item click is
        // silently swallowed — the menu dismisses and returns 0.
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);

        HMENU menu = CreatePopupMenu();

        // Show/Hide main window
        AppendMenuA(menu, MF_STRING, IDM_EXIT + 100, "Show/Hide Window");
        AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);

        // Bulk actions — show global hotkey hint next to Start/Stop labels if set
        {
            std::string lbl = "Start All Workflows";
            if (!globalHotkeyLabel.empty()) { lbl += '\t'; lbl += globalHotkeyLabel; }
            AppendMenuA(menu, MF_STRING, IDM_STARTALL, lbl.c_str());
        }
        {
            std::string lbl = "Stop All Workflows";
            if (!globalHotkeyLabel.empty()) { lbl += '\t'; lbl += globalHotkeyLabel; }
            AppendMenuA(menu, MF_STRING, IDM_STOPALL, lbl.c_str());
        }
        AppendMenuA(menu, MF_STRING, IDM_PAUSEALL,  "Pause All Workflows");
        AppendMenuA(menu, MF_STRING, IDM_RESUMEALL, "Resume All Workflows");

        if (!workflows.empty()) {
            AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);

            // Start submenu: workflows that are not running
            HMENU subStart = BuildSubMenu(workflows, IDM_BASE_START,
                [](const TrayWorkflowDesc& w){ return !w.running; });
            AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(subStart), "Start Workflow...");

            // Stop submenu: workflows that are running
            HMENU subStop = BuildSubMenu(workflows, IDM_BASE_STOP,
                [](const TrayWorkflowDesc& w){ return w.running; });
            AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(subStop), "Stop Workflow...");

            // Pause submenu: running and not paused
            HMENU subPause = BuildSubMenu(workflows, IDM_BASE_PAUSE,
                [](const TrayWorkflowDesc& w){ return w.running && !w.paused; });
            AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(subPause), "Pause Workflow...");

            // Resume submenu: running and paused
            HMENU subResume = BuildSubMenu(workflows, IDM_BASE_RESUME,
                [](const TrayWorkflowDesc& w){ return w.running && w.paused; });
            AppendMenuA(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(subResume), "Resume Workflow...");
        }

        AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(menu, MF_STRING, IDM_EXIT, "Exit");

        UINT cmd = TrackPopupMenuEx(menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
            pt.x, pt.y, hwnd, nullptr);
        DestroyMenu(menu);
        PostMessage(hwnd, WM_NULL, 0, 0);

        // Re-hide the helper window now that the menu is dismissed.
        ShowWindow(hwnd, SW_HIDE);

        if (cmd == 0) return;

        if (cmd == IDM_EXIT) {
            pending.push_back({TrayAction::Exit, ""});
        } else if (cmd == IDM_EXIT + 100) {
            pending.push_back({TrayAction::ShowWindow, ""});
        } else if (cmd == IDM_STARTALL) {
            pending.push_back({TrayAction::StartAll, ""});
        } else if (cmd == IDM_STOPALL) {
            pending.push_back({TrayAction::StopAll, ""});
        } else if (cmd == IDM_PAUSEALL) {
            pending.push_back({TrayAction::PauseAll, ""});
        } else if (cmd == IDM_RESUMEALL) {
            pending.push_back({TrayAction::ResumeAll, ""});
        } else if (cmd >= IDM_BASE_START && cmd < IDM_BASE_STOP) {
            int idx = (int)(cmd - IDM_BASE_START);
            if (idx < (int)workflows.size())
                pending.push_back({TrayAction::StartWorkflow, workflows[idx].id});
        } else if (cmd >= IDM_BASE_STOP && cmd < IDM_BASE_PAUSE) {
            int idx = (int)(cmd - IDM_BASE_STOP);
            if (idx < (int)workflows.size())
                pending.push_back({TrayAction::StopWorkflow, workflows[idx].id});
        } else if (cmd >= IDM_BASE_PAUSE && cmd < IDM_BASE_RESUME) {
            int idx = (int)(cmd - IDM_BASE_PAUSE);
            if (idx < (int)workflows.size())
                pending.push_back({TrayAction::PauseWorkflow, workflows[idx].id});
        } else if (cmd >= IDM_BASE_RESUME && cmd < IDM_BASE_RESUME + 1000) {
            int idx = (int)(cmd - IDM_BASE_RESUME);
            if (idx < (int)workflows.size())
                pending.push_back({TrayAction::ResumeWorkflow, workflows[idx].id});
        }
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_CREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA,
                             reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }

        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProc(hwnd, msg, wParam, lParam);

        if (msg == WM_TRAYICON) {
            // With NOTIFYICON_VERSION_4: LOWORD(lParam) = event, HIWORD(lParam) = icon id.
            // Windows 7+ sends WM_CONTEXTMENU (not WM_RBUTTONUP) on right-click.
            // VERSION_4 also replaces WM_LBUTTONUP with NIN_SELECT on single left-click.
            UINT ev = LOWORD(lParam);
            if (ev == WM_LBUTTONDBLCLK || ev == WM_LBUTTONUP || ev == NIN_SELECT) {
                self->pending.push_back({TrayAction::ShowWindow, ""});
            } else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU || ev == NIN_KEYSELECT) {
                self->ShowContextMenu();
            }
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    bool RegisterClass() {
        WNDCLASSEXA wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.lpszClassName = kWndClass;
        return RegisterClassExA(&wc) != 0;
    }
};

// ── TrayManager public API ────────────────────────────────────────────────────

TrayManager::TrayManager()  = default;
TrayManager::~TrayManager() { Shutdown(); }

void TrayManager::Init(const uint8_t* iconPixels, int iconW, int iconH) {
    m_impl = new Impl();

    // Register window class (may already exist on second init — ignore error)
    m_impl->RegisterClass();

    m_impl->hwnd = CreateWindowExA(
        WS_EX_TOOLWINDOW,
        kWndClass, "AriesTray",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        m_impl);

    if (!m_impl->hwnd) return;

    m_impl->hIcon = CreateIconFromRGBA(iconPixels, iconW, iconH);

    NOTIFYICONDATAA nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_impl->hwnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = m_impl->hIcon;
    strncpy_s(nid.szTip, "Aries Automation Tools", _TRUNCATE);

    Shell_NotifyIconA(NIM_ADD, &nid);

    // Request modern balloon notifications behavior
    NOTIFYICONDATAA nidv4{};
    nidv4.cbSize  = sizeof(nidv4);
    nidv4.hWnd    = m_impl->hwnd;
    nidv4.uID     = 1;
    nidv4.uVersion= NOTIFYICON_VERSION_4;
    Shell_NotifyIconA(NIM_SETVERSION, &nidv4);

    m_impl->added = true;
}

void TrayManager::Shutdown() {
    if (!m_impl) return;
    if (m_impl->added && m_impl->hwnd) {
        NOTIFYICONDATAA nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = m_impl->hwnd;
        nid.uID    = 1;
        Shell_NotifyIconA(NIM_DELETE, &nid);
    }
    if (m_impl->hwnd) DestroyWindow(m_impl->hwnd);
    if (m_impl->hIcon) DestroyIcon(m_impl->hIcon);
    delete m_impl;
    m_impl = nullptr;
}

void TrayManager::UpdateWorkflows(const std::vector<TrayWorkflowDesc>& wfs) {
    if (m_impl) m_impl->workflows = wfs;
}

void TrayManager::UpdateIcon(const uint8_t* pixels, int w, int h) {
    if (!m_impl || !m_impl->added) return;
    if (m_impl->hIcon) { DestroyIcon(m_impl->hIcon); m_impl->hIcon = nullptr; }
    m_impl->hIcon = CreateIconFromRGBA(pixels, w, h);
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_impl->hwnd;
    nid.uID    = 1;
    nid.uFlags = NIF_ICON;
    nid.hIcon  = m_impl->hIcon;
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

void TrayManager::SetGlobalHotkeyLabel(const std::string& label) {
    if (m_impl) m_impl->globalHotkeyLabel = label;
}

void TrayManager::Poll(std::vector<TrayPendingAction>& out) {
    if (!m_impl || !m_impl->hwnd) return;

    // SDL_PollEvent uses PeekMessage(NULL,...) which drains messages for all
    // windows in the thread, including our hidden HWND. WndProc/ShowContextMenu
    // may therefore have already pushed to pending before Poll is called.
    // Run our own pump to catch any remaining messages, then consume pending.
    MSG msg;
    while (PeekMessage(&msg, m_impl->hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);  // calls WndProc → may call ShowContextMenu
    }

    out = std::move(m_impl->pending);
    m_impl->pending.clear();
}
