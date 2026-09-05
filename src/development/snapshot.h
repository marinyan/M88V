// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#pragma once
#include "pc88/pc88.h"
#include "pc88/config.h"
#include <string>
#include <vector>
namespace M88V {
class StateCodec;
// Shared GUI/headless codec. ROMs/media remain external; reject a mismatching
// ROM set/configuration and validate the entire envelope before changing state.
class Snapshot {
public:
    static bool Capture(PC88& pc,const PC8801::Config& config,uint32_t romIdentity,
        const std::vector<uint8_t>& frontend,std::vector<uint8_t>& output,std::string& error);
    static bool Restore(PC88& pc,const PC8801::Config& config,uint32_t romIdentity,
        const std::vector<uint8_t>& input,std::vector<uint8_t>& frontend,std::string& error);
    static bool ReadFile(const std::string& path,std::vector<uint8_t>& bytes,std::string& error);
    static bool WriteFile(const std::string& path,const std::vector<uint8_t>& bytes,std::string& error,bool replace=false);
private:
    static void Runtime(PC88& pc,StateCodec& codec);
    static IDevice::TimeFunc Callback(PC88& pc,IDevice* device,uint32_t token);
};
}
