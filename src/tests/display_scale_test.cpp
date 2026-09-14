// SPDX-License-Identifier: BSD-2-Clause
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "win32/display_scale.h"
#include "win32/screen_presenter.h"
#include <vector>
#include <iostream>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        for (int percent=50; percent<=400; percent+=50) {
            int w=640*percent/100,h=400*percent/100;
            RECT fit=M88V::FitScreen(w,h);
            Check(fit.left==0 && fit.top==0 && fit.right==w && fit.bottom==h,"preset size");
        }
        RECT maximized=M88V::FitScreen(1920,1000);
        Check(maximized.left==160 && maximized.top==0 && maximized.right==1760 && maximized.bottom==1000,"maximized aspect ratio");
        for (UINT edge=WMSZ_LEFT; edge<=WMSZ_BOTTOMRIGHT; ++edge) {
            RECT r={10,20,950,790};
            M88V::ConstrainScreen(r,edge,16,118);
            int w=r.right-r.left-16,h=r.bottom-r.top-118;
            Check(abs(w*5-h*8)<=4,"drag keeps 8:5 after excluding chrome/footer");
            if (edge==WMSZ_LEFT || edge==WMSZ_TOPLEFT || edge==WMSZ_BOTTOMLEFT) Check(r.right==950,"opposite horizontal edge anchored");
            if (edge==WMSZ_TOP || edge==WMSZ_TOPLEFT || edge==WMSZ_TOPRIGHT) Check(r.bottom==790,"opposite vertical edge anchored");
        }
        Gdiplus::GdiplusStartupInput startup; ULONG_PTR token=0;
        Check(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok,"GDI+ startup");
        BITMAPINFO info={}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=160; info.bmiHeader.biHeight=-100;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        HDC dc=CreateCompatibleDC(nullptr); void* output=nullptr;
        HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&output,nullptr,0);
        Check(bitmap && output,"test surface"); HGDIOBJ old=SelectObject(dc,bitmap);
        std::vector<uint32_t> source(64*40);
        for(int y=0;y<40;++y) for(int x=0;x<64;++x) source[y*64+x]=((x/3+y/3)&1)?0xffeeeeee:0xff112244;
        std::vector<uint32_t> rendered[3];
        for(int filter=0;filter<3;++filter) {
            Check(PresentScreen(dc,source.data(),64,40,160,100,filter),"render selected filter");
            GdiFlush(); auto pixels=static_cast<uint32_t*>(output);
            rendered[filter].assign(pixels,pixels+160*100);
        }
        for(auto pixel:rendered[0]) Check((pixel&0xffffff)==0xeeeeee || (pixel&0xffffff)==0x112244,"nearest preserves source colors");
        Check(rendered[0]!=rendered[1] && rendered[1]!=rendered[2],"filters produce distinct pixels");
        Check(PresentScreen(dc,source.data(),64,40,64,40,0),"100 percent render");
        GdiFlush(); auto result=static_cast<uint32_t*>(output);
        for(int y=0;y<40;++y) for(int x=0;x<64;++x)
            Check((result[y*160+x]&0xffffff)==(source[y*64+x]&0xffffff),"unscaled pixels unchanged");
        std::fill(source.begin(),source.end(),0xff336699);
        for(int filter=0;filter<3;++filter) {
            std::fill(result,result+160*100,0x00775533);
            Check(PresentScreen(dc,source.data(),64,40,32,20,filter),"50 percent render");
            GdiFlush();
            for(int y=0;y<20;++y) for(int x=0;x<32;++x)
                Check((result[y*160+x]&0xffffff)==0x336699,"downscale has no dark borders");
            Check(result[20*160]==0x00775533,"footer remains untouched");
        }
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);Gdiplus::GdiplusShutdown(token);
        std::cout<<"Scale presets, aspect ratio, all resize handles and three render filters passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
