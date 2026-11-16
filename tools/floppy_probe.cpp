#include "machine.h"
#include <fstream>
#include <iostream>
#include <memory>
int main(int argc,char** argv){try{
    if(argc!=8&&argc!=10){std::cerr<<"usage: floppy_probe main scanner prom135 prom137 cycles initial-cylinder trace.csv [image.emuiifd report2010|hxc2026]\n";return 2;}
    const auto number=[](const char* text){std::string s(text);std::size_t n=0;if(s.empty())throw std::invalid_argument("unsigned number required");for(char c:s)if(c<'0'||c>'9')throw std::invalid_argument("unsigned decimal digits required");auto v=std::stoull(s,&n);if(n!=s.size())throw std::invalid_argument("invalid number");return v;};
    const auto cycles=number(argv[5]),cylinder=number(argv[6]);if(cylinder>=80)throw std::invalid_argument("cylinder0..79 required");
    const std::string profile=argc==10?argv[9]:"";
    if(argc==10&&profile!="report2010"&&profile!="hxc2026")throw std::invalid_argument("explicit image profile required");
    auto m=std::make_unique<eii::Machine>();m->load(argv[1],argv[2],argv[3],argv[4]);
    m->floppy.attach(0,unsigned(cylinder),400000);m->floppy.drives[0]->insertSensorDisk(true);
    std::shared_ptr<const eii::EmuiiImage> image;
    if(argc==10){image=std::make_shared<const eii::EmuiiImage>(eii::readFile(argv[8]));m->diskRead=std::make_unique<eii::DiskReadPath>(87,8);m->diskRead->mount(0,image,profile=="report2010"?eii::EmuiiGap::Esynthesist2010:eii::EmuiiGap::Hxc2026);}
    std::ofstream csv(argv[7]);if(!csv)throw std::runtime_error("cannot create trace");
    csv<<"# SA465 reference; explicit initial cylinder="<<cylinder<<"; initial rotor phase=400000 CPU ticks; "<<(argc==10?"nominal FM image profile="+profile+"; LS22187/readpulse8 source edges":"sensor-only medium, no data")<<"\n";
    csv<<"tick,write,port,data,pc,cylinder,track00_n,index_n,ready_n,steps,index_pulses,rx_crc,rx_chars,rx_matches,rr1,dma_transfers\n";
    unsigned entries=0;
    m->trace=[&](bool scan,bool wr,std::uint16_t p,std::uint8_t v,std::uint16_t pc){
        if(scan || ((p&0xf8)!=0x20&&(p&0xf8)!=0x30&&(p&0xf8)!=0x38) || entries++>=20000)return;
        const auto& d=*m->floppy.drives[0];
        const auto& rx=m->diskSio.channels[0].rx;
        csv<<m->ticks<<","<<wr<<","<<std::hex<<p<<","<<unsigned(v)<<","<<pc<<std::dec<<","<<d.cylinder<<","<<m->floppy.trackZeroN()<<","<<m->floppy.indexN()<<","<<m->floppy.readyN()<<","<<d.steps<<","<<d.indexPulses<<","<<rx.crc<<","<<rx.characters<<","<<rx.matches<<","<<unsigned(rx.status)<<","<<m->diskDma.transfers<<"\n";
    };
    m->run(cycles);
    std::cout<<"FLOPPY EXPERIMENT: SA465 reference, drive0 initial cylinder="<<cylinder<<"/79, phase400000; "<<(argc==10?"nominal image profile="+profile+" LS22187/readpulse8 source edges":"sensor-only write-protected medium, no data")<<".\n"<<m->report();
    std::cout<<"Known-main2.1 PC counters (interpret only for that ROM): homing00d4="<<m->main.fetched[0xd4]<<" index02f3="<<m->main.fetched[0x2f3]<<" ready0311="<<m->main.fetched[0x311]<<" data setup031e="<<m->main.fetched[0x31e]<<"\n";
    if(image){unsigned matching=0,known=0;const auto payload=image->payload(0,0);for(unsigned i=0;i<payload.size();++i){known+=m->dramKnown[0x2800+i]==255;matching+=m->dramKnown[0x2800+i]==255&&m->memory[0x2800+i]==payload[i];}std::cout<<"Known-main2.1 first-sector destination fixture2800..35ff: matching-known bytes="<<matching<<"/3584 known="<<known<<"/3584 (comparison only; not an OS boot criterion).\n";}
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
