// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <algorithm>
#include <cstdint>

// Scheduler ticks are 10 microseconds. Credit the actual executed ticks,
// including instruction overshoot, against the remaining frame budget.
class FrameExecutionBudget {
public:
    FrameExecutionBudget(int frameTicks, int speedPercent, int carry)
        : frame(std::max(1, frameTicks)), speed(std::max(1, speedPercent)),
          completed(std::max(0, carry)) {}
    int NextBatch() const { return std::max(0, frame - completed); }
    uint32_t DeadlineOffset() const { return uint32_t(int64_t(completed) * 100 / speed); }
    void Consume(int actualTicks) { completed += std::max(0, actualTicks); }
    int Carry() const { return std::max(0, completed - frame); }
private:
    int frame, speed, completed;
};
