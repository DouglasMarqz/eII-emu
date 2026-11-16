#include "machine.h"
#include <iomanip>
#include <iostream>
#include <memory>

// Fixed main2.1/ZM325 laboratory harness. The Python launcher identifies all
// assets and labels any isolated source mutations. No runtime reset/status
// callback, ROM modification, sector-to-RAM copy or raw-payload output.
int main(int argc,char** argv){try{
    if(argc!=7)return 2;
    const std::string profile=argv[6];
    if(profile!="report2010"&&profile!="hxc2026")throw std::invalid_argument("explicit FM profile required");
    auto image=std::make_shared<const eii::EmuiiImage>(eii::readFile(argv[5]));
    auto m=std::make_unique<eii::Machine>();m->load(argv[1],argv[2],argv[3],argv[4]);
    m->floppy.attach(0,37,400000);m->floppy.drives[0]->insertSensorDisk(true);
    m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);
    m->diskRead->mount(0,image,profile=="report2010"?eii::EmuiiGap::Esynthesist2010:eii::EmuiiGap::Hxc2026);
    unsigned pointer=0,reads=0;
    m->trace=[&](bool scan,bool write,std::uint16_t port,std::uint8_t value,std::uint16_t pc){
        if(scan||(port&0xfb)!=0x39)return;
        const auto old=pointer;pointer=m->diskSio.channels[0].pointer;
        if(!write&&old==1&&reads++<64){
            unsigned known=0,equal=0;const auto payload=image->payload(0,0);
            for(unsigned i=0;i<payload.size();++i){known+=m->dramKnown[0x2800+i]==255;equal+=m->dramKnown[0x2800+i]==255&&m->memory[0x2800+i]==payload[i];}
            std::cout<<"CrcRead\t"<<m->ticks<<","<<pc<<","<<unsigned(value)<<","<<m->diskSio.channels[0].rx.crc<<","<<m->diskDma.transfers<<","<<known<<","<<equal<<"\n";
        }
    };
    m->run(4000000);
    std::uint64_t loaded=0;for(unsigned a=0x2800;a<0x3600;++a)loaded+=m->main.fetched[a];
    std::cout<<"LoadedCodeFetches\t"<<loaded<<"\nFinalPC\t"<<m->main.cpu.pc<<"\n";
    std::cout<<"DmaTransfers\t"<<m->diskDma.transfers<<"\nDmaDramWrites\t"<<m->dmaDramWrites<<"\nDmaWaitClocks\t"<<m->dmaDramWaitClocks<<"\n";
    std::cout<<"CpuDramReads\t"<<m->dramReadCycles<<"\nCpuDramWrites\t"<<m->dramWriteCycles<<"\nCpuWaitClocks\t"<<m->dramWaitClocks<<"\n";
    std::cout<<"PioMainToScanner\t"<<m->mainToScanTransfers<<"\n";
    for(unsigned row=0;row<2;++row){std::cout<<"Lcd"<<row<<"Hex\t"<<std::hex<<std::setfill('0');for(unsigned char c:m->lcd.line(row))std::cout<<std::setw(2)<<unsigned(c);std::cout<<std::dec<<"\n";}
    return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
