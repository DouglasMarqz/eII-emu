// Asset-free PCM fingerprints of every existing VCF/VCA route (2026-10-29 audit gate).
// Deterministic eight-voice stimulus with changing DAC/ACV/FCV/QCV and S3528 codes;
// reference (48k-held) and native (event) inputs; S3528 OFF and ON. Prints FNV-1a
// of the float PCM. Compare outputs before/after a change: identical lines = bit exact.
#include "runtime_audio.h"
#include "host_audio_float.h"
#include <bit>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>
using namespace eii;
struct Fnv{std::uint64_t h=14695981039346656037ull;void add(float f){auto u=std::bit_cast<std::uint32_t>(f);for(unsigned i=0;i<4;++i){h^=std::uint8_t(u>>(8*i));h*=1099511628211ull;}}};
static constexpr unsigned frames=12000;
static analog::PlaybackVoiceBank::Input referenceInput(unsigned n){
    analog::PlaybackVoiceBank::Input in;in.channel0Playback=true;
    for(unsigned ch=0;ch<8;++ch){
        const double t=n/48000.0;
        const double wave=std::sin(2*3.14159265358979*(110.0*(ch+1))*t)+.3*std::sin(2*3.14159265358979*(2900.0+700*ch)*t);
        const int v=int(std::lround(wave*55));
        in.playback.codes[ch]={std::uint8_t(v<0?0x80|std::min(127,-v):std::min(127,v)),std::uint8_t(n%2999==ch?0:255)};
        in.playback.amplitudeCv[ch]={2.5+2*std::sin(n*.0007+ch),true};
        in.frequency[ch]={2.5+2.4*std::sin(n*.0011+ch*.7),true};
        in.resonance[ch]={ch<4?0.0:2.5+2.4*std::sin(n*.0009+ch),true};
        in.reconstructionCode[ch]={std::uint8_t((ch*7+n/1500)%64),63};
    }
    return in;
}
static analog::PlaybackVoiceBank::Input nativeInput(unsigned n){
    using E=analog::NativeEvent;analog::PlaybackVoiceBank::Input in;in.native.enabled=true;in.native.start=std::uint64_t(n)*analog::nativeFrameTime;in.native.valid=true;
    auto& p=in.native.initial;p.channel0=true;
    for(unsigned ch=0;ch<8;++ch){const auto r=referenceInput(n);p.dac[ch]={r.playback.codes[ch].value,255};p.phases[ch]=12;
        p.cv[0][ch]={std::uint8_t(std::lround(r.playback.amplitudeCv[ch].value*256/5)),255};
        p.cv[1][ch]={std::uint8_t(std::lround(r.frequency[ch].value*256/5)),255};
        p.cv[2][ch]={std::uint8_t(std::lround(r.resonance[ch].value*256/5)),255};
        p.sw[ch]=r.reconstructionCode[ch];}
    unsigned count=0;
    for(unsigned off:{300u,900u,1400u,2000u})for(unsigned ch=0;ch<8;++ch){
        const std::uint8_t phase=off==300?13:off==900?12:off==1400?14:12;
        in.native.events[count++]={std::uint16_t(off+ch),E::Pins,std::uint8_t(ch),p.dac[ch].value,255,phase};}
    in.native.count=std::uint16_t(count);return in;
}
int main(){
    const host::AudioFloatScope floats;const analog::FilterCalibration nominal{};
    for(bool on:{false,true}){
        Fnv f;std::vector<analog::BasicPlaybackVoice> v(8,analog::BasicPlaybackVoice(48000));
        for(unsigned n=0;n<frames;++n){const auto in=referenceInput(n);double s=0;
            for(unsigned ch=0;ch<8;++ch)s+=v[ch].process(in.playback.codes[ch],in.playback.amplitudeCv[ch],in.frequency[ch],in.reconstructionCode[ch],on);
            f.add(float(std::clamp(s,-1.0,1.0)));}
        std::cout<<"FINGERPRINT legacy48 ref s3528="<<on<<" fnv="<<std::hex<<f.h<<std::dec<<'\n';
    }
    struct R{const char* name;unsigned factor;bool q,e,m2=false;};
    for(const R r:{R{"ssm96",2,false,false},R{"ssm192",4,false,false},R{"legacyq96",2,true,false},R{"eiii96",2,false,true},R{"ssm2m96",2,false,false,true},R{"ssm2m192",4,false,false,true}})
        for(bool native:{false,true})for(bool on:{false,true}){
            auto bank=std::make_unique<analog::PlaybackVoiceBank>(r.factor,r.q,r.e,r.m2);bank->calibrate(nominal);Fnv f;double e=0;
            for(unsigned n=0;n<frames;++n){auto in=native?nativeInput(n):referenceInput(n);in.reconstructionEnabled=on;const float y=bank->process(in);f.add(y);e+=double(y)*y;}
            std::cout<<"FINGERPRINT "<<r.name<<(native?" native":" ref")<<" s3528="<<on<<" fnv="<<std::hex<<f.h<<std::dec<<std::setprecision(6)<<" rms="<<std::sqrt(e/frames)<<'\n';
        }
    return 0;
}
