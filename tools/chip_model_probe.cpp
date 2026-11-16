// Native-only real firmware/input lab. No production emulation changes.
#include "machine.h"
#include "floppy_hfe.h"
#include "midi_host_tools.h"
#include <sys/wait.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <fstream>
#include <memory>
struct PcmStats {
    double energy=0,peak=0;std::uint64_t frames=0,nonfinite=0,clipped=0;
    void add(auto block){for(auto f:block){++frames;if(!std::isfinite(f.left)){++nonfinite;continue;}energy+=double(f.left)*f.left;peak=std::max(peak,double(std::abs(f.left)));clipped+=std::abs(f.left)>=1;}}
};
static void state(eii::Machine& m,const char* label,const PcmStats& s={}) {
    std::cout<<label<<" tick="<<m.ticks<<" LCD=["<<m.lcd.line(0)<<"]["<<m.lcd.line(1)<<"] frames="<<s.frames<<" rms="<<(s.frames?std::sqrt(s.energy/s.frames):0)<<" peak="<<s.peak<<" nonfinite="<<s.nonfinite<<" clipped="<<s.clipped<<" ACV=";
    for(auto cv:m.controlVoltage.held[eii::ControlVoltageBus::Amplitude])std::cout<<cv.volts<<',';
    std::cout<<" PC="<<std::hex<<m.main.cpu.pc<<'/'<<m.scanner.cpu.pc<<" IY="<<m.main.cpu.iy<<std::dec<<" flags="<<unsigned(m.readMemory(false,m.main.cpu.iy))<<'/'<<unsigned(m.readMemory(false,std::uint16_t(m.main.cpu.iy+9)))<<" UART="<<m.midiSerial.received<<'/'<<m.midiSerial.reads<<'/'<<m.midiSerial.overruns<<'/'<<m.midiSerial.rejected<<" PIO=";
    for(auto n:{&m.main,&m.scanner})for(auto& p:n->pio)std::cout<<unsigned(p.mode)<<':'<<p.enabled<<':'<<p.ready<<':'<<p.pending<<':'<<p.servicing<<',';
    std::cout<<" CTC=";for(auto n:{&m.main,&m.scanner})for(auto& c:n->ctc.chn)std::cout<<unsigned(c.int_state)<<',';
    std::cout<<std::endl;
}
int main(int argc,char**argv)try {
    if(argc<3||argc>4)throw std::invalid_argument("usage: chip_model_probe ROM-folder original-bank-image [wall-seconds-per-sim-second;0=offline-drain]");
    const double pace=argc==4?std::stod(argv[3]):1;
    if(!std::isfinite(pace)||pace<0||pace>4)throw std::invalid_argument("pace must be0..4");
    auto ptr=std::make_unique<eii::Machine>(0);auto& m=*ptr;
    m.channelTimers.enableControllerTiming();m.channelTimers.controllerTiming.enableRegisterWrites();
    const std::filesystem::path rom=argv[1];
    m.load(rom/"main.bin",rom/"scanner.bin",rom/"ic135.bin",rom/"ic137.bin");
    m.initializeDramPowerOn(1);m.enableProductionController();m.initializeControllerPowerOn(0xe1122026u);m.enableNormalRuntime();
    auto bytes=eii::readFile(argv[2]);if(bytes.size()>=8&&std::equal(bytes.begin(),bytes.begin()+8,"HXCPICFE"))bytes=eii::HfeImage(std::move(bytes)).extractEmuii();
    m.floppy.attach(0,37,400000);m.floppy.drives[0]->insertSensorDisk(true);m.diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m.diskRead->mount(0,std::make_shared<const eii::EmuiiImage>(std::move(bytes)),eii::EmuiiGap::Hxc2026);
    while(m.ticks<200000000){m.run(1000000);if(m.diskDma.transfers>=520552&&m.lcd.line(0).find("P01")==0&&!m.floppy.drives[0]->selected())break;}
    m.run(12000000);state(m,"BOOT");

    auto hash=[](const auto& data){std::uint64_t h=14695981039346656037ull;for(auto b:data){h^=b;h*=1099511628211ull;}return h;};
    unsigned failed=0;
    for(unsigned model=0;model<3;++model) {
        const auto pid=fork();if(pid<0)throw std::runtime_error("fork");
        if(!pid) {
            PcmStats stats;std::mutex mutex;std::condition_variable drained;
            const auto mode=model==2?eii::analog::RuntimeAudioMode::NonlinearChip192:model==1?eii::analog::RuntimeAudioMode::NonlinearChip:eii::analog::RuntimeAudioMode::BasicVoice;
            m.enableRuntimeAudio([&](auto pcm){std::lock_guard lock(mutex);stats.add(pcm);drained.notify_one();},mode);
            const auto startTick=m.ticks;
            const auto start=std::chrono::steady_clock::now();
            auto run=[&](std::uint64_t ticks){while(ticks){const auto n=std::min<std::uint64_t>(20000,ticks);m.run(n);ticks-=n;
                if(pace==0) {
                    const auto wanted=(m.ticks-startTick)*48000/4000000;
                    std::unique_lock lock(mutex);
                    if(!drained.wait_for(lock,std::chrono::seconds(5),[&]{return stats.frames==wanted;}))throw std::runtime_error("offline DSP drain failed");
                } else std::this_thread::sleep_until(start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(pace*double(m.ticks-startTick)/4000000)));}};
            for(unsigned note=60;note<68;++note){const std::array<std::uint8_t,3> msg{0x90,std::uint8_t(note),100};if(!m.receiveMidi(msg))throw std::runtime_error("note rejected");run(20000);}
            run(8000000);
            for(unsigned note=60;note<68;++note){const std::array<std::uint8_t,3> msg{0x80,std::uint8_t(note),0};if(!m.receiveMidi(msg))throw std::runtime_error("noteoff rejected");}
            run(8000000);
            const auto expected=(m.ticks-startTick)*48000/4000000;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            for(;;) {
                {std::lock_guard lock(mutex);if(stats.frames==expected)break;}
                if(std::chrono::steady_clock::now()>deadline)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto dsp=m.chipAudioStats();
            bool pass;
            {std::lock_guard lock(mutex);pass=stats.frames==expected&&stats.nonfinite==0&&stats.peak>1e-6&&!dsp.dropped&&!dsp.failures;
                state(m,model==2?"NONLINEAR192":model==1?"NONLINEAR96":"LEGACY",stats);}
            const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            std::cout<<"MODEL "<<model<<" wallPerSim="<<pace<<" expectedFrames="<<expected<<" wall="<<wall<<" dspRendered="<<dsp.rendered<<" dspDropped="<<dsp.dropped<<" dspFailures="<<dsp.failures<<" PASS="<<pass<<"\n";
            std::cout<<"STATE model="<<model<<" memory="<<hash(m.memory)<<" known="<<hash(m.dramKnown)<<" scanner="<<hash(m.scannerRam)<<" tick="<<m.ticks<<" PC="<<m.main.cpu.pc<<'/'<<m.scanner.cpu.pc<<" invalid="<<m.channelTimers.controllerTiming.productionEngineInvalid()<<std::endl;
            _exit(pass?0:1);
        }
        int status=0;waitpid(pid,&status,0);if(!WIFEXITED(status)||WEXITSTATUS(status))++failed;
    }
    return failed?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
