// SPDX-License-Identifier: BSD-2-Clause
// Manual test only: supply a legally owned ROM directory; never register with CTest.
#include "headers.h"
#include "windraw.h"
#include "sequence.h"
#include "pc88/pc88.h"
#include "pc88/config.h"
#include "pc88/memory.h"
#include "pc88/diskmgr.h"
#include "pc88/tapemgr.h"
#include "WinKeyIF.h"
#include "development/rom_overlay.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void RunFor(DWORD milliseconds) {
    const ULONGLONG end=GetTickCount64()+milliseconds;
    do {
        MSG message;
        while (PeekMessage(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessage(&message);
        }
        Sleep(1);
    } while (GetTickCount64()<end);
}
class Machine : public PC88 {
public:
    ~Machine() { DeInit(); }
    bool ConnectKeyboard() {
        IOBus::Connector ports[19]{};
        ports[0]={PC88::pres,IOBus::portout,PC8801::WinKeyIF::reset};
        ports[1]={PC88::vrtc,IOBus::portout,PC8801::WinKeyIF::vsync};
        for(int row=0;row<16;++row) ports[row+2]={ushort(row),IOBus::portin,PC8801::WinKeyIF::in};
        return keyboard.Init(nullptr) && bus1.Connect(&keyboard,ports);
    }
    void ConfigureKeyboard(const PC8801::Config& config) {
        keyboard.ApplyConfig(&config); keyboard.Activate(true);
    }
    bool SetKey(const char* name,bool down) {
        const UINT key=std::strcmp(name,"enter")==0 ? VK_RETURN : UINT(name[0]);
        if(down) keyboard.KeyDown(key,0); else keyboard.KeyUp(key,0);
        return true;
    }
private:
    PC8801::WinKeyIF keyboard;
};
long TakeCycles(Sequencer& sequence) {
    sequence.Lock();
    const long cycles=sequence.GetExecCount();
    sequence.Unlock();
    return cycles;
}
bool Contains(const uint8* bytes, size_t size, const char* text) {
    const size_t length=std::strlen(text);
    return std::search(bytes,bytes+size,text,text+length)!=bytes+size;
}
}

int main(int argc,char** argv) {
    if (argc!=2 && argc!=3) {
        std::cerr<<"Usage: native_rom_timing_test ROM_DIRECTORY [CLOCK_MHZ]\n";
        return 2;
    }
    HWND window=nullptr;
    try {
        const int clockMHz=argc==3 ? std::stoi(argv[2]) : 4;
        Check(clockMHz==4 || clockMHz==8,"CLOCK_MHZ must be 4 or 8");
        M88V::RomOverlay roms;
        std::string error;
        const bool prepared=roms.Prepare(std::filesystem::absolute(argv[1]).u8string(),"",PC8801::Config::N88V1H,&error);
        Check(prepared,error.c_str());
        window=CreateWindowEx(0,"STATIC","Native ROM timing test",WS_POPUP,
            0,0,640,400,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
        Check(window!=nullptr,"hidden window creation failed");
        {
            WinDraw draw;
            DiskManager disk;
            TapeManager tape;
            Machine machine;
            Sequencer sequence; // Destroy worker before its VM and renderer.
            Check(draw.Init0(window),"draw Init0 failed");
            draw.SetPresentation(640,400,0);
            Check(draw.ChangeDisplayMode(false),"renderer creation failed");
            Check(disk.Init(),"disk initialization failed");
            const auto original=std::filesystem::current_path();
            std::filesystem::current_path(std::filesystem::u8path(roms.Directory()));
            const bool initialized=machine.Init(&draw,&disk,&tape,roms.Directory().c_str());
            std::filesystem::current_path(original);
            Check(initialized,"ROM core initialization failed");
            Check(machine.ConnectKeyboard(),"idle keyboard connection failed");
            PC8801::Config config{};
            config.basicmode=PC8801::Config::N88V1H;
            config.clock=clockMHz*10; config.speed=100; config.mainsubratio=1;
            config.cpumode=PC8801::Config::msauto; config.dipsw=1829;
            config.flags=PC8801::Config::subcpucontrol|PC8801::Config::enablewait;
            config.sound=48000; config.soundbuffer=20; config.mastervol=64;
            config.mousesensibility=10;
            machine.ApplyConfig(&config);
            machine.ConfigureKeyboard(config);
            machine.Reset();
            Check(sequence.Init(&machine),"sequencer initialization failed");
            sequence.SetClock(config.clock); sequence.SetSpeed(100);
            sequence.Activate(true);
            RunFor(3500);
            const ULONGLONG pauseStart=GetTickCount64();
            sequence.Activate(false);
            const ULONGLONG pauseMs=GetTickCount64()-pauseStart;
            const long bootCycles=TakeCycles(sequence);
            RunFor(50);
            Check(TakeCycles(sequence)==0,"CPU advanced while paused");
            Check(bootCycles>clockMHz*1000000,"ROM did not execute enough cycles to boot");
            auto* memory=machine.GetMem1();
            // Disk BASIC asks for the number of files before the ready prompt.
            for(const char* key : {"0","enter"}) {
                Check(machine.SetKey(key,true),"test key mapping failed");
                sequence.Activate(true); RunFor(100); sequence.Activate(false);
                machine.SetKey(key,false);
                sequence.Activate(true); RunFor(100); sequence.Activate(false);
            }
            const bool prompt=Contains(memory->GetTVRAM(),4096,"Ok") || Contains(memory->GetRAM()+0xf000,4096,"Ok");
            const bool banner=Contains(memory->GetTVRAM(),4096,"BASIC") || Contains(memory->GetRAM()+0xf000,4096,"BASIC");
            Check(banner,"BASIC banner absent after answering file-count prompt");
            Check(prompt,"BASIC ready prompt absent from text RAM");
            Check(draw.GetDrawCount()>0,"native renderer did not present boot frames");

            // Exercise each native filter while the real ROM continues running.
            for(int filter=0;filter<3;++filter) {
                draw.SetPresentation(960,600,filter);
                draw.RequestPaint();
                sequence.Activate(true);
                RunFor(350);
                sequence.Activate(false);
                Check(TakeCycles(sequence)>0,"CPU failed to resume");
            }
            // Rapid transitions must neither lose the wakeup nor replay paused time.
            for(int change=0;change<8;++change) {
                sequence.Activate(true);
                sequence.SetClock(change%2 ? 40 : 80);
                sequence.SetSpeed(change%2 ? 100 : 200);
                RunFor(20);
                sequence.Activate(false);
                TakeCycles(sequence);
                RunFor(5);
                Check(TakeCycles(sequence)==0,"rapid transition advanced paused CPU");
            }
            sequence.SetClock(config.clock); sequence.SetSpeed(100);
            sequence.Activate(true);
            RunFor(100);
            const ULONGLONG cleanupStart=GetTickCount64();
            Check(sequence.Cleanup(),"sequencer cleanup failed");
            const ULONGLONG cleanupMs=GetTickCount64()-cleanupStart;
            Check(pauseMs<1000 && cleanupMs<1000,"pause or cleanup was unresponsive");
            std::cout<<"N88 BASIC banner + Ok verified; clock_mhz="<<clockMHz<<"; boot cycles="<<bootCycles
                <<"; pause="<<pauseMs<<" ms; cleanup="<<cleanupMs<<" ms\n";
        }
        DestroyWindow(window);
        std::cout<<"ROM-backed native sequencer validation passed (hidden window, no audio device)\n";
        return 0;
    } catch(const std::exception& exception) {
        if(window) DestroyWindow(window);
        std::cerr<<exception.what()<<'\n'; return 1;
    }
}
