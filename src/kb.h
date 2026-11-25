#pragma once
#include <windows.h>
#include <string>
#include <vector>

enum KeyAct {
    KA_Char = 0,
    KA_Esc, KA_Tab, KA_Caps, KA_Shift, KA_Ctrl, KA_Alt, KA_Win, KA_Fn,
    KA_Back, KA_Del, KA_Enter, KA_Space, KA_Home, KA_End,
    KA_Left, KA_Up, KA_Down, KA_Right, KA_Close,
    KA_Ime,          // placeholder: shows the IME key, does nothing yet
    KA_Layer         // switch between the letter and number layers (split mode)
};

struct Key {
    float x = 0, y = 0, w = 0, h = 0;   // DIP, absolute
    std::wstring label;
    std::wstring label2;                // shifted label
    std::wstring hint;                  // small corner label (digits in split mode)
    UINT vk = 0;
    KeyAct act = KA_Char;
    bool special = false;               // darker key face
};

struct RectF {
    float l = 0, t = 0, r = 0, b = 0;
    bool Contains(float x, float y) const { return x >= l && x <= r && y >= t && y <= b; }
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

    // Landscape split mode: one canvas spanning both panels, an empty hinge
    // strip in the middle, and the two chrome buttons.
    bool   split = false;
    bool   half = false;             // single half filling one panel
    float  gapW = 0.0f;
    RectF  gearBtn;
    RectF  closeBtn2;

    // In-app mode menu (gear popup): span / left / right.
    bool   showModeMenu = false;
    int    menuSel = 0;              // 1 = span, 2 = left, 3 = right
    bool   acrylicOn = false;        // for the acrylic toggle label
    bool   menuPortrait = false;     // portrait menu: display / zoom / touchpad
    int    dispCount = 0;
    int    dispSel = 0;
    std::wstring dispLabel[4];
    std::wstring zoomText;
    RectF  menuPanel;
    RectF  modeBtn[3];
    RectF  dispBtn[4];
    RectF  zoomBtn[3];
    RectF  touchBtn;
    RectF  acrylicBtn;
};

struct MonButton {
    float x = 0, y = 0, w = 0, h = 0;
    std::wstring label;
};

Layout BuildLayout(float screenW, float screenH, float zoom);
Layout BuildSplitLayout(float screenW, float screenH, float zoom, bool numLayer);
Layout BuildPanelLayout(float screenW, float screenH, float zoom, bool numLayer);
int    HitTest(const Layout& L, float x, float y);
