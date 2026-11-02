// SPDX-License-Identifier: BSD-2-Clause
#include "headless/headless_machine.h"
#include "pc88/beep.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace fs = std::filesystem;
void Check(bool value, const std::string& why) { if (!value) throw std::runtime_error(why); }
std::vector<uint8_t> Read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}};
}
uint32_t U32(const std::vector<uint8_t>& v, size_t at) {
    Check(at + 4 <= v.size(), "truncated RIFF");
    return v[at] | uint32_t(v[at+1])<<8 | uint32_t(v[at+2])<<16 | uint32_t(v[at+3])<<24;
}
bool Tag(const std::vector<uint8_t>& v, size_t at, const char* text) {
    return at+4<=v.size() && std::equal(text,text+4,v.begin()+at);
}

int main() {
    const auto root=fs::temp_directory_path()/("m88-media-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Check(fs::create_directory(root), "fresh test directory");
        // Executable synthetic ROM: display, SSG tone/noise/envelope, FM, BEEP, HALT.
        std::vector<uint8_t> program{0xf3};
        auto out=[&](uint8_t port,uint8_t data) { program.insert(program.end(),{0x3e,data,0xd3,port}); };
        out(0x51,0); for (auto b:{78,24,15,64,0})out(0x50,uint8_t(b)); out(0x51,0x20);
        auto reg=[&](uint8_t r,uint8_t v) {out(0x44,r);out(0x45,v);};
        reg(0,0x40);reg(1,1);reg(6,11);reg(7,0x1e);reg(8,12);reg(9,16);
        reg(11,29);reg(12,1);reg(13,10);
        for (int slot=0;slot<4;++slot) {
            reg(0x30+slot*4,1+slot);reg(0x40+slot*4,24+slot*3);
            reg(0x50+slot*4,31);reg(0x60+slot*4,7);reg(0x70+slot*4,3);reg(0x80+slot*4,0x4f);
        }
        reg(0xb0,0x2c);reg(0xa4,0x22);reg(0xa0,0x69);reg(0x28,0xf0);out(0x40,0x20);
        program.push_back(0x76);program.resize(32768,0x76);
        for (auto name:{"n88.rom","n88_0.rom","n88_1.rom","n88_2.rom","n88_3.rom","disk.rom","font.rom"}) {
            std::ofstream f(root/name,std::ios::binary); f.write(reinterpret_cast<const char*>(program.data()),program.size());
        }
        auto pc=std::make_unique<HeadlessMachine>(); std::string error;
        Check(pc->Initialize(root.u8string(),"",PC8801::Config::N88V2,&error),error);
        Check(pc->RunFrames(2,&error),error);
        const auto state=(root/"start.m88vstate").u8string();
        Check(pc->SaveState(state,error),error);
        auto wav=root/fs::u8path(u8"\u97f3\u58f0.wav"), avi=root/fs::u8path(u8"\u6620\u50cf.avi");
        Check(pc->StartMedia(false,wav.u8string(),error),error);
        Check(pc->StartMedia(true,avi.u8string(),error),error);
        Check(!pc->StartMedia(false,(root/"duplicate.wav").u8string(),error),"duplicate recording accepted");
        Check(!pc->LoadState(state,error),"restoring while recording accepted");
        const auto before=pc->GetCPUTick();
        Check(pc->RunFrames(20,&error),error);
        const auto ticks=uint32_t(pc->GetCPUTick()-before);
        const auto count=pc->Media(false).Samples();
        Check(count>1000 && count>=uint64_t(ticks)*48000/4000000 && count<=uint64_t(ticks)*48000/4000000+1,"PCM duration does not match CPU time");
        Check(pc->Media(true).Frames()==(count*60+47999)/48000,"AVI duration does not track PCM");
        Check(pc->StopMedia(false,error),error); Check(pc->StopMedia(true,error),error);
        const auto first=Read(wav), movie=Read(avi);
        Check(Tag(first,0,"RIFF") && Tag(first,8,"WAVE") && U32(first,4)+8==first.size(),"WAV RIFF size");
        Check(U32(first,24)==48000 && U32(first,40)==count*4,"WAV sample rate/length");
        Check(std::any_of(first.begin()+44,first.end(),[](uint8_t b){return b!=0;}),"tone recorded as silence");
        Check(Tag(movie,8,"AVI ") && U32(movie,4)+8==movie.size(),"AVI RIFF size");
        Check(U32(movie,48)==pc->Media(true).Frames(),"AVI frame count");
        size_t list=12; while(list+12<movie.size() && !Tag(movie,list+8,"movi"))list+=8+U32(movie,list+4);
        Check(Tag(movie,list+8,"movi"),"missing movi list");
        const size_t end=list+8+U32(movie,list+4);
        std::vector<uint8_t> pcm; uint32_t frames=0;
        for(size_t at=list+12;at<end;) {
            const auto size=U32(movie,at+4);Check(at+8+size<=end,"invalid AVI chunk length");
            if(Tag(movie,at,"01wb"))pcm.insert(pcm.end(),movie.begin()+at+8,movie.begin()+at+8+size);
            else {Check(Tag(movie,at,"00dc") && size>8 && movie[at+8]==137 && Tag(movie,at+9,"PNG\r"),"invalid PNG frame");++frames;}
            at+=8+size+(size&1);
        }
        Check(pcm==std::vector<uint8_t>(first.begin()+44,first.end()),"AVI and WAV audio differ");
        Check(Tag(movie,end,"idx1") && frames==pc->Media(true).Frames(),"AVI index/frame mismatch");
        const auto original=first;
        Check(!pc->StartMedia(false,wav.u8string(),error) && Read(wav)==original,"existing output overwritten");
        Check(!pc->StartMedia(true,(root/"bad.mp4").u8string(),error),"unsupported extension accepted");
        Check(!pc->StartMedia(false,(root/"missing"/"out.wav").u8string(),error),"missing directory accepted");
        // Restore the exact FM/SSG/BEEP phase. Request batching must not affect PCM.
        Check(pc->LoadState(state,error),error);
        auto second=root/"second.wav";
        Check(pc->StartMedia(false,second.u8string(),error),error);
        for(int i=0;i<20;++i)Check(pc->RunFrames(1,&error),error);
        Check(pc->StopMedia(false,error),error);
        Check(Read(second)==first,"checkpoint/batched requests changed PCM");
        // Stopping/restarting capture does not reset oscillators, and no run means no audio.
        Check(pc->StartMedia(false,(root/"empty.wav").u8string(),error),error);
        Check(pc->RunFrames(0,&error),error); Check(pc->StopMedia(false,error),error);
        Check(Read(root/"empty.wav").size()==44,"zero-frame capture advanced time");
        Check(pc->StartMedia(true,(root/"shutdown.avi").u8string(),error),error);
        Check(pc->RunFrames(2,&error),error); pc.reset();
        const auto shutdown=Read(root/"shutdown.avi");Check(U32(shutdown,4)+8==shutdown.size(),"shutdown did not finalize AVI");
        std::cout<<"Headless media: timed PCM, PNG/PCM AVI, checkpoints, split requests, UTF-8 paths, no overwrite, shutdown passed\n";
        fs::remove_all(root); return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<" (fixtures: "<<root<<")\n"; return 1; }
}
