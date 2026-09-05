// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#pragma once

#include "profile.h"
#include <filesystem>
#include <string>

namespace M88V {
// Per-launch aliases only. Never modifies the supplied firmware directory.
class RomOverlay {
public:
    RomOverlay() = default;
    ~RomOverlay();
    RomOverlay(const RomOverlay&) = delete;
    RomOverlay& operator=(const RomOverlay&) = delete;
    bool Prepare(const std::string& directory, const std::string& preferredN80Rom,
                 BasicMode mode, std::string* error);
    std::string Directory() const { return directory_.u8string(); }
    const std::string& SelectedN80Rom() const { return selectedN80Rom_; }
    uint32_t Fingerprint() const { return fingerprint_; }
private:
    std::filesystem::path directory_;
    std::string selectedN80Rom_;
    uint32_t fingerprint_ = 0;
};
} // namespace M88V
