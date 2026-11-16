#include "machine.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static eii::PromLatch serviceProm(){
    eii::PromLatch p;p.lower.fill(64); // disable ALU bus, no register writes
    for(unsigned a=0;a<512;++a){unsigned phase=(a>>4)&7;unsigned ctl=0;
        if(!(a&8) && phase<=2){ctl=0x4000;if(phase>=1)ctl|=0x1800;if(phase==2)ctl|=0x200;if((a&4) && phase==2)ctl|=0x400;}
        p.upper[a]=std::uint8_t(ctl>>8);
    }return p;
}
static eii::PromLatch priorityProm(){
    auto p=serviceProm();
    for(unsigned a=0;a<512;++a){
        const unsigned phase=(a>>4)&7,feedback=a>>7;
        unsigned u=0;
        if(phase==0)u=(a&1)?128:((a&8)?0:64);
        if(phase==4)u=(a&8)?0:64;
        if(phase==1||phase==5)u=feedback==1?88:feedback==2?152:0;
        if(phase==2||phase==6)u=feedback==1?((a&4)?94:90):feedback==2?152:0;
        p.upper[a]=std::uint8_t(u);
    }return p;
}
int main(){try{
    std::vector<std::uint8_t> values(0x80000),known(0x80000);values[0x23456]=0xa7;known[0x23456]=255;
    eii::DramCpuPort port;port.cpuBus(true,0x23456,{0,255},true);
    const eii::DramBackingView view{&values,&known};
    auto rise=[&](unsigned old){port.rising({std::uint16_t(old),65535},{},view);};
    auto fall=[&](unsigned ctl,bool nine){port.falling({std::uint16_t(ctl),65535},{true,nine},view);};
    rise(0);fall(0,false);rise(0);fall(0,false);
    rise(0);port.settle({0x4000,65535},view);fall(0x4000,false);
    rise(0x4000);port.settle({0x5800,65535},view);fall(0x5800,false);
    rise(0x5800);port.settle({0x5a00,65535},view);fall(0x5a00,false);
    require(port.reads==1 && !port.readReady,"RAS row/CPU bank and CAS column read precede IC103 capture");
    rise(0x5a00);port.settle({0,65535},view);fall(0,true);
    require(!port.readReady,"falling MPGTE must not capture read data");
    rise(0);require(port.readReady && port.readLatch.known==255 && port.readLatch.value==0xa7,"delayed UCD09 MPGTE rising captures previous MDO on CAS closing edge");
    const auto prom=serviceProm();
    for(const auto& cpuProm:{prom,priorityProm()})for(unsigned bank=0;bank<8;++bank){
        auto m=std::make_unique<eii::Machine>();m->channelTimers.controllerTiming.loadProms(cpuProm);m->enableCpuDram();
        const std::uint8_t datum=0x31+bank;
        const std::uint8_t code[]={0x3e,std::uint8_t(bank*2),0xd3,0x6b,0x3e,datum,0x32,0x34,0xd2,0xaf,0x3a,0x34,0xd2,0x32,0x00,0x20,0x76};
        std::copy(std::begin(code),std::end(code),m->main.rom.begin());
        const unsigned physical=bank*65536+0x5234;m->memory[physical]=0xee;
        bool heldWrite=false,writeDeferred=false,waitSeen=false;
        for(unsigned tick=0;tick<2000;++tick){m->run(1);
            if(m->dramBusActive && (m->main.pins&Z80_WR) && !m->dramServiced){heldWrite=true;writeDeferred|=m->memory[physical]==0xee;}
            waitSeen|=bool(m->main.pins&Z80_WAIT);
        }
        require(heldWrite && writeDeferred && waitSeen && m->dramWaitClocks>0,"CPU write waits without direct memory side effect");
        if(m->memory[physical]!=datum || m->dramKnown[physical]!=255)std::cerr<<"bank="<<bank<<" memory="<<unsigned(m->memory[physical])<<" known="<<unsigned(m->dramKnown[physical])<<" expected="<<unsigned(datum)<<"\n"<<m->report();
        require(m->memory[physical]==datum && m->dramKnown[physical]==255,"resolved CAS/WE writes selected CPU bank");
        require(m->memory[0x2000]==datum && (m->main.pins&Z80_HALT),"Z80 receives IC103 read byte then continues to local SRAM/HALT");
        require(m->dramWriteCycles==1 && m->dramReadCycles==1,"held MREQ commits one write and one read across16 phase paths");
    }
    auto local=std::make_unique<eii::Machine>();local->enableCpuDram();
    const std::uint8_t localCode[]={0x3e,0x7b,0x32,0x00,0x20,0x3a,0x00,0x20,0x76};
    std::copy(std::begin(localCode),std::end(localCode),local->main.rom.begin());local->run(500);
    require((local->main.pins&Z80_HALT) && local->memory[0x2000]==0x7b && local->dramWaitClocks==0,"local ROM/SRAM remains independent of DRAM/PROM readiness");
    auto unknown=std::make_unique<eii::Machine>();unknown->channelTimers.controllerTiming.loadProms(prom);unknown->enableCpuDram();
    const std::uint8_t readUnknown[]={0x3a,0x00,0x40,0x32,0x00,0x20,0x76};
    std::copy(std::begin(readUnknown),std::end(readUnknown),unknown->main.rom.begin());unknown->run(1000);
    require(!(unknown->main.pins&Z80_HALT) && unknown->dramWaitClocks>0 && unknown->dramReadCycles==0 && unknown->dramKnown[0x4000]==0,"unknown DRAM never becomes fabricated zero or an acknowledged read");
    require(unknown->scanner.cpu.pc>10 && unknown->channelTimers.controllerTiming.risingEdges==2500,"scanner and controller clocks continue while main CPU waits");
    std::cout<<"PASS: CPU bank/row/column RAS-CAS service, delayed IC103 capture, real Z80 WAIT write/read/continue across8 banks and16 startup phases, single committed held writes, local memory bypass and unknown-data stalls; sample transport absent\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
