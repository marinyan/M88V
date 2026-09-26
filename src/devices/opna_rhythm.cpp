// SPDX-License-Identifier: BSD-2-Clause
#include "opna_rhythm.h"
#include "zlib/zlib.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <limits>

namespace FM {
namespace {
constexpr uint16_t starts[]={0,0x1c0,0x440,0x1b80,0x1d00,0x1f80};
constexpr uint16_t ends[]={0x1bf,0x43f,0x1b7f,0x1cff,0x1f7f,0x1fff};
void Put(std::vector<uint8_t>& b,uint32_t v) {
    for(int i=0;i<4;++i)b.push_back(uint8_t(v>>(8*i)));
}
uint32_t Get(const std::vector<uint8_t>& b,size_t at) {
    return uint32_t(b[at])|(uint32_t(b[at+1])<<8)|(uint32_t(b[at+2])<<16)|(uint32_t(b[at+3])<<24);
}
}
OPNARhythm::OPNARhythm():engine(*this,0) { Reset(); }
bool OPNARhythm::Load(const char* directory) {
    available=false; identity=0; Reset();
    std::ifstream file(std::filesystem::u8path(directory?directory:"")/"ym2608_adpcm_rom.bin",std::ios::binary|std::ios::ate);
    if(!file||file.tellg()!=8192)return false;
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(rom.data()),rom.size()))return false;
    identity=uint32_t(crc32(0,rom.data(),uInt(rom.size())));
    available=true; return true;
}
uint8_t OPNARhythm::ymfm_external_read(ymfm::access_class,uint32_t address) {
    return rom[address & 8191];
}
void OPNARhythm::Reset() {
    engine.reset(); phase=half=0; period=1;
    for(int i=0;i<6;++i)engine.set_start_end(i,starts[i],ends[i]);
}
void OPNARhythm::Write(unsigned address,unsigned data) {
    if(address>=0x10&&address<=0x1d)engine.write(address&15,uint8_t(data));
}
void OPNARhythm::Mix(int32_t* dest,unsigned samples,unsigned clock,unsigned rate,
                     unsigned prescale,unsigned mask,const int* gains) {
    if(!available||!rate||!clock)return;
    // ymfm: one ADPCM-A nibble per three FM clocks; TOM/RIM at half rate.
    static constexpr unsigned divisors[]={432,216,144};
    const uint64_t next=uint64_t(rate)*divisors[std::min(prescale,2u)];
    if(next>std::numeric_limits<uint32_t>::max())return;
    if(period!=next) {phase=uint32_t(uint64_t(phase)*next/period);period=uint32_t(next);}
    for(unsigned s=0;s<samples;++s) {
        uint64_t left=clock; int64_t accum[2]={};
        while(left) {
            const uint32_t span=uint32_t(std::min<uint64_t>(left,period-phase));
            int64_t value[2]={};
            for(unsigned ch=0;ch<6;++ch)if(!(mask&(1u<<ch))) {
                ymfm::ymfm_output<2> output; output.clear();
                engine.output(output,1u<<ch);
                for(int side=0;side<2;++side)value[side]+=int64_t(output.data[side])*gains[ch]/65536;
            }
            for(int side=0;side<2;++side)accum[side]+=value[side]*span;
            left-=span; phase+=span;
            if(phase==period) {phase=0;half^=1;engine.clock(half?0x0f:0x3f);}
        }
        for(int side=0;side<2;++side) {
            const int64_t sum=int64_t(dest[side])+accum[side]/clock;
            dest[side]=int32_t(std::clamp<int64_t>(sum,INT32_MIN,INT32_MAX));
        }
        dest+=2;
    }
}
std::vector<uint8_t> OPNARhythm::Save() {
    std::vector<uint8_t> b; ymfm::ymfm_saved_state state(b,true); engine.save_restore(state);
    Put(b,available);Put(b,identity);Put(b,half);Put(b,phase);Put(b,period);
    return b;
}
bool OPNARhythm::Restore(const std::vector<uint8_t>& b) {
    // Fixed pinned ymfm layout: 48 register bytes, six 24-byte channel records.
    if(b.size()!=212||Get(b,192)!=unsigned(available)||Get(b,196)!=identity||
       Get(b,200)>1||!Get(b,208)||Get(b,204)>=Get(b,208))return false;
    for(unsigned ch=0;ch<6;++ch) {
        if((b[0x10+ch]|(b[0x18+ch]<<8))!=starts[ch]||
           (b[0x20+ch]|(b[0x28+ch]<<8))!=ends[ch])return false;
        const size_t at=48+ch*24;
        if(Get(b,at)>1||Get(b,at+4)>1||Get(b,at+8)>255||Get(b,at+12)>8192||
           Get(b,at+16)>4095||Get(b,at+20)>48)return false;
    }
    auto copy=b;ymfm::ymfm_saved_state state(copy,false);engine.save_restore(state);
    half=Get(b,200);phase=Get(b,204);period=Get(b,208);return true;
}
}
