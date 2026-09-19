// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "pc88/diskmgr.h"
#include "pc88/fdc.h"
#include "pc88/config.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace PC8801 {
struct FdcRegressionAccess {
    static uint8* Buffer(FDC& f) { return f.buffer; }
    static void RestoreBuffer(FDC& f, uint8* p) { f.buffer = p; }
};
}
namespace {
using PC8801::FDC;
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << message << '\n'; }
}
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class Clock : public Scheduler {
    int Execute(int n) override { return n; }
    void Shorten(int) override {}
    int GetTicks() override { return 0; }
};
class IRQ : public Device {
public:
    IRQ() : Device(0) {}
    bool level = false;
    void IOCALL Set(uint, uint value) { level = value != 0; }
};
void Image(const std::filesystem::path& p, uint8 status, bool mfm) {
    D88::ImageHeader h{}; std::strcpy(h.title, "FDC regression");
    h.trackptr[0] = sizeof(h); h.disksize = sizeof(h) + 16 + 128;
    D88::SectorHeader s{}; s.id = {0,0,1,0}; s.sectors = 1; s.length = 128;
    s.status = status; s.density = mfm ? 0 : 0x40;
    std::array<char,128> data{}; data.fill(0x35);
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<char*>(&h), sizeof(h));
    out.write(reinterpret_cast<char*>(&s), sizeof(s));
    out.write(data.data(), data.size()); Require(bool(out), "write test D88");
}
struct Rig {
    Clock clock;
    IOBus bus;
    IRQ irq;
    DiskManager dm;
    FDC fdc{0};
    uint8* allocation = nullptr;
    Rig(const std::filesystem::path& p, bool wait) {
        Require(clock.Init(), "scheduler init"); clock.Proceed(1);
        Require(bus.Init(2), "bus init");
        bus.ConnectOut(0, &irq, static_cast<Device::OutFuncPtr>(&IRQ::Set));
        Require(dm.Mount(0, p.string().c_str(), false, 0, false), "mount test D88");
        Require(fdc.Init(&dm, &clock, &bus, 0, 1), "FDC init");
        allocation = PC8801::FdcRegressionAccess::Buffer(fdc);
        PC8801::Config config{}; if (!wait) config.flag2 |= PC8801::Config::fddnowait;
        fdc.ApplyConfig(&config);
    }
    ~Rig() {
        // Permit the pre-fix zero-sector regression to report a failure instead
        // of freeing an interior pointer. This does not affect command assertions.
        PC8801::FdcRegressionAccess::RestoreBuffer(fdc, allocation);
    }
    void Send(uint8 b) {
        Require((fdc.Status(0) & 0xc0) == 0x80, "FDC not ready for command/data");
        fdc.SetData(0,b);
    }
    void Command(std::initializer_list<uint8> bytes) { for (auto b : bytes) Send(b); }
    std::array<uint8,7> Result() {
        clock.Proceed(50000);
        std::array<uint8,7> r{};
        Require((fdc.Status(0) & 0xf0) == 0xd0, "expected result phase");
        for (auto& b : r) b = uint8(fdc.GetData(0));
        Check(!fdc.IsBusy(), "result must finish command");
        return r;
    }
};
void ErrorRead(const std::filesystem::path& p, bool wait, bool mfm, uint8 error) {
    Image(p,error,mfm); Rig r(p,wait);
    const auto op = uint8((mfm ? 0x40 : 0) | 6);
    r.Command({op,0,0,0,1,0,1,0x1b,128});
    r.clock.Proceed(1000);
    const bool dataPhase = (r.fdc.Status(0) & 0xf0) == 0xf0;
    Check(dataPhase == (error == 0 || error == 0xb0), "ID CRC / missing DAM must stop before data transfer");
    if (dataPhase) {
        for (int i=0;i<128;++i) Check(r.fdc.GetData(0) == 0x35, "read payload");
    }
    const auto result = r.Result();
    const auto st1 = error == 0xa0 || error == 0xb0 ? 0x20 : error == 0xf0 ? 1 : 0x80;
    const auto st2 = error == 0xb0 ? 0x20 : error == 0xf0 ? 1 : 0;
    Check(result[0] == 0x40 && result[1] == st1 && result[2] == st2, "Read Data status mismatch");
    r.Command({uint8((mfm ? 0x40 : 0) | 0x0a),0});
    const auto id = r.Result();
    Check(id[0] == (error == 0xa0 ? 0x40 : 0) && id[1] == (error == 0xa0 ? 0x20 : 0)
          && id[2] == 0 && id[5] == 1, "Read ID CRC status mismatch");
    if (error == 0xa0) {
        r.Command({uint8((mfm ? 0x40 : 0) | 5),0,0,0,1,0,1,0x1b,128});
        r.clock.Proceed(1000);
        for (unsigned i=0;i<128;++i) r.Send(0x99);
        const auto written = r.Result();
        Check(written[0] == 0x40 && written[1] == 0x20 && written[2] == 0,
              "Write Data must preserve ID CRC error instead of returning no-data");
    }
}
void Format(const std::filesystem::path& p, bool wait, unsigned bytes, bool zero) {
    Image(p,0,true); Rig r(p,wait);
    r.Command({0x4d,0,0,uint8(zero ? 0 : 3),0x1b,0xe5});
    const std::array<uint8,12> ids{0,0,7,0, 0,0,8,0, 0,0,9,0};
    if (!zero) {
        for (unsigned i=0;i<bytes;++i) r.Send(ids[i]);
        if (bytes < ids.size()) {
            r.fdc.TC(0);
            Check(!r.irq.level, "TC must withdraw byte-transfer IRQ while format completes");
        }
    }
    const auto result = r.Result();
    Check(PC8801::FdcRegressionAccess::Buffer(r.fdc) == r.allocation,
          "zero-sector format changed allocation pointer");
    if (zero) {
        Check(result[0] == 0x40 && result[1] == 0x80, "zero-sector format must fail safely");
        std::array<uint8,128> data{};
        Check(r.dm.GetFDU(0)->ReadSector(0x40, {0,0,1,0}, data.data()) == 0 && data[0] == 0x35,
              "zero-sector format changed original media");
    }
    else {
        Check(result[0] == 0 && result[1] == 0 && result[2] == 0, "format completion status");
        std::array<uint8,128> data{};
        for (unsigned i=0;i<3;++i) {
            const auto status = r.dm.GetFDU(0)->ReadSector(0x40, {0,0,uint8(7+i),0}, data.data());
            Check((status == 0) == (i < bytes/4), "format must use complete received IDs only");
            if (!status) Check(data[0] == 0xe5 && data[127] == 0xe5, "formatted fill bytes");
        }
        // NEC's command table marks Format's result CHRN as meaningless.
    }
    // A following command must still work; no stale formatting timer may fire.
    r.Command({0x04,0});
    Check((r.fdc.GetData(0) & 0x20) != 0, "next Sense Drive Status failed");
    r.clock.Proceed(50000); Check(!r.fdc.IsBusy(), "stale format event");
}
}
int main() {
    const auto p = std::filesystem::temp_directory_path() /
        ("m88v-fdc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".d88");
    try {
        for (bool wait : {false,true}) {
            for (bool mfm : {false,true}) for (uint8 error : {0,0xa0,0xb0,0xf0}) ErrorRead(p,wait,mfm,error);
            for (unsigned n : {0,1,3,4,5,7,8,11,12}) Format(p,wait,n,false);
            Format(p,wait,0,true);
        }
    } catch (const std::exception& e) { ++failures; std::cerr << e.what() << '\n'; }
    std::filesystem::remove(p);
    std::cout << failures << " failures\n"; return failures ? 1 : 0;
}
