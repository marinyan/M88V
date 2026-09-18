// SPDX-License-Identifier: BSD-2-Clause
#include "legacy_snapshot.h"
#include "device.h"
#include "devices/Z80.h"
#include "zlib/zlib.h"
#include <cstring>
#include <map>

namespace M88V::LegacySnapshot {
namespace {
constexpr size_t limit = 16 * 1024 * 1024;
uint32_t Word(const uint8_t* p) { uint32_t n; std::memcpy(&n,p,4); return n; }
struct Record { const uint8_t* data; size_t size; };
bool Records(const std::vector<uint8_t>& bytes, std::map<uint32_t,Record>& records) {
    size_t pos=0;
    while (bytes.size()-pos >= 8) {
        auto id=Word(bytes.data()+pos), size=Word(bytes.data()+pos+4); pos+=8;
        if (!id) return !size && pos==bytes.size();
        if (!size || size>bytes.size()-pos) return false;
        const size_t padded=(size_t(size)+3)&~size_t(3);
        if (padded>bytes.size()-pos || !records.emplace(id,Record{bytes.data()+pos,size}).second) return false;
        pos+=padded;
    }
    return false;
}
}
bool Decode(const std::vector<uint8_t>& file, Header& header, std::vector<uint8_t>& payload) {
    if (file.size()<sizeof(Header) || file.size()>limit) return false;
    Header h; std::memcpy(&h,file.data(),sizeof(h));
    if (std::memcmp(h.id,"M88 SnapshotData",16) || h.major!=1 || h.minor>1 ||
        h.datasize<8 || size_t(h.datasize)>limit || h.clock<1 || h.clock>1000 ||
        h.erambanks>256 || h.cpumode>2 || h.mainsubratio<1 || h.mainsubratio>2) return false;
    using C=PC8801::Config;
    switch (h.basicmode) {
    case C::N80: case C::N802: case C::N80V2: case C::N88V1:
    case C::N88V1H: case C::N88V2: case C::N88V2CD: break;
    default: return false;
    }
    std::vector<uint8_t> result(size_t(h.datasize));
    const size_t remaining=file.size()-sizeof(h);
    if (h.flags & 0x80000000u) {
        if (remaining<4) return false;
        int32_t signedSize; std::memcpy(&signedSize,file.data()+sizeof(h),4);
        if (signedSize>=0 || signedSize==INT32_MIN || size_t(-signedSize)!=remaining-4) return false;
        z_stream stream{};
        stream.next_in=const_cast<Bytef*>(file.data()+sizeof(h)+4);
        stream.avail_in=uInt(remaining-4);
        stream.next_out=result.data(); stream.avail_out=uInt(result.size());
        if (inflateInit(&stream)!=Z_OK) return false;
        const int status=inflate(&stream,Z_FINISH);
        const bool valid=status==Z_STREAM_END && stream.total_out==result.size() && stream.avail_in==0;
        inflateEnd(&stream);
        if (!valid) return false;
    } else {
        if (remaining!=result.size()) return false;
        std::memcpy(result.data(),file.data()+sizeof(h),remaining);
    }
    std::map<uint32_t,Record> records;
    if (!Records(result,records)) return false;
    header=h; payload.swap(result); return true;
}
bool ValidateDevices(DeviceList& devices, const std::vector<uint8_t>& payload,
                     unsigned currentBanks, const Header& target, unsigned effectiveTargetBanks) {
    const unsigned targetBanks=effectiveTargetBanks==UINT32_MAX?target.erambanks:effectiveTargetBanks;
    std::map<uint32_t,Record> incoming, reference;
    if (currentBanks>256 || targetBanks>256 || !Records(payload,incoming)) return false;
    std::vector<uint8_t> saved(devices.GetStatusSize());
    if (!devices.SaveStatus(saved.data()) || !Records(saved,reference)) return false;
    // OPN presence/size changes when the snapshot switches sound boards.
    // Its legacy status is six header bytes, 512 registers, and optional RAM.
    const auto opn1=DEV_ID('O','P','N','1'),opn2=DEV_ID('O','P','N','2');
    reference.erase(opn1);reference.erase(opn2);
    size_t expectedCount=reference.size();
    using C=PC8801::Config;
    const bool sound1=(target.basicmode&1)||!(target.flag2&C::disableopn44);
    const bool sound2=(target.flags&(C::opnaona8|C::opnona8))!=0;
    if(devices.Find(opn1)&&sound1)++expectedCount;
    if(devices.Find(opn2)&&sound2)++expectedCount;
    if(incoming.size()!=expectedCount)return false;
    for (const auto& entry: incoming) {
        if(entry.first==opn1||entry.first==opn2) {
            const bool enabled=entry.first==opn1?sound1:sound2;
            const bool adpcm=(target.flags&(entry.first==opn1?C::enableopna:C::opnaona8))!=0;
            if(!devices.Find(entry.first)||!enabled||entry.second.size!=518+(adpcm?0x40000:0)||entry.second.data[0]!=3)return false;
            continue;
        }
        auto found=reference.find(entry.first);
        if (found==reference.end()) return false;
        auto data=entry.second.data, old=found->second.data;
        size_t expected=found->second.size;
        if (entry.first==DEV_ID('M','E','M','1')) expected=expected-currentBanks*0x8000+targetBanks*0x8000;
        if (entry.second.size!=expected) return false;
        size_t rev=0, width=1;
        switch (entry.first) {
        case DEV_ID('C','P','U','1'): case DEV_ID('C','P','U','2'): rev=sizeof(Z80Reg)+3; break;
        case DEV_ID('S','U','B',' '): case DEV_ID('S','C','R','N'): width=4; break;
        case DEV_ID('C','R','T','C'):
            if (expected<5 || data[0]<1 || data[0]>old[0] || data[3]>6 || data[4]>1) return false;
            continue;
        case DEV_ID('S','I','O',' '): case DEV_ID('S','I','O','M'):
            if (expected<20 || Word(data+12)<5 || Word(data+12)>8 || Word(data+16)>3) return false;
            break;
        case DEV_ID('F','D','C',' '):
            // Idle snapshots contain historically uninitialized transfer fields.
            // Validate those only when the restored phase will consume them.
            if(expected<32 || Word(data+20)>8 || Word(data+28)>8) return false;
            if(Word(data+20)!=0 && ((Word(data+12)!=UINT32_MAX&&Word(data+12)>0x4000) ||
               Word(data+16)>0x4000 || Word(data+24)>8)) return false;
            break;
        case DEV_ID('M','E','M','1'):
        case DEV_ID('O','P','N','1'): case DEV_ID('O','P','N','2'):
        case DEV_ID('B','E','E','P'): case DEV_ID('C','A','L','N'):
        case DEV_ID('D','M','A','C'): case DEV_ID('T','A','P','E'): break;
        case DEV_ID('I','N','T','C'): case DEV_ID('K','N','J','1'): case DEV_ID('K','N','J','2'): continue;
        // External modules have no non-mutating validation API. Do not enter an
        // irreversible restore when we cannot preflight their private payload.
        default: return false;
        }
        if (rev+width>expected || std::memcmp(data+rev,old+rev,width)) return false;
    }
    return true;
}
}
