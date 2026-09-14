// ---------------------------------------------------------------------------
//	M88 - PC-8801 Series Emulator
//	Copyright (C) cisc 1999.
// ---------------------------------------------------------------------------
//	Implementation of USART(uPD8251AF)
// ---------------------------------------------------------------------------
//	$Id: sio.cpp,v 1.6 2001/02/21 11:57:57 cisc Exp $

#include "headers.h"
#include "schedule.h"
#include "sio.h"
#include "tapemgr.h"

#define LOGNAME "sio"
#include "diag.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	構築・破棄
//
SIO::SIO(const ID& id)
: Device(id)
{
}

SIO::~SIO()
{
}

// ---------------------------------------------------------------------------
//	初期化
//
bool SIO::Init(IOBus* _bus, uint _prxrdy, uint _preq)
{
	bus = _bus, prxrdy = _prxrdy, prequest = _preq;
	LOG0("SIO::Init\n");

	return true;
}

// ---------------------------------------------------------------------------
//	りせっと
//
void SIO::Reset(uint, uint)
{
	rxen = txen = false;
	datalen = 8; parity = none; stop = 3; data = 0; clock = 1200; outputType = 0xcc;
	if (tapeOutput) tapeOutput->SetSerial(false, outputType);
	mode = clear;
	status = TXRDY | TXE;
	HostClear();
	baseclock = 1200 * 64;
}

// ---------------------------------------------------------------------------
//	こんとろーるぽーと
//
void IOCALL SIO::SetControl(uint, uint d)
{
	LOG1("[%.2x] ", d);
	
	switch (mode)
	{
	case clear:
		outputType = d & (d & 0x10 ? 0xfc : 0xdc);
		// Mode Instruction
		if (d & 3)
		{
			// Asynchronus mode
			mode = async;
			// b7 b6 b5 b4 b3 b2 b1 b0
			// STOP  EP PE CHAR  RATE
			static const int clockdiv[] = { 1, 1, 16, 64 };
			clock = baseclock / clockdiv[d & 3];
			datalen = 5 + ((d >> 2) & 3);
			parity = d & 0x10 ? (d & 0x20 ? even : odd) : none;
			stop = (d >> 6) & 3;
			LOG4("Async: %d baud, Parity:%c Data:%d Stop:%s\n", clock, parity, datalen, stop==3 ? "2" : stop==2 ? "1.5" : "1");
		}
		else
		{
			// Synchronus mode
			mode = sync1;
			clock = 0;
			parity = d & 0x10 ? (d & 0x20 ? even : odd) : none;
			LOG2("Sync: %d baud, Parity:%c / ", clock, parity);
		}
		break;
		
	case sync1:
		mode = sync2;
		break;

	case sync2:
		mode = sync;
		LOG0("\n");
		break;

	case async:
	case sync:
		// Command Instruction
		// b7 - enter hunt mode
		// b6 - internal reset
		if (d & 0x40)
		{
			// Reset!
			LOG0(" Internal Reset!\n");
			mode = clear; rxen = txen = false;
			if (hostEnabled) HostClear();
			if (tapeOutput) tapeOutput->SetSerial(false, outputType);
			break;
		}
		// b5 - request to send
		// b4 - error reset
		if (d & 0x10)
		{
			LOG0(" ERRCLR"); 
			status &= ~(PE | OE | FE);
		}
		// b3 - send break charactor
		if (d & 8)
		{
			LOG0(" SNDBRK");
		}
		// b2 - receive enable 
		rxen = (d & 4) != 0;
		// b1 - data terminal ready
		// b0 - send enable
		txen = (d & 1) != 0;
		if (tapeOutput) tapeOutput->SetSerial(txen && !hostEnabled, outputType);
		if (hostEnabled) PumpHost();

		LOG2(" RxE:%d TxE:%d\n", rxen, txen);
		break;
	default:
		LOG1("internal error? <%d>\n", mode);
		break;
	}
}

// ---------------------------------------------------------------------------
//	でーたせっと
//
void IOCALL SIO::SetData(uint, uint d)
{
	LOG1("<%.2x ", d);
    if (!txen) return;
    d &= (1u << datalen) - 1;
    if (hostEnabled) {
        if (hostTx.size() < HostCapacity) hostTx.push_back(uint8(d));
        else ++hostDropped;
        PumpHost();
    } else if (tapeOutput) tapeOutput->WriteByte(d);

}

// ---------------------------------------------------------------------------
//	じょうたいしゅとく
//
uint IOCALL SIO::GetStatus(uint)
{
	if (hostEnabled) PumpHost();
//	LOG1("!%.2x ", status      );
	return status;
}

