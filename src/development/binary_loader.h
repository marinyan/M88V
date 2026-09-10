// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#pragma once
#include <cstdint>
#include <string>
class PC88;
namespace M88V {
bool ParseLoadAddress(const char* text, uint16_t* address);
// Caller must hold the frontend's emulation lock (or not yet be running).
bool LoadDevelopmentBinary(PC88& machine, const std::string& path,
                           uint16_t address, std::string* message);
}
