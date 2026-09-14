// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1997, 2000.
// ---------------------------------------------------------------------------
//	$Id: tapemgr.cpp,v 1.3 2000/08/06 09:58:51 cisc Exp $

#include "headers.h"
#include "tapemgr.h"
#include "file.h"
#include "status.h"
#include "misc.h"
#include <fstream>
#include <filesystem>
#include <limits>


#define LOGNAME	"tape"
#include "diag.h"

#define T88ID	"PC-8801 Tape Image(T88)"

// ---------------------------------------------------------------------------
//	構築
//
TapeManager::TapeManager()
: Device(DEV_ID('T','A','P','E')), tags(0), scheduler(0), event(0)
{
	datasize = 0;
	datatype = 0;
	pinput = 0;
	pos = 0;
	bus = 0;
	offset = 0;
	mode = T_BLANK;
	data = nullptr; tick = time = timercount = timerremain = 0;
	motor = false;
}

// ---------------------------------------------------------------------------
//	破棄
//
TapeManager::~TapeManager()
{
	Close();
}

// ---------------------------------------------------------------------------
//	初期化
//
bool TapeManager::Init(Scheduler* s, IOBus* b, int pi)
{
	scheduler = s;
	bus = b;
	pinput = pi;

	motor = false;
	timercount = 0;
	timerremain = 0;
	tick = 0;
	return true;
}
	
// ---------------------------------------------------------------------------
//	T88 を開く
//
bool TapeManager::Open(const char* file)
{
    // Parse into a temporary image: failed opens must leave playback intact.
    TapeManager candidate;
    FileIO fio;
    if (!fio.Open(file, FileIO::readonly)) return false;
    char buf[24];
    if (fio.Read(buf, 24) != 24 || memcmp(buf, T88ID, 24)) return false;
    Tag* previous = nullptr;
    size_t total = 0;
    for (;;) {
        TagHdr hdr;
        if (fio.Read(&hdr, 4) != 4) return false;
        total += 4 + hdr.length;
        if (total > 64 * 1024 * 1024) return false;
        Tag* tag = (Tag*)new uchar[sizeof(Tag) + hdr.length];
        tag->prev = previous; tag->next = nullptr;
        tag->id = hdr.id; tag->length = hdr.length;
        (previous ? previous->next : candidate.tags) = tag;
        previous = tag;
        if (fio.Read(tag->data, tag->length) != tag->length) return false;
        if (hdr.id == T_END) { if (hdr.length) return false; break; }
        if (hdr.id >= T_BLANK && hdr.id <= T_MARK) {
            if (hdr.length < 8) return false;
            const BlankTag* timed = (const BlankTag*)tag->data;
            if (timed->pos > 0x3fffffff || timed->tick > 16000000 || timed->tick > 0x3fffffff - timed->pos) return false;
        }
        if (hdr.id == T_DATA) {
            if (hdr.length < 12) return false;
            const DataTag* d = (const DataTag*)tag->data;
            if (d->length > hdr.length - 12) return false;
        }
    }
    if (!candidate.tags || candidate.tags->id != T_VERSION || candidate.tags->length < 2 ||
        *(uint16*)candidate.tags->data != T88VER) return false;
    Close();
    tags = candidate.tags; candidate.tags = nullptr;
    return Rewind();
}

// ---------------------------------------------------------------------------
//	とじる
//
bool TapeManager::Close()
{
	if (scheduler)
		SetTimer(0);
	while (tags)
	{
		Tag* n = tags->next;
		delete []reinterpret_cast<uchar*>(tags);
		tags = n;
	}
	pos = nullptr; data = nullptr; datasize = offset = 0;
	tick = timercount = timerremain = 0; mode = T_BLANK;
	return true;
}

