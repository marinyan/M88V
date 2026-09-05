// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "snapshot.h"
#include "pc88/base.h"
#include "pc88/crtc.h"
#include "pc88/fdc.h"
#include "pc88/memory.h"
#include "pc88/opnif.h"
#include "pc88/tapemgr.h"
#include "pc88/diskmgr.h"
#include "pc88/sound.h"
#include "pc88/calender.h"
#include "zlib/zlib.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <chrono>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#endif

namespace M88V {
class StateCodec {
public:
    std::vector<uint8_t>& bytes; bool loading; size_t position=0;
    StateCodec(std::vector<uint8_t>& b,bool read):bytes(b),loading(read) {}
    void Data(void* data,size_t size) {
        if(loading) { if(size>bytes.size()-position)throw std::runtime_error("Truncated runtime state");std::memcpy(data,bytes.data()+position,size);position+=size; }
        else {auto p=static_cast<const uint8_t*>(data);bytes.insert(bytes.end(),p,p+size);}
    }
    template<class T> void Value(T& value) {static_assert(std::is_trivially_copyable<T>::value,"state must not contain owning objects");Data(&value,sizeof(value));}
};

IDevice::TimeFunc Snapshot::Callback(PC88& p,IDevice* d,uint32_t token) {
    using F=IDevice::TimeFunc;
    if(d==p.base && token==1)return static_cast<F>(&PC8801::Base::RTC);
    if(d==p.crtc) {
        if(token==2)return static_cast<F>(&PC8801::CRTC::StartDisplay);
        if(token==3)return static_cast<F>(&PC8801::CRTC::ExpandLine);
        if(token==4)return static_cast<F>(&PC8801::CRTC::ExpandLineEnd);
    }
    if(d==p.fdc) {
        if(token==5)return static_cast<F>(&PC8801::FDC::PhaseTimer);
        if(token==6)return static_cast<F>(&PC8801::FDC::SeekEvent);
    }
    if((d==p.opn1||d==p.opn2)&&token==7)return static_cast<F>(&PC8801::OPNIF::TimeEvent);
    if(d==p.tapemgr&&token==8)return static_cast<F>(&TapeManager::Timer);
    if(dynamic_cast<PC8801::Sound*>(d)&&token==9)return static_cast<F>(&PC8801::Sound::UpdateCounter);
    return nullptr;
}

void Snapshot::Runtime(PC88& p,StateCodec& c) {
#define FIELD(o,f) c.Value(o.f)
    FIELD(p,clock);FIELD(p,eclock);FIELD(p,cpumode);FIELD(p,dexc);
    auto& scheduler=static_cast<Scheduler&>(p);
    FIELD(scheduler,time);FIELD(scheduler,etime);FIELD(scheduler,evlast);
    auto& calendar=*p.caln;
    FIELD(calendar,diff);FIELD(calendar,developmentEpoch);FIELD(calendar,developmentLast);FIELD(calendar,developmentTicks);
    bool emulated=calendar.developmentClock!=nullptr;c.Value(emulated);
    if(c.loading)calendar.developmentClock=emulated?&scheduler:nullptr;
    if(c.loading && (scheduler.evlast < -1 || scheduler.evlast>=Scheduler::maxevents))throw std::runtime_error("Invalid scheduler count");
    for(int i=0;i<Scheduler::maxevents;++i) {
        auto& e=scheduler.events[i];
        uint32_t device=(!c.loading&&i<=scheduler.evlast&&e.inst)?e.inst->GetID():0, token=0;
        int count=0,arg=0,time=0;
        if(device) {
            for(uint32_t k=1;k<=9;++k)if(Callback(p,e.inst,k)==e.func) {token=k;break;}
            if(!token)throw std::runtime_error("Unknown scheduled device callback");
            count=e.count;arg=e.arg;time=e.time;
        }
        c.Value(device);c.Value(token);c.Value(count);c.Value(arg);c.Value(time);
        if(c.loading) {
            e={};if(!device)continue;
            auto d=p.devlist.Find(device);auto fn=Callback(p,d,token);
            if(!d||!fn||time<0)throw std::runtime_error("Incompatible scheduled device");
            e.inst=d;e.func=fn;e.count=count;e.arg=arg;e.time=time;
        }
    }
    const auto handle=[&](SchedulerEvent*& event) {
        int index=-1;
        if(!c.loading&&event) {
            for(int i=0;i<Scheduler::maxevents;++i)if(event==&scheduler.events[i])index=i;
            if(index<0)throw std::runtime_error("Invalid event handle");
        }
        c.Value(index);if(index < -1 || index>=Scheduler::maxevents)throw std::runtime_error("Invalid event handle");
        if(c.loading)event=index<0?nullptr:&scheduler.events[index];
    };
    handle(p.crtc->sev);handle(p.fdc->timerhandle);handle(p.tapemgr->event);
    for(auto cpu:{&p.cpu1,&p.cpu2}) {
        FIELD((*cpu),clockcount);FIELD((*cpu),stopcount);FIELD((*cpu),delaycount);
        FIELD((*cpu),eshift);FIELD((*cpu),startcount);
        FIELD((*cpu),pendingDebugIRQ);FIELD((*cpu),pendingDebugNMI);
        if(c.loading) {cpu->uf=0;cpu->index_mode=Z80C::USEHL;cpu->debugPaused=false;}
    }
    auto& b=*p.base;
    FIELD(b,port40);FIELD(b,sw30);FIELD(b,sw31);FIELD(b,sw6e);FIELD(b,autoboot);
    auto& m=*p.mem1;
    FIELD(m,alureg);FIELD(m,seldic);FIELD(m,maskr);FIELD(m,maski);FIELD(m,masks);FIELD(m,aluread);
    FIELD(m,waitmode);FIELD(m,waittype);
    if(c.loading) {
        if(m.waitmode<0||m.waittype<0||m.waitmode+m.waittype>=48)throw std::runtime_error("Invalid memory wait state");
        if(m.n80mode)m.UpdateN80G();else {m.UpdateC0();m.UpdateF0();}m.SetWait();
    }
    auto& v=*p.crtc;
    FIELD(v,cmdm);FIELD(v,cmdc);FIELD(v,cursormode);FIELD(v,linesize);FIELD(v,line200);
    FIELD(v,attr);FIELD(v,attr_cursor);FIELD(v,attr_blink);FIELD(v,status);FIELD(v,column);
    FIELD(v,linetime);FIELD(v,frametime);FIELD(v,pcgadr);FIELD(v,pcgdat);FIELD(v,pat_col);
    FIELD(v,pat_mask);FIELD(v,pat_rev);FIELD(v,underlineptr);FIELD(v,bank);FIELD(v,screenheight);
    FIELD(v,cursor_x);FIELD(v,cursor_y);FIELD(v,attrperline);FIELD(v,linecharlimit);FIELD(v,linesperchar);
    FIELD(v,width);FIELD(v,height);FIELD(v,blinkrate);FIELD(v,cursor_type);FIELD(v,vretrace);FIELD(v,mode);
    FIELD(v,widefont);FIELD(v,pcgenable);FIELD(v,kanaenable);FIELD(v,kanamode);
    FIELD(v,pcount);FIELD(v,param0);FIELD(v,param1);FIELD(v,event);
    if(c.loading && (v.width>80||v.height>100||v.bank>1||v.pcount[0]>6||v.pcount[1]>1))throw std::runtime_error("Invalid CRTC state");
    c.Data(v.vram[0],0x5000);c.Data(v.pcgram,0x400);c.Data(v.font,0x18000);
    for(auto o:{p.opn1,p.opn2}) {
        FIELD((*o),regs);
        FIELD((*o),nextcount);FIELD((*o),prevtime);FIELD((*o),basetime);FIELD((*o),basetick);FIELD((*o),delay);
        FIELD(o->opn,intrenabled);FIELD(o->opn,intrpending);
        auto& timer=static_cast<FM::Timer&>(o->opn);
        FIELD(timer,status);FIELD(timer,regtc);FIELD(timer,regta);FIELD(timer,timera);FIELD(timer,timera_count);
        FIELD(timer,timerb);FIELD(timer,timerb_count);FIELD(timer,timer_step);
    }
    auto& t=*p.tapemgr;
    FIELD(t,tick);FIELD(t,mode);FIELD(t,time);FIELD(t,timercount);FIELD(t,timerremain);
    FIELD(t,motor);FIELD(t,datasize);FIELD(t,datatype);
    // The portable core caches the opcode page and its wait value. They can
    // legitimately lag behind VRTC wait-table changes until the next SetPC.
    for(auto cpu:{&p.cpu1,&p.cpu2}) {
        uint32_t pc=cpu->GetPC();
        bool direct=cpu->instlim!=nullptr;
        uint32_t page=direct?static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cpu->instpage)-reinterpret_cast<uintptr_t>(cpu->instbase))&0xffff:pc;
        c.Value(direct);c.Value(page);int cachedWait=cpu->instwait;c.Value(cachedWait);FIELD((*cpu),waittable);
        if(c.loading) {
            if(page>65535||cachedWait<0||cachedWait>1024)throw std::runtime_error("Invalid CPU fetch cache");
            cpu->SetPC(direct?page:pc);cpu->inst=cpu->instbase+pc;cpu->instwait=cachedWait;
        }
    }
    c.Value(Z80C::cbase);
    int current=Z80C::currentcpu==&p.cpu1?1:Z80C::currentcpu==&p.cpu2?2:0;c.Value(current);
    if(c.loading)Z80C::currentcpu=current==1?&p.cpu1:current==2?&p.cpu2:nullptr;
