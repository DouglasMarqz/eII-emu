#include "nonlinear_voice.h"
#include "host_audio_float.h"
#include <chrono>
#include <iostream>
#include <cmath>
int main(){eii::host::AudioFloatScope fp;eii::analog::NonlinearVoiceBank bank(4,false);eii::analog::NonlinearVoiceBank::Input in{};in.channel0Playback=true;
for(unsigned ch=0;ch<8;++ch){in.playback.amplitudeCv[ch]={2.5,true};in.frequency[ch]={2.,true};in.resonance[ch]={1.5,true};}
for(unsigned window=0;window<6;++window){auto before=bank.solverStats();auto start=std::chrono::steady_clock::now();double energy=0;
for(unsigned n=0;n<240000;++n){for(unsigned ch=0;ch<8;++ch)in.playback.codes[ch]={std::uint8_t(window<3?((n/48)%2?0x3f:0xbf):0x7f),255};float y=bank.process(in);if(!std::isfinite(y))return 1;energy+=double(y)*y;}
auto after=bank.solverStats();std::cout<<"window="<<window<<" phase="<<(window<3?"signal":"tail")<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" iterations="<<after.iterations-before.iterations<<" samples="<<after.samples-before.samples<<" fallbacks="<<after.fallbacks-before.fallbacks<<" energy="<<energy<<std::endl;}}
