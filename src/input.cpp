#include "input.h"
#include <imm.h>
#include <vector>

#pragma comment(lib, "imm32.lib")

// Not exposed by the public SDK header but stable since Windows 2000.
#ifndef IMC_GETCONVERSIONMODE
#define IMC_GETCONVERSIONMODE 0x0005
#endif

namespace {

void AddKey(std::vector<INPUT>& v, WORD vk, bool up) {
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = 0;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    v.push_back(in);
}

} // namespace

void SendChord(UINT vk, bool shift, bool ctrl, bool alt, bool win) {
    if (vk == 0) return;
    std::vector<INPUT> v;
    v.reserve(10);

    if (ctrl) AddKey(v, VK_CONTROL, false);
    if (alt)  AddKey(v, VK_MENU, false);
    if (shift) AddKey(v, VK_SHIFT, false);
    if (win)  AddKey(v, VK_LWIN, false);

    AddKey(v, (WORD)vk, false);
    AddKey(v, (WORD)vk, true);

    if (win)  AddKey(v, VK_LWIN, true);
    if (shift) AddKey(v, VK_SHIFT, true);
    if (alt)  AddKey(v, VK_MENU, true);
    if (ctrl) AddKey(v, VK_CONTROL, true);

    SendInput((UINT)v.size(), v.data(), sizeof(INPUT));
}

void SendKeyEvent(UINT vk, bool down) {
    if (vk == 0) return;
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = (WORD)vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

int ReadImeState(HWND target) {
    if (!target || !IsWindow(target)) return -1;

    DWORD tid = GetWindowThreadProcessId(target, nullptr);
    HWND focus = target;

    GUITHREADINFO gti;
    ZeroMemory(&gti, sizeof(gti));
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) focus = gti.hwndFocus;

    // Same-process path (rarely works cross-thread, kept as fast path).
    HIMC himc = ImmGetContext(focus);
    if (himc) {
        DWORD conv = 0, sent = 0;
        BOOL ok = ImmGetConversionStatus(himc, &conv, &sent);
        ImmReleaseContext(focus, himc);
        if (ok) return (conv & IME_CMODE_NATIVE) ? 1 : 0;
    }

    // Cross-process path: ask the target's IME window for its conversion mode.
    HWND ime = ImmGetDefaultIMEWnd(focus);
    if (ime) {
        LRESULT m = SendMessageW(ime, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0);
        return ((DWORD)m & IME_CMODE_NATIVE) ? 1 : 0;
    }
    return -1;
}

std::wstring GetTargetTitle(HWND target) {
    if (!target || !IsWindow(target)) return L"\u2014";
    wchar_t buf[256];
    ZeroMemory(buf, sizeof(buf));
    GetWindowTextW(target, buf, 255);
    std::wstring s = buf;
    if (s.empty()) {
        wchar_t cls[128];
        ZeroMemory(cls, sizeof(cls));
        GetClassNameW(target, cls, 127);
        s = cls;
        if (s.empty()) s = L"\u2014";
    }
    if (s.size() > 42) s = s.substr(0, 41) + L"\u2026";
    return s;
}
