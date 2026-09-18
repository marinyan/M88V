// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "pc88/base.h"
#include "pc88/pc88.h"
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Exercise the real Base interrupt path without requiring BIOS or peripherals.
class Clock final : public PC88 {
public:
    using PC88::bus1;
    int overshoot = 0;
private:
    int Execute(int ticks) override { return ticks + (ticks ? overshoot : 0); }
    void Shorten(int) override {}
    int GetTicks() override { return 0; }
};

class Probe final : public Device {
public:
    Clock& clock;
    std::vector<int> edges;
    explicit Probe(Clock& c) : Device(0), clock(c) {}
    void IOCALL Interrupt(uint, uint value) {
        if (value) edges.push_back(clock.GetTime());
    }
};

void Run(int overshoot) {
    Clock clock;
    Check(clock.Scheduler::Init(), "scheduler initialization");
    clock.Scheduler::Proceed(1);
    Check(clock.bus1.Init(PC88::portend), "bus initialization");
    Probe probe(clock);
    clock.bus1.ConnectOut(PC88::pint2, &probe,
        static_cast<Device::OutFuncPtr>(&Probe::Interrupt));
    PC8801::Base base(0);
    Check(base.Init(&clock), "base initialization");
    Check(probe.edges.size() == 1, "initial interrupt retained");
    probe.edges.clear();
    const int start = clock.GetTime();
    clock.overshoot = overshoot;
    // 100 emulated seconds: every individual edge must remain within one
    // instruction overshoot of its rational deadline, not just average 600 Hz.
    clock.Scheduler::Proceed(10000000);
    Check(probe.edges.size() == 60000, "60000 interrupts in 100 seconds");
    for (size_t i = 0; i < probe.edges.size(); ++i) {
        const int ideal = start + static_cast<int>((i + 1) * 100000 / 600);
        const int lateness = probe.edges[i] - ideal;
        Check(lateness >= 0 && lateness <= overshoot, "RTC phase drift");
    }

    // Reset while an event is pending must replace it and restart the phase.
    clock.overshoot = 0;
    clock.Scheduler::Proceed(73);
    base.ResetRTC();
    base.ResetRTC();
    probe.edges.clear();
    const int resetTime = clock.GetTime();
    clock.Scheduler::Proceed(100000);
    Check(probe.edges.size() == 600, "reset leaves exactly one timer");
    Check(probe.edges.front() == resetTime + 166, "reset restarts RTC phase");
    Check(probe.edges.back() == resetTime + 100000, "reset keeps exact frequency");

    // A pathological execution stall cannot schedule zero/negative delays or
    // generate a catch-up interrupt storm; normal operation must recover.
    clock.overshoot = 500;
    probe.edges.clear();
    clock.Scheduler::Proceed(1);
    Check(probe.edges.size() == 1, "stall emits only the due interrupt");
    clock.overshoot = 0;
    const int recovery = clock.GetTime();
    probe.edges.clear();
    clock.Scheduler::Proceed(100000);
    Check(probe.edges.size() == 600, "RTC recovers from a long stall");
    Check(probe.edges.front() > recovery, "recovery deadline is in the future");
}
}

int main() {
    try {
        Run(0);
        Run(1);
        Run(7);
        std::cout << "600 Hz: exact rational timing, instruction overshoot, reset and stall recovery passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
