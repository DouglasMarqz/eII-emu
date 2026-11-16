// Whole-machine A/B gate for controller/CPU optimizations. Same stimulus as
// the PGO trainer (boot, notes, fast route, sampling, record). After every
// stage: ticks, PCs, LCD, DRAM/known/scanner-RAM hashes, the 64 register
// cells and an FNV-1a of every native audio frame (the exact DSP input).
// Identical output before/after a change = bit exact. Build with
// -DEII_PLAYBACK_TIMING_LAB=1 (frame observer). Assets supplied by caller.
#include "floppy_hfe.h"
#include "machine.h"
#include <cstdio>
#include <ctime>
#include <iostream>
namespace {
double cpuNow(){timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec+t.tv_nsec*1e-9;}
struct Fnv{std::uint64_t h=1469598103934665603ull;void add(const void* p,std::size_t n){auto b=static_cast<const std::uint8_t*>(p);for(std::size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;}}template<class T>void pod(const T& v){add(&v,sizeof v);}};
template<class C>std::uint64_t fnv(const C& c){Fnv f;f.add(c.data(),c.size()*sizeof(c[0]));return f.h;}
}
int main(int argc,char** argv){
    if(argc!=3){std::cerr<<"usage: machine_fingerprint_probe ROM_FOLDER BANK_IMAGE\n";return 2;}
    const std::filesystem::path folder=argv[1];
    auto m=std::make_unique<eii::Machine>(0);
    m->channelTimers.enableControllerTiming();m->channelTimers.controllerTiming.enableRegisterWrites();
    m->load(folder/"main.bin",folder/"scanner.bin",folder/"ic135.bin",folder/"ic137.bin");
    m->initializeDramPowerOn(1);m->enableProductionController();m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
    auto bytes=eii::readFile(argv[2]);
    if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
    m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
    struct Sine final:eii::analog::SampleInputSource{double volts(std::uint64_t t,double f=0)override{return .05*std::sin((double(t)+f)*440*2*3.14159265358979323846/4000000);}};
    m->sampleInput=std::make_shared<Sine>();
    m->enableRuntimeAudio([](auto){},eii::analog::RuntimeAudioMode::LegacyQ);
    Fnv frames;std::uint64_t frameCount=0,eventCount=0;
    m->nativeFrameObserver([&](const eii::analog::NativeFrame& f){++frameCount;eventCount+=f.count;frames.pod(f.start);frames.pod(f.count);frames.pod(f.valid);
        frames.pod(f.initial.dac);frames.pod(f.initial.sw);frames.pod(f.initial.cv);frames.pod(f.initial.phases);frames.pod(f.initial.channel0);frames.pod(f.initial.monitorSampling);
        for(unsigned i=0;i<f.count;++i){const auto& e=f.events[i];frames.pod(e.offset);frames.pod(e.kind);frames.pod(e.channel);frames.pod(e.value);frames.pod(e.known);frames.pod(e.phases);frames.pod(e.analogBits);}});
    m->nativeAudio(true);m->reconstruction(true);
    double cpuTotal=0;
    const auto stage=[&](const char* label,std::uint64_t ticks){
        const double c0=cpuNow();m->run(ticks);const double c1=cpuNow();if(std::string(label)!="boot")cpuTotal+=c1-c0;Fnv r;for(unsigned a=0;a<64;++a)r.pod(m->channelTimers.controllerTiming.registerValue(std::uint8_t(a)));
        std::printf("%-18s t=%llu pc=%04x/%04x lcd='%s' mem=%016llx known=%016llx scan=%016llx rf=%016llx frames=%llu ev=%llu fh=%016llx\n",label,(unsigned long long)m->ticks,
            m->main.cpu.pc,m->scanner.cpu.pc,m->lcd.line(0).c_str(),(unsigned long long)fnv(m->memory),(unsigned long long)fnv(m->dramKnown),(unsigned long long)fnv(m->scannerRam),
            (unsigned long long)r.h,(unsigned long long)frameCount,(unsigned long long)eventCount,(unsigned long long)frames.h);std::fflush(stdout);
    };
    stage("boot",200000000);stage("idle",8000000);
    for(unsigned n=48;n<56;++n){const std::array<std::uint8_t,3> midi{0x90,std::uint8_t(n),100};m->receiveMidi(midi);}
    stage("eight-notes",16000000);
    for(unsigned n=48;n<56;++n){const std::array<std::uint8_t,3> midi{0x80,std::uint8_t(n),0};m->receiveMidi(midi);}
    stage("release",8000000);
    m->nativeAudio(false);for(unsigned n=48;n<52;++n){const std::array<std::uint8_t,3> midi{0x90,std::uint8_t(n),100};m->receiveMidi(midi);}
    stage("fast-route-notes",8000000);
    for(unsigned n=48;n<52;++n){const std::array<std::uint8_t,3> midi{0x80,std::uint8_t(n),0};m->receiveMidi(midi);}
    m->nativeAudio(true);stage("fast-route-release",4000000);
    m->scannerControls.setVoltage(eii::ScannerAnalogInput(0),3.3);stage("slider",2000000);
    m->scannerControls.setButton(eii::ScannerButton::Sample,true);stage("sample-button",800000);
    m->scannerControls.setButton(eii::ScannerButton::Sample,false);stage("monitor",8000000);
    const std::array<std::uint8_t,3> on{0x90,60,100};m->receiveMidi(on);stage("monitor-note",8000000);
    m->scannerControls.setButton(eii::ScannerButton::Digit9,true);stage("record-button",800000);
    m->scannerControls.setButton(eii::ScannerButton::Digit9,false);stage("record",4000000);
    m->scannerControls.setButton(eii::ScannerButton::Digit0,true);stage("stop-button",800000);
    m->scannerControls.setButton(eii::ScannerButton::Digit0,false);stage("after-stop",4000000);
    std::printf("cpu after boot: %.2f s for %.1f emulated s\n",cpuTotal,(m->ticks-200000000)/4e6);
    m.reset();
}
