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
            PC8801::Config config{};
            config.basicmode = mode;
            config.keytype = PC8801::Config::AT106;
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
        }
        std::cout << "Native keyboard C press/release and snapshot state: PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