// ---------------------------------------------------------------------------
//	まきもどす
//
bool TapeManager::Rewind(bool timer)
{
	pos = tags;
	SetTimer(0);
	tick = 0; data = nullptr; datasize = offset = 0; mode = T_BLANK;
	if (pos)
	{
		tick = 0;

		// バージョン確認
		// 最初のタグはバージョンタグになるはず？
		if (   pos->id != T_VERSION
			|| pos->length < 2 
			|| *(uint16*)pos->data != T88VER)
			return false;
		
		pos = pos->next;
		Proceed(timer);
	}
	return true;
}

// ---------------------------------------------------------------------------
//	モータ
//
bool TapeManager::Motor(bool s)
{
	if (motor == s) return true;
	FlushCarrier();
	if (s)
	{
		statusdisplay.Show(10, 2000, "Motor on: %d %d", timerremain, timercount);
		time = scheduler->GetTime();
		if (timerremain)
			event = scheduler->AddEvent(timerremain*125/6, this, STATIC_CAST(TimeFunc, &TapeManager::Timer));
		motor = true;
	}
	else
	{
		if (timercount)
		{
			int td = (scheduler->GetTime() - time) * 6 / 125;
			timerremain = Max(0, int(timerremain) - td);
			scheduler->DelEvent(event), event = 0;
			statusdisplay.Show(10, 2000, "Motor off: %d %d", timerremain, timercount);
		}
		motor = false;
	}
	return true;
}

// ---------------------------------------------------------------------------

uint TapeManager::GetPos()
{
	if (motor)
	{
		if (timercount)
			return tick + timercount - timerremain + (scheduler->GetTime() - time) * 6 / 125;
		else
			return tick;
	}
	else
	{
		return tick + timercount - timerremain;
	}
}

// ---------------------------------------------------------------------------
//	タグを処理
//
void TapeManager::Proceed(bool timer)
{
	while (pos)
	{
		LOG1("TAG %d\n", pos->id);
		switch (pos->id)
		{
		case T_END:
			mode = T_BLANK;
			pos = 0;
			statusdisplay.Show(50, 0, "end of tape", tick);
			SetTimer(0); data = nullptr; datasize = offset = 0;
			return;

		case T_BLANK:
		case T_SPACE:
		case T_MARK:
		{
			BlankTag* t = (BlankTag*) pos->data;
			mode = (Mode) pos->id;

			if (t->pos + t->tick <= tick)
				break;

			if (timer)
				SetTimer(t->pos + t->tick - tick);
			else
				timercount = t->pos + t->tick - tick;

			pos = pos->next;
			return;
		}

		case T_DATA:
		{
			DataTag* t = (DataTag*) pos->data;
			mode = T_DATA;

			data = t->data;
			datasize = t->length;
			datatype = t->type;
			offset = 0;

			if (!datasize)
				break;

			pos = pos->next;
			if (timer)
				SetTimer(datatype & 0x100 ? 44 : 88);
			else
				timercount = t->tick;
			return;
		}
		}
		pos = pos->next;
	}
}

// ---------------------------------------------------------------------------
//
//
void IOCALL TapeManager::Timer(uint)
{
    event = nullptr; // Scheduler has already removed this one-shot event.
	tick += timercount;
	statusdisplay.Show(50, 0, "tape: %d", tick);
	
	if (mode == T_DATA)
	{
		Send(*data++);
		offset++;
		if (--datasize > 0)
		{
			SetTimer(datatype & 0x100 ? 44 : 88);
			return;
		}
		LOG0("\n");
	}
	Proceed();
}

