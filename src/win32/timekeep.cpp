// SPDX-License-Identifier: BSD-2-Clause
// Original TimeKeeper: Copyright (c) cisc 1998, 2001.
#include "timekeep.h"

TimeKeeper::TimeKeeper() {
    LARGE_INTEGER value, rate;
    if (QueryPerformanceFrequency(&rate) && rate.QuadPart > 0 && QueryPerformanceCounter(&value)) {
        frequency = rate.QuadPart;
        origin = value.QuadPart;
    } else origin = GetTickCount64();
    // Unsupported Windows versions reject the flag; use a normal timer there.
    constexpr DWORD highResolutionFlag = 0x00000002;
    timer = CreateWaitableTimerExW(nullptr, nullptr, highResolutionFlag, TIMER_ALL_ACCESS);
    highResolution = timer != nullptr;
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
}
TimeKeeper::~TimeKeeper() {
    if (timer) CloseHandle(timer);
}
uint32_t TimeKeeper::GetTime() {
    if (frequency) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return CounterUnits(uint64_t(now.QuadPart) - origin, frequency);
    }
    return uint32_t((GetTickCount64() - origin) * unit);
}
bool TimeKeeper::WaitUntil(uint32_t deadline, HANDLE interrupt) {
    for (;;) {
        if (interrupt && WaitForSingleObject(interrupt, 0) == WAIT_OBJECT_0) return false;
        int32_t remaining = int32_t(deadline - GetTime());
        if (remaining <= 0) return true;
        LARGE_INTEGER due;
        due.QuadPart = -int64_t(remaining) * 100; // 10 microseconds -> 100 ns.
        if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            DWORD result;
            if (interrupt) {
                HANDLE handles[] = {interrupt, timer};
                result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                if (result == WAIT_OBJECT_0) { CancelWaitableTimer(timer); return false; }
                if (result != WAIT_OBJECT_0 + 1) return false;
            } else if (WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0) return false;
        } else {
            DWORD ms = (DWORD(remaining) + unit - 1) / unit;
            if (interrupt) {
                if (WaitForSingleObject(interrupt, ms) != WAIT_TIMEOUT) return false;
            } else Sleep(ms);
        }
        // Check the clock again: never execute early because of timer rounding.
    }
}
