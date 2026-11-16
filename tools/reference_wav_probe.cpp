// Deterministic offline reference for CPU work: boots the real firmware and
// bank, plays fixed scenarios and writes the final mono PCM (what the DSP
// sink delivers) as a float WAV. Per stage: FNV-1a of the PCM, machine-thread
// wall time (Machine::run only) and DSP-thread CPU time. The producer never
// lets the DSP queue overflow, so the output does not depend on host speed:
// identical WAV before/after a change = bit exact.
// usage: reference_wav_probe ROM_FOLDER BANK_IMAGE MODE(0..7) NATIVE(0/1) RECON(0/1) OUT.wav [SCENARIO=all]
// EII_PROFILE=file: sample the post-boot stages (tools/sampling_profiler.h, build with -g).
// Assets are supplied by the caller and never stored in Git.
#include "floppy_hfe.h"
#include "machine.h"
#include "sampling_profiler.h"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
namespace {
double ft(FILETIME t){return (double(t.dwHighDateTime)*4294967296.0+t.dwLowDateTime)*1e-7;}
double threadCpu(){FILETIME c,e,k,u;GetThreadTimes(GetCurrentThread(),&c,&e,&k,&u);return ft(k)+ft(u);}
double processCpu(){FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);return ft(k)+ft(u);}
struct Fnv{std::uint64_t h=1469598103934665603ull;void add(const void* p,std::size_t n){auto b=static_cast<const std::uint8_t*>(p);for(std::size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;}}};
}
int main(int argc,char** argv) try {
    if(argc<7){std::cerr<<"usage: reference_wav_probe ROM_FOLDER BANK_IMAGE MODE NATIVE RECON OUT.wav [BOOT_ONLY_TICKS]\n";return 2;}
    const std::filesystem::path folder=argv[1];
    const int mode=std::atoi(argv[3]),native=std::atoi(argv[4]),recon=std::atoi(argv[5]);
    auto m=std::make_unique<eii::Machine>(0);
    m->channelTimers.enableControllerTiming();m->channelTimers.controllerTiming.enableRegisterWrites();
    m->load(folder/"main.bin",folder/"scanner.bin",folder/"ic135.bin",folder/"ic137.bin");
    m->initializeDramPowerOn(1);m->enableProductionController();m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
    auto bytes=eii::readFile(argv[2]);
    if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
    m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
    std::mutex pcmMutex;std::vector<float> pcm;pcm.reserve(48000*90);
    m->enableRuntimeAudio([&](auto block){std::lock_guard l(pcmMutex);for(auto f:block)pcm.push_back(f.left);},eii::analog::RuntimeAudioMode(mode));
    m->reconstruction(recon!=0);m->nativeAudio(native!=0);
    const bool worker=eii::analog::workerAudioMode(eii::analog::RuntimeAudioMode(mode));
    std::size_t hashed=0;double machineTotal=0,dspTotal=0,emuTotal=0;
    const auto drain=[&](unsigned limit){if(!worker)return;while(m->chipAudioStats().queuedBlocks>limit)Sleep(0);};
    const auto stage=[&](const char* label,std::uint64_t ticks){
        double machine=0;const double p0=processCpu(),t0=threadCpu();
        for(std::uint64_t done=0;done<ticks;){
            const std::uint64_t n=std::min<std::uint64_t>(8000,ticks-done);
            const auto a=std::chrono::steady_clock::now();m->run(n);
            machine+=std::chrono::duration<double>(std::chrono::steady_clock::now()-a).count();done+=n;drain(5);
        }
        drain(0);Sleep(20); // let the DSP thread hand over its last block
        const double dsp=(processCpu()-p0)-(threadCpu()-t0);
        Fnv f;std::size_t n;{std::lock_guard l(pcmMutex);n=pcm.size();f.add(pcm.data()+hashed,(n-hashed)*sizeof(float));}
        const double emu=ticks/4e6;
        std::printf("%-14s emu=%5.2fs machine=%.4f dsp=%.4f (host s per emulated s) frames=%zu pcm=%016llx lcd='%s'\n",label,emu,machine/emu,dsp/emu,n-hashed,(unsigned long long)f.h,m->lcd.line(0).c_str());
        std::fflush(stdout);hashed=n;
        if(std::string(label)!="boot"){machineTotal+=machine;dspTotal+=dsp;emuTotal+=emu;}
    };
    const auto midi3=[&](unsigned a,unsigned b,unsigned c){const std::array<std::uint8_t,3> x{std::uint8_t(a),std::uint8_t(b),std::uint8_t(c)};m->receiveMidi(x);};
    using In=eii::ScannerAnalogInput;
    std::unique_ptr<eii::tools::SamplingProfiler> profiler;
    if(const char* p=std::getenv("EII_PROFILE"))if(*p)profiler=std::make_unique<eii::tools::SamplingProfiler>();
    stage("boot",200000000);
    if(profiler)profiler->enable(true);
    if(argc>7)goto done;
    stage("idle",8000000);
    // 1: eight held notes, release
    for(unsigned n=48;n<56;++n)midi3(0x90,n,100);
    stage("chord8",16000000);
    for(unsigned n=48;n<56;++n)midi3(0x80,n,0);
    stage("chord8-rel",8000000);
    // 2: one voice at a time, different pitches and velocities
    {static const unsigned notes[]={36,43,52,60,67,76,84,96},vel[]={20,45,70,90,110,127,64,100};
     for(unsigned i=0;i<8;++i){midi3(0x90,notes[i],vel[i]);stage("solo",2000000);midi3(0x80,notes[i],0);stage("solo-rel",1000000);}}
    // 3: growing polyphony with wheel and slider motion
    for(unsigned step=0;step<60;++step){
        if(step%8==0)midi3(0x90,40+step/8*5,60+step);
        if(step%8==6&&step>16)midi3(0x80,40+(step/8-2)*5,0);
        m->scannerControls.setVoltage(In::LeftWheel,2.5+2.0*std::sin(step*0.37));
        m->scannerControls.setVoltage(In::RightWheel,2.5+2.4*std::sin(step*0.21));
        m->scannerControls.setVoltage(In(unsigned(In::SliderA)+step%4),(step*37%100)*0.05);
        midi3(0xe0,(step*29)&127,(64+int(50*std::sin(step*0.5)))&127);midi3(0xb0,1,(step*9)&127);
        stage("poly-motion",400000);
    }
    for(unsigned n=0;n<128;++n)midi3(0x80,n,0);
    stage("final-rel",8000000);
done:
    if(profiler)profiler->write(std::getenv("EII_PROFILE"));
    {const auto st=m->chipAudioStats();
     std::printf("TOTAL after boot: emu=%.2fs machine=%.4f dsp=%.4f host s per emulated s; dropped=%llu failures=%llu native overflows=%llu ordering=%llu\n",emuTotal,emuTotal?machineTotal/emuTotal:0,emuTotal?dspTotal/emuTotal:0,
        (unsigned long long)st.dropped,(unsigned long long)st.failures,(unsigned long long)m->nativeAudioOverflows(),(unsigned long long)m->nativeAudioOrderingErrors());
     if(st.dropped||st.failures){std::cerr<<"NOT A VALID REFERENCE: DSP queue dropped or failed\n";return 1;}}
    {std::lock_guard l(pcmMutex);Fnv f;f.add(pcm.data(),pcm.size()*sizeof(float));
     const std::uint32_t bytesN=std::uint32_t(pcm.size()*4),rate=48000;
     std::ofstream o(argv[6],std::ios::binary);
     const auto u32=[&](std::uint32_t v){o.write(reinterpret_cast<const char*>(&v),4);};const auto u16=[&](std::uint16_t v){o.write(reinterpret_cast<const char*>(&v),2);};
     o.write("RIFF",4);u32(36+bytesN);o.write("WAVEfmt ",8);u32(16);u16(3);u16(1);u32(rate);u32(rate*4);u16(4);u16(32);o.write("data",4);u32(bytesN);
     o.write(reinterpret_cast<const char*>(pcm.data()),bytesN);
     std::printf("WAV %s frames=%zu fnv=%016llx\n",argv[6],pcm.size(),(unsigned long long)f.h);}
    m.reset();
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
