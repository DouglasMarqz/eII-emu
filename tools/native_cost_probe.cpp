// Machine-thread cost of the native (hardware DAC event) audio route vs the
// fast route, eight held notes, model 2. Thread CPU time of Machine::run only.
#include "machine.h"
#include "floppy_hfe.h"
#include <ctime>
#include <cstdio>
#include <iostream>
static double cpu(){timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(int argc,char** argv){
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
    m->enableRuntimeAudio([](auto){},eii::analog::RuntimeAudioMode::Ssm2Chip192);
    m->nativeAudio(true);m->reconstruction(true);
    m->run(200000000);std::cout<<"lcd "<<m->lcd.line(0)<<std::endl;
    for(unsigned n=48;n<56;++n){const std::array<std::uint8_t,3> midi{0x90,std::uint8_t(n),100};m->receiveMidi(midi);}
    m->run(4000000);
    const std::uint64_t T=std::stoull(argc>3?argv[3]:"16000000");
    for(int r=0;r<2;++r)for(int nat=1;nat>=0;--nat){m->nativeAudio(nat);const double a=cpu();m->run(T);const double b=cpu();
        std::printf("native %d: %.3f s cpu per emulated s\n",nat,(b-a)/(T/4e6));}
    m.reset();
}
