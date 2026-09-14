// SPDX-License-Identifier: BSD-2-Clause
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "draw_scaled.h"
#include "screen_presenter.h"

bool WinDrawScaled::Init(HWND hwnd, uint w, uint h, GUID*) {
    window=hwnd;
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&gdiplus, &startup, nullptr) != Gdiplus::Ok) return false;
    return Resize(w,h);
}
bool WinDrawScaled::Resize(uint w, uint h) {
    width=w; height=h;
    indices.assign(size_t(w)*h,0x40); pixels.resize(size_t(w)*h);
    status |= Draw::shouldrefresh;
    return true;
}
bool WinDrawScaled::Cleanup() {
    if (gdiplus) { Gdiplus::GdiplusShutdown(gdiplus); gdiplus=0; }
    return true;
}
void WinDrawScaled::SetPresentation(int w, int h, int method) {
    viewWidth=std::max(0,w); viewHeight=std::max(0,h); filter=method;
}
void WinDrawScaled::SetPalette(PALETTEENTRY* entries, int first, int count) {
    for (int i=0;i<count;++i) {
        const auto& c=entries[i];
        palette[first+i]=0xff000000u | (uint32(c.peRed)<<16) | (uint32(c.peGreen)<<8) | c.peBlue;
    }
}
bool WinDrawScaled::Lock(uint8** image, int* stride) {
    *image=indices.data(); *stride=width; return !indices.empty();
}
void WinDrawScaled::DrawScreen(const RECT&, bool) {
    if (indices.empty() || !viewWidth || !viewHeight) return;
    for (size_t i=0;i<indices.size();++i) pixels[i]=palette[indices[i]];
    HDC dc=GetDC(window);
    PresentScreen(dc,pixels.data(),width,height,viewWidth,viewHeight,filter);
    ReleaseDC(window,dc);
}
