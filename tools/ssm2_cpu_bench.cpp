// CPU cost of every worker VCF/VCA route: 8 voices, 2 audio seconds, reference
// 48 k packets with moving FCV/QCV/ACV and DAC tones. Thread CPU seconds per audio second.
#include "nonlinear_voice.h"
#include "host_thread_cpu.h"
#include "host_audio_float.h"
#include <cstdio>
#include <memory>
using namespace eii;
int main(){
    const host::AudioFloatScope floats;
    struct R{const char* name;unsigned factor;bool q,e,m2;};
    for(int rep=0;rep<2;++rep)
    for(const R r:{R{"SSM2045 96k",2,false,false,false},R{"SSM2045 192k",4,false,false,false},R{"Legacy+Q 96k",2,true,false,false},R{"EIII CEM 96k",2,false,true,false},R{"SSM2045 model2 96k",2,false,false,true},R{"SSM2045 model2 192k",4,false,false,true}}){
        auto bank=std::make_unique<analog::PlaybackVoiceBank>(r.factor,r.q,r.e,r.m2);bank->calibrate({});
        const unsigned frames=96000;double acc=0;const double t0=host::threadCpuSeconds();
        for(unsigned n=0;n<frames;++n){analog::PlaybackVoiceBank::Input in;in.channel0Playback=true;
            for(unsigned ch=0;ch<8;++ch){const double t=n/48000.0;const int v=int(std::lround(60*std::sin(2*3.14159265*(110.0*(ch+1))*t)));
                in.playback.codes[ch]={std::uint8_t(v<0?0x80|std::min(127,-v):std::min(127,v)),255};in.playback.amplitudeCv[ch]={3+1.5*std::sin(n*1e-4+ch),true};
                in.frequency[ch]={2.5+2*std::sin(n*7e-5+ch),true};in.resonance[ch]={ch<4?0.0:2.5+2*std::sin(n*5e-5+ch),true};}
            acc+=bank->process(in);}
        const double cpu=host::threadCpuSeconds()-t0;const auto s=bank->solverStats();
        if(rep)std::printf("BENCH %-22s 8 voices: %.4f CPU s per audio s  (iterations/sample %.2f, fallbacks %llu) acc=%g\n",r.name,cpu/2,s.samples?double(s.iterations)/double(s.samples):0.0,(unsigned long long)s.fallbacks,acc);
    }
    return 0;
}
