// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "pc88/config.h"
#include <cstdint>
#include <vector>

class DeviceList;
namespace M88V::LegacySnapshot {
// The historical Windows format uses this 44-byte, little-endian header.
struct Header {
    char id[16];
    uint8_t major, minor;
    int8_t disk[2];
    int32_t datasize;
    PC8801::Config::BASICMode basicmode;
    int16_t clock;
    uint16_t erambanks, cpumode, mainsubratio;
    uint32_t flags, flag2;
};
static_assert(sizeof(Header) == 44, "Legacy snapshot header ABI changed");
bool Decode(const std::vector<uint8_t>& file, Header& header, std::vector<uint8_t>& payload);
// Does not call LoadStatus or change the machine configuration.
bool ValidateDevices(DeviceList& devices, const std::vector<uint8_t>& payload,
                     unsigned currentBanks, const Header& target, unsigned effectiveTargetBanks = UINT32_MAX);
}
