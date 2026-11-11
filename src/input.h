#pragma once
#include <windows.h>
#include <string>

// Send a full chord: modifiers down, key down/up, modifiers up.
void SendChord(UINT vk, bool shift, bool ctrl, bool alt, bool win);

// Send a bare key down or up, without pairing it (used for held Alt).
void SendKeyEvent(UINT vk, bool down);

// 1 = native (Chinese) mode, 0 = alphanumeric (English), -1 = unknown.
int ReadImeState(HWND target);

std::wstring GetTargetTitle(HWND target);
