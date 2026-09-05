// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "debugger.h"
#include "json.h"
#include "devices/Z80c.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace M88V {
void Debugger::Attach(Z80C& cpu,PC8801::Memory& memory) { cpu_=&cpu; memory_=&memory; Refresh(); }
void Debugger::Refresh() { if(cpu_) cpu_->SetObserver(profiling_||history_||trackWrites_||!watches_.empty()?this:nullptr); }
void Debugger::Configure(bool p,unsigned h,bool w) { profiling_=p; history_=std::min(h,16384u); trackWrites_=w; while(traces_.size()>history_) traces_.pop_front(); Resume(); Refresh(); }
void Debugger::Resume() { stopped_=false; if(cpu_) cpu_->ResumeDebug(); }
void Debugger::Clear() { stats_.clear(); writers_.clear(); traces_.clear(); sequence_=total_=waits_=idle_=0; for(auto& r:regions_) { r.hits=r.total=r.max=r.waits=r.idle=0; r.pending.clear(); } Resume(); }
void Debugger::FrameEnd() { for(auto& s:stats_) { s.second.maxFrame=std::max(s.second.maxFrame,s.second.frame); s.second.frame=0; } }
Debugger::Registers Debugger::Reg(Z80C& cpu) {
    Registers r; r.af=cpu.DebugAF(); const auto& z=cpu.GetReg();
    r.pc=static_cast<uint16_t>(cpu.GetPC()); r.sp=z.r.w.sp;r.bc=z.r.w.bc;r.de=z.r.w.de;r.hl=z.r.w.hl;r.ix=z.r.w.ix;r.iy=z.r.w.iy;r.iff1=z.iff1;r.iff2=z.iff2; return r;
}
std::string Debugger::RegJson(const Registers& r) {
    std::ostringstream o; o<<"{\"pc\":"<<r.pc<<",\"sp\":"<<r.sp<<",\"af\":"<<r.af<<",\"bc\":"<<r.bc<<",\"de\":"<<r.de<<",\"hl\":"<<r.hl<<",\"ix\":"<<r.ix<<",\"iy\":"<<r.iy<<",\"iff1\":"<<(r.iff1?"true":"false")<<",\"iff2\":"<<(r.iff2?"true":"false")<<'}'; return o.str();
}
bool Debugger::LoadSymbols(const std::string& path,std::string& error) {
    std::ifstream f(std::filesystem::u8path(path));
    if(!f) { error="Cannot read symbols";return false; }
    std::map<uint16_t,std::string> syms; std::map<std::string,uint16_t> names;
    const std::regex pattern(R"(^\s*([A-Za-z_.$][A-Za-z0-9_.$@]*):?\s+(?:(?:[Ee][Qq][Uu]|=)\s*)?(?:0[xX]|\$)?([0-9A-Fa-f]{1,4})[Hh]?\s*(?:;.*)?$)");
    std::string line; unsigned lines=0;
    while(std::getline(f,line)) {
        if(++lines>100000 || line.size()>4096) { error="Symbol file exceeds limits";return false; }
        if(line.empty() || line.find_first_not_of(" \t\r")==std::string::npos || line.find_first_not_of(" \t")==line.find(';')) continue;
        std::smatch match;
        if(!std::regex_match(line,match,pattern)) { error="Invalid symbol line "+std::to_string(lines)+" (expected NAME HEX or NAME EQU HEX)";return false; }
        auto address=static_cast<uint16_t>(std::stoul(match[2],nullptr,16));
        if(names.count(match[1]) && names[match[1]]!=address) {error="Conflicting symbol name";return false;}
        names[match[1]]=address; if(!syms.count(address)) syms[address]=match[1];
    }
    if(names.empty()) {error="No symbols found";return false;}
    symbols_=std::move(syms); addresses_=std::move(names); Clear(); return true;
}
bool Debugger::Resolve(const std::string& text,uint16_t& address) const {
    auto i=addresses_.find(text); if(i!=addresses_.end()) {address=i->second;return true;}
    try { std::string s=text; int base=10; if(!s.empty()&&(s.back()=='H'||s.back()=='h')) {s.pop_back();base=16;} else if(s.rfind("0x",0)==0||s.rfind("0X",0)==0) {s=s.substr(2);base=16;} else if(!s.empty()&&s[0]=='$') {s=s.substr(1);base=16;}
        if(s.empty()||s[0]=='-'||s[0]=='+')return false; size_t n=0; auto v=std::stoul(s,&n,base); if(n!=s.size()||v>65535)return false;address=static_cast<uint16_t>(v);return true;
    } catch(...) {return false;}
}
std::string Debugger::Symbol(uint16_t pc) const { auto i=symbols_.upper_bound(pc);return i==symbols_.begin()?"<unlabelled>":std::prev(i)->second; }
bool Debugger::Watch(uint16_t begin,uint32_t length,const std::string& space,std::string& error) {
    if(!length || uint32_t(begin)+length>65536 || watches_.size()>=64) {error="Watch range must fit 64 KiB; at most 64 watches";return false;}
    if(space!="cpu" && space!="ram" && space!="tvram" && space!="gvram-b" && space!="gvram-r" && space!="gvram-g" && space!="gvram-alu") {error="Unsupported watch space";return false;}
    watches_.push_back({begin,length,space}); Resume();Refresh();return true;
}
void Debugger::ClearWatches() {watches_.clear();Resume();Refresh();}
bool Debugger::Region(const std::string& name,uint16_t begin,uint16_t end,std::string& error) {
    if(name.empty()||name.size()>128||begin==end||regions_.size()>=64) {error="Invalid region markers";return false;}
    for(auto& r:regions_) if(r.name==name) {error="Region already exists";return false;}
    regions_.push_back({name,begin,end});return true;
}
void Debugger::Begin(Z80C& cpu,const char* kind) {
    current_={};current_.sequence=++sequence_;current_.before=Reg(cpu);
    current_.kind=kind;
    auto mapping=MemoryInspector::At(*memory_,cpu,current_.before.pc,false);current_.space=mapping.space;
    if(history_ || !watches_.empty()) {
        std::ostringstream bytes; bytes<<std::hex<<std::setfill('0');
        for(unsigned i=0;i<4;++i) {auto m=MemoryInspector::At(*memory_,cpu,static_cast<uint16_t>(current_.before.pc+i),false);if(!m.data)break;bytes<<std::setw(2)<<unsigned(*m.data);}
        current_.bytes=bytes.str();
    }
    if(profiling_ && current_.kind=="instruction") for(auto& r:regions_) {
        if(current_.before.pc==r.end && !r.pending.empty()) {
            auto p=r.pending.back();r.pending.pop_back();const auto elapsed=total_-p[0];++r.hits;r.total+=elapsed;r.max=std::max(r.max,elapsed);r.waits+=waits_-p[1];r.idle+=idle_-p[2];
        }
        if(current_.before.pc==r.begin && r.pending.size()<1024) r.pending.push_back({total_,waits_,idle_});
    }
}
bool Debugger::End(Z80C& cpu,uint32_t elapsed,uint32_t waits,uint32_t idle,const char* kind) {
    current_.kind=kind;
    current_.after=Reg(cpu); current_.elapsed=elapsed;current_.waits=waits;current_.idle=idle;
    total_+=elapsed;waits_+=waits;idle_+=idle;
    if(profiling_) {
        const auto name=current_.kind=="instruction"?Symbol(current_.before.pc):"<"+current_.kind+">";const auto key=current_.space+":"+name;
        auto& s=stats_[key];s.symbol=name;s.space=current_.space;
        auto symbol=symbols_.upper_bound(current_.before.pc);s.address=symbol==symbols_.begin()?0:std::prev(symbol)->first;
        if(current_.kind=="instruction"){++s.instructions;if(current_.before.pc==s.address)++s.entries;}
        s.total+=elapsed;s.waits+=waits;s.idle+=idle;s.frame+=elapsed;s.maxInstruction=std::max(s.maxInstruction,elapsed);
    }
    const unsigned capacity=std::max(history_,watches_.empty()?0u:32u);
    if(capacity) { traces_.push_back(current_);while(traces_.size()>capacity)traces_.pop_front(); }
    return stopped_;
}
void Debugger::Write(Z80C& cpu,uint16_t address,uint8_t value) {
    if(!trackWrites_ && watches_.empty() && !history_)return;
    auto mapping=MemoryInspector::At(*memory_,cpu,address,true);mapping.data=nullptr;
    WriteEvent event{current_.sequence,current_.before.pc,address,static_cast<uint16_t>(cpu.GetReg().r.w.sp),value,bool(cpu.GetReg().iff1),mapping};
    if(trackWrites_) {
        for(const auto& key:{mapping.space+":"+std::to_string(mapping.offset),"cpu:"+std::to_string(address)})
            if(writers_.size()<262144||writers_.count(key))writers_[key]=event;
    }
    if(current_.writes.size()<16) current_.writes.push_back(event);
    for(const auto& w:watches_) {
        uint32_t a=w.space=="cpu"?address:mapping.offset;
        if((w.space=="cpu"||w.space==mapping.space)&&a>=w.begin&&a<uint32_t(w.begin)+w.length) {if(!stopped_)hit_=event;stopped_=true;}
    }
}
std::string Debugger::WriteJson(const WriteEvent& e) {
    return "{\"sequence\":"+std::to_string(e.sequence)+",\"pc\":"+std::to_string(e.pc)+",\"address\":"+std::to_string(e.address)+",\"value\":"+std::to_string(e.value)+",\"space\":"+Quote(e.map.space)+",\"offset\":"+std::to_string(e.map.offset)+",\"sp\":"+std::to_string(e.sp)+",\"iff1\":"+(e.iff1?"true":"false")+"}";
}
std::string Debugger::StatusJson() const {
    return "{\"profiling\":"+std::string(profiling_?"true":"false")+",\"history_capacity\":"+std::to_string(history_)+",\"track_writes\":"+(trackWrites_?"true":"false")+",\"watchpoints\":"+std::to_string(watches_.size())+",\"stopped\":"+(stopped_?"true":"false")+",\"hit\":"+(stopped_?WriteJson(hit_):"null")+"}";
}
std::string Debugger::WriterJson(uint16_t address,const std::string& space) const {
    auto i=writers_.find(space+":"+std::to_string(address));return "{\"ok\":true,\"writer\":"+(i==writers_.end()?std::string("null"):WriteJson(i->second))+"}";
}
std::string Debugger::TraceJson(unsigned last) const {
    std::string out="{\"ok\":true,\"trace\":[";bool comma=false;
    size_t begin=traces_.size()>last?traces_.size()-last:0;
    for(size_t i=begin;i<traces_.size();++i) {auto& t=traces_[i];if(comma)out+=',';comma=true;
        out+="{\"sequence\":"+std::to_string(t.sequence)+",\"symbol\":"+Quote(Symbol(t.before.pc))+",\"space\":"+Quote(t.space)+",\"bytes\":"+Quote(t.bytes)+",\"before\":"+RegJson(t.before)+",\"after\":"+RegJson(t.after)+",\"tstates\":"+std::to_string(t.elapsed)+",\"wait_tstates\":"+std::to_string(t.waits)+",\"idle_tstates\":"+std::to_string(t.idle)+",\"writes\":[";
        for(size_t j=0;j<t.writes.size();++j) {if(j)out+=',';out+=WriteJson(t.writes[j]);}out+="],\"kind\":"+Quote(t.kind)+"}";
    }return out+"]}";
}
std::string Debugger::ProfileJson(unsigned top) const {
    std::vector<const Stat*> list;for(auto& s:stats_)list.push_back(&s.second);
    std::sort(list.begin(),list.end(),[](auto a,auto b){return a->total>b->total;});
    std::string out="{\"ok\":true,\"tstates\":"+std::to_string(total_)+",\"wait_tstates\":"+std::to_string(waits_)+",\"idle_tstates\":"+std::to_string(idle_)+",\"symbols\":[";
    for(size_t i=0;i<std::min<size_t>(top,list.size());++i) {auto& s=*list[i];if(i)out+=',';
        out+="{\"name\":"+Quote(s.symbol)+",\"space\":"+Quote(s.space)+",\"address\":"+std::to_string(s.address)+",\"instructions\":"+std::to_string(s.instructions)+",\"entry_hits\":"+std::to_string(s.entries)+",\"tstates\":"+std::to_string(s.total)+",\"wait_tstates\":"+std::to_string(s.waits)+",\"idle_tstates\":"+std::to_string(s.idle)+",\"max_frame_tstates\":"+std::to_string(std::max(s.frame,s.maxFrame))+"}";
    }
    out+="],\"regions\":[";
    for(size_t i=0;i<regions_.size();++i) {auto& r=regions_[i];if(i)out+=',';out+="{\"name\":"+Quote(r.name)+",\"hits\":"+std::to_string(r.hits)+",\"tstates\":"+std::to_string(r.total)+",\"max_tstates\":"+std::to_string(r.max)+",\"wait_tstates\":"+std::to_string(r.waits)+",\"idle_tstates\":"+std::to_string(r.idle)+"}";}
    return out+"]}";
}
std::string Debugger::SymbolsJson() const {return "{\"ok\":true,\"symbols\":"+std::to_string(addresses_.size())+"}";}
}
