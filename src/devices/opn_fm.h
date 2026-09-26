// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace FM {
// FM synthesis only. M88's scheduler owns timers/IRQ; SSG and ADPCM are mixed separately.
class OPNFM {
public:
    struct Engine;
    explicit OPNFM(bool opna = true);
    ~OPNFM();
    void Select(bool opna);
    bool IsOPNA() const { return opna; }
    void Reset();
    void Write(unsigned address, unsigned data);
    void TimerA();
    void Mix(int32_t* dest, unsigned samples, unsigned externalClock,
             unsigned rate, unsigned prescale, unsigned mask, int gain,
             unsigned enabledChannels = 63);
    std::vector<uint8_t> Save();
    bool Restore(const std::vector<uint8_t>& state);
private:
    std::unique_ptr<Engine> engine;
    bool opna;
    uint32_t phase = 0, period = 1;
    int32_t last[6][2]{};
};
}
