// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "headers.h"
#include "device.h"
#include "memmgr.h"
#include "Z80c.h"
#include <array>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

// NEC 1979 Microcomputer Catalog, uPD780 instruction tables (printed pp.177-182,
// PDF pp.174-179): block I/O timing, Z from B, N set and C unchanged.
// http://bitsavers.org/components/nec/_dataBooks/1979_NEC_Microcomputer_Catalog.pdf
// Undefined flags and intra-instruction B/port ordering are deliberately not
// asserted: Zilog NMOS behavior is not sufficient evidence for NEC silicon.
struct Machine : Device {
    Z80C cpu{DEV_ID('B','I','O','T')}; MemoryManager memory; IOBus bus;
    alignas(2) std::array<uint8,65536> ram{};
    unsigned value=0, calls=0, seenPort=0, written=0;
    Machine() : Device(DEV_ID('P','R','O','B')) {
        MemoryPage *rd,*wr; cpu.GetPages(&rd,&wr);
        if(!memory.Init(65536,rd,wr)||!bus.Init(256)) throw std::runtime_error("init");
        const int owner=memory.Connect(this);
        if(!memory.AllocR(owner,0,65536,ram.data())||!memory.AllocW(owner,0,65536,ram.data()))
            throw std::runtime_error("map");
        for(unsigned p=0;p<256;++p) {
            bus.ConnectIn(p,this,static_cast<Device::InFuncPtr>(&Machine::Input));
            bus.ConnectOut(p,this,static_cast<Device::OutFuncPtr>(&Machine::Output));
        }
        cpu.Init(&memory,&bus,0);
        std::fill(cpu.GetWaits(),cpu.GetWaits()+(65536>>MemoryManager::pagebits),0);
    }
    uint IOCALL Input(uint p) { ++calls; seenPort=p; return value; }
    void IOCALL Output(uint p,uint v) { ++calls; seenPort=p; written=v; }
    void Prepare(unsigned op,unsigned b,unsigned c,unsigned l,unsigned data,unsigned flags,unsigned pc) {
        cpu.Reset(); cpu.SetObserver(nullptr); cpu.SetPC(0);
        const uint8 setup[]={0x31,0x00,0x70,0xf1,0x01,uint8(c),uint8(b),0x21,uint8(l),0x50};
        std::copy(std::begin(setup),std::end(setup),ram.begin());
        ram[0x7000]=uint8(flags); ram[0x7001]=0x5a;
        for(unsigned i=0;i<4;++i) cpu.ExecOne();
        ram[pc]=0xed; ram[(pc+1)&0xffff]=uint8(op); cpu.SetPC(pc);
        ram[0x5000+l]=uint8((op&1)?data:data^0xff); value=data; calls=0; written=0;
    }
};
struct Observer : Z80Observer {
    unsigned cycles=0,writes=0;
    void Begin(Z80C& cpu,const char*) override {cpu.DebugAF();}
    bool End(Z80C& cpu,uint32_t c,uint32_t,uint32_t,const char*) override {cycles+=c;cpu.DebugAF();return false;}
    void Write(Z80C&,uint16_t,uint8_t) override {++writes;}
};
int main() {
    try {
        auto m=std::make_unique<Machine>(); unsigned cases=0,failures=0;
        auto check=[&](bool ok,const char* what,unsigned op,unsigned b,unsigned data) {
            if(!ok) {if(failures++<15) std::cerr<<what<<" op="<<std::hex<<op<<" B="<<b<<" data="<<data<<std::dec<<'\n';}
        };
        const unsigned ops[]={0xa2,0xaa,0xa3,0xab,0xb2,0xba,0xb3,0xbb};
        const unsigned bs[]={0,1,2,0x10,0x11,0x29,0x80,0x81};
        const unsigned operands[]={0,1,7,0x7f,0xfe,0xff};
        for(unsigned op:ops) for(unsigned b:bs) for(unsigned c:operands) for(unsigned data=0;data<256;++data) {
            // Cover both directions crossing an L-byte boundary, with distinct ports.
            const unsigned l=c^0xff,pc=(data&1)?0x2800:0x1000;
            m->Prepare(op,b,c,l,data,(data&1)?0xff:0,pc);
            Observer observer; if(data&2) m->cpu.SetObserver(&observer);
            const int cycles=m->cpu.ExecOne(); const bool repeat=(op&0x10)&&b!=1;
            const unsigned afterB=(b-1)&255, afterHL=(0x5000+l+((op&8)?-1:1))&65535;
            check(cycles==(repeat?21:16),"cycles",op,b,data);
            check(m->cpu.GetPC()==(repeat?pc:pc+2),"PC",op,b,data);
            check((m->cpu.DebugAF()&0x43)==((afterB?0:0x40)|2|(data&1)),"defined flags",op,b,data);
            check(m->cpu.GetReg().r.b.b==afterB&&(m->cpu.GetReg().r.w.hl&65535)==afterHL&&
                m->cpu.GetReg().r.b.c==c&&m->cpu.GetReg().r.b.a==0x5a,"registers",op,b,data);
            check(m->calls==1&&m->seenPort==c,"bus transfer count/port",op,b,data);
            check((op&1)?m->written==data:m->ram[0x5000+l]==data,"transfer",op,b,data);
            if(data&2) check(observer.cycles==unsigned(cycles)&&observer.writes==((op&1)?0:1),"observer",op,b,data);
            m->cpu.SetObserver(nullptr); ++cases;
        }
        // Exercise repeat through termination, including B=0 meaning 256 transfers.
        for(unsigned op: {0xb2,0xba,0xb3,0xbb}) for(unsigned b: {2,0}) {
            m->Prepare(op,b,0x42,0x80,0x80,0xff,0x2800);
            unsigned cycles=0, count=b?b:256;
            for(unsigned i=0;i<count;++i) cycles+=m->cpu.ExecOne();
            check(cycles==21*(count-1)+16&&m->calls==count&&m->cpu.GetPC()==0x2802,"complete repeat",op,b,0);
            ++cases;
        }
        std::cout<<cases<<" block I/O cases, "<<failures<<" failures\n";
        return failures?1:0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
