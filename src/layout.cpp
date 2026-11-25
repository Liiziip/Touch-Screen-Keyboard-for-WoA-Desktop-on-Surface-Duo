#include "kb.h"
#include <algorithm>
#include <cwctype>

namespace {

struct KSpec {
    int row;
    int col;
    float units;
    int half;              // 0 = full height, 1 = top half, 2 = bottom half
    const wchar_t* label;
    const wchar_t* label2;
    UINT vk;
    KeyAct act;
    bool special;
};

const wchar_t* kQ[]  = { L"q", L"w", L"e", L"r", L"t", L"y", L"u", L"i", L"o", L"p" };
const wchar_t* kQ2[] = { L"Q", L"W", L"E", L"R", L"T", L"Y", L"U", L"I", L"O", L"P" };
const wchar_t* kA[]  = { L"a", L"s", L"d", L"f", L"g", L"h", L"j", L"k", L"l" };
const wchar_t* kA2[] = { L"A", L"S", L"D", L"F", L"G", L"H", L"J", L"K", L"L" };
const wchar_t* kZ[]  = { L"z", L"x", L"c", L"v", L"b", L"n", L"m" };
const wchar_t* kZ2[] = { L"Z", L"X", L"C", L"V", L"B", L"N", L"M" };
const wchar_t* kN[]  = { L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0" };
const wchar_t* kN2[] = { L"!", L"@", L"#", L"$", L"%", L"^", L"&", L"*", L"(", L")" };
const UINT kNVk[]    = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0' };

void Put(std::vector<KSpec>& v, int row, int col, float units, int half,
         const wchar_t* label, const wchar_t* label2, UINT vk, KeyAct act, bool special) {
    KSpec s;
    s.row = row; s.col = col; s.units = units; s.half = half;
    s.label = label; s.label2 = label2; s.vk = vk; s.act = act; s.special = special;
    v.push_back(s);
}

} // namespace

Layout BuildLayout(float screenW, float screenH, float zoom) {
    const float DW = 900.0f;
    const float DH = 675.0f;

    float fit = (std::min)(screenW / DW, screenH / DH);
    if (fit < 0.05f) fit = 0.05f;
    if (zoom <= 0.0f) zoom = 1.0f;
    float u = fit * zoom;
    if (u < 0.05f) u = 0.05f;

    Layout L;
    L.designW = DW;
    L.designH = DH;
    L.uiScale = u;
    L.ox = (screenW - DW * u) * 0.5f;
    L.oy = screenH - DH * u;

    float W = DW;
    float H = DH;
    L.gap = 5.0f;
    L.topBarH = 0.0f;
    L.cols = 14;

    const float pad = 8.0f;
    L.gridX = pad;
    L.gridW = W - pad * 2.0f;

    float cell = (L.gridW + L.gap) / (float)L.cols;
    L.keyW = cell - L.gap;

    float availH = H - L.topBarH - pad * 2.0f;
    float fillH = (availH - 4.0f * L.gap) / 5.0f;
    float keyH = (std::min)(fillH, L.keyW * 1.42f);
    keyH = (std::max)(keyH, L.keyW * 0.90f);
    L.keyH = keyH;

    L.gridH = 5.0f * keyH + 4.0f * L.gap;
    L.gridY = H - pad - L.gridH;
    L.statusTop = L.topBarH;
    L.statusH = L.gridY - L.topBarH;

    std::vector<KSpec> specs;

    // Row 0: Esc/Tab stacked, digits, - =, Del/Bksp stacked
    Put(specs, 0, 0, 1, 1, L"Esc", nullptr, VK_ESCAPE, KA_Esc, true);
    Put(specs, 0, 0, 1, 2, L"Tab", nullptr, VK_TAB, KA_Tab, true);
    for (int i = 0; i < 10; ++i)
        Put(specs, 0, i + 1, 1, 0, kN[i], kN2[i], kNVk[i], KA_Char, false);
    Put(specs, 0, 11, 1, 0, L"-", L"_", VK_OEM_MINUS, KA_Char, false);
    Put(specs, 0, 12, 1, 0, L"=", L"+", VK_OEM_PLUS, KA_Char, false);
    Put(specs, 0, 13, 1, 1, L"Del", nullptr, VK_DELETE, KA_Del, true);
    Put(specs, 0, 13, 1, 2, L"Bksp", nullptr, VK_BACK, KA_Back, true);

    // Row 1: q..p, brackets, backslash. Stretched to the full grid width so the
    // row protrudes under the Tab key, like the 10X reference.
    for (int i = 0; i < 10; ++i) {
        wchar_t up = (wchar_t)towupper(kQ[i][0]);
        Put(specs, 1, i, 1, 0, kQ[i], kQ2[i], (UINT)up, KA_Char, false);
    }
    Put(specs, 1, 10, 1, 0, L"[", L"{", VK_OEM_4, KA_Char, false);
    Put(specs, 1, 11, 1, 0, L"]", L"}", VK_OEM_6, KA_Char, false);
    Put(specs, 1, 12, 1, 0, L"\\", L"|", VK_OEM_5, KA_Char, false);

    // Row 2: a..l ; ' Enter (same full-width treatment)
    for (int i = 0; i < 9; ++i) {
        wchar_t up = (wchar_t)towupper(kA[i][0]);
        Put(specs, 2, i, 1, 0, kA[i], kA2[i], (UINT)up, KA_Char, false);
    }
    Put(specs, 2, 9, 1, 0, L";", L":", VK_OEM_1, KA_Char, false);
    Put(specs, 2, 10, 1, 0, L"'", L"\"", VK_OEM_7, KA_Char, false);
    Put(specs, 2, 11, 2, 0, L"Enter", nullptr, VK_RETURN, KA_Enter, true);

    // Row 3: Shift z..m , . / Up
    Put(specs, 3, 0, 1, 0, L"Shift", nullptr, VK_SHIFT, KA_Shift, true);
    for (int i = 0; i < 7; ++i) {
        wchar_t up = (wchar_t)towupper(kZ[i][0]);
        Put(specs, 3, 1 + i, 1, 0, kZ[i], kZ2[i], (UINT)up, KA_Char, false);
    }
    Put(specs, 3, 8, 1, 0, L",", L"<", VK_OEM_COMMA, KA_Char, false);
    Put(specs, 3, 9, 1, 0, L".", L">", VK_OEM_PERIOD, KA_Char, false);
    Put(specs, 3, 10, 1, 0, L"/", L"?", VK_OEM_2, KA_Char, false);
    Put(specs, 3, 11, 3, 0, L"\x2191", nullptr, VK_UP, KA_Up, true);

    // Row 4: Ctrl Fn Win Alt Space Left Down Right
    Put(specs, 4, 0, 1, 0, L"Ctrl", nullptr, VK_CONTROL, KA_Ctrl, true);
    Put(specs, 4, 1, 1, 0, L"Fn", nullptr, 0, KA_Fn, true);
    Put(specs, 4, 2, 1, 0, L"Win", nullptr, VK_LWIN, KA_Win, true);
    Put(specs, 4, 3, 1, 0, L"Alt", nullptr, VK_MENU, KA_Alt, true);
    Put(specs, 4, 4, 7, 0, L"", nullptr, VK_SPACE, KA_Space, false);
    Put(specs, 4, 11, 1, 0, L"\x2190", nullptr, VK_LEFT, KA_Left, true);
    Put(specs, 4, 12, 1, 0, L"\x2193", nullptr, VK_DOWN, KA_Down, true);
    Put(specs, 4, 13, 1, 0, L"\x2192", nullptr, VK_RIGHT, KA_Right, true);

    for (const KSpec& s : specs) {
        float rowTop = L.gridY + (float)s.row * (L.keyH + L.gap);
        float y = rowTop;
        float h = L.keyH;
        if (s.half == 1) {
            h = (L.keyH - L.gap) * 0.5f;
        } else if (s.half == 2) {
            h = (L.keyH - L.gap) * 0.5f;
            y = rowTop + L.keyH - h;
        }
        Key k;
        float rowCell = (s.row == 1 || s.row == 2) ? (L.gridW / 13.0f) : cell;
        k.x = L.gridX + (float)s.col * rowCell;
        k.y = y;
        k.w = s.units * rowCell - L.gap;
        k.h = h;
        k.label = s.label ? s.label : L"";
        k.label2 = s.label2 ? s.label2 : L"";
        k.vk = s.vk;
        k.act = s.act;
        k.special = s.special;
        L.keys.push_back(k);
    }

    // Portrait chrome + settings menu (display / zoom / touchpad / acrylic).
    L.menuPortrait = true;
    float toolRowY = L.gridY - 66.0f;
    if (toolRowY < 96.0f) toolRowY = 96.0f;
    L.gearBtn = RectF{ 16.0f, toolRowY, 60.0f, toolRowY + 44.0f };
    L.menuPanel = RectF{ 14.0f, 52.0f, 326.0f, 222.0f };
    for (int i = 0; i < 3; ++i) {
        L.dispBtn[i] = RectF{ 24.0f + i * 100.0f, 62.0f,
                              116.0f + i * 100.0f, 106.0f };
    }
    L.zoomBtn[0] = RectF{ 24.0f, 114.0f, 64.0f, 158.0f };
    L.zoomBtn[1] = RectF{ 70.0f, 114.0f, 134.0f, 158.0f };
    L.zoomBtn[2] = RectF{ 140.0f, 114.0f, 180.0f, 158.0f };
    L.acrylicBtn = RectF{ 24.0f, 166.0f, 316.0f, 210.0f };

    wchar_t pct[16];
    swprintf_s(pct, L"%d%%", (int)(zoom * 100.0f + 0.5f));
    L.zoomText = pct;

    return L;
}


// ---------------------------------------------------------------------------
// Split / half layouts
//
// Adaptive: built from the window's DIP size, so the keys fill the window.
// A "half" is five/six columns with a staircase towards the inner edge; the
// split layout puts two halves either side of an empty hinge strip.
// ---------------------------------------------------------------------------

static void AddChrome(Layout& L, float screenW, float chrome) {
    const float pad = 10.0f;
    const float iconW = 34.0f;
    const float iconH = chrome - 6.0f;
    L.gearBtn = RectF{ pad, 3.0f, pad + iconW, 3.0f + iconH };
    L.closeBtn2 = RectF{ screenW - pad - iconW, 3.0f, screenW - pad, 3.0f + iconH };

    const float mPadX = 10.0f, mPadY = 8.0f, mBtnW = 92.0f, mBtnH = 44.0f, mGap = 8.0f;
    float mx = pad;
    float my = 3.0f + iconH + 6.0f;
    float rowW = 3.0f * mBtnW + 2.0f * mGap;
    L.menuPanel = RectF{ mx, my,
                         mx + rowW + 2.0f * mPadX,
                         my + 2.0f * mBtnH + mGap + 2.0f * mPadY };
    for (int i = 0; i < 3; ++i) {
        float bx = mx + mPadX + (float)i * (mBtnW + mGap);
        L.modeBtn[i] = RectF{ bx, my + mPadY, bx + mBtnW, my + mPadY + mBtnH };
    }
    L.acrylicBtn = RectF{ mx + mPadX, my + mPadY + mBtnH + mGap,
                          mx + mPadX + rowW, my + mPadY + 2.0f * mBtnH + mGap };
}

static void AddHalfKeys(Layout& L, int side, float x0, float areaW, float top,
                        float rowH, bool numLayer) {
    // Rows narrow towards the top; the home row keeps the QWERTY indent.
    static const float kRowFit[4] = { 0.72f, 0.78f, 0.86f, 0.95f };
    static const float kRowShift[4] = { 0.0f, 0.35f, 0.0f, 0.0f };

    auto add = [&](int row, int col, float units, int cells,
                   const wchar_t* hint, const wchar_t* label, const wchar_t* label2,
                   UINT vk, KeyAct act, bool special) {
        float cw = areaW / (float)cells;
        float shift = kRowShift[row] * cw;
        float rw = areaW * kRowFit[row];
        float w0 = rw - shift;
        float rx = (side == 0) ? (x0 + shift) : (x0 + areaW - rw);
        float cellW = w0 / (float)cells;
        Key k;
        k.x = rx + (float)col * cellW;
        k.w = units * cellW - L.gap;
        k.y = top + (float)row * (rowH + L.gap);
        k.h = rowH;
        k.hint = hint ? hint : L"";
        k.label = label ? label : L"";
        k.label2 = label2 ? label2 : L"";
        k.vk = vk;
        k.act = act;
        k.special = special;
        L.keys.push_back(k);
    };

    if (!numLayer) {
        if (side == 0) {
            static const wchar_t* kHint[5] = { L"1", L"2", L"3", L"4", L"5" };
            for (int i = 0; i < 5; ++i)
                add(0, i, 1, 5, kHint[i], kQ[i], kQ2[i], (UINT)towupper(kQ[i][0]), KA_Char, false);
            for (int i = 0; i < 5; ++i)
                add(1, i, 1, 5, nullptr, kA[i], kA2[i], (UINT)towupper(kA[i][0]), KA_Char, false);
            add(2, 0, 1, 6, nullptr, L"Shift", nullptr, VK_SHIFT, KA_Shift, true);
            for (int i = 0; i < 5; ++i)
                add(2, 1 + i, 1, 6, nullptr, kZ[i], kZ2[i], (UINT)towupper(kZ[i][0]), KA_Char, false);
            add(3, 0, 1, 5, nullptr, L"&123", nullptr, 0, KA_Layer, true);
            add(3, 1, 1, 5, nullptr, L"Win", nullptr, VK_LWIN, KA_Win, true);
            add(3, 2, 1, 5, nullptr, L"Ctrl", nullptr, VK_CONTROL, KA_Ctrl, true);
            add(3, 3, 2, 5, nullptr, L"", nullptr, VK_SPACE, KA_Space, false);
        } else {
            static const wchar_t* kHint[5] = { L"6", L"7", L"8", L"9", L"0" };
            for (int i = 0; i < 5; ++i)
                add(0, i, 1, 5, kHint[i], kQ[i + 5], kQ2[i + 5], (UINT)towupper(kQ[i + 5][0]), KA_Char, false);
            for (int i = 0; i < 4; ++i)
                add(1, i, 1, 5, nullptr, kA[i + 5], kA2[i + 5], (UINT)towupper(kA[i + 5][0]), KA_Char, false);
            add(1, 4, 1, 5, nullptr, L"'", L"\"", VK_OEM_7, KA_Char, false);
            for (int i = 0; i < 2; ++i)
                add(2, i, 1, 6, nullptr, kZ[i + 5], kZ2[i + 5], (UINT)towupper(kZ[i + 5][0]), KA_Char, false);
            add(2, 2, 1, 6, nullptr, L",", L"<", VK_OEM_COMMA, KA_Char, false);
            add(2, 3, 1, 6, nullptr, L".", L">", VK_OEM_PERIOD, KA_Char, false);
            add(2, 4, 1, 6, nullptr, L"?", L"/", VK_OEM_2, KA_Char, false);
            add(2, 5, 1, 6, nullptr, L"Bksp", nullptr, VK_BACK, KA_Back, true);
            add(3, 0, 2, 5, nullptr, L"", nullptr, VK_SPACE, KA_Space, false);
            add(3, 2, 1, 5, nullptr, L"\x2190", nullptr, VK_LEFT, KA_Left, true);
            add(3, 3, 1, 5, nullptr, L"\x2192", nullptr, VK_RIGHT, KA_Right, true);
            add(3, 4, 1, 5, nullptr, L"Enter", nullptr, VK_RETURN, KA_Enter, true);
        }
    } else {
        if (side == 0) {
            for (int i = 0; i < 5; ++i)
                add(0, i, 1, 5, nullptr, kN[i], kN2[i], kNVk[i], KA_Char, false);
            add(1, 0, 1, 5, nullptr, L"(", L"9", '9', KA_Char, false);
            add(1, 1, 1, 5, nullptr, L")", L"0", '0', KA_Char, false);
            add(1, 2, 1, 5, nullptr, L"-", L"_", VK_OEM_MINUS, KA_Char, false);
            add(1, 3, 1, 5, nullptr, L"=", L"+", VK_OEM_PLUS, KA_Char, false);
            add(1, 4, 1, 5, nullptr, L"/", L"?", VK_OEM_2, KA_Char, false);
            add(2, 0, 1, 6, nullptr, L"Esc", nullptr, VK_ESCAPE, KA_Esc, true);
            add(2, 1, 1, 6, nullptr, L"Tab", nullptr, VK_TAB, KA_Tab, true);
            add(2, 2, 1, 6, nullptr, L"Alt", nullptr, VK_MENU, KA_Alt, true);
            add(2, 3, 1, 6, nullptr, L"Win", nullptr, VK_LWIN, KA_Win, true);
            add(2, 4, 1, 6, nullptr, L"Fn", nullptr, 0, KA_Fn, true);
            add(2, 5, 1, 6, nullptr, L"Caps", nullptr, VK_CAPITAL, KA_Caps, true);
            add(3, 0, 1, 5, nullptr, L"abc", nullptr, 0, KA_Layer, true);
            add(3, 1, 1, 5, nullptr, L"Win", nullptr, VK_LWIN, KA_Win, true);
            add(3, 2, 1, 5, nullptr, L"Ctrl", nullptr, VK_CONTROL, KA_Ctrl, true);
            add(3, 3, 2, 5, nullptr, L"", nullptr, VK_SPACE, KA_Space, false);
        } else {
            for (int i = 0; i < 5; ++i)
                add(0, i, 1, 5, nullptr, kN[i + 5], kN2[i + 5], kNVk[i + 5], KA_Char, false);
            add(1, 0, 1, 5, nullptr, L"[", L"{", VK_OEM_4, KA_Char, false);
            add(1, 1, 1, 5, nullptr, L"]", L"}", VK_OEM_6, KA_Char, false);
            add(1, 2, 1, 5, nullptr, L"\\", L"|", VK_OEM_5, KA_Char, false);
            add(1, 3, 1, 5, nullptr, L";", L":", VK_OEM_1, KA_Char, false);
            add(1, 4, 1, 5, nullptr, L"'", L"\"", VK_OEM_7, KA_Char, false);
            add(2, 0, 1, 6, nullptr, L",", L"<", VK_OEM_COMMA, KA_Char, false);
            add(2, 1, 1, 6, nullptr, L".", L">", VK_OEM_PERIOD, KA_Char, false);
            add(2, 2, 1, 6, nullptr, L"?", L"/", VK_OEM_2, KA_Char, false);
            add(2, 3, 1, 6, nullptr, L"!", L"1", '1', KA_Char, false);
            add(2, 4, 1, 6, nullptr, L"Del", nullptr, VK_DELETE, KA_Del, true);
            add(2, 5, 1, 6, nullptr, L"Bksp", nullptr, VK_BACK, KA_Back, true);
            add(3, 0, 2, 5, nullptr, L"", nullptr, VK_SPACE, KA_Space, false);
            add(3, 2, 1, 5, nullptr, L"\x2190", nullptr, VK_LEFT, KA_Left, true);
            add(3, 3, 1, 5, nullptr, L"\x2192", nullptr, VK_RIGHT, KA_Right, true);
            add(3, 4, 1, 5, nullptr, L"Enter", nullptr, VK_RETURN, KA_Enter, true);
        }
    }
}

static void SplitMetrics(float screenW, float screenH, float* chrome, float* gapW,
                         float* pad) {
    float c = screenH * 0.10f;
    if (c < 24.0f) c = 24.0f;
    if (c > 40.0f) c = 40.0f;
    *chrome = c;
    float g = screenW * 0.05f;
    if (g < 40.0f) g = 40.0f;
    if (g > 110.0f) g = 110.0f;
    *gapW = g;
    *pad = 10.0f;
}

Layout BuildSplitLayout(float screenW, float screenH, float zoom, bool numLayer) {
    (void)zoom;
    Layout L;
    L.split = true;
    L.uiScale = 1.0f;
    L.ox = 0.0f;
    L.oy = 0.0f;
    L.designW = screenW;
    L.designH = screenH;
    L.gap = 5.0f;
    L.cols = 10;

    float chrome = 0, gapW = 0, pad = 10.0f;
    SplitMetrics(screenW, screenH, &chrome, &gapW, &pad);
    L.gapW = gapW;

    const float halfW = (screenW - gapW) * 0.5f;
    const float areaW = halfW - pad * 2.0f;
    const float top = chrome + pad * 0.5f;
    const float gridH = screenH - top - pad;
    const float rowH = (gridH - 3.0f * L.gap) / 4.0f;

    L.gridX = pad;
    L.gridY = top;
    L.gridW = areaW;
    L.gridH = gridH;
    L.keyH = rowH;
    L.keyW = areaW / 6.0f - L.gap;
    L.statusTop = 0.0f;
    L.statusH = chrome;

    AddChrome(L, screenW, chrome);
    AddHalfKeys(L, 0, pad, areaW, top, rowH, numLayer);
    AddHalfKeys(L, 1, halfW + gapW + pad, areaW, top, rowH, numLayer);
    return L;
}

// Full keyboard on a single panel (left/right screen modes): ten columns, four
// rows, styled after the system touch keyboard.
static void AddFullKeys(Layout& L, float x0, float areaW, float top, float rowH,
                        bool numLayer) {
    static const float kShift[4] = { 0.0f, 0.5f, 0.0f, 0.0f };

    auto add = [&](int row, int col, float units, int cells,
                   const wchar_t* hint, const wchar_t* label, const wchar_t* label2,
                   UINT vk, KeyAct act, bool special) {
        float cw = areaW / (float)cells;
        float shift = kShift[row] * cw;
        float w0 = areaW - shift;
        float cellW = w0 / (float)cells;
        Key k;
        k.x = x0 + shift + (float)col * cellW;
        k.w = units * cellW - L.gap;
        k.y = top + (float)row * (rowH + L.gap);
        k.h = rowH;
        k.hint = hint ? hint : L"";
        k.label = label ? label : L"";
        k.label2 = label2 ? label2 : L"";
        k.vk = vk;
        k.act = act;
        k.special = special;
        L.keys.push_back(k);
    };

    auto arrowsAndEnter = [&](int row, int col) {
        add(row, col, 1, 9, nullptr, L"\x2190", nullptr, VK_LEFT, KA_Left, true);
        add(row, col + 1, 1, 9, nullptr, L"Enter", nullptr, VK_RETURN, KA_Enter, true);
    };

    if (!numLayer) {
        static const wchar_t* kHint[10] = { L"1", L"2", L"3", L"4", L"5",
                                            L"6", L"7", L"8", L"9", L"0" };
        for (int i = 0; i < 10; ++i)
            add(0, i, 1, 10, kHint[i], kQ[i], kQ2[i], (UINT)towupper(kQ[i][0]), KA_Char, false);
        for (int i = 0; i < 9; ++i)
            add(1, i, 1, 9, nullptr, kA[i], kA2[i], (UINT)towupper(kA[i][0]), KA_Char, false);
        add(2, 0, 1, 9, nullptr, L"Shift", nullptr, VK_SHIFT, KA_Shift, true);
        for (int i = 0; i < 7; ++i)
            add(2, 1 + i, 1, 9, nullptr, kZ[i], kZ2[i], (UINT)towupper(kZ[i][0]), KA_Char, false);
        add(2, 8, 1, 9, nullptr, L"Bksp", nullptr, VK_BACK, KA_Back, true);

        add(3, 0, 1, 11, nullptr, L"&123", nullptr, 0, KA_Layer, true);
        add(3, 1, 1, 11, nullptr, L"Win", nullptr, VK_LWIN, KA_Win, true);
        add(3, 2, 1, 11, nullptr, L"Alt", nullptr, VK_MENU, KA_Alt, true);
        add(3, 3, 1, 11, nullptr, L",", L"<", VK_OEM_COMMA, KA_Char, false);
        add(3, 4, 3, 11, nullptr, L"", nullptr, VK_SPACE, KA_Space, false);
        add(3, 7, 1, 11, nullptr, L".", L">", VK_OEM_PERIOD, KA_Char, false);
        add(3, 8, 1, 11, nullptr, L"\x2190", nullptr, VK_LEFT, KA_Left, true);
        add(3, 9, 1, 11, nullptr, L"\x2192", nullptr, VK_RIGHT, KA_Right, true);
        add(3, 10, 1, 11, nullptr, L"Enter", nullptr, VK_RETURN, KA_Enter, true);
    } else {
        for (int i = 0; i < 10; ++i)
            add(0, i, 1, 10, nullptr, kN[i], kN2[i], kNVk[i], KA_Char, false);
        add(1, 0, 1, 9, nullptr, L"[", L"{", VK_OEM_4, KA_Char, false);
        add(1, 1, 1, 9, nullptr, L"]", L"}", VK_OEM_6, KA_Char, false);
        add(1, 2, 1, 9, nullptr, L"\\", L"|", VK_OEM_5, KA_Char, false);
        add(1, 3, 1, 9, nullptr, L";", L":", VK_OEM_1, KA_Char, false);
        add(1, 4, 1, 9, nullptr, L"'", L"\"", VK_OEM_7, KA_Char, false);
        add(1, 5, 1, 9, nullptr, L"-", L"_", VK_OEM_MINUS, KA_Char, false);
        add(1, 6, 1, 9, nullptr, L"=", L"+", VK_OEM_PLUS, KA_Char, false);
        add(1, 7, 1, 9, nullptr, L"/", L"?", VK_OEM_2, KA_Char, false);
        add(1, 8, 1, 9, nullptr, L",", L"<", VK_OEM_COMMA, KA_Char, false);
        add(2, 0, 1, 9, nullptr, L"Esc", nullptr, VK_ESCAPE, KA_Esc, true);
        add(2, 1, 1, 9, nullptr, L"Tab", nullptr, VK_TAB, KA_Tab, true);
        add(2, 2, 1, 9, nullptr, L"Ctrl", nullptr, VK_CONTROL, KA_Ctrl, true);
        add(2, 3, 1, 9, nullptr, L"Alt", nullptr, VK_MENU, KA_Alt, true);
        add(2, 4, 1, 9, nullptr, L"Win", nullptr, VK_LWIN, KA_Win, true);
        add(2, 5, 1, 9, nullptr, L"Fn", nullptr, 0, KA_Fn, true);
        add(2, 6, 1, 9, nullptr, L"Caps", nullptr, VK_CAPITAL, KA_Caps, true);
        add(2, 7, 1, 9, nullptr, L"Del", nullptr, VK_DELETE, KA_Del, true);
        add(2, 8, 1, 9, nullptr, L"Bksp", nullptr, VK_BACK, KA_Back, true);

        add(3, 0, 1, 11, nullptr, L"abc", nullptr, 0, KA_Layer, true);
        add(3, 1, 1, 11, nullptr, L"Win", nullptr, VK_LWIN, KA_Win, true);
        add(3, 2, 1, 11, nullptr, L"Alt", nullptr, VK_MENU, KA_Alt, true);
        add(3, 3, 1, 11, nullptr, L",", L"<", VK_OEM_COMMA, KA_Char, false);
        add(3, 4, 3, 11, nullptr, L"", nullptr, VK_SPACE, KA_Space, false);
        add(3, 7, 1, 11, nullptr, L".", L">", VK_OEM_PERIOD, KA_Char, false);
        add(3, 8, 1, 11, nullptr, L"\x2190", nullptr, VK_LEFT, KA_Left, true);
        add(3, 9, 1, 11, nullptr, L"\x2192", nullptr, VK_RIGHT, KA_Right, true);
        add(3, 10, 1, 11, nullptr, L"Enter", nullptr, VK_RETURN, KA_Enter, true);
    }
}

Layout BuildPanelLayout(float screenW, float screenH, float zoom, bool numLayer) {
    (void)zoom;
    Layout L;
    L.split = false;
    L.half = true;
    L.uiScale = 1.0f;
    L.ox = 0.0f;
    L.oy = 0.0f;
    L.designW = screenW;
    L.designH = screenH;
    L.gap = 5.0f;
    L.cols = 10;

    float chrome = 0, gapW = 0, pad = 10.0f;
    SplitMetrics(screenW, screenH, &chrome, &gapW, &pad);
    L.gapW = 0.0f;

    const float areaW = screenW - pad * 2.0f;
    const float top = chrome + pad * 0.5f;
    const float gridH = screenH - top - pad;
    const float rowH = (gridH - 3.0f * L.gap) / 4.0f;

    L.gridX = pad;
    L.gridY = top;
    L.gridW = areaW;
    L.gridH = gridH;
    L.keyH = rowH;
    L.keyW = areaW / 10.0f - L.gap;
    L.statusTop = 0.0f;
    L.statusH = chrome;

    AddChrome(L, screenW, chrome);
    AddFullKeys(L, pad, areaW, top, rowH, numLayer);
    return L;
}

int HitTest(const Layout& L, float x, float y) {
    for (size_t i = 0; i < L.keys.size(); ++i) {
        const Key& k = L.keys[i];
        if (x >= k.x && x <= k.x + k.w && y >= k.y && y <= k.y + k.h) return (int)i;
    }
    return -1;
}