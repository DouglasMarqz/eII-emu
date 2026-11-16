// Native laboratory: public Machine observations at exact48kHz boundaries.
// Does not modify emulator source, reset firmware, write media or export audio.
#include "machine.h"
#include "floppy_hfe.h"
#include "host_thread_cpu.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>
#include <bit>
static std::uint64_t hashInput(std::uint64_t h,const eii::analog::NonlinearVoiceBank::Input& in){
    auto add=[&](std::uint64_t n){for(unsigned i=0;i<8;++i){h^=std::uint8_t(n>>(i*8));h*=1099511628211ull;}};
    add(in.channel0Playback);
    for(unsigned ch=0;ch<8;++ch){add(in.playback.codes[ch].value);add(in.playback.codes[ch].known);
        for(auto v:{in.playback.amplitudeCv[ch],in.frequency[ch],in.resonance[ch]}){add(v.known);add(std::bit_cast<std::uint64_t>(v.value));}}
    return h;
}
int main(int argc,char** argv)try {
    if(argc!=4)throw std::runtime_error("usage: filter_restart_probe ROM-folder bank.hfe calibration.txt");
    std::ifstream profileFile(argv[3]);if(!profileFile)throw std::runtime_error("profile open failed");
    const auto profile=eii::analog::FilterCalibration::parse({std::istreambuf_iterator<char>(profileFile),{}});
    const auto image=eii::HfeImage(eii::readFile(argv[2])).extractEmuii();
    auto ptr=std::make_unique<eii::Machine>(0);auto& m=*ptr;std::filesystem::path rom=argv[1];
    m.channelTimers.enableControllerTiming();m.channelTimers.controllerTiming.enableRegisterWrites();
    m.load(rom/"main.bin",rom/"scanner.bin",rom/"ic135.bin",rom/"ic137.bin");
    m.initializeDramPowerOn(1);m.enableProductionController();m.initializeControllerPowerOn(0xe1122026u);m.enableNormalRuntime();
    m.floppy.attach(0,37,400000);m.floppy.drives[0]->insertSensorDisk(true);m.diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m.diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(image),eii::EmuiiGap::Hxc2026);
    while(m.ticks<240000000){m.run(1000000);if(m.ticks%40000000==0)std::cout<<"BOOT_PROGRESS tick="<<m.ticks<<" LCD=["<<m.lcd.line(0)<<"] DMA="<<m.diskDma.transfers<<std::endl;
        if(m.diskDma.transfers>=500000&&m.lcd.line(0).starts_with("P01")&&!m.floppy.drives[0]->selected())break;}
    if(!m.lcd.line(0).starts_with("P01"))throw std::runtime_error("bank failed to reach preset screen");
    m.run(4000000);
    std::cout<<"BOOT_OK tick="<<m.ticks<<" LCD=["<<m.lcd.line(0)<<"] DMA="<<m.diskDma.transfers<<std::endl;
    constexpr unsigned rate=48000,seconds=10;
    std::vector<eii::analog::NonlinearVoiceBank::Input> tape;tape.reserve(rate*seconds);
    const auto origin=m.ticks;std::uint64_t hash=14695981039346656037ull;
    for(unsigned frame=0;frame<rate*seconds;++frame){
        if(frame==rate)m.scannerControls.setButton(eii::ScannerButton::Play,true);
        if(frame==rate+rate/5)m.scannerControls.setButton(eii::ScannerButton::Play,false);
        const auto target=origin+(std::uint64_t(frame+1)*4000000+rate-1)/rate;m.run(target-m.ticks);
        eii::analog::NonlinearVoiceBank::Input in;in.playback=m.playbackAnalogInputs();in.channel0Playback=m.channel0PlaybackEnabled();
        for(unsigned ch=0;ch<8;++ch){auto f=m.controlVoltage.held[eii::ControlVoltageBus::Frequency][ch];auto q=m.controlVoltage.held[eii::ControlVoltageBus::Resonance][ch];in.frequency[ch]={f.volts,f.known};in.resonance[ch]={q.volts,q.known};}
        hash=hashInput(hash,in);tape.push_back(in);
        if((frame+1)%rate==0)std::cout<<"CAPTURE second="<<(frame+1)/rate<<" LCD=["<<m.lcd.line(0)<<"]["<<m.lcd.line(1)<<"]"<<std::endl;
    }
    std::cout<<"PINS frames="<<tape.size()<<" hash="<<std::hex<<hash<<std::dec<<" invalid="<<m.channelTimers.controllerTiming.productionEngineInvalid()<<std::endl;
    const eii::host::AudioFloatScope floatScope;
    for(unsigned pass=0;pass<4;++pass){
        const bool restart=pass%2;auto bank=std::make_unique<eii::analog::NonlinearVoiceBank>(2);bank->calibrate(profile);
        for(unsigned sec=0;sec<seconds;++sec){
            if(restart&&sec==5){bank=std::make_unique<eii::analog::NonlinearVoiceBank>(2);bank->calibrate(profile);}
            const auto before=bank->solverStats();const double cpu=eii::host::threadCpuSeconds();double peak=0,energy=0;unsigned nonfinite=0;
            for(unsigned n=sec*rate;n<(sec+1)*rate;++n){const auto v=bank->process(tape[n]);nonfinite+=!std::isfinite(v);peak=std::max(peak,double(std::abs(v)));energy+=double(v)*v;}
            const double elapsed=eii::host::threadCpuSeconds()-cpu;const auto after=bank->solverStats();
            std::cout<<std::setprecision(9)<<"DSP pass="<<pass<<" restart="<<restart<<" second="<<sec+1<<" cpu="<<elapsed<<" samples="<<after.samples-before.samples<<" iterations="<<after.iterations-before.iterations<<" fallbacks="<<after.fallbacks-before.fallbacks<<" held="<<after.held-before.held<<" peak="<<peak<<" rms="<<std::sqrt(energy/rate)<<" nonfinite="<<nonfinite<<std::endl;
            if(nonfinite)throw std::runtime_error("nonfinite output");
        }
    }
    return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<std::endl;return 1;}
