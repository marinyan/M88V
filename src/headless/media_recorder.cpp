// SPDX-License-Identifier: BSD-2-Clause
#include "media_recorder.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
void U16(std::vector<uint8_t>& out, uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); }
void U32(std::vector<uint8_t>& out, uint32_t v) { U16(out, uint16_t(v)); U16(out, uint16_t(v >> 16)); }
void Tag(std::vector<uint8_t>& out, const char* tag) { out.insert(out.end(), tag, tag + 4); }
void Put(std::vector<uint8_t>& out, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) out[at + i] = uint8_t(v >> (8 * i));
}
void PcmFormat(std::vector<uint8_t>& out) {
    U16(out, 1); U16(out, 2); U32(out, MediaRecorder::Rate);
    U32(out, MediaRecorder::Rate * 4); U16(out, 4); U16(out, 16);
}
size_t List(std::vector<uint8_t>& out, const char* tag) {
    Tag(out, "LIST"); const size_t size = out.size(); U32(out, 0); Tag(out, tag); return size;
}
void EndList(std::vector<uint8_t>& out, size_t size) { Put(out, size, uint32_t(out.size() - size - 4)); }
std::FILE* CreateExclusive(const std::filesystem::path& path) {
#ifdef _WIN32
    const int fd = _wopen(path.c_str(), _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return nullptr;
    auto* file = _fdopen(fd, "w+b"); if (!file) _close(fd);
#else
    const int fd = open(path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0) return nullptr;
    auto* file = fdopen(fd, "w+b"); if (!file) close(fd);
#endif
    return file;
}
}

MediaRecorder::~MediaRecorder() { if (Active()) { std::string ignored; Stop(ignored); } }

bool MediaRecorder::Start(const std::string& path, bool video, uint32_t width, uint32_t height, std::string& error) {
    if (Active()) { error = "Already recording"; return false; }
    if (path.empty() || path.find('\0') != std::string::npos || (video && (!width || !height || width > 4096 || height > 4096))) {
        error = "A valid output path and framebuffer are required"; return false;
    }
    std::filesystem::path destination;
    std::string extension;
    try {
        destination = std::filesystem::absolute(std::filesystem::u8path(path));
        extension = destination.extension().u8string();
    } catch (const std::exception&) { error = "Cannot resolve output path (UTF-8 required)"; return false; }
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (extension != (video ? ".avi" : ".wav")) {
        error = video ? "Video output must have an .avi extension (PNG + PCM)" : "Audio output must have a .wav extension";
        return false;
    }
    auto* file = CreateExclusive(destination);
    if (!file) { error = "Cannot create recording; output must be a new file in an existing directory"; return false; }
    file_ = file; video_ = video; samples_ = bytes_ = frames_ = 0;
    index_.clear(); error_.clear(); path_ = destination.u8string();
    std::vector<uint8_t> h;
    Tag(h, "RIFF"); U32(h, 0); Tag(h, video ? "AVI " : "WAVE");
    if (!video) {
        Tag(h, "fmt "); U32(h, 16); PcmFormat(h); Tag(h, "data"); U32(h, 0);
    } else {
        const auto hdrl = List(h, "hdrl");
        Tag(h, "avih"); U32(h, 56);
        U32(h, 1000000 / Fps); U32(h, 0); U32(h, 0); U32(h, 0x10); // has index
        U32(h, 0); U32(h, 0); U32(h, 2); U32(h, 0); U32(h, width); U32(h, height);
        for (int i = 0; i < 4; ++i) U32(h, 0);
        auto strl = List(h, "strl");
        Tag(h, "strh"); U32(h, 56); Tag(h, "vids"); Tag(h, "png ");
        U32(h, 0); U32(h, 0); U32(h, 0); U32(h, 1); U32(h, Fps); U32(h, 0);
        videoLength_ = uint32_t(h.size()); U32(h, 0);
        U32(h, width * height * 3); U32(h, 0xffffffff); U32(h, 0);
        U16(h, 0); U16(h, 0); U16(h, uint16_t(width)); U16(h, uint16_t(height));
        Tag(h, "strf"); U32(h, 40); U32(h, 40); U32(h, width); U32(h, height);
        U16(h, 1); U16(h, 24); Tag(h, "png "); U32(h, width * height * 3);
        for (int i = 0; i < 4; ++i) U32(h, 0);
        EndList(h, strl);
        strl = List(h, "strl");
        Tag(h, "strh"); U32(h, 56); Tag(h, "auds"); U32(h, 0);
        U32(h, 0); U32(h, 0); U32(h, 0); U32(h, 1); U32(h, Rate); U32(h, 0);
        audioLength_ = uint32_t(h.size()); U32(h, 0);
        U32(h, Rate * 4); U32(h, 0xffffffff); U32(h, 4);
        for (int i = 0; i < 4; ++i) U16(h, 0);
        Tag(h, "strf"); U32(h, 16); PcmFormat(h);
        EndList(h, strl); EndList(h, hdrl);
        Tag(h, "LIST"); movi_ = uint32_t(h.size()); U32(h, 4); Tag(h, "movi");
    }
    if (!Write(h.data(), h.size())) return Fail(error, "Cannot write recording header");
    return true;
}

