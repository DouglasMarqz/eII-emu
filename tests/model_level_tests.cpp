// Engine-level level regression (docs/OUTPUT_STAGE.md): every VCF/VCA model,
// both DAC routes and both S3528 states through RuntimeAudioEngine, the object
// the plug-in engine's machine drives. Stimulus: closed-loop DPCM codes of a
// 1 kHz, 2 V-peak tone at the deglitch output Y, 27.78 kHz channel rate (as the
// firmware's sampling loop writes them), ACV 5 V, FCV 0, Q 0.
#include "runtime_audio.h"
#include "host_audio_float.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
using namespace eii;using namespace eii::analog;
static int failures=0;
static void check(bool ok,const std::string& what){std::cout<<(ok?"PASS ":"FAIL ")<<what<<std::endl;if(!ok)++failures;}
static constexpr double pi=3.14159265358979323846;
static constexpr unsigned period=720; // half-edges (20 MHz): 27.777... kHz channel rate
// Deglitch input voltage (km - kp) x ACV for a code (NativeDacChannel nominal R values).
static double ivOf(std::uint8_t code,double acv){const double m=2490.0*Am6072::magnitudeUnits(code)/(9400.0*2112.0)*acv;return (code&128)?-m:m;}
// Closed-loop DPCM: choose the code whose leaky-integrator step lands nearest the target.
static std::vector<std::uint8_t> dpcm(double peak,double hz,unsigned count,double acv){
    std::vector<std::uint8_t> codes;double y=0;
    for(unsigned n=0;n<count;++n){const double target=peak*std::sin(2*pi*hz*n*period/20e6);
        unsigned best=0;double error=1e30;
        for(unsigned c=0;c<256;++c){const double next=50.0/51*y-50.0/51*ivOf(std::uint8_t(c),acv);if(std::abs(next-target)<error){error=std::abs(next-target);best=c;}}
        y=50.0/51*y-50.0/51*ivOf(std::uint8_t(best),acv);codes.push_back(std::uint8_t(best));}
    return codes;
}
struct Result{double rmsDb=-300,channelDb=-300;std::uint64_t frames=0;bool native=false;};
static Result render(RuntimeAudioMode mode,bool native,bool s3528,const std::vector<std::uint8_t>& codes,bool jacks=false){
    std::vector<float> out,jack;std::mutex m;
    auto held=std::make_unique<RuntimeAudioEngine>([&](std::span<const host::StereoFrame> f){std::lock_guard l(m);for(auto& x:f)out.push_back(x.left);},mode,
        [&](std::span<const ChipAudioWorker::ChannelFrame> f){std::lock_guard l(m);for(auto& x:f)jack.push_back(x[1]);});auto& engine=*held; // heap: 1 MB Windows stack
    engine.calibrate(FilterCalibration{});engine.reconstruction(s3528);engine.channelOutputs(jacks);
    const std::uint64_t totalEdges=std::uint64_t(codes.size())*period;std::uint64_t expected=0;
    // Bounded DSP queue: feed in 96-frame blocks and let the worker keep up (as the engine does).
    auto keepUp=[&](std::uint64_t produced){engine.flush();const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        for(;;){{std::lock_guard l(m);if(out.size()+192>=produced)return;}if(std::chrono::steady_clock::now()>until)return;std::this_thread::sleep_for(std::chrono::microseconds(200));}};
    if(native){
        engine.nativeAudio(true,0);if(!engine.nativeAudioEnabled())return {};
        auto ev=[&](std::uint64_t edge,NativeEvent e){engine.nativeEvent(edge,e);};
        ev(0,{0,NativeEvent::Mode,0,0,255,0});
        for(unsigned ch=1;ch<8;++ch){ev(0,{0,NativeEvent::Amp,std::uint8_t(ch),std::uint8_t(ch==1?255:0),255,0});ev(0,{0,NativeEvent::Fc,std::uint8_t(ch),0,255,0});
            ev(0,{0,NativeEvent::Q,std::uint8_t(ch),0,255,0});ev(0,{0,NativeEvent::Switch,std::uint8_t(ch),59,63,0});ev(0,{0,NativeEvent::Pins,std::uint8_t(ch),0,255,12});}
        for(std::size_t n=0;n<codes.size();++n){const std::uint64_t t=std::uint64_t(n)*period;
            for(auto [offset,phase]:{std::pair{0u,12},{32u,13},{64u,12},{96u,14},{128u,12}})ev(t+offset,{0,NativeEvent::Pins,1,codes[n],255,std::uint8_t(phase)});
            for(std::uint64_t e=t+128;e<t+period;e+=100)engine.nativeAdvance(e);
            if(n%64==63)keepUp(t*6/nativeFrameTime);}
        engine.nativeAdvance(totalEdges);expected=totalEdges*6/nativeFrameTime;
    }else{
        const double acv=255*5.0/256;const unsigned frames=unsigned(double(totalEdges)/20e6*48000);
        for(unsigned f=0;f<frames;++f){PlaybackInputs in;std::array<KnownSignal,8> fc{},q{};std::array<KnownByte,8> sw{};
            const auto code=codes[std::min<std::size_t>(codes.size()-1,std::size_t(f/48000.0*20e6/period))];
            for(unsigned ch=0;ch<8;++ch){in.codes[ch]={ch==1?code:std::uint8_t(0),255};in.amplitudeCv[ch]={ch==1?acv:0.0,true};fc[ch]={0,true};q[ch]={0,true};sw[ch]={59,63};}
            engine.sample(in,fc,false,q,sw);if(f%96==95)keepUp(f+1);}
        expected=frames;
    }
    engine.flush();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    for(;;){{std::lock_guard l(m);if(out.size()>=expected-2)break;}if(std::chrono::steady_clock::now()>deadline)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    std::lock_guard l(m);Result r;r.frames=out.size();r.native=engine.nativeAudioEnabled();
    double e=0;std::size_t n=0;for(std::size_t i=out.size()/2;i<out.size();++i){e+=double(out[i])*out[i];++n;}
    if(n)r.rmsDb=10*std::log10(std::max(1e-30,e/double(n)));
    double ej=0;std::size_t nj=0;for(std::size_t i=jack.size()/2;i<jack.size();++i){ej+=double(jack[i])*jack[i];++nj;}
    if(nj)r.channelDb=10*std::log10(std::max(1e-30,ej/double(nj)));return r;
}
int main()try{
    const host::AudioFloatScope floats;
    // Level-model constants against their sources.
    {   const double w=2*pi*1000/(20e6/period);const double re=1-50.0/51*std::cos(w),im=50.0/51*std::sin(w);
        check(std::abs(fastRouteMatch-1/(51*std::hypot(re,im)))<1e-12,"fast-route match = leaky deglitch integrator at 1 kHz / 27.78 kHz (-21.17 dB)");
        check(std::abs(vendorToPin8+9.119117647058824)<1e-12&&hostOutputReference==1.0&&tl084SwingVolts==13.5&&std::abs(s3528SwingVolts-2.1*std::sqrt(2.0))<1e-12&&mc34004SwingVolts==13.5,
              "pin-8 scale -8.1k/68k : 900/68.9k; 1 V at MIX OUT = 0 dBFS; TL084 13.5 V, S3528 2.1 Vrms, MC34004 13.5 V");
        check(std::abs(20*std::log10(-vendorToPin8*hostOutputReference/0.47)-levelModelMigrationDb)<1e-9,"migration offset equals the native-route level change");
    }
    // IC73 rail: the integrator output never passes the TL084 swing.
    {   NativeDacCoefficients c;NativeDacChannel dac;double peak=0;
        for(unsigned n=0;n<4000;++n)for(auto [len,phase]:{std::pair{192u,12},{192u,13},{192u,12},{192u,14},{1184u,12},{1184u,12},{1184u,12}}){
            const auto y=dac.advance(len,{std::uint8_t(0x7f),255},{5,true},std::uint8_t(phase),c,tl084SwingVolts);peak=std::max(peak,std::abs(y.value/len));}
        check(peak<=tl084SwingVolts&&peak>13,"native deglitch output limited at the TL084 swing (peak "+std::to_string(peak)+" V)");
        check(std::abs(opampSwing(227,tl084SwingVolts))<=13.5+1e-9&&std::abs(opampSwing(1,tl084SwingVolts)-1)<1e-15,"fast route: 227 V request limited to 13.5 V, small signals unchanged");
    }
    const auto codes=dpcm(2.0,1000,5556,255*5.0/256); // 0.2 s
    const std::map<RuntimeAudioMode,const char*> names{{RuntimeAudioMode::BasicVoice,"historical"},{RuntimeAudioMode::NonlinearChip,"ssm96"},{RuntimeAudioMode::NonlinearChip192,"ssm192"},
        {RuntimeAudioMode::LegacyQ,"legacyq"},{RuntimeAudioMode::EiiiCustom,"eiii"},{RuntimeAudioMode::Ssm2Chip,"model2-96"},{RuntimeAudioMode::Ssm2Chip192,"model2-192"}};
    std::map<std::pair<bool,bool>,std::vector<double>> levels;
    for(auto [mode,name]:names)for(bool native:{false,true})for(bool s3528:{false,true}){
        if(native&&mode==RuntimeAudioMode::BasicVoice){const auto r=render(mode,true,s3528,codes);check(!r.native,std::string(name)+": no hardware DAC route (menu marks it unavailable)");continue;}
        const auto r=render(mode,native,s3528,codes);
        std::cout<<"LEVEL model="<<name<<" route="<<(native?"hardware":"fast")<<" s3528="<<s3528<<" rms_dBFS="<<r.rmsDb<<" frames="<<r.frames<<std::endl;
        check(r.native==native&&r.rmsDb>-60,std::string(name)+" renders on the requested route");
        levels[{native,s3528}].push_back(r.rmsDb);
    }
    // Absolute level from the schematic: Y 2 V peak x 8.1k/68k x R58/R66 2.128 x VCA
    // (8.17 S/A x 500 uA x R154 4.7k x 470/10470 = 0.8616) x mix -1 = 0.4370 V peak at
    // MIX OUT = -10.21 dBFS rms with 1 V = 0 dBFS. Every model within 1.0 dB.
    {   const double expected=20*std::log10(2*8100.0/68000*10000.0/4700*8.17*500e-6*4700*470.0/10470/std::sqrt(2.0));
        for(double x:levels[{true,false}])check(std::abs(x-expected)<1.0,"MIX OUT level matches the schematic chain within 1 dB ("+std::to_string(x)+" vs "+std::to_string(expected)+" dBFS)");
        // Individual output J7 (channel 1) = CnOUT.V, the same level as the mix of one voice.
        const auto r=render(RuntimeAudioMode::NonlinearChip,true,true,codes,true);
        check(std::abs(r.channelDb-r.rmsDb)<0.2,"individual output (J7, channel 1) carries CnOUT.V at the mix level: "+std::to_string(r.channelDb)+" dBFS");
        const auto b=render(RuntimeAudioMode::BasicVoice,false,false,codes,true);
        check(std::abs(b.channelDb-b.rmsDb)<0.2,"individual output in Historical Legacy (48k) as well");
        const auto off=render(RuntimeAudioMode::NonlinearChip,true,true,codes,false);
        check(off.channelDb<-250,"individual outputs off by default: silent, no work");
    }
    // Tolerances: every model within +/-1.0 dB of the mean of its route/S3528 group;
    // fast vs hardware route within 1.0 dB (matched at this 1 kHz stimulus); S3528 ON vs OFF within 0.5 dB.
    std::map<std::pair<bool,bool>,double> mean;
    for(auto& [key,v]:levels){double s=0;for(double x:v)s+=x;mean[key]=s/double(v.size());
        double worst=0;for(double x:v)worst=std::max(worst,std::abs(x-mean[key]));
        check(worst<1.0,std::string("models agree within 1.0 dB: route=")+(key.first?"hardware":"fast")+" s3528="+std::to_string(key.second)+" spread "+std::to_string(worst)+" dB");}
    for(bool s:{false,true})check(std::abs(mean[{true,s}]-mean[{false,s}])<1.0,"fast route matches the hardware route at 1 kHz, s3528="+std::to_string(s));
    for(bool n:{false,true})check(std::abs(mean[{n,true}]-mean[{n,false}])<0.5,"S3528 ON and OFF share one host level, route="+std::string(n?"hardware":"fast"));
    std::cout<<(failures?"FAIL":"PASS")<<" model level regression\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
