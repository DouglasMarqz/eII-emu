// 2026-10-29 analog audit regressions (docs/ANALOG_AUDIT_S3528_SSM2045.md):
// S3528 board input stage gain and per-code routing in every route, EII -BIAS
// VCA threshold and the SSM2045 internal 1.8k Q emitter resistor.
#include "runtime_audio.h"
#include "host_audio_float.h"
#include <bit>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
using namespace eii;using namespace eii::analog;
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static constexpr double pi=3.14159265358979323846;
// Least-squares ratio of b onto a after settling: sum(a*b)/sum(a*a).
struct Ratio{double ab=0,aa=0,bb=0;void add(double a,double b){ab+=a*b;aa+=a*a;bb+=b*b;}double value()const{return ab/aa;}double rms(double n)const{return std::sqrt(bb/n);}};
static NativeFrame nativeFrame(unsigned n,std::uint8_t code,std::uint8_t sw){
    NativeFrame f;f.enabled=true;f.valid=true;f.start=std::uint64_t(n)*nativeFrameTime;auto& p=f.initial;p.channel0=false;
    for(unsigned ch=0;ch<8;++ch){p.dac[ch]={ch==1?code:std::uint8_t(0),255};p.phases[ch]=12;p.cv[0][ch]={std::uint8_t(ch==1?150:0),255};p.cv[1][ch]={20,255};p.cv[2][ch]={0,255};p.sw[ch]={sw,63};}
    unsigned count=0;for(unsigned off:{300u,900u,1400u,2000u}){
        f.events[count++]={std::uint16_t(off),NativeEvent::Pins,1,code,255,std::uint8_t(off==300?13:off==1400?14:12)};}
    f.count=std::uint16_t(count);return f;
}
static std::uint8_t sineCode(unsigned n,double hz,double amplitude){
    const int v=int(std::lround(amplitude*std::sin(2*pi*hz*n/48000.0)));return std::uint8_t(v<0?0x80|std::min(127,-v):std::min(127,v));
}
int main()try{
    const host::AudioFloatScope floats;
    constexpr auto level=LevelModel::hardware();
    require(s3528BoardInputGain==-10000.0/4700.0&&level.s3528OffInputStage&&level.s3528OnMatch==1,"7-45 R58/R66 input stage in both S3528 states; one host level");
    constexpr double expected=1; // ON/OFF: both carry -R58/R66; only the S3528 filter (0 dB passband) differs
    require(std::abs(VcaBoardControl::biasVolts-(-0.26884))<1e-12&&std::abs(VcaBoardControl::threshold-0.23116)<1e-12,"7-52 -BIAS shifts VCA2 turn-on point");
    require(std::abs(NonlinearPlaybackVoice::qCurrent(4.408)-160e-6)<1e-12,"R146 plus internal 1.8k Q emitter resistor");
    {   // Legacy BasicVoice (48k): linear chain, so ON/OFF ratio is the stage gain times the 0dB passband.
        BasicPlaybackVoice off(48000),on(48000);Ratio r;
        for(unsigned n=0;n<48000;++n){const KnownByte code{sineCode(n,250,40),255};
            const double a=off.process(code,{3,true},{1,true},{59,63},false),b=on.process(code,{3,true},{1,true},{59,63},true);if(n>24000)r.add(a,b);}
        std::cout<<"legacy48 ON/OFF ratio="<<r.value()<<'\n';
        require(std::abs(r.value()-expected)<.02,"Legacy S3528 ON/OFF: same input stage and host level");
    }
    for(unsigned rate:{96000u,192000u})for(unsigned model=0;model<3;++model)for(bool native:{false,true}){
        if(model&&rate==192000)continue; // LegacyQ/EIII routes are rendered at96k only.
        NonlinearPlaybackVoice off(rate,model==1,model==2),on(rate,model==1,model==2),closed(rate,model==1,model==2);
        FilterCalibration c;off.calibrate(c);on.calibrate(c);closed.calibrate(c);Ratio r,z;const unsigned steps=rate/2;
        for(unsigned n=0;n<steps;++n){const unsigned frame=n*48000/rate;const KnownByte code{sineCode(frame,250,6),255};
            const KnownSignal acv{3,true},fcv{1,true},qcv{0,true},input{.004*std::sin(2*pi*250*n/rate),true};
            const double a=off.process(code,acv,fcv,qcv,{59,63},false,input,native);
            const double b=on.process(code,acv,fcv,qcv,{59,63},true,input,native);
            const double y=closed.process(code,acv,fcv,qcv,{0,63},true,input,native); // code00: fc=61Hz
            if(n>steps/2){r.add(a,b);z.add(a,y);}}
        std::cout<<"rate="<<rate<<" model="<<model<<" native="<<native<<" ON/OFF ratio="<<r.value()<<" code00 rms ratio="<<std::sqrt(z.bb/z.aa)<<'\n';
        require(std::abs(r.value()-expected)<.05,"nonlinear S3528 ON/OFF: same input stage and host level (small signal)");
        require(std::sqrt(z.bb/z.aa)<.2,"firmware S3528 code must reach the filter (code00=61Hz attenuates 250Hz)");
    }
    {   // Bank level: native NativePins::sw and reference reconstructionCode both select the filter.
        for(bool native:{false,true})for(unsigned model=0;model<3;++model){
            std::array<double,2> energy{};
            for(unsigned pass=0;pass<2;++pass){PlaybackVoiceBank bank(2,model==1,model==2);bank.calibrate({});
                const std::uint8_t sw=pass?0:63;
                for(unsigned n=0;n<24000;++n){PlaybackVoiceBank::Input in;const auto code=sineCode(n,1000,30);
                    if(native)in.native=nativeFrame(n,code,sw);
                    else{in.playback.codes.fill({0,255});in.playback.codes[1]={code,255};in.playback.amplitudeCv.fill({0,true});in.playback.amplitudeCv[1]={3,true};
                        in.frequency.fill({.4,true});in.resonance.fill({0,true});in.reconstructionCode.fill({sw,63});}
                    in.reconstructionEnabled=true;const double y=bank.process(in);if(n>12000)energy[pass]+=y*y;}}
            std::cout<<"bank native="<<native<<" model="<<model<<" code63/code00 energy="<<energy[0]<<'/'<<energy[1]<<'\n';
            require(energy[0]>1e-10&&energy[1]<energy[0]*1e-3,"bank must apply per-channel firmware S3528 code");
        }
    }
    {   // RuntimeAudioEngine routes, including LegacyQ and EIII worker modes (reference packets).
        for(auto mode:{RuntimeAudioMode::BasicVoice,RuntimeAudioMode::NonlinearChip,RuntimeAudioMode::NonlinearChip192,RuntimeAudioMode::LegacyQ,RuntimeAudioMode::EiiiCustom}){
            std::array<double,2> energy{};
            for(unsigned pass=0;pass<2;++pass){host::AudioQueue queue(1056);RuntimeAudioEngine engine([&](auto f){queue.push(f);},mode);engine.reconstruction(true);
                const std::uint8_t sw=pass?0:63;
                for(unsigned begin=0;begin<12000;begin+=96){
                    for(unsigned n=begin;n<begin+96;++n){PlaybackInputs in;std::array<KnownSignal,8> fc{},q{};std::array<KnownByte,8> codes{};
                        for(unsigned ch=0;ch<8;++ch){in.codes[ch]={ch==1?sineCode(n,1000,30):std::uint8_t(0),255};in.amplitudeCv[ch]={ch==1?3.0:0.0,true};fc[ch]={.4,true};q[ch]={0,true};codes[ch]={sw,63};}
                        engine.sample(in,fc,false,q,codes);}
                    engine.flush();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
                    while(queue.stats().queued<96){require(std::chrono::steady_clock::now()<deadline&&!engine.chipStats().failures,"worker timeout");std::this_thread::sleep_for(std::chrono::microseconds(100));}
                    std::array<host::StereoFrame,96> frames{};queue.pop(std::span(frames.data(),96));
                    if(begin>=6000)for(auto& f:frames)energy[pass]+=double(f.left)*f.left;}
                require(queue.stats().dropped==0&&engine.chipStats().dropped==0,"no transport loss");}
            std::cout<<"engine mode="<<unsigned(mode)<<" code63/code00 energy="<<energy[0]<<'/'<<energy[1]<<'\n';
            require(energy[0]>1e-10&&energy[1]<energy[0]*1e-3,"engine route must apply firmware S3528 code");
        }
    }
    {   // -BIAS threshold: steady branch current.
        VcaBoardControl v(96000);for(unsigned n=0;n<96000;++n)v.process(1);
        require(std::abs(v.process(1)-(1-VcaBoardControl::threshold)/9850)<1e-15,"loaded steady current with -BIAS threshold");
        for(unsigned n=0;n<96000;++n)v.process(.2);require(v.process(.2)==0,"below shifted threshold VCA2 is off");
    }
    std::cout<<"PASS analog audit: S3528 -R58/R66 input stage and code routing in all routes; -BIAS VCA threshold; Q internal 1.8k\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