// ---------------------------------------------------------------------------
//	キャリア確認
//
bool TapeManager::Carrier()
{
	if (mode == T_MARK)
	{
		LOG0("*");
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	タイマー更新
//
void TapeManager::SetTimer(int count)
{
	if (count > 100)
		LOG1("Timer: %d\n", count);
	if (scheduler) scheduler->DelEvent(event);
	event = 0;
	timercount = timerremain = count;
	if (motor && scheduler)
	{
		time = scheduler->GetTime();
		if (count)			// 100000/4800
			event = scheduler->AddEvent(count*125/6, this, STATIC_CAST(TimeFunc, &TapeManager::Timer));
	}
	else
		timerremain = count;
}

// ---------------------------------------------------------------------------
//	バイト転送
//
inline void TapeManager::Send(uint byte)
{
	LOG1("%.2x ", byte);
	statusdisplay.TapeAccess(false);
	bus->Out(pinput, byte);
}

// ---------------------------------------------------------------------------
//	即座にデータを要求する
//
void TapeManager::RequestData(uint, uint)
{
	if (motor && mode == T_DATA && event)
	{
		scheduler->SetEvent(event, 1, this, STATIC_CAST(TimeFunc, &TapeManager::Timer));
	}
}

// ---------------------------------------------------------------------------
//	シークする
//
bool TapeManager::Seek(uint newpos, uint off)
{
	if (!Rewind(false))
		return false;

	while (pos && (tick + timercount) < newpos)
	{
		tick += timercount;
		Proceed(false);
	}
	if (!pos)
		return newpos == tick && off == 0;
	
	switch (pos->prev->id)
	{
		int l;

	case T_BLANK:
	case T_SPACE:
	case T_MARK:
		mode = (Mode) pos->prev->id;
		l = tick+timercount-newpos;
		tick = newpos;
		SetTimer(l);
		break;

	case T_DATA:
		mode = T_DATA;
		if (off >= uint(datasize)) return false;
		offset = off;
		tick += offset * (datatype & 0x100 ? 44 : 88);
		data += offset;
		datasize -= offset;
		SetTimer(datatype & 0x100 ? 44 : 88);
		break;

	default:
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------

void IOCALL TapeManager::Out30(uint, uint d)
{
	if ((outputControl & 0x3c) != (d & 0x3c)) FlushCarrier();
	outputControl = d;
	if (motor != !!(d & 8)) Motor(!!(d & 8));
}

uint IOCALL TapeManager::In40(uint)
{
	return IOBus::Active(Carrier() ? 4 : 0, 4);
}


// ---------------------------------------------------------------------------
//	状態保存
//
uint IFCALL TapeManager::GetStatusSize()
{
	return sizeof(Status);
}

bool IFCALL TapeManager::SaveStatus(uint8* s)
{
	Status* status = (Status*) s;
	status->rev = ssrev;
	status->motor = motor;
	status->pos = GetPos();
	status->offset = offset;
	statusdisplay.Show(0, 1000, "tapesave: %d", status->pos);
	return true;
}

bool IFCALL TapeManager::LoadStatus(const uint8* s)
{
	const Status* status = (const Status*) s;
	if (status->rev != ssrev)
		return false;
	motor = status->motor;
	Seek(status->pos, status->offset);
	statusdisplay.Show(0, 1000, "tapesave: %d", GetPos());
	return true;
}

// ---------------------------------------------------------------------------
//	device description
//
const Device::Descriptor TapeManager::descriptor =
{
	TapeManager::indef, TapeManager::outdef
};

const Device::OutFuncPtr TapeManager::outdef[] =
{
	STATIC_CAST(Device::OutFuncPtr, &TapeManager::RequestData),
	STATIC_CAST(Device::OutFuncPtr, &TapeManager::Out30),
};

const Device::InFuncPtr TapeManager::indef[] =
{
	STATIC_CAST(Device::InFuncPtr, &TapeManager::In40),
};

// M88V additions (BSD-2-Clause): separate cassette output, following X88000.
void TapeManager::FlushCarrier()
{
    uint now = scheduler ? scheduler->GetTime() : 0;
    uint elapsed = uint((uint64_t(uint(now - recordTime)) * 6) / 125);
    recordTime = now;
    elapsed = elapsed > recordDataTicks ? elapsed - recordDataTicks : 0;
    recordDataTicks = 0;
    if (!OutputActive() || !elapsed) return;
    uint16 id = outputControl & 4 ? T_SPACE : T_MARK;
    if (!recorded.empty() && recorded.back().id == id)
        recorded.back().tick += elapsed;
    else
        recorded.push_back({id, 0, elapsed, {}});
    recordingDirty = true;
}

void TapeManager::SetSerial(bool enabled, uint type)
{
    if (transmit == enabled && serialType == type) return;
    FlushCarrier();
    transmit = enabled;
    serialType = type;
}

void TapeManager::WriteByte(uint byte)
{
    if (!OutputActive()) return;
    statusdisplay.TapeAccess(true);
    uint now = scheduler ? scheduler->GetTime() : 0;
    uint ticks = outputControl & 0x10 ? 44 : 88;
    uint elapsed = uint((uint64_t(uint(now - recordTime)) * 6) / 125);
    if (elapsed > ticks) FlushCarrier();
    recordTime = now;
    recordDataTicks = ticks;
    uint16 type = uint16(serialType | (outputControl & 0x10 ? 0x100 : 0));
    if (recorded.empty() || recorded.back().id != T_DATA ||
        recorded.back().type != type || recorded.back().bytes.size() >= 32768)
        recorded.push_back({T_DATA, type, 0, {}});
    recorded.back().bytes.push_back(uint8(byte));
    recorded.back().tick += ticks;
    recordingDirty = true;
}

void TapeManager::ClearRecording()
{
    recorded.clear();
    recordDataTicks = 0;
    recordingDirty = false;
    recordTime = scheduler ? scheduler->GetTime() : 0;
}

bool TapeManager::WriteImage(const char* file, const std::vector<RecordedTag>& image, bool cmt)
{
    // Write beside the destination, then replace it only after successful close.
    namespace fs = std::filesystem;
    fs::path destination(file), temporary(destination);
    temporary += ".m88v-tmp";
    std::error_code ec;
    if (fs::exists(temporary, ec) || ec) return false;
    std::ofstream out(temporary, std::ios::binary);
    if (!out) return false;
    auto word = [&](uint v) { out.put(char(v)); out.put(char(v >> 8)); };
    auto dword = [&](uint v) { word(v); word(v >> 16); };
    if (!cmt) {
        out.write(T88ID, 24);
        word(T_VERSION); word(2); word(T88VER);
    }
    uint32 position = 0;
    for (const auto& tag : image) {
        if (!cmt) {
            word(tag.id); word(uint(tag.bytes.size()) + (tag.id == T_DATA ? 12 : 8));
            dword(position); dword(tag.tick);
            if (tag.id == T_DATA) { word(uint(tag.bytes.size())); word(tag.type); }
        }
        if (!tag.bytes.empty()) out.write((const char*)tag.bytes.data(), tag.bytes.size());
        position += tag.tick;
    }
    if (!cmt) { word(T_END); word(0); }
    out.flush();
    bool good = bool(out);
    out.close(); good = good && !out.fail();
    if (good) {
#ifdef _WIN32
        good = !!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
        fs::rename(temporary, destination, ec); good = !ec;
#endif
    }
    if (!good) fs::remove(temporary, ec);
    return good;
}

bool TapeManager::CreateEmpty(const char* file)
{
    return WriteImage(file, {}, false);
}

bool TapeManager::SaveRecording(const char* file, bool cmt)
{
    FlushCarrier();
    if (!WriteImage(file, recorded, cmt)) return false;
    recordingDirty = false;
    return true;
}

bool TapeManager::SeekEnd()
{
    if (!tags) return false;
    SetTimer(0);
    tick = 0;
    for (Tag* t = tags; t; t = t->next) {
        if (t->id >= T_BLANK && t->id <= T_MARK) {
            BlankTag* b = (BlankTag*)t->data;
            tick = Max(tick, b->pos + b->tick);
        }
    }
    pos = nullptr; data = nullptr; datasize = offset = 0; mode = T_BLANK;
    return true;
}
