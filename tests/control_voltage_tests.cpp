#include "machine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
int main() {
    try {
        eii::ControlVoltageBus bus;
        require(bus.knownCount()==0 && !bus.dacKnown && !bus.muxKnown,"power-up CVs are unknown");
        bus.writeDac(128,1);require(bus.knownCount()==0,"DAC alone cannot establish a selected capacitor");
        bus.writeMux(0x3f,2);require(bus.knownCount()==0,"all inhibited banks hold unknown state");
        bus.writeMux(0x32,3); // amplitude channel 2; frequency and Q inhibited
        require(bus.held[0][2].known && bus.held[0][2].volts==2.5 && bus.knownCount()==1,"amplitude mux address and 8-bit half-scale");
        bus.writeMux(0x3a,4);bus.writeDac(255,5);
        require(bus.held[0][2].volts==2.5 && bus.held[0][2].lastTrackTick==3,"inhibited sample holds prior charge");
        bus.writeMux(0x2d,6); // frequency channel 5
        require(bus.held[1][5].known && bus.held[1][5].volts==4.98046875,"DAC full-scale is 255/256");
        bus.writeDac(1,7);
        require(bus.held[1][5].volts==0.01953125,"connected capacitor tracks later DAC writes");
        bus.writeMux(0x1f,8); // resonance channel 7
        require(bus.held[2][7].code==1 && bus.held[2][7].known,"Q bank uses PDO5 inhibit");
        bus.writeMux(0xc0,9); // upper bits unlatched; all banks channel 0
        require(bus.muxLatch==0 && bus.held[0][0].known && bus.held[1][0].known && bus.held[2][0].known,"multiple enabled banks track together and upper bits are ignored");
        bus.writeDac(0,10);
        require(bus.held[0][0].volts==0 && bus.held[1][0].volts==0 && bus.held[2][0].volts==0,"zero DAC code");
        // Every bank/channel is addressed from independent expected bit
        // constants; this catches swapped inhibit lines and address bits.
        for(unsigned bank=0;bank<3;++bank)for(unsigned channel=0;channel<8;++channel) {
            const std::uint8_t inhibits[]={0x30,0x28,0x18};
            bus.writeMux(0x38,11);bus.writeDac(std::uint8_t(8*bank+channel),12);
            bus.writeMux(inhibits[bank]|channel,13);
            require(bus.held[bank][channel].code==8*bank+channel,"all 24 held outputs are independently addressable");
        }
        auto m=std::make_unique<eii::Machine>();
        // Synthetic Z80 bus program uses decode aliases and changes targets
        // with DAC data held. It runs on main CPU, never injects analog state.
        const std::uint8_t code[]={
            0x3e,0x38,0xd3,0x77, // inhibit all via alias of mux base 70h
            0x3e,0x80,0xd3,0x7f, // half-scale via alias of DAC base 78h
            0x3e,0x33,0xd3,0x70, // amplitude channel 3
            0x3e,0x38,0xd3,0x70, // disconnect before changing DAC
            0x3e,0xff,0xd3,0x78,
            0x3e,0x29,0xd3,0x70, // frequency channel 1
            0x3e,0x18,0xd3,0x70, // Q channel 0, same held DAC code
            0x76};
        std::copy(std::begin(code),std::end(code),m->main.rom.begin());
        m->run(1000);
        const auto& cv=m->controlVoltage;
        require(cv.dacWrites==2 && cv.muxWrites==5,"CPU OUT strobes reach DAC/mux once");
        require(cv.knownCount()==3 && cv.held[0][3].volts==2.5 && cv.held[1][1].volts==4.98046875 && cv.held[2][0].volts==4.98046875,"real Z80 instructions drive all control banks");
        require(m->unmappedIo==0,"control ports are mapped without unmapped fallback");
        const auto writes=cv.dacWrites;
        m->output(m->scanner,0x78,0);require(cv.dacWrites==writes,"scanner I/O cannot write main DAC");
        std::cout<<"PASS: AD7524 code levels, 24 ideal track/hold routes, inhibit/address wiring, alias decode, unknown power-up and real Z80 OUT integration; not analog settling calibration\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
