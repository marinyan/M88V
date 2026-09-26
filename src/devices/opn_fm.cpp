// SPDX-License-Identifier: BSD-2-Clause
#include "opn_fm.h"
#include "third_party/ymfm/ymfm_opn.h"
#include <algorithm>
#include <limits>

namespace FM {
struct OPNFM::Engine {
    virtual ~Engine() = default;
    virtual void Write(unsigned address, unsigned data) = 0;
    virtual void TimerA() = 0;
    virtual void Clock(unsigned prescale, unsigned enabled, int32_t (&last)[6][2]) = 0;
    virtual std::vector<uint8_t> Save() = 0;
    virtual bool Restore(const std::vector<uint8_t>& bytes) = 0;
};
namespace {
template<bool OPNA>
class EngineImpl final : public OPNFM::Engine, private ymfm::ymfm_interface {
    using Registers = ymfm::opn_registers_base<OPNA>;
    ymfm::fm_engine_base<Registers> fm{*this};
public:
    EngineImpl() { fm.reset(); }
    void Write(unsigned address, unsigned data) override {
        // Filter addresses absent from the selected physical chip, not just its output channels.
        if(address >= Registers::REGISTERS || address < 0x20) return;
        if(!OPNA && (address == 0x22 || address == 0x29 ||
                    (address == 0x28 && (data & 4)))) return;
        fm.write(uint16_t(address), uint8_t(data));
    }
    void TimerA() override { fm.engine_timer_expired(0); }
    void Clock(unsigned prescale, unsigned enabled, int32_t (&last)[6][2]) override {
        fm.set_clock_prescale(prescale);
        fm.clock(Registers::ALL_CHANNELS);
        for(unsigned ch = 0; ch < Registers::CHANNELS; ++ch) {
            typename ymfm::fm_engine_base<Registers>::output_data value;
            // Output also updates operator feedback. Always do it for user-muted channels.
            fm.output(value.clear(), OPNA ? 1 : 0, 32767, enabled & (1u << ch));
            last[ch][0] = value.data[0];
            last[ch][1] = value.data[OPNA ? 1 : 0];
        }
    }
    std::vector<uint8_t> Save() override {
        std::vector<uint8_t> bytes;
        ymfm::ymfm_saved_state state(bytes, true); fm.save_restore(state);
        return bytes;
    }
    bool Restore(const std::vector<uint8_t>& bytes) override {
        // Layout of the pinned ymfm engine: header, registers, feedback, operators.
        if(bytes.size() != Save().size()) return false;
        if((bytes[5] != 2 && bytes[5] != 3 && bytes[5] != 6) ||
           bytes[7] > 1 || bytes[8] > 1 || bytes[9] > 1) return false;
        const size_t operators = 11 + (OPNA ? 5 : 0) + Registers::REGISTERS + Registers::CHANNELS * 6;
        for(unsigned i = 0; i < Registers::OPERATORS; ++i) {
            const size_t at = operators + i * 10;
            const unsigned attenuation = bytes[at+4] | (bytes[at+5] << 8);
            if(attenuation > 1023 || bytes[at+6] < ymfm::EG_ATTACK ||
               bytes[at+6] > ymfm::EG_RELEASE || bytes[at+7] > 1 ||
               bytes[at+8] > 1 || bytes[at+9] > 7) return false;
        }
        auto copy = bytes;
        ymfm::ymfm_saved_state state(copy, false); fm.save_restore(state);
        return true;
    }
    // Timer status and IRQ are owned by FM::Timer, driven by emulated CPU time.
    // Only Timer A's CSM pulse is forwarded here; no second host timer is scheduled.
    void ymfm_set_timer(uint32_t, int32_t) override {}
};
std::unique_ptr<OPNFM::Engine> MakeEngine(bool opna) {
    if(opna) return std::make_unique<EngineImpl<true>>();
    return std::make_unique<EngineImpl<false>>();
}
void Put(std::vector<uint8_t>& bytes, uint32_t value) {
    for(unsigned i=0; i<4; ++i) bytes.push_back(uint8_t(value >> (i*8)));
}
uint32_t Get(const std::vector<uint8_t>& bytes, size_t at) {
    return uint32_t(bytes[at]) | (uint32_t(bytes[at+1]) << 8) |
           (uint32_t(bytes[at+2]) << 16) | (uint32_t(bytes[at+3]) << 24);
}
}
OPNFM::OPNFM(bool type) : engine(MakeEngine(type)), opna(type) {}
OPNFM::~OPNFM() = default;
void OPNFM::Select(bool type) { if(opna != type) { opna = type; Reset(); } }
void OPNFM::Reset() {
    engine = MakeEngine(opna); phase = 0; period = 1;
    for(auto& channel : last) channel[0] = channel[1] = 0;
}
void OPNFM::Write(unsigned address, unsigned data) { engine->Write(address, data); }
void OPNFM::TimerA() { engine->TimerA(); }
void OPNFM::Mix(int32_t* dest, unsigned samples, unsigned externalClock,
                unsigned rate, unsigned prescale, unsigned mask, int gain, unsigned enabledChannels) {
    if(!rate || !externalClock || prescale > 2) return;
    static constexpr unsigned scales[] = {6, 3, 2};
    const uint64_t next = uint64_t(rate) * scales[prescale] * (opna ? 24 : 12);
    if(next > UINT32_MAX) return;
    if(period != next) { phase = uint32_t(uint64_t(phase) * next / period); period = uint32_t(next); }
    for(unsigned s=0; s<samples; ++s, dest+=2) {
        uint64_t remaining = externalClock;
        int64_t accum[2]{};
        while(remaining) {
            if(!phase) engine->Clock(scales[prescale], enabledChannels, last);
            const uint32_t span = uint32_t(std::min<uint64_t>(remaining, period-phase));
            for(unsigned side=0; side<2; ++side) {
                int32_t value = 0;
                for(unsigned ch=0; ch<(opna ? 6u : 3u); ++ch)
                    if(!(mask & (1u << ch))) value += last[ch][side];
                // Match the chip-specific DAC path in upstream ym2203/ym2608.
                value = opna ? std::clamp(value, -32768, 32767) : ymfm::roundtrip_fp(value);
                accum[side] += int64_t(value) * span;
            }
            remaining -= span; phase += span;
            if(phase == period) phase = 0;
        }
        for(unsigned side=0; side<2; ++side) {
            const int64_t value = int64_t(dest[side]) + (accum[side] / externalClock) * gain / 16384;
            dest[side] = int32_t(std::clamp<int64_t>(value, INT32_MIN, INT32_MAX));
        }
    }
}
std::vector<uint8_t> OPNFM::Save() {
    std::vector<uint8_t> bytes;
    Put(bytes, opna); Put(bytes, phase); Put(bytes, period);
    for(auto& channel : last) for(auto value : channel) Put(bytes, uint32_t(value));
    const auto core = engine->Save(); bytes.insert(bytes.end(), core.begin(), core.end());
    return bytes;
}
bool OPNFM::Restore(const std::vector<uint8_t>& bytes) {
    if(bytes.size() != Save().size() || Get(bytes, 0) != unsigned(opna) ||
       !Get(bytes, 8) || Get(bytes, 4) >= Get(bytes, 8)) return false;
    for(size_t at=12; at<60; at+=4) {
        const auto value = int32_t(Get(bytes, at));
        if(value < -32768 || value > 32767) return false;
    }
    auto restored = MakeEngine(opna);
    if(!restored->Restore({bytes.begin()+60, bytes.end()})) return false;
    engine = std::move(restored); phase = Get(bytes, 4); period = Get(bytes, 8);
    for(unsigned ch=0; ch<6; ++ch) for(unsigned side=0; side<2; ++side)
        last[ch][side] = int32_t(Get(bytes, 12 + ch*8 + side*4));
    return true;
}
}
