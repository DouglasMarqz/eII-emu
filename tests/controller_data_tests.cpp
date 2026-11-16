#include "machine.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    // Enumerate all ternary enable/data states for three one-bit drivers.
    // Independent concrete enumeration treats floating/contention as unknown.
    for(unsigned pattern=0;pattern<729;++pattern){
        unsigned n=pattern;std::array<eii::RfdDriver,3> drivers;unsigned ek=0,ev=0,dk=0,dv=0;
        for(unsigned i=0;i<3;++i){unsigned e=n%3;n/=3;unsigned d=n%3;n/=3;
            drivers[i]={{d==2?1u:0u,d?1u:0u},{e!=0,e==2}};
            if(e){ek|=1u<<i;if(e==2)ev|=1u<<i;}if(d){dk|=1u<<i;if(d==2)dv|=1u<<i;}
        }
        unsigned outcomes=0;
        for(unsigned en=0;en<8;++en)if(!((en^ev)&ek))for(unsigned dat=0;dat<8;++dat)if(!((dat^dv)&dk)){
            if(!en){outcomes|=3;continue;}
            bool low=false,high=false;
            for(unsigned b=0;b<3;++b)if(en&(1u<<b)){if(dat&(1u<<b))high=true;else low=true;}
            outcomes|=(low?1:0)|(high?2:0);
        }
        const auto bus=eii::resolveRfdBus(drivers);
        require(bool(bus.known&1)==(outcomes==1 || outcomes==2),"729 bus enable/data combinations vs independent enumeration");
        if(bus.known&1)require(bool(bus.value&1)==(outcomes==2),"bus known value polarity");
    }
    const auto mixed=eii::resolveRfdBus({eii::RfdDriver{{0x123456,0xffffff},{true,true}},
        eii::RfdDriver{{0x123457,0xffffff},{true,true}},eii::RfdDriver{{}, {true,false}}});
    require(mixed.known==0xfffffe,"contention loses only disagreeing bits");
    eii::ControllerDataPipeline pipe;
    require(pipe.low20.known==0 && pipe.flags.known==0 && !pipe.ucd4b.known,"pipeline starts unknown");
    for(unsigned bit=0;bit<24;++bit){
        pipe.clock({1u<<bit,0xffffff},{true,true},{true,false});
        require(pipe.low20.value==((1u<<bit)&0xfffff) && pipe.low20.known==0xfffff,"RFD0..19 exact pipeline lanes");
        const unsigned flags=bit==22?1:bit==21?2:bit==20?4:0;
        require(pipe.flags.value==flags && pipe.flags.known==7,"IC141 RFD22/21/20 lane order");
        auto input=eii::inputFlags({1u<<bit,0xffffff});
        require(input.value==(bit>=20?1u<<(23-bit):0) && input.known==15,"IC140 reverse flag order");
    }
    pipe.clock({0x563412,0x5abcde},{},{true,true});
    require(pipe.low20.known==0xabcde && pipe.flags.known==5 && !pipe.ucd4b.known && pipe.ucd9.high,"partial-known pipeline masks and controls");
    eii::PromLatch rom;
    for(unsigned a=0;a<512;++a){rom.lower[a]=65|((a&256)?4:0)|((a&32)?16:0);rom.upper[a]=(a&16)?2:0;}
    std::array<eii::LogicLevel,8> requests;requests.fill({true,false});
    eii::ControllerTiming c(8);c.loadProms(rom);c.enableRegisterWrites();c.reset(false);
    eii::CpuRegisterStaging staged;staged.write(0x48,5);staged.write(0x50,0x12);staged.write(0x58,0x34);staged.write(0x60,0x56);c.cpuRegisterWrite(staged,true);
    bool readSeen=false,flagSeen=false,commitSeen=false,rawToggleSeen=false;
    for(unsigned n=0;n<240;++n){
        auto oldControl=c.control;auto oldCh=c.channel;auto oldShift=c.shift;
        const auto before=c.registerValue(5);
        c.fallingClock();c.risingClock(requests);
        const auto data=c.dataPipelineValue(),bus=c.sampledRfdValue();
        require(data.known==(bus.known&0xfffff) && !((data.value^bus.value)&data.known),"pipeline samples pre-edge RFD bus");
        const auto four=c.delayedUcd4();
        require(four.known==bool(c.control.known&16) && (!four.known || four.high==bool(c.control.value&16)),"UCD04.1A and .1B sample same RAW PROM bit");
        rawToggleSeen|=(oldControl.known&c.control.known&16) && ((oldControl.value^c.control.value)&16);
        if(oldCh.value==1 && data.known==0xfffff){readSeen=true;require(data.value==0x63412 && c.delayedFlagValue().value==5,"SRAM read feeds both data and IC141 flag latches");}
        if(oldCh.value==1 && (oldShift.known&oldShift.value&1) && c.captured.known==31){flagSeen=true;require(c.captured.value==20,"RFD23..20 feed gated IC140 flags");}
        const auto after=c.registerValue(5);
        if(before.known!=0xffffff && after.known==0xffffff){commitSeen=true;require(data.known==0xfffff && data.value==0x63412,"CPU bus is sampled on write-closing edge before SRAM output enables");}
    }
    require(readSeen && flagSeen && commitSeen && rawToggleSeen,"connected SRAM/pipeline/control/flag observations reached");
    eii::ControllerTiming envelope;envelope.loadProms(rom);envelope.enableRegisterWrites();envelope.reset(false);
    std::array<eii::ControllerTiming,16> concrete;
    for(unsigned i=0;i<16;++i){concrete[i]=eii::ControllerTiming(i);concrete[i].loadProms(rom);concrete[i].enableRegisterWrites();concrete[i].reset(false);}
    for(unsigned n=0;n<180;++n){
        if(n==17){envelope.cpuRegisterWrite(staged,true);for(auto& x:concrete)x.cpuRegisterWrite(staged,true);}
        envelope.fallingClock();envelope.risingClock(requests);
        for(auto& x:concrete){x.fallingClock();x.risingClock(requests);}
        auto expected=concrete[0].dataPipelineValue();
        for(unsigned i=1;i<16;++i)expected=eii::merge24(expected,concrete[i].dataPipelineValue());
        const auto got=envelope.dataPipelineValue();
        require(got.known==expected.known && !((got.value^expected.value)&got.known),"per-path pipeline equals16 concrete startup phases");
    }
    auto retained=c.dataPipelineValue();c.reset(true);
    require(c.dataPipelineValue().known==retained.known && c.dataPipelineValue().value==retained.value,"CRST does not asynchronously clear data pipeline");
    std::cout<<"PASS: all729 ternary bus combinations vs concrete enumeration, contention/floating unknowns, RFD20-bit and flag lanes/masks, raw PROM UCD04 capture, connected CPU write/SRAM read/pipeline/IC140 flags and reset retention; ALU absent\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
