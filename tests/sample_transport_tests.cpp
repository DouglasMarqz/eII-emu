#include "machine.h"
#include "sample_dac.h"
#include "playback_analog.h"
#include "transport_fixture.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    // Rising -CHAK, rotated selection, holds and possible edges.
    eii::SampleDacRegisters dac;
    dac.drive({0,7},{true,false},{0x12,255});
    require(dac.codes[7].known==0,"power-up latch does not invent a code");
    dac.drive({0,7},{true,true},{0x34,255});
    require(dac.codes[7].known==0,"ACK assertion falling edge does not clock DAC");
    dac.drive({0,7},{true,false},{0xa5,255});
    require(dac.codes[7].known==255&&dac.codes[7].value==0xa5,"Y0 clocks channel7 on ACK release");
    for(unsigned ch=0;ch<8;++ch){dac.drive({std::uint8_t((ch+1)&7),7},{true,true},{0,255});dac.drive({std::uint8_t((ch+1)&7),7},{true,false},{std::uint8_t(0x40+ch),255});}
    for(unsigned ch=0;ch<8;++ch)require(dac.codes[ch].known==255&&dac.codes[ch].value==0x40+ch,"all eight independent channel latches retain their codes");
    dac.drive({1,7},{false,false},{0,255});dac.drive({1,7},{true,false},{0x41,255});
    require(dac.codes[0].known==254,"possible ACK edge merges hold/capture instead of choosing one");
    require(dac.codes[1].known==255,"uncertain channel0 ACK cannot corrupt channel1");

    // Shared DRAM strobe path and IC40 are independently exercised first.
    std::vector<std::uint8_t> bytes(0x80000),known(0x80000);bytes[0x75324]=0xb6;known[0x75324]=255;
    eii::DramBackingView memory{&bytes,&known};eii::DramCpuPort port;
    auto rise=[&](unsigned ctl,eii::Known24 address=eii::Known24{0x75324,0xfffff}){port.rising({std::uint16_t(ctl),65535},address,memory);};
    auto fall=[&](unsigned ctl){port.falling({std::uint16_t(ctl),65535},{true,false},memory);};
    // UCD07 captures SUM, then the original sample strobe sequence.
    rise(0x80);fall(0x80);rise(0x80);fall(0x80);
    rise(0x80);port.settle({0x8080,65535},memory);fall(0x8080);
    rise(0x8080);port.settle({0x9880,65535},memory);fall(0x9880);
    // The CPU bus can be idle/updated between row and column without
    // erasing the sample transaction that owns this row.
    port.cpuBus(false,0,{0,255},true);
    rise(0x9880);port.settle({0x9880,65535},memory);fall(0x9880);
    require(port.sampleReads==1&&!port.readReady&&port.channelLatch.known==0,"sample CAS reads DRAM independently of CPU request/IC103 readiness");
    rise(0x9880);port.settle({0x180,65535},memory);fall(0x180);
    require(port.channelLatch.known==0,"IC40 gated clock falling edge does not capture");
    rise(0x180);require(port.channelLatch.known==255&&port.channelLatch.value==0xb6,"IC40 captures OLD MDO before same-edge CAS closing");
    require(!port.readReady,"playback read never services an inactive CPU");

    for(unsigned blocked:{0x400u,0x4000u}) {
        eii::DramCpuPort blockedPort;
        const auto r=[&](unsigned ctl){blockedPort.rising({std::uint16_t(ctl),65535},{0x75324,0xfffff},memory);};
        const auto f=[&](unsigned ctl){blockedPort.falling({std::uint16_t(ctl),65535},{true,false},memory);};
        r(0x80);f(0x80);r(0x80);f(0x80);
        r(0x80);blockedPort.settle({std::uint16_t(0x8080|blocked),65535},memory);f(0x8080|blocked);
        r(0x8080|blocked);blockedPort.settle({std::uint16_t(0x9880|blocked),65535},memory);f(0x9880|blocked);
        r(0x9880|blocked);f(0x9880|blocked);r(0x9880|blocked);
        blockedPort.settle({0x180,65535},memory);f(0x180);r(0x180);
        require(blockedPort.sampleReads==0&&blockedPort.channelLatch.known==0&&!blockedPort.writeReady,"write mode and asynchronous bank reset cannot fabricate playback reads/CPU writes");
        require(bytes[0x75324]==0xb6,"unsupported sample record never mutates backing RAM");
    }

    // Real Z80 OUT -> staged RF -> pipeline -> ALU SUM -> ALUGO -> RAM ->
    // IC40 -> rotated ACK -> all eight DAC latches; plus interleaved CPU I/O.
    const auto prom=transportProm();
    for(unsigned phase=0;phase<=16;++phase) {
        auto m=std::make_unique<eii::Machine>();
        std::vector<eii::host::StereoFrame> pcm;
        if(phase==16)m->enableRuntimeAudio([&](auto frames){pcm.insert(pcm.end(),frames.begin(),frames.end());});
        if(phase<16){m->channelTimers.controllerTiming=eii::ControllerTiming(phase);m->channelTimers.enableControllerTiming();}
        m->channelTimers.controllerTiming.loadProms(prom);m->enableCpuDram();
        std::vector<std::uint8_t> code;
        const auto out=[&](unsigned port,unsigned value){code.insert(code.end(),{0x3e,std::uint8_t(value),0xd3,std::uint8_t(port)});};
        // IC92 has no reset. A dummy strobe/scan establishes its cleared
        // idle state before the eight deliberate register initializations.
        out(0x48,63);out(0x60,0);code.insert(code.end(),64,0);
        for(unsigned ch=0;ch<8;++ch){
            const unsigned address=(ch<<16)|0x5234;const unsigned datum=0x91+ch*11;
            m->memory[address]=datum;m->dramKnown[address]=255;
            out(0x48,ch*4+1);out(0x50,address&255);out(0x58,(address>>8)&255);out(0x60,address>>16);
            code.insert(code.end(),32,0); // permit the staged ninth-slot write
        }
        if(phase==16)for(unsigned ch=0;ch<8;++ch){
            out(0x70,0x30|ch);out(0x78,128); // actual amplitude CV hold
            out(0x70,0x28|ch);out(0x78,200); // actual filter CV hold
        }
        const std::uint8_t tail[]={0x3e,0xd5,0x32,0x89,0x67,0xaf,0x3a,0x89,0x67,0x32,0,0x20,0x76};
        code.insert(code.end(),std::begin(tail),std::end(tail));std::copy(code.begin(),code.end(),m->main.rom.begin());m->run(5000);
        if(phase==16) {
            require(pcm.size()==60,"actual Z80/sample-DAC path produces exactly48kHz monitor frames");
            require(std::any_of(pcm.begin(),pcm.end(),[](auto f){return std::abs(f.left)>1e-6f;}),
                    "real OUT/RF/DRAM/ACK/DAC/CV/VCF/VCA transport reaches nonzero host PCM");
        }
        for(unsigned ch=0;ch<8;++ch){const auto v=m->channelTimers.controllerTiming.channelDacCode(ch);
            if(v.known!=255||v.value!=0x91+ch*11)std::cerr<<m->report()<<"RF value/mask="<<m->channelTimers.controllerTiming.registerValue(ch*4+1).value<<"/"<<m->channelTimers.controllerTiming.registerValue(ch*4+1).known<<"\n"<<"phase="<<phase<<" ch="<<ch<<" value/mask="<<unsigned(v.value)<<"/"<<unsigned(v.known)<<"\n";
            require(v.known==255&&v.value==0x91+ch*11,"all eight DAC codes match their bank's DRAM byte through the complete digital path");
        }
        require((m->main.pins&Z80_HALT)&&m->memory[0x2000]==0xd5&&m->dramWriteCycles==1&&m->dramReadCycles==1,"CPU WAIT/read/write coexists with continuously serviced playback reads");
        if(phase==16) {
            // A fresh read from an unknown byte must replace the old known
            // sample, without poisoning other channels or inventing CPU I/O.
            m->dramKnown[0x35234]=0;m->run(300);
            require(m->channelTimers.controllerTiming.channelDacCode(3).known==0,"unknown sample RAM replaces stale known DAC data");
            require(m->channelTimers.controllerTiming.channelDacCode(4).known==255,"unknown bank3 sample leaves bank4 DAC independent");
            m->dramKnown[0x35234]=255;m->memory[0x35234]=0x27;m->run(300);
            const auto refreshed=m->channelTimers.controllerTiming.channelDacCode(3);
            require(refreshed.known==255&&refreshed.value==0x27,"new sample data propagates on the next service/ACK cycle");
            require(m->dramWriteCycles==1&&m->dramReadCycles==1,"sample transactions never count as CPU requests");
            // Real DAC-code and amplitude-CV latches feed the explicit
            // zero-Q diagnostic analog route. This is not OS audio playback.
            const auto input=m->playbackAnalogInputs();
            eii::analog::PlaybackAnalogBank bank(192000);bank.resetKnownZero();
            std::array<eii::analog::PlaybackPins,8> pins;
            for(auto& p:pins){p.filterGm={100e-6,true};p.vcaBranchAmps={10e-6,true};p.channel0PlaybackConfirmed=true;p.diagnosticDirectReconstruction=true;p.diagnosticDirectDeglitch=true;}
            std::array<eii::analog::PlaybackObservation,8> analog;
            for(unsigned n=0;n<16000;++n)analog=bank.process(input,pins);
            for(unsigned ch=0;ch<8;++ch){
                const auto nominal=eii::analog::Am6072::eiiSteadyVolts(input.codes[ch],input.amplitudeCv[ch]);
                require(nominal.known&&analog[ch].dacVolts.known&&std::abs(analog[ch].dacVolts.value-nominal.value)<1e-10,"real sample/CV latches reach nominal DAC voltage");
                require(analog[ch].feedbackVolts.known&&std::isfinite(analog[ch].feedbackVolts.value),"diagnostic DAC/filter/VCA/feedback path receives real hardware latch observations");
            }
        }
        if(phase<16) {
            // Test reset hold during a known inactive ACK interval. Reset can
            // otherwise reroute ACK and clock a latch; it is not a latch clear.
            for(unsigned n=0;n<100;++n){const auto a=m->channelTimers.controllerTiming.acknowledgeEnable;if(a.known&&!a.high)break;m->run(1);}
            require(m->channelTimers.controllerTiming.acknowledgeEnable.known&&!m->channelTimers.controllerTiming.acknowledgeEnable.high,"idle ACK interval reached");
            const auto held=m->channelTimers.controllerTiming.channelDacCode(3);m->channelTimers.controllerTiming.reset(true);
            require(m->channelTimers.controllerTiming.channelDacCode(3)==held,"CRST does not asynchronously clear channel DAC latches");
        }
    }
    std::cout<<"PASS: shared sample DRAM/IC40 old-data capture, independent CPU readiness, rotated rising-ACK DAC latches/unknown edges and real Z80 staging-to-RF-to-ALU-to-DRAM-to-eight-DAC path across all banks/16 concrete phases plus phase envelope; interleaved strict CPU reads/writes and ideal analog snapshot bridge pass; no OS audio or recording\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
