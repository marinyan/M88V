#include "headless_machine.h"

#include "memory.h"
#include "pc88/calender.h"
#include "development/rom_overlay.h"
#include "development/binary_loader.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace fs = std::filesystem;

namespace {


void AppendLe32(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(static_cast<uint8_t>(value & 0xff));
    output.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    output.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    output.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

uint16_t Word(Z80Reg::wordreg value) {
    return static_cast<uint16_t>(value & 0xffffu);
}

} // namespace

PC8801::Config HeadlessMachine::MakeDevelopmentConfig(M88V::BasicMode mode) {
    PC8801::Config cfg{};
    cfg.basicmode = mode;
    cfg.clock = 40;
    cfg.speed = 100;
    cfg.mainsubratio = 1;
    cfg.cpumode = PC8801::Config::msauto;
    cfg.dipsw = 1829;
    cfg.flags = PC8801::Config::subcpucontrol |
                PC8801::Config::enablewait |
                PC8801::Config::precisemixing |
                PC8801::Config::mixsoundalways;
    cfg.flag2 = PC8801::Config::resetondrop;
    cfg.sound = 48000;
    cfg.soundbuffer = 20;
    cfg.mastervol = 64;
    cfg.mousesensibility = 10;
    cfg.keytype = PC8801::Config::AT106;
    return cfg;
}


bool HeadlessMachine::Initialize(const std::string& romDirectory, const std::string& preferredN80Rom,
                                 M88V::BasicMode mode, std::string* error) {
    if (initialized_) return true;
    M88V::RomOverlay roms;
    if (!roms.Prepare(romDirectory, preferredN80Rom, mode, error)) return false;
    const std::string overlayDirectory = roms.Directory();
    selectedN80Rom_ = roms.SelectedN80Rom();
    romIdentity_ = roms.Fingerprint();

    std::error_code ec;
    const fs::path originalDirectory = fs::current_path(ec);
    const fs::path absoluteRomDirectory = fs::absolute(fs::u8path(romDirectory), ec);
    if (ec) {
        if (error) *error = "cannot resolve ROM directory: " + ec.message();
        return false;
    }
    romDirectory_ = absoluteRomDirectory.u8string();

    if (!diskManager_.Init()) {
        if (error) *error = "DiskManager initialization failed";
        return false;
    }

    fs::current_path(fs::u8path(overlayDirectory), ec);
    if (ec) {
        if (error) *error = "cannot enter ROM directory: " + ec.message();
        return false;
    }

    const bool coreReady = PC88::Init(&draw_, &diskManager_, &tapeManager_, overlayDirectory.c_str());
    std::error_code restoreError;
    fs::current_path(originalDirectory, restoreError);
    if (restoreError) { if (error) *error = "Cannot restore working directory"; return false; }
    if (!coreReady) {
        if (error) *error = "M88 core initialization failed while loading ROMs";
        return false;
    }

    if (!keyboard_.Init(&bus1)) {
        if (error) *error = "keyboard matrix connection failed";
        return false;
    }

    config_ = MakeDevelopmentConfig(mode);
    ApplyConfig(&config_);
    Reset();
    GetCalender()->UseEmulatedClock(this, 946684800); // 2000-01-01 UTC; local calendar representation.
    if (M88V::IsPC80(mode) && !IsN80Supported()) {
        if (error) *error = "n80_2.rom was not accepted by the M88 core";
        return false;
    }
    if (mode == PC8801::Config::N80V2 && !IsN80V2Supported()) {
        if (error) *error = "n80_3.rom is required for N80V2 mode";
        return false;
    }
    UpdateScreen(true);
    debugger_.Attach(*GetCPU1(),*GetMem1());
    initialized_ = true;
    return true;
}

void HeadlessMachine::ResetMachine() {
    if (!initialized_) return;
    if (com_.Connected()) { com_.Close(); Serial().EnableHost(false); }
    keyboard_.ReleaseAll();
    debugger_.Clear();
    ApplyConfig(&config_);
    Reset();
    UpdateScreen(true);
    frameCount_ = 0;
    frameRemaining_ = 0;
    GetCalender()->UseEmulatedClock(this, 946684800);
}

bool HeadlessMachine::RunFrames(uint32_t frames, std::string* error) {
    if (!initialized_) {
        if (error) *error = "machine is not initialized";
        return false;
    }
    if (frames > 100000) {
        if (error) *error = "frames must be <= 100000 per request";
        return false;
    }
    if (recording_ && (inputEvents_.size()>=50000 || recordedFrames_+frames>100000)) { if(error)*error="Input recording limit reached (50000 events / 100000 frames); stop recording";return false; }
    for (uint32_t i = 0; i < frames && !debugger_.Stopped(); ++i) {
        PumpSerial();
        if (!frameRemaining_) {
            TimeSync();
            frameRemaining_ = GetFramePeriod();
        }
        frameRemaining_ -= Proceed(static_cast<uint>(frameRemaining_), static_cast<uint>(config_.clock), static_cast<uint>(config_.clock));
        PumpSerial();
        diskManager_.Update();
        UpdateScreen(true);
        if (frameRemaining_ <= 0) { frameRemaining_ = 0; ++frameCount_; debugger_.FrameEnd(); }
    }
    if(recording_ && frames) {inputEvents_.push_back({frames,{}});recordedFrames_+=frames;}
    return true;
}

bool HeadlessMachine::LoadBinary(const std::string& path, uint16_t address, bool installLauncher, std::string* error) {
    if (!initialized_) {
        if (error) *error = "machine is not initialized";
        return false;
    }
    std::ifstream input(fs::u8path(path), std::ios::binary);
    if (!input) {
        if (error) *error = "cannot open BIN: " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        if (error) *error = "BIN is empty";
        return false;
    }
    const uint32_t end = static_cast<uint32_t>(address) + static_cast<uint32_t>(bytes.size());
    const uint32_t limit = installLauncher ? M88V::BinaryLoadLimit : 0x10000u;
    if (end > limit) {
        if (error) *error = "BIN overlaps the FFF0H launcher or exceeds 64 KiB RAM";
        return false;
    }

    uint8_t* ram = GetMem1()->GetRAM();
    debugger_.Clear();
    std::memcpy(ram + address, bytes.data(), bytes.size());
    if (installLauncher) {
        const auto launcherAddress = M88V::BinaryLauncher(end);
        const auto stack = M88V::BinaryStack(launcherAddress);
        // LD SP,stack / CALL address / JP 0000; preserve the loaded image.
        const uint8_t launcher[] = {
            0x31, uint8_t(stack), uint8_t(stack >> 8),
            0xcd, static_cast<uint8_t>(address & 0xff), static_cast<uint8_t>(address >> 8),
            0xc3, 0x00, 0x00,
        };
        std::memcpy(ram + launcherAddress, launcher, sizeof(launcher));
        // V1H/V2 may map independent text RAM at F000H. Install the high
        // launcher in that CPU-visible bank as well; the BIN remains in RAM.
        if (GetMem1()->GetRdBank(launcherAddress) == PC8801::Memory::mTV)
            std::memcpy(GetMem1()->GetTVRAM() + (launcherAddress & 0xfff), launcher, sizeof(launcher));
        GetCPU1()->SetPC(launcherAddress);
    } else {
        GetCPU1()->SetPC(address);
    }
    return true;
}

bool HeadlessMachine::OpenTape(const std::string& path, std::string* error) {
    if (!initialized_) {
        if (error) *error = "machine is not initialized";
        return false;
    }
    const std::string absolute = fs::absolute(fs::u8path(path)).string();
    if (!tapeManager_.Open(absolute.c_str())) {
        if (error) *error = "cannot open T88 tape: " + path;
        return false;
    }
    return true;
}

HeadlessMachine::DiskStatus HeadlessMachine::GetDiskStatus(unsigned drive) const {
    DiskStatus result;
    if (drive>=DiskManager::max_drives) return result;
    auto& disks=const_cast<DiskManager&>(diskManager_);
    CriticalSection::Lock lock(disks.GetCS());
    result.mounted=disks.GetFDU(drive)->IsMounted();
    result.path=disks.GetImagePath(drive);
    result.count=disks.GetNumDisks(drive);
    result.index=result.mounted ? disks.GetCurrentDisk(drive) : -1;
    result.readOnly=result.mounted && (disks.GetFDU(drive)->SenceDeviceStatus() & 0x40);
    result.requestedReadOnly=diskReadOnly_[drive];
    return result;
}

bool HeadlessMachine::MountDisk(unsigned drive, const std::string& path, unsigned index, bool readOnly, std::string& error) {
    if (!initialized_ || drive>=DiskManager::max_drives || recording_) {
        error="invalid drive, uninitialized machine or active input recording"; return false;
    }
    if (path.empty() || path.find('\0')!=std::string::npos || index>=DiskImageHolder::max_disks) {
        error="provide an image path and index from 0 to 63"; return false;
    }
    std::error_code ec;
    const auto canonical=fs::canonical(fs::u8path(path),ec);
    if (ec || !fs::is_regular_file(canonical,ec) || ec) { error="disk image file not found"; return false; }
    std::string absolute=canonical.u8string();
    if (absolute.size()>=MAX_PATH) { error="disk image path is too long"; return false; }
    const auto other=GetDiskStatus(1-drive);
    if (other.mounted && fs::equivalent(canonical,fs::u8path(other.path),ec) && !ec) {
        if (unsigned(other.index)==index) { error="this disk is already mounted in the other drive"; return false; }
        if (other.requestedReadOnly!=readOnly) { error="drives sharing an image must use the same readonly option"; return false; }
        absolute=other.path; // Match the existing holder even through a path alias.
    }
    // Validate with an independent read-only drive before replacing current media.
    {
        DiskManager probe;
        if (!probe.Init() || !probe.Mount(0,absolute.c_str(),true,int(index),false) ||
            !probe.GetFDU(0)->IsMounted()) {
            error="cannot read disk image or index is out of range"; return false;
        }
    }
    if (!diskManager_.Unmount(drive)) { error="failed to save previous disk while unmounting"; return false; }
    if (!diskManager_.Mount(drive,absolute.c_str(),readOnly,int(index),false)) {
        error="disk mount failed after removing previous media"; return false;
    }
    diskReadOnly_[drive]=readOnly;
    return true;
}

bool HeadlessMachine::UnmountDisk(unsigned drive, std::string& error) {
    if (!initialized_ || drive>=DiskManager::max_drives || recording_) {
        error="invalid drive, uninitialized machine or active input recording"; return false;
    }
    if (!diskManager_.Unmount(drive)) { error="failed to save disk while unmounting"; return false; }
    diskReadOnly_[drive]=true;
    return true;
}

bool HeadlessMachine::SelectDisk(unsigned drive, unsigned index, std::string& error) {
    const auto disk=GetDiskStatus(drive);
    if (!disk.mounted) { error="drive has no mounted image"; return false; }
    if (index>=disk.count) { error="disk index is out of range"; return false; }
    return MountDisk(drive,disk.path,index,disk.requestedReadOnly,error);
}

HeadlessMachine::Registers HeadlessMachine::GetRegisters() const {
    auto* cpu = const_cast<HeadlessMachine*>(this)->GetCPU1();
    cpu->DebugAF(); // Materialize the portable core's lazy condition flags.
    const Z80Reg& reg = cpu->GetReg();
    return Registers{
        static_cast<uint16_t>(cpu->GetPC()),
        Word(reg.r.w.sp), Word(reg.r.w.af), Word(reg.r.w.bc), Word(reg.r.w.de), Word(reg.r.w.hl),
        Word(reg.r.w.ix), Word(reg.r.w.iy), Word(reg.r_af), Word(reg.r_bc), Word(reg.r_de), Word(reg.r_hl),
        reg.ireg, static_cast<uint8_t>((reg.rreg & 0x7f) | reg.rreg7), reg.intmode, reg.iff1, reg.iff2,
    };
}

std::vector<uint8_t> HeadlessMachine::ReadMemory(const std::string& space, uint32_t address, uint32_t length, std::string* error) const {
    if (!initialized_) {
        if (error) *error = "machine is not initialized";
        return {};
    }
    if (length > 0x10000u) {
        if (error) *error = "length must be <= 65536";
        return {};
    }

    auto* memory = const_cast<HeadlessMachine*>(this)->GetMem1();
    const uint8_t* source = nullptr;
    uint32_t available = 0;
    if (space == "ram") {
        source = memory->GetRAM();
        available = 0x10000;
    } else if (space == "tvram") {
        source = memory->GetTVRAM();
        available = 0x1000;
    } else if (space == "gvram-b" || space == "gvram-r" || space == "gvram-g") {
        const int plane = space == "gvram-b" ? 0 : (space == "gvram-r" ? 1 : 2);
        available = 0x4000;
        if (address > available || length > available - address) {
            if (error) *error = "GVRAM range is outside 16 KiB plane";
            return {};
        }
        std::vector<uint8_t> result(length);
        const PC8801::Memory::quadbyte* gvram = memory->GetGVRAM();
        for (uint32_t i = 0; i < length; ++i) result[i] = gvram[address + i].byte[plane];
        return result;
    } else {
        if (error) *error = "space must be ram, tvram, gvram-b, gvram-r, or gvram-g";
        return {};
    }

    if (address > available || length > available - address) {
        if (error) *error = "memory range is outside selected space";
        return {};
    }
    return std::vector<uint8_t>(source + address, source + address + length);
}

std::vector<uint8_t> HeadlessMachine::CreateDevelopmentDump() const {
    constexpr uint32_t headerSize = 48;
    constexpr uint32_t ramSize = 0x10000;
    constexpr uint32_t textSize = 0x1000;
    constexpr uint32_t gvramPlaneSize = 0x4000;
    const uint32_t ramOffset = headerSize;
    const uint32_t textOffset = ramOffset + ramSize;
    const uint32_t gvramOffset = textOffset + textSize;

    std::vector<uint8_t> output;
    output.reserve(gvramOffset + gvramPlaneSize * 3);
    output.insert(output.end(), {'M', '8', '8', 'D', 'M', 'P', '1', 0});
    AppendLe32(output, 1);
    AppendLe32(output, GetRegisters().pc);
    AppendLe32(output, ramOffset);
    AppendLe32(output, ramSize);
    AppendLe32(output, textOffset);
    AppendLe32(output, textSize);
    AppendLe32(output, gvramOffset);
    AppendLe32(output, gvramPlaneSize);
    AppendLe32(output, 3);
    AppendLe32(output, 0);

    auto* memory = const_cast<HeadlessMachine*>(this)->GetMem1();
    output.insert(output.end(), memory->GetRAM(), memory->GetRAM() + ramSize);
    output.insert(output.end(), memory->GetTVRAM(), memory->GetTVRAM() + textSize);
    const PC8801::Memory::quadbyte* gvram = memory->GetGVRAM();
    for (int plane = 0; plane < 3; ++plane) {
        for (uint32_t i = 0; i < gvramPlaneSize; ++i) output.push_back(gvram[i].byte[plane]);
    }
    return output;
}
