// SPDX-License-Identifier: BSD-2-Clause
#include "timekeep.h"
#include "frame_execution_budget.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

static void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main() {
    try {
        int carry = 0;
        int64_t executed = 0;
        for (int frame = 0; frame < 10000; ++frame) {
            FrameExecutionBudget budget(1703, 100, carry);
            Check(budget.NextBatch() == 1703-carry, "carry reduces next frame budget");
            while (int batch = budget.NextBatch()) {
                Check(batch > 0 && batch <= 1703, "bounded CPU frame");
                budget.Consume(batch + 3);
                executed += batch + 3;
            }
            carry = budget.Carry();
            Check(carry <= 3, "instruction overshoot is bounded");
            Check(executed == int64_t(frame + 1) * 1703 + carry,
                "instruction overshoot must not increase emulation speed");
        }
        for (int speed : {50, 100, 200}) {
            FrameExecutionBudget budget(1703, speed, 7);
            budget.Consume(103);
            Check(budget.DeadlineOffset() == 11000 / speed, "speed scales batch deadline");
        }
        FrameExecutionBudget carried(100, 100, 205);
        Check(carried.NextBatch() == 0 && carried.Carry() == 105, "excess carry survives frame boundary");
        Check(TimeKeeper::CounterUnits(3579545ULL*3600,3579545)==360000000,"non-divisible QPC frequency has no long-term drift");
        Check(TimeKeeper::CounterUnits(5000000000ULL*10,5000000000ULL)==1000000,"full 64-bit counter frequency");
        Check(TimeKeeper::CounterUnits(10000000ULL*50000,10000000)==uint32_t(5000000000ULL),"legacy time units wrap predictably");
        TimeKeeper clock;
        HANDLE cancel=CreateEvent(nullptr,FALSE,FALSE,nullptr);
        Check(cancel!=nullptr,"create cancellation event");
        uint32_t start=clock.GetTime();
        Check(clock.WaitUntil(start+200,cancel),"short deadline");
        Check(int32_t(clock.GetTime()-start)>=200,"wait never returns before deadline");
        SetEvent(cancel);
        Check(!clock.WaitUntil(clock.GetTime()+100000,cancel),"pending cancellation interrupts wait");
        std::thread wake([cancel]{ Sleep(10); SetEvent(cancel); });
        bool completed=clock.WaitUntil(clock.GetTime()+500000,cancel);
        wake.join();
        Check(!completed,"pause/shutdown wakes an active timer wait");
        Check(clock.WaitUntil(clock.GetTime()+100,cancel),"timer reusable after cancellation");
        CloseHandle(cancel);

        // Diagnostic measurement only: host load must not make timing tests flaky.
        for (bool precise : {false,true}) {
            std::vector<int32_t> error;
            for (int i=0;i<90;++i) {
                uint32_t deadline=clock.GetTime()+1664;
                if (precise) clock.WaitUntil(deadline,nullptr);
                else Sleep(16); // Previous sequencer truncated to whole milliseconds.
                error.push_back(int32_t(clock.GetTime()-deadline));
            }
            std::sort(error.begin(),error.end());
            std::cout<<(precise ? "Deadline wait" : "Sleep(16)")
                <<" error ms: min="<<error.front()/100.0<<" median="<<error[45]/100.0
                <<" p95="<<error[85]/100.0<<" max="<<error.back()/100.0<<'\n';
        }
        std::cout<<"High resolution timer: "<<clock.HasHighResolutionTimer()<<'\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