#undef FIELD
}

namespace {
constexpr uint32_t maxState=16*1024*1024;
struct Envelope {
    char magic[12];uint32_t version,abi,rom,mode,clock,eram,flags,flag2;
    uint32_t deviceSize,runtimeSize,frontendSize,crc,configId;
};
uint32_t Abi() {return static_cast<uint32_t>(sizeof(PC8801::Config)*65536+sizeof(Z80Reg)*256+sizeof(void*));}
uint32_t Sum(const uint8_t* p,size_t n) {return crc32(0,p,static_cast<uInt>(n));}
uint32_t ConfigId(const PC8801::Config& c) {
    const int32_t values[]={c.flags,c.flag2,c.cpumode,c.mainsubratio,c.dipsw,c.opnclock,c.sound};
    return Sum(reinterpret_cast<const uint8_t*>(values),sizeof(values));
}
void CheckDevicePayload(DeviceList& devices,const std::vector<uint8_t>& bytes) {
    size_t pos=0;std::set<uint32_t> seen;
    while(pos+8<=bytes.size()) {
        uint32_t id,size;std::memcpy(&id,bytes.data()+pos,4);std::memcpy(&size,bytes.data()+pos+4,4);pos+=8;
        if(!id) {if(size||pos!=bytes.size())break;return;}
        auto device=devices.Find(id);
        if(!device||device->GetStatusSize()!=size||!seen.insert(id).second||size>bytes.size()-pos)break;
        // The legacy CRTC loader iterates this untrusted count before runtime validation.
        if(id==DEV_ID('C','R','T','C')&&(size<5||bytes[pos+3]>6||bytes[pos+4]>1))break;
        pos+=(size+3)&~size_t(3);
    }
    throw std::runtime_error("Invalid device state payload");
}
}

