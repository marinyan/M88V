// SPDX-License-Identifier: BSD-2-Clause
#include "development/legacy_snapshot.h"
#include "device.h"
#include "zlib/zlib.h"
#include "pc88/pc88.h"
#include "headless/headless_draw.h"
#include "pc88/diskmgr.h"
#include "pc88/tapemgr.h"
#include "pc88/memory.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace M88V::LegacySnapshot;
namespace {
void Check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
void Word(std::vector<uint8_t>& b,size_t at,uint32_t n) { std::memcpy(b.data()+at,&n,4); }
class StateDevice:public Device {
public:
    explicit StateDevice(uint32_t id):Device(id) {}
    unsigned loads=0;
    unsigned size=8;
    uint8_t revision=1;
    uint8_t value=37;
    uint IFCALL GetStatusSize() override { return size; }
    bool IFCALL SaveStatus(uint8* p) override { std::memset(p,0,size);p[0]=revision;p[1]=value;return true; }
    bool IFCALL LoadStatus(const uint8* p) override { if(p[0]!=1)return false;++loads;value=p[1];return true; }
};
Header HeaderFor(size_t size) {
    Header h{};std::memcpy(h.id,"M88 SnapshotData",16);h.major=1;h.minor=1;
    h.datasize=int32_t(size);h.basicmode=PC8801::Config::N88V2;h.clock=40;h.mainsubratio=1;return h;
}
std::vector<uint8_t> File(Header h,const std::vector<uint8_t>& body,bool zipped=false) {
    std::vector<uint8_t> file(sizeof(h));
    if(zipped) {
        h.flags|=0x80000000u;
        uLongf size=compressBound(uLong(body.size()));std::vector<uint8_t> encoded(size);
        Check(compress(encoded.data(),&size,body.data(),uLong(body.size()))==Z_OK,"compress fixture");
        encoded.resize(size);file.resize(sizeof(h)+4);Word(file,sizeof(h),uint32_t(-int32_t(size)));
        file.insert(file.end(),encoded.begin(),encoded.end());
    } else file.insert(file.end(),body.begin(),body.end());
    std::memcpy(file.data(),&h,sizeof(h));return file;
}
class Machine:public PC88 {
public:
    DeviceList& Devices() {return devlist;}
};
struct ROMs {
    std::filesystem::path previous=std::filesystem::current_path(),directory;
    ROMs() {
        directory=std::filesystem::temp_directory_path()/
            ("m88-legacy-state-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Check(std::filesystem::create_directory(directory),"create synthetic ROM directory");
        for(auto name:{"N88.ROM","N80.ROM","DISK.ROM","FONT.ROM","n80_2.rom","n80_3.rom"}) {
            std::ofstream f(directory/name,std::ios::binary);std::array<char,40960> zeros{};
            f.write(zeros.data(),zeros.size());Check(bool(f),"write synthetic ROM");
        }
        std::filesystem::current_path(directory);
    }
    ~ROMs() {
        std::filesystem::current_path(previous);
        for(auto name:{"N88.ROM","N80.ROM","DISK.ROM","FONT.ROM","n80_2.rom","n80_3.rom"})std::filesystem::remove(directory/name);
        std::filesystem::remove(directory);
    }
};
void ActualCoreSchemas() {
    ROMs roms;HeadlessDraw draw;DiskManager disks;TapeManager tape;Machine pc;
    Check(disks.Init(),"disk initialization");
    Check(pc.Init(&draw,&disks,&tape,roms.directory.string().c_str()),"core initialization");
    using C=PC8801::Config;
    C config{};config.basicmode=C::N88V2;config.clock=40;config.speed=100;
    config.mainsubratio=1;config.cpumode=C::msauto;config.sound=44100;
    for(auto mode:{C::N80,C::N88V1,C::N88V1H,C::N88V2,C::N802,C::N80V2})
    for(unsigned flags:{0u,unsigned(C::enableopna),unsigned(C::opnona8),unsigned(C::opnaona8)})
    for(unsigned banks:{0u,2u})for(bool disable:{false,true}) {
        config.basicmode=mode;config.flags=flags;config.erambanks=banks;config.flag2=disable?C::disableopn44:0;
        pc.ApplyConfig(&config);pc.Reset();
        std::vector<uint8_t> saved(pc.Devices().GetStatusSize());pc.Devices().SaveStatus(saved.data());
        Header target=HeaderFor(saved.size());target.basicmode=mode;target.flags=flags;
        target.erambanks=banks;target.flag2=config.flag2;
        Check(ValidateDevices(pc.Devices(),saved,pc.GetMem1()->GetERAMBanks(),target,
            pc.GetMem1()->GetResetERAMBanks(banks,mode)),"actual core same-config schema");
        config.basicmode=C::N88V2;config.flags=C::enableopna|C::opnaona8;config.erambanks=1;config.flag2=0;
        pc.ApplyConfig(&config);pc.Reset();
        Check(ValidateDevices(pc.Devices(),saved,pc.GetMem1()->GetERAMBanks(),target,
            pc.GetMem1()->GetResetERAMBanks(banks,mode)),"actual core cross-config schema");
        config.basicmode=mode;config.flags=flags;config.erambanks=banks;config.flag2=target.flag2;
        pc.ApplyConfig(&config);pc.Reset();
        Check(pc.Devices().LoadStatus(saved.data()),"actual core validated payload restore");
    }
}
}
int main() {
    try {
        StateDevice first(DEV_ID('B','E','E','P')),second(DEV_ID('C','A','L','N'));
        DeviceList devices;devices.Add(&first);devices.Add(&second);
        std::vector<uint8_t> body(devices.GetStatusSize());Check(devices.SaveStatus(body.data()),"save fixture");
        Header h=HeaderFor(body.size()),out{};std::vector<uint8_t> decoded;
        auto accept=[&](const std::vector<uint8_t>& bytes) {
            // Same preflight sequence as WinCore; failed inputs must not reach restore.
            if(!Decode(bytes,out,decoded)||!ValidateDevices(devices,decoded,0,out))return false;
            return devices.LoadStatus(decoded.data());
        };
        Check(accept(File(h,body)),"valid raw restore");Check(accept(File(h,body,true)),"valid compressed restore");
        const unsigned oldLoads=first.loads+second.loads;
        auto reject=[&](const std::vector<uint8_t>& b,const char* name) {
            Check(!accept(b),name);Check(first.loads+second.loads==oldLoads,"invalid input mutated live devices");
            Check(first.value==37&&second.value==37,"invalid input altered device state");
        };
        auto raw=File(h,body),zip=File(h,body,true);
        for(size_t n=0;n<raw.size();++n) reject({raw.begin(),raw.begin()+n},"raw truncation");
        for(size_t n=0;n<zip.size();++n) reject({zip.begin(),zip.begin()+n},"compressed truncation");
        auto bad=h;bad.datasize=-1;reject(File(bad,body),"negative size");
        bad=h;bad.datasize=0x7fffffff;reject(File(bad,body),"excessive allocation");
        bad=h;++bad.datasize;reject(File(bad,body,true),"decompression size mismatch");
        for(int value:{0,-1,1001}) {bad=h;bad.clock=int16_t(value);reject(File(bad,body),"invalid clock");}
        bad=h;bad.erambanks=257;reject(File(bad,body),"invalid ERAM count");
        bad=h;bad.basicmode=PC8801::Config::BASICMode(99);reject(File(bad,body),"invalid BASIC mode");
        bad=h;bad.cpumode=3;reject(File(bad,body),"invalid CPU mode");
        bad=h;bad.mainsubratio=0;reject(File(bad,body),"invalid CPU ratio");
        auto broken=body;Word(broken,4,0xfffffffdu);reject(File(h,broken),"record size overflow");
        broken=body;Word(broken,broken.size()-4,1);reject(File(h,broken),"invalid terminator");
        broken=body;std::memcpy(broken.data()+16,broken.data(),4);reject(File(h,broken),"duplicate device");
        broken=body;broken[24]=255;reject(File(h,broken),"late device revision mismatch");
        broken=body;Word(broken,0,DEV_ID('B','A','D','!'));reject(File(h,broken),"unknown device");
        auto extra=raw;extra.push_back(0);reject(extra,"trailing raw data");
        extra=zip;extra.push_back(0);Word(extra,sizeof(h),uint32_t(-int32_t(extra.size()-sizeof(h)-4)));
        reject(extra,"trailing compressed data");
        extra=zip;Word(extra,sizeof(h),0x80000000u);reject(extra,"compressed length INT_MIN");
        // Demonstrate the pre-existing DeviceList failure mechanism on intact,
        // bounded data: a later bad revision leaves an earlier device changed.
        broken=body;broken[9]=91;broken[24]=255;
        Check(!devices.LoadStatus(broken.data()),"baseline late revision unexpectedly accepted");
        Check(first.value==91||second.value==91,"baseline partial mutation not reproduced");
        // Config changes legitimately alter RAM sizes and the presence of sound boards.
        StateDevice memory(DEV_ID('M','E','M','1')),opn1(DEV_ID('O','P','N','1')),opn2(DEV_ID('O','P','N','2'));
        DeviceList configurable;configurable.Add(&memory);configurable.Add(&opn1);configurable.Add(&opn2);
        opn1.revision=opn2.revision=3;opn1.size=518;opn2.size=0;
        for(bool expanded:{false,true}) {
            memory.size=8+(expanded?0x8000:0);opn1.size=518+(expanded?0x40000:0);opn2.size=expanded?518:0;
            std::vector<uint8_t> target(configurable.GetStatusSize());configurable.SaveStatus(target.data());
            auto config=HeaderFor(target.size());config.erambanks=expanded?1:0;
            config.flags=expanded?PC8801::Config::enableopna|PC8801::Config::opnona8:0;
            memory.size=8;opn1.size=0;opn2.size=518+0x40000;
            Check(ValidateDevices(configurable,target,0,config),"cross-config ERAM/OPN restore rejected");
        }
        ActualCoreSchemas();
        std::cout<<"legacy snapshot preflight: valid raw/zlib, all truncations, malformed records/config, partial-mutation regression, and 96 real-core config combinations passed\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
