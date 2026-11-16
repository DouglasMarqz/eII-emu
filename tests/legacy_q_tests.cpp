#include "runtime_audio.h"
#include "vst3/plugin_state.h"
#include "reference/nonlinear_voice_ce2e306.h"
#include <chrono>
#include <iostream>
#include <random>
#include <bit>
using namespace eii::analog;
using frozen_ce2e306::GoldenPlaybackVoice;using frozen_ce2e306::GoldenVoiceBank; // rebaselined oracle, see header provenance
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);std::cout<<"PASS "<<m<<'\n';}
int main()try{
    {
        FilterCalibration cal;LegacyQLinearVca legacy;legacy.calibrate(cal);
        Ssm2045LinearVca original;original.setControlCurrent(500e-6);
        check(std::abs(legacy.process(.1)-original.outputCurrent(.1))<1e-18,"Legacy amplifier matches original linear VCA nominal gain");
        ssm2045::VcaConfig config;config.iosVca=0;ssm2045::VcaMixer nonlinear(config);nonlinear.setLinearControlCurrents(0,500e-6);
        check(std::abs(legacy.process(1e-8)-nonlinear.process(0,1e-8).current)<1e-18,"Legacy and SSM VCA share the nominal small-signal reference");
        check(std::abs(legacy.process(1)-10*legacy.process(.1))<1e-17&&std::abs(legacy.process(1)-nonlinear.process(0,1).current)>1e-3,"Legacy linear and SSM saturating amplifiers are distinct models");
        cal[FilterParam::VcaGain]*=.5;legacy.calibrate(cal);
        check(std::abs(legacy.process(.1)-original.outputCurrent(.1)*.5)<1e-18,"Legacy VCA gain parameter applies to its own amplifier");
        check(!legacyQParameterSupported(unsigned(FilterParam::VcaRange)),"Legacy hides the unsupported nonlinear VCA saturation knob");
    }
    auto c=NonlinearPlaybackVoice::config(96000);
    {
        LegacyQFilter fast(c);Ssm2045OpenLoop old(96000,{c.caps.c1,c.caps.c2,c.caps.c3,c.caps.c4});
        const double gm=c.gm0*std::exp(c.fcBiasVolts/c.vtef);old.setTransconductance(gm);
        double error=0,energy=0;
        for(int n=0;n<20000;++n){double u=n==0?.01:0;auto a=fast.process(u,0,0);auto b=old.process(u);error+=(a.fourPole-b.fourPole)*(a.fourPole-b.fourPole);energy+=b.fourPole*b.fourPole;}
        std::cout<<"zero Q relative error "<<std::sqrt(error/energy)<<'\n';
        check(error<energy*1e-20,"zero Q matches original two-section trapezoidal kernel");
    }
    for(double q:{0.,30e-6,60e-6}){
        auto linear=c;linear.nonlinear=false;linear.offsets=false;
        ssm2045::Filter ref(linear);ref.setFcPinVoltsImmediate(0);ref.setQCurrent(q);ref.reset();LegacyQFilter fast(c);
        double error=0,energy=0;
        for(int n=0;n<12000;++n){double u=1e-5*std::sin(n*.04);auto a=fast.process(u,0,q);auto b=ref.process(u);error+=std::pow(a.fourPole-b.fourPole,2);energy+=b.fourPole*b.fourPole;}
        std::cout<<"Q "<<q<<" small signal relative error "<<std::sqrt(error/energy)<<'\n';
        check(error<energy*1e-5,"Q small-signal response matches independent four-state SSM solver");
    }
    {
        double maxPeak=0;std::uint64_t fallback=0;
        for(double fcv:{0.,2.5,5.})for(double q:{0.,150e-6,300e-6}){
            LegacyQFilter f(c);for(int n=0;n<24000;++n){double u=n<12000?std::sin(n*.17):0;
                auto y=f.process(u,NonlinearPlaybackVoice::frequencyPinVolts(fcv),q);
                if(!std::isfinite(y.fourPole)||std::abs(y.fourPole)>100)throw std::runtime_error("unstable high Q");maxPeak=std::max(maxPeak,std::abs(y.fourPole));}
            fallback+=f.solverStats().fallbacks;
        }
        std::cout<<"stress peak "<<maxPeak<<" scalar fallbacks "<<fallback<<'\n';
        check(true,"FC/Q extremes and post-excitation resonance remain bounded");
    }
    {
        bool equal=true;
        for(unsigned rate:{96000u,192000u}){
            NonlinearPlaybackVoice current(rate);current.levelModel(LevelModel::ce2e306());GoldenPlaybackVoice golden(rate);FilterCalibration cal;current.calibrate(cal);golden.calibrate(cal);
            for(unsigned n=0;n<4000;++n){const eii::KnownByte code{std::uint8_t((n/19)&1?127:255),255};KnownSignal acv{4,true},fcv{2.5,true},qcv{2.2,true};
                const auto a=current.process(code,acv,fcv,qcv),b=golden.process(code,acv,fcv,qcv);equal&=std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
        }
        check(equal,"accepted nonlinear SSM voice remains bit-identical at 96/192k");
    }
    {
        eii::vst::PluginSettings s;s.audioMode=RuntimeAudioMode::LegacyQ;s.nativeAudio=true;s.legacyCalibration[FilterParam::Qscale]=.7;s.calibration[FilterParam::FcRin]=31;s.calibration[FilterParam::Qres]=27;s.calibration[FilterParam::Gm0]=88;s.calibration[FilterParam::W1]=2.4;
        auto back=eii::vst::parseSettings(eii::vst::serializeSettings(s));
        check(back==s&&back.calibration[FilterParam::Qscale]==1&&back.calibration[FilterParam::FcRin]==31&&back.calibration[FilterParam::Gm0]==88,"Legacy Q profile and mode round trip without modifying SSM profile");
        auto isolated=s.calibration;isolated.resetGroupDefaults(1);
        check(isolated[FilterParam::FcRin]==31&&isolated[FilterParam::Qres]==27&&isolated[FilterParam::Gm0]==FilterCalibration{}[FilterParam::Gm0],"reset chip FC/Q group preserves external board FC/Q values");
        isolated=s.calibration;isolated.resetGroupDefaults(0);
        check(isolated[FilterParam::Gm0]==88&&isolated[FilterParam::W1]==2.4&&isolated[FilterParam::FcRin]==FilterCalibration{}[FilterParam::FcRin],"reset external board FC/Q preserves chip FC/Q tuning");
        auto old=eii::vst::parseSettings("EIIVST3 1\nmodel=2\n");check(old.audioMode==RuntimeAudioMode::NonlinearChip192&&old.legacyCalibration==FilterCalibration{},"old plugin state keeps old model and nominal new profile");
        check(!legacyQParameterSupported(unsigned(FilterParam::Offsets))&&!legacyQParameterSupported(unsigned(FilterParam::G1Limit))&&legacyQParameterSupported(unsigned(FilterParam::GaLimit)),"service hides unsupported VCF saturation/offset controls");
    }
    {
        NonlinearPlaybackVoice voice(96000,true);voice.calibrate(FilterCalibration{});
        check(voice.process({127,255},{4,true},{2.5,true},{0,false})==0,"Legacy Q unknown resonance is guarded");
        check(nativeTimeRate==120000000&&nativeFrameTime==2500,"native hardware event time grid and 48k packet cadence unchanged");
    }
    {
        NonlinearVoiceBank active(2,true),held(2,true),ssm(2);GoldenVoiceBank golden(2);
        FilterCalibration cal;active.calibrate(cal);held.calibrate(cal);ssm.calibrate(cal);golden.calibrate(cal);ssm.levelModel(LevelModel::ce2e306()); // only the level model differs
        double energy=0,heldEnergy=0,ssmEnergy=0;bool equal=true;
        for(unsigned n=0;n<3000;++n){
            NonlinearVoiceBank::Input in;in.native.enabled=true;in.native.valid=true;in.native.start=std::uint64_t(n)*nativeFrameTime;in.native.initial.channel0=false;
            in.reconstructionEnabled=true;
            for(unsigned ch=0;ch<8;++ch){auto& pins=in.native.initial;pins.dac[ch]={std::uint8_t((n/48)&1?0xb8:0x38),255};pins.cv[0][ch]={180,255};pins.cv[1][ch]={128,255};pins.cv[2][ch]={0,255};pins.sw[ch]={35,63};pins.phases[ch]=13;
                in.native.events[in.native.count++]={500,NativeEvent::Pins,std::uint8_t(ch),pins.dac[ch].value,255,14};
                in.native.events[in.native.count++]={900,NativeEvent::Pins,std::uint8_t(ch),pins.dac[ch].value,255,12};}
            // Native event array is sorted by time, not by channel.
            std::stable_sort(in.native.events.begin(),in.native.events.begin()+in.native.count,[](const auto& a,const auto& b){return a.offset<b.offset;});
            GoldenVoiceBank::Input original;original.native=in.native;original.reconstructionEnabled=true;
            const float a=ssm.process(in),b=golden.process(original);equal&=std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);ssmEnergy+=double(a)*a;
            const float v=active.process(in);energy+=v*v;
            auto silent=in;for(auto& phase:silent.native.initial.phases)phase=12;for(unsigned i=0;i<silent.native.count;++i)silent.native.events[i].phases=12;
            const float h=held.process(silent);heldEnergy+=h*h;
        }
        check(equal&&ssmEnergy>1e-12,"accepted SSM native DAC/deglitch/S3528 route remains bit-identical (non-silent)");
        check(energy>1e-10&&heldEnergy==0,"Legacy Q native audio requires actual hardware deglitch switching events");
    }
    for(bool fast:{false,true}){
        NonlinearVoiceBank bank(2,fast);bank.calibrate(FilterCalibration{});NonlinearVoiceBank::Input in;
        in.native.enabled=true;in.native.valid=true;in.native.initial.channel0=false;in.reconstructionEnabled=true;
        for(unsigned ch=0;ch<8;++ch){auto& pins=in.native.initial;pins.cv[0][ch]={180,255};pins.cv[1][ch]={128,255};pins.cv[2][ch]={128,255};pins.sw[ch]={35,63};pins.phases[ch]=13;
            in.native.events[in.native.count++]={500,NativeEvent::Pins,std::uint8_t(ch),0xb8,255,14};
            in.native.events[in.native.count++]={900,NativeEvent::Pins,std::uint8_t(ch),0xb8,255,12};}
        std::stable_sort(in.native.events.begin(),in.native.events.begin()+in.native.count,[](const auto& a,const auto& b){return a.offset<b.offset;});
        auto t=std::chrono::steady_clock::now();double sum=0;
        for(unsigned n=0;n<48000;++n){in.native.start=std::uint64_t(n)*nativeFrameTime;const auto code=std::uint8_t((n/48)&1?0xb8:0x38);
            for(auto& dac:in.native.initial.dac)dac={code,255};for(unsigned j=0;j<in.native.count;++j)in.native.events[j].value=code;
            sum+=bank.process(in);}
        std::cout<<"BENCH_NATIVE "<<(fast?"LegacyQ":"SSM96")<<" DAC/deglitch+VCF+S3528, 7 voices, 1 emulated audio second: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count()<<" wall seconds; checksum "<<sum<<'\n';
        check(std::isfinite(sum),"native bank output finite");
    }
    for(bool fast:{false,true}){
        NonlinearVoiceBank bank(2,fast);bank.calibrate(FilterCalibration{});NonlinearVoiceBank::Input in;in.channel0Playback=true;
        for(unsigned ch=0;ch<8;++ch){in.playback.amplitudeCv[ch]={4,true};in.frequency[ch]={2.5,true};in.resonance[ch]={2.5,true};}
        auto t=std::chrono::steady_clock::now();double sum=0;
        for(unsigned n=0;n<48000;++n){for(unsigned ch=0;ch<8;++ch)in.playback.codes[ch]={std::uint8_t(((n+ch*7)/24)&1?127:255),255};sum+=bank.process(in);}
        std::cout<<"BENCH "<<(fast?"LegacyQ":"SSM96")<<" 8 voices 1 emulated audio second: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count()<<" wall seconds; checksum "<<sum<<'\n';
        check(std::isfinite(sum),"bank output finite");
    }
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
