#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <uiautomation.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include "kb.h"
#include "render.h"
#include "input.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uiautomationcore.lib")
#pragma comment(lib, "dwmapi.lib")

static const wchar_t* kClass = L"Win10XKeyboardM1";
static const UINT TIMER_IME = 1;
static const UINT TIMER_REPEAT = 2;
static const UINT TIMER_AUTOCLOSE = 3;
static const UINT TIMER_SELFTEST = 4;
static const UINT TIMER_ALT = 5;
static const UINT TIMER_LAYOUT = 6;

static const UINT WM_TRAY = WM_APP + 10;
static const UINT WM_SHOW_KEYBOARD = WM_APP + 11;
static const UINT WM_TRAY_INIT = WM_APP + 12;
static const UINT WM_RAISE_KEYBOARD = WM_APP + 13;

struct MonInfo {
    RECT rc;
    std::wstring dev;
    bool primary = false;
};

struct App {
    HWND hwnd = nullptr;
    Layout L;
    Renderer R;

    float scale = 1.0f;
    float widthDip = 0.0f;
    float heightDip = 0.0f;

    int pressed = -1;
    UINT activePointer = 0;
    bool usePointer = false;

    int ime = -1;
    std::wstring target = L"—";
    std::wstring monitor = L"";
    HWND lastFg = nullptr;

    bool shiftOn = false, ctrlOn = false, altOn = false, winOn = false, capsOn = false, fnOn = false;
    bool winPending = false;
    ULONGLONG winDownTick = 0;
    bool shiftPending = false;
    ULONGLONG shiftDownTick = 0;
    bool altPhysDown = false;

    UINT repVk = 0;
    bool repShift = false, repCtrl = false, repAlt = false, repWin = false;
    bool repeating = false;

    bool preview = false;
    bool topmost = true;
    bool selftest = false;
    bool log = false;
    int  autocloseMs = 0;
    int  monitorIndex = 0;
    int  screenMode = 0;      // 0 auto, 1 both panels (landscape), 2 left only, 3 right only
    bool split = false;       // effective: landscape split layout active
    int  viewMode = 0;        // 0 portrait, 1 split, 2 left half, 3 right half
    int  splitLayer = 0;      // 0 = letters, 1 = numbers/symbols (split mode)
    RECT fullRect = { 0, 0, 0, 0 };

    std::vector<MonInfo> mons;
    std::vector<MonButton> monBtns;
    std::vector<MonButton> zoomBtns;
    std::vector<MonButton> toolBtns;
    ULONGLONG padLaunchTick = 0;
    float zoom = 1.0f;
    bool dark = true;
    int  themeOverride = -1;   // -1 = follow system, 0 = light, 1 = dark

    bool visible = true;
    bool menuOpen = false;      // a popup menu is up; do not steal its z-order
    bool modeMenuOpen = false;  // in-app mode menu (gear) is open
    bool quitting = false;
    bool software = false;      // --software: force the WARP/software D2D target
    bool autoShow = true;       // pop up when a text field gains focus (default on)
    bool acrylic = false;       // blurred screenshot backdrop; opt in with --acrylic
    HBITMAP bgBmp = nullptr;
    int bgW = 0, bgH = 0;
    bool bgDirty = false;
    bool trayAdded = false;
    NOTIFYICONDATAW nid;
    IUIAutomation* uia = nullptr;
    IUIAutomationFocusChangedEventHandler* focusHandler = nullptr;
};

static App* g = nullptr;
static HANDLE g_uiaStop = nullptr;

static void ForceTopmost(App* a);

static void Log(const wchar_t* fmt, ...) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, L"kb_log.txt", L"a, ccs=UTF-8") != 0 || !f) return;
    va_list ap;
    va_start(ap, fmt);
    vfwprintf_s(f, fmt, ap);
    va_end(ap);
    fputws(L"\n", f);
    fclose(f);
}

// ---- persisted user preferences ----
static bool LoadAcrylicPref() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\DuoKeyboard", 0,
                      KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) {
        return false;
    }
    DWORD v = 0, cb = sizeof(v), type = 0;
    LONG rc = RegQueryValueExW(k, L"Acrylic", nullptr, &type, (BYTE*)&v, &cb);
    RegCloseKey(k);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return false;
    return v != 0;
}

static void SaveAcrylicPref(bool on) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\DuoKeyboard", 0, nullptr,
                        0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) {
        return;
    }
    DWORD v = on ? 1 : 0;
    RegSetValueExW(k, L"Acrylic", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(k);
}

static int LoadMonitorPref() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\DuoKeyboard", 0,
                      KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) {
        return -1;
    }
    DWORD v = 0, cb = sizeof(v), type = 0;
    LONG rc = RegQueryValueExW(k, L"Monitor", nullptr, &type, (BYTE*)&v, &cb);
    RegCloseKey(k);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return -1;
    return (int)v;
}

static void SaveMonitorPref(int idx) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\DuoKeyboard", 0, nullptr,
                        0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) {
        return;
    }
    DWORD v = (DWORD)idx;
    RegSetValueExW(k, L"Monitor", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(k);
}

static bool IsDarkTheme() {
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &h) != ERROR_SUCCESS) {
        return true;                     // unknown -> dark (safe default)
    }
    DWORD v = 0;
    DWORD sz = sizeof(v);
    DWORD type = 0;
    LONG r = RegQueryValueExW(h, L"AppsUseLightTheme", nullptr, &type,
                              reinterpret_cast<BYTE*>(&v), &sz);
    RegCloseKey(h);
    if (r != ERROR_SUCCESS || type != REG_DWORD || sz != sizeof(DWORD)) {
        return true;                     // value missing/garbled -> dark
    }
    return v == 0;
}

static std::vector<MonInfo> EnumMons() {
    std::vector<MonInfo> v;
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR h, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* out = reinterpret_cast<std::vector<MonInfo>*>(lp);
            MONITORINFOEXW mi;
            ZeroMemory(&mi, sizeof(mi));
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(h, &mi)) {
                MonInfo m;
                m.rc = mi.rcMonitor;
                m.dev = mi.szDevice;
                m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                out->push_back(m);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&v));

    std::sort(v.begin(), v.end(), [](const MonInfo& a, const MonInfo& b) {
        if (a.rc.top != b.rc.top) return a.rc.top < b.rc.top;
        return a.rc.left < b.rc.left;
    });
    return v;
}

