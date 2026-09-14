// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <windows.h>
#include <algorithm>

namespace M88V {
enum class ScaleFilter { Nearest, Bilinear, Bicubic };
inline RECT FitScreen(int width, int height) {
    width = std::max(0, width); height = std::max(0, height);
    int w = width, h = MulDiv(w, 5, 8);
    if (h > height) { h = height; w = MulDiv(h, 8, 5); }
    int x = (width-w)/2, y = (height-h)/2;
    return {x,y,x+w,y+h};
}
inline void ConstrainScreen(RECT& rect, UINT edge, int frameWidth, int frameHeight, int startWidth=640, int startHeight=400) {
    int w = std::max(320L, rect.right-rect.left-frameWidth);
    int h = std::max(200L, rect.bottom-rect.top-frameHeight);
    // Horizontal edges follow width; vertical edges follow height.
    // At corners choose the axis with the larger relative change from 8:5.
    bool corner=edge==WMSZ_TOPLEFT || edge==WMSZ_TOPRIGHT || edge==WMSZ_BOTTOMLEFT || edge==WMSZ_BOTTOMRIGHT;
    if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM || (corner && abs(h-startHeight)*8 > abs(w-startWidth)*5)) w = MulDiv(h,8,5);
    else h = MulDiv(w,5,8);
    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT) rect.left=rect.right-w-frameWidth;
    else rect.right=rect.left+w+frameWidth;
    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT) rect.top=rect.bottom-h-frameHeight;
    else rect.bottom=rect.top+h+frameHeight;
}
}
