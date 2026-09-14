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
    paletteDirty = presentationDirty = true;
    status |= Draw::shouldrefresh;
    return true;
}
bool WinDrawScaled::Cleanup() {
    if (gdiplus) { Gdiplus::GdiplusShutdown(gdiplus); gdiplus=0; }
    return true;
}
void WinDrawScaled::SetPresentation(int w, int h, int method) {
    presentationDirty |= viewWidth != std::max(0,w) || viewHeight != std::max(0,h) || filter != method;
    viewWidth=std::max(0,w); viewHeight=std::max(0,h); filter=method;
}
void WinDrawScaled::SetPalette(PALETTEENTRY* entries, int first, int count) {
    for (int i=0;i<count;++i) {
        const auto& c=entries[i];
        const uint32 color=0xff000000u | (uint32(c.peRed)<<16) | (uint32(c.peGreen)<<8) | c.peBlue;
        paletteDirty |= palette[first+i] != color;
        palette[first+i]=color;
    }
}
bool WinDrawScaled::Lock(uint8** image, int* stride) {
    *image=indices.data(); *stride=width; return !indices.empty();
}
void WinDrawScaled::DrawScreen(const RECT& changed, bool refresh) {
    if (indices.empty() || !viewWidth || !viewHeight) return;
    RECT area={std::clamp<LONG>(changed.left,0,width),std::clamp<LONG>(changed.top,0,height),
        std::clamp<LONG>(changed.right,0,width),std::clamp<LONG>(changed.bottom,0,height)};
    const bool valid=area.left<area.right && area.top<area.bottom;
    if (!valid && !refresh && !paletteDirty && !presentationDirty) return;
    // Palette changes affect unchanged pixels too. A forced repaint also repairs
    // the entire cache, while ordinary frames convert only their dirty region.
    if (paletteDirty || refresh) area={0,0,width,height};
    for (LONG y=area.top;y<area.bottom;++y)
        for (LONG x=area.left;x<area.right;++x) {
            const size_t i=size_t(y)*width+x;
            pixels[i]=palette[indices[i]];
        }
    HDC dc=GetDC(window);
    if (dc) {
        const bool presented=PresentScreen(dc,pixels.data(),width,height,viewWidth,viewHeight,filter);
        ReleaseDC(window,dc);
        paletteDirty = presentationDirty = !presented;
    } else {
        presentationDirty = true;
    }
}