bool MediaRecorder::Write(const void* data, size_t size) {
    if (size && std::fwrite(data, 1, size, file_) != size) return false;
    bytes_ += size; return true;
}
bool MediaRecorder::Patch(uint32_t offset, uint32_t value) {
    std::vector<uint8_t> data; U32(data, value);
    return std::fseek(file_, long(offset), SEEK_SET) == 0 && std::fwrite(data.data(), 1, 4, file_) == 4;
}
bool MediaRecorder::Chunk(const char* id, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> h; Tag(h, id); U32(h, uint32_t(data.size()));
    Tag(index_, id); U32(index_, 0x10); U32(index_, uint32_t(bytes_) - movi_ - 4); U32(index_, uint32_t(data.size()));
    if (!Write(h.data(), h.size()) || !Write(data.data(), data.size())) return false;
    const uint8_t padding = 0;
    return !(data.size() & 1) || Write(&padding, 1);
}
bool MediaRecorder::Fail(std::string& error, const char* message) {
    error_ = message; error = error_;
    if (Active()) { std::string ignored; Stop(ignored); }
    return false;
}

bool MediaRecorder::Audio(const std::vector<int16_t>& pcm, std::string& error) {
    if (!Active() || pcm.empty()) return true;
    if (pcm.size() & 1) return Fail(error, "Invalid stereo sample buffer");
    // Stay below classic AVI/RIFF and signed 32-bit file-offset limits, including the index.
    if (bytes_ + pcm.size() * 2 + index_.size() + 32 >= 0x7fff0000ULL)
        return Fail(error, "Recording reached the 2 GiB file limit; start a new file");
    std::vector<uint8_t> data; data.reserve(pcm.size() * 2);
    for (auto sample : pcm) U16(data, uint16_t(sample));
    if (!(video_ ? Chunk("01wb", data) : Write(data.data(), data.size())))
        return Fail(error, "Failed while writing audio");
    samples_ += pcm.size() / 2; return true;
}
bool MediaRecorder::Video(const std::vector<uint8_t>& png, std::string& error) {
    if (!Active() || !video_ || uint64_t(frames_) * Rate >= samples_ * Fps) return true;
    if (png.empty()) return Fail(error, "Empty video frame");
    while (uint64_t(frames_) * Rate < samples_ * Fps) {
        if (bytes_ + png.size() + index_.size() + 32 >= 0x7fff0000ULL || index_.size() >= 16000000)
            return Fail(error, "Recording reached its file/index limit; start a new file");
        if (!Chunk("00dc", png)) return Fail(error, "Failed while writing video");
        ++frames_;
    }
    return true;
}
bool MediaRecorder::Stop(std::string& error) {
    if (!Active()) { error = error_.empty() ? "Not recording" : error_; return false; }
    bool ok = true;
    if (video_) {
        const auto moviEnd = uint32_t(bytes_);
        std::vector<uint8_t> h; Tag(h, "idx1"); U32(h, uint32_t(index_.size()));
        ok = Write(h.data(), h.size()) && Write(index_.data(), index_.size());
        ok = Patch(movi_, moviEnd - movi_ - 4) && ok;
        ok = Patch(48, frames_) && Patch(videoLength_, frames_) && Patch(audioLength_, uint32_t(samples_)) && ok;
    } else ok = Patch(40, uint32_t(samples_ * 4));
    ok = Patch(4, uint32_t(bytes_ - 8)) && ok;
    ok = (std::fflush(file_) == 0) && ok;
    ok = (std::fclose(file_) == 0) && ok; file_ = nullptr; index_.clear();
    if (!ok) error_ = "Failed to finalize recording";
    error = error_; return ok && error_.empty();
}
