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

    return L;
}

int HitTest(const Layout& L, float x, float y) {
    for (size_t i = 0; i < L.keys.size(); ++i) {
        const Key& k = L.keys[i];
        if (x >= k.x && x <= k.x + k.w && y >= k.y && y <= k.y + k.h) return (int)i;
    }
    return -1;
}
