#include "headless_draw.h"

#include <zlib.h>

#include <array>
#include <cstring>
#include <fstream>

namespace {

void AppendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

void AppendChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
    AppendU32(out, static_cast<uint32_t>(data.size()));
    const size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uLong crc = crc32(0L, out.data() + crcStart, static_cast<uInt>(4 + data.size()));
    AppendU32(out, static_cast<uint32_t>(crc));
}

} // namespace

bool HeadlessDraw::Init(uint width, uint height, uint bpp) {
    if (bpp != 8 || width == 0 || height == 0) {
        return false;
    }
    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<size_t>(width_) * height_, 0);
    palette_.assign(256, Palette{0, 0, 0, 0});
    return true;
}

bool HeadlessDraw::Cleanup() {
    pixels_.clear();
    palette_.clear();
    width_ = height_ = 0;
    return true;
}

bool HeadlessDraw::Lock(uint8** image, int* bytesPerLine) {
    if (!image || !bytesPerLine || pixels_.empty()) {
        return false;
    }
    *image = pixels_.data();
    *bytesPerLine = static_cast<int>(width_);
    return true;
}

bool HeadlessDraw::Unlock() {
    return true;
}

uint HeadlessDraw::GetStatus() {
    return readytodraw | shouldrefresh;
}

void HeadlessDraw::Resize(uint width, uint height) {
    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        pixels_.assign(static_cast<size_t>(width_) * height_, 0);
    }
}

void HeadlessDraw::DrawScreen(const Region&) {}

void HeadlessDraw::SetPalette(uint index, uint count, const Palette* palette) {
    if (!palette || index >= palette_.size()) {
        return;
    }
    const size_t available = palette_.size() - index;
    const size_t copied = count < available ? count : available;
    std::memcpy(palette_.data() + index, palette, copied * sizeof(Palette));
}

std::vector<uint8_t> HeadlessDraw::EncodePng() const {
    if (pixels_.empty() || palette_.empty()) {
        return {};
    }

    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height_) * (1 + static_cast<size_t>(width_) * 3));
    for (uint y = 0; y < height_; ++y) {
        raw.push_back(0); // PNG filter: None
        for (uint x = 0; x < width_; ++x) {
            const Palette& color = palette_[pixels_[static_cast<size_t>(y) * width_ + x]];
            raw.push_back(color.red);
            raw.push_back(color.green);
            raw.push_back(color.blue);
        }
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK) {
        return {};
    }
    compressed.resize(compressedSize);

    std::vector<uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr;
    AppendU32(ihdr, width_);
    AppendU32(ihdr, height_);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    AppendChunk(png, "IHDR", ihdr);
    AppendChunk(png, "IDAT", compressed);
    AppendChunk(png, "IEND", {});
    return png;
}

bool HeadlessDraw::SavePng(const std::string& path, std::string* error) const {
    const std::vector<uint8_t> png = EncodePng();
    if (png.empty()) {
        if (error) *error = "framebuffer is empty";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) *error = "cannot create output file";
        return false;
    }
    output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!output) {
        if (error) *error = "failed while writing PNG";
        return false;
    }
    return true;
}
