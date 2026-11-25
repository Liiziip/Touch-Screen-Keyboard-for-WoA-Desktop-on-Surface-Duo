#include "render.h"
#include <cstdio>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "gdi32.lib")

namespace {
D2D1_RECT_F Rect(float l, float t, float r, float b) {
    D2D1_RECT_F x;
    x.left = l; x.top = t; x.right = r; x.bottom = b;
    return x;
}
} // namespace

void Renderer::BuildPalette() {
    // With a blurred backdrop behind, key faces are translucent so the material
    // shows through. Without it they stay opaque.
    const float ka = acrylic ? 0.72f : 1.0f;
    const float sa = acrylic ? 0.78f : 1.0f;
    const float pa = acrylic ? 0.88f : 1.0f;

    if (darkTheme) {
        pal.bg       = D2D1::ColorF(0x1F1F1F);
        pal.key      = D2D1::ColorF(0x333333, ka);
        pal.special  = D2D1::ColorF(0x2B2B2B, sa);
        pal.pressed  = D2D1::ColorF(0x4D4D4D, pa);
        pal.text     = D2D1::ColorF(0xF2F2F2);
        pal.dim      = D2D1::ColorF(0x9E9E9E);
        pal.accent   = D2D1::ColorF(0x0078D4);
        pal.onAccent = D2D1::ColorF(0xFFFFFF);
        pal.tint     = D2D1::ColorF(0x1F1F1F, 0.80f);
        pal.edge     = D2D1::ColorF(0xFFFFFF, acrylic ? 0.10f : 0.0f);
    } else {
        pal.bg       = D2D1::ColorF(0xE6E6E6);
        pal.key      = D2D1::ColorF(0xFFFFFF, acrylic ? 0.68f : 1.0f);
        pal.special  = D2D1::ColorF(0xD8D8D8, acrylic ? 0.72f : 1.0f);
        pal.pressed  = D2D1::ColorF(0xC2C2C2, pa);
        pal.text     = D2D1::ColorF(0x1A1A1A);
        pal.dim      = D2D1::ColorF(0x5A5A5A);
        pal.accent   = D2D1::ColorF(0x0067C0);
        pal.onAccent = D2D1::ColorF(0xFFFFFF);
        pal.tint     = D2D1::ColorF(0xF2F2F2, 0.78f);
        pal.edge     = D2D1::ColorF(0x000000, acrylic ? 0.10f : 0.0f);
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

void Renderer::SetAcrylic(bool on) {
    acrylic = on;
    if (!on && bgBmp) { bgBmp->Release(); bgBmp = nullptr; }
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
    if (bTint)     { bTint->Release();     bTint = nullptr; }
    if (bEdge)     { bEdge->Release();     bEdge = nullptr; }
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
    if (FAILED(rt->CreateSolidColorBrush(pal.tint, &bTint))) return false;
    if (FAILED(rt->CreateSolidColorBrush(pal.edge, &bEdge))) return false;
    return true;
}

void Renderer::SetBackdrop(HBITMAP bmp, int w, int h) {
    if (!rt || !bmp || w <= 0 || h <= 0) return;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<UINT32> px((size_t)w * (size_t)h);
    HDC dc = GetDC(nullptr);
    int got = GetDIBits(dc, bmp, 0, h, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (!got) return;

    if (bgBmp) { bgBmp->Release(); bgBmp = nullptr; }
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    rt->CreateBitmap(D2D1::SizeU((UINT32)w, (UINT32)h), px.data(), (UINT32)(w * 4), props, &bgBmp);
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
    (void)hwnd;
    if (pxW <= 0 || pxH <= 0) return false;
    if (!rt) {
        D2D1_RENDER_TARGET_PROPERTIES props = software
            ? D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE)
            : D2D1::RenderTargetProperties();
        props.dpiX = 96.0f;
        props.dpiY = 96.0f;
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                             D2D1_ALPHA_MODE_IGNORE);
        if (FAILED(d2d->CreateDCRenderTarget(&props, &rt))) return false;
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (!MakeBrushes()) return false;
    }
    if (pxW != memW || pxH != memH) {
        if (memDC) {
            SelectObject(memDC, memOld);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            memDC = nullptr;
            memBmp = nullptr;
        }
        HDC screen = GetDC(nullptr);
        memDC = CreateCompatibleDC(screen);
        memBmp = CreateCompatibleBitmap(screen, pxW, pxH);
        memOld = SelectObject(memDC, memBmp);
        ReleaseDC(nullptr, screen);
        memW = pxW;
        memH = pxH;
    }
    return true;
}

void Renderer::SetSizes(float keyH, float keyW) {
    if (!dw) return;
    // Never let a label outgrow a narrow key.
    float base = (std::min)(keyH * 0.30f, keyW * 0.42f);
    if (base < 10.0f) base = 10.0f;
    struct Slot { IDWriteTextFormat** p; float size; bool left; };
    Slot slots[] = {
        { &fKey,    base, false },
        { &fKey2,   base * 0.66f, false },
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

    if (fIcon) { fIcon->Release(); fIcon = nullptr; }
    if (SUCCEEDED(dw->CreateTextFormat(L"Segoe MDL2 Assets", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"zh-CN", &fIcon))) {
        fIcon->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fIcon->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fIcon->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
}

void Renderer::Shutdown() {
    ReleaseBrushes();
    if (bgBmp) { bgBmp->Release(); bgBmp = nullptr; }
    if (rt) { rt->Release(); rt = nullptr; }
    if (memDC) {
        SelectObject(memDC, memOld);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        memDC = nullptr;
        memBmp = nullptr;
        memW = memH = 0;
    }
    if (fKey)  { fKey->Release();  fKey = nullptr; }
    if (fKey2) { fKey2->Release(); fKey2 = nullptr; }
    if (fSmall){ fSmall->Release();fSmall = nullptr; }
    if (fSmallL){fSmallL->Release();fSmallL = nullptr; }
    if (fMid)  { fMid->Release();  fMid = nullptr; }
    if (fMidL) { fMidL->Release(); fMidL = nullptr; }
    if (fBig)  { fBig->Release();  fBig = nullptr; }
    if (fIcon) { fIcon->Release(); fIcon = nullptr; }
    if (dw) { dw->Release(); dw = nullptr; }
    if (d2d){ d2d->Release(); d2d = nullptr; }
}

void Renderer::Draw(const Layout& L, float scale, float widthDip, int pressed,
                    int ime, const std::wstring& target, const std::wstring& monitor,
                    bool shiftOn, bool capsOn, bool ctrlOn, bool altOn, bool winOn, bool fnOn,
                    const std::vector<MonButton>& mons, int curMon,
                    const std::vector<MonButton>& zoomBtns,
                    const std::vector<MonButton>& toolBtns, HDC targetDC) {
    if (!rt) return;

    RECT bindRect = { 0, 0, memW, memH };
    rt->BindDC(memDC, &bindRect);
    rt->BeginDraw();
    D2D1::Matrix3x2F xf = D2D1::Matrix3x2F::Scale(L.uiScale, L.uiScale) *
                         D2D1::Matrix3x2F::Translation(L.ox, L.oy) *
                         D2D1::Matrix3x2F::Scale(scale, scale);
    rt->SetTransform(xf);
    rt->Clear(pal.bg);
    if (acrylic && bgBmp) {
        // Cover the whole window, not just the 900x675 canvas, so the blurred
        // backdrop reaches the letterboxed edges too.
        float winW = (scale > 0.0f) ? (float)memW / scale : L.designW;
        float winH = (scale > 0.0f) ? (float)memH / scale : L.designH;
        D2D1_RECT_F bgRect = Rect(-L.ox / L.uiScale, -L.oy / L.uiScale,
                                  (winW - L.ox) / L.uiScale, (winH - L.oy) / L.uiScale);
        rt->DrawBitmap(bgBmp, bgRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        rt->FillRectangle(bgRect, bTint);
    }
    (void)widthDip;

    auto text = [&](const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F r, ID2D1SolidColorBrush* b) {
        if (!s.empty() && f) rt->DrawTextW(s.c_str(), (UINT32)s.size(), f, r, b,
                                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    // ---- floating close button (single-screen layout) ----
    if (!L.split && !L.half) {
        D2D1_RECT_F closeR = Rect(L.designW - 44, 8, L.designW - 12, 40);
        rt->FillRoundedRectangle(D2D1::RoundedRect(closeR, 6, 6), bSpecial);
        text(L"\x00D7", fMid, closeR, bText);

        if (L.menuPortrait) {
            D2D1_RECT_F gr = Rect(L.gearBtn.l, L.gearBtn.t, L.gearBtn.r, L.gearBtn.b);
            rt->FillRoundedRectangle(D2D1::RoundedRect(gr, 5, 5), bSpecial);
            text(L"\xE713", fIcon, gr, bText);
        }
    } else {
        // Landscape split: gear at the top-left of the left half, close at the
        // top-left of the right half.
        auto iconBtn = [&](const RectF& r, const wchar_t* glyph) {
            D2D1_RECT_F rr = Rect(r.l, r.t, r.r, r.b);
            rt->FillRoundedRectangle(D2D1::RoundedRect(rr, 5, 5), bSpecial);
            text(glyph, fIcon, rr, bText);
        };
        iconBtn(L.gearBtn, L"\xE713");
        iconBtn(L.closeBtn2, L"\xE711");
    }
    (void)monitor;

    // ---- status area ----
    float sy = 16.0f;
    if (!L.split && L.statusH > 90.0f) {
        D2D1_RECT_F pill = Rect(16, sy, 136, sy + 56);
        ID2D1SolidColorBrush* pf = (ime == 1) ? bAccent : bSpecial;
        rt->FillRoundedRectangle(D2D1::RoundedRect(pill, 12, 12), pf);
        std::wstring imeText = (ime == 1) ? L"中" : (ime == 0 ? L"EN" : L"—");
        text(imeText, fBig, pill, (ime == 1) ? bOnAccent : bText);

        std::wstring imeLine = (ime == 1) ? L"Chinese IME"
                              : (ime == 0 ? L"English input" : L"No IME detected");
        text(imeLine, fMidL, Rect(152, sy + 2, L.designW - 16, sy + 32), bText);
        text(L"Tap a text field, then type",
             fSmallL, Rect(152, sy + 30, L.designW - 16, sy + 54), bDim);

        float ty = sy + 72.0f;
        text(L"Target: " + target, fSmallL, Rect(16, ty, L.designW - 16, ty + 22), bDim);

        if (L.statusH > 170.0f) {
            text(L"Tap Shift for CN/EN · Hold Shift/Win to lock",
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
        if (!toolBtns.empty()) {
            float tr = toolBtns.back().x + toolBtns.back().w + 10.0f;
            if (tr > right) right = tr;
        }
        float bottom = mons[0].y + mons[0].h + 10.0f;
        rt->FillRoundedRectangle(D2D1::RoundedRect(Rect(left, top, right, bottom), 10, 10), bSpecial);
        text(L"Display", fSmallL, Rect(left + 10.0f, top, left + 62.0f, bottom), bDim);

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

    for (const MonButton& t : toolBtns) {
        D2D1_RECT_F r = Rect(t.x, t.y, t.x + t.w, t.y + t.h);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 8, 8), bKey);
        text(t.label, fSmall, r, bText);
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
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 8, 8);
        rt->FillRoundedRectangle(rr, fill);
        if (acrylic) rt->DrawRoundedRectangle(rr, bEdge, 1.0f);
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

        if (!k.hint.empty()) {
            D2D1_RECT_F hr = Rect(k.x + 7.0f, k.y + 2.0f,
                                  k.x + k.w * 0.55f, k.y + k.h * 0.34f);
            text(k.hint, fSmallL, hr, bDim);
        }

        if (sub.empty()) {
            text(mainLabel, mainLabel.size() >= 3 ? fKey2 : fKey, r, labelBrush);
        } else {
            D2D1_RECT_F rt2 = Rect(k.x, k.y + k.h * 0.06f, k.x + k.w, k.y + k.h * 0.42f);
            D2D1_RECT_F rb2 = Rect(k.x, k.y + k.h * 0.40f, k.x + k.w, k.y + k.h * 0.94f);
            text(sub, fKey2, rt2, bDim);
            text(mainLabel, fKey, rb2, labelBrush);
        }
    }

    // ---- in-app mode menu (gear) ----
    if (L.showModeMenu && (L.split || L.half || L.menuPortrait)) {
        D2D1_RECT_F pr = Rect(L.menuPanel.l, L.menuPanel.t, L.menuPanel.r, L.menuPanel.b);
        rt->FillRoundedRectangle(D2D1::RoundedRect(pr, 10, 10), bSpecial);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(pr, 10, 10), bEdge, 1.0f);

        if (L.menuPortrait) {
            for (int i = 0; i < L.dispCount; ++i) {
                D2D1_RECT_F br = Rect(L.dispBtn[i].l, L.dispBtn[i].t,
                                      L.dispBtn[i].r, L.dispBtn[i].b);
                bool sel = (i == L.dispSel);
                rt->FillRoundedRectangle(D2D1::RoundedRect(br, 8, 8), sel ? bAccent : bKey);
                text(L.dispLabel[i], fSmall, br, sel ? bOnAccent : bText);
            }
            D2D1_RECT_F z0 = Rect(L.zoomBtn[0].l, L.zoomBtn[0].t, L.zoomBtn[0].r, L.zoomBtn[0].b);
            D2D1_RECT_F z1 = Rect(L.zoomBtn[1].l, L.zoomBtn[1].t, L.zoomBtn[1].r, L.zoomBtn[1].b);
            D2D1_RECT_F z2 = Rect(L.zoomBtn[2].l, L.zoomBtn[2].t, L.zoomBtn[2].r, L.zoomBtn[2].b);
            rt->FillRoundedRectangle(D2D1::RoundedRect(z0, 8, 8), bKey);
            rt->FillRoundedRectangle(D2D1::RoundedRect(z1, 8, 8), bKey);
            rt->FillRoundedRectangle(D2D1::RoundedRect(z2, 8, 8), bKey);
            text(L"-", fSmall, z0, bText);
            text(L.zoomText.empty() ? L"100%" : L.zoomText, fSmall, z1, bText);
            text(L"+", fSmall, z2, bText);
        } else {
            static const wchar_t* kLabels[3] = { L"Span", L"Left", L"Right" };
            for (int i = 0; i < 3; ++i) {
                D2D1_RECT_F br = Rect(L.modeBtn[i].l, L.modeBtn[i].t,
                                      L.modeBtn[i].r, L.modeBtn[i].b);
                bool sel = (L.menuSel == i + 1);
                rt->FillRoundedRectangle(D2D1::RoundedRect(br, 8, 8), sel ? bAccent : bKey);
                text(kLabels[i], fSmall, br, sel ? bOnAccent : bText);
            }
        }

        D2D1_RECT_F ar = Rect(L.acrylicBtn.l, L.acrylicBtn.t,
                              L.acrylicBtn.r, L.acrylicBtn.b);
        rt->FillRoundedRectangle(D2D1::RoundedRect(ar, 8, 8), L.acrylicOn ? bAccent : bKey);
        text(L.acrylicOn ? L"Acrylic : on" : L"Acrylic : off",
             fSmall, ar, L.acrylicOn ? bOnAccent : bText);
    }

    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseBrushes();
        rt->Release();
        rt = nullptr;
        return;
    }
    if (SUCCEEDED(hr) && targetDC && memDC) {
        BitBlt(targetDC, 0, 0, memW, memH, memDC, 0, 0, SRCCOPY);
    }
}
