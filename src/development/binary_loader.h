// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#pragma once
#include <cstdint>
#include <string>
class PC88;
namespace M88V {
// Preserve the legacy launcher/stack for small images; large images reserve
// FFF0..FFFF for the 9-byte launcher and the initial CALL return address.
constexpr uint32_t BinaryLoadLimit = 0xfff0;
constexpr uint16_t BinaryLauncher(uint32_t end) { return end <= 0xeff0 ? 0xeff0 : 0xfff0; }
constexpr uint16_t BinaryStack(uint16_t launcher) { return launcher == 0xeff0 ? 0xf000 : 0xffff; }
bool ParseLoadAddress(const char* text, uint16_t* address);
// Caller must hold the frontend's emulation lock (or not yet be running).
bool LoadDevelopmentBinary(PC88& machine, const std::string& path,
                           uint16_t address, std::string* message);
}
