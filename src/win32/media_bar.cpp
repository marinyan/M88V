// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "media_bar.h"
#include <algorithm>
#include <commctrl.h>

void DrawMediaBar(HDC dc, RECT bounds, const std::array<MediaSlot, 3>& slots, UINT dpi)
{
    int saved = SaveDC(dc);
    IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    auto px = [dpi](int value) { return MulDiv(value, dpi, 96); };
    auto fill = [&](RECT rect, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush);
    };
    const COLORREF background = GetSysColor(COLOR_3DFACE);
    const COLORREF text = GetSysColor(COLOR_BTNTEXT);
    const COLORREF subdued = GetSysColor(COLOR_GRAYTEXT);
    const COLORREF divider = GetSysColor(COLOR_3DSHADOW);
    fill(bounds, background);
    HFONT caption = CreateFontW(-px(11), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT filename = CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Yu Gothic UI");
    SetBkMode(dc, TRANSPARENT);
    const wchar_t* labels[] = { L"DISK 1", L"DISK 2", L"CASSETTE" };
    int width = bounds.right - bounds.left;
    for (int i = 0; i < 3; ++i) {
        RECT cell = {bounds.left + width*i/3, bounds.top, bounds.left + width*(i+1)/3, bounds.bottom};
        int clipped = SaveDC(dc);
        IntersectClipRect(dc, cell.left, cell.top, cell.right, cell.bottom);
        if (i) fill({cell.left, cell.top+px(9), cell.left+1, cell.bottom-px(9)}, divider);
        int x = cell.left + px(12), y = cell.top + px(12);
        COLORREF light = slots[i].lamp == 2 ? RGB(210, 48, 40) : slots[i].lamp == 1 ? RGB(0, 150, 75) : divider;
        HBRUSH brush = CreateSolidBrush(light);
        HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, x, y, x+px(8), y+px(8));
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(brush);
        SelectObject(dc, caption);
        SetTextColor(dc, text);
        RECT title = {x+px(15), cell.top+px(8), cell.right-px(8), cell.top+px(25)};
        DrawTextW(dc, labels[i], -1, &title, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        RECT detail = {x+px(i == 2 ? 85 : 68), title.top, cell.right-px(10), title.bottom};
        SetTextColor(dc, slots[i].mounted ? text : subdued);
        DrawTextW(dc, slots[i].detail.c_str(), -1, &detail,
            DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, filename);
        SetTextColor(dc, slots[i].mounted ? text : subdued);
        RECT name = {x, cell.top+px(29), cell.right-px(10), cell.bottom-px(8)};
        DrawTextW(dc, slots[i].name.c_str(), -1, &name,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        RestoreDC(dc, clipped);
    }
    RestoreDC(dc, saved);
    DeleteObject(caption); DeleteObject(filename);
}

bool MediaBar::Create(HWND owner)
{
    if (window) return true;
    parent = owner;
    HDC dc = GetDC(parent); dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(parent, dc);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"M88VMediaBar"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    window = CreateWindowExW(0, wc.lpszClassName, L"Media status", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, parent, nullptr, wc.hInstance, this);
    if (!window) return false;
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, wc.hInstance, nullptr);
    if (tooltip) {
        SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, MulDiv(500, dpi, 96));
        for (UINT i=0; i<3; ++i) {
            TOOLINFOW tool = {}; tool.cbSize=sizeof(tool); tool.hwnd=window; tool.uId=i+1;
            tool.uFlags=TTF_SUBCLASS; tool.lpszText=const_cast<wchar_t*>(L"");
            SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
    }
    SetTimer(parent, TimerID, 50, nullptr);
    return true;
}

void MediaBar::Destroy()
{
    if (tooltip) { DestroyWindow(tooltip); tooltip = nullptr; }
    if (window) { KillTimer(parent, TimerID); DestroyWindow(window); window = nullptr; }
}

void MediaBar::Resize(int statusHeight)
{
    if (!window) return;
    RECT client; GetClientRect(parent, &client);
    SetWindowPos(window, HWND_TOP, 0, std::max(0L, client.bottom-statusHeight-Height()),
        client.right, Height(), SWP_NOACTIVATE);
    UpdateTooltips();
}

void MediaBar::Update(const std::array<MediaSlot, 3>& value)
{
    if (slots == value) return;
    slots = value;
    UpdateTooltips();
    if (window) InvalidateRect(window, nullptr, FALSE);
}

void MediaBar::UpdateTooltips()
{
    if (!tooltip) return;
    RECT rect; GetClientRect(window, &rect);
    for (UINT i=0; i<3; ++i) {
        TOOLINFOW tool = {}; tool.cbSize=sizeof(tool); tool.hwnd=window; tool.uId=i+1;
        tool.rect = {rect.right*int(i)/3, 0, rect.right*int(i+1)/3, rect.bottom};
        SendMessageW(tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
        const auto& text = slots[i].fullName.empty() ? slots[i].name : slots[i].fullName;
        tool.lpszText = const_cast<wchar_t*>(text.c_str());
        SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

LRESULT CALLBACK MediaBar::WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    auto self = reinterpret_cast<MediaBar*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<MediaBar*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_SYSCOLORCHANGE || message == WM_THEMECHANGED || message == WM_SETTINGCHANGE)
        InvalidateRect(hwnd, nullptr, FALSE);
    if (message == WM_PAINT && self) {
        PAINTSTRUCT ps; HDC target = BeginPaint(hwnd, &ps);
        RECT rect; GetClientRect(hwnd, &rect);
        if (rect.right > 0 && rect.bottom > 0) {
            HDC buffer = CreateCompatibleDC(target);
            HBITMAP bitmap = CreateCompatibleBitmap(target, rect.right, rect.bottom);
            HGDIOBJ previous = SelectObject(buffer, bitmap);
            DrawMediaBar(buffer, rect, self->slots, self->dpi);
            BitBlt(target, 0, 0, rect.right, rect.bottom, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, previous); DeleteObject(bitmap); DeleteDC(buffer);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