bool Snapshot::Capture(PC88& p,const PC8801::Config& cfg,uint32_t rom,
        const std::vector<uint8_t>& frontend,std::vector<uint8_t>& output,std::string& error) {
    try {
        if(p.diskmgr->GetCurrentDisk(0)>=0||p.diskmgr->GetCurrentDisk(1)>=0||p.tapemgr->IsOpen())
            throw std::runtime_error("Development checkpoints require unmounted disks and a closed tape (external media are not rolled back)");
        std::vector<uint8_t> devices(p.devlist.GetStatusSize());
        if(!p.devlist.SaveStatus(devices.data()))throw std::runtime_error("Device state capture failed");
        std::vector<uint8_t> runtime;StateCodec codec(runtime,false);Runtime(p,codec);
        Envelope h{};std::memcpy(h.magic,"M88VSTATE",9);h.version=1;h.abi=Abi();h.rom=rom;
        h.mode=cfg.basicmode;h.clock=cfg.clock;h.eram=cfg.erambanks;h.flags=cfg.flags;h.flag2=cfg.flag2;
        h.configId=ConfigId(cfg);
        h.deviceSize=static_cast<uint32_t>(devices.size());h.runtimeSize=static_cast<uint32_t>(runtime.size());h.frontendSize=static_cast<uint32_t>(frontend.size());
        output.resize(sizeof(h));output.insert(output.end(),devices.begin(),devices.end());output.insert(output.end(),runtime.begin(),runtime.end());output.insert(output.end(),frontend.begin(),frontend.end());
        if(output.size()>maxState)throw std::runtime_error("State exceeds size limit");
        h.crc=Sum(output.data()+sizeof(h),output.size()-sizeof(h));std::memcpy(output.data(),&h,sizeof(h));return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

bool Snapshot::Restore(PC88& p,const PC8801::Config& cfg,uint32_t rom,
        const std::vector<uint8_t>& input,std::vector<uint8_t>& frontend,std::string& error) {
    try {
        if(input.size()<sizeof(Envelope)||input.size()>maxState)throw std::runtime_error("Invalid state size");
        Envelope h{};std::memcpy(&h,input.data(),sizeof(h));
        const uint32_t coreFlags=PC8801::Config::subcpucontrol|PC8801::Config::enablewait|PC8801::Config::enableopna|PC8801::Config::opnaona8|PC8801::Config::opnona8|PC8801::Config::fv15k;
        if(std::memcmp(h.magic,"M88VSTATE",9)||h.version!=1||h.abi!=Abi()||h.rom!=rom||h.mode!=uint32_t(cfg.basicmode)||h.clock!=uint32_t(cfg.clock)||h.eram!=cfg.erambanks||((h.flags^cfg.flags)&coreFlags)||h.configId!=ConfigId(cfg))
            throw std::runtime_error("State format, ROM set, machine configuration or build ABI mismatch");
        if(uint64_t(sizeof(h))+h.deviceSize+h.runtimeSize+h.frontendSize!=input.size()||h.frontendSize!=frontend.size()||h.crc!=Sum(input.data()+sizeof(h),input.size()-sizeof(h)))
            throw std::runtime_error("State checksum/length mismatch");
        std::vector<uint8_t> backup;std::string reason;
        if(!Capture(p,cfg,rom,{},backup,reason))throw std::runtime_error(reason);
        std::vector<uint8_t> devices(input.begin()+sizeof(h),input.begin()+sizeof(h)+h.deviceSize);
        CheckDevicePayload(p.devlist,devices);
        std::vector<uint8_t> runtime(input.begin()+sizeof(h)+h.deviceSize,input.begin()+sizeof(h)+h.deviceSize+h.runtimeSize);
        Envelope old{};std::memcpy(&old,backup.data(),sizeof(old));
        if(h.deviceSize!=old.deviceSize||h.runtimeSize!=old.runtimeSize)throw std::runtime_error("Incompatible device layout");
        auto apply=[&](const std::vector<uint8_t>& dev,std::vector<uint8_t>& run) {
            p.Reset();
            if(!p.devlist.LoadStatus(dev.data()))throw std::runtime_error("Device state restore failed");
            StateCodec codec(run,true);Runtime(p,codec);
            if(codec.position!=run.size())throw std::runtime_error("Unexpected runtime data");
            p.region.Reset();p.updated=false;p.UpdateScreen(true);
        };
        try {apply(devices,runtime);} catch(...) {
            std::vector<uint8_t> dev(backup.begin()+sizeof(old),backup.begin()+sizeof(old)+old.deviceSize);
            std::vector<uint8_t> run(backup.begin()+sizeof(old)+old.deviceSize,backup.end());
            apply(dev,run);throw;
        }
        frontend.assign(input.end()-h.frontendSize,input.end());return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

bool Snapshot::ReadFile(const std::string& path,std::vector<uint8_t>& b,std::string& error) {
    try {std::ifstream f(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);auto size=f.tellg();if(!f||size<=0||size>maxState){error="Cannot read state/replay file or size exceeds 16 MiB";return false;}
        b.resize(static_cast<size_t>(size));f.seekg(0);if(!f.read(reinterpret_cast<char*>(b.data()),size)){error="Incomplete state/replay file";return false;}return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Snapshot::WriteFile(const std::string& path,const std::vector<uint8_t>& b,std::string& error,bool replace) {
    if(path.empty()){error="path is required";return false;}
    std::filesystem::path temporary;
    bool created=false;
    try {
        const auto destination=std::filesystem::u8path(path);
        if(!replace&&std::filesystem::exists(destination))throw std::runtime_error("Output already exists; choose a new checkpoint/replay path");
        static std::atomic<unsigned> sequence{0};
        temporary=destination;temporary+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++);
#ifdef _WIN32
        FILE* f=_wfopen(temporary.c_str(),L"wbx");
#else
        FILE* f=fopen(temporary.c_str(),"wbx");
#endif
        if(!f)throw std::runtime_error("Cannot create state/replay output");
        created=true;
        const bool written=fwrite(b.data(),1,b.size(),f)==b.size();const bool closed=fclose(f)==0;
        if(!written||!closed)throw std::runtime_error("Cannot finish state/replay output");
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),destination.c_str(),replace?MOVEFILE_REPLACE_EXISTING:0))throw std::runtime_error("Cannot publish state/replay output (destination exists or is unavailable)");
#else
        if(replace)std::filesystem::rename(temporary,destination);
        else {std::filesystem::create_hard_link(temporary,destination);std::filesystem::remove(temporary);}
#endif
        return true;
    }catch(const std::exception& e) {
        if(created){std::error_code ignored;std::filesystem::remove(temporary,ignored);}
        error=e.what();return false;
    }
}
}
