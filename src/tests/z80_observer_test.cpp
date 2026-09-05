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
int main() {
    try {
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
