// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "ifcommon.h"
#include <cstdint>
#include <vector>
class PC88;

class HeadlessAudio final : public ISoundControl {
public:
    void Attach(PC88& pc);
    void Detach();
    void Reset(uint32_t phase = 0);
    uint32_t Phase() const { return phase_; }
    void Capture(bool capture) { capture_ = capture; }
    void Suspend(bool suspend) { suspended_ = suspend; }
    std::vector<int16_t> TakeSamples() { std::vector<int16_t> result; result.swap(pending_); return result; }
    bool IFCALL Connect(ISoundSource* source) override;
    bool IFCALL Disconnect(ISoundSource* source) override;
    bool IFCALL Update(ISoundSource*) override;
    int IFCALL GetSubsampleTime(ISoundSource*) override;
private:
    PC88* pc_ = nullptr;
    std::vector<ISoundSource*> sources_;
    std::vector<int16_t> pending_;
    uint32_t previous_ = 0, phase_ = 0;
    bool capture_ = false, suspended_ = false;
};