// ---------------------------------------------------------------------------
//	でーたしゅとく
//
uint IOCALL SIO::GetData(uint)
{
	LOG1(">%.2x ", data);
	
	int f = status & RXRDY;
	status &= ~RXRDY;

	const uint result = data;
	if (hostEnabled) PumpHost();
	else if (f) bus->Out(prequest, 0);
	return result;
}

void IOCALL SIO::AcceptData(uint, uint d)
{
	if (hostEnabled) return; // The host endpoint exclusively owns this USART.
	LOG1("Accept: [%.2x]", d);
	if (rxen)
	{
		data = d;
		if (status & RXRDY)
		{
			status |= OE;
			LOG0(" - overrun");
		}
		status |= RXRDY;
		bus->Out(prxrdy, 1);		// 割り込み
		LOG0("\n");
	}
	else
	{
		LOG0(" - ignored\n");
	}
}

// ---------------------------------------------------------------------------
//	状態保存
//
uint IFCALL SIO::GetStatusSize()
{
	return sizeof(Status);
}

bool IFCALL SIO::SaveStatus(uint8* s)
{
	Status* status = (Status*) s;
	status->rev			= ssrev;
	status->rxen = rxen; status->txen = txen; status->status = this->status;
	status->baseclock	= baseclock;
	status->clock		= clock;
	status->datalen		= datalen;
	status->stop		= stop;
	status->data		= data;
	status->mode		= mode;
	status->parity		= parity;
	return true;
}

bool IFCALL SIO::LoadStatus(const uint8* s)
{
	const Status* status = (const Status*) s;
	if (status->rev != ssrev)
		return false;
	rxen = status->rxen; txen = status->txen; this->status = status->status;
	baseclock	= status->baseclock;
	clock		= status->clock;
	datalen		= status->datalen;
	stop		= status->stop;
	data		= status->data;
	mode		= status->mode;
	parity		= status->parity;
	if (datalen < 5 || datalen > 8 || stop > 3) return false;
	outputType = (stop << 6) | ((datalen - 5) << 2) | (parity == none ? 0 : parity == even ? 0x30 : 0x10);
	return true;
}


// ---------------------------------------------------------------------------
//	device description
//
const Device::Descriptor SIO::descriptor = { indef, outdef };

const Device::OutFuncPtr SIO::outdef[] = 
{
	STATIC_CAST(Device::OutFuncPtr, &SIO::Reset),
	STATIC_CAST(Device::OutFuncPtr, &SIO::SetControl),
	STATIC_CAST(Device::OutFuncPtr, &SIO::SetData),
	STATIC_CAST(Device::OutFuncPtr, &SIO::AcceptData),
};

const Device::InFuncPtr SIO::indef[] = 
{
	STATIC_CAST(Device::InFuncPtr, &SIO::GetStatus),
	STATIC_CAST(Device::InFuncPtr, &SIO::GetData),
};


void SIO::EnableHost(bool enabled) {
    if (hostEnabled == enabled) return;
    hostEnabled = enabled;
    HostClear();
    if (tapeOutput) tapeOutput->SetSerial(txen && !enabled, outputType);
}
void SIO::HostClear() {
    hostRx.clear(); hostTx.clear(); hostDropped = 0;
    status &= ~(RXRDY | OE | DSR);
    status |= TXRDY | TXE;
    data = 0;
    if (hostEnabled) status |= DSR;
}
void SIO::PumpHost() {
    status &= ~(TXRDY | TXE);
    if (hostTx.size() < HostCapacity) status |= TXRDY;
    if (hostTx.empty()) status |= TXE;
    if (rxen && !(status & RXRDY) && !hostRx.empty()) {
        data = hostRx.front() & ((1u << datalen) - 1);
        hostRx.pop_front(); status |= RXRDY;
        bus->Out(prxrdy, 1);
    }
}
bool SIO::HostWrite(const std::vector<uint8>& bytes) {
    if (!hostEnabled || bytes.size() > HostCapacity - HostRxPending()) return false;
    hostRx.insert(hostRx.end(), bytes.begin(), bytes.end());
    PumpHost(); return true;
}
std::vector<uint8> SIO::HostRead(size_t maximum) {
    std::vector<uint8> result;
    if (!hostEnabled) return result;
    const size_t count = std::min(maximum, hostTx.size());
    result.reserve(count);
    for (size_t i=0; i<count; ++i) { result.push_back(hostTx.front()); hostTx.pop_front(); }
    PumpHost(); return result;
}
