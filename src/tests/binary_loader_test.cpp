// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "development/binary_loader.h"
#include "headless/headless_machine.h"
#include "pc88/memory.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <algorithm>

static void Check(bool ok, const std::string& error) {
    if (!ok) throw std::runtime_error(error);
}

int main(int argc, char** argv) {
    const auto path = std::filesystem::temp_directory_path() /
        ("m88v-bin-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    try {
        uint16_t address = 0;
        for (const char* value : {"0xC000", "C000H", "c000h", "49152"}) {
            Check(M88V::ParseLoadAddress(value, &address) && address == 0xc000, "address parse failed");
        }
        for (const char* value : {"", "-1", "65536", "10000H", "0x", "C000Hx", "0xC000!"})
            Check(!M88V::ParseLoadAddress(value, &address), "invalid address accepted");
        Check(!M88V::ParseLoadAddress(nullptr, &address), "null accepted");
        PC88 empty;
        std::string error;
        Check(!M88V::LoadDevelopmentBinary(empty, path.u8string(), 0xc000, &error), "missing BIN accepted");
        { std::ofstream file(path, std::ios::binary); }
        Check(!M88V::LoadDevelopmentBinary(empty, path.u8string(), 0xc000, &error), "empty BIN accepted");
        // DI; LD A,5A; LD (E010),A; HALT. Must not execute until the launcher is installed.
        const unsigned char program[] = {0xf3, 0x3e, 0x5a, 0x32, 0x10, 0xe0, 0x76};
        { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(program), sizeof(program)); }
        Check(!M88V::LoadDevelopmentBinary(empty, path.u8string(), 0xffef, &error), "overlapping BIN accepted");
        std::cout << "BIN address and rejection tests: PASS\n";
        if (argc == 2) {
            for (auto mode : {PC8801::Config::N802, PC8801::Config::N80V2, PC8801::Config::N80,
                    PC8801::Config::N88V1, PC8801::Config::N88V1H, PC8801::Config::N88V2}) {
                auto storage = std::make_unique<HeadlessMachine>();
                auto& pc = *storage;
                Check(pc.Initialize(argv[1], "", mode, &error), error);
                const uint16_t start = M88V::IsPC80(mode) ? 0xc000 : 0xb000;
                Check(M88V::LoadDevelopmentBinary(pc, path.u8string(), start, &error), error);
                Check(pc.GetCPU1()->GetPC() == 0xeff0, "launcher PC mismatch");
                Check(pc.RunFrames(1, &error), error);
                Check(pc.GetMem1()->GetRAM()[0xe010] == 0x5a, "shared GUI BIN loader did not run");
                // Exercise both frontends at the old boundary and the new exact limit.
                for (bool shared : {false, true}) for (uint32_t end : {0xeff0u, 0xf100u, 0xfff0u}) {
                    auto largeStorage = std::make_unique<HeadlessMachine>();
                    auto& largePC = *largeStorage;
                    Check(largePC.Initialize(argv[1], "", mode, &error), error);
                    std::vector<uint8_t> large(end - start, 0xa5);
                    std::copy(std::begin(program), std::end(program), large.begin());
                    { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(large.data()), large.size()); }
                    Check(shared ? M88V::LoadDevelopmentBinary(largePC, path.u8string(), start, &error) : largePC.LoadBinary(path.u8string(), start, true, &error), error);
                    Check(largePC.GetCPU1()->GetPC() == M88V::BinaryLauncher(end), "large launcher PC mismatch");
                    Check(largePC.RunFrames(1, &error), error);
                    Check(largePC.GetMem1()->GetRAM()[0xe010] == 0x5a, "large BIN did not execute");
                    for (uint32_t p = start; p < end; ++p)
                        if (p != 0xe010) Check(largePC.GetMem1()->GetRAM()[p] == large[p-start], "launcher/stack corrupted loaded bytes");
                }
                // Both loaders reject overlap; raw headless loads retain the 64 KiB bound.
                Check(!pc.LoadBinary(path.u8string(), start+1, true, &error), "headless overlap accepted");
                { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(program), sizeof(program)); }
                std::cout << M88V::BasicModeName(mode) << " shared GUI BIN startup: PASS\n";
            }
        }
        std::filesystem::remove(path);
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
