#include "render.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace {
D2D1_RECT_F Rect(float l, float t, float r, float b) {
    D2D1_RECT_F x;
    x.left = l; x.top = t; x.right = r; x.bottom = b;
    return x;
}
} // namespace

void Renderer::BuildPalette() {
    if (darkTheme) {
        pal.bg       = D2D1::ColorF(0x1F1F1F);
        pal.key      = D2D1::ColorF(0x333333);
        pal.special  = D2D1::ColorF(0x2B2B2B);
        pal.pressed  = D2D1::ColorF(0x4D4D4D);
        pal.text     = D2D1::ColorF(0xF2F2F2);
        pal.dim      = D2D1::ColorF(0x9E9E9E);
        pal.accent   = D2D1::ColorF(0x0078D4);
        pal.onAccent = D2D1::ColorF(0xFFFFFF);
    } else {
        pal.bg       = D2D1::ColorF(0xE6E6E6);
        pal.key      = D2D1::ColorF(0xFFFFFF);
        pal.special  = D2D1::ColorF(0xD8D8D8);
        pal.pressed  = D2D1::ColorF(0xC2C2C2);
        pal.text     = D2D1::ColorF(0x1A1A1A);
        pal.dim      = D2D1::ColorF(0x5A5A5A);
        pal.accent   = D2D1::ColorF(0x0067C0);
        pal.onAccent = D2D1::ColorF(0xFFFFFF);
    }
}

void Renderer::SetTheme(bool dark) {
    darkTheme = dark;
    BuildPalette();
    if (rt) {
        ReleaseBrushes();
        MakeBrushes();
    }
}

void Renderer::ReleaseBrushes() {
    if (bKey)      { bKey->Release();      bKey = nullptr; }
    if (bSpecial)  { bSpecial->Release();  bSpecial = nullptr; }
    if (bPressed)  { bPressed->Release();  bPressed = nullptr; }
    if (bText)     { bText->Release();     bText = nullptr; }
    if (bDim)      { bDim->Release();      bDim = nullptr; }
    if (bAccent)   { bAccent->Release();   bAccent = nullptr; }
    if (bOnAccent) { bOnAccent->Release(); bOnAccent = nullptr; }
}

bool Renderer::MakeBrushes() {
    if (!rt) return false;
    if (FAILED(rt->CreateSolidColorBrush(pal.key, &bKey))) return false;
    if (FAILED(rt->CreateSolidColorBrush(pal.special, &bSpecial))) return false;
    if (FAILED(rt->CreateSolidColorBrush(pal.pressed, &bPressed))) return false;
    if (FAILED(rt->CreateSolidColorBrush(pal.text, &bText))) return false;
    if (FAILED(rt->CreateSolidColorBrush(pal.dim, &bDim))) return false;
    if (FAILED(rt->CreateSolidColorBrush(pal.accent, &bAccent))) return false;
    if (FAILED(rt->CreateSolidColorBrush(pal.onAccent, &bOnAccent))) return false;
    return true;
}

bool Renderer::Init() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&dw)))) {
        return false;
    }
    return true;
}

bool Renderer::EnsureTarget(HWND hwnd, int pxW, int pxH) {
    if (pxW <= 0 || pxH <= 0) return false;
    if (!rt) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        props.dpiX = 96.0f;
        props.dpiY = 96.0f;
        D2D1_HWND_RENDER_TARGET_PROPERTIES hp =
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU((UINT32)pxW, (UINT32)pxH));
        if (FAILED(d2d->CreateHwndRenderTarget(props, hp, &rt))) return false;
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (!MakeBrushes()) return false;
    } else {
        D2D1_SIZE_U cur = rt->GetPixelSize();
        if ((int)cur.width != pxW || (int)cur.height != pxH) {
            if (FAILED(rt->Resize(D2D1::SizeU((UINT32)pxW, (UINT32)pxH)))) return false;
        }
    }
    return true;
}

