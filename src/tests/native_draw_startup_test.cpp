// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "windraw.h"
#include <stdexcept>
#include <iostream>

static void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main() {
    HWND window=CreateWindowEx(0,"STATIC","Draw startup test",WS_POPUP,0,0,640,400,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    try {
        Check(window!=nullptr,"create hidden test window");
        WinDraw draw;
        Check(draw.Init0(window),"window initialization");
        // Match UI startup: presentation is configured before the core initializes.
        draw.SetPresentation(640,400,0);
        draw.RequestPaint();
        Check(draw.ChangeDisplayMode(false),"create window renderer");
        Check(draw.Init(640,400,8),"core framebuffer initialization");
        draw.RequestPaint();
        Check((draw.GetStatus() & Draw::readytodraw)!=0,"renderer ready after startup");
        uint8* pixels=nullptr; int stride=0;
        Check(draw.Lock(&pixels,&stride),"startup framebuffer must exist");
        Check(stride==640,"source stride is 640");
        memset(pixels,1,640*400);
        draw.Unlock();
        Draw::Palette palette[2]={{0,0,0,0},{255,255,255,0}};
        draw.SetPalette(0,2,palette);
        for(int percent=50;percent<=400;percent+=50) {
            draw.SetPresentation(640*percent/100,400*percent/100,percent%3);
            Check(draw.Lock(&pixels,&stride),"framebuffer survives scaling");
            Check(stride==640 && pixels[639+399*640]==1,"scaling preserves framebuffer");
            draw.Unlock();
            Draw::Region region={0,0,640,399};
            draw.DrawScreen(region);
            Check(draw.GetDrawCount()>0,"frames advance after scaling");
        }
        draw.Cleanup();
        DestroyWindow(window);
        std::cout<<"Native renderer startup and scaling passed\n";
        return 0;
    } catch(const std::exception& error) {
        DestroyWindow(window);
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
