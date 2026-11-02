// SPDX-License-Identifier: BSD-2-Clause
#include "headless_audio.h"
#include "media_recorder.h"
#include "pc88/pc88.h"
#include "pc88/opnif.h"
#include "pc88/beep.h"
#include <algorithm>
#include <array>

void HeadlessAudio::Attach(PC88& pc) {
    pc_ = &pc; Reset();
    pc.GetOPN1()->Connect(this); pc.GetOPN2()->Connect(this); pc.GetBEEP()->Connect(this);
}
void HeadlessAudio::Detach() {
    // Sources disconnect themselves; keep this controller alive until they are detached.
    if (pc_) { pc_->GetOPN1()->Connect(nullptr); pc_->GetOPN2()->Connect(nullptr); pc_->GetBEEP()->Connect(nullptr); }
    pc_ = nullptr;
}
void HeadlessAudio::Reset(uint32_t phase) { previous_ = pc_ ? pc_->GetCPUTick() : 0; phase_ = phase; pending_.clear(); }
bool HeadlessAudio::Connect(ISoundSource* source) {
    if (std::find(sources_.begin(), sources_.end(), source) != sources_.end()) return false;
    source->SetRate(MediaRecorder::Rate); sources_.push_back(source); return true;
}
bool HeadlessAudio::Disconnect(ISoundSource* source) {
    sources_.erase(std::remove(sources_.begin(), sources_.end(), source), sources_.end()); return true;
}
int HeadlessAudio::GetSubsampleTime(ISoundSource*) {
    return int(uint64_t(phase_) * 2000 / (pc_->GetCPUSpeed() * 100000ULL));
}
bool HeadlessAudio::Update(ISoundSource*) {
    if (!pc_ || suspended_) return true;
    const uint32_t now = pc_->GetCPUTick();
    const uint64_t scaled = uint64_t(uint32_t(now - previous_)) * MediaRecorder::Rate + phase_;
    const uint64_t frequency = pc_->GetCPUSpeed() * 100000ULL;
    uint64_t frames = scaled / frequency;
    phase_ = uint32_t(scaled % frequency); previous_ = now;
    while (frames) {
        const int count = int(std::min<uint64_t>(frames, 2048));
        std::array<int32, 4096> mixed{};
        for (auto* source : sources_) source->Mix(mixed.data(), count);
        if (capture_) for (int i = 0; i < count * 2; ++i)
            pending_.push_back(int16_t(std::clamp(mixed[i], int32(-32768), int32(32767))));
        frames -= count;
    }
    return true;
}
