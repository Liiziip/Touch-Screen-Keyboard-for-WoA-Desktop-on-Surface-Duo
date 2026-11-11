#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "kb.h"

struct Palette {
    D2D1_COLOR_F bg;
    D2D1_COLOR_F key;
    D2D1_COLOR_F special;
    D2D1_COLOR_F pressed;
    D2D1_COLOR_F text;
    D2D1_COLOR_F dim;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F onAccent;
    D2D1_COLOR_F tint;      // tint drawn over the blurred backdrop
    D2D1_COLOR_F edge;      // hairline around translucent key faces
};

struct Renderer {
    ID2D1Factory* d2d = nullptr;
    ID2D1DCRenderTarget* rt = nullptr;
    IDWriteFactory* dw = nullptr;

    // Off-screen surface; we blit it into the window in WM_PAINT. Avoiding the
    // HWND render target keeps EndDraw() away from the DWM present path.
    HDC memDC = nullptr;
    HBITMAP memBmp = nullptr;
    HGDIOBJ memOld = nullptr;
    int memW = 0;
    int memH = 0;

    Palette pal;
    bool darkTheme = true;
    bool software = false;
    bool acrylic = true;

    IDWriteTextFormat* fKey = nullptr;
    IDWriteTextFormat* fKey2 = nullptr;
    IDWriteTextFormat* fSmall = nullptr;
    IDWriteTextFormat* fSmallL = nullptr;
    IDWriteTextFormat* fMid = nullptr;
    IDWriteTextFormat* fMidL = nullptr;
    IDWriteTextFormat* fBig = nullptr;

    ID2D1SolidColorBrush* bKey = nullptr;
    ID2D1SolidColorBrush* bSpecial = nullptr;
    ID2D1SolidColorBrush* bPressed = nullptr;
    ID2D1SolidColorBrush* bText = nullptr;
    ID2D1SolidColorBrush* bDim = nullptr;
    ID2D1SolidColorBrush* bAccent = nullptr;
    ID2D1SolidColorBrush* bOnAccent = nullptr;
    ID2D1SolidColorBrush* bTint = nullptr;
    ID2D1SolidColorBrush* bEdge = nullptr;

    // Blurred screenshot of the screen behind the keyboard (simulated acrylic).
    ID2D1Bitmap* bgBmp = nullptr;

    bool Init();
    bool EnsureTarget(HWND hwnd, int pxW, int pxH);
    void SetSizes(float keyH);
    void SetTheme(bool dark);
    void SetAcrylic(bool on);
    void SetBackdrop(HBITMAP bmp, int w, int h);
    void Shutdown();

    void Draw(const Layout& L, float scale, float widthDip, int pressed,
              int ime, const std::wstring& target, const std::wstring& monitor,
              bool shiftOn, bool capsOn, bool ctrlOn, bool altOn, bool winOn, bool fnOn,
              const std::vector<MonButton>& mons, int curMon,
              const std::vector<MonButton>& zoomBtns, HDC targetDC);

private:
    void BuildPalette();
    void ReleaseBrushes();
    bool  MakeBrushes();
};
