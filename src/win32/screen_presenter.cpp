// SPDX-License-Identifier: BSD-2-Clause
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "screen_presenter.h"
#include "display_scale.h"

bool PresentScreen(HDC dc, const uint32_t* pixels, int width, int height, int viewWidth, int viewHeight, int filter)
{
    if (!dc || !pixels || width<=0 || height<=0 || viewWidth<=0 || viewHeight<=0) return false;
    bool result=false;
    int saved=SaveDC(dc);
    IntersectClipRect(dc,0,0,viewWidth,viewHeight);
    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetInterpolationMode(filter==2 ? Gdiplus::InterpolationModeHighQualityBicubic :
            filter==1 ? Gdiplus::InterpolationModeHighQualityBilinear : Gdiplus::InterpolationModeNearestNeighbor);
        RECT fit=M88V::FitScreen(viewWidth,viewHeight);
        // Clear only letterbox margins, avoiding a black flash over the image.
        Gdiplus::SolidBrush black(Gdiplus::Color(255,0,0,0));
        graphics.FillRectangle(&black,0,0,viewWidth,fit.top);
        graphics.FillRectangle(&black,0,fit.bottom,viewWidth,viewHeight-fit.bottom);
        graphics.FillRectangle(&black,0,fit.top,fit.left,fit.bottom-fit.top);
        graphics.FillRectangle(&black,fit.right,fit.top,viewWidth-fit.right,fit.bottom-fit.top);
        Gdiplus::Bitmap bitmap(width,height,width*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(const_cast<uint32_t*>(pixels)));
        Gdiplus::ImageAttributes attributes;
        attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        result = graphics.DrawImage(&bitmap,Gdiplus::Rect(fit.left,fit.top,fit.right-fit.left,fit.bottom-fit.top),
            0,0,width,height,Gdiplus::UnitPixel,&attributes) == Gdiplus::Ok;
    }
    RestoreDC(dc,saved); return result;
}
