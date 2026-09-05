// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#pragma once
#include <cstdint>
#include <string>
class Z80C;
namespace PC8801 { class Memory; }
namespace M88V {
struct Mapping {
    std::string space = "unmapped";
    uint32_t offset = 0;
    int wait = 0;
    const uint8_t* data = nullptr;
};
class MemoryInspector {
public:
    static Mapping At(PC8801::Memory& memory, Z80C& cpu, uint16_t address, bool write);
    static std::string Json(PC8801::Memory& memory, Z80C& cpu);
};
}