static void BuildMonButtons(App* a) {
    // Display / zoom live in the settings menu; Touchpad stays in the status
    // row, right aligned.
    a->monBtns.clear();
    a->zoomBtns.clear();
    a->toolBtns.clear();
    if (a->viewMode != 0) return;

    float y = a->L.gridY - 66.0f;
    if (y < 96.0f) y = 96.0f;
    MonButton b;
    b.w = 150.0f;
    b.h = 44.0f;
    b.x = a->L.designW - 16.0f - b.w;
    b.y = y;
    b.label = L"Touchpad";
    a->toolBtns.push_back(b);
}

// The shell's virtual touchpad is a UWP window: a frame hosting a core window.
static BOOL CALLBACK FindPadProc(HWND h, LPARAM lp) {
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, 64);
    wchar_t title[256] = {};
    GetWindowTextW(h, title, 256);

    if (wcsstr(title, L"Touchpad")) {
        HWND core = FindWindowExW(h, nullptr, L"Windows.UI.Core.CoreWindow", nullptr);
        *(HWND*)lp = core ? core : h;
        return FALSE;
    }
    if (lstrcmpW(cls, L"ApplicationFrameWindow") == 0) {
        HWND child = nullptr;
        while ((child = FindWindowExW(h, child, L"Windows.UI.Core.CoreWindow", nullptr)) != nullptr) {
            wchar_t ct[256] = {};
            GetWindowTextW(child, ct, 256);
            if (wcsstr(ct, L"Shell Experience Host") || wcsstr(ct, L"Touchpad")) {
                *(HWND*)lp = child;
                return FALSE;
            }
        }
    }
    return TRUE;
}

static HWND FindVirtualTouchpadChild() {
    HWND found = nullptr;
    EnumWindows(FindPadProc, (LPARAM)&found);
    return found;
}

static void ToggleVirtualTouchpad(App* a) {
    HWND pad = FindVirtualTouchpadChild();
    if (pad) {
        PostMessageW(pad, WM_CLOSE, 0, 0);
        if (a->log) Log(L"virtual touchpad: close");
        return;
    }
    if (GetTickCount64() - a->padLaunchTick < 1200) return;   // debounce
    a->padLaunchTick = GetTickCount64();

    HINSTANCE r = ShellExecuteW(a->hwnd, L"open", L"ms-virtualtouchpad:",
                                nullptr, nullptr, SW_SHOWNORMAL);
    if (a->log) Log(L"virtual touchpad: launch ret=%p", (void*)r);
    (void)r;
}

static void Relayout(App* a) {
    RECT rc;
    GetClientRect(a->hwnd, &rc);
    a->widthDip = (float)(rc.right - rc.left) / a->scale;
    a->heightDip = (float)(rc.bottom - rc.top) / a->scale;
    if (a->widthDip < 100.0f || a->heightDip < 100.0f) return;
    switch (a->viewMode) {
    case 1:
        a->L = BuildSplitLayout(a->widthDip, a->heightDip, a->zoom, a->splitLayer != 0);
        break;
    case 2:
    case 3:
        a->L = BuildPanelLayout(a->widthDip, a->heightDip, a->zoom, a->splitLayer != 0);
        break;
    default:
        a->L = BuildLayout(a->widthDip, a->heightDip, a->zoom);
        break;
    }
    a->L.showModeMenu = a->modeMenuOpen;
    a->L.acrylicOn = a->acrylic;
    a->L.menuSel = (a->screenMode == 0) ? (a->viewMode == 1 ? 1 : 0) : a->screenMode;
    if (a->L.menuPortrait) {
        int n = (int)a->mons.size();
        if (n > 4) n = 4;
        a->L.dispCount = n;
        for (int i = 0; i < n; ++i) {
            wchar_t buf[64];
            swprintf_s(buf, L"Display %d", i + 1);
            a->L.dispLabel[i] = buf;
        }
        a->L.dispSel = a->monitorIndex;
    }
    a->R.SetSizes(a->L.keyH, a->L.keyW);
    BuildMonButtons(a);
    if (a->log) {
        Log(L"Relayout client=%ldx%ld scale=%.3f dip=%.1fx%.1f key=%.1fx%.1f gridY=%.1f gridH=%.1f",
            (long)(rc.right - rc.left), (long)(rc.bottom - rc.top), a->scale,
            a->widthDip, a->heightDip, a->L.keyW, a->L.keyH, a->L.gridY, a->L.gridH);
    }
}

static void BoxBlurBitmap(HBITMAP bmp, int w, int h, int radius) {
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<DWORD> px((size_t)w * (size_t)h);
    std::vector<DWORD> tmp((size_t)w * (size_t)h);
    HDC dc = GetDC(nullptr);
    if (!GetDIBits(dc, bmp, 0, h, px.data(), &bi, DIB_RGB_COLORS)) {
        ReleaseDC(nullptr, dc);
        return;
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, n = 0;
            for (int k = -radius; k <= radius; ++k) {
                int xx = x + k;
                if (xx < 0 || xx >= w) continue;
                DWORD c = px[(size_t)y * (size_t)w + (size_t)xx];
                sb += c & 0xFF; sg += (c >> 8) & 0xFF; sr += (c >> 16) & 0xFF; ++n;
            }
            tmp[(size_t)y * (size_t)w + (size_t)x] =
                (DWORD)(((sr / n) << 16) | ((sg / n) << 8) | (sb / n));
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0, n = 0;
            for (int k = -radius; k <= radius; ++k) {
                int yy = y + k;
                if (yy < 0 || yy >= h) continue;
                DWORD c = tmp[(size_t)yy * (size_t)w + (size_t)x];
                sb += c & 0xFF; sg += (c >> 8) & 0xFF; sr += (c >> 16) & 0xFF; ++n;
            }
            px[(size_t)y * (size_t)w + (size_t)x] =
                (DWORD)(((sr / n) << 16) | ((sg / n) << 8) | (sb / n));
        }
    }
    SetDIBits(dc, bmp, 0, h, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
}

// Two panels side by side share the same top edge.
static bool MonitorsSideBySide(const std::vector<MonInfo>& mons) {
    if (mons.size() < 2) return false;
    return mons[0].rc.top == mons[1].rc.top && mons[0].rc.left != mons[1].rc.left;
}

