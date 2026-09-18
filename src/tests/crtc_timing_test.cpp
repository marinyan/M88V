// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "pc88/crtc.h"
#include "pc88/pd8257.h"
#include "pc88/pc88.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>

namespace {
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// CRTC initialization needs font bytes, but their contents cannot affect timing.
// Supply synthetic zero bytes in an isolated directory, never a proprietary ROM.
struct SyntheticFont {
    std::filesystem::path old = std::filesystem::current_path();
    std::filesystem::path dir;
    SyntheticFont() {
        dir = std::filesystem::temp_directory_path() /
            ("m88-crtc-timing-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        Check(std::filesystem::create_directory(dir), "create font directory");
        std::ofstream file(dir / "FONT.ROM", std::ios::binary);
        std::array<char, 2048> bytes{};
        file.write(bytes.data(), bytes.size());
        Check(bool(file), "write synthetic font");
        file.close();
        std::filesystem::current_path(dir);
    }
    ~SyntheticFont() {
        std::filesystem::current_path(old);
        std::filesystem::remove(dir / "FONT.ROM");
        std::filesystem::remove(dir);
    }
};

class Clock final : public Scheduler {
public:
    // Model an instruction completing after the requested CPU deadline.
    explicit Clock(int overshoot = 0) : overshoot(overshoot) {}
private:
    int overshoot;
    int Execute(int ticks) override { return ticks + overshoot; }
    void Shorten(int) override {}
    int GetTicks() override { return 0; }
};

class Probe final : public Device {
public:
    Clock& clock;
    bool lowFrequency;
    std::vector<std::pair<int, uint>> edges;
    Probe(Clock& c, bool low) : Device(0), clock(c), lowFrequency(low) {}
    uint IOCALL Monitor(uint) { return lowFrequency ? 2 : 0; }
    void IOCALL Vrtc(uint, uint value) { edges.emplace_back(clock.GetTime(), value); }
};

enum class Transfer { Normal, Disabled, Underrun, StopFirst, StopMiddle, StopLast };

void Run(unsigned rows, unsigned rasters, unsigned retrace, bool low, Transfer transfer,
         bool skipline = false, bool restorePendingEnd = false,
         unsigned frames = 4, int overshoot = 0, bool restoreVblank = false) {
    Clock clock(overshoot);
    Check(clock.Init(), "clock initialization");
    clock.Proceed(1); // Establish the scheduler's initial execution deadline.
    IOBus bus;
    Check(bus.Init(512), "bus initialization");
    Probe probe(clock, low);
    bus.ConnectIn(0x40, &probe, static_cast<Device::InFuncPtr>(&Probe::Monitor));
    bus.ConnectOut(PC88::vrtc, &probe, static_cast<Device::OutFuncPtr>(&Probe::Vrtc));
    PC8801::PD8257 dma(0);
    std::array<uint8, 0x10000> memory{};
    Check(dma.ConnectRd(memory.data(), 0, memory.size()), "DMA memory");
    // Transparent mode, 80 characters and one control attribute pair per row.
    constexpr unsigned lineBytes = 82;
    int stopRow = -1;
    if (transfer == Transfer::StopFirst) stopRow = 0;
    if (transfer == Transfer::StopMiddle) stopRow = rows / 2;
    if (transfer == Transfer::StopLast) stopRow = rows - 1;
    if (stopRow >= 0) {
        memory[stopRow * lineBytes + 80] = 0x60;
        memory[stopRow * lineBytes + 81] = 1;
    }
    dma.SetAddr(4, 0); dma.SetAddr(4, 0);
    unsigned count = transfer == Transfer::Underrun ? lineBytes / 2 - 1 : 0x3fff;
    dma.SetCount(4, count & 255); dma.SetCount(4, count >> 8);
    dma.SetMode(0, transfer == Transfer::Disabled ? 0 : 4);

    // Legacy construction leaves unrelated display fields unspecified; zero
    // storage before full public initialization, as in screen_reset_test.
    alignas(PC8801::CRTC) std::array<unsigned char, sizeof(PC8801::CRTC)> storage{};
    auto destroy = [](PC8801::CRTC* value) { value->~CRTC(); };
    std::unique_ptr<PC8801::CRTC, decltype(destroy)> crtc(
        new (storage.data()) PC8801::CRTC(0), destroy);
    Check(crtc->Init(&bus, &clock, &dma, nullptr), "CRTC initialization");
    crtc->Reset();
    crtc->Out(0x51, 0);
    for (unsigned value : {78u, rows - 1, rasters - 1 + (skipline ? 128u : 0u),
                          (retrace - 1) << 5, 0u}) crtc->Out(0x50, value);
    probe.edges.clear();
    const int start = clock.GetTime();
    crtc->Out(0x51, 0x20);
    // The scheduler unit is 10 us. Compute independently from programmed
    // geometry; do not use GetFramePeriod as the sole timing oracle.
    // Rounded 16.16 periods from 62.58 us / 40.28 us per raster.
    // Accumulate before truncating: truncating each character row introduces
    // a visible frequency error and must not become the test's own oracle.
    const uint64_t rowFixed = (low ? 410124ull : 263979ull) * rasters;
    const auto ticksAt = [rowFixed](uint64_t row) {
        return int(row * rowFixed / 65536);
    };
    const int period = ticksAt(rows + retrace);
    Check(crtc->GetFramePeriod() == period, "reported frame period");
    int elapsed = 0;
    unsigned restoredRow = 0;
    if (restorePendingEnd || restoreVblank) {
        // Save precisely when the normal last row or a stop-control row has
        // queued retrace, before its remaining row time has elapsed. Legacy
        // SaveStatus stores the event kind, not a sub-row remaining deadline.
        unsigned row = stopRow >= 0 ? unsigned(stopRow) : rows - 1;
        // At VRTC start SaveStatus encodes event=-1 as the byte 0xff.
        // Loading must decode that sentinel, not wait for 254 display rows.
        restoredRow = restoreVblank ? rows + retrace : retrace + row;
        elapsed = ticksAt(restoredRow);
        clock.Proceed(elapsed);
        std::vector<uint8> saved(crtc->GetStatusSize());
        Check(crtc->SaveStatus(saved.data()), "save CRTC timing state");
        Check(crtc->LoadStatus(saved.data()), "restore CRTC timing state");
    }
    clock.Proceed(ticksAt(uint64_t(rows + retrace) * frames) + overshoot - elapsed);
    std::string label = "rows=" + std::to_string(rows) + " rasters=" + std::to_string(rasters) +
        " low=" + std::to_string(low) + " transfer=" + std::to_string(int(transfer)) +
        " skip=" + std::to_string(skipline) + " restore=" + std::to_string(restorePendingEnd) +
        " overshoot=" + std::to_string(overshoot) + " restore-vblank=" + std::to_string(restoreVblank);
    Check(probe.edges.size() >= frames * 2, label + ": missing VRTC edges");
    const auto expected = [&](uint64_t row) {
        // Legacy status files do not contain fractional phase. Loading starts
        // a fresh phase at the restore instant while retaining pending rows.
        return start + ((restorePendingEnd || restoreVblank) && row > restoredRow ?
            elapsed + ticksAt(row - restoredRow) : ticksAt(row));
    };
    const auto checkDeadline = [&](int actual, int deadline, const std::string& edge) {
        Check(actual >= deadline && actual <= deadline + overshoot,
            label + ": " + edge + " expected " + std::to_string(deadline) +
            ".." + std::to_string(deadline + overshoot) + ", got " + std::to_string(actual));
    };
    for (unsigned frame = 0; frame < frames; ++frame) {
        const auto& begin = probe.edges[frame * 2];
        const auto& end = probe.edges[frame * 2 + 1];
        Check(begin.second == 0 && end.second == 1, label + ": VRTC edge order");
        checkDeadline(begin.first, expected(uint64_t(frame) * (rows + retrace) + retrace),
            "display start frame " + std::to_string(frame));
        checkDeadline(end.first, expected(uint64_t(frame + 1) * (rows + retrace)),
            "VRTC start frame " + std::to_string(frame));
    }
    if (transfer == Transfer::Disabled || transfer == Transfer::Underrun)
        Check((crtc->GetStatus() & 0x18) == 8, label + ": underrun was not exercised");
}
}

int main() {
    try {
        SyntheticFont font;
        for (bool low : {false, true}) {
            for (unsigned rows : {20u, 25u}) {
                unsigned rasters = low ? (rows == 20 ? 10 : 8) : (rows == 20 ? 20 : 16);
                unsigned retrace = low ? (rows == 20 ? 6 : 7) : (rows == 20 ? 2 : 3);
                for (auto transfer : {Transfer::Normal, Transfer::Disabled, Transfer::Underrun,
                                     Transfer::StopFirst, Transfer::StopMiddle, Transfer::StopLast})
                    Run(rows, rasters, retrace, low, transfer);
                Run(rows, rasters, retrace, low, Transfer::Normal, true);
                Run(rows, rasters, retrace, low, Transfer::Normal, false, true);
                Run(rows, rasters, retrace, low, Transfer::StopMiddle, false, true);
                Run(rows, rasters, retrace, low, Transfer::Normal, false, false, 4, 0, true);
            }
            Run(1, 1, 1, low, Transfer::Normal);
            Run(3, 7, 4, low, Transfer::StopMiddle);
            // Check many fractional carries and prove callback lateness does
            // not compound across rows or frames. The smallest row is still
            // longer than this modeled three-tick instruction overshoot.
            Run(1, 1, 1, low, Transfer::Normal, false, false, 1000);
            Run(1, 1, 1, low, Transfer::Normal, false, false, 1000, 3);
            Run(25, low ? 8 : 16, low ? 7 : 3, low,
                Transfer::Normal, false, false, 128, 3);
            Run(25, low ? 8 : 16, low ? 7 : 3, low,
                Transfer::StopMiddle, false, false, 128, 3);
        }
        std::cout << "CRTC active rows, retrace and frame cadence: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
