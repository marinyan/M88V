// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "opna.h"
#include "third_party/ymfm/ymfm_opn.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool ok, const char* why) { if(!ok) throw std::runtime_error(why); }
struct Interface : ymfm::ymfm_interface { void TimerA() { m_engine->engine_timer_expired(0); } };
template<class Write>
void Voice(Write write, unsigned ch, unsigned algorithm=4, unsigned envelope=0) {
    const unsigned address = ch%3 + (ch>=3 ? 0x100 : 0);
    for(unsigned slot=0; slot<4; ++slot) {
        const unsigned offset = address + slot*4;
        write(0x30+offset, 1+slot); write(0x40+offset, 16+slot*3);
        write(0x50+offset, 31); write(0x60+offset, 0x87);
        write(0x70+offset, 3); write(0x80+offset, 0x4f);
        write(0x90+offset, envelope);
    }
    write(0xb0+address, 0x28|algorithm); write(0xb4+address, 0xf7);
    write(0xa4+address, 0x22); write(0xa0+address, 0x69+ch*11);
    write(0x28, 0xf0 | (ch%3) | (ch>=3 ? 4 : 0));
}
std::vector<int32_t> Render(FM::OPNA& op, unsigned samples, unsigned chunk=0) {
    std::vector<int32_t> result(samples*2);
    if(!chunk) chunk=samples;
    for(unsigned i=0; i<samples; i+=chunk) op.Mix(result.data()+i*2, std::min(chunk,samples-i));
    return result;
}
bool Audible(const std::vector<int32_t>& samples) {
    return std::any_of(samples.begin(),samples.end(),[](int32_t value){return value!=0;});
}
template<class Chip, bool OPNA>
void Reference() {
    // Compare against the complete upstream chip, including its DAC conversion,
    // at integral native rates so no independent resampling convention is involved.
    for(unsigned scale=0; scale<3; ++scale) for(unsigned algorithm=0; algorithm<8; ++algorithm)
    for(unsigned envelope : {0u, 8u, 10u, 12u, 14u}) {
        static constexpr unsigned prescales[]={6,3,2};
        Interface intf; Chip reference(intf); reference.reset();
        FM::OPNA actual;
        const unsigned rate=300000/prescales[scale];
        Check(actual.Init(7200000,rate),"initialize FM");
        actual.SetFMChip(OPNA); actual.Reset();
        const auto write=[&](unsigned reg,unsigned value) {
            actual.SetReg(reg,value);
            reference.write(reg>=0x100 ? 2 : 0,uint8_t(reg));
            reference.write(reg>=0x100 ? 3 : 1,uint8_t(value));
        };
        write(0x2d+scale,0);
        if(OPNA) { write(0x29,0x9f); write(0x22,0x0c); }
        for(unsigned ch=0; ch<(OPNA ? 6u : 3u); ++ch) Voice(write,ch,algorithm,envelope);
        // Exercise channel 3's independent operator frequencies.
        write(0xac,0x23); write(0xa8,0x71);
        write(0xad,0x20); write(0xa9,0x41);
        write(0xae,0x21); write(0xaa,0x81);
        write(0x27,0x40);
        for(unsigned s=0;s<512;++s) {
            if(OPNA && s==100) write(0x29,0x1f);
            if(OPNA && s==200) write(0x29,0x9f);
            if(s==250) write(0x28,0);
            std::array<int32_t,2> sample{}; actual.Mix(sample.data(),1);
            typename Chip::output_data upstream[18];
            reference.generate(upstream,prescales[scale]*3);
            Check(sample[0]==upstream[0].data[0] && sample[1]==upstream[0].data[OPNA?1:0],
                  OPNA ? "YM2608 FM differs from upstream chip" : "YM2203 FM differs from upstream chip");
        }
    }
}
void StateAndMask(bool opna, unsigned rate) {
    FM::OPNA op; Check(op.Init(7987200,rate),"init state fixture");
    op.SetFMChip(opna); op.Reset();
    op.SetReg(0x29,0x9f); op.SetReg(0x22,0x0f);
    Voice([&](unsigned r,unsigned v){op.SetReg(r,v);},0,5,10);
    Render(op,173);
    const auto state=op.SaveFMState();
    const auto expected=Render(op,1024);
    Check(Audible(expected),"FM fixture silent");
    Check(op.RestoreFMState(state),"restore FM state");
    Check(Render(op,1024,7)==expected,"FM chunk/save-restore continuity");
    Check(op.RestoreFMState(state),"restore before mute");
    op.SetChannelMask(1); Check(!Audible(Render(op,1024)),"channel mute");
    op.SetChannelMask(0); const auto afterMute=Render(op,256);
    Check(op.RestoreFMState(state),"restore before reference");
    Render(op,1024); Check(Render(op,256)==afterMute,"muting stopped FM feedback or envelopes");
    Check(op.RestoreFMState(state),"restore before volume mute");
    op.SetVolumeFM(-192); Check(!Audible(Render(op,1024)),"FM volume mute");
    op.SetVolumeFM(0);Check(Render(op,256)==afterMute,"volume mute stopped the engine");
    auto bad=state;bad[0]^=1;
    const auto before=op.SaveFMState();
    Check(!op.RestoreFMState(bad) && before==op.SaveFMState(),"reject foreign chip state atomically");
    bad=state;bad.resize(bad.size()-1);Check(!op.RestoreFMState(bad),"reject truncated state");
    bad=state;bad[65]=0;Check(!op.RestoreFMState(bad),"reject invalid engine prescale");
    // Operator envelope state is validated before it can index ymfm's tables.
    bad=state;const size_t operatorStart=60+11+(opna?5:0)+(opna?512:256)+(opna?6:3)*6;
    bad[operatorStart+6]=255;Check(!op.RestoreFMState(bad),"reject invalid envelope state");
}
void ChipDifferences() {
    FM::OPNA op;Check(op.Init(7987200,44100),"init differences");
    auto write=[&](unsigned r,unsigned v){op.SetReg(r,v);};
    op.SetFMChip(false);op.Reset();Voice(write,3);
    Check(!Audible(Render(op,1024)),"YM2203 accepted channel 4");
    Voice(write,0);write(0xb4,0x80);
    auto mono=Render(op,256);Check(Audible(mono),"OPN voice silent");
    for(size_t i=0;i<mono.size();i+=2)Check(mono[i]==mono[i+1],"YM2203 is not mono");
    op.SetFMChip(true);op.Reset();Voice(write,3);write(0x1b4,0x80);
    Check(!Audible(Render(op,256)),"OPNA channel 4 escaped 3-channel mode");
    write(0x29,0x9f);auto stereo=Render(op,256);Check(Audible(stereo),"OPNA channel 4 silent");
    for(size_t i=1;i<stereo.size();i+=2)Check(stereo[i]==0,"OPNA left pan leaked right");
    for(bool type:{false,true}) {
        op.SetFMChip(type);op.Reset();Voice(write,2);write(0x28,2);
        Render(op,50000);Check(!Audible(Render(op,128)),"release failed");
        write(0x24,255);write(0x25,3);write(0x27,0x85);
        op.Count(op.GetNextEvent());
        Check((op.ReadStatus()&1)!=0,"timer A status");
        Check(Audible(Render(op,128)),"CPU timer did not trigger CSM");
        write(0x27,0x10);Check((op.ReadStatus()&1)==0,"timer A reset");
    }
}
}
int main() {
    try {
        Reference<ymfm::ym2203,false>(); Reference<ymfm::ym2608,true>();
        for(bool type:{false,true})for(unsigned rate:{8000u,44100u,48000u,96000u})StateAndMask(type,rate);
        ChipDifferences();
        for(bool type:{false,true}) {
            FM::OPNA op;Check(op.Init(7987200,55467),"benchmark init");op.SetFMChip(type);op.Reset();op.SetReg(0x29,0x9f);
            for(unsigned ch=0;ch<(type?6u:3u);++ch)Voice([&](unsigned r,unsigned v){op.SetReg(r,v);},ch);
            const auto start=std::chrono::steady_clock::now();Render(op,554670);
            std::cout<<(type?"YM2608":"YM2203")<<" 10s synthesis: "
                     <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
        }
        std::cout<<"FM chip reference, modes, rates, pan, mute, CSM and state tests passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
