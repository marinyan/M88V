// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator.
//	Copyright (C) cisc 1998, 2001.
// ---------------------------------------------------------------------------
//	$Id: sequence.h,v 1.1 2002/04/07 05:40:10 cisc Exp $

#pragma once

// ---------------------------------------------------------------------------

#include "types.h"
#include "critsect.h"
#include "timekeep.h"
#include <atomic>

class PC88;

// ---------------------------------------------------------------------------
//	Sequencer
//
//	VM 進行と画面更新のタイミングを調整し
//	VM 時間と実時間の同期をとるクラス
//
class Sequencer
{
public:
	Sequencer();
	~Sequencer();

	bool Init(PC88* vm);
	bool Cleanup();

	long GetExecCount();
	void Activate(bool active);

	void Lock() { cs.lock(); }
	void Unlock() { cs.unlock(); }

	void SetClock(int clk);
	void SetSpeed(int spd);
	void SetRefreshTiming(uint rti);

private:
	int Execute(long clock, long length, long ec);
	bool WaitForDeadline(uint32 deadline);
	void ExecuteAsynchronus();

	uint ThreadMain();
	static uint CALLBACK ThreadEntry(LPVOID arg);

	PC88* vm;

	TimeKeeper keeper;

	CriticalSection cs;
	HANDLE hthread;
    HANDLE wakeEvent = nullptr;
	uint idthread;

	std::atomic<int> clock{1};					// 1秒は何tick?
	std::atomic<int> speed{100};					//
	std::atomic<int> execcount{0};
	int effclock;
	uint32 time;
	int executionCarry = 0;
	std::atomic<uint32> timingRevision{0};
	uint32 executionRevision = 0;

	uint skippedframe;
	uint refreshcount;
	std::atomic<uint> refreshtiming{1};

	std::atomic<bool> shouldterminate = false;
	std::atomic<bool> active = false;
};

inline void Sequencer::SetClock(int clk)
{
	CriticalSection::Lock lock(cs);
	if (clock.exchange(clk) != clk) {
		++timingRevision;
		if (wakeEvent) SetEvent(wakeEvent);
	}
}

inline void Sequencer::SetSpeed(int spd)
{
	CriticalSection::Lock lock(cs);
	if (spd < 1) spd = 1;
	if (speed.exchange(spd) != spd) {
		++timingRevision;
		if (wakeEvent) SetEvent(wakeEvent);
	}
}

inline void Sequencer::SetRefreshTiming(uint rti)
{
	refreshtiming = rti ? rti : 1;
}

