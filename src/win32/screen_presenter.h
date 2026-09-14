// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <windows.h>
#include <cstdint>

// Caller owns GDI+ startup and a top-down ARGB framebuffer.
bool PresentScreen(HDC dc, const uint32_t* pixels, int width, int height,
    int viewWidth, int viewHeight, int filter);
