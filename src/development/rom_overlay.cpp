// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "rom_overlay.h"

#include <chrono>
#include <map>
#include <sstream>
#include <vector>
#include <fstream>
#include "zlib/zlib.h"

namespace fs = std::filesystem;
namespace M88V {
namespace {
std::string Lower(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return name;
}
}

RomOverlay::~RomOverlay() {
    // Only remove the exact fresh temporary directory owned by this object.
    if (!directory_.empty()) { std::error_code ec; fs::remove_all(directory_, ec); }
}

bool RomOverlay::Prepare(const std::string& directory, const std::string& preferredN80Rom,
                         BasicMode mode, std::string* error) {
    if (!directory_.empty()) { *error = "ROM overlay is already initialized"; return false; }
    std::error_code ec;
    const fs::path root = fs::absolute(fs::u8path(directory), ec);
    if (ec || !fs::is_directory(root, ec)) { *error = "ROM directory does not exist: " + directory; return false; }
    std::map<std::string, fs::path> files;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && Lower(entry.path().extension().u8string()) == ".rom") {
            files[Lower(entry.path().filename().u8string())] = entry.path();
        }
    }
    if (ec) { *error = "Cannot enumerate ROM directory: " + ec.message(); return false; }
    const auto has = [&](const char* name) { return files.count(name) != 0; };
    std::vector<std::string> missing;
    const auto require = [&](const char* name) { if (!has(name)) missing.emplace_back(name); };
    if (!has("pc88.rom")) require("n88.rom"); // Base set required by the original core.
    if (!has("font.rom") && has("font80.rom")) files["font.rom"] = files.at("font80.rom");
    if (!has("font.rom") && !has("kanji1.rom")) missing.emplace_back("FONT.ROM, FONT80.ROM, or KANJI1.ROM");

    if (IsPC80(mode)) {
        fs::path selected;
        if (!preferredN80Rom.empty()) {
            const auto it = files.find(Lower(preferredN80Rom));
            if (it != files.end()) selected = it->second;
            else missing.emplace_back(preferredN80Rom + " (requested N80 ROM)");
        } else {
            for (const char* name : {"n80_2.rom", "n80_11.rom", "n80_102.rom", "n80_101.rom"}) {
                if (has(name)) { selected = files.at(name); break; }
            }
            if (selected.empty()) missing.emplace_back("N80_2.ROM (or N80_11/N80_102/N80_101.ROM)");
        }
        if (!selected.empty()) {
            // An explicitly selected BIOS takes precedence over N80_2.ROM.
            files["n80_2.rom"] = selected;
            selectedN80Rom_ = selected.filename().u8string();
        }
        if (mode == PC8801::Config::N80V2) require("n80_3.rom");
    } else {
        if (!preferredN80Rom.empty()) { *error = "--n80-rom applies only to N802/N80V2"; return false; }
        if (!has("pc88.rom")) {
            if (mode == PC8801::Config::N80) require("n80.rom");
            else for (const char* name : {"n88_0.rom", "n88_1.rom", "n88_2.rom", "n88_3.rom"}) require(name);
            require("disk.rom");
        }
    }
    if (!missing.empty()) {
        std::ostringstream message;
        message << "ROM directory is incomplete for " << BasicModeName(mode) << ": " << root.u8string();
        for (const auto& item : missing) message << "\n - " << item;
        *error = message.str();
        return false;
    }
    const auto unique = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path candidate = fs::temp_directory_path(ec) / ("m88v-roms-" + std::to_string(unique));
    if (ec || !fs::create_directory(candidate, ec)) { *error = "Cannot create fresh ROM overlay: " + ec.message(); return false; }
    directory_ = candidate;
    for (const auto& item : files) {
        fingerprint_=crc32(fingerprint_,reinterpret_cast<const Bytef*>(item.first.data()),static_cast<uInt>(item.first.size()));
        std::ifstream rom(item.second,std::ios::binary);
        char buffer[16384];
        while(rom.read(buffer,sizeof(buffer)) || rom.gcount()) fingerprint_=crc32(fingerprint_,reinterpret_cast<const Bytef*>(buffer),static_cast<uInt>(rom.gcount()));
        if(!rom.eof()) { *error="Cannot fingerprint ROM";return false; }
        const fs::path destination = directory_ / item.first;
        fs::create_symlink(item.second, destination, ec);
        if (ec) {
            ec.clear();
            fs::copy_file(item.second, destination, fs::copy_options::none, ec);
        }
        if (ec) { *error = "Cannot prepare ROM alias: " + ec.message(); return false; }
    }
    return true;
}
} // namespace M88V
