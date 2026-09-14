// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#pragma once
#include <windows.h>
#include <array>
#include <string>

struct MediaSlot {
    std::wstring name = L"Empty";
    std::wstring detail = L"NO MEDIA";
    bool mounted = false;
    int lamp = 0; // 0: idle, 1: disk/read, 2: cassette recording
    std::wstring fullName;
    bool operator==(const MediaSlot& other) const {
        return name == other.name && detail == other.detail && mounted == other.mounted && lamp == other.lamp && fullName == other.fullName;
    }
};

// Shared by the live window and the ROM-free visual preview.
void DrawMediaBar(HDC dc, RECT bounds, const std::array<MediaSlot, 3>& slots, UINT dpi);

class MediaBar {
public:
    static constexpr UINT TimerID = 10;
    static constexpr UINT MenuMessage = WM_APP + 0x310;
    bool Create(HWND parent);
    void Destroy();
    void Resize(int statusHeight);
    void Update(const std::array<MediaSlot, 3>& slots);
    int Height() const { return window ? MulDiv(58, dpi, 96) : 0; }
    bool IsOpen() const { return window != nullptr; }
private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    void UpdateTooltips();
    int HitTest(POINT point) const;
    int pressedSlot = -1;
    HWND window = nullptr, parent = nullptr;
    HWND tooltip = nullptr;
    UINT dpi = 96;
    std::array<MediaSlot, 3> slots;
};
