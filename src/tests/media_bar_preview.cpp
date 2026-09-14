// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "win32/media_bar.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main()
{
    try {
        // Hidden parent: verify footer sizing without ROMs or an interactive desktop.
        HWND parent = CreateWindowW(L"STATIC", L"Media layout test", WS_POPUP,
            0, 0, 640, 500, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
        Check(parent != nullptr, "create parent");
        MediaBar bar;
        Check(bar.Create(parent) && bar.Create(parent), "create is idempotent");
        bar.Resize(20);
        HWND child = FindWindowExW(parent, nullptr, L"M88VMediaBar", nullptr);
        Check(child != nullptr, "media child exists");
        RECT bounds; GetWindowRect(child, &bounds);
        Check(bounds.right-bounds.left == 640 && bounds.bottom == 480 && bounds.bottom-bounds.top == bar.Height(), "footer avoids status row");
        auto click = [&](int downX, int upX, int expected) {
            SendMessage(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(downX, 12));
            SendMessage(child, WM_LBUTTONUP, 0, MAKELPARAM(upX, 12));
            MSG message = {};
            bool posted = !!PeekMessage(&message, parent, MediaBar::MenuMessage, MediaBar::MenuMessage, PM_REMOVE);
            Check(expected < 0 ? !posted : posted && message.wParam == expected, "indicator click routes to matching menu");
        };
        click(20, 20, 0);
        click(212, 212, 0);
        click(213, 213, 1);
        click(426, 426, 2);
        click(639, 639, 2);
        click(20, 300, -1); // Moving to another slot cancels.
        click(20, -1, -1);  // Releasing outside the bar cancels.
        SetWindowPos(parent, nullptr, 0, 0, 800, 600, SWP_NOZORDER | SWP_NOACTIVATE);
        bar.Resize(0); GetWindowRect(child, &bounds);
        Check(bounds.right-bounds.left == 800 && bounds.bottom == 600, "footer resizes without status row");
        click(266, 266, 1);
        click(533, 533, 2);
        bar.Destroy(); Check(!bar.IsOpen() && bar.Height() == 0 && !IsWindow(child), "hide footer");
        Check(bar.Create(parent), "restore footer after fullscreen");
        bar.Destroy(); DestroyWindow(parent);

        constexpr int width = 640, height = 3*74;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HDC dc = CreateCompatibleDC(nullptr);
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        Check(bitmap && pixels, "create preview bitmap");
        HGDIOBJ old = SelectObject(dc, bitmap);
        RECT canvas = {0,0,width,height};
        FillRect(dc, &canvas, GetSysColorBrush(COLOR_3DFACE));
        std::array<MediaSlot,3> slots;
        slots[0] = {L"system.d88 / N88-BASIC", L"READY 1/2", true, 0};
        slots[1] = {L"work.d88 / USER DISK", L"READY", true, 0};
        slots[2] = {L"sample.t88", L"STOP 00:00", true, 0};
        DrawMediaBar(dc, {0,0,width,58}, slots, 96);
        slots[0].detail=L"ACCESS 1/2"; slots[0].lamp=1;
        slots[1] = {};
        slots[2].detail=L"PLAY 01:24"; slots[2].lamp=1;
        DrawMediaBar(dc, {0,74,width,132}, slots, 96);
        slots[0] = {L"long-project-name-backup.d88 / Disk 2", L"READY 2/2", true, 0};
        slots[1] = {L"\u958b\u767a\u7528.d88 / DATA", L"ACCESS", true, 1};
        slots[2] = {L"Recording buffer *", L"REC", true, 2};
        DrawMediaBar(dc, {0,148,width,206}, slots, 96);
        GdiFlush();
        BITMAPFILEHEADER header = {};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header)+sizeof(info.bmiHeader);
        header.bfSize = header.bfOffBits+width*height*4;
        std::ofstream out("media-bar-preview.bmp",std::ios::binary);
        out.write(reinterpret_cast<char*>(&header),sizeof(header));
        out.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader));
        out.write(static_cast<char*>(pixels),width*height*4);
        Check(bool(out), "write preview");
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
        std::cout << "Media layout/lifecycle checks passed; media-bar-preview.bmp rendered\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
