// Private assets supplied by caller; no media or audio is exported.
// LAB-only native-frame tap is excluded from the GUI build.
#include "machine.h"
#include "floppy_hfe.h"
#include "host_thread_cpu.h"
#include <bit>
#include <chrono>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>

static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(int argc,char** argv)try{
    require(argc==4,"usage: reconstruction_bank_probe ROM-folder bank.hfe calibration.txt");
    std::ifstream file(argv[3]);require(bool(file),"profile open");
    const auto profile=eii::analog::FilterCalibration::parse({std::istreambuf_iterator<char>(file),{}});
    const auto image=eii::HfeImage(eii::readFile(argv[2])).extractEmuii();
    auto machine=std::make_unique<eii::Machine>(0);auto& m=*machine;
    const std::filesystem::path rom=argv[1];
    m.channelTimers.enableControllerTiming();m.channelTimers.controllerTiming.enableRegisterWrites();
    m.load(rom/"main.bin",rom/"scanner.bin",rom/"ic135.bin",rom/"ic137.bin");
    m.initializeDramPowerOn(1);m.enableProductionController();m.initializeControllerPowerOn(0xe1122026u);m.enableNormalRuntime();
    m.floppy.attach(0,37,400000);m.floppy.drives[0]->insertSensorDisk(true);
    m.diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m.diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(image),eii::EmuiiGap::Hxc2026);
    while(m.ticks<240000000){m.run(1000000);
        if(m.ticks%40000000==0)std::cout<<"BOOT tick="<<m.ticks<<" LCD=["<<m.lcd.line(0)<<"] DMA="<<m.diskDma.transfers<<std::endl;
        if(m.diskDma.transfers>=500000&&m.lcd.line(0).starts_with("P01")&&!m.floppy.drives[0]->selected())break;
    }
    require(m.lcd.line(0).starts_with("P01"),"firmware bank boot failed");m.run(4000000);
    std::cout<<"BOOT_OK tick="<<m.ticks<<" DMA="<<m.diskDma.transfers<<std::endl;
    eii::analog::NonlinearVoiceBank bypass(2),hardware(2),test(2);
    bypass.calibrate(profile);hardware.calibrate(profile);auto lab=profile;
    lab[eii::analog::FilterParam::S3528Override]=1;lab[eii::analog::FilterParam::S3528Code]=35;test.calibrate(lab);
    std::array<double,3> power{};std::array<double,2> difference{};std::array<unsigned,2> changed{};
    std::cout<<m.report();const auto start=m.ticks;
    constexpr unsigned frames=96000;
    for(unsigned n=0;n<frames;++n){
        if(n%24000==0){const std::array<std::uint8_t,3> note={0x90,std::uint8_t(48+(n/24000)*5),100};require(m.receiveMidi(note),"MIDI enqueue");}
        const auto end=start+std::uint64_t(n+1)*4000000/48000;m.run(end-m.ticks);
        eii::analog::NonlinearVoiceBank::Input in;in.playback=m.playbackAnalogInputs();in.channel0Playback=m.channel0PlaybackEnabled();
        for(unsigned ch=0;ch<8;++ch){const auto& f=m.controlVoltage.held[1][ch];const auto& q=m.controlVoltage.held[2][ch];
            in.frequency[ch]={f.volts,f.known};in.resonance[ch]={q.volts,q.known};in.reconstructionCode[ch]=m.reconstructionControl[ch].activeCode();}
        const double a=bypass.process(in);in.reconstructionEnabled=true;const double b=hardware.process(in),c=test.process(in);
        const std::array y={a,b,c};for(unsigned k=0;k<3;++k)power[k]+=y[k]*y[k];
        difference[0]+=(a-b)*(a-b);difference[1]+=(a-c)*(a-c);changed[0]+=a!=b;changed[1]+=a!=c;
    }
    require(power[0]>1e-9,"bank has no audible stimulus");
    std::cout<<std::setprecision(9)<<"REAL_BANK_REFERENCE_ROUTE frames="<<frames<<" rms="<<std::sqrt(power[0]/frames)
        <<" firmwareS3528Rms="<<std::sqrt(power[1]/frames)<<" forced3205HzRms="<<std::sqrt(power[2]/frames)
        <<" firmwareRelativeDiff="<<std::sqrt(difference[0]/power[0])<<" forcedRelativeDiff="<<std::sqrt(difference[1]/power[0])
        <<" changed="<<changed[0]<<'/'<<changed[1]<<" UART="<<m.midiSerial.received<<'/'<<m.midiSerial.reads<<'/'<<m.midiSerial.overruns<<std::endl;
    require(changed[0]>100&&changed[1]>100&&!m.midiSerial.overruns,"S3528 did not reach final PCM");return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
