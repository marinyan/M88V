// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#pragma once
#include "devices/z80_observer.h"
#include "memory_inspector.h"
#include <array>
#include <deque>
#include <map>
#include <string>
#include <vector>
namespace M88V {
class Debugger final : public Z80Observer {
public:
    void Attach(Z80C& cpu, PC8801::Memory& memory);
    void Configure(bool profile, unsigned history, bool writes);
    void Clear();
    void Resume();
    bool Stopped() const { return stopped_; }
    bool HasWatches() const { return !watches_.empty(); }
    void FrameEnd();
    bool LoadSymbols(const std::string& path, std::string& error);
    bool Resolve(const std::string& text, uint16_t& address) const;
    bool Watch(uint16_t begin, uint32_t length, const std::string& space, std::string& error);
    void ClearWatches();
    bool Region(const std::string& name, uint16_t begin, uint16_t end, std::string& error);
    std::string StatusJson() const;
    std::string ProfileJson(unsigned top) const;
    std::string TraceJson(unsigned last) const;
    std::string WriterJson(uint16_t address, const std::string& space) const;
    std::string SymbolsJson() const;
private:
    struct Registers { uint16_t pc=0,sp=0,af=0,bc=0,de=0,hl=0,ix=0,iy=0; bool iff1=false,iff2=false; };
    struct WriteEvent { uint64_t sequence=0; uint16_t pc=0,address=0,sp=0; uint8_t value=0; bool iff1=false; Mapping map; };
    struct Trace { uint64_t sequence=0; Registers before,after; std::string bytes,space,kind; uint32_t elapsed=0,waits=0,idle=0; std::vector<WriteEvent> writes; };
    struct Stat { uint64_t instructions=0,entries=0,total=0,waits=0,idle=0,frame=0,maxFrame=0; uint32_t maxInstruction=0; std::string symbol,space; uint16_t address=0; };
    struct Watchpoint { uint16_t begin; uint32_t length; std::string space; };
    struct RegionStat { std::string name; uint16_t begin,end; uint64_t hits=0,total=0,max=0,waits=0,idle=0; std::vector<std::array<uint64_t,3>> pending; };
    void Begin(Z80C& cpu,const char* kind) override;
    bool End(Z80C& cpu,uint32_t elapsed,uint32_t waits,uint32_t idle,const char* kind) override;
    void Write(Z80C& cpu,uint16_t address,uint8_t value) override;
    void Refresh();
    static Registers Reg(Z80C& cpu);
    static std::string RegJson(const Registers& r);
    static std::string WriteJson(const WriteEvent& event);
    std::string Symbol(uint16_t pc) const;
    Z80C* cpu_=nullptr;
    PC8801::Memory* memory_=nullptr;
    bool profiling_=false,trackWrites_=false,stopped_=false;
    unsigned history_=0;
    uint64_t sequence_=0,total_=0,waits_=0,idle_=0;
    std::map<uint16_t,std::string> symbols_;
    std::map<std::string,uint16_t> addresses_;
    std::map<std::string,Stat> stats_;
    std::map<std::string,WriteEvent> writers_;
    std::vector<Watchpoint> watches_;
    std::vector<RegionStat> regions_;
    std::deque<Trace> traces_;
    Trace current_;
    WriteEvent hit_;
};
}
