#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <uiautomation.h>
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

static const wchar_t* kClass = L"Win10XKeyboardM0";
static const UINT TIMER_IME = 1;
static const UINT TIMER_REPEAT = 2;
static const UINT TIMER_AUTOCLOSE = 3;
static const UINT TIMER_SELFTEST = 4;
static const UINT TIMER_ALT = 5;

static const UINT WM_TRAY = WM_APP + 10;
static const UINT WM_SHOW_KEYBOARD = WM_APP + 11;
static const UINT WM_TRAY_INIT = WM_APP + 12;

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
    RECT fullRect = { 0, 0, 0, 0 };

    std::vector<MonInfo> mons;
    std::vector<MonButton> monBtns;
    std::vector<MonButton> zoomBtns;
    float zoom = 1.0f;
    bool dark = true;
    int  themeOverride = -1;   // -1 = follow system, 0 = light, 1 = dark

    bool visible = true;
    bool quitting = false;
    bool software = false;      // --software: force the WARP/software D2D target
    bool autoShow = true;       // pop up when a text field gains focus (default on)
    bool acrylic = true;        // blurred screenshot backdrop (--no-acrylic to disable)
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
    a->monBtns.clear();
    float y = a->L.gridY - 66.0f;
    if (y < 96.0f) y = 96.0f;
    const float w = 150.0f, h = 44.0f, gap = 8.0f;
    float x = 78.0f;
    for (size_t i = 0; i < a->mons.size(); ++i) {
        MonButton b;
        b.x = x;
        b.y = y;
        b.w = w;
        b.h = h;
        wchar_t buf[64];
        swprintf_s(buf, L"Display %d%s", (int)i + 1,
                   a->mons[i].primary ? L" \u00B7Main" : L"");
        b.label = buf;
        a->monBtns.push_back(b);
        x += w + gap;
    }

    // Zoom controls sit right after the monitor group.
    a->zoomBtns.clear();
    float zx = x + 16.0f;
    wchar_t pct[16];
    swprintf_s(pct, L"%d%%", (int)(a->zoom * 100.0f + 0.5f));

    MonButton minus;
    minus.x = zx; minus.y = y; minus.w = 40.0f; minus.h = h; minus.label = L"-";
    MonButton label;
    label.x = zx + 46.0f; label.y = y; label.w = 64.0f; label.h = h; label.label = pct;
    MonButton plus;
    plus.x = zx + 116.0f; plus.y = y; plus.w = 40.0f; plus.h = h; plus.label = L"+";

    a->zoomBtns.push_back(minus);
    a->zoomBtns.push_back(label);
    a->zoomBtns.push_back(plus);
}

