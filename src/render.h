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
};

struct Renderer {
    ID2D1Factory* d2d = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    IDWriteFactory* dw = nullptr;

    Palette pal;
    bool darkTheme = true;

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

    bool Init();
    bool EnsureTarget(HWND hwnd, int pxW, int pxH);
    void SetSizes(float keyH);
    void SetTheme(bool dark);
    void Shutdown();

    void Draw(const Layout& L, float scale, float widthDip, int pressed,
              int ime, const std::wstring& target, const std::wstring& monitor,
              bool shiftOn, bool capsOn, bool ctrlOn, bool altOn, bool winOn, bool fnOn,
              const std::vector<MonButton>& mons, int curMon,
              const std::vector<MonButton>& zoomBtns);

private:
    void BuildPalette();
    void ReleaseBrushes();
    bool  MakeBrushes();
};
