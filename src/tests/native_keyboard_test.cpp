// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "headers.h"
#include "WinKeyIF.h"
#include "pc88/config.h"
#include <iostream>
#include <stdexcept>

static void Check(bool condition) {
    if (!condition) throw std::runtime_error("Native keyboard matrix/state mismatch");
}

int main() {
    try {
        for (auto mode : {PC8801::Config::N802, PC8801::Config::N80V2, PC8801::Config::N88V2}) {
        for (auto layout : {PC8801::Config::AT106, PC8801::Config::AT101}) {
            PC8801::Config config{};
            config.basicmode = mode;
            config.keytype = layout;
            PC8801::WinKeyIF keyboard;
            Check(keyboard.Init(nullptr));
            keyboard.ApplyConfig(&config);
            keyboard.Reset();
            keyboard.Activate(true);
            keyboard.VSync(0, 1);
            Check((keyboard.In(2) & 8) != 0);
            keyboard.KeyDown('C', 0);
            keyboard.VSync(0, 1);
            Check((keyboard.In(2) & 8) == 0);
            const auto pressed = keyboard.CaptureDevelopmentState();
            keyboard.KeyUp('C', 0);
            keyboard.VSync(0, 1);
            Check((keyboard.In(2) & 8) != 0);
            keyboard.RestoreDevelopmentState(pressed);
            Check((keyboard.In(2) & 8) == 0);
            keyboard.RestoreDevelopmentState({0});
            Check(keyboard.CaptureDevelopmentState() == pressed);

            keyboard.Activate(true);
            keyboard.EnableRawShift(true);
            auto raw = [&](USHORT scan, USHORT flags = 0) {
                RAWKEYBOARD key{};
                key.VKey = VK_SHIFT; key.MakeCode = scan; key.Flags = flags;
                keyboard.RawKeyboard(key);
            };
            auto shifted = [&]() {
                keyboard.VSync(0, 1);
                return (keyboard.In(8) & 0x40) == 0;
            };
            const UINT aliases[] = {VK_INSERT, VK_END, VK_DOWN, VK_NEXT, VK_LEFT,
                                    VK_CLEAR, VK_RIGHT, VK_HOME, VK_UP, VK_PRIOR};
            const USHORT scans[] = {0x52,0x4f,0x50,0x51,0x4b,0x4c,0x4d,0x47,0x48,0x49};
            for (USHORT side : {USHORT(0x2a), USHORT(0x36)}) {
                for (int pad = 0; pad < 10; ++pad) {
                    for (bool numlock : {false, true}) {
                        keyboard.Activate(true);
                        raw(side);
                        Check(shifted());
                        // NumLock override: synthetic legacy and raw SHIFT release.
                        keyboard.KeyUp(VK_SHIFT, UINT(side) << 16);
                        raw(scans[pad], RI_KEY_BREAK);
                        const UINT vk = numlock ? aliases[pad] : VK_NUMPAD0 + pad;
                        keyboard.KeyDown(vk, UINT(scans[pad]) << 16);
                        Check(shifted());
                        const int row = pad < 8 ? 0 : 1;
                        const int bit = pad < 8 ? pad : pad - 8;
                        Check((keyboard.In(row) & (1 << bit)) == 0);
                        // Release may use the other alias after NumLock/Shift changes.
                        keyboard.KeyUp(numlock ? VK_NUMPAD0 + pad : aliases[pad], 0);
                        keyboard.VSync(0, 1);
                        Check((keyboard.In(row) & (1 << bit)) != 0);
                        raw(side, RI_KEY_BREAK);
                        Check(!shifted());
                        keyboard.KeyDown(VK_SHIFT, UINT(side) << 16);
                        raw(scans[pad]); // synthetic restore must not stick SHIFT
                        Check(!shifted());
                    }
                }
            }
            raw(0x2a); raw(0x36); raw(0x2a, RI_KEY_BREAK);
            Check(shifted());
            const auto rightHeld = keyboard.CaptureDevelopmentState();
            raw(0x36, RI_KEY_BREAK);
            Check(!shifted());
            keyboard.RestoreDevelopmentState(rightHeld);
            Check(shifted());
            raw(0x36, RI_KEY_BREAK);
            Check(!shifted());
            raw(0x2a, RI_KEY_E0); // PrintScreen prefix
            Check(!shifted());
            raw(0x2a);
            keyboard.Activate(false);
            raw(0x36);
            keyboard.Activate(true);
            Check(!shifted());
            // Dedicated extended right arrow must not become numeric keypad 6.
            keyboard.KeyDown(VK_RIGHT, 1u << 24);
            keyboard.VSync(0, 1);
            Check((keyboard.In(0) & 0x40) != 0);
            keyboard.KeyUp(VK_RIGHT, 1u << 24);
            keyboard.EnableRawShift(false); // registration failure fallback
            keyboard.KeyDown(VK_SHIFT, 0);
            Check(shifted());
            keyboard.KeyUp(VK_SHIFT, 0);
            Check(!shifted());
        }
        }
        std::cout << "Native keyboard: letters, keypad/SHIFT, focus and snapshot state: PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
