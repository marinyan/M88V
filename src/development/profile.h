// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#pragma once

#include "pc88/config.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace M88V {
using BasicMode = PC8801::Config::BASICMode;

inline bool IsPC80(BasicMode mode) {
    return mode == PC8801::Config::N802 || mode == PC8801::Config::N80V2;
}

inline const char* BasicModeName(BasicMode mode) {
    switch (mode) {
    case PC8801::Config::N80: return "N"; // PC-8801 N-BASIC, not PC-8001mkII
    case PC8801::Config::N802: return "N802";
    case PC8801::Config::N80V2: return "N80V2";
    case PC8801::Config::N88V1: return "N88V1";
    case PC8801::Config::N88V1H: return "N88V1H";
    case PC8801::Config::N88V2: return "N88V2";
    default: return "UNKNOWN";
    }
}

inline const char* MachineName(BasicMode mode) {
    if (mode == PC8801::Config::N802) return "PC-8001mkII";
    if (mode == PC8801::Config::N80V2) return "PC-8001mkIISR";
    return "PC-8801";
}

inline bool ParseBasicMode(std::string name, BasicMode* mode) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    for (BasicMode candidate : {PC8801::Config::N80, PC8801::Config::N802,
             PC8801::Config::N80V2, PC8801::Config::N88V1,
             PC8801::Config::N88V1H, PC8801::Config::N88V2}) {
        if (name == BasicModeName(candidate)) { *mode = candidate; return true; }
    }
    return false;
}
} // namespace M88V
