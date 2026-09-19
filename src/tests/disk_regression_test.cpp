// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "pc88/diskmgr.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

// Only fault injection needs private access; assertions use saved media / FDU output.
struct DiskManagerTestAccess {
    static FileIO* File(DiskManager& dm, unsigned dr) {
        auto& d = dm.drive[dr];
        return d.holder->GetDisk(d.index);
    }
};

namespace {
int failures = 0;
void Check(bool ok, const char* text) {
    if (!ok) { ++failures; std::cerr << text << '\n'; }
}
void Require(bool ok, const char* text) {
    if (!ok) throw std::runtime_error(text);
}
void Image(const std::filesystem::path& path) {
    D88::ImageHeader h{};
    std::strcpy(h.title, "regression");
    h.disksize = sizeof(h) + sizeof(D88::SectorHeader) + 256;
    h.trackptr[0] = sizeof(h);
    D88::SectorHeader s{};
    s.id = {0, 0, 1, 1}; s.sectors = 1; s.length = 256;
    std::array<char, 256> data{};
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&h), sizeof(h));
    out.write(reinterpret_cast<const char*>(&s), sizeof(s));
    out.write(data.data(), data.size());
    Require(bool(out), "create synthetic D88");
}
std::vector<char> ReadFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void SavedByte(const std::filesystem::path& p, unsigned char expected) {
    DiskManager dm;
    Require(dm.Mount(0, p.string().c_str(), true, 0, false), "remount saved D88");
    std::array<uint8, 256> data{};
    Require(dm.GetFDU(0)->ReadSector(0x40, {0, 0, 1, 1}, data.data()) == 0,
            "read saved sector");
    Check(data[0] == expected, "saved sector does not contain pending write");
}
void WriteFailure(const std::filesystem::path& p, bool seekFailure, bool retryUpdate) {
    Image(p);
    const auto before = ReadFile(p);
    DiskManager dm;
    Require(dm.Mount(0, p.string().c_str(), false, 0, false), "mount writable D88");
    std::array<uint8, 256> data{}; data.fill(0x5a);
    Require(dm.GetFDU(0)->WriteSector(0x40, {0, 0, 1, 1}, data.data(), false) == 0,
            "write guest sector");
    auto* file = DiskManagerTestAccess::File(dm, 0);
    if (seekFailure) file->Close();
    else Require(file->Reopen(FileIO::readonly), "inject write failure");
    dm.Update();
    Check(ReadFile(p) == before, "failed update must not alter disk image");
    Require(file->Reopen(), "restore writable file");
    if (retryUpdate) dm.Update();
    Require(dm.Unmount(0), "save pending data after recovery");
    SavedByte(p, 0x5a);
}
void UnmountFailure(const std::filesystem::path& p) {
    Image(p);
    DiskManager dm;
    Require(dm.Mount(0, p.string().c_str(), false, 0, false), "mount failure fixture");
    std::array<uint8, 256> data{}; data.fill(0x6b);
    Require(dm.GetFDU(0)->WriteSector(0x40, {0, 0, 1, 1}, data.data(), false) == 0,
            "write failure fixture");
    Require(DiskManagerTestAccess::File(dm, 0)->Reopen(FileIO::readonly), "make backing file unwritable");
    dm.Update();
    Check(!dm.Unmount(0), "unmount must report an outstanding save failure");
}
void FormatSecondDrive(const std::filesystem::path& first, const std::filesystem::path& second) {
    Image(first); Image(second);
    const auto original = ReadFile(first);
    DiskManager dm;
    Require(dm.Mount(0, first.string().c_str(), false, 0, false), "mount drive 1");
    Require(dm.Mount(1, second.string().c_str(), false, 0, false), "mount drive 2");
    Require(dm.FormatDisk(1), "format drive 2");
    dm.Update();
    Require(dm.Unmount(1) && dm.Unmount(0), "save formatted disk");
    Check(ReadFile(first) == original, "format drive 2 changed drive 1");
    SavedByte(second, 0xc9);
    Require(dm.Mount(1, second.string().c_str(), true, 0, false), "reopen formatted disk");
    auto* fdu = dm.GetFDU(1);
    // 2D media uses every other cylinder of the core's 80-cylinder drive.
    fdu->Seek(78);
    std::array<uint8, 256> data{};
    Check(fdu->ReadSector(0x41, {39, 1, 16, 1}, data.data()) == 0,
          "last formatted sector was not persisted");
    Check(data[0] == 0xff && data[255] == 0xff, "last sector contents");
}
void MountFailures(const std::filesystem::path& root) {
    const auto first = root / "first.d88", second = root / "second.d88";
    Image(first); Image(second);
    const auto bad = root / "bad.d88", list = root / "bad.m3u";
    { std::ofstream out(bad, std::ios::binary); out << "not a disk"; }
    { std::ofstream out(list); out << "missing.d88\n"; }
    DiskManager dm;
    Require(dm.Mount(0, first.string().c_str(), false, 0, false), "mount preserved drive 1");
    Require(dm.Mount(1, second.string().c_str(), false, 0, false), "mount preserved drive 2");
    std::array<uint8, 256> pending{}; pending.fill(0x68);
    Require(dm.GetFDU(0)->WriteSector(0x40, {0,0,1,1}, pending.data(), false) == 0,
            "dirty existing disk before failed mount");
    const auto expectPreserved = [&](const std::filesystem::path& path, int index) {
        Check(!dm.Mount(0, path.string().c_str(), false, index, false), "invalid mount succeeded");
        Check(dm.GetFDU(0)->IsMounted() && dm.GetCurrentDisk(0) == 0,
              "failed mount ejected original disk");
        std::array<uint8, 256> data{};
        Check(dm.GetFDU(0)->ReadSector(0x40, {0,0,1,1}, data.data()) == 0 && data[0] == 0x68,
              "failed mount lost pending guest data");
        Check(dm.GetFDU(1)->IsMounted(), "failed mount disturbed other drive");
    };
    expectPreserved(root / "missing.d88", 0);
    expectPreserved(bad, 0);
    expectPreserved(list, 0);
    expectPreserved(second, 4);
    Image(bad);
    { std::fstream out(bad, std::ios::binary | std::ios::in | std::ios::out);
      out.seekp(0x1b); out.put(char(0x30)); }
    expectPreserved(bad, 0); // Valid container, unsupported media type.
    Require(dm.Unmount(0), "save retained pending data");
    SavedByte(first, 0x68);
    Require(dm.Mount(0, first.string().c_str(), false, 0, false), "valid replacement still works");
    Require(dm.Mount(0, first.string().c_str(), false, 0, false), "same-image remount still works");
    std::filesystem::remove(bad); std::filesystem::remove(list);
}
void ReadOnlyImage(const std::filesystem::path& p) {
    Image(p);
    const auto original = ReadFile(p);
#ifdef _WIN32
    Require(SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "set host read-only attribute");
    {
        DiskManager dm;
        Require(dm.Mount(0, p.string().c_str(), false, 0, false), "read-only fallback mount");
        Check((dm.GetFDU(0)->SenceDeviceStatus() & 0x40) != 0, "read-only fallback lacks write protection");
        std::array<uint8, 256> data{}; data.fill(0x99);
        Check(dm.GetFDU(0)->WriteSector(0x40, {0,0,1,1}, data.data(), false) != 0,
              "guest write accepted on read-only backing file");
        Require(dm.Unmount(0), "unmount read-only image without pending writes");
    }
    Require(SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL) != 0, "restore host attributes");
