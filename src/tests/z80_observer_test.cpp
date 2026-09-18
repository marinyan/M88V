// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "headers.h"
#include "device.h"
#include "memmgr.h"
#include "Z80c.h"
#include <array>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <memory>

struct Recorder : Z80Observer {
    struct Event {unsigned pc,after,cycles,waits,writes;std::string kind;};
    std::vector<Event> events;
    Event event{};bool stopWrites=false;
    void Begin(Z80C& cpu,const char* kind) override {event={cpu.GetPC(),0,0,0,0,kind};}
    bool End(Z80C& cpu,uint32_t cycles,uint32_t waits,uint32_t,const char* kind) override {
        event.after=cpu.GetPC();event.cycles=cycles;event.waits=waits;event.kind=kind;events.push_back(event);return stopWrites&&event.writes;
    }
    void Write(Z80C&,uint16_t,uint8_t) override {++event.writes;}
};
struct Machine {
    Z80C cpu{DEV_ID('T','E','S','T')};MemoryManager memory;IOBus bus;
    alignas(2) std::array<uint8,65536> ram{};
    Machine(const std::vector<uint8>& code) {
        MemoryPage *rd,*wr;cpu.GetPages(&rd,&wr);
        if(!memory.Init(65536,rd,wr)||!bus.Init(256))throw std::runtime_error("init");
        int owner=memory.Connect(this);
        if(!memory.AllocR(owner,0,65536,ram.data())||!memory.AllocW(owner,0,65536,ram.data()))throw std::runtime_error("map");
        cpu.Init(&memory,&bus,0);std::copy(code.begin(),code.end(),ram.begin());
        std::fill(cpu.GetWaits(),cpu.GetWaits()+(65536>>MemoryManager::pagebits),2);cpu.SetPC(0);
    }
};
void Require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void Compare(const std::vector<uint8>& code,unsigned steps,bool irq=false) {
    auto aMachine=std::make_unique<Machine>(code),bMachine=std::make_unique<Machine>(code);
    auto& plain=*aMachine;auto& observed=*bMachine;Recorder log;observed.cpu.SetObserver(&log);
    if(irq){plain.cpu.IRQ(0,1);observed.cpu.IRQ(0,1);}
    for(unsigned i=0;i<steps;++i) {
        unsigned before=log.events.size();int a=plain.cpu.ExecOne(),b=observed.cpu.ExecOne();
        unsigned sum=0;for(unsigned j=before;j<log.events.size();++j)sum+=log.events[j].cycles;
        Require(a==b&&b==sum,"observed cycles differ from unobserved core");
        Require(plain.cpu.GetPC()==observed.cpu.GetPC()&&plain.ram==observed.ram,"observer changed execution");
        Require(plain.cpu.DebugAF()==observed.cpu.DebugAF(),"observer changed flags");
    }
}
void PreservedAdd16Flags() {
    struct FlagsRecorder : Recorder {
        void Begin(Z80C& cpu,const char* kind) override {
            cpu.DebugAF(); // The real debugger captures registers at each boundary.
            Recorder::Begin(cpu,kind);
        }
        bool End(Z80C& cpu,uint32_t cycles,uint32_t waits,uint32_t idle,const char* kind) override {
            cpu.DebugAF();
            return Recorder::End(cpu,cycles,waits,idle,kind);
        }
    };
    struct Arithmetic { uint8 a, opcode, operand, preserved; };
    const Arithmetic cases[] = {
        {0x7f,0xc6,1,0x84}, // ADD: negative signed overflow.
        {1,0xd6,1,0x40},    // SUB: zero, without overflow.
        {0x80,0xd6,1,4},    // SUB: positive signed overflow.
        {1,0xc6,1,0},       // ADD: clear previously set Z/PV.
    };
    for (uint8 prefix : {0,0xdd,0xfd}) for (uint8 opcode : {0x09,0x19,0x29,0x39}) {
        for (const auto& test : cases) {
            std::vector<uint8> code{0xaf,0x3e,test.a,test.opcode,test.operand};
            if(prefix) code.push_back(prefix);
            code.push_back(opcode);
            auto plain=std::make_unique<Machine>(code),observed=std::make_unique<Machine>(code);
            FlagsRecorder recorder; observed->cpu.SetObserver(&recorder);
            int plainCycles=0,observedCycles=0;
            // Do not read the plain flags between instructions: doing so hid
            // the lost lazy S/Z/PV state in the original per-step comparison.
            for(unsigned i=0;i<4;++i) {
                plainCycles+=plain->cpu.ExecOne();
                observedCycles+=observed->cpu.ExecOne();
            }
            const auto flags=plain->cpu.DebugAF(), observedFlags=observed->cpu.DebugAF();
            Require((flags&0xc4)==test.preserved,"ADD HL/IX/IY failed to preserve lazy S/Z/PV");
            Require(flags==observedFlags&&plainCycles==observedCycles,
                "register observation changed ADD HL/IX/IY flags or cycles");
        }
    }
}
// Zilog UM0080 instruction tables: nominal T states, without board wait states.
void CompatibilityRegressions() {
    unsigned failures=0,cases=0;
    auto check=[&](bool ok,const std::string& message) {
        ++cases;
        if(!ok) { ++failures; if(failures<35) std::cerr<<message<<'\n'; }
    };
    auto machine=[](const std::vector<uint8>& code) {
        auto m=std::make_unique<Machine>(code);
        std::fill(m->cpu.GetWaits(),m->cpu.GetWaits()+(65536>>MemoryManager::pagebits),0);
        m->cpu.SetPC(0); return m;
    };
    auto timing=[&](const char* name,const std::vector<uint8>& code,unsigned setup,int expected,unsigned pc) {
        for(bool observed : {false,true}) {
            auto m=machine(code); Recorder log;
            m->ram[0x4000]=0x34;m->ram[0x4001]=0x12;
            if(observed)m->cpu.SetObserver(&log);
            for(unsigned i=0;i<setup;++i)m->cpu.ExecOne();
            int actual=m->cpu.ExecOne();
            check(actual==expected,std::string(name)+" cycles "+std::to_string(actual)+" != "+std::to_string(expected));
            check(m->cpu.GetPC()==pc,std::string(name)+" changed control flow");
            if(observed)check(log.events.back().cycles==unsigned(actual),std::string(name)+" observer cycles");
        }
    };
    timing("DJNZ taken",{0x06,2,0x10,2},1,13,6);
    timing("DJNZ untaken",{0x06,1,0x10,2},1,8,4);
    timing("RET",{0x31,0,0x40,0xc9},1,10,0x1234);
    for(uint8 op : {0xc0,0xc8,0xd0,0xd8,0xe0,0xe8,0xf0,0xf8}) {
        for(uint8 flags : {0,0xc5}) {
            // POP AF loads flags from a separate location, then restore SP.
            std::vector<uint8> code{0x31,0,0x41,0xf1,0x31,0,0x40,op};
            bool bit=(flags & ((op&0x30)==0 ? 0x40 : (op&0x30)==0x10 ? 1 : (op&0x30)==0x20 ? 4 : 0x80))!=0;
            bool taken=bit==bool(op&8);
            for(bool observed : {false,true}) {
                auto m=machine(code);Recorder log;if(observed)m->cpu.SetObserver(&log);
                m->ram[0x4100]=flags;m->ram[0x4000]=0x34;m->ram[0x4001]=0x12;
                for(int i=0;i<3;++i)m->cpu.ExecOne();
                check(m->cpu.ExecOne()==(taken?11:5),"RET cc cycles");
                check(m->cpu.GetPC()==(taken?0x1234:8),"RET cc control flow");
            }
        }
    }
    for(uint8 op : {0xc7,0xcf,0xd7,0xdf,0xe7,0xef,0xf7,0xff})
        timing("RST",{0x31,0,0x40,op},1,11,op&0x38);
    for(uint8 prefix : {0,0xdd,0xfd}) {
        for(uint8 op : {0x22,0x2a}) {
            std::vector<uint8> code;if(prefix)code.push_back(prefix);
            code.insert(code.end(),{op,0,0x40});
            timing("LD 16-bit absolute",code,0,prefix?20:16,unsigned(code.size()));
        }
        std::vector<uint8> setup;if(prefix)setup.push_back(prefix);
        setup.insert(setup.end(),{0x21,0,0x40});
        for(uint8 op : {0x36,0x34,0x35,0x46,0x70,0x86}) {
            auto code=setup;if(prefix)code.push_back(prefix);code.push_back(op);
            if(prefix)code.push_back(1);
            if(op==0x36)code.push_back(0x55);
            int expected=(op==0x34||op==0x35)?(prefix?23:11):(op==0x36?(prefix?19:10):(prefix?19:7));
            timing("indexed/base memory instruction",code,1,expected,unsigned(code.size()));
        }
    }
    // Every flag byte must survive POP and both directions of EX AF,AF'.
    for(unsigned flags=0;flags<256;++flags) {
        auto m=machine({0x31,0,0x40,0xf1,0x08,0xaf,0x08});
        m->ram[0x4000]=uint8(flags);m->ram[0x4001]=0xa5;
        m->cpu.ExecOne();m->cpu.ExecOne();
        check(m->cpu.DebugAF()==(0xa500|flags),"POP AF lost flag bits");
        m->cpu.ExecOne();m->cpu.ExecOne();m->cpu.ExecOne();
        check(m->cpu.DebugAF()==(0xa500|flags),"EX AF lost flag bits");
    }
    auto reset=machine({0x3e,0x28,0xb7});
    reset->cpu.ExecOne();reset->cpu.ExecOne();reset->cpu.Reset();
    check(reset->cpu.DebugAF()==0,"Reset retained stale undocumented flags");
    std::cout<<"Z80 compatibility: "<<cases<<" checks, "<<failures<<" failures\n";
    Require(failures==0,"Z80 compatibility regressions");
}
int main() {
    try {
        CompatibilityRegressions();
        PreservedAdd16Flags();
        Compare({0x31,0x01,0x44,0x01,0x34,0x12,0xC5,0xDD,0x21,0x00,0x40,0xDD,0x36,0x02,0x77},5);
        Compare({0xED,0x56,0xFB,0x00},2,true); // IM1; EI; NOP; interrupt acknowledgement.
        Compare({0xFB,0xF3,0x00},3,true); // EI/DI lookahead.
        Compare({0xFB,0x00,0xD3,0x20,0x00},3); // ordinary output.
        auto mHeap=std::make_unique<Machine>(std::vector<uint8>{0xED,0x56,0xFB,0x00});auto& m=*mHeap;Recorder log;m.cpu.SetObserver(&log);m.cpu.IRQ(0,1);
        m.cpu.ExecOne();m.cpu.ExecOne();
        Require(log.events.size()==4,"EI must split into EI, following instruction and IRQ");
        Require(log.events[1].pc==2&&log.events[2].pc==3&&log.events[3].kind=="irq"&&log.events[3].writes==2,"IRQ attribution");
        auto outHeap=std::make_unique<Machine>(std::vector<uint8>{0xFB,0x00,0xD3,0x20,0x00});auto& out=*outHeap;Recorder outLog;out.cpu.SetObserver(&outLog);out.cpu.ExecOne();out.cpu.IRQ(0,1);out.cpu.ExecOne();
        Require(outLog.events[2].pc==2&&outLog.events[3].pc==4&&outLog.events[4].kind=="irq","OUT lookahead attribution");
        auto pushHeap=std::make_unique<Machine>(std::vector<uint8>{0x31,0x01,0x44,0x01,0x34,0x12,0xC5,0x00});auto& push=*pushHeap;Recorder watch;push.cpu.SetObserver(&watch);
        push.cpu.ExecOne();push.cpu.ExecOne();watch.stopWrites=true;push.cpu.ExecOne();
        Require(push.cpu.IsDebugPaused()&&push.cpu.GetPC()==7&&watch.events.back().writes==2,"cross-page PUSH watch");
        Require(push.ram[0x43ff]==0x34&&push.ram[0x4400]==0x12,"PUSH did not finish both writes");
        auto pc=push.cpu.GetPC();push.cpu.TestIntr();push.cpu.ExecOne();Require(push.cpu.GetPC()==pc,"paused CPU executed");
        push.cpu.ResumeDebug();push.cpu.ExecOne();Require(push.cpu.GetPC()==8,"resume");
        auto eiHeap=std::make_unique<Machine>(std::vector<uint8>{0xED,0x56,0xFB,0x32,0x00,0x40});auto& ei=*eiHeap;
        Recorder eiLog;eiLog.stopWrites=true;ei.cpu.SetObserver(&eiLog);ei.cpu.IRQ(0,1);ei.cpu.ExecOne();ei.cpu.ExecOne();
        Require(ei.cpu.GetPC()==6&&ei.cpu.IsDebugPaused(),"EI write stop boundary");
        eiLog.stopWrites=false;ei.cpu.ResumeDebug();ei.cpu.ExecOne();
        Require(eiLog.events[eiLog.events.size()-2].kind=="irq","pending IRQ lost across watch resume");
        ei.cpu.Reset();ei.cpu.SetPC(0);eiLog.events.clear();eiLog.stopWrites=true;
        ei.cpu.IRQ(0,1);ei.cpu.ExecOne();ei.cpu.ExecOne();ei.cpu.SetObserver(nullptr);ei.cpu.ExecOne();
        Require(ei.cpu.GetPC()==0x39,"pending IRQ lost when instrumentation disabled");
        std::cout<<"Z80 observer cycles, prefixes, EI, IRQ, OUT and PUSH boundaries: PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
