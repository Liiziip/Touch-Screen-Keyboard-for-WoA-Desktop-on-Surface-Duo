#pragma once
#include <windows.h>
#include <string>
#include <vector>

enum KeyAct {
    KA_Char = 0,
    KA_Esc, KA_Tab, KA_Caps, KA_Shift, KA_Ctrl, KA_Alt, KA_Win, KA_Fn,
    KA_Back, KA_Del, KA_Enter, KA_Space, KA_Home, KA_End,
    KA_Left, KA_Up, KA_Down, KA_Right, KA_Close
};

struct Key {
    float x = 0, y = 0, w = 0, h = 0;   // DIP, absolute
    std::wstring label;
    std::wstring label2;                // shifted label
    UINT vk = 0;
    KeyAct act = KA_Char;
    bool special = false;               // darker key face
};

struct Layout {
    std::vector<Key> keys;
    float gridX = 0, gridY = 0, gridW = 0, gridH = 0;
    float keyW = 0, keyH = 0, gap = 5.0f;
    float topBarH = 40.0f;
    float statusTop = 40.0f, statusH = 0.0f;
    int   cols = 14;

    // Canvas mapping: the layout is built on a fixed 900x675 design grid and
    // then scaled/positioned onto the real screen.
    float designW = 900.0f, designH = 675.0f;
    float uiScale = 1.0f;
    float ox = 0.0f, oy = 0.0f;
};

struct MonButton {
    float x = 0, y = 0, w = 0, h = 0;
    std::wstring label;
};

Layout BuildLayout(float screenW, float screenH, float zoom);
int    HitTest(const Layout& L, float x, float y);
