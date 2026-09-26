// SPDX-License-Identifier: BSD-2-Clause
#include "headers.h"
#include "opna.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace {
void Check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
struct Fixture {
    std::filesystem::path dir=std::filesystem::temp_directory_path()/
        ("m88-rhythm-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {std::filesystem::create_directory(dir);Rom(8192);}
    void Rom(size_t size) {std::ofstream f(dir/"ym2608_adpcm_rom.bin",std::ios::binary);
        std::vector<char> data(size,0x12);f.write(data.data(),data.size());Check(bool(f),"write fixture");}
    ~Fixture() {for(auto& e:std::filesystem::directory_iterator(dir))std::filesystem::remove(e.path());std::filesystem::remove(dir);}
    std::string Path() const {return dir.string()+"/";}
};
void Trigger(FM::OPNA& op,unsigned channel,unsigned pan=0xc0) {
    op.SetReg(7,63);op.SetReg(0x11,63);op.SetReg(0x18+channel,pan|31);op.SetReg(0x10,1<<channel);
}
std::vector<int32_t> Render(FM::OPNA& op,unsigned samples,unsigned chunk=0) {
    std::vector<int32_t> out(samples*2);
    if(!chunk)chunk=samples;
    for(unsigned i=0;i<samples;i+=chunk)op.Mix(out.data()+i*2,std::min(chunk,samples-i));
    return out;
}
bool Audible(const std::vector<int32_t>& b) {return std::any_of(b.begin(),b.end(),[](int32_t v){return v!=0;});}
void Put16(std::ofstream& f,unsigned v) {f.put(char(v));f.put(char(v>>8));}
void Put32(std::ofstream& f,unsigned v) {Put16(f,v);Put16(f,v>>16);}
void Wav(const std::filesystem::path& path,const std::vector<int32_t>& samples,unsigned channels,unsigned rate) {
    std::ofstream f(path,std::ios::binary);f.write("RIFF",4);Put32(f,36+unsigned(samples.size())*2);
    f.write("WAVEfmt ",8);Put32(f,16);Put16(f,1);Put16(f,channels);Put32(f,rate);
    Put32(f,rate*channels*2);Put16(f,channels*2);Put16(f,16);f.write("data",4);Put32(f,unsigned(samples.size())*2);
    for(auto v:samples)Put16(f,unsigned(std::clamp(v,-32768,32767)));
    Check(bool(f),"write WAV");
}
}
int main(int argc,char** argv) {
    try {
        Fixture f;const auto path=f.Path();
        FM::OPNARhythm core;Check(core.Load(path.c_str()),"load ROM");
        const int gains[]={65536,65536,65536,65536,65536,65536};
        // Independent known-vector: 0x12 => +6 then +10, 12-bit ADPCM-A.
        // With maximum level, ymfm's DAC quantization gives 44 then 120.
        for(unsigned ch=0;ch<6;++ch) {
            core.Reset();core.Write(0x11,63);core.Write(0x18+ch,0xdf);core.Write(0x10,1<<ch);
            std::array<int32_t,10> out{};core.Mix(out.data(),5,43200,100,0,0,gains);
            Check(out[0]==0 && out[1]==0,"initial phase");
            const unsigned step=ch<4?1:2;
            Check(out[step*2]==44 && out[step*2+1]==44,"first nibble/rate");
            Check(out[step*4]==120,"second nibble");
            auto state=core.Save();auto bad=state;bad[48+ch*24+20]=255;
            Check(!core.Restore(bad),"invalid predictor accepted");Check(core.Save()==state,"failed restore mutated state");
            core.Write(0x10,1<<ch);out.fill(0);core.Mix(out.data(),5,43200,100,0,0,gains);
            // Key-on resets the predictor, but leaves the common clock phase running.
            Check(out[2]==44,"retrigger predictor");
        }
        for(unsigned rate:{8000u,44100u,48000u}) for(unsigned ch=0;ch<6;++ch) {
            FM::OPNA op;Check(op.Init(7987200,rate,false,path.c_str()),"OPNA init");
            Check(op.UsesRhythmROM(),"ROM selection");Trigger(op,ch);
            Render(op,137);auto state=op.SaveRhythmState();
            auto expected=Render(op,rate);Check(Audible(expected),"instrument silent");
            Check(op.RestoreRhythmState(state),"restore predictor");
            Check(Render(op,rate,17)==expected,"state/chunk continuity");
            Check(!Audible(Render(op,rate)),"instrument failed to stop");
            op.Reset();Trigger(op,ch,0x80);auto left=Render(op,1000);
            for(size_t i=1;i<left.size();i+=2)Check(left[i]==0,"left pan leaked");
            op.Reset();Trigger(op,ch,0x40);auto right=Render(op,1000);
            for(size_t i=0;i<right.size();i+=2)Check(right[i]==0,"right pan leaked");
            op.SetReg(0x11,0);Check(!Audible(Render(op,100)),"total level mute");op.SetReg(0x11,63);
            op.SetReg(0x10,0x80|(1<<ch));Render(op,20);
            Check(!Audible(Render(op,100)),"key off");
            op.Reset();Trigger(op,ch);op.SetChannelMask(1u<<(10+ch));
            Check(!Audible(Render(op,rate*2)),"mute mask");op.SetChannelMask(0);
            Check(!Audible(Render(op,100)),"muting stopped decoder time");
            op.Reset();Trigger(op,ch);op.SetVolumeRhythmTotal(-192);
            Check(!Audible(Render(op,100)),"user mute");
        }
        // Missing or malformed ROM falls back to the established WAV path.
        for(auto name:{"BD","SD","TOP","HH","TOM","RIM"})
            Wav(f.dir/(std::string("2608_")+name+".WAV"),std::vector<int32_t>(256,1000),1,8000);
        for(size_t size:{0u,8191u,8193u}) {
            f.Rom(size);FM::OPNA op;Check(op.Init(7987200,44100,false,path.c_str()),"fallback init");
            Check(!op.UsesRhythmROM(),"malformed ROM selected");Trigger(op,0);
            Check(Audible(Render(op,100)),"WAV fallback silent");
            auto state=op.SaveRhythmState();auto expected=Render(op,100);
            Check(op.RestoreRhythmState(state)&&Render(op,100)==expected,"WAV continuity");
        }
        f.Rom(8192);FM::OPNA priority;Check(priority.Init(7987200,44100,false,path.c_str()),"priority init");
        Check(priority.UsesRhythmROM(),"WAV overrode ROM");
        auto identity=priority.SaveRhythmState();identity[196]^=1;
        Check(!priority.RestoreRhythmState(identity),"different ROM identity accepted");
        if(argc==3) {
            FM::OPNA op;std::string rompath=std::string(argv[1])+"/";
            Check(op.Init(7987200,44100,false,rompath.c_str())&&op.UsesRhythmROM(),"real ROM load");
            std::vector<int32_t> demo;
            for(unsigned ch=0;ch<6;++ch) {op.Reset();Trigger(op,ch);auto hit=Render(op,44100);
                Check(Audible(hit),"real instrument silent");demo.insert(demo.end(),hit.begin(),hit.end());}
            Wav(argv[2],demo,2,44100);
        }
        std::cout<<"Rhythm: vectors, six channels, rates, pan, key-off, mute, continuity and WAV fallback passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
