// Separate Windows PGO TRAINING driver; never a release/performance plugin.
// Boots caller-owned read-only media, exercises both selected analog paths
// in separate processes, and runs monitor/SAR recording in emulated RAM.
#define WITH_MIDI 1
#include "floppy_hfe.h"
#include "machine.h"
#include <atomic>
#include <cmath>
#include <iostream>

static int train(int argc,char** argv)try {
    if(argc!=4)throw std::invalid_argument("usage: EII-PGO-TRAIN.exe ROM_FOLDER BANK_IMAGE ssm192|legacy|eiii|model2");
    const std::string model=argv[3];
    if(model!="ssm192"&&model!="legacy"&&model!="eiii"&&model!="model2")throw std::invalid_argument("expected ssm192, legacy, eiii or model2");
    const auto folder=std::filesystem::u8path(argv[1]);
    std::atomic<std::uint64_t> frames{0}; // outlives Machine/DSP on every exit path
    auto m=std::make_unique<eii::Machine>(0);
    m->channelTimers.enableControllerTiming();m->channelTimers.controllerTiming.enableRegisterWrites();
    m->load(folder/"main.bin",folder/"scanner.bin",folder/"ic135.bin",folder/"ic137.bin");
    m->initializeDramPowerOn(1);m->enableProductionController();
    m->initializeControllerPowerOn(0xe1122026u);m->enableNormalRuntime();
    auto bytes=eii::readFile(std::filesystem::u8path(argv[2]));
    if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))
        bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
    m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
    struct Sine final:eii::analog::SampleInputSource {
        double volts(std::uint64_t tick,double frac=0)override{
            return .05*std::sin((double(tick)+frac)*440*2*3.14159265358979323846/4000000);
        }
    };
    m->sampleInput=std::make_shared<Sine>();
    // DSP callbacks can execute on their own thread. Atomic count is training
    // status only; this program neither saves nor returns rendered audio.
    m->enableRuntimeAudio([&](auto block){frames.fetch_add(block.size(),std::memory_order_relaxed);},
        model=="ssm192"?eii::analog::RuntimeAudioMode::NonlinearChip192:model=="eiii"?eii::analog::RuntimeAudioMode::EiiiCustom:model=="model2"?eii::analog::RuntimeAudioMode::Ssm2Chip192:eii::analog::RuntimeAudioMode::LegacyQ);
    m->nativeAudio(true);m->reconstruction(true);
    const auto stage=[&](const char* label,std::uint64_t ticks){
        std::cout<<model<<" "<<label<<" ..."<<std::endl;m->run(ticks);
        std::cout<<model<<" "<<label<<" done frames="<<frames.load(std::memory_order_relaxed)<<std::endl;
    };
    stage("boot",200000000);
    if(!m->lcd.line(0).starts_with("P"))throw std::runtime_error("Bank did not boot to preset display; select a known bootable OS3.1 bank.");
    stage("idle",8000000);
    for(unsigned n=48;n<56;++n){const std::array<std::uint8_t,3> midi{0x90,std::uint8_t(n),100};m->receiveMidi(midi);}
    stage("eight-notes",16000000);
    for(unsigned n=48;n<56;++n){const std::array<std::uint8_t,3> midi{0x80,std::uint8_t(n),0};m->receiveMidi(midi);}
    stage("release",8000000);
    // Fast/approximate DAC route (the hardware route stays the default afterwards).
    m->nativeAudio(false);for(unsigned n=48;n<52;++n){const std::array<std::uint8_t,3> midi{0x90,std::uint8_t(n),100};m->receiveMidi(midi);}
    stage("fast-route-notes",8000000);
    for(unsigned n=48;n<52;++n){const std::array<std::uint8_t,3> midi{0x80,std::uint8_t(n),0};m->receiveMidi(midi);}
    m->nativeAudio(true);stage("fast-route-release",4000000);
    m->scannerControls.setButton(eii::ScannerButton::Sample,true);stage("sample-button",800000);
    m->scannerControls.setButton(eii::ScannerButton::Sample,false);stage("monitor",8000000);
    const std::array<std::uint8_t,3> on{0x90,60,100};m->receiveMidi(on);stage("monitor-note",8000000);
    m->scannerControls.setButton(eii::ScannerButton::Digit9,true);stage("record-button",800000);
    m->scannerControls.setButton(eii::ScannerButton::Digit9,false);stage("record",4000000);
    m->scannerControls.setButton(eii::ScannerButton::Digit0,true);stage("stop-button",800000);
    m->scannerControls.setButton(eii::ScannerButton::Digit0,false);stage("after-stop",4000000);
    if(!m->sampleReads||!m->sampleSar.conversions)throw std::runtime_error("Sampling workload did not execute; select a bootable OS3.1 bank.");
    std::cout<<"TRAINING_COMPLETE model="<<model<<" reads="<<m->sampleReads<<" conversions="<<m->sampleSar.conversions<<std::endl;
    // Join/stop the DSP before the atomic sink counter goes out of scope.
    m.reset();return 0;
}catch(const std::exception& e){std::cerr<<"TRAINING_FAILED: "<<e.what()<<std::endl;return 1;}
#ifdef _WIN32
#include <windows.h>
int wmain(int argc,wchar_t** argv){
    std::vector<std::string> args;args.reserve(argc);
    for(int i=0;i<argc;++i){const int n=WideCharToMultiByte(CP_UTF8,0,argv[i],-1,nullptr,0,nullptr,nullptr);if(n<=0)return 2;
        std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,argv[i],-1,s.data(),n,nullptr,nullptr);s.pop_back();args.push_back(std::move(s));}
    std::vector<char*> pointers;for(auto& a:args)pointers.push_back(a.data());return train(argc,pointers.data());
}
#else
int main(int argc,char** argv){return train(argc,argv);}
#endif
