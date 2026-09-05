#pragma once

#include "draw.h"

#include <cstdint>
#include <string>
#include <vector>

class HeadlessDraw final : public Draw {
public:
    bool Init(uint width, uint height, uint bpp) override;
    bool Cleanup() override;
    bool Lock(uint8** image, int* bytesPerLine) override;
    bool Unlock() override;
    uint GetStatus() override;
    void Resize(uint width, uint height) override;
    void DrawScreen(const Region& region) override;
    void SetPalette(uint index, uint count, const Palette* palette) override;
    bool SetFlipMode(bool) override { return true; }

    std::vector<uint8_t> EncodePng() const;
    bool SavePng(const std::string& path, std::string* error = nullptr) const;
    uint Width() const { return width_; }
    uint Height() const { return height_; }

private:
    uint width_ = 0;
    uint height_ = 0;
    std::vector<uint8_t> pixels_;
    std::vector<Palette> palette_;
};
