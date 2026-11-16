#include "machine.h"
#include <iostream>
#include <memory>
#include <vector>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void program(eii::DiskDma& d,unsigned address,unsigned length){
    for(auto v:{0xc3,0x79,int(address&255),int(address>>8),int(length&255),int(length>>8),0x14,0x28,0x80,0x95,0x38,0x12,0xfc,0x92,0xcf,0xab,0x87})d.write(std::uint8_t(v));
}
static void finish(eii::DiskDma& d,std::vector<std::pair<unsigned,unsigned>>& writes){
    unsigned guard=0;bool writing=false;
    while(d.enabled||d.granted){
        require(++guard<700000,"bounded DMA operation");
        if(d.request(false))d.grant();
        const auto pins=d.pins();
        if((pins&(Z80_MREQ|Z80_WR))==(Z80_MREQ|Z80_WR)){
            if(!writing)writes.push_back({Z80_GET_ADDR(pins),Z80_GET_DATA(pins)});
            writing=true;
        }else writing=false;
        d.tick(false,std::uint8_t(0x31+d.transfers));
    }
}
static eii::PromLatch serviceProm(){
    eii::PromLatch p;p.lower.fill(64);
    for(unsigned a=0;a<512;++a){const unsigned phase=(a>>4)&7;unsigned ctl=0;
        if(!(a&8)&&phase<=2){ctl=0x4000;if(phase>=1)ctl|=0x1800;if(phase==2)ctl|=0x200;if((a&4)&&phase==2)ctl|=0x400;}
        p.upper[a]=std::uint8_t(ctl>>8);
    }return p;
}
int main(){try{
    eii::DiskDma d;program(d,0x200f,3);
    require(d.supported()&&d.request(false)&&!d.request(true),"firmware-format follow bytes, active-low Ready and byte-mode request");
    std::vector<std::pair<unsigned,unsigned>> writes;finish(d,writes);
    require(writes.size()==4&&d.transfers==4&&d.grants==4&&d.count==3,"sequential lengthN transfersN+1 bytes with one grant each");
    for(unsigned i=0;i<4;++i)require(writes[i]==std::pair<unsigned,unsigned>{0x200f+i,0x31+i},"real read/write cycles and incrementing destination");
    require(d.address[0]==0x2012&&d.address[1]==0x38,"Table11 destination last address/source fixed counter");
    require(d.requestLine(),"EOB interrupt pending only after final write");
    auto pins=d.daisy(Z80_IEIO|Z80_M1|Z80_IORQ);require(Z80_GET_DATA(pins)==0xfc&&d.acks==1&&d.servicing&&!d.requestLine(),"DMA vectorFC, acknowledge and service block");
    pins=d.daisy(Z80_IEIO|Z80_RETI);require((pins&Z80_IEIO)&&!(pins&Z80_RETI)&&!d.servicing,"DMA consumes ownRETI");
    d.write(0xd3);d.write(0x87);writes.clear();finish(d,writes);
    require(writes.front().first==0x2013&&writes.back().first==0x2016,"Continue reloads count and advances retained destination");
    eii::DiskDma zero;program(zero,0xfffe,0);writes.clear();finish(zero,writes);
    require(zero.transfers==65537&&zero.count==0&&writes[2].first==0&&writes.back().first==0xfffe,"zero-length special65537 transfer count and16-bit address wrap");
    // The loaded OS uses WR4=BD for continuous memory copies. Unlike byte
    // transfers these must retain the CPU bus for the whole block.
    eii::DiskDma copy;
    for(auto v:{0xc3,0x7d,0x00,0x30,0x0f,0x00,0x14,0x10,0xbd,0x00,0x40,0x12,0xfc,0x92,0xcf,0xab,0xb3,0x87})copy.write(std::uint8_t(v));
    require(copy.supported()&&copy.request(true),"continuous memory copy accepts OS WR4 and forced Ready");
    std::array<std::uint8_t,65536> ram{};for(unsigned i=0;i<16;++i)ram[0x3000+i]=std::uint8_t(0x91+i);
    copy.grant();unsigned cycles=0,seen=0;bool writeActive=false;
    while(copy.granted){
        require(++cycles<300,"continuous copy completes without another grant");
        auto pins=copy.pins();const auto a=Z80_GET_ADDR(pins);
        if((pins&(Z80_MREQ|Z80_WR))==(Z80_MREQ|Z80_WR)){
            if(!writeActive){ram[a]=Z80_GET_DATA(pins);++seen;}writeActive=true;
        }else writeActive=false;
        if((pins&Z80_MREQ)&&cycles%13==0){copy.tick(true,0);require(copy.pins()==pins,"continuous WAIT preserves bus pins");}
        copy.tick(false,ram[a]);
    }
    for(unsigned i=0;i<16;++i)require(ram[0x4000+i]==ram[0x3000+i],"continuous transfer preserves actual source data");
    require(copy.transfers==16&&copy.grants==1&&seen==16&&copy.count==15&&copy.endOfBlock&&copy.requestLine(),"continuous bus held until EOB with one completion interrupt");
    eii::DiskDma reverse;
    for(auto v:{0xc3,0x79,0x0f,0x50,0x0f,0x00,0x04,0x00,0xbd,0x0f,0x30,0x12,0xfc,0x92,0xcf,0xab,0xb3,0x87})reverse.write(std::uint8_t(v));
    require(reverse.supported(),"continuous B-to-A decrementing memory copy supported");
    reverse.grant();cycles=0;
    while(reverse.granted){require(++cycles<300,"reverse continuous copy bounded");auto pins=reverse.pins();auto a=Z80_GET_ADDR(pins);if((pins&(Z80_MREQ|Z80_WR))==(Z80_MREQ|Z80_WR))ram[a]=Z80_GET_DATA(pins);reverse.tick(false,ram[a]);}
    for(unsigned i=0;i<16;++i)require(ram[0x5000+i]==ram[0x3000+i],"continuous reversed ports and descending addresses preserve data");
    require(reverse.grants==1&&reverse.transfers==16&&reverse.address[0]==0x5000&&reverse.address[1]==0x2fff,"reverse source/destination counters follow existing load rule");
    eii::DiskDma ioContinuous;program(ioContinuous,0x2000,1);ioContinuous.write(0xb5);
    require(!ioContinuous.supported()&&!ioContinuous.request(false),"continuous I/O remains explicitly unsupported");
    eii::DiskDma unsupported;program(unsupported,0x2000,1);unsupported.write(0xd5); // WR4 burst mode
    require(!unsupported.supported()&&!unsupported.request(false),"unsupported DMA mode cannot fake transfers");
    eii::DiskDma stalled;program(stalled,0x2000,1);stalled.grant();stalled.tick(false,0);auto held=stalled.pins();
    for(unsigned i=0;i<17;++i){stalled.tick(true,0);require(stalled.pins()==held&&stalled.transfers==0,"WAIT holds source pins/counter without a transfer");}
    require(stalled.holdClocks==17,"WAIT clocks observable");
    // All8 DMA-bank destinations use controller RAS/CAS/WAIT consensus;
    // CPU remains mapped to bank0 and executes a real IM2 EOB handler.
    for(unsigned bank=0;bank<8;++bank) {
        auto m=std::make_unique<eii::Machine>();m->scanner.rom[0]=0; // scanner keeps executing during CPU grant
        m->channelTimers.controllerTiming.loadProms(serviceProm());m->enableCpuDram();
        m->pageReset=false;m->pages[0]=std::uint8_t(bank*2);m->pages[2]=0;m->pages[3]=0;
        const std::uint8_t code[]={0xf3,0x31,0xf0,0x27,0x3e,0x20,0xed,0x47,0xed,0x5e,0xfb,0,0x76,0x18,0xfd};
        std::copy(std::begin(code),std::end(code),m->main.rom.begin());
        const std::uint8_t isr[]={0xf5,0x3e,0x8b,0xd3,0x20,0x3a,0x00,0x21,0x3c,0x32,0x00,0x21,0xf1,0xfb,0xed,0x4d};
        std::copy(std::begin(isr),std::end(isr),m->main.rom.begin()+0x100);
        m->memory[0x20fc]=0;m->memory[0x20fd]=1;
        const auto set=[&](unsigned r,unsigned v){m->output(m->main,0x39,std::uint8_t(r));m->output(m->main,0x39,std::uint8_t(v));};
        set(4,0x10);set(5,0x64);set(6,0xfa);set(7,0x96);set(3,0xd9);set(1,0xe0);
        program(m->diskDma,0x5234,3);m->run(1000);
        std::uint64_t heldTicks=0;
        const auto run=[&](unsigned ticks){while(ticks--){const auto pc=m->main.cpu.pc;const bool grant=m->diskDma.granted;m->run(1);if(grant&&m->diskDma.granted){require(m->main.cpu.pc==pc,"CPU instruction state frozen under DMA ownership");++heldTicks;}}};
        for(unsigned value:{0xfau,0x96u,0x31u,0x32u,0x33u,0x34u})for(unsigned bit=0;bit<8;++bit){m->diskSio.rxClock((value>>bit)&1);run(64);}
        for(unsigned i=0;i<4;++i){m->diskSio.rxClock(0);run(64);}run(1000);
        const auto physical=bank*65536+0x5234;
        for(unsigned i=0;i<4;++i)require(m->memory[physical+i]==0x31+i&&m->dramKnown[physical+i]==255,"DMA commits all4known bytes to its own selected DRAM bank");
        require(m->diskDma.transfers==4&&m->diskDma.acks==1&&m->memory[0x2100]==1&&!m->diskDma.servicing,"realZ80 IM2 DMA completion ISR andRETI once");
        require(m->dmaDramWrites==4&&m->dmaDramWaitClocks>0&&m->dramWriteCycles==0&&heldTicks>20,"strict controller DMA writes/waits counted separately from CPU");
        require(m->scanner.cpu.pc>100&&m->channelTimers.controllerTiming.risingEdges>1000,"scanner and controller clocks continue during grants");
        // Continuous DRAM -> DRAM uses the same strict controller service,
        // while the CPU remains frozen and the scanner keeps running.
        m->diskDma=eii::DiskDma{};
        for(unsigned i=0;i<16;++i){m->memory[bank*65536+0x6000+i]=std::uint8_t(0x71+i);m->dramKnown[bank*65536+0x6000+i]=255;}
        for(auto v:{0xc3,0x7d,0x00,0x60,0x0f,0x00,0x14,0x10,0xbd,0x00,0x70,0x12,0xfc,0x92,0xcf,0xab,0xb3,0x87})m->diskDma.write(std::uint8_t(v));
        const auto scanPc=m->scanner.cpu.pc;const auto readBefore=m->dmaDramReads,writeBefore=m->dmaDramWrites;
        heldTicks=0;run(10000);
        for(unsigned i=0;i<16;++i)require(m->memory[bank*65536+0x7000+i]==std::uint8_t(0x71+i)&&m->dramKnown[bank*65536+0x7000+i]==255,"continuous DRAM copy services both source and destination in DMA bank");
        require(m->dmaDramReads-readBefore==16&&m->dmaDramWrites-writeBefore==16&&heldTicks>100,"continuous read/write bytes use controller WAIT service");
        require(m->diskDma.grants==1&&m->diskDma.transfers==16&&m->diskDma.acks==1&&!m->diskDma.servicing&&m->memory[0x2100]==2,"continuous transfer holds one grant then realZ80 EOB ISR returns");
        require(m->scanner.cpu.pc!=scanPc,"scanner runs during continuous DMA ownership");
    }
    std::cout<<"PASS: Z80DMA byte and continuous memory transfers, register follows/Ready, N+1/zero65537/count/address wrap/Continue, pins/WAIT/EOB/vector/RETI; realZ80 CPU bus ownership and IM2 ISR, strict DMA DRAM reads/writes through all8 banks/16 startup paths; no direct block copy.\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