void Renderer::SetSizes(float keyH) {
    if (!dw) return;
    struct Slot { IDWriteTextFormat** p; float size; bool left; };
    Slot slots[] = {
        { &fKey,    keyH * 0.30f, false },
        { &fKey2,   keyH * 0.20f, false },
        { &fSmall,  14.0f, false },
        { &fSmallL, 14.0f, true  },
        { &fMid,    18.0f, false },
        { &fMidL,   18.0f, true  },
        { &fBig,    34.0f, false },
    };
    for (auto& s : slots) {
        if (*s.p) { (*s.p)->Release(); *s.p = nullptr; }
        if (SUCCEEDED(dw->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, s.size, L"zh-CN", s.p))) {
            (*s.p)->SetTextAlignment(s.left ? DWRITE_TEXT_ALIGNMENT_LEADING
                                            : DWRITE_TEXT_ALIGNMENT_CENTER);
            (*s.p)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            (*s.p)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
}

void Renderer::Shutdown() {
    ReleaseBrushes();
    if (rt) { rt->Release(); rt = nullptr; }
    if (fKey)  { fKey->Release();  fKey = nullptr; }
    if (fKey2) { fKey2->Release(); fKey2 = nullptr; }
    if (fSmall){ fSmall->Release();fSmall = nullptr; }
    if (fSmallL){fSmallL->Release();fSmallL = nullptr; }
    if (fMid)  { fMid->Release();  fMid = nullptr; }
    if (fMidL) { fMidL->Release(); fMidL = nullptr; }
    if (fBig)  { fBig->Release();  fBig = nullptr; }
    if (dw) { dw->Release(); dw = nullptr; }
    if (d2d){ d2d->Release(); d2d = nullptr; }
}

void Renderer::Draw(const Layout& L, float scale, float widthDip, int pressed,
                    int ime, const std::wstring& target, const std::wstring& monitor,
                    bool shiftOn, bool capsOn, bool ctrlOn, bool altOn, bool winOn, bool fnOn,
                    const std::vector<MonButton>& mons, int curMon,
                    const std::vector<MonButton>& zoomBtns) {
    if (!rt) return;

    rt->BeginDraw();
    D2D1::Matrix3x2F xf = D2D1::Matrix3x2F::Scale(L.uiScale, L.uiScale) *
                         D2D1::Matrix3x2F::Translation(L.ox, L.oy) *
                         D2D1::Matrix3x2F::Scale(scale, scale);
    rt->SetTransform(xf);
    rt->Clear(pal.bg);
    (void)widthDip;

    auto text = [&](const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F r, ID2D1SolidColorBrush* b) {
        if (!s.empty() && f) rt->DrawTextW(s.c_str(), (UINT32)s.size(), f, r, b,
                                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    // ---- floating close button ----
    D2D1_RECT_F closeR = Rect(L.designW - 44, 8, L.designW - 12, 40);
    rt->FillRoundedRectangle(D2D1::RoundedRect(closeR, 6, 6), bSpecial);
    text(L"\x00D7", fMid, closeR, bText);
    (void)monitor;

    // ---- status area ----
    float sy = 16.0f;
    if (L.statusH > 90.0f) {
        D2D1_RECT_F pill = Rect(16, sy, 136, sy + 56);
        ID2D1SolidColorBrush* pf = (ime == 1) ? bAccent : bSpecial;
        rt->FillRoundedRectangle(D2D1::RoundedRect(pill, 12, 12), pf);
        std::wstring imeText = (ime == 1) ? L"\u4E2D" : (ime == 0 ? L"EN" : L"\u2014");
        text(imeText, fBig, pill, (ime == 1) ? bOnAccent : bText);

        std::wstring imeLine = (ime == 1) ? L"\u4E2D\u6587\u8F93\u5165\u6CD5"
                              : (ime == 0 ? L"\u82F1\u6587\u8F93\u5165" : L"\u672A\u68C0\u6D4B\u5230\u8F93\u5165\u6CD5");
        text(imeLine, fMidL, Rect(152, sy + 2, L.designW - 16, sy + 32), bText);
        text(L"\u70B9\u6309\u8F93\u5165\u6846\u805A\u7126\u540E\u5373\u53EF\u6253\u5B57",
             fSmallL, Rect(152, sy + 30, L.designW - 16, sy + 54), bDim);

        float ty = sy + 72.0f;
        text(L"\u76EE\u6807\uFF1A" + target, fSmallL, Rect(16, ty, L.designW - 16, ty + 22), bDim);

        if (L.statusH > 170.0f) {
            text(L"\u70B9 Shift \u5207\u4E2D\u82F1\uFF0C\u957F\u6309 Shift/Win \u9501\u5B9A",
                 fSmallL, Rect(16, ty + 26, L.designW - 16, ty + 48), bDim);
        }
    }

    // ---- monitor selector + zoom ----
    if (!mons.empty()) {
        float left = 16.0f;
        float top = mons[0].y - 10.0f;
        float right = mons.back().x + mons.back().w + 10.0f;
        if (!zoomBtns.empty()) {
            float zr = zoomBtns.back().x + zoomBtns.back().w + 10.0f;
            if (zr > right) right = zr;
        }
        float bottom = mons[0].y + mons[0].h + 10.0f;
        rt->FillRoundedRectangle(D2D1::RoundedRect(Rect(left, top, right, bottom), 10, 10), bSpecial);
        text(L"\u5C4F\u5E55", fSmallL, Rect(left + 10.0f, top, left + 62.0f, bottom), bDim);

        for (size_t i = 0; i < mons.size(); ++i) {
            const MonButton& m = mons[i];
            D2D1_RECT_F r = Rect(m.x, m.y, m.x + m.w, m.y + m.h);
            bool sel = ((int)i == curMon);
            rt->FillRoundedRectangle(D2D1::RoundedRect(r, 8, 8), sel ? bAccent : bKey);
            text(m.label, fSmall, r, sel ? bOnAccent : bText);
        }
        for (const MonButton& z : zoomBtns) {
            D2D1_RECT_F r = Rect(z.x, z.y, z.x + z.w, z.y + z.h);
            rt->FillRoundedRectangle(D2D1::RoundedRect(r, 8, 8), bKey);
            text(z.label, fSmall, r, bText);
        }
    }

    // ---- keys ----
    for (size_t i = 0; i < L.keys.size(); ++i) {
        const Key& k = L.keys[i];
        bool latched = (k.act == KA_Shift && (shiftOn || capsOn)) || (k.act == KA_Caps && capsOn) ||
                       (k.act == KA_Ctrl && ctrlOn) || (k.act == KA_Alt && altOn) ||
                       (k.act == KA_Win && winOn) || (k.act == KA_Fn && fnOn);
        ID2D1SolidColorBrush* fill = (int)i == pressed ? bPressed
                                   : latched ? bAccent
                                   : (k.special ? bSpecial : bKey);
        D2D1_RECT_F r = Rect(k.x, k.y, k.x + k.w, k.y + k.h);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 8, 8), fill);
        ID2D1SolidColorBrush* labelBrush = latched ? bOnAccent : bText;

        std::wstring mainLabel = k.label;
        std::wstring sub = k.label2;
        bool letter = k.label.size() == 1 && iswalpha(k.label[0]) != 0;
        bool shifted = shiftOn || (capsOn && letter);
        if (!sub.empty()) {
            if (shifted) { mainLabel = sub; sub.clear(); }
            else if (letter) { sub.clear(); }
        }
        if (mainLabel.empty()) continue;

        if (sub.empty()) {
            text(mainLabel, fKey, r, labelBrush);
        } else {
            D2D1_RECT_F rt2 = Rect(k.x, k.y + k.h * 0.06f, k.x + k.w, k.y + k.h * 0.42f);
            D2D1_RECT_F rb2 = Rect(k.x, k.y + k.h * 0.40f, k.x + k.w, k.y + k.h * 0.94f);
            text(sub, fKey2, rt2, bDim);
            text(mainLabel, fKey, rb2, labelBrush);
        }
    }

    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseBrushes();
        rt->Release();
        rt = nullptr;
    }
}