#else
    DiskManager dm;
    Require(dm.Mount(0, p.string().c_str(), true, 0, false), "explicit read-only mount");
    Check((dm.GetFDU(0)->SenceDeviceStatus() & 0x40) != 0, "read-only disk lacks write protection");
    Require(dm.Unmount(0), "unmount read-only disk");
#endif
    Check(ReadFile(p) == original, "read-only image changed");
}
void Diagnostic(bool mfm, bool deleted, bool mixed) {
    FloppyDisk disk;
    Require(disk.Init(FloppyDisk::MD2D, false), "create diagnostic disk");
    disk.Seek(0);
    const unsigned density = mfm ? 0x40 : 0;
    if (mixed) {
        auto* wrong = disk.AddSector(128);
        Require(wrong != nullptr, "add mismatched-density sector");
        wrong->id = {0, 0, 1, 0}; wrong->flags = density ^ 0x40; wrong->size = 128;
        std::memset(wrong->image, 0x11, 128);
    }
    auto* sec = disk.AddSector(128);
    Require(sec != nullptr, "add diagnostic sector");
    sec->id = {0, 0, 2, 0}; sec->size = 128;
    sec->flags = density | (deleted ? FloppyDisk::deleted : 0);
    std::memset(sec->image, 0x73, 128);
    PC8801::FDU fdu;
    fdu.Mount(&disk);
    alignas(void*) std::array<uint8, 0x4000> data{};
    unsigned size = 0;
    Require(fdu.MakeDiagData(density, data.data(), &size) == 0, "generate Read Diagnostic data");
    unsigned start = mfm ? 146 : 97;
    if (mixed) start += mfm ? (49 + 128) * 2 : (94 + 128) / 2;
    const unsigned mark = start + (mfm ? 59 : 30);
    Check(data[mark] == (deleted ? 0xf8 : 0xfb), "wrong diagnostic data address mark / offset");
    Check(data[mark + 1] == 0x73 && data[mark + 128] == 0x73,
          "diagnostic sector payload shifted");
    if (mixed) {
        unsigned preamble = mfm ? 146 : 97;
        Check(std::all_of(data.begin() + preamble, data.begin() + start,
                          [](uint8 b) { return b == 0; }), "mismatched-density span overwritten");
    }
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("m88v-disk-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try {
        for (bool seekFailure : {false, true})
            for (bool retry : {false, true}) WriteFailure(root / "write.d88", seekFailure, retry);
        UnmountFailure(root / "write.d88");
        FormatSecondDrive(root / "first.d88", root / "second.d88");
        MountFailures(root);
        ReadOnlyImage(root / "write.d88");
        for (bool mfm : {false, true})
            for (bool deleted : {false, true})
                for (bool mixed : {false, true}) Diagnostic(mfm, deleted, mixed);
    } catch (const std::exception& e) { ++failures; std::cerr << e.what() << '\n'; }
    for (const char* name : {"write.d88", "first.d88", "second.d88"})
        std::filesystem::remove(root / name);
    std::filesystem::remove(root);
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