// Posture is decided by the panel resolution:
//   1800x1350 (wider than tall) -> book/portrait, keyboard on the lower panel
//   1350x1800 (taller than wide) -> flat landscape, keyboard spans both panels
static bool IsLandscapePosture(const std::vector<MonInfo>& mons, int* outW, int* outH) {
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (mons.empty()) return false;
    const int w = mons[0].rc.right - mons[0].rc.left;
    const int h = mons[0].rc.bottom - mons[0].rc.top;
    if (outW) *outW = w;
    if (outH) *outH = h;

    bool wide = false;
    if (w == 1800 && h == 1350) wide = false;      // 1800x1350 -> single screen
    else if (w == 1350 && h == 1800) wide = true;  // 1350x1800 -> span both
    else wide = (w < h);
    if (wide && !MonitorsSideBySide(mons)) wide = false;
    return wide;
}

// Window rectangle + view mode for the current posture.
//   1 = split (both panels), 2 = left half, 3 = right half, 0 = portrait panel.
static RECT ComputeWindowRect(App* a, int* viewOut) {
    RECT r = { 0, 0, 0, 0 };
    if (viewOut) *viewOut = 0;
    std::vector<MonInfo> mons = EnumMons();
    if (mons.empty()) return r;

    int pw = 0, ph = 0;
    bool landscape = IsLandscapePosture(mons, &pw, &ph);
    int mode = a->screenMode;
    if (mode == 0) mode = landscape ? 1 : 0;
    if (a->log) Log(L"posture: %dx%d -> %s (mode=%d)", pw, ph,
                    landscape ? L"landscape/split" : L"portrait/single", mode);

    if (mode == 1 && landscape) {
        RECT u;
        u.left = (std::min)(mons[0].rc.left, mons[1].rc.left);
        u.top = (std::min)(mons[0].rc.top, mons[1].rc.top);
        u.right = (std::max)(mons[0].rc.right, mons[1].rc.right);
        u.bottom = (std::max)(mons[0].rc.bottom, mons[1].rc.bottom);
        int h = u.bottom - u.top;
        // Keyboard strip: 45% of the panel height, so the keys stay compact.
        int kh = (int)((long long)h * 450 / 1000);
        u.top = u.bottom - kh;
        r = u;
        if (viewOut) *viewOut = 1;
        return r;
    }

    if (mode == 2) {                 // keyboard strip on the left panel
        r = mons[0].rc;
        int h = r.bottom - r.top;
        r.top = r.bottom - (int)((long long)h * 450 / 1000);
        if (viewOut) *viewOut = 2;
        return r;
    }
    if (mode == 3) {                 // keyboard strip on the right panel
        r = mons[mons.size() - 1].rc;
        int h = r.bottom - r.top;
        r.top = r.bottom - (int)((long long)h * 450 / 1000);
        if (viewOut) *viewOut = 3;
        return r;
    }

    // Portrait: keyboard on the lower panel.
    int idx = (int)mons.size() - 1;
    if (a->monitorIndex >= 0 && a->monitorIndex < (int)mons.size())
        idx = a->monitorIndex;
    r = mons[idx].rc;
    return r;
}

// Simulated acrylic: grab what is on the target screen before the keyboard is
// shown, blur it, and use it as a static backdrop.
static void CaptureBackdrop(App* a) {
    if (!a->acrylic) return;
    RECT r = { 0, 0, 0, 0 };
    int view = 0;
    if (a->preview) {
        if (a->hwnd) GetWindowRect(a->hwnd, &r);
    } else {
        r = ComputeWindowRect(a, &view);
    }
    if (r.right <= r.left || r.bottom <= r.top) {
        if (a->hwnd) GetWindowRect(a->hwnd, &r);
    }
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;

    HDC screen = GetDC(nullptr);
    if (!screen) return;
    HDC full = CreateCompatibleDC(screen);
    HBITMAP fullBmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ oldFull = SelectObject(full, fullBmp);
    int sw = w / 10, sh = h / 10;
    if (sw < 16) sw = 16;
    if (sh < 16) sh = 16;
    HDC blurDC = CreateCompatibleDC(screen);
    HBITMAP smallBmp = CreateCompatibleBitmap(screen, sw, sh);
    HGDIOBJ oldSmall = SelectObject(blurDC, smallBmp);

    // Rotation can leave a white transition frame on screen for a moment; if
    // the shot looks blank, wait and grab it again.
    for (int attempt = 0; attempt < 8; ++attempt) {
        BitBlt(full, 0, 0, w, h, screen, r.left, r.top, SRCCOPY);
        SetStretchBltMode(blurDC, HALFTONE);
        SetBrushOrgEx(blurDC, 0, 0, nullptr);
        StretchBlt(blurDC, 0, 0, sw, sh, full, 0, 0, w, h, SRCCOPY);

        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = sw;
        bi.bmiHeader.biHeight = -sh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        std::vector<DWORD> thumb((size_t)sw * (size_t)sh);
        if (GetDIBits(blurDC, smallBmp, 0, sh, thumb.data(), &bi, DIB_RGB_COLORS)) {
            long long sum = 0;
            for (size_t i = 0; i < thumb.size(); ++i) {
                DWORD c = thumb[i];
                sum += (long long)((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF));
            }
            int avg = (int)(sum / (3 * (long long)thumb.size()));
            if (a->log) Log(L"backdrop: rect=(%ld,%ld)-(%ld,%ld) try=%d avg=%d",
                            r.left, r.top, r.right, r.bottom, attempt, avg);
            if (avg < 235 || attempt == 7) break;
        }
        Sleep(220);
    }
    BoxBlurBitmap(smallBmp, sw, sh, 2);
    SetStretchBltMode(full, HALFTONE);
    SetBrushOrgEx(full, 0, 0, nullptr);
    StretchBlt(full, 0, 0, w, h, blurDC, 0, 0, sw, sh, SRCCOPY);

    SelectObject(blurDC, oldSmall);
    DeleteObject(smallBmp);
    DeleteDC(blurDC);
    SelectObject(full, oldFull);
    DeleteDC(full);
    ReleaseDC(nullptr, screen);

    if (a->bgBmp) DeleteObject(a->bgBmp);
    a->bgBmp = fullBmp;
    a->bgW = w;
    a->bgH = h;
    a->bgDirty = true;
    if (a->log) Log(L"backdrop captured %dx%d", w, h);
}

