// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "development/snapshot.h"
#include "headless/headless_draw.h"
#include "pc88/opnif.h"
#include "pc88/diskmgr.h"
#include "pc88/tapemgr.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool ok,const std::string& why) { if(!ok)throw std::runtime_error(why); }
class Machine : public PC88 {
public:
    void ConfigureDisplay() {
        bus1.Out(0x51,0);
        for(unsigned value:{78u,24u,15u,64u,0u})bus1.Out(0x50,value);
        bus1.Out(0x51,0x20);
    }
};
struct ROMs {
    std::filesystem::path previous=std::filesystem::current_path(), directory;
    ROMs() {
        directory=std::filesystem::temp_directory_path()/
            ("m88-adpcm-state-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Check(std::filesystem::create_directory(directory),"create synthetic ROM directory");
        for(auto name:{"N88.ROM","N80.ROM","DISK.ROM","FONT.ROM"}) {
            std::ofstream f(directory/name,std::ios::binary);
            std::array<char,32768> zeros{};f.write(zeros.data(),zeros.size());
            Check(bool(f),"write synthetic ROM");
        }
        std::filesystem::current_path(directory);
    }
    ~ROMs() {
        std::filesystem::current_path(previous);
        for(auto name:{"N88.ROM","N80.ROM","DISK.ROM","FONT.ROM"})std::filesystem::remove(directory/name);
        std::filesystem::remove(directory);
    }
};
void Reg(PC8801::OPNIF& o,unsigned reg,unsigned value) {o.SetIndex1(0,reg);o.WriteData1(0,value);}
std::vector<int32> Continue(PC8801::OPNIF& o) {
    std::vector<int32> result;
    for(int i=0;i<600;++i) {
        std::array<int32,2> sample{};o.Mix(sample.data(),1);
        result.insert(result.end(),sample.begin(),sample.end());
        result.push_back(o.ReadStatusEx(0));
        if(i%37==0)Reg(o,0x10,0x80);
    }
    return result;
}
}
int main() {
    try {
        ROMs roms; HeadlessDraw draw; DiskManager disks; TapeManager tape; Machine pc;
        Check(disks.Init(),"disk initialization");
        Check(pc.Init(&draw,&disks,&tape,roms.directory.string().c_str()),"core initialization");
        PC8801::Config cfg{};
        cfg.basicmode=PC8801::Config::N88V2;cfg.clock=40;cfg.speed=100;cfg.mainsubratio=1;
        cfg.cpumode=PC8801::Config::msauto;cfg.flags=PC8801::Config::enableopna;
        cfg.sound=44100;cfg.mastervol=64;
        pc.ApplyConfig(&cfg);pc.Reset();pc.ConfigureDisplay();
        auto& o=*pc.GetOPN1();
        std::string error;std::vector<uint8_t> state,front;
        for(unsigned rate:{8000u,44100u})for(bool repeat:{false,true})for(bool eightBit:{false,true}) {
            o.SetRate(rate);
            Reg(o,0,1);Reg(o,1,eightBit?0xc2:0xc0);
            Reg(o,2,0);Reg(o,3,0);Reg(o,4,31);Reg(o,5,0);
            Reg(o,12,0xff);Reg(o,13,0xff);Reg(o,0,0x60);
            for(unsigned i=0;i<1024;++i)Reg(o,8,(i*73+19)&255);
            Reg(o,0,1);Reg(o,2,0);Reg(o,3,0);Reg(o,4,3);Reg(o,5,0);
            Reg(o,9,0x67);Reg(o,10,0x93);Reg(o,11,255);Reg(o,16,0x80);Reg(o,16,0);
            Reg(o,0,repeat?0xb0:0xa0);
            std::array<int32,6> warmup{};o.Mix(warmup.data(),3);
            Check(M88V::Snapshot::Capture(pc,cfg,123,front,state,error),error);
            const auto expected=Continue(o);
            bool audible=false,eos=false;
            for(size_t i=0;i<expected.size();i+=3) {
                audible|=expected[i]!=0||expected[i+1]!=0;eos|=(expected[i+2]&4)!=0;
            }
            Check(audible&&eos,"fixture must generate nonzero audio and reach EOS");
            Check(M88V::Snapshot::Restore(pc,cfg,123,state,front,error),error);
            Check(Continue(o)==expected,"ADPCM samples/EOS diverged after checkpoint restore");
            Check(M88V::Snapshot::Capture(pc,cfg,123,front,state,error),error);
            const auto afterBoundary=Continue(o);
            Check(M88V::Snapshot::Restore(pc,cfg,123,state,front,error),error);
            const auto actual=Continue(o);
            Check(actual==afterBoundary,"ADPCM post-EOS state diverged after restore");
        }
        // External RAM reads contain a two-byte pipeline and advance a cursor.
        Reg(o,0,1);Reg(o,1,0xc2);Reg(o,2,0);Reg(o,3,0);Reg(o,0,0x20);
        o.SetIndex1(0,8);for(int i=0;i<7;++i)o.ReadData1(0);
        Check(M88V::Snapshot::Capture(pc,cfg,123,front,state,error),error);
        std::vector<unsigned> expected;
        for(int i=0;i<75;++i)expected.push_back(o.ReadData1(0));
        Check(M88V::Snapshot::Restore(pc,cfg,123,state,front,error),error);
        for(auto value:expected)Check(o.ReadData1(0)==value,"ADPCM RAM read pipeline diverged");
        std::cout<<"ADPCM checkpoint: one-shot/repeat samples, EOS, RAM read pipeline passed\n";
        return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
