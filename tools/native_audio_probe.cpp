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
    require(argc==4||argc==5,"usage: native_audio_probe ROM-folder bank.hfe calibration.txt [reference|native-no-capture]");
    const bool capture=argc==4,native=capture||std::string(argv[4])=="native-no-capture";
    require(native||std::string(argv[4])=="reference","unknown route");
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
    eii::host::AudioQueue queue(1056);
    m.enableRuntimeAudio([&](auto frames){queue.push(frames);},eii::analog::RuntimeAudioMode::NonlinearChip);
    m.calibrateAudio(profile);m.reconstruction(true);m.nativeAudio(native);
    std::deque<eii::analog::NativeFrame> tape;
    std::uint64_t events=0,invalidFrames=0;unsigned maxEvents=0;
    if(capture)m.nativeFrameObserver([&](const auto& frame){events+=frame.count;maxEvents=std::max(maxEvents,unsigned(frame.count));invalidFrames+=!frame.valid;
        tape.push_back(frame);if(tape.size()>9600)tape.pop_front();});
    double cpuSeconds=0,energy=0,peak=0;std::uint64_t frames=0;
    const auto begin=std::chrono::steady_clock::now();
    for(unsigned ms=0;ms<6000;++ms){
        if(ms==1000)m.scannerControls.setButton(eii::ScannerButton::Play,true);
        if(ms==1200)m.scannerControls.setButton(eii::ScannerButton::Play,false);
        const double before=eii::host::threadCpuSeconds();m.run(4000);cpuSeconds+=eii::host::threadCpuSeconds()-before;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(queue.stats().queued<48){require(std::chrono::steady_clock::now()<deadline&&!m.chipAudioStats().failures,"DSP failed/timeout");std::this_thread::sleep_for(std::chrono::microseconds(50));}
        std::array<eii::host::StereoFrame,48> out;queue.pop(out);
        for(auto v:out){require(std::isfinite(v.left),"nonfinite live PCM");energy+=double(v.left)*v.left;peak=std::max(peak,double(std::abs(v.left)));++frames;}
        if((ms+1)%1000==0)std::cout<<"LIVE second="<<(ms+1)/1000<<" LCD=["<<m.lcd.line(0)<<"]["<<m.lcd.line(1)<<"] ownerCPU="<<cpuSeconds<<std::endl;
    }
    const auto stats=m.chipAudioStats();
    std::uint64_t digitalHash=14695981039346656037ull;
    for(unsigned char c:m.report()){digitalHash^=c;digitalHash*=1099511628211ull;}
    std::cout<<std::setprecision(10)<<"ROUTE native="<<native<<" capture="<<capture<<" digitalHash="<<std::hex<<digitalHash<<std::dec<<" frames="<<frames<<" events="<<events<<" maxEvents="<<maxEvents<<" invalidFrames="<<invalidFrames
        <<" overflow="<<m.nativeAudioOverflows()<<" orderErrors="<<m.nativeAudioOrderingErrors()<<" controllerInvalid="<<m.channelTimers.controllerTiming.productionEngineInvalid()
        <<" ownerCPU="<<cpuSeconds<<" elapsed="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<" peak="<<peak<<" rms="<<std::sqrt(energy/frames)
        <<" DSPdropped="<<stats.dropped<<" failures="<<stats.failures<<" queueDropped="<<queue.stats().dropped<<" missing="<<queue.stats().missing<<std::endl;
    require(!invalidFrames&&!m.nativeAudioOverflows()&&!m.nativeAudioOrderingErrors()&&!m.channelTimers.controllerTiming.productionEngineInvalid(),"native/digital failure");
    require(!stats.dropped&&!stats.failures&&!queue.stats().dropped&&!queue.stats().missing&&peak>1e-7,"live PCM transport/signal failure");
    if(!capture)return 0;
    m.nativeFrameObserver({});
    const eii::host::AudioFloatScope floats;
    for(unsigned factor:{2u,4u}){
        std::vector<std::uint32_t> reference;
        for(unsigned pass=0;pass<3;++pass){
            const bool reconstruction=pass==2;
            auto bank=std::make_unique<eii::analog::NonlinearVoiceBank>(factor);bank->calibrate(profile);
            std::vector<std::uint32_t> pcm;pcm.reserve(tape.size());double power=0,max=0;
            const double before=eii::host::threadCpuSeconds();
            for(const auto& frame:tape){eii::analog::NonlinearVoiceBank::Input in;in.native=frame;in.reconstructionEnabled=reconstruction;
                const float v=bank->process(in);require(std::isfinite(v),"nonfinite replay PCM");pcm.push_back(std::bit_cast<std::uint32_t>(v));power+=double(v)*v;max=std::max(max,double(std::abs(v)));}
            const auto s=bank->solverStats();unsigned changed=0;
            if(pass==0)reference=pcm;else if(pass==1)require(reference==pcm,"identical native tape replay is not bit exact");
            else for(unsigned n=0;n<pcm.size();++n)changed+=pcm[n]!=reference[n];
            std::cout<<"REPLAY factor="<<factor<<" pass="<<pass<<" S3528="<<reconstruction<<" frames="<<pcm.size()<<" changed="<<changed
                <<" cpu="<<eii::host::threadCpuSeconds()-before<<" peak="<<max<<" rms="<<std::sqrt(power/pcm.size())<<" samples="<<s.samples<<" fallbacks="<<s.fallbacks<<" held="<<s.held<<std::endl;
            require(max>1e-7&&(!reconstruction||changed>100),"native tape/S3528 did not reach PCM");
        }
    }
    return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<std::endl;return 1;}