// Hiding and capturing in the same frame can still pick up the composed image
// that contains the keyboard, so let the compositor settle before grabbing.
static void HideAndCaptureBackdrop(App* a) {
    if (!a->acrylic || !a->hwnd) return;
    ShowWindow(a->hwnd, SW_HIDE);

    // The compositor needs a moment before the desktop behind us is back in
    // the frame; repaint the desktop and give it a longer beat.
    RedrawWindow(GetDesktopWindow(), nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
    DwmFlush();
    Sleep(320);
    DwmFlush();

    CaptureBackdrop(a);
}
static void ApplyWindowLayout(App* a) {
    if (a->preview || !a->hwnd) return;
    auto mons = EnumMons();
    if (mons.empty()) return;
    a->mons = mons;
    if (a->monitorIndex < 0 || a->monitorIndex >= (int)a->mons.size())
        a->monitorIndex = (int)a->mons.size() - 1;

    // The new layout covers a different screen area: re-capture the backdrop
    // while the window is hidden, otherwise the old blur stays on screen.
    if (a->acrylic && IsWindowVisible(a->hwnd)) HideAndCaptureBackdrop(a);

    int view = 0;
    RECT r = ComputeWindowRect(a, &view);
    if (r.right <= r.left || r.bottom <= r.top) return;

    a->viewMode = view;
    a->split = (view == 1);
    if (view == 0) a->splitLayer = 0;
    a->fullRect = r;
    a->monitor = a->mons[a->monitorIndex].dev;
    SetWindowPos(a->hwnd, a->topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ForceTopmost(a);
    Relayout(a);
}

static void ShowModeMenu(App* a) {
    // In-app menu: span / left / right, drawn by the renderer.
    a->modeMenuOpen = !a->modeMenuOpen;
    a->L.showModeMenu = a->modeMenuOpen;
    a->L.acrylicOn = a->acrylic;
    a->L.menuSel = a->screenMode;
    InvalidateRect(a->hwnd, nullptr, FALSE);
}

static float PreviewScale() {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    float fitW = (float)sw / 900.0f;
    float fitH = (float)sh / 675.0f;
    float fit = (std::min)(fitW, fitH);
    float dpi = (float)GetDpiForSystem() / 96.0f;
    if (fit > dpi) fit = dpi;
    if (fit > 1.0f) fit = 1.0f;
    if (fit < 0.3f) fit = 0.3f;
    return fit;
}

static void RefreshMonitors(App* a) {
    a->mons = EnumMons();
    if (a->mons.empty()) {
        a->monitorIndex = 0;
    } else if (a->monitorIndex >= (int)a->mons.size()) {
        a->monitorIndex = (int)a->mons.size() - 1;
    }
    BuildMonButtons(a);
}

static void SelectMonitor(App* a, int idx) {
    if (idx < 0 || idx >= (int)a->mons.size()) return;
    a->monitorIndex = idx;
    SaveMonitorPref(idx);
    if (a->log) Log(L"SelectMonitor idx=%d", idx);
    if (a->preview) {
        RECT r = a->mons[idx].rc;
        int w = (int)(900.0f * a->scale);
        int h = (int)(675.0f * a->scale);
        SetWindowPos(a->hwnd, a->topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     r.left + 40, r.top + 40, w, h, SWP_NOACTIVATE);
    } else {
        CaptureBackdrop(a);   // the new screen is still uncovered
        ApplyWindowLayout(a);
    }
    InvalidateRect(a->hwnd, nullptr, FALSE);
}

static void ResizePreview(App* a) {
    RECT wr;
    GetWindowRect(a->hwnd, &wr);
    int w = a->viewMode == 1 ? 1350 : (a->viewMode == 2 || a->viewMode == 3) ? 675 : (int)(900.0f * a->scale);
    int h = a->viewMode == 1 ? 405 : (a->viewMode == 2 || a->viewMode == 3) ? 405 : (int)(675.0f * a->scale);
    SetWindowPos(a->hwnd, nullptr, wr.left, wr.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ForceTopmost(App* a) {
    if (!a->topmost) return;
    SetWindowPos(a->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void PollTarget(App* a) {
    HWND fg = GetForegroundWindow();
    bool changed = false;
    if (fg != a->lastFg) {
        a->lastFg = fg;
        std::wstring t = GetTargetTitle(fg);
        if (t != a->target) { a->target = t; changed = true; }
    }
    int ime = ReadImeState(fg);
    if (ime != a->ime) { a->ime = ime; changed = true; }

    // The taskbar/Start menu are topmost and win the z-order as soon as they
    // are touched; keep re-asserting so the keyboard stays on top after a
    // layout switch too.
    if (a->visible && !a->menuOpen) ForceTopmost(a);

    if (changed) InvalidateRect(a->hwnd, nullptr, FALSE);
}

static bool CanRepeat(KeyAct act) {
    switch (act) {
    case KA_Char: case KA_Back: case KA_Space:
    case KA_Left: case KA_Right: case KA_Up: case KA_Down:
    case KA_Home: case KA_End:
        return true;
    default:
        return false;
    }
}

static void PressDown(App* a, float x, float y) {
    if (a->modeMenuOpen) {
        if (a->L.acrylicBtn.Contains(x, y)) {
            a->acrylic = !a->acrylic;
            SaveAcrylicPref(a->acrylic);      // remember the choice across runs
            a->R.SetAcrylic(a->acrylic);
            a->L.acrylicOn = a->acrylic;
            if (a->acrylic) {
                HideAndCaptureBackdrop(a);
                ShowWindow(a->hwnd, SW_SHOWNOACTIVATE);
                ForceTopmost(a);
            }
            InvalidateRect(a->hwnd, nullptr, FALSE);
            return;
        }
        if (a->L.menuPortrait) {
            for (int i = 0; i < a->L.dispCount; ++i) {
                if (a->L.dispBtn[i].Contains(x, y)) {
                    a->modeMenuOpen = false;
                    a->L.showModeMenu = false;
                    SelectMonitor(a, i);
                    return;
                }
            }
            if (a->L.zoomBtn[0].Contains(x, y) || a->L.zoomBtn[2].Contains(x, y)) {
                if (a->L.zoomBtn[2].Contains(x, y))
                    a->zoom = (std::min)(1.0f, a->zoom + 0.1f);
                else
                    a->zoom = (std::max)(0.6f, a->zoom - 0.1f);
                Relayout(a);
                InvalidateRect(a->hwnd, nullptr, FALSE);
                return;
            }
            a->modeMenuOpen = false;
            a->L.showModeMenu = false;
            InvalidateRect(a->hwnd, nullptr, FALSE);
            return;
        }
        for (int i = 0; i < 3; ++i) {
            if (a->L.modeBtn[i].Contains(x, y)) {
                a->screenMode = i + 1;      // 1 span, 2 left, 3 right
                a->modeMenuOpen = false;
                ApplyWindowLayout(a);
                InvalidateRect(a->hwnd, nullptr, FALSE);
                return;
            }
        }
        a->modeMenuOpen = false;
        a->L.showModeMenu = false;
        InvalidateRect(a->hwnd, nullptr, FALSE);
        return;
    }
    if (a->L.gearBtn.Contains(x, y)) {
        if (a->log) Log(L"chrome: gear");
        ShowModeMenu(a);
        return;
    }
    if ((a->L.split || a->L.half) && a->L.closeBtn2.Contains(x, y)) {
        if (a->log) Log(L"chrome: close");
        PostMessageW(a->hwnd, WM_CLOSE, 0, 0);
        return;
    }
    for (size_t i = 0; i < a->zoomBtns.size(); ++i) {
        const MonButton& b = a->zoomBtns[i];
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
            if (i == 0) {
                a->zoom = (std::max)(0.6f, a->zoom - 0.1f);
            } else if (i == 2) {
                a->zoom = (std::min)(1.0f, a->zoom + 0.1f);
            } else {
                return;
            }
            if (a->log) Log(L"Zoom -> %.0f%%", a->zoom * 100.0f);
            Relayout(a);
            InvalidateRect(a->hwnd, nullptr, FALSE);
            return;
        }
    }
    for (size_t i = 0; i < a->toolBtns.size(); ++i) {
        const MonButton& b = a->toolBtns[i];
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
            ToggleVirtualTouchpad(a);
            return;
        }
    }
    for (size_t i = 0; i < a->monBtns.size(); ++i) {
        const MonButton& b = a->monBtns[i];
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
            SelectMonitor(a, (int)i);
            return;
        }
    }
    if (y < 48.0f && x > a->L.designW - 56.0f) {
        PostMessageW(a->hwnd, WM_CLOSE, 0, 0);
        return;
    }
    int idx = HitTest(a->L, x, y);
    if (a->log) Log(L"PressDown dip=(%.0f,%.0f) idx=%d", x, y, idx);
    if (idx < 0) return;
    a->pressed = idx;
    InvalidateRect(a->hwnd, nullptr, FALSE);

    const Key& k = a->L.keys[idx];
    switch (k.act) {
    case KA_Shift:
        // Quick tap = one-shot shift. Hold >= 300 ms = caps lock.
        if (a->capsOn) { a->capsOn = false; return; }
        if (a->shiftOn) { a->shiftOn = false; return; }
        a->shiftPending = true;
        a->shiftDownTick = GetTickCount64();
        return;
    case KA_Caps:  a->capsOn = !a->capsOn;   return;
    case KA_Ctrl:  a->ctrlOn = !a->ctrlOn;   return;
    case KA_Alt:
        // Alt can stay physically held across Alt+Tab, so releasing is explicit.
        if (a->altOn || a->altPhysDown) {
            if (a->altPhysDown) { SendKeyEvent(VK_MENU, false); a->altPhysDown = false; }
            a->altOn = false;
            KillTimer(a->hwnd, TIMER_ALT);
        } else {
            a->altOn = true;
        }
        return;
    case KA_Win:
        // Quick tap = Start menu. Hold >= 300 ms = latch for Win+key combos.
        if (a->winOn) {
            a->winOn = false;
        } else {
            a->winPending = true;
            a->winDownTick = GetTickCount64();
        }
        return;
    case KA_Fn:    a->fnOn = !a->fnOn;       return;
    case KA_Ime:   return;                   // placeholder, no action yet
    case KA_Layer:
        a->splitLayer ^= 1;
        Relayout(a);
        InvalidateRect(a->hwnd, nullptr, FALSE);
        return;
    case KA_Close: PostMessageW(a->hwnd, WM_CLOSE, 0, 0); return;
    default: break;
    }

    bool alpha = (k.vk >= 'A' && k.vk <= 'Z');
    bool sh = a->shiftOn ^ (alpha && a->capsOn);
    UINT vk = k.vk;

    // Alt+Tab: keep Alt held so repeated Tab keeps cycling the task switcher.
    if (k.act == KA_Tab && a->altOn && !a->ctrlOn && !a->shiftOn && !a->winOn) {
        if (!a->altPhysDown) {
            SendKeyEvent(VK_MENU, true);
            a->altPhysDown = true;
        }
        SendKeyEvent(VK_TAB, true);
        SendKeyEvent(VK_TAB, false);
        SetTimer(a->hwnd, TIMER_ALT, 4000, nullptr);
        return;
    }

    if (a->fnOn) {
        UINT f = 0;
        if (vk >= '1' && vk <= '9')        f = VK_F1 + (vk - '1');
        else if (vk == '0')                f = VK_F10;
        else if (vk == VK_OEM_MINUS)       f = VK_F11;
        else if (vk == VK_OEM_PLUS)        f = VK_F12;
        if (f) { vk = f; sh = false; }
    }

    bool altInChord = a->altOn && !a->altPhysDown;
    SendChord(vk, sh, a->ctrlOn, altInChord, a->winOn);
    if (a->altPhysDown) {
        SendKeyEvent(VK_MENU, false);
        a->altPhysDown = false;
    }

    a->repVk = vk;
    a->repShift = sh;
    a->repCtrl = a->ctrlOn;
    a->repAlt = altInChord;
    a->repWin = a->winOn;

    a->shiftOn = false;
    a->ctrlOn = false;
    a->altOn = false;
    a->winOn = false;
    a->fnOn = false;

    if (CanRepeat(k.act)) {
        a->repeating = true;
        SetTimer(a->hwnd, TIMER_REPEAT, 400, nullptr);
    }
}

static bool ChineseImeActive() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD tid = GetWindowThreadProcessId(fg, nullptr);
    HKL hkl = GetKeyboardLayout(tid);
    if (!hkl) return false;
    return PRIMARYLANGID(LOWORD(hkl)) == LANG_CHINESE;
}

static void PressAt(App* a, float px, float py) {
    float dipX = px / a->scale;
    float dipY = py / a->scale;
    PressDown(a, (dipX - a->L.ox) / a->L.uiScale, (dipY - a->L.oy) / a->L.uiScale);
}

static void PressUp(App* a) {
    if (a->winPending) {
        a->winPending = false;
        ULONGLONG held = GetTickCount64() - a->winDownTick;
        if (a->log) Log(L"Win held=%llu ms", held);
        if (held >= 300) {
            a->winOn = true;              // latch: next key becomes Win+key
        } else {
            SendChord(VK_LWIN, false, false, false, false);   // tap: Start menu
        }
    }
    if (a->shiftPending) {
        a->shiftPending = false;
        ULONGLONG held = GetTickCount64() - a->shiftDownTick;
        if (a->log) Log(L"Shift held=%llu ms", held);
        if (held >= 300) {
            a->capsOn = true;             // lock: caps
        } else if (ChineseImeActive()) {
            // Chinese IME: a bare Shift taps the IME's CN/EN toggle, same as a
            // physical keyboard.
            if (a->log) Log(L"Shift -> IME toggle");
            SendChord(VK_SHIFT, false, false, false, false);
        } else {
            a->shiftOn = true;            // one-shot shift
        }
    }
    a->pressed = -1;
    a->activePointer = 0;
    a->repeating = false;
    KillTimer(a->hwnd, TIMER_REPEAT);
    InvalidateRect(a->hwnd, nullptr, FALSE);
}

static void ShowKeyboard(App* a) {
    if (a->visible) {
        ForceTopmost(a);
        return;
    }
    CaptureBackdrop(a);          // still hidden, so the screen is clean
    a->visible = true;
    ApplyWindowLayout(a);
    ShowWindow(a->hwnd, SW_SHOWNOACTIVATE);
    ForceTopmost(a);
    InvalidateRect(a->hwnd, nullptr, TRUE);
}

static void HideKeyboard(App* a) {
    a->visible = false;
    ShowWindow(a->hwnd, SW_HIDE);
}

static void AddTray(App* a) {
    ZeroMemory(&a->nid, sizeof(a->nid));
    a->nid.cbSize = sizeof(a->nid);
    a->nid.hWnd = a->hwnd;
    a->nid.uID = 1;
    a->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    a->nid.uCallbackMessage = WM_TRAY;
    a->nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(a->nid.szTip, L"Win10X Keyboard", 63);
    a->trayAdded = Shell_NotifyIconW(NIM_ADD, &a->nid) != FALSE;
    if (!a->trayAdded && a->log) {
        DWORD e = GetLastError();
        wchar_t tmp[64];
        swprintf_s(tmp, L"tray add failed err=%lu", e);
        Log(tmp);
    }
}

static void RemoveTray(App* a) {
    if (!a->trayAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &a->nid);
    a->trayAdded = false;
}

static void ShowTrayMenu(App* a) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"Show keyboard");
    AppendMenuW(menu, MF_STRING, 2, L"Hide keyboard");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Exit");

    HWND prev = GetForegroundWindow();
    SetForegroundWindow(a->hwnd);
    a->menuOpen = true;
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, a->hwnd, nullptr);
    a->menuOpen = false;
    DestroyMenu(menu);
    if (prev && prev != a->hwnd && IsWindow(prev)) SetForegroundWindow(prev);

    if (cmd == 1) ShowKeyboard(a);
    else if (cmd == 2) HideKeyboard(a);
    else if (cmd == 3) {
        a->quitting = true;
        PostMessageW(a->hwnd, WM_CLOSE, 0, 0);
    }
}

