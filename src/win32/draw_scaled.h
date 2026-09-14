// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "windraw.h"
#include "display_scale.h"
#include <vector>

// The emulated framebuffer stays indexed 640x400. Only presentation is scaled.
class WinDrawScaled : public WinDrawSub {
public:
    ~WinDrawScaled() override { Cleanup(); }
    bool Init(HWND, uint, uint, GUID*) override;
    bool Resize(uint, uint) override;
    bool Cleanup() override;
    void SetPresentation(int, int, int) override;
    void SetPalette(PALETTEENTRY*, int, int) override;
    void DrawScreen(const RECT&, bool) override;
    bool Lock(uint8** image, int* stride) override;
    bool Unlock() override { status &= ~Draw::shouldrefresh; return true; }
private:
    HWND window = nullptr;
    ULONG_PTR gdiplus = 0;
    int width=640, height=400, viewWidth=640, viewHeight=400;
    int filter=2;
    std::vector<uint8> indices;
    std::vector<uint32> pixels;
    uint32 palette[256] = {};
    bool paletteDirty = true;
    bool presentationDirty = true;
};
