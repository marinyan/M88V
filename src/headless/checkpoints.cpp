// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026, marinyan
#include "headless_machine.h"
#include "development/snapshot.h"
#include "zlib/zlib.h"
#include <cstring>

namespace {
struct FrontendState {uint64_t frames=0;int32_t remaining=0;uint32_t version=1;std::array<uint8_t,16> keys{};};
struct ReplayHeader {char magic[8];uint32_t stateSize,eventCount,crc;};
}
bool HeadlessMachine::CaptureState(std::vector<uint8_t>& bytes,std::string& error) {
    FrontendState f{frameCount_,frameRemaining_,1,keyboard_.Rows()};
    std::vector<uint8_t> front(sizeof(f));std::memcpy(front.data(),&f,sizeof(f));
    return M88V::Snapshot::Capture(*this,config_,romIdentity_,front,bytes,error);
}
bool HeadlessMachine::RestoreState(const std::vector<uint8_t>& bytes,std::string& error) {
    // Inspect the fixed frontend trailer before the core can be changed.
    if(bytes.size()<sizeof(FrontendState)){error="Missing headless checkpoint metadata";return false;}
    FrontendState f{};std::memcpy(&f,bytes.data()+bytes.size()-sizeof(f),sizeof(f));
    if(f.version!=1||f.remaining<0||f.remaining>100000){error="Invalid headless checkpoint metadata";return false;}
    std::vector<uint8_t> front(sizeof(f));
    if(!M88V::Snapshot::Restore(*this,config_,romIdentity_,bytes,front,error))return false;
    frameCount_=f.frames;frameRemaining_=f.remaining;keyboard_.SetRows(f.keys);debugger_.Clear();return true;
}
bool HeadlessMachine::SaveState(const std::string& path,std::string& error) {
    std::vector<uint8_t> bytes;return CaptureState(bytes,error)&&M88V::Snapshot::WriteFile(path,bytes,error);
}
bool HeadlessMachine::LoadState(const std::string& path,std::string& error) {
    if(recording_){error="Stop input recording before loading a state";return false;}
    std::vector<uint8_t> bytes;return M88V::Snapshot::ReadFile(path,bytes,error)&&RestoreState(bytes,error);
}
void HeadlessMachine::RecordKeys() {if(recording_)inputEvents_.push_back({0,keyboard_.Rows()});}
bool HeadlessMachine::SetKey(int row,int bit,bool down) {
    if(recording_&&inputEvents_.size()>=50000)return false;
    if(!keyboard_.SetKey(row,bit,down))return false;RecordKeys();return true;
}
bool HeadlessMachine::SetNamedKey(const std::string& name,bool down) {
    if(recording_&&inputEvents_.size()>=50000)return false;
    if(!keyboard_.SetNamedKey(name,down))return false;RecordKeys();return true;
}
bool HeadlessMachine::ReleaseAllKeys() {
    if(recording_&&inputEvents_.size()>=50000)return false;
    keyboard_.ReleaseAll();RecordKeys();return true;
}
bool HeadlessMachine::StartRecording(std::string& error) {
    if(recording_){error="Already recording";return false;}
    if(debugger_.Stopped()||debugger_.HasWatches()){error="Clear watchpoints and resume before recording";return false;}
    if(!CaptureState(recordingState_,error))return false;
    inputEvents_.clear();recordedFrames_=0;recording_=true;return true;
}
bool HeadlessMachine::StopRecording(const std::string& path,std::string& error) {
    if(!recording_){error="Not recording";return false;}
    ReplayHeader h{};std::memcpy(h.magic,"M88VRPL1",8);h.stateSize=static_cast<uint32_t>(recordingState_.size());h.eventCount=static_cast<uint32_t>(inputEvents_.size());
    std::vector<uint8_t> file(sizeof(h));file.insert(file.end(),recordingState_.begin(),recordingState_.end());
    for(const auto& event:inputEvents_) {const auto* p=reinterpret_cast<const uint8_t*>(&event);file.insert(file.end(),p,p+sizeof(event));}
    h.crc=crc32(0,file.data()+sizeof(h),static_cast<uInt>(file.size()-sizeof(h)));std::memcpy(file.data(),&h,sizeof(h));
    if(!M88V::Snapshot::WriteFile(path,file,error))return false;
    recording_=false;inputEvents_.clear();recordingState_.clear();return true;
}
bool HeadlessMachine::Replay(const std::string& path,std::string& error) {
    if(recording_||debugger_.HasWatches()){error="Stop recording and clear watchpoints before replay";return false;}
    std::vector<uint8_t> file;if(!M88V::Snapshot::ReadFile(path,file,error))return false;
    if(file.size()<sizeof(ReplayHeader)){error="Truncated replay";return false;}
    ReplayHeader h{};std::memcpy(&h,file.data(),sizeof(h));
    if(std::memcmp(h.magic,"M88VRPL1",8)||h.eventCount>50000||uint64_t(sizeof(h))+h.stateSize+uint64_t(h.eventCount)*sizeof(InputEvent)!=file.size()||h.crc!=crc32(0,file.data()+sizeof(h),static_cast<uInt>(file.size()-sizeof(h)))){error="Replay checksum/length mismatch";return false;}
    std::vector<InputEvent> events(h.eventCount);std::memcpy(events.data(),file.data()+sizeof(h)+h.stateSize,events.size()*sizeof(InputEvent));
    uint64_t frames=0;for(const auto& e:events) {frames+=e.frames;if(e.frames>100000||frames>100000){error="Replay exceeds 100000 frame limit";return false;}}
    std::vector<uint8_t> state(file.begin()+sizeof(h),file.begin()+sizeof(h)+h.stateSize);
    if(!RestoreState(state,error))return false;
    for(const auto& e:events) {if(e.frames){if(!RunFrames(e.frames,&error))return false;}else keyboard_.SetRows(e.matrix);}
    return true;
}
