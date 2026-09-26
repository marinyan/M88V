// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "third_party/ymfm/ymfm_adpcm.h"
#include <array>
#include <string>

namespace FM {
// YM2608 embedded ADPCM-A ROM playback. FM/SSG/ADPCM-B remain in fmgen.
class OPNARhythm : private ymfm::ymfm_interface {
public:
    OPNARhythm();
    bool Load(const char* directory);
    bool Available() const { return available; }
    void Reset();
    void Write(unsigned address, unsigned data);
    void Mix(int32_t* dest, unsigned samples, unsigned clock, unsigned rate,
             unsigned prescale, unsigned mask, const int* gains);
    std::vector<uint8_t> Save();
    bool Restore(const std::vector<uint8_t>& bytes);
private:
    uint8_t ymfm_external_read(ymfm::access_class, uint32_t address) override;
    ymfm::adpcm_a_engine engine;
    std::array<uint8_t,8192> rom{};
    bool available=false;
    uint32_t identity=0, half=0, phase=0, period=1;
};
}
