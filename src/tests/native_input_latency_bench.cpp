// SPDX-License-Identifier: BSD-2-Clause
// Manual ROM-backed benchmark: injected KeyDown -> completed frame submission.
// No physical input, desktop visibility, or photon latency is measured here.
#include "headers.h"
#include "windraw.h"
#include "sequence.h"
#include "messages.h"
#include "WinKeyIF.h"
#include "pc88/pc88.h"
#include "pc88/config.h"
#include "pc88/diskmgr.h"
#include "pc88/tapemgr.h"
#include "development/rom_overlay.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <set>

bool RealPresentScreen(HDC,const uint32_t*,int,int,int,int,int);
namespace {
using Clock=std::chrono::steady_clock;
int64_t Nanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}
double ProcessCpuSeconds() {
    FILETIME created,exited,kernel,user;
    if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) return 0;
    auto ticks=[](FILETIME value) { return (uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime; };
    return (ticks(kernel)+ticks(user))/10000000.0;
}
struct Probe {
    std::mutex mutex;
    std::vector<uint32_t> frame;
    std::vector<uint32_t> glyph;
    int x=0,y=0;
    bool capture=true,armed=false,matches=false,completed=false;
    int64_t start=0,end=0,presentStart=0;
    uint64_t presentations=0;
    HANDLE ready=nullptr;
} probe;
class MeasuredDraw : public WinDraw {
public:
    void DrawScreen(const Region& region) override {
        const int64_t timestamp=Nanoseconds();
        {
            std::lock_guard<std::mutex> lock(cadenceMutex);
            if(measuring) calls.push_back(timestamp);
        }
        WinDraw::DrawScreen(region);
    }
    void StartMeasurement() {
        std::lock_guard<std::mutex> lock(cadenceMutex);
        calls.clear(); calls.reserve(8192); measuring=true;
    }
    std::vector<int64_t> Timestamps() {
        std::lock_guard<std::mutex> lock(cadenceMutex);
        return calls;
    }
private:
    std::mutex cadenceMutex;
    std::vector<int64_t> calls;
    bool measuring=false;
};
void Check(bool value,const char* message) {
    if(!value) throw std::runtime_error(message);
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_M88_SENDKEYSTATE) {
        GetKeyboardState(reinterpret_cast<BYTE*>(wp));
        SetEvent(reinterpret_cast<HANDLE>(lp));
        return 0;
    }
    return DefWindowProc(window,message,wp,lp);
}
void Pump(DWORD milliseconds,bool completion=false) {
    const ULONGLONG deadline=GetTickCount64()+milliseconds;
    for(;;) {
        MSG message;
        while(PeekMessage(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessage(&message);
        }
        if(completion && WaitForSingleObject(probe.ready,0)==WAIT_OBJECT_0) return;
        const ULONGLONG now=GetTickCount64();
        if(now>=deadline) return;
        const DWORD remaining=DWORD(deadline-now);
        MsgWaitForMultipleObjectsEx(completion?1:0,completion?&probe.ready:nullptr,
            remaining,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
}
class Machine : public PC88 {
public:
    ~Machine() { DeInit(); }
    bool ConnectKeyboard(HWND window) {
        IOBus::Connector ports[19]{};
        ports[0]={PC88::pres,IOBus::portout,PC8801::WinKeyIF::reset};
        ports[1]={PC88::vrtc,IOBus::portout,PC8801::WinKeyIF::vsync};
        for(int row=0;row<16;++row) ports[row+2]={ushort(row),IOBus::portin,PC8801::WinKeyIF::in};
        return keyboard.Init(window) && bus1.Connect(&keyboard,ports);
    }
    void ConfigureKeyboard(const PC8801::Config& config) {
        keyboard.ApplyConfig(&config); keyboard.Activate(true);
    }
    void Down(UINT key) { keyboard.KeyDown(key,0); }
    void Up(UINT key) { keyboard.KeyUp(key,0); }
private:
    PC8801::WinKeyIF keyboard;
};
void Tap(Machine& machine,UINT key,DWORD duration=100) {
    machine.Down(key); Pump(duration); machine.Up(key); Pump(duration);
}
std::vector<uint32_t> Frame() {
    std::lock_guard<std::mutex> lock(probe.mutex);
    return probe.frame;
}
std::vector<uint32_t> Cell(const std::vector<uint32_t>& frame,int x,int y) {
    std::vector<uint32_t> cell;
    for(int row=0;row<16;++row)
        for(int column=0;column<8;++column) cell.push_back(frame[(y+row)*640+x+column]);
    return cell;
}
bool LooksLikeGlyph(const std::vector<uint32_t>& cell) {
    std::set<unsigned> rows;
    for(int y=0;y<16;++y) {
        unsigned bits=0;
        for(int x=0;x<8;++x) if(cell[y*8+x]&0xffffff) bits|=1u<<x;
        rows.insert(bits);
    }
    // Blank and block/underline cursors have at most two distinct row masks.
    return rows.size()>=3;
}
}

bool PresentScreen(HDC dc,const uint32_t* pixels,int width,int height,int viewWidth,int viewHeight,int filter) {
    const int64_t presentStart=Nanoseconds();
    const bool result=RealPresentScreen(dc,pixels,width,height,viewWidth,viewHeight,filter);
    const int64_t submitted=Nanoseconds();
    bool signal=false;
    if(result && width==640 && height==400) {
        std::lock_guard<std::mutex> lock(probe.mutex);
        ++probe.presentations;
        if(probe.capture) probe.frame.assign(pixels,pixels+640*400);
        if(!probe.glyph.empty()) {
            bool same=true;
            for(int y=0;y<16 && same;++y)
                for(int x=0;x<8;++x)
                    if(pixels[(probe.y+y)*640+probe.x+x]!=probe.glyph[y*8+x]) {same=false;break;}
            probe.matches=same;
            if(probe.armed && same) {
                probe.end=submitted; probe.presentStart=presentStart; probe.completed=true; probe.armed=false; signal=true;
            }
        }
    }
    if(signal) SetEvent(probe.ready);
    return result;
}

int main(int argc,char** argv) {
    if(argc<3 || argc>7) {
        std::cerr<<"Usage: native_input_latency_bench ROM_DIRECTORY RAW_SAMPLES.csv [SAMPLES] [CLOCK_MHZ] [FILTER_0_1_2] [SCALE_PERCENT]\n";
        return 2;
    }
    HWND window=nullptr;
    try {
        const int sampleCount=argc>=4 ? std::stoi(argv[3]) : 72;
        const int filter=argc>=6 ? std::stoi(argv[5]) : 2;
        const int scale=argc>=7 ? std::stoi(argv[6]) : 100;
        Check(filter>=0 && filter<=2 && scale>=50 && scale<=400,"invalid filter/scale");
        Check(sampleCount>=10 && sampleCount<=120,"sample count must be 10..120");
        const int clockMHz=argc>=5 ? std::stoi(argv[4]) : 4;
        Check(clockMHz==4 || clockMHz==8,"CLOCK_MHZ must be 4 or 8");
        M88V::RomOverlay roms;
        std::string error;
        const bool prepared=roms.Prepare(std::filesystem::absolute(argv[1]).u8string(),"",PC8801::Config::N88V1H,&error);
        Check(prepared,error.c_str());
        WNDCLASS wc{}; wc.lpfnWndProc=WindowProc; wc.hInstance=GetModuleHandle(nullptr); wc.lpszClassName="M88LatencyBench";
        Check(RegisterClass(&wc)!=0,"window class registration failed");
        window=CreateWindowEx(0,wc.lpszClassName,"M88 input latency benchmark",WS_POPUP,
            0,0,640,400,nullptr,nullptr,wc.hInstance,nullptr);
        Check(window!=nullptr,"hidden window creation failed");
        probe.ready=CreateEvent(nullptr,TRUE,FALSE,nullptr);
        Check(probe.ready!=nullptr,"probe event creation failed");
        std::vector<double> elapsed;
        std::ofstream csv(argv[2]);
        Check(bool(csv),"cannot create CSV output");
        csv<<"sample,phase_ms,key_down_ns,frame_submission_ns,latency_ms,present_ms\n";
        {
            MeasuredDraw draw;
            DiskManager disk;
            TapeManager tape;
            Machine machine;
            Sequencer sequence;
            Check(draw.Init0(window),"draw initialization failed");
            draw.SetPresentation(640*scale/100,400*scale/100,filter);
            Check(draw.ChangeDisplayMode(false),"renderer creation failed");
            Check(disk.Init(),"disk initialization failed");
            const auto original=std::filesystem::current_path();
            std::filesystem::current_path(std::filesystem::u8path(roms.Directory()));
            const bool initialized=machine.Init(&draw,&disk,&tape,roms.Directory().c_str());
            std::filesystem::current_path(original);
            Check(initialized,"ROM core initialization failed");
            Check(machine.ConnectKeyboard(window),"native keyboard connection failed");
            PC8801::Config config{};
            config.basicmode=PC8801::Config::N88V1H;
            config.clock=clockMHz*10; config.speed=100; config.mainsubratio=1;
            config.cpumode=PC8801::Config::msauto; config.dipsw=1829;
            config.flags=PC8801::Config::subcpucontrol|PC8801::Config::enablewait;
            config.sound=48000; config.soundbuffer=20; config.mastervol=64; config.mousesensibility=10;
            machine.ApplyConfig(&config); machine.ConfigureKeyboard(config); machine.Reset();
            Check(sequence.Init(&machine),"sequencer initialization failed");
            sequence.SetClock(config.clock); sequence.SetSpeed(100); sequence.Activate(true);
            Pump(3500);
            Tap(machine,'0'); Tap(machine,VK_RETURN); Pump(100);
            const auto blank=Frame();
            Check(blank.size()==640*400,"no boot presentation captured");
            Tap(machine,'A');
            const auto letter=Frame();
            bool found=false;
            for(int y=0;y<400 && !found;y+=16) {
                for(int x=0;x<640 && !found;x+=8) {
                    const auto cell=Cell(letter,x,y);
                    if(cell!=Cell(blank,x,y) && LooksLikeGlyph(cell)) {
                        std::lock_guard<std::mutex> lock(probe.mutex);
                        probe.x=x; probe.y=y; probe.glyph=cell; probe.capture=false;
                        found=true;
                    }
                }
            }
            Check(found,"cannot identify calibrated A glyph (not a blinking cursor)");
            std::cout<<"Calibrated A glyph at source pixel "<<probe.x<<','<<probe.y<<" (8x16); filter="<<filter<<" scale_percent="<<scale<<" clock_mhz="<<clockMHz<<'\n';
            Tap(machine,VK_BACK);
            sequence.Lock();
            sequence.GetExecCount(); // Discard boot/calibration cycles under the execution lock.
            draw.StartMeasurement();
            {
                std::lock_guard<std::mutex> lock(probe.mutex);
                probe.presentations=0;
            }
            const int64_t measurementStart=Nanoseconds();
            const double cpuStart=ProcessCpuSeconds();
            sequence.Unlock();
            std::minstd_rand random(8801);
            const ULONGLONG deadline=GetTickCount64()+sampleCount*700+5000;
            for(int sample=0;sample<sampleCount;++sample) {
                Check(GetTickCount64()<deadline,"benchmark exceeded bounded sampling duration");
                const DWORD phase=random()%17;
                Pump(phase);
                {
                    std::lock_guard<std::mutex> lock(probe.mutex);
                    Check(!probe.matches,"previous A has not been erased on a submitted frame");
                    ResetEvent(probe.ready); probe.completed=false;
                    probe.start=Nanoseconds(); probe.armed=true;
                }
                machine.Down('A');
                Pump(500,true);
                machine.Up('A');
                int64_t start,end,presentStart;
                {
                    std::lock_guard<std::mutex> lock(probe.mutex);
                    Check(probe.completed,"key echo did not reach submitted framebuffer within 500ms");
                    start=probe.start; end=probe.end; presentStart=probe.presentStart;
                }
                const double milliseconds=(end-start)/1000000.0;
                Check(milliseconds>=0,"invalid completion timestamp");
                elapsed.push_back(milliseconds);
                csv<<sample<<','<<phase<<','<<start<<','<<end<<','<<milliseconds<<','<<(end-presentStart)/1000000.0<<'\n';
                Pump(100);
                Tap(machine,VK_BACK,100);
            }
            sequence.Activate(false);
            const int64_t measurementEnd=Nanoseconds();
            sequence.Cleanup();
            // The worker has exited, so counters and cadence can be read without races.
            const long vmCycles=sequence.GetExecCount();
            const double wallSeconds=(measurementEnd-measurementStart)/1000000000.0;
            const double cpuSeconds=ProcessCpuSeconds()-cpuStart;
            const auto timestamps=draw.Timestamps();
            std::vector<double> intervals;
            for(size_t i=1;i<timestamps.size();++i)
                intervals.push_back((timestamps[i]-timestamps[i-1])/1000000.0);
            std::sort(intervals.begin(),intervals.end());
            uint64_t presentations;
            {
                std::lock_guard<std::mutex> lock(probe.mutex);
                presentations=probe.presentations;
            }
            std::cout<<"Pacing: requested_draw_calls="<<timestamps.size()
                <<" successful_presentations="<<presentations<<" wall_s="<<wallSeconds
                <<" requested_draw_hz="<<timestamps.size()/wallSeconds;
            if(!intervals.empty())
                std::cout<<" interval_median_ms="<<(intervals[(intervals.size()-1)/2]+intervals[intervals.size()/2])/2
                    <<" interval_p95_ms="<<intervals[(intervals.size()*95+99)/100-1]
                    <<" interval_max_ms="<<intervals.back();
            std::cout<<" vm_cycles="<<vmCycles<<" effective_mhz="<<vmCycles/wallSeconds/1000000.0
                <<" nominal_speed_ratio="<<vmCycles/wallSeconds/(config.clock*100000.0)
                <<" process_cpu_percent_one_core="<<cpuSeconds/wallSeconds*100<<'\n';
        }
        std::sort(elapsed.begin(),elapsed.end());
        std::cout<<"Samples="<<elapsed.size()<<" median_ms="<<(elapsed[(elapsed.size()-1)/2]+elapsed[elapsed.size()/2])/2
            <<" p95_ms="<<elapsed[(elapsed.size()*95+99)/100-1]<<" min_ms="<<elapsed.front()<<" max_ms="<<elapsed.back()<<'\n';
        std::cout<<"Endpoint: injected native KeyDown -> completed native frame submission, hidden window; CSV="<<argv[2]<<'\n';
        CloseHandle(probe.ready); probe.ready=nullptr;
        DestroyWindow(window); return 0;
    } catch(const std::exception& exception) {
        if(probe.ready) CloseHandle(probe.ready);
        if(window) DestroyWindow(window);
        std::cerr<<exception.what()<<'\n'; return 1;
    }
}
