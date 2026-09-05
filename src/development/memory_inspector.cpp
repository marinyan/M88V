// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "memory_inspector.h"
#include "pc88/memory.h"
#include "devices/Z80c.h"
#include <sstream>

namespace M88V {
Mapping MemoryInspector::At(PC8801::Memory& m, Z80C& cpu, uint16_t address, bool write) {
    MemoryPage *r, *w;
    cpu.GetPages(&r, &w);
    const auto& page = (write ? w : r)[address >> MemoryManager::pagebits];
    Mapping result;
    result.wait = cpu.GetWaits()[address >> MemoryManager::pagebits];
#ifdef PTR_IDBIT
    const bool function = (page.ptr & MemoryManager::idbit) != 0;
    const auto ptr = page.ptr & ~MemoryManager::idbit;
#else
    const bool function = page.func;
    const auto ptr = page.ptr;
#endif
    if (!function) {
        const auto actual = static_cast<uintptr_t>(ptr) + (address & MemoryManager::pagemask);
        const auto match = [&](const uint8_t* base, size_t size, const std::string& name) {
            const auto begin = reinterpret_cast<uintptr_t>(base);
            if (!base || actual < begin || actual - begin >= size) return false;
            result.space = name; result.offset = static_cast<uint32_t>(actual-begin);
            result.data = reinterpret_cast<const uint8_t*>(actual); return true;
        };
        if (match(m.ram,0x10000,"ram") || match(m.tvram,0x1000,"tvram") ||
            match(m.rom,0x8000,"rom-n88") || match(m.rom+0x8000,0x8000,"rom-n88-ext") ||
            match(m.rom+0x10000,0x8000,"rom-n") || match(m.n80rom,0x8000,"rom-n80") ||
            match(m.n80v2rom,0xa000,"rom-n80sr") || match(m.dicrom,0x80000,"rom-dictionary") ||
            match(m.cdbios,0x10000,"rom-cd") || match(m.eram,m.erambanks*0x8000,"eram")) return result;
        for (int i=1;i<=8;++i) if (match(m.erom[i],0x2000,"rom-expansion-"+std::to_string(i))) return result;
        return result;
    }
    if (ptr == reinterpret_cast<intpointer>(&PC8801::Memory::RdWindow) ||
        ptr == reinterpret_cast<intpointer>(&PC8801::Memory::WrWindow)) {
        result.space="ram"; result.offset=(m.txtwnd+(address&0x3ff))&0xffff;
        result.data=m.ram+result.offset;
    } else {
        const intpointer reads[] = {reinterpret_cast<intpointer>(&PC8801::Memory::RdGVRAM0),
            reinterpret_cast<intpointer>(&PC8801::Memory::RdGVRAM1),reinterpret_cast<intpointer>(&PC8801::Memory::RdGVRAM2)};
        const intpointer writes[] = {reinterpret_cast<intpointer>(&PC8801::Memory::WrGVRAM0),
            reinterpret_cast<intpointer>(&PC8801::Memory::WrGVRAM1),reinterpret_cast<intpointer>(&PC8801::Memory::WrGVRAM2)};
        const char* names[] = {"gvram-b","gvram-r","gvram-g"};
        for (int i=0;i<3;++i) if (ptr == reads[i] || ptr == writes[i]) {
            result.space=names[i]; result.offset=address&0x3fff;
            result.data=&m.gvram[result.offset].byte[i]; return result;
        }
        if (ptr==reinterpret_cast<intpointer>(&PC8801::Memory::RdALU) ||
            ptr==reinterpret_cast<intpointer>(&PC8801::Memory::WrALUSet) ||
            ptr==reinterpret_cast<intpointer>(&PC8801::Memory::WrALURGB) ||
            ptr==reinterpret_cast<intpointer>(&PC8801::Memory::WrALUR) ||
            ptr==reinterpret_cast<intpointer>(&PC8801::Memory::WrALUB)) {
            result.space="gvram-alu"; result.offset=address&0x3fff;
        }
    }
    return result;
}

std::string MemoryInspector::Json(PC8801::Memory& m, Z80C& cpu) {
    std::ostringstream out;
    out << "{\"ok\":true,\"sp\":" << cpu.GetReg().r.w.sp
        << ",\"iff1\":" << (cpu.GetReg().iff1 ? "true":"false")
        << ",\"iff2\":" << (cpu.GetReg().iff2 ? "true":"false")
        << ",\"ports\":{\"31\":" << m.port31 << ",\"32\":" << m.port32
        << ",\"33\":" << m.port33 << ",\"34\":" << m.port34 << ",\"35\":" << m.port35
        << ",\"40\":" << m.port40 << ",\"5x\":" << m.port5x << ",\"70\":" << (m.txtwnd>>8)
        << ",\"71\":" << m.port71 << ",\"e2\":" << m.porte2 << ",\"e3\":" << m.porte3
        << ",\"f0\":" << m.portf0 << "},\"dictionary_selected\":" << (m.seldic?"true":"false")
        << ",\"regions\":[";
    bool comma=false;
    // Inspect real CPU pages, coalescing only contiguous physical mappings.
    for (uint32_t start=0; start<0x10000;) {
        auto rd=At(m,cpu,static_cast<uint16_t>(start),false), wr=At(m,cpu,static_cast<uint16_t>(start),true);
        uint32_t end=start+1;
        for (;end<0x10000;++end) {
            auto r=At(m,cpu,static_cast<uint16_t>(end),false), w=At(m,cpu,static_cast<uint16_t>(end),true);
            if (r.space!=rd.space || w.space!=wr.space || r.wait!=rd.wait ||
                (rd.space!="unmapped"&&r.offset!=rd.offset+end-start) ||
                (wr.space!="unmapped"&&w.offset!=wr.offset+end-start)) break;
        }
        if(comma) out<<','; comma=true;
        out << "{\"start\":"<<start<<",\"length\":"<<end-start
            <<",\"read\":\""<<rd.space<<"\",\"read_offset\":"<<rd.offset
            <<",\"write\":\""<<wr.space<<"\",\"write_offset\":"<<wr.offset<<",\"wait\":"<<rd.wait<<'}';
        start=end;
    }
    return out.str()+"]}";
}
}
