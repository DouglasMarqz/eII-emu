#include "machine.h"
#include <iostream>
#include <memory>
#include <stdexcept>
static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
int main() {
    try {
        eii::ChannelRequestTiming c;
        require(!c.dividerKnown() && !c.request.known,"power-up divider and request unknown");
        for(unsigned n=1;n<=100;++n)c.fallingSourceClock(n);
        require(!c.dividerKnown() && c.requestClocks==0,"unknown timer cannot establish a fake phase");
        c.acknowledge(false);c.acknowledge(true);c.fallingSourceClock(101);
        require(!c.request.known,"unknown timer cannot preserve a falsely known cleared request after ACK release");
        c.timer({true,true});
        for(unsigned n=1;n<=64;++n)c.fallingSourceClock(n);
        // All possible first-half power-up phases are now stopped by Q2.
        // Timer clear then removes the unused second-half phase ambiguity.
        require(!c.dividerKnown(),"unused second-half startup bits still unknown");
        c.timer({true,false});
        require(c.dividerKnown() && c.firstCount()==0 && c.secondLowBits()==0,"timer reset after feedback stop synchronizes divider");
        c.acknowledge(false);c.acknowledge(true);c.timer({true,true});
        const auto before=c.requestClocks;
        for(unsigned n=1;n<=64;++n) {
            c.fallingSourceClock(100+n);
            if(n==16) require(c.phi1().known && c.phi1().high && !c.phi2().high,"phase 1 at second count 1");
            if(n==32) require(!c.phi1().high && !c.phi2().high,"non-overlap phase at second count 2");
            if(n==48) require(!c.phi1().high && c.phi2().high,"phase 2 at second count 3");
            if(n<64) require(c.request.known && !c.request.high,"request waits for fourth divided edge");
        }
        require(c.request.high && c.requestClocks==before+1 && c.lastRequestClock==164,"Q2 rising clocks D=1 request latch");
        for(unsigned n=0;n<1000;++n)c.fallingSourceClock(165+n);
        require(c.firstCount()==0 && c.secondLowBits()==4 && c.requestClocks==before+1,"feedback stops divider; no repeated request without timer reset");
        c.acknowledge(false);
        require(c.request.known && !c.request.high,"active-low CHAK asynchronously clears request");
        c.acknowledge(true);
        require(!c.request.high,"ACK release does not retrigger a held-high request clock");
        c.timer({true,false});c.fallingSourceClock(2000);c.timer({true,true});
        for(unsigned n=1;n<=63;++n)c.fallingSourceClock(2000+n);
        require(c.request.high && c.lastRequestClock==2063,"timer clears second half only; first half runs during low pulse");
        c.acknowledge(false);c.timer({true,false});c.timer({true,true});
        for(unsigned n=1;n<=64;++n)c.fallingSourceClock(3000+n);
        require(c.request.known && !c.request.high,"asynchronous ACK dominates request clock");
        eii::ChannelRequestTiming sample(true);
        sample.timer({true,true});
        for(unsigned n=0;n<128;++n)sample.fallingSourceClock(n);
        sample.timer({true,false});sample.timer({true,true});
        require(sample.dividerKnown(),"sampling channel synchronizes after Q3 feedback stop");
        for(unsigned n=1;n<=128;++n) {
            sample.fallingSourceClock(n);
            if(n==80) require(sample.samplingPulse().known && sample.samplingPulse().high && !sample.phi1().high && !sample.phi2().high,"channel 0 sampling phase Q2 & !Q1 & Q0, playback phases gated off");
            if(n==64) require(sample.firstCount()==0 && sample.secondLowBits()==4,"channel 0 continues past Q2 request edge");
        }
        require(sample.secondLowBits()==8 && sample.firstCount()==0,"sampling channel stops on Q3, not Q2");
        eii::ChannelTimers bank;
        bank.write(0x1b,0x34);bank.write(0x18,163);bank.write(0x18,0);
        bank.driveControllerPins(1,7,{true,true});bank.driveControllerPins(0,7,{true,false});
        for(unsigned n=0;n<1000;++n)bank.advanceCpuTick();
        require(bank.channels[0].dividerKnown() && bank.channels[0].requestClocks>10 && bank.channels[0].request.high,"100ns timer reset pulses survive 250ns CPU grouping");
        require(!bank.channels[1].dividerKnown() && !bank.channels[1].request.known,"unprogrammed channels retain unknown state");
        // Observe pulse scheduling against an independent elementary divider
        // for many full periods, including the one-clock reset interval.
        unsigned first=0,second=0;std::uint64_t expected=0;
        eii::ChannelRequestTiming referenceChannel;auto& ch=referenceChannel;
        // Synchronize both at a feedback-stopped state then timer-clear.
        ch.timer({true,true});for(unsigned n=0;n<64;++n)ch.fallingSourceClock(n);
        ch.timer({true,false});
        const auto initial=ch.requestClocks;
        for(unsigned tick=1;tick<=8000;++tick) {
            const bool high=tick%80!=0;
            ch.timer({true,high});
            if(!high)second=0;
            const auto oldSecond=second;
            if(second&4)first=0;
            else if(++first==16) {first=0;if(high)++second;}
            if(!(oldSecond&4) && (second&4))++expected;
            if(second&4)first=0;
            ch.fallingSourceClock(tick);
            require(ch.firstCount()==first && ch.secondLowBits()==second,"settled circuit follows independent two-counter reference");
        }
        require(ch.requestClocks==initial+expected,"request edge count matches reference");
        auto m=std::make_unique<eii::Machine>();
        m->output(m->main,0x1b,0x34);m->output(m->main,0x18,0x68);m->output(m->main,0x18,1);
        m->run(4000);
        require(m->channelTimers.channels[0].requestClocks>20 && m->channelTimers.channels[0].dividerKnown(),"machine scheduler drives request divider");
        require(!m->channelTimers.serviceBus.acknowledgeEnable.known,"unclocked PROM ACK enable remains unknown in machine");
        std::cout<<"PASS: HC393 feedback/reset, startup uncertainty convergence, two settled filter phases, HCT74 request/ACK priority, preserved 100ns pulses, independent counter reference and machine integration\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
