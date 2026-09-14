// SPDX-License-Identifier: BSD-2-Clause
// Original TimeKeeper: Copyright (c) cisc 1998, 2001.
#pragma once
#include <windows.h>
#include <cstdint>

// Units remain compatible with the sequencer: 1/100 millisecond.
class TimeKeeper {
public:
    enum { unit = 100 };
    TimeKeeper();
    ~TimeKeeper();
    TimeKeeper(const TimeKeeper&) = delete;
    TimeKeeper& operator=(const TimeKeeper&) = delete;
    uint32_t GetTime();
    bool WaitUntil(uint32_t deadline, HANDLE interrupt);
    bool HasHighResolutionTimer() const { return highResolution; }
    static uint32_t CounterUnits(uint64_t ticks, uint64_t frequency) {
        return uint32_t((ticks / frequency) * 100000 + (ticks % frequency) * 100000 / frequency);
    }
private:
    uint64_t frequency = 0, origin = 0;
    HANDLE timer = nullptr;
    bool highResolution = false;
};
