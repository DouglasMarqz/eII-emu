// Native-only real firmware/input lab. No production emulation changes.
#include "machine.h"
#include "floppy_hfe.h"
#include "midi_host_tools.h"
#include <sys/wait.h>
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
    if(argc<3||argc>4)throw std::invalid_argument("usage: stuck_note_probe ROM-folder original-bank-image [raw]");
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
    PcmStats stats;
    m.enableRuntimeAudio([&](auto block){stats.add(block);},argc==4?eii::analog::RuntimeAudioMode::DacMonitor:eii::analog::RuntimeAudioMode::BasicVoice);
    auto run=[&](const char* label,std::uint64_t ticks){stats={};m.run(ticks);state(m,label,stats);};
    auto midi=[&](unsigned a,unsigned b,unsigned c){const std::array<std::uint8_t,3> x{std::uint8_t(a),std::uint8_t(b),std::uint8_t(c)};const auto n=eii::midiMessageLength(x[0]);if(!n||!m.receiveMidi(std::span(x.data(),n)))throw std::runtime_error("invalid/rejected MIDI");};
    auto press=[&](unsigned b){m.scannerControls.setButton(eii::ScannerButton(b),true);m.run(800000);m.scannerControls.setButton(eii::ScannerButton(b),false);m.run(800000);};
    auto digit=[](unsigned n){return n?unsigned(eii::ScannerButton::Digit1)+1-n:unsigned(eii::ScannerButton::Digit0);};
    auto select=[&](unsigned n){press(digit(n/10));press(digit(n%10));run("SELECT",400000);std::ostringstream wanted;wanted<<'P'<<std::setw(2)<<std::setfill('0')<<n;return m.lcd.line(0).find(wanted.str())==0;};

    if(!select(4))throw std::runtime_error("preset04 unavailable");
    midi(0xb0,64,127);midi(0x90,60,127);midi(0x90,64,127);run("PEDAL_HELD_5s",20000000);
    unsigned failures=0;
    for(unsigned fixed=0;fixed<2;++fixed) {
        const auto pid=fork();if(pid<0)throw std::runtime_error("fork");
        if(!pid) {
            const auto received=m.midiSerial.received,reads=m.midiSerial.reads,overruns=m.midiSerial.overruns;
            midi(0x80,60,0);midi(0x80,64,0);
            if(fixed)for(auto message:eii::host::MidiHostTools::releaseAllMessages())m.receiveMidi(message);
            else for(unsigned ch=0;ch<16;++ch)midi(0xb0|ch,123,0);
            run(fixed?"FIXED_RELEASE_8s":"OLD_CC123_8s",32000000);
            double max=0;for(auto cv:m.controlVoltage.held[eii::ControlVoltageBus::Amplitude])max=std::max(max,cv.volts);
            const unsigned expected=fixed?102:54;
            const bool pass=(fixed?max<1:max>4) && m.midiSerial.received-received==expected && m.midiSerial.reads-reads==expected && m.midiSerial.overruns==overruns && !m.channelTimers.controllerTiming.productionEngineInvalid() && stats.nonfinite==0;
            std::cout<<"RESULT fixed="<<fixed<<" maxACV="<<max<<" received/read="<<m.midiSerial.received-received<<'/'<<m.midiSerial.reads-reads<<" expected="<<expected<<" overruns="<<m.midiSerial.overruns-overruns<<" PASS="<<pass<<std::endl;
            _exit(pass?0:1);
        }
        int status=0;waitpid(pid,&status,0);if(!WIFEXITED(status)||WEXITSTATUS(status))++failures;
    }
    return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
