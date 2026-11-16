#include "machine.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
int main() {
    try {
        eii::ChannelServiceBus bus;
        std::array<eii::LogicLevel,8> req{};
        require(!bus.ureq.known,"no assumed startup selection/request");
        for(unsigned mask=0;mask<8;++mask) for(unsigned select=0;select<8;++select)
            for(unsigned enable=0;enable<3;++enable) {
                bus.drive(select,mask,{enable<2,bool(enable)});
                for(unsigned channel=0;channel<8;++channel) {
                    bool seenHigh=false,seenLow=false;
                    for(unsigned code=0;code<8;++code) if(((code^select)&mask)==0)
                        for(unsigned e=0;e<2;++e) if(enable==2 || e==enable) {
                            const bool low=e && channel==((code+7)&7);
                            seenLow|=low;seenHigh|=!low;
                        }
                    const auto actual=bus.acknowledge[channel];
                    require(actual.known==!(seenLow&&seenHigh) && (!actual.known || actual.high==seenHigh),"ACK truth table across all partial selections and enable states");
                }
            }
        for(unsigned select=0;select<8;++select) for(unsigned active=0;active<8;++active) {
            for(unsigned i=0;i<8;++i)req[i]={true,i==active};
            bus.drive(select,7,{true,false});bus.requests(req);
            require(bus.ureq.known && bus.ureq.high==(select==active),"HCT151 selects corresponding CHRQ without inversion");
        }
        req.fill({true,false});bus.drive(0,0,{false,false});bus.requests(req);
        require(bus.ureq.known && !bus.ureq.high,"all zero requests determine UREQ even if selection unknown");
        req[7]={false,false};bus.requests(req);require(!bus.ureq.known,"unknown possible request propagates");
        bus.drive(0,4,{true,false});bus.requests(req);require(bus.ureq.known && !bus.ureq.high,"unknown unselected input cannot contaminate UREQ");
        for(unsigned channel=0;channel<8;++channel) {
            eii::ChannelTimers bank;
            bank.driveControllerPins((channel+1)&7,7,{true,true});
            require(bank.channels[channel].request.known && !bank.channels[channel].request.high,"rotated ACK reaches physical request latch");
            bank.driveControllerPins(channel,7,{true,false});
            auto& divider=bank.channels[channel];divider.timer({true,true});
            for(unsigned n=1;n<=128;++n)divider.fallingSourceClock(n);
            divider.timer({true,false});divider.timer({true,true});
            // Timer registers maintain this known-high source during the test.
            bank.counters[channel].configure(0x34);
            const auto before=divider.requestClocks;
            bank.advanceSourceClocks(64);
            require(divider.requestClocks==before+1 && divider.request.known && divider.request.high,"selected request becomes high on Q2 clock");
            require(bank.serviceBus.ureq.known && bank.serviceBus.ureq.high,"latched CHRQ propagates to UREQ within source edge");
            bank.driveControllerPins((channel+1)&7,7,{true,true});
            require(divider.request.known && !divider.request.high,"ACK clears serviced channel immediately");
            bank.driveControllerPins(channel,7,{true,false});
            require(bank.serviceBus.ureq.known && !bank.serviceBus.ureq.high,"cleared request feeds back through mux without a fabricated CPU step");
        }
        eii::ChannelTimers bank;
        bank.driveControllerPins(0,0,{true,false});
        for(const auto ack:bank.serviceBus.acknowledge)require(ack.known && ack.high,"disabled decoder releases all ACKs even with unknown selection");
        bank.driveControllerPins(0,7,{true,true}); // Y0 -> channel 7
        bank.channels[7].acknowledge(true); // test setup: restore unasserted clear temporarily
        bank.channels[7].timer({true,true});
        for(unsigned n=0;n<64;++n)bank.channels[7].fallingSourceClock(n);
        bank.channels[7].timer({true,false});bank.channels[7].timer({true,true});
        bank.driveControllerPins(0,7,{true,true}); // reapply physical active clear
        bank.counters[7].configure(0x34);bank.advanceSourceClocks(64);
        require(bank.channels[7].request.known && !bank.channels[7].request.high,"controller ACK remains dominant through future request clocks");
        std::cout<<"PASS: channel service HCT151 request mux, rotated HCT138 ACK truth table including partial unknowns, all-eight request/clear feedback, source-edge UREQ propagation and held ACK priority; controller sequencer absent\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
