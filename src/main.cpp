#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
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
#pragma comment(lib, "advapi32.lib")

static const wchar_t* kClass = L"Win10XKeyboardM0";
static const UINT TIMER_IME = 1;
static const UINT TIMER_REPEAT = 2;
static const UINT TIMER_AUTOCLOSE = 3;
static const UINT TIMER_SELFTEST = 4;
static const UINT TIMER_ALT = 5;

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
    std::wstring target = L"\u2014";
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
};

static App* g = nullptr;

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
        swprintf_s(buf, L"\u5C4F\u5E55 %d%s", (int)i + 1,
                   a->mons[i].primary ? L" \u00B7\u4E3B" : L"");
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

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* a = g;
    if (!a) return DefWindowProcW(hwnd, msg, wParam, lParam);

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
        if (a->R.EnsureTarget(hwnd, rc.right - rc.left, rc.bottom - rc.top)) {
            a->R.Draw(a->L, a->scale, a->widthDip, a->pressed, a->ime, a->target,
                      a->monitor, a->shiftOn, a->capsOn, a->ctrlOn, a->altOn, a->winOn, a->fnOn,
                      a->monBtns, a->monitorIndex, a->zoomBtns);
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
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
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

    HWND hwnd = CreateWindowExW(exStyle, kClass, L"KB M0", WS_POPUP,
                                wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    app.usePointer = EnableMouseInPointer(TRUE) != FALSE;
    if (app.log) {
        Log(L"EnableMouseInPointer -> %d, hwnd=%p, dpi=%u, scale=%.2f",
            (int)app.usePointer, (void*)hwnd, GetDpiForWindow(hwnd), app.scale);
    }

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    if (app.preview) {
        SetWindowPos(hwnd, app.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        ApplyFullscreen(&app);
    }
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    app.R.Shutdown();
    CoUninitialize();
    return 0;
}