// UIA focus events: pop the keyboard back up when a text field gets focus.
class FocusHandler : public IUIAutomationFocusChangedEventHandler {
public:
    explicit FocusHandler(HWND hwnd) : hwnd_(hwnd) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IUIAutomationFocusChangedEventHandler)) {
            *ppv = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* el) override {
        if (!el) return S_OK;
        bool isText = false;
        CONTROLTYPEID ct = 0;
        if (SUCCEEDED(el->get_CurrentControlType(&ct)) && ct == UIA_EditControlTypeId) {
            isText = true;
        } else {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(el->GetCurrentPropertyValue(UIA_IsTextPatternAvailablePropertyId, &v)) &&
                v.vt == VT_BOOL && v.boolVal == VARIANT_TRUE) {
                isText = true;
            }
            VariantClear(&v);
        }
        if (isText) PostMessageW(hwnd_, WM_SHOW_KEYBOARD, 0, 0);
        return S_OK;
    }

private:
    LONG ref_ = 1;
    HWND hwnd_;
};

// UIA lives on its own MTA thread: if it blocks (locked-down sessions, broken
// shell), the keyboard window keeps working and only auto-show is unavailable.
static DWORD WINAPI UiaThreadProc(LPVOID param) {
    HWND hwnd = reinterpret_cast<HWND>(param);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* uia = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&uia));
    if (g && g->log) Log(L"UIA co hr=0x%08X", (unsigned)hr);
    if (SUCCEEDED(hr)) {
        FocusHandler* h = new FocusHandler(hwnd);
        HRESULT hr2 = uia->AddFocusChangedEventHandler(nullptr, h);
        if (g && g->log) Log(L"UIA add hr=0x%08X", (unsigned)hr2);
        if (SUCCEEDED(hr2)) {
            if (g_uiaStop) WaitForSingleObject(g_uiaStop, INFINITE);
            uia->RemoveFocusChangedEventHandler(h);
        }
        h->Release();
        uia->Release();
    }
    CoUninitialize();
    return 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* a = g;
    if (!a) return DefWindowProcW(hwnd, msg, wParam, lParam);

    static UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbarCreated) {
        if (!a->trayAdded) AddTray(a);
        return 0;
    }

    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;

    case WM_CREATE:
        a->hwnd = hwnd;
        a->scale = a->preview ? PreviewScale()
                              : (float)GetDpiForWindow(hwnd) / 96.0f;
        if (a->preview) ResizePreview(a);
        Relayout(a);
        RefreshMonitors(a);
        SetTimer(hwnd, TIMER_IME, 250, nullptr);
        if (a->autocloseMs > 0) SetTimer(hwnd, TIMER_AUTOCLOSE, (UINT)a->autocloseMs, nullptr);
        if (a->selftest) SetTimer(hwnd, TIMER_SELFTEST, 1200, nullptr);
        return 0;

    case WM_SIZE:
        Relayout(a);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DPICHANGED: {
        if (a->preview) {
            ResizePreview(a);
            Relayout(a);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        a->scale = (float)HIWORD(wParam) / 96.0f;
        RECT* pr = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, pr->left, pr->top, pr->right - pr->left,
                     pr->bottom - pr->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Relayout(a);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_DISPLAYCHANGE:
        a->screenMode = 0;      // re-detect the posture from the panel resolution
        a->splitLayer = 0;
        RefreshMonitors(a);
        // Rotation shows a white transition frame for a moment; wait it out
        // before re-laying out (and re-capturing the backdrop).
        SetTimer(hwnd, TIMER_LAYOUT, 650, nullptr);
        return 0;

    case WM_SETTINGCHANGE:
        if (a->themeOverride < 0 && lParam) {
            const wchar_t* area = reinterpret_cast<const wchar_t*>(lParam);
            if (area && lstrcmpW(area, L"ImmersiveColorSet") == 0) {
                bool dark = IsDarkTheme();
                if (dark != a->dark) {
                    a->dark = dark;
                    a->R.SetTheme(dark);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (a->log) Log(L"paint: begin %ldx%ld", rc.right - rc.left, rc.bottom - rc.top);
        if (a->R.EnsureTarget(hwnd, rc.right - rc.left, rc.bottom - rc.top)) {
            if (a->log) Log(L"paint: target ok");
            if (a->bgDirty && a->bgBmp) {
                a->R.SetBackdrop(a->bgBmp, a->bgW, a->bgH);
                a->bgDirty = false;
            }
            a->R.Draw(a->L, a->scale, a->widthDip, a->pressed, a->ime, a->target,
                      a->monitor, a->shiftOn, a->capsOn, a->ctrlOn, a->altOn, a->winOn, a->fnOn,
                      a->monBtns, a->monitorIndex, a->zoomBtns, a->toolBtns, ps.hdc);
            if (a->log) Log(L"paint: drawn");
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_POINTERDOWN: {
        UINT id = GET_POINTERID_WPARAM(wParam);
        if (a->activePointer != 0) return 0;
        POINTER_INFO pi;
        ZeroMemory(&pi, sizeof(pi));
        if (!GetPointerInfo(id, &pi)) return 0;
        POINT pt = pi.ptPixelLocation;
        ScreenToClient(hwnd, &pt);
        if (a->log) Log(L"WM_POINTERDOWN id=%u pt=(%ld,%ld) dip=(%d,%d)", id, pt.x, pt.y,
                        (int)(pt.x / a->scale), (int)(pt.y / a->scale));
        a->activePointer = id;
        SetCapture(hwnd);
        PressAt(a, (float)pt.x, (float)pt.y);
        return 0;
    }

    case WM_POINTERUP: {
        UINT id = GET_POINTERID_WPARAM(wParam);
        if (id == a->activePointer) {
            ReleaseCapture();
            PressUp(a);
        }
        return 0;
    }

    case WM_POINTERCAPTURECHANGED:
        a->winPending = false;
        a->shiftPending = false;
        PressUp(a);
        return 0;

    case WM_LBUTTONDOWN:
        if (!a->usePointer) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (a->log) Log(L"WM_LBUTTONDOWN pt=(%ld,%ld)", pt.x, pt.y);
            a->activePointer = 1;
            SetCapture(hwnd);
            PressAt(a, (float)pt.x, (float)pt.y);
        }
        return 0;

    case WM_LBUTTONUP:
        if (!a->usePointer) {
            ReleaseCapture();
            PressUp(a);
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_LAYOUT) {
            KillTimer(hwnd, TIMER_LAYOUT);
            ApplyWindowLayout(a);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == TIMER_IME) {
            PollTarget(a);
        } else if (wParam == TIMER_REPEAT) {
            if (a->repeating) {
                SendChord(a->repVk, a->repShift, a->repCtrl, a->repAlt, a->repWin);
                SetTimer(hwnd, TIMER_REPEAT, 45, nullptr);
            }
        } else if (wParam == TIMER_AUTOCLOSE) {
            a->quitting = true;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else if (wParam == TIMER_ALT) {
            KillTimer(hwnd, TIMER_ALT);
            if (a->altPhysDown) {
                SendKeyEvent(VK_MENU, false);
                a->altPhysDown = false;
            }
            a->altOn = false;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wParam == TIMER_SELFTEST) {
            KillTimer(hwnd, TIMER_SELFTEST);
            const UINT keys[] = { 'H', 'E', 'L', 'L', 'O' };
            for (UINT k : keys) {
                SendChord(k, false, false, false, false);
                Sleep(70);
            }
            Sleep(200);
            a->quitting = true;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_TRAY_INIT:
        if (!a->trayAdded) {
            AddTray(a);
            if (a->log) Log(L"tray=%d", (int)a->trayAdded);
        }
        return 0;

    case WM_TRAY:
        if (lParam == WM_LBUTTONUP) {
            if (a->visible) HideKeyboard(a);
            else ShowKeyboard(a);
        } else if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(a);
        }
        return 0;

    case WM_SHOW_KEYBOARD:
        if (!a->visible) ShowKeyboard(a);
        return 0;

    case WM_RAISE_KEYBOARD:
        ForceTopmost(a);
        return 0;

    case WM_CLOSE:
        if (a->quitting || !a->trayAdded) DestroyWindow(hwnd);
        else HideKeyboard(a);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_IME);
        KillTimer(hwnd, TIMER_REPEAT);
        KillTimer(hwnd, TIMER_AUTOCLOSE);
        KillTimer(hwnd, TIMER_ALT);
        if (a->altPhysDown) {
            SendKeyEvent(VK_MENU, false);
            a->altPhysDown = false;
        }
        RemoveTray(a);
        if (a->bgBmp) { DeleteObject(a->bgBmp); a->bgBmp = nullptr; }
        if (g_uiaStop) SetEvent(g_uiaStop);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // One instance per session: a second launch just brings the keyboard back.
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\DuoKeyboard_SingleInstance");
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(kClass, nullptr);
        if (prev) {
            PostMessageW(prev, WM_SHOW_KEYBOARD, 0, 0);
            PostMessageW(prev, WM_APP + 13, 0, 0);   // raise z-order
        }
        CloseHandle(instanceMutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    App app;
    g = &app;

    int argc = 0;
    bool acrylicExplicit = false;
    bool monitorExplicit = false;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring s = argv[i];
        if (s == L"--preview") {
            app.preview = true;
        } else if (s == L"--split") {
            app.screenMode = 1;
        } else if (s == L"--menu") {
            app.modeMenuOpen = true;   // show the mode menu at startup (preview aid)
        } else if (s == L"--mode" && i + 1 < argc) {
            std::wstring m = argv[++i];
            if (m == L"both") app.screenMode = 1;
            else if (m == L"left") app.screenMode = 2;
            else if (m == L"right") app.screenMode = 3;
            else app.screenMode = 0;
        } else if (s == L"--no-topmost") {
            app.topmost = false;
        } else if (s == L"--monitor" && i + 1 < argc) {
            app.monitorIndex = _wtoi(argv[++i]);
            monitorExplicit = true;
        } else if (s == L"--autoclose" && i + 1 < argc) {
            app.autocloseMs = _wtoi(argv[++i]);
        } else if (s == L"--selftest") {
            app.selftest = true;
            app.preview = true;
        } else if (s == L"--zoom" && i + 1 < argc) {
            int v = _wtoi(argv[++i]);
            if (v >= 50 && v <= 100) app.zoom = (float)v / 100.0f;
        } else if (s == L"--log") {
            app.log = true;
        } else if (s == L"--software") {
            app.software = true;
        } else if (s == L"--acrylic") {
            app.acrylic = true;
            acrylicExplicit = true;
        } else if (s == L"--no-acrylic") {
            app.acrylic = false;
            acrylicExplicit = true;
        } else if (s == L"--auto-show") {
            app.autoShow = true;
        } else if (s == L"--no-auto-show") {
            app.autoShow = false;
        } else if (s == L"--light") {
            app.themeOverride = 0;
        } else if (s == L"--dark") {
            app.themeOverride = 1;
        } else if (s == L"--list-monitors") {
            auto mons = EnumMons();
            std::wstring msg;
            for (size_t m = 0; m < mons.size(); ++m) {
                wchar_t line[512];
                swprintf_s(line, L"[%d] %s  %ldx%ld @ (%ld,%ld)%s\n", (int)m,
                           mons[m].dev.c_str(),
                           mons[m].rc.right - mons[m].rc.left,
                           mons[m].rc.bottom - mons[m].rc.top,
                           mons[m].rc.left, mons[m].rc.top,
                           mons[m].primary ? L"  [primary]" : L"");
                msg += line;
            }
            MessageBoxW(nullptr, msg.c_str(), L"Monitors", MB_OK);
            return 0;
        }
    }
    if (argv) LocalFree(argv);

    if (!acrylicExplicit) app.acrylic = LoadAcrylicPref();
    if (!monitorExplicit) {
        int m = LoadMonitorPref();
        if (m >= 0) app.monitorIndex = m;
    }
    app.R.software = app.software;
    app.R.acrylic = app.acrylic;
    if (!app.R.Init()) {
        MessageBoxW(nullptr, L"Direct2D/DirectWrite init failed", L"KB M0", MB_ICONERROR);
        return 1;
    }
    app.dark = (app.themeOverride >= 0) ? (app.themeOverride == 1) : IsDarkTheme();
    app.R.SetTheme(app.dark);
    if (app.log) Log(L"Theme dark=%d override=%d", (int)app.dark, app.themeOverride);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc)) return 1;

    RECT wr;
    if (app.preview) {
        UINT dpi = GetDpiForSystem();
        wr.left = 0;
        wr.top = 0;
        if (app.screenMode == 1) {
            // Split layout preview: 1350x405 DIP at 1:1 pixels (45% strip).
            wr.right = 1350;
            wr.bottom = 405;
            app.split = true;
            app.viewMode = 1;
        } else if (app.screenMode == 2 || app.screenMode == 3) {
            // Half layout preview: 675x405 DIP (one panel's keyboard strip).
            wr.right = 675;
            wr.bottom = 405;
            app.viewMode = app.screenMode;
        } else {
            wr.right = wr.left + MulDiv(900, (int)dpi, 96);
            wr.bottom = wr.top + MulDiv(675, (int)dpi, 96);
            app.viewMode = 0;
        }
        app.monitor = L"preview";
    } else {
        auto mons = EnumMons();
        if (mons.empty()) return 1;
        int idx = app.monitorIndex;
        if (idx < 0 || idx >= (int)mons.size()) idx = (mons.size() > 1) ? 1 : 0;
        app.monitorIndex = idx;
        int view = 0;
        wr = ComputeWindowRect(&app, &view);
        app.viewMode = view;
        app.split = (view == 1);
        app.monitor = mons[idx].dev;
    }

    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (app.topmost) exStyle |= WS_EX_TOPMOST;

    HWND hwnd = CreateWindowExW(exStyle, kClass, L"KB M1", WS_POPUP,
                                wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    app.usePointer = EnableMouseInPointer(TRUE) != FALSE;
    if (app.log) {
        Log(L"EnableMouseInPointer -> %d, hwnd=%p, dpi=%u, scale=%.2f",
            (int)app.usePointer, (void*)hwnd, GetDpiForWindow(hwnd), app.scale);
    }

    CaptureBackdrop(&app);        // window not shown yet: screen is still clean
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    if (app.preview) {
        SetWindowPos(hwnd, app.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        ApplyWindowLayout(&app);
    }
    UpdateWindow(hwnd);

    // Tray icon is added from the message loop (see WM_TRAY_INIT): the shell
    // sometimes drops NIM_ADD if the window is not pumping messages yet.
    PostMessageW(hwnd, WM_TRAY_INIT, 0, 0);

    // Watch focus changes so a text field can bring the keyboard back.
    // On by default; disable with --no-auto-show.
    if (app.autoShow) {
        g_uiaStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE th = CreateThread(nullptr, 0, UiaThreadProc, hwnd, 0, nullptr);
        if (th) CloseHandle(th);
        if (app.log) Log(L"UIA thread started");
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    app.R.Shutdown();
    CoUninitialize();
    return 0;
}