static void Relayout(App* a) {
    RECT rc;
    GetClientRect(a->hwnd, &rc);
    a->widthDip = (float)(rc.right - rc.left) / a->scale;
    a->heightDip = (float)(rc.bottom - rc.top) / a->scale;
    if (a->widthDip < 100.0f || a->heightDip < 100.0f) return;
    a->L = BuildLayout(a->widthDip, a->heightDip, a->zoom);
    a->R.SetSizes(a->L.keyH);
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

// Simulated acrylic: grab what is on the target screen before the keyboard is
// shown, blur it, and use it as a static backdrop.
static void CaptureBackdrop(App* a) {
    if (!a->acrylic) return;
    RECT r = { 0, 0, 0, 0 };
    if (a->monitorIndex >= 0 && a->monitorIndex < (int)a->mons.size())
        r = a->mons[a->monitorIndex].rc;
    else if (a->hwnd)
        GetWindowRect(a->hwnd, &r);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;

    HDC screen = GetDC(nullptr);
    if (!screen) return;
    HDC full = CreateCompatibleDC(screen);
    HBITMAP fullBmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ oldFull = SelectObject(full, fullBmp);
    BitBlt(full, 0, 0, w, h, screen, r.left, r.top, SRCCOPY);

    int sw = w / 10, sh = h / 10;
    if (sw < 16) sw = 16;
    if (sh < 16) sh = 16;
    HDC blurDC = CreateCompatibleDC(screen);
    HBITMAP smallBmp = CreateCompatibleBitmap(screen, sw, sh);
    HGDIOBJ oldSmall = SelectObject(blurDC, smallBmp);
    SetStretchBltMode(blurDC, HALFTONE);
    SetBrushOrgEx(blurDC, 0, 0, nullptr);
    StretchBlt(blurDC, 0, 0, sw, sh, full, 0, 0, w, h, SRCCOPY);
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

static void ApplyFullscreen(App* a) {
    if (a->preview) return;
    auto mons = EnumMons();
    if (mons.empty()) return;
    int idx = a->monitorIndex;
    if (idx < 0 || idx >= (int)mons.size()) idx = 0;
    RECT r = mons[idx].rc;
    a->fullRect = r;
    a->monitor = mons[idx].dev;
    SetWindowPos(a->hwnd, a->topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ForceTopmost(a);
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
    if (a->log) Log(L"SelectMonitor idx=%d", idx);
    if (a->preview) {
        RECT r = a->mons[idx].rc;
        int w = (int)(900.0f * a->scale);
        int h = (int)(675.0f * a->scale);
        SetWindowPos(a->hwnd, a->topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     r.left + 40, r.top + 40, w, h, SWP_NOACTIVATE);
    } else {
        CaptureBackdrop(a);   // the new screen is still uncovered
        ApplyFullscreen(a);
        Relayout(a);
    }
    InvalidateRect(a->hwnd, nullptr, FALSE);
}

static void ResizePreview(App* a) {
    RECT wr;
    GetWindowRect(a->hwnd, &wr);
    int w = (int)(900.0f * a->scale);
    int h = (int)(675.0f * a->scale);
    SetWindowPos(a->hwnd, nullptr, wr.left, wr.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ForceTopmost(App* a) {
    if (!a->topmost) return;
    SetWindowPos(a->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static bool IsTaskbarForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    wchar_t cls[64];
    ZeroMemory(cls, sizeof(cls));
    if (!GetClassNameW(fg, cls, 63)) return false;
    return lstrcmpW(cls, L"Shell_TrayWnd") == 0 ||
           lstrcmpW(cls, L"Shell_SecondaryTrayWnd") == 0;
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

    // The taskbar is topmost and, once focused, the shell keeps it above us.
    // Re-assert our z-order while it owns the foreground so we still cover it.
    if (a->visible && IsTaskbarForeground()) ForceTopmost(a);

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
    ApplyFullscreen(a);
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
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, a->hwnd, nullptr);
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
        RefreshMonitors(a);
        ApplyFullscreen(a);
        Relayout(a);
        InvalidateRect(hwnd, nullptr, FALSE);
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
                      a->monBtns, a->monitorIndex, a->zoomBtns, ps.hdc);
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
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    App app;
    g = &app;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring s = argv[i];
        if (s == L"--preview") {
            app.preview = true;
        } else if (s == L"--no-topmost") {
            app.topmost = false;
        } else if (s == L"--monitor" && i + 1 < argc) {
            app.monitorIndex = _wtoi(argv[++i]);
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
        } else if (s == L"--no-acrylic") {
            app.acrylic = false;
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
        wr.right = wr.left + MulDiv(900, (int)dpi, 96);
        wr.bottom = wr.top + MulDiv(675, (int)dpi, 96);
        app.monitor = L"preview";
    } else {
        auto mons = EnumMons();
        if (mons.empty()) return 1;
        int idx = app.monitorIndex;
        if (idx < 0 || idx >= (int)mons.size()) idx = (mons.size() > 1) ? 1 : 0;
        app.monitorIndex = idx;
        wr = mons[idx].rc;
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
        ApplyFullscreen(&app);
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
