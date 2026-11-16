#include "machine.h"
#include <iostream>
#include <memory>
#include <vector>

// Passive CRC audit of the first header/data pair, not a receiver override.
// Independent MSB-first 8005 long division is reflected only for comparison
// with the receiver's LSB-first state. The 20-clock observation delay is the
// documented RxD-to-completed-CRC delay, not a fitted firmware delay.
static unsigned step(unsigned value,bool bit){return ((value<<1)^(((value>>15)^unsigned(bit))&1?0x8005u:0u))&65535;}
static unsigned reflected(unsigned value){unsigned r=0;for(unsigned i=0;i<16;++i)r=(r<<1)|((value>>i)&1);return r;}
struct Frame {
    std::uint64_t syncEdge=0,syncClock=0,checked=0,mismatches=0,crcBits=0;
    unsigned seed=0,retained=0,zero=0,influence=0;
    unsigned assembled=0,rawBytes=0,rawMatching=0;
    bool eligible=true;
    std::vector<bool> bits;
};
int main(int argc,char** argv){try{
    if(argc!=7){std::cerr<<"usage: sio_crc_probe main scanner prom135 prom137 image.emuiifd report2010|hxc2026\n";return 2;}
    const std::string profile=argv[6];
    if(profile!="report2010"&&profile!="hxc2026")throw std::invalid_argument("explicit FM profile required");
    auto image=std::make_shared<const eii::EmuiiImage>(eii::readFile(argv[5]));
    auto m=std::make_unique<eii::Machine>();m->load(argv[1],argv[2],argv[3],argv[4]);
    m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,image,profile=="report2010"?eii::EmuiiGap::Esynthesist2010:eii::EmuiiGap::Hxc2026);
    std::vector<Frame> frames;unsigned matches=0,resetCommands=0,huntCommands=0,rr1Reads=0,pointer=0;
    std::uint64_t channelReset=0;bool active=false;
    std::cout<<"Passive SIO CRC audit: main2.1 fixture; drive0cyl37/phase400000; LS22187/readpulse8; profile="<<profile<<"\n";
    m->diskRead->observeReceive=[&](std::uint64_t edge,bool input,const eii::DiskSio& sio){
        const auto& c=sio.channels[0];const auto& r=c.rx;
        if(sio.resets!=channelReset){channelReset=sio.resets;matches=0;active=false;}
        if(r.matches!=matches){matches=unsigned(r.matches);active=false;
            if(frames.size()<2){Frame f;f.syncEdge=edge;f.syncClock=r.clocks;f.seed=r.crc;f.retained=f.influence=reflected(r.crc);frames.push_back(f);active=true;}
        }
        if(!active||r.hunt)return;
        auto& f=frames.back();const auto k=r.clocks-f.syncClock;
        f.eligible&=(c.wr[4]&0xfd)==0x10&&(c.wr[3]&0xc9)==0xc9&&(c.wr[5]&4)&&r.crcKnown;
        f.bits.push_back(input);
        f.assembled|=unsigned(input)<<(k%8);
        if(k%8==7){if(frames.size()==2&&f.rawBytes<3584)f.rawMatching+=f.assembled==image->payload(0,0)[f.rawBytes];++f.rawBytes;f.assembled=0;}
        if(k>=20){const bool b=f.bits[std::size_t(k-20)];f.retained=step(f.retained,b);f.zero=step(f.zero,b);f.influence=step(f.influence,false);++f.crcBits;
            if(f.eligible){++f.checked;f.mismatches+=r.crc!=reflected(f.retained);f.mismatches+=(f.retained^f.zero)!=f.influence;}
        }
    };
    m->trace=[&](bool scan,bool write,std::uint16_t port,std::uint8_t value,std::uint16_t pc){
        if(scan||(port&0xfb)!=0x39)return; // channelA control aliases only
        const auto oldPointer=pointer;pointer=m->diskSio.channels[0].pointer;
        if(write&&!oldPointer&&(value&0xc0)==0x40)++resetCommands;
        if(write&&oldPointer==3&&(value&16)){++huntCommands;active=false;
            std::cout<<"Hunt tick="<<m->ticks<<" pc="<<std::hex<<pc<<" crc="<<m->diskSio.channels[0].rx.crc<<std::dec<<" explicitRxCrcResets="<<resetCommands<<"\n";}
        if(!write&&oldPointer==1&&rr1Reads++<2){
            std::cout<<"RR1 tick="<<m->ticks<<" pc="<<std::hex<<pc<<" value="<<unsigned(value)<<std::dec;
            if(!frames.empty()){const auto& f=frames.back();std::cout<<" frame="<<frames.size()<<" rawBytes="<<f.rawBytes<<" crcBits="<<f.crcBits<<" actualCRC="<<m->diskSio.channels[0].rx.crc<<" retainedShadow="<<reflected(f.retained)<<" zeroSeedShadow="<<reflected(f.zero)<<" seedInfluence="<<reflected(f.influence)<<" eligible="<<f.eligible;}
            std::cout<<"\n";
        }
    };
    m->run(3600000);
    bool valid=frames.size()==2&&rr1Reads>=2;
    for(unsigned i=0;i<frames.size();++i){const auto& f=frames[i];
        std::cout<<"Frame="<<i+1<<" syncEdge="<<f.syncEdge<<" syncClock="<<f.syncClock<<" seed="<<f.seed<<" checkedBits="<<f.checked<<" mismatches="<<f.mismatches<<" eligible="<<f.eligible<<" rawPayloadMatching="<<f.rawMatching<<"\n";
        valid&=f.eligible&&f.checked>0&&!f.mismatches;
        if(i==1)valid&=f.rawMatching==3584;
    }
    unsigned known=0,equal=0;const auto payload=image->payload(0,0);
    for(unsigned i=0;i<payload.size();++i){known+=m->dramKnown[0x2800+i]==255;equal+=m->dramKnown[0x2800+i]==255&&m->memory[0x2800+i]==payload[i];}
    valid&=known==3584&&equal==3584;
    std::cout<<"DMA transfers="<<m->diskDma.transfers<<" strictDRAMwrites="<<m->dmaDramWrites<<" RAMknown="<<known<<" RAMequal="<<equal<<" huntCommands="<<huntCommands<<" explicitRxCrcResets="<<resetCommands<<"\n";
    std::cout<<"LCD=["<<m->lcd.line(0)<<"]["<<m->lcd.line(1)<<"] CPU DRAM W/R/WAIT="<<m->dramWriteCycles<<"/"<<m->dramReadCycles<<"/"<<m->dramWaitClocks<<"\n";
    std::cout<<(valid?"PASS":"FAIL")<<": passive pin-stream/MSB polynomial and seed-influence audit. Zero-seed shadow is a counterfactual comparison, never connected to RR1/DMA/RAM. No silicon-reset proof or accepted boot.\n";
    return valid?0:1;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
