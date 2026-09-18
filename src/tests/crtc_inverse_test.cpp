// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "pc88/crtc.h"
#include "pc88/pd8257.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Font {
    std::filesystem::path old = std::filesystem::current_path();
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("m88-crtc-inverse-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Font() {
        Check(std::filesystem::create_directory(dir), "temporary directory");
        std::array<char, 2048> data{};
        for (int i = 8; i < 16; ++i) data[i] = char(0x80);
        std::ofstream f(dir / "FONT.ROM", std::ios::binary);
        f.write(data.data(), data.size());
        Check(bool(f), "synthetic font");
        f.close();
        std::filesystem::current_path(dir);
    }
    ~Font() {
        std::filesystem::current_path(old);
        std::filesystem::remove(dir / "FONT.ROM");
        std::filesystem::remove(dir);
    }
};
class Clock final : public Scheduler {
    int Execute(int ticks) override { return ticks; }
    void Shorten(int) override {}
    int GetTicks() override { return 0; }
};
struct Fixture {
    Clock clock;
    IOBus bus;
    PC8801::PD8257 dma{0};
    std::array<uint8, 65536> memory{};
    std::array<uint8, 640 * 400> image{};
    alignas(PC8801::CRTC) std::array<unsigned char, sizeof(PC8801::CRTC)> storage{};
    PC8801::CRTC* crtc;
    unsigned format;
    Fixture(unsigned f, bool wide, bool color) : format(f) {
        Check(clock.Init() && bus.Init(512), "initialize clock/bus");
        clock.Proceed(1);
        Check(dma.ConnectRd(memory.data(), 0, memory.size()), "DMA memory");
        crtc = new (storage.data()) PC8801::CRTC(0);
        Check(crtc->Init(&bus, &clock, &dma, nullptr), "initialize CRTC");
        crtc->Reset();
        crtc->Out(0x51, 0);
        for (unsigned v : {78u, 24u, 15u, 0u, format << 5}) crtc->Out(0x50, v);
        crtc->Out(0x51, 0x80); // Disable cursor.
        crtc->Out(0x50, 0); crtc->Out(0x50, 0);
        crtc->SetTextSize(wide);
        crtc->SetTextMode(color);
        Fill(false);
    }
    ~Fixture() { crtc->~CRTC(); }
    void Fill(bool local, bool inherit = false) {
        memory.fill(1);
        if (format != 1) {
            for (unsigned row = 0; row < 25; ++row) {
                memory[row * 82 + 80] = inherit ? 127 : 0;
                memory[row * 82 + 81] = local ? 4 : 0;
            }
        }
    }
    void Frame(bool global, bool startCommand = true) {
        dma.SetMode(0, 0);
        dma.SetAddr(4, 0); dma.SetAddr(4, 0);
        dma.SetCount(4, 0xff); dma.SetCount(4, 0x3f);
        dma.SetMode(0, 4);
        if (startCommand) crtc->Out(0x51, global ? 0x21 : 0x20);
        clock.Proceed(crtc->GetFramePeriod());
        Draw::Region r;
        crtc->UpdateScreen(image.data(), 640, r, false);
        crtc->UpdateScreen(image.data(), 640, r, false);
    }
};

void Run(unsigned format, bool wide, bool color, bool local) {
    Fixture f(format, wide, color);
    f.Fill(local);
    f.Frame(false);
    auto normal = f.image;
    const uint8 mask = color ? 8 : 16;
    for (unsigned x = 0; x < 640; ++x) {
        const bool foreground = (x % (wide ? 16 : 8)) < (wide ? 2u : 1u);
        const uint8 expected = uint8((7 | (foreground ? 8 : 0)) ^ (local ? mask : 0));
        Check(normal[x] == expected, "local reverse attribute must render known synthetic glyph");
    }
    f.Frame(true);
    for (unsigned x = 0; x < 640; ++x)
        Check((normal[x] ^ f.image[x]) == mask, "START DISPLAY inverse must toggle every pixel");
    f.Frame(false);
    for (unsigned x = 0; x < 640; ++x)
        Check(normal[x] == f.image[x], "inverse off must restore pixels without forced refresh");

    if (format != 1) {
        f.Frame(true);
        std::vector<uint8> state(f.crtc->GetStatusSize());
        Check(f.crtc->SaveStatus(state.data()), "save legacy state");
        // Stable rev-2 legacy byte layout: attr at offset 18, mode at 15.
        Check(state.size() == 21 && state[0] == 2, "legacy layout unchanged");
        Check((state[18] & 1) == unsigned(!local), "legacy attr stores folded global inverse");
        f.Fill(local, true); // No attribute before column 127: inherit saved latch.
        for (uint8 revision : {1, 2}) {
            state[0] = revision;
            Check(f.crtc->LoadStatus(state.data()), "load legacy state");
            f.Frame(true, false); // Resume the pending display event from the state.
            for (unsigned x = 0; x < 640; ++x)
                Check((normal[x] ^ f.image[x]) == mask, "restored legacy inverse applied exactly once");
        }
    }
}
}
int main() {
    try {
        Font font;
        for (unsigned format : {1u, 0u, 2u})
            for (bool wide : {false, true})
                for (bool color : {false, true})
                    for (bool local : {false, true}) {
                        if (format == 1 && local) continue;
                        Run(format, wide, color, local);
                    }
        std::cout << "CRTC inverse: 20 mode combinations and legacy restoration PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
