// SPDX-License-Identifier: BSD-2-Clause
#include <windows.h>
#include "draw_scaled.h"
#include "screen_presenter.h"
#include <cstdio>
#include <stdexcept>

namespace {
int presentations=0;
bool succeed=true;
std::vector<uint32_t> lastPixels;
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

// Record the actual renderer's presentation requests, independent of the desktop.
bool PresentScreen(HDC, const uint32_t* pixels, int width, int height, int, int, int) {
    ++presentations;
    lastPixels.assign(pixels,pixels+size_t(width)*height);
    return succeed;
}

int main() {
    HWND window=CreateWindowExW(0,L"STATIC",L"dirty-frame-test",WS_POPUP,0,0,64,40,
        nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    try {
        Check(window!=nullptr,"create hidden window");
        {
            WinDrawScaled draw;
            Check(draw.Init(window,64,40,nullptr),"renderer init");
            draw.SetPresentation(64,40,0);
            PALETTEENTRY palette[2]={{0,0,0,0},{255,0,0,0}};
            draw.SetPalette(palette,0,2);
            uint8* source=nullptr; int stride=0;
            Check(draw.Lock(&source,&stride),"source lock");
            std::fill(source,source+64*40,0);
            draw.Unlock();
            const RECT empty={0,0,0,0},one={2,3,3,4};
            draw.DrawScreen(empty,false);
            Check(presentations==1 && lastPixels[0]==0xff000000,"initial dirty frame");
            draw.DrawScreen(empty,false);
            Check(presentations==1,"unchanged frame must not present");
            Check(draw.Lock(&source,&stride),"source relock");
            source[3*stride+2]=1;
            draw.Unlock();
            draw.DrawScreen(one,false);
            Check(presentations==2 && lastPixels[3*64+2]==0xffff0000 && lastPixels[0]==0xff000000,
                "partial update keeps cached pixels");
            palette[1]={0,255,0,0};
            draw.SetPalette(palette+1,1,1);
            draw.DrawScreen(empty,false);
            Check(presentations==3 && lastPixels[3*64+2]==0xff00ff00,"palette-only update");
            draw.SetPalette(palette+1,1,1);
            draw.DrawScreen(empty,false);
            Check(presentations==3,"identical palette does not force presentation");
            draw.SetPresentation(128,80,2);
            draw.DrawScreen(empty,false);
            Check(presentations==4,"presentation change repaints cached frame");
            draw.DrawScreen(empty,true);
            Check(presentations==5,"expose refresh repaints");
            succeed=false;
            draw.DrawScreen(one,false);
            succeed=true;
            draw.DrawScreen(empty,false);
            Check(presentations==7,"failed presentation retries on unchanged frame");
        }
        DestroyWindow(window);
        std::puts("native draw dirty-region tests passed");
        return 0;
    } catch (const std::exception& e) {
        if (window) DestroyWindow(window);
        std::fprintf(stderr,"%s\n",e.what()); return 1;
    }
}
