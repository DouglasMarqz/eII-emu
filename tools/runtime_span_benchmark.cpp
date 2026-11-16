#include "machine.h"
#include "floppy_hfe.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

int main(int argc,char** argv)try {
    if(argc!=3)throw std::invalid_argument("usage: runtime_span_benchmark ROM-folder bank-image");
    const std::filesystem::path folder=argv[1];
    auto m=std::make_unique<eii::Machine>(0);
    m->load(folder/"main.bin",folder/"scanner.bin",folder/"ic135.bin",folder/"ic137.bin");
    m->initializeDramPowerOn(1);m->enableNormalRuntime();
    auto bytes=eii::readFile(argv[2]);
    if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
    m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
    double peak=0;std::uint64_t frames=0;
    m->enableRuntimeAudio([&](auto block){frames+=block.size();for(auto f:block)peak=std::max(peak,double(std::abs(f.left)));});
    m->run(200000000);
    if(!m->channelTimers.controllerTiming.voiceTransactions)throw std::runtime_error("voice controller did not run");
    std::cout<<std::setprecision(12)<<"BOOT lcd="<<m->lcd.line(0)<<" / "<<m->lcd.line(1)<<std::endl;
    const auto sequence=[&](const char* stage) {
        for(bool spans:{false,true,true,false}) {
            m->channelTimers.enableControllerSpans(spans);
            const auto cpu=std::clock();const auto wall=std::chrono::steady_clock::now();
            const auto quiet=m->channelTimers.controllerTiming.quietClocks;
            m->run(8000000);
            const double wallSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();
            const double cpuSeconds=double(std::clock()-cpu)/CLOCKS_PER_SEC;
            std::cout<<stage<<" spans="<<spans<<" ticks=8000000 cpu="<<cpuSeconds<<" wall="<<wallSeconds
                     <<" collapsed="<<m->channelTimers.controllerTiming.quietClocks-quiet<<std::endl;
        }
    };
    sequence("idle");
    const std::array<std::uint8_t,3> on{0x90,60,100};m->receiveMidi(on);m->run(2000000);
    sequence("note");
    std::cout<<"AUDIO frames="<<frames<<" peak="<<peak<<" UART="<<m->midiSerial.received
             <<" firmwareReads="<<m->midiSerial.reads<<std::endl;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
