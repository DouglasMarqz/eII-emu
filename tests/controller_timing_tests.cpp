#include "machine.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
int main() {
    try {
        std::array<eii::LogicLevel,8> requests;requests.fill({true,false});requests[1]={true,true};
        eii::ControllerTiming c(8);c.reset(false);
        for(unsigned edge=1;edge<=72;++edge) {
            c.risingClock(requests,{15,15});
            require(c.microCount.known==15 && c.microCount.value==8+(edge%8),"state counter reloads 8 after 15, not zero");
            require(c.channel.known==15 && c.channel.value==(edge/8)%9,"scan counter includes slot 8 then loads zero");
            if(edge>=8) {
                require(c.shift.known==255,"LS164 becomes known after eight rising edges");
                require(bool(c.shift.value&1)==(edge%8==0),"STATE0 is delayed old carry, high at new state 0");
                require(bool(c.shift.value&8)==(edge%8==3),"STATE3 is three source clocks after STATE0");
            }
            if(edge==8) require(c.captured.known==1 && !(c.captured.value&1),"unknown startup shift preserves known-zero request but uncertain early flag capture");
            if(edge==9) require(c.captured.known==31 && c.captured.value==31,"gated S174 captures OLD STATE0 and selected CHRQ1 on edge ending state 0");
            if(edge==16)require(c.captured.value==31,"input latch holds through rest of slot");
            if(edge==17)require(c.captured.value==30,"next slot samples its matching request");
        }
        const auto oldPhase=c.microCount.value,oldShift=c.shift.value;
        c.reset(true);
        require(c.channel.value==0 && c.channel.known==15 && c.captured.value==0 && c.captured.known==31,"CRST asynchronously clears scan and input latch");
        require(c.microCount.value==oldPhase && c.shift.value==oldShift,"CRST does not reset state counter or shift register");
        c.risingClock(requests,{15,15});
        require(c.microCount.value==9 && c.channel.value==0 && c.captured.value==0,"free-running state and shift continue while CRST held");
        eii::ControllerTiming partial(8);partial.reset(false);
        for(unsigned n=0;n<9;++n)partial.risingClock(requests,{15,5});
        require(partial.captured.known==11,"input latch preserves partial-known flag mask and known selected request");
        requests.fill({false,false});
        for(unsigned n=0;n<8;++n)partial.risingClock(requests,{5,5});
        require(partial.captured.known==10,"unknown selected request is not replaced by false during capture");
        eii::ControllerTiming unknown;
        require(unknown.microCount.known==0 && unknown.channel.known==15 && unknown.shift.known==0,"startup state and shift unknown while scan reset known");
        unknown.reset(false);
        for(unsigned n=0;n<1000;++n)unknown.risingClock(requests);
        require((unknown.microCount.known&8) && (unknown.microCount.value&8) && !(unknown.microCount.known&7),"startup microphase uncertainty persists, no guessed phase lock");
        // Compare the conservative startup model to every concrete initial
        // phase, including a varying request/flag stream and reset episodes.
        eii::ControllerTiming envelope;
        std::array<eii::ControllerTiming,16> concrete;
        for(unsigned i=0;i<16;++i)concrete[i]=eii::ControllerTiming(i);
        envelope.reset(false);for(auto& x:concrete)x.reset(false);
        for(unsigned n=0;n<400;++n) {
            for(unsigned i=0;i<8;++i)requests[i]={true,((n/11+i)%3)==0};
            const eii::KnownByte flags{std::uint8_t(n/7),15};
            if(n==101 || n==104) {envelope.reset(n==101);for(auto& x:concrete)x.reset(n==101);}
            envelope.risingClock(requests,flags);for(auto& x:concrete)x.risingClock(requests,flags);
            for(unsigned signal=0;signal<4;++signal) {
                auto get=[signal](const eii::ControllerTiming& x) {
                    if(signal==0)return x.channel;
                    if(signal==1)return x.microCount;
                    if(signal==2)return x.shift;
                    return x.captured;
                };
                auto expected=get(concrete[0]);
                for(unsigned i=1;i<16;++i) {const auto v=get(concrete[i]);expected.known&=v.known & ~(expected.value^v.value);}
                const auto actual=get(envelope);
                require(actual.known==expected.known && !((actual.value^expected.value)&actual.known),"unknown phase envelope equals all concrete startup phases");
            }
        }
        eii::ChannelTimers bank;bank.enableControllerTiming();
        bank.advanceCpuTick();
        require(bank.halfClocks==5 && bank.sourceClocks==2 && bank.controllerTiming.risingEdges==3,"first 250ns frame retains third rising edge at 250ns");
        bank.advanceCpuTick();
        require(bank.halfClocks==10 && bank.sourceClocks==5 && bank.controllerTiming.risingEdges==5,"second CPU frame completes exact 5/2 source periods");
        for(unsigned n=0;n<1000;++n)bank.advanceCpuTick();
        require(bank.halfClocks==5010 && bank.sourceClocks==2505 && bank.controllerTiming.risingEdges==2505,"shared half-edge clock has no cumulative drift");
        require(bank.serviceBus.selectionKnown==(bank.controllerTiming.channel.known&7),"scan counter drives service mux known bits");
        require(!bank.serviceBus.acknowledgeEnable.known,"no invented PROM ACK output");
        std::cout<<"PASS: S161 modulo-8 state and modulo-9 scan, LS164 STATE0/3, gated S174 inputs/reset, all initial-phase envelope vs concrete paths and 20MHz half-edge machine clock; PROM/datapath still absent\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
