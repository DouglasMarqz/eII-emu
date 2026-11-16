#include "front_panel_session.h"
#include <fstream>
#include <iostream>
using namespace std::chrono_literals;
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>static auto until(eii::FrontPanelSession& s,F f){const auto end=std::chrono::steady_clock::now()+30s;
    for(;;){auto v=s.snapshot();if(!v.error.empty())throw std::runtime_error(v.error);if(f(v))return v;
        if(std::chrono::steady_clock::now()>end)throw std::runtime_error("session timeout");std::this_thread::sleep_for(1ms);}}
struct Fixture{
    std::filesystem::path dir=std::filesystem::temp_directory_path()/("eii-trace-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture(){std::filesystem::create_directory(dir);std::vector<std::uint8_t> main(8192),scanner(8192),upper(512),lower(512,0x40);
        const std::uint8_t code[]={0x3e,35,0xd3,0x40,0x3e,0x90,0xd3,4,0xc3,6,0};std::copy(std::begin(code),std::end(code),main.begin());scanner[0]=0x76;
        for(unsigned a=256;a<512;++a)if(((a>>5)&7)==2)lower[a]|=4;
        put("main.bin",main);put("scanner.bin",scanner);put("ic135.bin",upper);put("ic137.bin",lower);}
    void put(const char* name,const std::vector<std::uint8_t>& b){std::ofstream f(dir/name,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());}
    ~Fixture(){std::error_code e;std::filesystem::remove_all(dir,e);}
};
int main()try{
    Fixture fixture;eii::FrontPanelSession a(std::make_shared<eii::host::AudioQueue>()),b(std::make_shared<eii::host::AudioQueue>());
    eii::PanelBoot boot;boot.romFolder=fixture.dir;boot.realTime=false;boot.nativeAudio=true;
    a.boot(boot);b.boot(boot);auto av=until(a,[](auto&v){return v.loaded;});auto bv=until(b,[](auto&v){return v.loaded;});
    require(!av.trace.enabled&&!bv.trace.enabled,"trace default off");b.traceMetrics(true);bv=until(b,[](auto&v){return v.trace.enabled;});
    require(bv.ticks==0&&bv.trace.machineGeneration==1&&bv.trace.audioGeneration==1,"enable does not advance firmware");
    a.step(200000);b.step(200000);av=until(a,[](auto&v){return !v.running&&v.ticks==200000;});bv=until(b,[](auto&v){return !v.running&&v.ticks==200000;});
    require(!av.trace.enabled&&bv.trace.enabled&&av.lcd==bv.lcd&&av.dma==bv.dma&&av.dramReads==bv.dramReads,"telemetry preserves execution surface");
    a.captureState();b.captureState();av=until(a,[](auto&v){return v.report.starts_with("MANUAL STATE CAPTURE");});bv=until(b,[](auto&v){return v.report.starts_with("MANUAL STATE CAPTURE");});
    const auto marker="EII digital prototype;";auto pa=av.report.find(marker),pb=bv.report.find(marker);
    require(pa!=std::string::npos&&pb!=std::string::npos&&av.report.substr(pa)==bv.report.substr(pb),"full digital reports agree with trace on/off");
    const auto pc=bv.trace.mainPc;const auto gen=bv.trace.audioGeneration;b.audioModel(eii::analog::RuntimeAudioMode::NonlinearChip);
    bv=until(b,[&](auto&v){return v.trace.audioGeneration>gen;});
    require(bv.ticks==200000&&bv.trace.mainPc==pc&&bv.audioMode==eii::analog::RuntimeAudioMode::NonlinearChip,"DSP generation changes without advancing CPU");
    require(bv.nativeAudio&&!bv.nativeOverflow&&!bv.nativeOrderErrors,"native path selected only in nonlinear model");
    b.nativeAudio(false);bv=until(b,[](auto&v){return !v.nativeAudio;});
    require(bv.ticks==200000,"reference-path toggle preserves CPU time");
    b.nativeAudio(true);bv=until(b,[](auto&v){return v.nativeAudio;});
    require(bv.ticks==200000,"native-path toggle preserves CPU time");
    require(!bv.reconstruction&&bv.trace.reconstructionCode[0].value==35&&bv.trace.reconstructionCode[0].known==63,"real Z80 OUT40h reaches trace; default bypass");
    b.reconstruction(true);bv=until(b,[](auto&v){return v.reconstruction;});
    require(bv.ticks==200000&&bv.trace.audioGeneration==gen+1,"S3528 toggle preserves firmware time and DSP generation");
    b.audioModel(eii::analog::RuntimeAudioMode::BasicVoice);bv=until(b,[](auto&v){return v.audioMode==eii::analog::RuntimeAudioMode::BasicVoice;});
    // Since 0.71 (08a9e1f, docs/NATIVE_CALIBRATION_071.md) the requested S3528
    // also applies in Legacy; only DacMonitor bypasses it.
    require(bv.reconstruction,"requested S3528 also applies in Legacy (0.71)");
    require(!bv.nativeAudio,"Legacy cannot run native nonlinear path");
    b.audioModel(eii::analog::RuntimeAudioMode::NonlinearChip192);bv=until(b,[](auto&v){return v.audioMode==eii::analog::RuntimeAudioMode::NonlinearChip192;});
    require(bv.reconstruction&&bv.ticks==200000,"requested S3528 survives mode changes");
    require(bv.nativeAudio,"requested native DAC survives mode changes");
    a.step(20000);b.step(20000);until(a,[](auto&v){return !v.running&&v.ticks==220000;});until(b,[](auto&v){return !v.running&&v.ticks==220000;});
    a.captureState();b.captureState();av=until(a,[](auto&v){return v.report.find("CPU tick=220000")!=std::string::npos;});bv=until(b,[](auto&v){return v.report.find("CPU tick=220000")!=std::string::npos;});
    require(av.report.substr(av.report.find(marker))==bv.report.substr(bv.report.find(marker)),"S3528 enabled execution preserves complete digital report");
    boot.reconstruction=true;boot.audioMode=eii::analog::RuntimeAudioMode::NonlinearChip;
    const auto machine=bv.trace.machineGeneration;b.boot(boot);bv=until(b,[&](auto&v){return v.trace.machineGeneration>machine;});
    require(bv.reconstruction,"boot applies requested S3528 setting");
    require(bv.ticks==0&&bv.trace.enabled&&bv.trace.ownerBusy==-1,"recording metrics survive explicit boot with generation change");
    b.traceMetrics(false);bv=until(b,[](auto&v){return !v.trace.enabled;});require(bv.ticks==0,"disable does not advance firmware");
    std::cout<<"PASS: opt-in telemetry keeps full digital report equal; default off; audio/boot generation markers; toggle preserves CPU ticks.\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
