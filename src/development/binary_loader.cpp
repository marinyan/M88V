// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "binary_loader.h"
#include "pc88/pc88.h"
#include "pc88/memory.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace M88V {
bool ParseLoadAddress(const char* text, uint16_t* address) {
    if (!text || !*text || !address || *text == '-') return false;
    std::string value(text);
    int base = 0;
    if (value.size() > 1 && (value.back() == 'H' || value.back() == 'h')) {
        value.pop_back(); base = 16;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, base);
    if (errno || end == value.c_str() || *end || parsed > 0xffff) return false;
    *address = static_cast<uint16_t>(parsed);
    return true;
}

bool LoadDevelopmentBinary(PC88& machine, const std::string& path,
                           uint16_t address, std::string* message) {
    auto fail = [&](const std::string& error) {
        if (message) *message = error;
        return false;
    };
    try {
        std::ifstream input(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        if (!input) return fail("Cannot open BIN: " + path);
        const auto size = input.tellg();
        if (size <= 0) return fail("BIN is empty or its size cannot be read");
        if (uint64_t(address) + static_cast<uint64_t>(size) > BinaryLoadLimit)
            return fail("BIN overlaps the FFF0H launcher");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
            return fail("Cannot read complete BIN: " + path);
        uint8_t* ram = machine.GetMem1()->GetRAM();
        std::memcpy(ram + address, bytes.data(), bytes.size());
        const auto launcherAddress = BinaryLauncher(uint32_t(address) + uint32_t(bytes.size()));
        const auto stack = BinaryStack(launcherAddress);
        const uint8_t launcher[] = {0x31, uint8_t(stack), uint8_t(stack >> 8), 0xcd,
            uint8_t(address), uint8_t(address >> 8), 0xc3, 0x00, 0x00};
        std::memcpy(ram + launcherAddress, launcher, sizeof(launcher));
        // V1H/V2 may map independent text RAM at F000H. Install the high
        // launcher in that CPU-visible bank as well; the BIN remains in RAM.
        if (machine.GetMem1()->GetRdBank(launcherAddress) == PC8801::Memory::mTV)
            std::memcpy(machine.GetMem1()->GetTVRAM() + (launcherAddress & 0xfff), launcher, sizeof(launcher));
        machine.GetCPU1()->SetPC(launcherAddress);
        if (message) *message = "BIN loaded";
        return true;
    } catch (const std::exception& error) { return fail(error.what()); }
}
}
