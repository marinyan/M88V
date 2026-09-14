// SPDX-License-Identifier: BSD-2-Clause
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "screen_presenter.h"
#include "display_scale.h"
#include "screen_resampler.h"

bool PresentScreen(HDC dc, const uint32_t* pixels, int width, int height, int viewWidth, int viewHeight, int filter)
{
    if (!dc || !pixels || width<=0 || height<=0 || viewWidth<=0 || viewHeight<=0) return false;
    bool result=false;
    int saved=SaveDC(dc);
    if (!saved) return false;
    IntersectClipRect(dc,0,0,viewWidth,viewHeight);
    RECT fit=M88V::FitScreen(viewWidth,viewHeight);
    if (filter==0 || (fit.right-fit.left==width && fit.bottom-fit.top==height)) {
        // Opaque native pixels need no GDI+ image conversion or interpolation.
        const RECT margins[]={{0,0,viewWidth,fit.top},{0,fit.bottom,viewWidth,viewHeight},
            {0,fit.top,fit.left,fit.bottom},{fit.right,fit.top,viewWidth,fit.bottom}};
        for (const RECT& margin : margins) FillRect(dc,&margin,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        info.bmiHeader.biCompression=BI_RGB;
        SetStretchBltMode(dc,COLORONCOLOR);
        const int lines=StretchDIBits(dc,fit.left,fit.top,fit.right-fit.left,fit.bottom-fit.top,
            0,0,width,height,pixels,&info,DIB_RGB_COLORS,SRCCOPY);
        RestoreDC(dc,saved);
        return lines!=0 && lines!=GDI_ERROR;
    }
    const int scaledWidth=fit.right-fit.left,scaledHeight=fit.bottom-fit.top;
    thread_local M88V::ScreenResampler resampler;
    result=resampler.Render(pixels,width,height,scaledWidth,scaledHeight,filter);
    if (result) {
        const RECT margins[]={{0,0,viewWidth,fit.top},{0,fit.bottom,viewWidth,viewHeight},
            {0,fit.top,fit.left,fit.bottom},{fit.right,fit.top,viewWidth,fit.bottom}};
        for (const RECT& margin : margins) FillRect(dc,&margin,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=scaledWidth; info.bmiHeader.biHeight=-scaledHeight;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        info.bmiHeader.biCompression=BI_RGB;
        const int lines=StretchDIBits(dc,fit.left,fit.top,scaledWidth,scaledHeight,
            0,0,scaledWidth,scaledHeight,resampler.Pixels(),&info,DIB_RGB_COLORS,SRCCOPY);
        result=lines!=0 && lines!=GDI_ERROR;
    }
    RestoreDC(dc,saved); return result;
}
