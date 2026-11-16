#include "machine.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
int main() {
    try {
        eii::PromConsensus table;
        require(!table.lookup(0,511).known,"missing PROM never reads as invented zeros");
        std::array<std::uint8_t,512> rom;
        for(unsigned i=0;i<512;++i)rom[i]=std::uint8_t((i*37)^(i>>3));
        table.load(rom);
        for(unsigned mask=0;mask<512;++mask)for(unsigned value=0;value<512;++value) {
            if(value&~mask)continue;
            const auto result=table.lookup(value,mask);
            unsigned first=0;bool found=false;std::uint8_t known=255;
            for(unsigned address=0;address<512;++address) if((address&mask)==value) {
                if(!found) {first=rom[address];found=true;}
                known&=std::uint8_t(~(first^rom[address]));
            }
            require(result.known==known && !((result.value^first)&known),"all 3^9 partial PROM inputs agree with independent full-address enumeration");
        }
        rom.fill(0xa5);table.load(rom);
        require(table.lookup(0,0).known==255 && table.lookup(0,0).value==0xa5,"reload truth table resets ternary indexing");
        eii::CpuRequestHandshake handshake;
        handshake.drive({true,false});handshake.cpuClock();
        require(handshake.requestToPipeline().known && !handshake.requestToPipeline().high && !handshake.waitInactive().high,"DRAM request waits for completion");
        handshake.completionClock({true,false});handshake.completionClock({true,true});
        require(handshake.done.known && handshake.done.high && handshake.requestToPipeline().high,"UCD09 rising acknowledges request");
        require(!handshake.waitInactive().high,"wait is not released before the CPU clock");
        handshake.cpuClock();require(handshake.waitInactive().high,"CPU-clocked wait release");
        handshake.drive({true,true});require(handshake.done.known && !handshake.done.high,"inactive request asynchronously resets completion FF");
        handshake.completionClock({true,false});handshake.completionClock({true,true});
        require(!handshake.done.high,"asynchronous clear dominates completion edge");
        handshake.cpuClock();require(!handshake.waitInactive({true,false}).high,"external IWAIT dominates wait release");
        eii::PromLatch image;
        for(unsigned i=0;i<512;++i) {image.upper[i]=std::uint8_t((i*19)^(i>>2));image.lower[i]=std::uint8_t((i*13)^(i>>3));}
        std::array<eii::LogicLevel,8> requests;requests.fill({true,false});
        eii::ControllerTiming c(8);c.loadProms(image);c.reset(false);
        // First pipeline clock becomes known but PROM must still see its
        // previous unknown contents, not the just-sampled idle request.
        c.risingClock(requests,{0,15});
        require(c.requestPipeline.known==3 && c.requestPipeline.value==3,"pipeline captures idle request/read electrical highs");
        for(unsigned i=0;i<512;++i)image.upper[i]=(i&8)?0x11:0x22;
        image.lower.fill(0);c=eii::ControllerTiming(8);c.loadProms(image);c.reset(false);
        c.risingClock(requests,{0,15});
        require((c.control.known&0xff00)==0xcc00,"PROM uses OLD unknown request pipeline on first edge");
        c.risingClock(requests,{0,15});
        require((c.control.known&0xff00)==0xff00 && (c.control.value&0xff00)==0x1100,"captured idle request affects PROM only on following edge");
        for(unsigned i=0;i<512;++i)image.upper[i]=std::uint8_t(i&12);
        c=eii::ControllerTiming(8);c.loadProms(image);c.reset(false);
        c.driveCpuBus({true,false},{true,false});
        c.risingClock(requests,{0,15});c.risingClock(requests,{0,15});
        require((c.control.known&0xff00)==0xff00 && (c.control.value&0xff00)==0,"active request/read reach A3/A2 as electrical low levels");
        c.driveCpuBus({true,false},{true,true});c.risingClock(requests,{0,15});
        require((c.control.value&0xff00)==0,"changed CPU read cannot bypass request pipeline");
        c.risingClock(requests,{0,15});
        require((c.control.value&0xff00)==0x0400,"inactive read reaches upper A2 independently of active request A3");
        // A controlled image encodes old state/captured request and upper
        // feedback, allowing direct checks of simultaneous source sampling.
        image.upper.fill(0x40);
        for(unsigned i=0;i<512;++i)image.lower[i]=std::uint8_t((i>>5)&7);
        c=eii::ControllerTiming(8);c.loadProms(image);c.reset(false);
        for(unsigned n=0;n<40;++n) {
            c.risingClock(requests,{0,15});
            require((c.control.known&0xff00)==0xff00 && (c.control.value&0xff00)==0x4000,"constant upper PROM resolves unknown startup feedback");
            require((c.control.known&255)==255 && (c.control.value&255)==(n&7),"PROM sees old counter before same-edge increment");
        }
        // Force one known feedback state, then compare simultaneous lookup
        // and both latched bytes against the existing independent kernel.
        for(unsigned i=0;i<512;++i)image.upper[i]=((i>>4)&7)==0?0x40:std::uint8_t((((i>>7)+1)&3)<<6);
        c=eii::ControllerTiming(8);c.loadProms(image);c.reset(false);
        eii::PromLatch reference=image;reference.latched=0;
        for(unsigned n=0;n<160;++n) {
            eii::PromInputs in;in.counter=n&7;in.channel3=((n/8)%9)==8;
            const auto expected=reference.clock(in);c.risingClock(requests,{0,15});
            require(c.control.known==65535 && c.control.value==expected,"clocked engine equals direct old-feedback lookup for both PROMs over scan wrap");
        }
        // Feedback alternates known UCD15/14 codes; compare against direct
        // PromLatch after a first instruction forces a known feedback state.
        for(unsigned i=0;i<512;++i)image.upper[i]=std::uint8_t((((i>>7)+1)&3)<<6);
        c=eii::ControllerTiming(8);c.loadProms(image);c.reset(false);
        c.risingClock(requests,{0,15});
        require(!(c.control.known&0xc000),"unknown feedback remains unknown under a permutation, never forced to zero");
        eii::ControllerTiming absent(8);absent.reset(false);absent.risingClock(requests);
        require(absent.control.known==0 && absent.promClocks==0,"no PROM clock claims without image");
        eii::ChannelTimers bank;bank.controllerTiming=eii::ControllerTiming(8);
        image.upper.fill(1);image.lower.fill(0);bank.controllerTiming.loadProms(image);bank.enableControllerTiming();
        bank.advanceHalfClocks(1);
        require(!bank.serviceBus.acknowledgeEnable.known,"first PROM word cannot bypass the UCD08.2 pipeline");
        bank.advanceHalfClocks(2);
        require(bank.serviceBus.acknowledgeEnable.known && bank.serviceBus.acknowledgeEnable.high,"OLD PROM UCD08 drives acknowledge enable through IC152 on next rising edge");
        require(bank.channels[7].request.known && !bank.channels[7].request.high,"CHSL0 routes PROM ACK through rotated decoder to channel7");
        eii::ControllerTiming lowerOnly(8);lowerOnly.loadProms(image,false,true);lowerOnly.reset(false);lowerOnly.risingClock(requests);
        require((lowerOnly.control.known&0xff00)==0 && (lowerOnly.control.known&255)==255,"one missing chip remains unknown independently");
        std::cout<<"PASS: all 19683 ternary PROM inputs, missing/reloaded images, old-state sampling/feedback uncertainty, request-completion/wait FFs, pipeline capture and PROM-to-channel ACK; WAIT/DRAM service still observational\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
