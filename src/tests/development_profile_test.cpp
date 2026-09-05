// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "development/profile.h"
#include "development/rom_overlay.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

int main() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto fixture = fs::temp_directory_path() / ("m88v-profile-test-" + std::to_string(stamp));
    Check(fs::create_directory(fixture), "Fixture must be a fresh directory");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{fixture};
    try {
        M88V::BasicMode mode;
        for (const auto& entry : {std::pair<const char*, int>{"n", 0x00}, {"N802", 0x02},
                {"n80v2", 0x12}, {"N88V1", 0x20}, {"n88v1h", 0x21}, {"N88V2", 0x31}}) {
            Check(M88V::ParseBasicMode(entry.first, &mode) && mode == entry.second, "Mode parsing mismatch");
        }
        Check(!M88V::ParseBasicMode("n80", &mode), "Ambiguous n80 alias must not select a wrong machine");
        Check(!M88V::IsPC80(PC8801::Config::N80), "N-BASIC is a PC-8801 profile");
        Check(!M88V::ParseBasicMode("n88v2cd", &mode), "Unsupported CD profile must be rejected");

        // Synthetic fixtures test selection only; they are not executable BIOSes.
        std::ofstream(fixture / "Pc88.RoM", std::ios::binary).put(0x88);
        std::ofstream(fixture / "FoNt.RoM", std::ios::binary).put(0x46);
        std::string error;
        for (auto pc88 : {PC8801::Config::N80, PC8801::Config::N88V1,
                         PC8801::Config::N88V1H, PC8801::Config::N88V2}) {
            M88V::RomOverlay roms;
            Check(roms.Prepare(fixture.u8string(), "", pc88, &error), "PC-88 must not require a PC-80 BIOS");
            Check(roms.SelectedN80Rom().empty(), "PC-88 must not report an N80_2 BIOS");
        }
        {
            M88V::RomOverlay roms;
            Check(!roms.Prepare(fixture.u8string(), "", PC8801::Config::N802, &error), "Missing N80 BIOS accepted");
        }
        std::ofstream(fixture / "N80_2.ROM", std::ios::binary).put(0x22);
        std::ofstream(fixture / "N80_11.ROM", std::ios::binary).put(0x11);
        {
            M88V::RomOverlay roms;
            Check(roms.Prepare(fixture.u8string(), "n80_11.rom", PC8801::Config::N802, &error), "Explicit alias failed");
            std::ifstream alias(fs::u8path(roms.Directory()) / "n80_2.rom", std::ios::binary);
            Check(alias.get() == 0x11, "Explicit BIOS did not override the default BIOS");
        }
        {
            M88V::RomOverlay roms;
            Check(!roms.Prepare(fixture.u8string(), "", PC8801::Config::N80V2, &error), "SR mode needs N80_3.ROM");
        }
        std::ofstream(fixture / "N80_3.ROM", std::ios::binary).put(0x33);
        {
            M88V::RomOverlay roms;
            Check(roms.Prepare(fixture.u8string(), "", PC8801::Config::N80V2, &error), "SR profile rejected");
        }
        std::ifstream original(fixture / "N80_2.ROM", std::ios::binary);
        Check(original.get() == 0x22, "Original ROM was modified");
        std::cout << "development profiles / ROM aliases: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
