// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "development/snapshot.h"
#include "headless/headless_draw.h"
#include "pc88/diskmgr.h"
#include "pc88/tapemgr.h"
#include "pc88/memory.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace PC8801 {
class MemoryPort40TestAccess {
public:
    static void SetSavedPort(std::vector<uint8_t>& state, uint8_t value) {
        state[offsetof(Memory::Status, p40)] = value;
    }
    // Emulate a checkpoint produced before the unused-bit fix.
    static void SetOldWait(Memory& memory) {
        memory.waittype = (memory.waittype & 3) | 8;
    }
};
}
namespace {
void Require(bool ok, const std::string& text) { if (!ok) throw std::runtime_error(text); }
int failures = 0;
void Expect(bool ok, const char* text) {
    if (!ok) { ++failures; std::cerr << text << '\n'; }
}
class Machine : public PC88 {
public:
    void Out(unsigned port, unsigned value) { bus1.Out(port, value); }
};
struct ROMs {
    std::filesystem::path previous = std::filesystem::current_path(), directory;
    ROMs() {
        directory = std::filesystem::temp_directory_path() /
            ("m88-port40-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Require(std::filesystem::create_directory(directory), "create synthetic ROM directory");
        for (auto name : {"N88.ROM", "N80.ROM", "DISK.ROM", "FONT.ROM", "n80_2.rom", "n80_3.rom"}) {
            std::ofstream file(directory / name, std::ios::binary);
            std::array<char, 40960> zeros{};
            file.write(zeros.data(), zeros.size()); Require(bool(file), "write ROM fixture");
        }
        std::filesystem::current_path(directory);
    }
    ~ROMs() {
        std::filesystem::current_path(previous);
        for (auto name : {"N88.ROM", "N80.ROM", "DISK.ROM", "FONT.ROM", "n80_2.rom", "n80_3.rom"})
            std::filesystem::remove(directory / name);
        std::filesystem::remove(directory);
    }
};
}
int main() {
    try {
        ROMs roms; HeadlessDraw draw; DiskManager disks; TapeManager tape; Machine pc;
        Require(disks.Init(), "disk init");
        Require(pc.Init(&draw, &disks, &tape, roms.directory.string().c_str()), "core init");
        using C = PC8801::Config;
        C cfg{}; cfg.speed=100; cfg.mainsubratio=1; cfg.cpumode=C::msauto;
        cfg.sound=44100; cfg.flags=C::enablewait;
        for (auto mode : {C::N802, C::N88V1, C::N80V2}) for (int clock : {40, 80}) {
            cfg.basicmode=mode; cfg.clock=clock; pc.ApplyConfig(&cfg); pc.Reset();
            pc.Out(0x51,0);
            for (unsigned value : {78u,24u,15u,64u,0u}) pc.Out(0x50,value);
            pc.Out(0x51,0x20);
            auto& mem=*pc.GetMem1();
            const unsigned address=mode==C::N88V1 ? 0xc000 : 0x8000;
            auto wait=[&]() { return pc.GetCPU1()->GetWaits()[address >> MemoryManager::pagebits]; };
            for (unsigned blank : {0u,1u}) {
                mem.Out40(0,0); pc.Out(0x5c,0); mem.VRTC(0,blank);
                const int normal=wait();
                std::vector<uint8_t> saved(mem.GetStatusSize());
                Require(mem.SaveStatus(saved.data()), "save memory");
                PC8801::MemoryPort40TestAccess::SetSavedPort(saved,0x10);
                Require(mem.LoadStatus(saved.data()), "restore bit4 memory");
                const int restored=wait();
                if (mode==C::N802) Expect(restored==normal,"mkII legacy restore changed wait");
                // Preserve the existing PC-88 and SR policy; only mkII is in scope.
                if (mode==C::N88V1 && blank==0) Expect(restored!=normal,"PC-88 wait switching lost");
                pc.Out(0x5f,0); pc.Out(0x5c,0);
                Expect(wait()==(mode==C::N802 ? normal : restored),"bank reselection changed policy");
                mem.VRTC(0,blank);
                Expect(wait()==(mode==C::N802 ? normal : restored),"VRTC changed policy");
                if (mode==C::N802) {
                    std::vector<uint8_t> checkpoint,front; std::string error;
                    PC8801::MemoryPort40TestAccess::SetOldWait(mem);
                    Require(M88V::Snapshot::Capture(pc,cfg,123,front,checkpoint,error),error);
                    Require(M88V::Snapshot::Restore(pc,cfg,123,checkpoint,front,error),error);
                    Expect(wait()==normal,"mkII checkpoint retained obsolete fast wait");
                }
            }
        }
        std::cout << "Port40 memory-state regression: " << failures << " failures\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
