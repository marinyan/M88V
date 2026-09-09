// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "headers.h"
#include "pc88/screen.h"
#include "pc88/crtc.h"
#include <array>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
class PaletteDraw final : public Draw {
public:
    std::array<Palette, 256> colors{};
    bool Init(uint, uint, uint) override { return true; }
    bool Cleanup() override { return true; }
    bool Lock(uint8**, int*) override { return false; }
    bool Unlock() override { return true; }
    uint GetStatus() override { return 0; }
    void Resize(uint, uint) override {}
    void DrawScreen(const Region&) override {}
    bool SetFlipMode(bool) override { return true; }
    void SetPalette(uint index, uint count, const Palette* palette) override {
        std::copy(palette, palette + count, colors.begin() + index);
    }
};

void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// No ROM is required: SetTextMode only updates CRTC's pixel masks. Zero its
// storage before construction because this test does not initialize the CRTC.
struct Fixture {
    alignas(PC8801::CRTC) std::array<unsigned char, sizeof(PC8801::CRTC)> crtStorage{};
    alignas(PC8801::Screen) std::array<unsigned char, sizeof(PC8801::Screen)> screenStorage;
    PC8801::CRTC* crtc;
    PC8801::Screen* screen;
    PaletteDraw draw;
    explicit Fixture(unsigned char fill) {
        crtc = new (crtStorage.data()) PC8801::CRTC(0);
        screenStorage.fill(fill);
        screen = new (screenStorage.data()) PC8801::Screen(0);
        screen->Init(nullptr, nullptr, crtc);
    }
    ~Fixture() { screen->~Screen(); crtc->~CRTC(); }
    void Reset(PC8801::Config::BASICMode mode) {
        PC8801::Config config{};
        config.basicmode = mode;
        screen->ApplyConfig(&config);
        screen->Reset();
        screen->Out31(0x31, (mode & 2) ? 4 : 0x11); // Color, 640x200, graphics off.
        screen->Out32(0x32, 0x80);
        screen->Out33(0x33, mode == PC8801::Config::N80V2 ? 0x82 : 2);
        screen->Out53(0x53, 0); // Ignored in N802, as on the legacy core.
    }
    bool WhiteText() {
        screen->UpdatePalette(&draw);
        const auto& white = draw.colors[0x4f]; // Black graphics + white text.
        return white.red == 255 && white.green == 255 && white.blue == 255;
    }
};
}

int main() {
    try {
        for (unsigned char fill : {0x00, 0x01, 0xa5, 0xff}) {
            Fixture f(fill);
            f.Reset(PC8801::Config::N802);
            Check(f.WhiteText(), "N802 text must be visible regardless of initial heap contents");
            std::cout << "screen initial heap fill " << unsigned(fill) << ": PASS\n";
        }
        for (unsigned char fill : {0x00, 0x01, 0xa5, 0xff}) {
            Fixture f(fill);
            for (auto mode : {PC8801::Config::N80, PC8801::Config::N88V1,
                    PC8801::Config::N88V1H, PC8801::Config::N88V2, PC8801::Config::N80V2}) {
                f.Reset(mode);
                Check(f.WhiteText(), "Text must be visible after mode reset");
                f.screen->Out53(0x53, 1);
                Check(!f.WhiteText(), "Port 53 must still hide text in supported modes");
                f.Reset(PC8801::Config::N802);
                Check(f.WhiteText(), "N802 must not inherit the previous mode's text-disable latch");
            }
            std::cout << "screen reset heap fill " << unsigned(fill) << ": PASS\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
