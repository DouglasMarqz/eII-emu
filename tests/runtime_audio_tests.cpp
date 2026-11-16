#include "runtime_audio.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <limits>
#include "deglitch.h"
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main()try {
    using namespace eii;
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned savedCsr=_mm_getcsr();
    const unsigned referenceCsr=savedCsr&~0x8040u;
    _mm_setcsr(referenceCsr);
    volatile double tiny=std::numeric_limits<double>::denorm_min()*1024;
    volatile double referenceTiny=tiny*2;
    check(referenceTiny!=0,"reference arithmetic retains subnormals");
    const unsigned beforeScope=_mm_getcsr();
    try {
        const host::AudioFloatScope scope;
        volatile double hostTiny=tiny*2;
        check(hostTiny==0,"host audio flushes subnormal tails");
        throw 123;
    } catch(int) {}
    check(_mm_getcsr()==beforeScope,"host audio restores caller FP environment on exception");
    bool sinkScoped=false;
    analog::RuntimeAudioEngine scoped([&](auto){sinkScoped=(_mm_getcsr()&0x8040)==0x8040;});
    const unsigned beforeSamples=_mm_getcsr();
    for(unsigned n=0;n<256;++n)scoped.sample({});
    check(sinkScoped && _mm_getcsr()==beforeSamples,"PCM sample scope covers DSP and restores after block");
    _mm_setcsr(savedCsr);
#endif
    std::vector<host::StereoFrame> pcm;
    analog::RuntimeAudioEngine monitor([&](auto frames){pcm.insert(pcm.end(),frames.begin(),frames.end());},analog::RuntimeAudioMode::DacMonitor);
    analog::PlaybackInputs input;
    input.codes[1]={0xff,255};input.amplitudeCv[1]={2.5,true};
    for(unsigned tick=0;tick<4000000;++tick)if(monitor.clock())monitor.sample(input);
    monitor.flush();check(pcm.size()==48000,"exact rational frame count");
    const auto expected=analog::Am6072::eiiSteadyVolts(input.codes[1],input.amplitudeCv[1]).value/40;
    check(std::abs(pcm.back().left-expected)<1e-8 && pcm.back().left==pcm.back().right,"known DAC polarity/scale/stereo");
    input.codes[1].known=127;
    monitor.sample(input);monitor.flush();check(pcm.back().left==0,"unknown DAC code is muted");
    input.codes[0]={0xff,255};input.amplitudeCv[0]={5,true};
    monitor.sample(input);monitor.flush();check(pcm.back().left==0,"unresolved channel0 mode is muted");
    monitor.sample(input,{},true);monitor.flush();check(pcm.back().left<0,"known playback MODE admits held channel0 DAC");
    input.codes[0]={0,0};
    input.codes[1]={0x7f,255};input.amplitudeCv[1]={5,true};
    monitor.sample(input);monitor.flush();check(pcm.back().left>0,"opposite hardware sign reaches PCM");
    input.amplitudeCv[1]={5,false};monitor.sample(input);monitor.flush();
    check(pcm.back().left==0,"unknown amplitude CV is muted");
    analog::BasicPlaybackVoice low(48000),high(48000),silent(48000);
    double lowEnergy=0,highEnergy=0;
    for(unsigned n=0;n<48000;++n) {
        const KnownByte code{std::uint8_t((n/24)&1?0xff:0x7f),255};
        const auto a=low.process(code,{5,true},{0,true});
        const auto b=high.process(code,{5,true},{5,true});
        check(std::isfinite(a)&&std::isfinite(b),"basic VCF/VCA finite at FCV extremes");
        check(silent.process(code,{0,true},{5,true})==0,"firmware ACV zero mutes basic voice");
        if(n>=24000){lowEnergy+=a*a;highEnergy+=b*b;}
    }
    check(lowEnergy>highEnergy*100 && lowEnergy>1e-6,"increasing board FCV closes the filter, matching negative pin5 sensitivity");
    check(high.process({0xff,255},{5,true},{0,false})==0,"unknown FCV mutes/resets voice");
    check(std::isfinite(high.process({0xff,255},{5,true},{5,true})),"basic voice recovers after known CV");
    check(analog::BasicPlaybackVoice::nominalPoleHz(5)<analog::BasicPlaybackVoice::nominalPoleHz(0),"board FCV map direction");
    using Voice=analog::BasicPlaybackVoice;
    for(double fcv:{0.0,0.82,2.5,5.0}) {
        const auto v=Voice::frequencyPinVolts(fcv);
        check(std::abs((fcv-v)/22600.0+(-15-v)/124000.0-v/1000.0)<1e-18,"independent frequency-pin Kirchhoff residual");
    }
    const double step=.0193*(1+1000.0/22600+1000.0/124000)*22600/1000;
    check(std::abs(Voice::nominalPoleHz(2+step)/Voice::nominalPoleHz(2)-.5)<1e-12,"board voltage rise of one nominal pin octave halves f0");
    analog::Deglitch3202 switched;switched.resetKnownZero();
    for(unsigned n=0;n<1200;++n){switched.advance(1.6e-6,{.001,true},{true,true},{true,false});switched.advance(1.6e-6,{0,true},{true,false},{true,true});}
    check(std::abs(switched.output().value-.001*Voice::nominalDeglitchGain)<1e-9,"basic path preserves existing switched deglitch DC gain and inversion");
    host::AudioQueue realtime;
    std::vector<host::StereoFrame> stalled(8000);
    for(unsigned n=0;n<stalled.size();++n)stalled[n]={float(n),-float(n)};
    realtime.push(stalled);
    check(realtime.stats().queued==960 && realtime.stats().highWater==960 &&
          realtime.stats().dropped==7040,"stalled consumer cannot build160ms of stale audio");
    std::array<host::StereoFrame,960> latest;
    realtime.pop(latest);
    check(latest.front().left==7040 && latest.back().left==7999,
          "real-time queue preserves newest chronological audio without replaying old backlog");
    realtime.setLimit(1440);realtime.push(std::span(stalled.data(),1440));
    std::vector<host::StereoFrame> deviceBlock(1440);realtime.pop(deviceBlock);
    check(realtime.stats().missing==0 && deviceBlock.back().left==1439,
          "larger endpoint periods can consume a complete buffer without forced underruns");
    realtime.push(stalled);const auto dropped=realtime.stats().dropped;
    realtime.setLimit(960);
    check(realtime.stats().queued==960 && realtime.stats().dropped==dropped+480,
          "changing endpoint bound trims oldest audio safely");
    host::AudioQueue queue(host::AudioQueue::capacity);
    std::vector<host::StereoFrame> in(host::AudioQueue::capacity+3);
    for(unsigned i=0;i<in.size();++i)in[i]={float(i),-float(i)};
    queue.push(in);check(queue.stats().dropped==3,"bounded queue drops oldest on overflow");
    std::vector<host::StereoFrame> out(host::AudioQueue::capacity+2);queue.pop(out);
    check(out[0].left==3 && out[8191].left==8194 && out[8192].left==0 && queue.stats().missing==2,
          "FIFO and silence on underrun");
    queue.push(std::span(in.data(),10));queue.clear();queue.pop(std::span(out.data(),1));
    check(out[0].left==0,"reset clears pending audio");
    std::atomic<bool> done=false;std::atomic<bool> valid=true;
    std::thread producer([&]{for(unsigned n=0;n<1000;++n)queue.push(std::span(in.data(),128));done=true;});
    std::thread consumer([&]{std::array<host::StereoFrame,111> block;while(!done||queue.stats().queued){
        queue.pop(block);for(auto f:block)if(f.left!=-f.right)valid=false;}});
    producer.join();consumer.join();check(valid,"concurrent block transport preserves complete frames");
    std::cout<<"PASS: basic VCF/VCA attenuation/stability/ACV/FCV/unknown recovery; rational48k DAC monitor, polarity/CV/unknown/channel0 guards, bounded FIFO/drop/underflow/reset and concurrent transport\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
