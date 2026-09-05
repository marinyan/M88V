#pragma once

#include "headless_draw.h"
#include "matrix_keyboard.h"
#include "pc88.h"
#include "pc88/config.h"
#include "diskmgr.h"
#include "tapemgr.h"
#include "development/profile.h"

#include <cstdint>
#include <string>
#include <vector>

class HeadlessMachine final : public PC88 {
public:
    struct Registers {
        uint16_t pc;
        uint16_t sp;
        uint16_t af;
        uint16_t bc;
        uint16_t de;
        uint16_t hl;
        uint16_t ix;
        uint16_t iy;
        uint16_t afAlt;
        uint16_t bcAlt;
        uint16_t deAlt;
        uint16_t hlAlt;
        uint8_t i;
        uint8_t r;
        uint8_t interruptMode;
        bool iff1;
        bool iff2;
    };

    bool Initialize(const std::string& romDirectory, const std::string& preferredN80Rom,
                    M88V::BasicMode mode, std::string* error);
    void ResetMachine();
    bool RunFrames(uint32_t frames, std::string* error);
    bool LoadBinary(const std::string& path, uint16_t address, bool installLauncher, std::string* error);
    bool OpenTape(const std::string& path, std::string* error);

    bool SetKey(int row, int bit, bool down) { return keyboard_.SetKey(row, bit, down); }
    bool SetNamedKey(const std::string& name, bool down) { return keyboard_.SetNamedKey(name, down); }
    void ReleaseAllKeys() { keyboard_.ReleaseAll(); }

    Registers GetRegisters() const;
    std::vector<uint8_t> ReadMemory(const std::string& space, uint32_t address, uint32_t length, std::string* error) const;
    std::vector<uint8_t> CreateDevelopmentDump() const;

    HeadlessDraw& Framebuffer() { return draw_; }
    const HeadlessDraw& Framebuffer() const { return draw_; }
    uint64_t FrameCount() const { return frameCount_; }
    const std::string& RomDirectory() const { return romDirectory_; }
    const std::string& SelectedN80Rom() const { return selectedN80Rom_; }
    const char* BasicModeName() const { return M88V::BasicModeName(config_.basicmode); }
    const char* MachineName() const { return M88V::MachineName(config_.basicmode); }

private:
    static PC8801::Config MakeDevelopmentConfig(M88V::BasicMode mode);

    HeadlessDraw draw_;
    DiskManager diskManager_;
    TapeManager tapeManager_;
    MatrixKeyboard keyboard_;
    PC8801::Config config_{};
    std::string romDirectory_;
    std::string selectedN80Rom_;
    uint64_t frameCount_ = 0;
    bool initialized_ = false;
};
