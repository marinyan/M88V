// SPDX-License-Identifier: BSD-2-Clause
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Streaming RIFF files: PCM WAV, or lossless PNG + PCM AVI. No host audio device.
class MediaRecorder {
public:
    MediaRecorder() = default;
    MediaRecorder(const MediaRecorder&) = delete;
    MediaRecorder& operator=(const MediaRecorder&) = delete;
    static constexpr uint32_t Rate = 48000;
    static constexpr uint32_t Fps = 60;
    ~MediaRecorder();
    bool Start(const std::string& path, bool video, uint32_t width, uint32_t height, std::string& error);
    bool Audio(const std::vector<int16_t>& pcm, std::string& error);
    bool Video(const std::vector<uint8_t>& png, std::string& error);
    bool Stop(std::string& error);
    bool Active() const { return file_ != nullptr; }
    uint64_t Samples() const { return samples_; }
    uint32_t Frames() const { return frames_; }
    uint64_t Bytes() const { return bytes_; }
    const std::string& Path() const { return path_; }
    const std::string& Error() const { return error_; }

private:
    bool Write(const void* data, size_t size);
    bool Patch(uint32_t offset, uint32_t value);
    bool Chunk(const char* id, const std::vector<uint8_t>& data);
    bool Fail(std::string& error, const char* message);
    std::FILE* file_ = nullptr;
    bool video_ = false;
    uint64_t samples_ = 0, bytes_ = 0;
    uint32_t frames_ = 0, movi_ = 0, videoLength_ = 0, audioLength_ = 0;
    std::vector<uint8_t> index_;
    std::string path_, error_;
};
