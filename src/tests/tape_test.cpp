// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "headers.h"
#include "pc88/tapemgr.h"
#include "pc88/sio.h"
#include "common/status.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Clock : Scheduler {
    Clock() { Init(); Proceed(1); }
    int Execute(int ticks) override { return ticks; }
    void Shorten(int) override {}
    int GetTicks() override { return 0; }
};
std::vector<unsigned char> Read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
int main() {
    try {
        fs::path dir = fs::current_path() / "tape-test-output";
        fs::create_directories(dir);
        auto empty = (dir / "empty.t88").string();
        auto image = (dir / "recorded.t88").string();
        auto raw = (dir / "recorded.cmt").string();
        Clock clock;
        IOBus bus;
        Check(bus.Init(16), "bus init");
        TapeManager tape;
        tape.Init(&clock, &bus, 0);
        Check(TapeManager::CreateEmpty(empty.c_str()), "create empty");
        Check(Read(empty).size() == 34, "empty T88 header/version/end");
        Check(tape.Open(empty.c_str()) && tape.IsOpen(), "open empty");
        Check(tape.Rewind() && tape.GetPos() == 0 && tape.SeekEnd(), "empty controls");
        PC8801::SIO sio(0);
        sio.Init(&bus, 1, 2);
        bus.ConnectOut(0, &sio, static_cast<Device::OutFuncPtr>(&PC8801::SIO::AcceptData));
        sio.SetTapeOutput(&tape);
        uint readActivity = statusdisplay.GetMediaActivity(2);
        uint writeActivity = statusdisplay.GetMediaActivity(3);
        sio.Reset(); sio.SetControl(0, 0xce); sio.SetControl(0, 1);
        sio.SetData(0, 99); // Motor off must not record.
        Check(!tape.HasRecording(), "motor gate");
        Check(statusdisplay.GetMediaActivity(3) == writeActivity, "no false recording activity");
        statusdisplay.FDAccess(0, false, true);
        statusdisplay.FDAccess(0, false, false);
        Check(statusdisplay.GetFDState(0) == 0 && statusdisplay.GetMediaActivity(0) > 0, "short disk pulse retained");
        tape.Out30(0, 0x18); // CMT 1200, mark carrier, motor on.
        clock.Proceed(100000); // 4800 ticks of leader.
        sio.SetData(0, 0x3a);
        sio.SetData(0, 0x80);
        Check(statusdisplay.GetMediaActivity(3) == writeActivity + 2, "recording activity counter");
        tape.Out30(0, 0x10);
        Check(tape.SaveRecording(image.c_str()), "save T88");
        Check(tape.SaveRecording(raw.c_str(), true), "save CMT");
        Check(Read(raw) == std::vector<unsigned char>({0x3a, 0x80}), "USART output bytes");
        auto bytes = Read(image);
        Check(bytes[30] == 3 && bytes[31] == 1, "mark tag");
        Check(bytes[42] == 1 && bytes[43] == 1 && bytes[56] == 0xcc && bytes[57] == 1, "data type 1200 baud");
        Check(tape.Open(image.c_str()), "roundtrip open");
        Check(tape.Carrier(), "leader carrier");
        tape.Motor(true); clock.Proceed(10000); tape.Motor(false);
        uint stopped = tape.GetPos();
        Check(stopped == 480, "play position before pause");
        clock.Proceed(20000); Check(tape.GetPos() == stopped, "motor pause");
        tape.RequestData(); Check(tape.GetPos() == stopped, "no read acceleration while stopped");
        tape.Motor(true); clock.Proceed(10000); tape.Motor(false);
        Check(tape.GetPos() == stopped + 480, "motor resumes from remaining time");
        Check(tape.Rewind() && tape.GetPos() == 0, "rewind resets position");
        sio.SetControl(0, 4); // Receive only; replay the generated image through USART.
        tape.Motor(true);
        clock.Proceed(100000 + 916);
        Check((sio.GetStatus() & 2) && sio.GetData() == 0x3a, "first replayed byte");
        clock.Proceed(916);
        Check((sio.GetStatus() & 2) && sio.GetData() == 0x80, "second replayed byte");
        Check(statusdisplay.GetMediaActivity(2) == readActivity + 2, "playback activity counter");
        clock.Proceed(1000);
        Check(!(sio.GetStatus() & 2), "end does not repeat data");
        tape.Motor(false);
        Check(tape.SeekEnd() && tape.GetPos() == 4888, "end position");
        // Truncation at every byte boundary must fail without unmounting the old tape.
        for (size_t n = 0; n < bytes.size(); ++n) {
            auto broken = (dir / "broken.t88").string();
            { std::ofstream f(broken, std::ios::binary); f.write((char*)bytes.data(), n); }
            Check(!tape.Open(broken.c_str()) && tape.IsOpen(), "truncated input retains tape");
        }
        Check(tape.Close() && !tape.IsOpen() && tape.GetPos() == 0, "eject");
        tape.ClearRecording();
        sio.SetControl(0, 1);
        tape.Out30(0, 0x28); // RS-232 must not record.
        sio.SetData(0, 33);
        Check(!tape.HasRecording(), "RS232 gate");
        tape.Out30(0, 8);
        sio.SetControl(0, 0); sio.SetData(0, 34);
        Check(!tape.HasRecording(), "transmit disable gate");
        sio.SetControl(0, 1);
        for (unsigned n=0; n<32769; ++n) sio.SetData(0, n);
        tape.Motor(false);
        Check(!tape.SaveRecording((dir / "missing" / "no.t88").string().c_str()), "write failure");
        Check(tape.RecordingDirty(), "failure preserves dirty data");
        Check(tape.SaveRecording(image.c_str()), "split data tags");
        Check(tape.Open(image.c_str()), "large output reload");
        Check(tape.SaveRecording(raw.c_str(), true) && Read(raw).size() == 32769, "large CMT export");
        tape.Close(); tape.ClearRecording();
        std::cout << "Tape creation, USART recording, export, controls and malformed-input tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
