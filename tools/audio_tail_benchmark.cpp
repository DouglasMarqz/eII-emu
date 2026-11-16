#include "runtime_audio.h"
#include <chrono>
#include <iostream>

// Asset-free reproduction of the note-release CPU regression. Build this
// unchanged against each revision with the same release compiler/flags.
int main() {
    eii::analog::PlaybackInputs input;
    std::array<eii::analog::KnownSignal,8> frequency;
    for(unsigned ch=0;ch<8;++ch) {
        input.codes[ch]={0x7f,255};
        input.amplitudeCv[ch]={3.65,true};frequency[ch]={2.5,true};
    }
    double peak=0;
    eii::analog::RuntimeAudioEngine engine([&](auto frames){
        for(auto f:frames)peak=std::max(peak,double(std::abs(f.left)));
    });
    const auto run=[&](const char* label,unsigned frames){
        const auto start=std::chrono::steady_clock::now();
        for(unsigned n=0;n<frames;++n)engine.sample(input,frequency,true);
        engine.flush();
        std::cout<<label<<" frames="<<frames<<" wall="
                 <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()
                 <<" peak="<<peak<<std::endl;peak=0;
    };
    run("held",48000);
    for(auto& cv:input.amplitudeCv)cv={0,true};
    run("release",240000);
    run("silent-tail",2000000);
}
