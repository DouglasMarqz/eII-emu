#include "runtime_audio.h"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <future>
#include <memory>
#include <stdexcept>
using namespace eii;
static void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
int main()try {
    using namespace analog;
    const auto cfg=NonlinearPlaybackVoice::config();
    require(cfg.caps.c1==20e-9&&cfg.caps.c2==3.9e-9&&cfg.caps.c3==20e-9&&cfg.caps.c4==1.8e-9,"EII capacitor values");
    require(NonlinearPlaybackVoice::qCurrent(0)==0&&std::abs(NonlinearPlaybackVoice::qCurrent(4.408)-160e-6)<1e-12,"R146 plus internal 1.8k Q emitter approximation");
    ssm2045::Filter limit(cfg);limit.setFcPinVoltsImmediate(NonlinearPlaybackVoice::frequencyPinVolts(0));
    const double second=limit.cutoffHz()*std::sqrt(cfg.caps.c1*cfg.caps.c2/(cfg.caps.c3*cfg.caps.c4));
    require(second<.49*cfg.sampleRate,"EII FCV open extreme fits96k without hidden cutoff clamp");
    NonlinearPlaybackVoice silent;
    for(unsigned n=0;n<1000;++n)require(silent.process({127,255},{0,true},{2.5,true},{0,true})==0,"no invented offset/noise");
    require(silent.process({127,255},{5,true},{2.5,true},{0,false})==0,"unknown Q is guarded");
    // Real EII caps, not the upstream demonstration circuit: exercise Q and
    // control extremes with bounded hot DAC input and explicit time history.
    NonlinearPlaybackVoice zeroQ,resonant;
    double a=0,b=0;
    for(unsigned n=0;n<192000/5;++n) {
        const KnownByte code{std::uint8_t((n/96)&1?127:255),255};
        const double x=zeroQ.process(code,{4,true},{2.5,true},{0,true});
        const double y=resonant.process(code,{4,true},{2.5,true},{3.3,true});
        require(std::isfinite(x)&&std::isfinite(y),"nonlinear/Q output stays finite");
        if(n>19200){a+=x*x;b+=y*y;}
    }
    require(a>1e-8&&std::abs(b-a)>a*.01,"QCV changes rendered audio");
    for(double fc:{0.,5.})for(double q:{0.,5.}) {
        NonlinearPlaybackVoice voice;
        for(unsigned n=0;n<4000;++n)require(std::isfinite(voice.process({std::uint8_t(n&1?127:255),255},{5,true},{fc,true},{q,true})),"FC/Q extremes finite");
    }
    // Freeze the consumer deliberately. The producer must remain bounded,
    // dropping old snapshots rather than waiting or accumulating latency.
    std::promise<void> entered,release;auto gate=release.get_future().share();
    std::atomic<bool> first=true;
    {
        // ~1.8 MB of inline rings: heap-allocate as RuntimeAudioEngine does
        // (a stack instance overflows the 1 MB default Windows main stack).
        auto owner=std::make_unique<ChipAudioWorker>([&](auto pcm){
            for(float x:pcm)require(std::isfinite(x),"worker PCM finite");
            if(first.exchange(false)){entered.set_value();gate.wait();}
        });
        auto& worker=*owner;
        NonlinearVoiceBank::Input input;
        for(unsigned n=0;n<96;++n)worker.push(input);
        require(entered.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready,"DSP consumer starts");
        for(unsigned n=0;n<96*12;++n)worker.push(input);
        const auto s=worker.stats();
        require(s.queuedFrames==768&&s.dropped==96*4,"DSP backlog bounded to16ms");
        release.set_value();
    }
    // Final partial block must reach the sink, exact frame order/count, no
    // callback on the emulation producer thread; teardown joins the worker.
    std::promise<void> complete;std::atomic<unsigned> frames=0;std::atomic<bool> correctThread=true;
    const auto producer=std::this_thread::get_id();
    {
        RuntimeAudioEngine engine([&](auto pcm){
            if(std::this_thread::get_id()==producer)correctThread=false;
            if(frames.fetch_add(unsigned(pcm.size()))+pcm.size()==137)complete.set_value();
        },RuntimeAudioMode::NonlinearChip192);
        for(unsigned n=0;n<137;++n)engine.sample({});
        engine.flush();
        require(complete.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready,"partial block flush");
    }
    require(frames==137&&correctThread,"DSP frame count and single separate owner");
    for(unsigned factor:{2u,4u}) {
    NonlinearVoiceBank bank(factor);NonlinearVoiceBank::Input input;
    for(unsigned ch=0;ch<8;++ch){input.playback.amplitudeCv[ch]={4,true};input.frequency[ch]={2.5,true};input.resonance[ch]={2.5,true};}
    input.channel0Playback=true;
    const auto start=std::chrono::steady_clock::now();double checksum=0;
    for(unsigned n=0;n<48000;++n){for(unsigned ch=0;ch<8;++ch)input.playback.codes[ch]={std::uint8_t(((n+ch*7)/24)&1?127:255),255};checksum+=bank.process(input);}
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    require(std::isfinite(checksum),"eight-voice mix finite");
    std::cout<<"PASS: EII adapter/Q/unknown guards; bounded separate DSP owner; partial flush and teardown. Eight voices one audio second: "<<seconds<<" wall seconds; rate="<<48000*factor<<" checksum="<<checksum<<"\n";
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
