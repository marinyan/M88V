// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator.
//	Copyright (C) cisc 1998, 2001.
// ---------------------------------------------------------------------------
//	$Id: sequence.cpp,v 1.3 2003/05/12 22:26:35 cisc Exp $

#include "headers.h"
#include "sequence.h"
#include "frame_execution_budget.h"
#include "pc88/pc88.h"
#include "misc.h"

#define LOGNAME "sequence"
#include "diag.h"

// ---------------------------------------------------------------------------
//	構築/消滅
//
Sequencer::Sequencer()
: hthread(0), execcount(0), vm(0)
{
}

Sequencer::~Sequencer()
{
	Cleanup();
}

// ---------------------------------------------------------------------------
//	初期化
//
bool Sequencer::Init(PC88* _vm)
{
	vm = _vm;
    if (!wakeEvent) wakeEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!wakeEvent) return false;

	active = false;
	shouldterminate = false;
	execcount = 0;
	clock = 1;
	speed = 100;

	skippedframe = 0;
	refreshtiming = 1;
	refreshcount = 0;

	if (!hthread)
	{
		hthread = (HANDLE)
			_beginthreadex(NULL, 0, ThreadEntry,
				reinterpret_cast<void*>(this), 0, &idthread);
	}
	return !!hthread;
}

// ---------------------------------------------------------------------------
//	後始末
//
bool Sequencer::Cleanup()
{
	if (hthread)
	{
		shouldterminate = true;
        SetEvent(wakeEvent);
		if (WAIT_TIMEOUT == WaitForSingleObject(hthread, 3000))
		{
			TerminateThread(hthread, 0);
		}
		CloseHandle(hthread);
		hthread = 0;
	}
	if (vm) {
		vm = nullptr;
	}
    if (wakeEvent) { CloseHandle(wakeEvent); wakeEvent = nullptr; }
	return true;
}

// ---------------------------------------------------------------------------
//	Core Thread
//
uint Sequencer::ThreadMain()
{
	time = keeper.GetTime();
	effclock = 100;
	executionCarry = 0;

	while (!shouldterminate)
	{
		if (active)
		{
			ExecuteAsynchronus();
		}
		else
		{
            WaitForSingleObject(wakeEvent, INFINITE);
			time = keeper.GetTime();
		}
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	サブスレッド開始点
//
uint CALLBACK Sequencer::ThreadEntry(void* arg)
{
	return reinterpret_cast<Sequencer*>(arg)->ThreadMain();
}

// ---------------------------------------------------------------------------
//	ＣＰＵメインループ
//	clock	ＣＰＵのクロック(0.1MHz)
//	length	実行する時間 (0.01ms)
//	eff		実効クロック
//
inline int Sequencer::Execute(long clk, long length, long eff)
{
	CriticalSection::Lock lock(cs);
	if (!active || shouldterminate || timingRevision != executionRevision || length <= 0) return 0;
	const int consumed = vm->Proceed(length, clk, eff);
	execcount += clk * consumed;
	return consumed;
}

bool Sequencer::WaitForDeadline(uint32 deadline)
{
	while (active && !shouldterminate && timingRevision == executionRevision) {
		if (keeper.WaitUntil(deadline, wakeEvent)) {
			if (active && !shouldterminate && timingRevision == executionRevision) return true;
			break;
		}
	}
	time = keeper.GetTime();
	executionCarry = 0;
	return false;
}

// ---------------------------------------------------------------------------
//	VSYNC 非同期
//
void Sequencer::ExecuteAsynchronus()
{
	int frameClock, frameSpeed, texec;
	{
		CriticalSection::Lock lock(cs);
		if (!active || shouldterminate) return;
		const uint32 revision = timingRevision;
		if (revision != executionRevision) {
			// A quick pause/resume must not replay the elapsed pause as CPU work.
			executionRevision = revision;
			time = keeper.GetTime();
			executionCarry = 0;
		}
		frameClock = clock;
		frameSpeed = speed;
		texec = frameClock > 0 ? Max(1, vm->GetFramePeriod()) : 0;
		if (frameClock <= 0) time = keeper.GetTime();
		vm->TimeSync();
	}
	if (frameClock <= 0)
	{
		executionCarry = 0;
		DWORD ms;
		int eclk = 0;
		do
		{
			const int consumed = frameClock ? Execute(-frameClock, 500, effclock)
				: Execute(effclock, 500 * frameSpeed / 100, effclock);
			if (consumed <= 0) {
				WaitForSingleObject(wakeEvent, 1);
				time = keeper.GetTime();
				return;
			}
			eclk += 5;
			ms = keeper.GetTime() - time;
		} while (ms < 1000);
		{
			CriticalSection::Lock lock(cs);
			if (!active || shouldterminate || timingRevision != executionRevision) return;
			vm->UpdateScreen();
		}

		effclock = Min((Min(1000, eclk) * effclock * 100 / ms) + 1, 10000);
	}
	else
	{
		const int twork = Max(1, texec * 100 / frameSpeed);
		// Pace frame execution against one absolute timeline. Charge instruction
		// overshoot to the next frame instead of gaining CPU time per call.
		FrameExecutionBudget budget(texec, frameSpeed, executionCarry);
		executionCarry = 0;
		while (budget.NextBatch() > 0) {
			const uint32 deadline = time + budget.DeadlineOffset();
			if (!WaitForDeadline(deadline)) return;
			const int consumed = Execute(frameClock, budget.NextBatch(),
				frameClock * frameSpeed / 100);
			if (consumed <= 0) {
				// The debugger can pause the core independently of the sequencer.
				// Yield before retrying so a zero-tick Proceed cannot busy-loop.
				WaitForSingleObject(wakeEvent, 1);
				time = keeper.GetTime();
				return;
			}
			budget.Consume(consumed);
		}
		executionCarry = budget.Carry();

		{
			CriticalSection::Lock lock(cs);
			if (active && !shouldterminate && timingRevision == executionRevision && ++refreshcount >= refreshtiming) {
				if (int32(keeper.GetTime() - time) < twork * 2 || ++skippedframe >= 20) {
					vm->UpdateScreen();
					refreshcount = skippedframe = 0;
				}
			}
		}
		if (!WaitForDeadline(time + twork)) return;
		time += twork;
		if (int32(keeper.GetTime() - time) > twork * 20) time = keeper.GetTime();
	}
}

// ---------------------------------------------------------------------------
//	実行クロックカウントの値を返し、カウンタをリセット
//
long Sequencer::GetExecCount()
{
	return execcount.exchange(0);
}

// ---------------------------------------------------------------------------
//	実行する
//
void Sequencer::Activate(bool a)
{
	CriticalSection::Lock lock(cs);
	if (active.exchange(a) != a) {
		++timingRevision;
		if (wakeEvent) SetEvent(wakeEvent);
	}
}

