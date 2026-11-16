#include "machine.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
int main() {
    try {
        eii::RateCounter8254 c;
        c.advance(100);require(!c.outputKnown && !c.running,"8254 power-up output unknown");
        c.configure(0x34);c.write(4);c.advance(100);
        require(!c.running && c.completeWrites==0 && c.writeHighNext,"LSB alone cannot start a 16-bit count");
        c.write(0);c.advance(1);
        require(c.count==4 && c.output && !c.nullCount,"initial CLK loads without decrement");
        c.advance(2);require(c.count==2 && c.output && !c.fallingEdges,"mode-2 high phase");
        c.advance(1);require(c.count==1 && !c.output && c.fallingEdges==1,"count=1 gives one low CLK");
        c.advance(1);require(c.count==4 && c.output && c.risingEdges==1,"reload edge ends low pulse");
        c.advance(1);c.write(6);c.advance(1);
        require(c.count==2 && c.reload==4,"partial running write leaves CR/CE active");
        c.write(0);require(c.nullCount && c.count==2,"new reload waits for old period");
        c.advance(1);require(c.count==1 && !c.output && c.nullCount,"old terminal pulse retained");
        c.advance(1);require(c.count==6 && c.output && !c.nullCount,"new count takes effect at reload");
        c.configure(0x14);c.write(0);c.advance(1);
        require(c.count==65536,"binary zero means 65536");
        c.advance(65535);require(!c.output && c.count==1,"maximum binary period low edge");
        c.advance(1);require(c.output && c.count==65536,"maximum binary period reload");
        c.configure(0x24);c.write(2);c.advance(1);require(c.count==512,"MSB-only loading zeroes low byte");
        c.configure(0x14);c.write(1);c.advance(100);
        require(c.invalidCount && !c.outputKnown && !c.running,"illegal mode-2 divisor one is flagged");
        c.configure(0x36);c.write(4);c.write(0);c.advance(100);
        require(!c.supported && !c.outputKnown && !c.running,"unsupported mode 3 cannot produce guessed pulses");
        // Bulk stepping must agree with individual falling-edge stepping,
        // including deferred reload and phase boundaries.
        eii::RateCounter8254 a,b;a.configure(0x34);a.write(7);a.write(0);b=a;
        a.advance(100000);for(int n=0;n<100000;++n)b.advance(1);
        require(a.count==b.count && a.output==b.output && a.risingEdges==b.risingEdges && a.fallingEdges==b.fallingEdges,"bulk stepping equals individual clocks");
        a.write(11);a.write(0);b=a;a.advance(1037);for(int n=0;n<1037;++n)b.advance(1);
        require(a.count==b.count && a.output==b.output && a.risingEdges==b.risingEdges && a.fallingEdges==b.fallingEdges,"bulk stepping preserves live reload timing");
        eii::ChannelTimers bank;
        for(unsigned i=0;i<9;++i) {
            const unsigned bases[]={0x18,0x10,0x08};const auto base=bases[i/3];
            bank.write(base+7,std::uint8_t(((i%3)<<6)|0x34));
            bank.write(base+(i%3)+4,std::uint8_t(i+2));bank.write(base+(i%3)+4,0);
        }
        for(int n=0;n<1000;++n)bank.advanceCpuTick();
        require(bank.sourceClocks==2500 && bank.writes==27,"exact 10MHz to 4MHz rational clock and aliases");
        for(unsigned i=0;i<9;++i)require(bank.counters[i].reload==i+2 && bank.counters[i].risingEdges>0,"all eight channel timers and SIO timer addressed");
        const auto mode=bank.counters[0].mode;bank.write(0x1b,0);bank.write(0x1b,0xc2);
        require(bank.counters[0].mode==mode && bank.unimplementedCommands==2,"unimplemented latch/read-back commands do not corrupt running mode");
        auto m=std::make_unique<eii::Machine>();
        const std::uint8_t code[]={0x3e,0x34,0xd3,0x1f,0x3e,0x68,0xd3,0x1c,0x3e,0x01,0xd3,0x18,0x76};
        std::copy(std::begin(code),std::end(code),m->main.rom.begin());m->run(1000);
        require(m->channelTimers.writes==3 && m->channelTimers.counters[0].reload==360 && m->channelTimers.counters[0].risingEdges>0,"real Z80 OUT starts physical channel-0 timer");
        require(m->channelTimers.sourceClocks==2500 && m->unmappedIo==0,"timer clocks integrated into machine scheduler");
        require(m->input(m->main,0x18)==255,"8254 RD tied high: no invented CPU readback");
        const auto writes=m->channelTimers.writes;m->output(m->scanner,0x18,0);
        require(m->channelTimers.writes==writes,"scanner cannot program main timers");
        std::cout<<"PASS: 8254 binary mode-2 loading/pulses/deferred reload, 16-bit/byte access, board aliases, 10MHz rational clock and Z80 integration; other timer modes absent\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
